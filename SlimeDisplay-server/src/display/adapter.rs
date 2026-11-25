//! Safe adapter over the SlimeDisplay C ABI.
//!
//! [`DisplayBackend`] is the boundary between the server and the native library.
//! No raw pointers cross it. [`NativeAdapter`] implements it over FFI (feature
//! `native`); [`MockAdapter`] implements it in-process for tests and development.

use std::sync::{Arc, Mutex, MutexGuard};

use tokio::sync::mpsc;

use super::{DisplayConfig, DisplayInfo};
use crate::{ffi::SdError, streaming::EncodedFrame};

/// Channel that receives encoded frames copied out of the native callback.
pub type FrameSink = mpsc::Sender<EncodedFrame>;

pub trait DisplayBackend: Send {
    fn create_virtual_display(&mut self, config: DisplayConfig) -> Result<(), SdError>;
    fn destroy_virtual_display(&mut self) -> Result<(), SdError>;
    fn display_info(&mut self) -> Result<DisplayInfo, SdError>;

    fn start_capture(&mut self) -> Result<(), SdError>;
    fn stop_capture(&mut self) -> Result<(), SdError>;

    fn start_encoder(&mut self) -> Result<(), SdError>;
    fn stop_encoder(&mut self) -> Result<(), SdError>;

    /// Registers (`Some`) or unregisters (`None`) the encoded-frame sink.
    fn set_frame_sink(&mut self, sink: Option<FrameSink>) -> Result<(), SdError>;

    fn version(&self) -> u32;
}

/// Returns the native backend when built with `native`, otherwise the mock.
pub fn default_backend() -> Result<Box<dyn DisplayBackend>, SdError> {
    #[cfg(feature = "native")]
    {
        Ok(Box::new(NativeAdapter::new()?))
    }
    #[cfg(not(feature = "native"))]
    {
        tracing::warn!("built without the `native` feature; using the mock display backend");
        Ok(Box::new(MockAdapter::default()))
    }
}

#[cfg(feature = "native")]
pub use native::NativeAdapter;

#[cfg(feature = "native")]
mod native {
    use std::{
        ffi::c_void,
        ptr::{self, NonNull},
        slice,
    };

    use bytes::Bytes;

    use super::{DisplayBackend, FrameSink};
    use crate::{
        display::{DisplayConfig, DisplayInfo},
        ffi::{check, slimedisplay as sys, SdError},
        streaming::EncodedFrame,
    };

    /// Passed to the native library as `user_data`. Boxed so its address stays stable.
    struct CallbackContext {
        sink: FrameSink,
    }

    /// Owns the native `SD_Handle` and the callback context.
    pub struct NativeAdapter {
        handle: NonNull<sys::SD_Handle>,
        callback_ctx: Option<Box<CallbackContext>>,
    }

    // SAFETY: the handle is only used through `&mut self` (DisplayService sits behind
    // a mutex), and the C ABI does not require calls to come from the creating thread.
    unsafe impl Send for NativeAdapter {}

    impl NativeAdapter {
        pub fn new() -> Result<Self, SdError> {
            let mut raw: *mut sys::SD_Handle = ptr::null_mut();
            check(unsafe { sys::sd_create(&mut raw) })?;
            let handle = NonNull::new(raw).ok_or(SdError::Initialization)?;
            Ok(Self {
                handle,
                callback_ctx: None,
            })
        }

        fn raw(&self) -> *mut sys::SD_Handle {
            self.handle.as_ptr()
        }
    }

    impl DisplayBackend for NativeAdapter {
        fn create_virtual_display(&mut self, config: DisplayConfig) -> Result<(), SdError> {
            let config = sys::SD_DisplayConfig {
                width: config.width,
                height: config.height,
                fps: config.fps,
            };
            check(unsafe { sys::sd_create_virtual_display(self.raw(), &config) })
        }

        fn destroy_virtual_display(&mut self) -> Result<(), SdError> {
            check(unsafe { sys::sd_destroy_virtual_display(self.raw()) })
        }

        fn display_info(&mut self) -> Result<DisplayInfo, SdError> {
            let mut info = sys::SD_DisplayInfo {
                id: 0,
                width: 0,
                height: 0,
                refresh_rate: 0,
            };
            check(unsafe { sys::sd_get_virtual_display_info(self.raw(), &mut info) })?;
            Ok(DisplayInfo {
                id: info.id,
                width: info.width,
                height: info.height,
                refresh_rate: info.refresh_rate,
            })
        }

        fn start_capture(&mut self) -> Result<(), SdError> {
            check(unsafe { sys::sd_start_capture(self.raw()) })
        }

        fn stop_capture(&mut self) -> Result<(), SdError> {
            check(unsafe { sys::sd_stop_capture(self.raw()) })
        }

        fn start_encoder(&mut self) -> Result<(), SdError> {
            check(unsafe { sys::sd_start_encoder(self.raw()) })
        }

