//! WebRTC peer management and signaling (README §5.3, §5.4).
//!
//! Note: this module shadows the `webrtc` crate inside this project; refer to
//! the crate as `::webrtc`.

pub mod peer_connection;
pub mod signaling;

pub use peer_connection::{PeerConnectionManager, PeerError, PeerState, SessionSummary};
pub use signaling::{SignalMessage, SignalingConnection, SignalingError, SignalingService};
