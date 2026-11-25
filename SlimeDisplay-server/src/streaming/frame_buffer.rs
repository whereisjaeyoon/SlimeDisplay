use std::sync::Mutex;

use bytes::Bytes;

/// An encoded frame owned by Rust (copied out of the native callback).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct EncodedFrame {
    pub data: Bytes,
    /// Presentation timestamp as reported by the native encoder.
    pub pts: i64,
    pub is_keyframe: bool,
}

/// Holds the most recent keyframe so newly connected peers can start decoding.
#[derive(Debug, Default)]
pub struct KeyframeCache {
    latest: Mutex<Option<EncodedFrame>>,
}

impl KeyframeCache {
    pub fn store(&self, frame: &EncodedFrame) {
        // Cloning `Bytes` is a refcount bump, not a copy.
        *self.latest.lock().unwrap() = Some(frame.clone());
    }

    pub fn latest(&self) -> Option<EncodedFrame> {
        self.latest.lock().unwrap().clone()
    }

    pub fn clear(&self) {
        *self.latest.lock().unwrap() = None;
    }
}
