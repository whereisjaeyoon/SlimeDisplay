use serde::Serialize;

use super::{
    adapter::{DisplayBackend, FrameSink},
    DisplayConfig, DisplayError, DisplayInfo,
};

/// Lifecycle state (README §5.1). Guards against invalid native calls.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
pub enum DisplayState {
    /// No native handle yet.
    Uninitialized,
    /// Native handle exists, no virtual display.
    Created,
    /// Virtual display exists and is idle.
    DisplayReady,
    /// Capture running, encoder not running (only left here if a rollback fails).
    Capturing,
    /// Capture and encoder running; frames flow to the sink.
    Encoding,
    /// Shut down; the native handle has been released.
    Stopped,
}

#[derive(Debug, Clone, Serialize)]
pub struct DisplayStatus {
    pub state: DisplayState,
    pub info: Option<DisplayInfo>,
}

/// Owns the display backend and coordinates every native operation.
pub struct DisplayService {
    backend: Option<Box<dyn DisplayBackend>>,
    state: DisplayState,
    info: Option<DisplayInfo>,
}

impl DisplayService {
    pub fn new(backend: Box<dyn DisplayBackend>) -> Self {
        Self {
            backend: Some(backend),
            state: DisplayState::Created,
            info: None,
        }
    }

    pub fn state(&self) -> DisplayState {
        self.state
    }

    pub fn status(&self) -> DisplayStatus {
        DisplayStatus {
            state: self.state,
            info: self.info,
        }
    }

    pub fn library_version(&self) -> Option<u32> {
        self.backend.as_ref().map(|b| b.version())
    }

    pub fn create_display(&mut self, config: DisplayConfig) -> Result<DisplayInfo, DisplayError> {
        self.expect("create display", &[DisplayState::Created])?;
        let backend = self.backend_mut()?;

        backend.create_virtual_display(config)?;
        let info = match backend.display_info() {
            Ok(info) => info,
            Err(e) => {
                let _ = backend.destroy_virtual_display();
                return Err(e.into());
            }
        };

        self.info = Some(info);
        self.state = DisplayState::DisplayReady;
        Ok(info)
    }

    /// Starts capture, registers the frame sink, then starts the encoder.
    /// The sink is registered first so no initial frames are lost (README §9.2).
    pub fn start_stream(&mut self, sink: FrameSink) -> Result<(), DisplayError> {
        self.expect("start stream", &[DisplayState::DisplayReady])?;
        let backend = self.backend_mut()?;

        backend.start_capture()?;
        if let Err(e) = backend.set_frame_sink(Some(sink)) {
            let _ = backend.stop_capture();
            return Err(e.into());
        }
        if let Err(e) = backend.start_encoder() {
            let _ = backend.set_frame_sink(None);
            let rolled_back = backend.stop_capture().is_ok();
            self.state = if rolled_back {
                DisplayState::DisplayReady
            } else {
                DisplayState::Capturing
            };
            return Err(e.into());
        }

        self.state = DisplayState::Encoding;
        Ok(())
    }

    /// Stops the encoder, unregisters the sink, then stops capture (README §9.3).
    pub fn stop_stream(&mut self) -> Result<(), DisplayError> {
        self.expect("stop stream", &[DisplayState::Capturing, DisplayState::Encoding])?;
        let encoding = self.state == DisplayState::Encoding;
        let backend = self.backend_mut()?;

        if encoding {
            backend.stop_encoder()?;
            backend.set_frame_sink(None)?;
        }
        backend.stop_capture()?;

        self.state = DisplayState::DisplayReady;
        Ok(())
    }

    pub fn destroy_display(&mut self) -> Result<(), DisplayError> {
        self.expect(
            "destroy display",
            &[DisplayState::DisplayReady, DisplayState::Capturing, DisplayState::Encoding],
        )?;
        if self.state != DisplayState::DisplayReady {
            self.stop_stream()?;
        }

        self.backend_mut()?.destroy_virtual_display()?;
        self.info = None;
        self.state = DisplayState::Created;
        Ok(())
    }

    /// Tears everything down and releases the native handle. Idempotent.
    pub fn shutdown(&mut self) -> Result<(), DisplayError> {
        match self.state {
            DisplayState::Stopped => return Ok(()),
            DisplayState::DisplayReady | DisplayState::Capturing | DisplayState::Encoding => {
                self.destroy_display()?;
            }
            DisplayState::Uninitialized | DisplayState::Created => {}
        }

        // Dropping the backend calls sd_destroy().
        self.backend = None;
        self.state = DisplayState::Stopped;
        Ok(())
    }

    fn expect(&self, op: &'static str, allowed: &[DisplayState]) -> Result<(), DisplayError> {
        if allowed.contains(&self.state) {
            Ok(())
        } else {
            Err(DisplayError::InvalidState {
                op,
                state: self.state,
            })
        }
    }

    fn backend_mut(&mut self) -> Result<&mut (dyn DisplayBackend + 'static), DisplayError> {
        self.backend
            .as_deref_mut()
            .ok_or(DisplayError::InvalidState {
                op: "access backend",
                state: self.state,
            })
    }
}