        fn stop_encoder(&mut self) -> Result<(), SdError> {
            check(unsafe { sys::sd_stop_encoder(self.raw()) })
        }

        fn set_frame_sink(&mut self, sink: Option<FrameSink>) -> Result<(), SdError> {
            // Both branches rely on the native library guaranteeing that, once
            // sd_set_encoded_frame_callback returns, no callback is still using the
            // previous user_data, and that NULL unregisters (README §4.7, §9.3).
            let Some(sink) = sink else {
                check(unsafe {
                    sys::sd_set_encoded_frame_callback(self.raw(), None, ptr::null_mut())
                })?;
                self.callback_ctx = None;
                return Ok(());
            };

            let ctx = Box::new(CallbackContext { sink });
            let user_data = &*ctx as *const CallbackContext as *mut c_void;
            check(unsafe {
                sys::sd_set_encoded_frame_callback(self.raw(), Some(on_encoded_frame), user_data)
            })?;
            self.callback_ctx = Some(ctx);
            Ok(())
        }

        fn version(&self) -> u32 {
            unsafe { sys::sd_get_version() }
        }
    }

    impl Drop for NativeAdapter {
        fn drop(&mut self) {
            if self.callback_ctx.is_some() {
                let _ = self.set_frame_sink(None);
            }
            unsafe { sys::sd_destroy(self.raw()) };
            // Any remaining callback_ctx is dropped after sd_destroy, so it outlives the handle.
        }
    }

    /// Runs on the native encoder thread: copy, forward, return. Never block here.
    unsafe extern "C" fn on_encoded_frame(
        data: *const u8,
        size: u32,
        presentation_time: i64,
        is_keyframe: i32,
        user_data: *mut c_void,
    ) {
        if data.is_null() || user_data.is_null() {
            return;
        }
        // SAFETY: user_data is the CallbackContext owned by NativeAdapter, which stays
        // alive until the callback is unregistered.
        let ctx = unsafe { &*(user_data as *const CallbackContext) };
        // SAFETY: data is borrowed for `size` bytes only for this call; copy it out.
        let data = Bytes::copy_from_slice(unsafe { slice::from_raw_parts(data, size as usize) });

        // Drop the frame rather than block when the pipeline is full (low latency).
        let _ = ctx.sink.try_send(EncodedFrame {
            data,
            pts: presentation_time,
            is_keyframe: is_keyframe != 0,
        });
    }
}

#[derive(Debug, Default)]
struct MockState {
    calls: Vec<&'static str>,
    config: Option<DisplayConfig>,
    sink: Option<FrameSink>,
}

/// In-process backend that records calls. Clones share state, so tests can keep
/// a handle after boxing one into a `DisplayService`.
#[derive(Debug, Clone, Default)]
pub struct MockAdapter {
    state: Arc<Mutex<MockState>>,
}

impl MockAdapter {
    /// Backend calls made so far, in order.
    pub fn calls(&self) -> Vec<&'static str> {
        self.state.lock().unwrap().calls.clone()
    }

    /// Simulates the native encoder producing a frame. Returns false if no sink
    /// is registered or the sink is full.
    pub fn emit(&self, frame: EncodedFrame) -> bool {
        self.state
            .lock()
            .unwrap()
            .sink
            .as_ref()
            .is_some_and(|sink| sink.try_send(frame).is_ok())
    }

    fn record(&self, call: &'static str) -> MutexGuard<'_, MockState> {
        let mut state = self.state.lock().unwrap();
        state.calls.push(call);
        state
    }
}

impl DisplayBackend for MockAdapter {
    fn create_virtual_display(&mut self, config: DisplayConfig) -> Result<(), SdError> {
        self.record("create_virtual_display").config = Some(config);
        Ok(())
    }

    fn destroy_virtual_display(&mut self) -> Result<(), SdError> {
        self.record("destroy_virtual_display").config = None;
        Ok(())
    }

    fn display_info(&mut self) -> Result<DisplayInfo, SdError> {
        let config = self.record("display_info").config.ok_or(SdError::InvalidState)?;
        Ok(DisplayInfo {
            id: 1,
            width: config.width,
            height: config.height,
            refresh_rate: config.fps,
        })
    }

    fn start_capture(&mut self) -> Result<(), SdError> {
        self.record("start_capture");
        Ok(())
    }

    fn stop_capture(&mut self) -> Result<(), SdError> {
        self.record("stop_capture");
        Ok(())
    }

    fn start_encoder(&mut self) -> Result<(), SdError> {
        self.record("start_encoder");
        Ok(())
    }

    fn stop_encoder(&mut self) -> Result<(), SdError> {
        self.record("stop_encoder");
        Ok(())
    }

    fn set_frame_sink(&mut self, sink: Option<FrameSink>) -> Result<(), SdError> {
        let call = if sink.is_some() { "set_frame_sink" } else { "clear_frame_sink" };
        self.record(call).sink = sink;
        Ok(())
    }

    fn version(&self) -> u32 {
        0
    }
}
