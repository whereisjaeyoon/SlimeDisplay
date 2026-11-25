use std::sync::Arc;

use tokio::sync::{
    broadcast::{self, error::RecvError},
    mpsc,
};
use tracing::warn;

use super::frame_buffer::{EncodedFrame, KeyframeCache};
use crate::display::adapter::FrameSink;

struct Shared {
    fanout: broadcast::Sender<EncodedFrame>,
    keyframes: KeyframeCache,
}

impl Shared {
    fn publish(&self, frame: EncodedFrame) {
        if frame.is_keyframe {
            self.keyframes.store(&frame);
        }
        // Errors only when there are no subscribers.
        let _ = self.fanout.send(frame);
    }
}

/// Distributes encoded frames to streaming sessions.
///
/// Both queues are bounded: the ingest queue drops frames when full (see the
/// native callback), and slow subscribers skip ahead to the next keyframe.
pub struct StreamManager {
    shared: Arc<Shared>,
    ingest_tx: mpsc::Sender<EncodedFrame>,
}

impl StreamManager {
    /// Must be called inside a Tokio runtime.
    pub fn new(capacity: usize) -> Self {
        let capacity = capacity.max(1);
        let (ingest_tx, mut ingest_rx) = mpsc::channel(capacity);
        let (fanout, _) = broadcast::channel(capacity);
        let shared = Arc::new(Shared {
            fanout,
            keyframes: KeyframeCache::default(),
        });

        let worker = shared.clone();
        tokio::spawn(async move {
            while let Some(frame) = ingest_rx.recv().await {
                worker.publish(frame);
            }
        });

        Self { shared, ingest_tx }
    }

    /// Sink to register with the display backend.
    pub fn frame_sink(&self) -> FrameSink {
        self.ingest_tx.clone()
    }

    /// Publishes a frame directly, bypassing the ingest queue.
    pub fn publish(&self, frame: EncodedFrame) {
        self.shared.publish(frame);
    }

    pub fn subscribe(&self) -> FrameSubscriber {
        // Subscribe before reading the cache so no keyframe falls between the two.
        let rx = self.shared.fanout.subscribe();
        FrameSubscriber {
            rx,
            pending: self.shared.keyframes.latest(),
            synced: false,
        }
    }

    pub fn subscriber_count(&self) -> usize {
        self.shared.fanout.receiver_count()
    }

    /// Forgets the cached keyframe, e.g. when the display is recreated.
    pub fn reset(&self) {
        self.shared.keyframes.clear();
    }
}

/// A peer's view of the frame stream. Always starts at a keyframe.
pub struct FrameSubscriber {
    rx: broadcast::Receiver<EncodedFrame>,
    pending: Option<EncodedFrame>,
    synced: bool,
}

impl FrameSubscriber {
    /// Next decodable frame, or `None` when the stream has ended.
    ///
    /// Delta frames are skipped until a keyframe arrives, both on start and
    /// after falling behind.
    // TODO: the cached keyframe may be stale relative to the deltas that follow it;
    // request an immediate keyframe once the C ABI exposes one (README §12).
    pub async fn recv(&mut self) -> Option<EncodedFrame> {
        if let Some(frame) = self.pending.take() {
            self.synced = true;
            return Some(frame);
        }

        loop {
            match self.rx.recv().await {
                Ok(frame) if self.synced || frame.is_keyframe => {
                    self.synced = true;
                    return Some(frame);
                }
                Ok(_) => continue,
                Err(RecvError::Lagged(skipped)) => {
                    warn!(skipped, "subscriber fell behind; waiting for next keyframe");
                    self.synced = false;
                }
                Err(RecvError::Closed) => return None,
            }
        }
    }
}
