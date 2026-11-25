//! Encoded frame pipeline: native callback → bounded queue → fan-out to peers (README §5.2).

pub mod frame_buffer;
pub mod manager;

pub use frame_buffer::{EncodedFrame, KeyframeCache};
pub use manager::{FrameSubscriber, StreamManager};
