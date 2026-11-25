use std::{
    collections::HashMap,
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    },
};

use serde::Serialize;
use thiserror::Error;
use tokio::{sync::RwLock, task::JoinHandle};
use tracing::{debug, info};
use uuid::Uuid;

use crate::streaming::StreamManager;

#[derive(Debug, Error)]
pub enum PeerError {
    #[error("unknown session `{0}`")]
    UnknownSession(String),
    #[error("server is shutting down")]
    ShuttingDown,
    #[error("not implemented: {0}")]
    NotImplemented(&'static str),
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "kebab-case")]
pub enum PeerState {
    New,
    Negotiating,
    Connected,
    Closed,
}

#[derive(Debug, Clone, Serialize)]
pub struct SessionSummary {
    pub session_id: String,
    pub state: PeerState,
}

struct PeerSession {
    state: PeerState,
    // TODO: hold the `::webrtc` RTCPeerConnection and outgoing H.264 track here.
    /// Task forwarding frames from a `FrameSubscriber` to the video track.
    frame_task: Option<JoinHandle<()>>,
}

/// Manages independent WebRTC peer connections, one per streaming session.
pub struct PeerConnectionManager {
    streams: Arc<StreamManager>,
    ice_servers: Vec<String>,
    sessions: RwLock<HashMap<String, PeerSession>>,
    shutting_down: AtomicBool,
}

impl PeerConnectionManager {
    pub fn new(streams: Arc<StreamManager>, ice_servers: Vec<String>) -> Self {
        Self {
            streams,
            ice_servers,
            sessions: RwLock::new(HashMap::new()),
            shutting_down: AtomicBool::new(false),
        }
    }

    pub async fn create_session(&self) -> Result<String, PeerError> {
        if self.shutting_down.load(Ordering::Acquire) {
            return Err(PeerError::ShuttingDown);
        }

        let session_id = Uuid::new_v4().to_string();
        self.sessions.write().await.insert(
            session_id.clone(),
            PeerSession {
                state: PeerState::New,
                frame_task: None,
            },
        );
        info!(%session_id, "session created");
        Ok(session_id)
    }

    /// Applies the client's SDP offer and returns the server's SDP answer.
    pub async fn handle_offer(&self, session_id: &str, sdp: &str) -> Result<String, PeerError> {
        let mut sessions = self.sessions.write().await;
        let session = sessions
            .get_mut(session_id)
            .ok_or_else(|| PeerError::UnknownSession(session_id.to_owned()))?;
        session.state = PeerState::Negotiating;

        // TODO(webrtc):
        // 1. Build an RTCPeerConnection via `::webrtc::api::APIBuilder` with H.264
        //    registered and `self.ice_servers` in the RTCConfiguration.
        // 2. Add a TrackLocalStaticSample (video/H264) to the connection.
        // 3. Set the remote description from `sdp`, create and set the answer.
        // 4. Spawn a task that drains `self.streams.subscribe()` into the track
        //    and store it in `session.frame_task`.
        // 5. Track connection state changes and update `session.state`.
        let _ = (&self.streams, &self.ice_servers, sdp);
        Err(PeerError::NotImplemented("WebRTC answer generation"))
    }

    pub async fn add_ice_candidate(
        &self,
        session_id: &str,
        candidate: &str,
        sdp_mid: Option<&str>,
        sdp_mline_index: Option<u16>,
    ) -> Result<(), PeerError> {
        if !self.sessions.read().await.contains_key(session_id) {
            return Err(PeerError::UnknownSession(session_id.to_owned()));
        }
        // TODO(webrtc): forward to RTCPeerConnection::add_ice_candidate.
        debug!(%session_id, candidate, ?sdp_mid, ?sdp_mline_index, "ICE candidate received");
        Ok(())
    }

    pub async fn close_session(&self, session_id: &str) -> Result<(), PeerError> {
        let session = self
            .sessions
            .write()
            .await
            .remove(session_id)
            .ok_or_else(|| PeerError::UnknownSession(session_id.to_owned()))?;

        if let Some(task) = session.frame_task {
            task.abort();
        }
        // TODO(webrtc): close the RTCPeerConnection.
        info!(%session_id, "session closed");
        Ok(())
    }

    /// Rejects new sessions from now on.
    pub fn begin_shutdown(&self) {
        self.shutting_down.store(true, Ordering::Release);
    }

    pub async fn close_all(&self) {
        let ids: Vec<String> = self.sessions.read().await.keys().cloned().collect();
        for id in ids {
            let _ = self.close_session(&id).await;
        }
    }

    pub async fn list_sessions(&self) -> Vec<SessionSummary> {
        self.sessions
            .read()
            .await
            .iter()
            .map(|(id, session)| SessionSummary {
                session_id: id.clone(),
                state: session.state,
            })
            .collect()
    }
}
