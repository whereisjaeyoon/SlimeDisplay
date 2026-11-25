use std::{collections::HashSet, sync::Arc};

use serde::{Deserialize, Serialize};
use thiserror::Error;

use super::peer_connection::{PeerConnectionManager, PeerError};

/// Signaling messages exchanged over `/ws/signaling` (README §5.4).
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "kebab-case")]
pub enum SignalMessage {
    /// Client → server: request a session. Server → client: the created session.
    SessionCreate {
        #[serde(default, skip_serializing_if = "Option::is_none")]
        session_id: Option<String>,
    },
    SessionClose {
        session_id: String,
    },
    /// Client → server. The client is the offerer; the server answers.
    Offer {
        session_id: String,
        sdp: String,
    },
    /// Server → client.
    Answer {
        session_id: String,
        sdp: String,
    },
    IceCandidate {
        session_id: String,
        candidate: String,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        sdp_mid: Option<String>,
        #[serde(default, skip_serializing_if = "Option::is_none")]
        sdp_mline_index: Option<u16>,
    },
    Error {
        #[serde(default, skip_serializing_if = "Option::is_none")]
        session_id: Option<String>,
        message: String,
    },
}

impl SignalMessage {
    pub fn error(session_id: Option<String>, message: impl Into<String>) -> Self {
        Self::Error {
            session_id,
            message: message.into(),
        }
    }

    pub fn session_id(&self) -> Option<&str> {
        match self {
            Self::SessionCreate { session_id } | Self::Error { session_id, .. } => {
                session_id.as_deref()
            }
            Self::SessionClose { session_id }
            | Self::Offer { session_id, .. }
            | Self::Answer { session_id, .. }
            | Self::IceCandidate { session_id, .. } => Some(session_id),
        }
    }
}

#[derive(Debug, Error)]
pub enum SignalingError {
    #[error("session `{0}` does not belong to this connection")]
    NotOwned(String),
    #[error("unexpected `{0}` message from client")]
    Unexpected(&'static str),
    #[error(transparent)]
    Peer(#[from] PeerError),
}

/// Per-WebSocket state: which sessions this connection owns.
#[derive(Debug, Default)]
pub struct SignalingConnection {
    owned_sessions: HashSet<String>,
}

impl SignalingConnection {
    fn authorize(&self, session_id: &str) -> Result<(), SignalingError> {
        if self.owned_sessions.contains(session_id) {
            Ok(())
        } else {
            Err(SignalingError::NotOwned(session_id.to_owned()))
        }
    }
}

/// Routes signaling messages to the PeerConnectionManager. Media never passes through here.
pub struct SignalingService {
    peers: Arc<PeerConnectionManager>,
}

impl SignalingService {
    pub fn new(peers: Arc<PeerConnectionManager>) -> Self {
        Self { peers }
    }

    /// Handles one client message and returns the reply, if any.
    pub async fn handle(
        &self,
        conn: &mut SignalingConnection,
        msg: SignalMessage,
    ) -> Option<SignalMessage> {
        let session_id = msg.session_id().map(str::to_owned);
        match self.dispatch(conn, msg).await {
            Ok(reply) => reply,
            Err(e) => Some(SignalMessage::error(session_id, e.to_string())),
        }
    }

    /// Closes every session owned by a disconnected WebSocket.
    pub async fn disconnect(&self, conn: SignalingConnection) {
        for session_id in conn.owned_sessions {
            let _ = self.peers.close_session(&session_id).await;
        }
    }

    async fn dispatch(
        &self,
        conn: &mut SignalingConnection,
        msg: SignalMessage,
    ) -> Result<Option<SignalMessage>, SignalingError> {
        match msg {
            SignalMessage::SessionCreate { .. } => {
                let session_id = self.peers.create_session().await?;
                conn.owned_sessions.insert(session_id.clone());
                Ok(Some(SignalMessage::SessionCreate {
                    session_id: Some(session_id),
                }))
            }
            SignalMessage::SessionClose { session_id } => {
                conn.authorize(&session_id)?;
                self.peers.close_session(&session_id).await?;
                conn.owned_sessions.remove(&session_id);
                Ok(None)
            }
            SignalMessage::Offer { session_id, sdp } => {
                conn.authorize(&session_id)?;
                let answer = self.peers.handle_offer(&session_id, &sdp).await?;
                Ok(Some(SignalMessage::Answer {
                    session_id,
                    sdp: answer,
                }))
            }
            SignalMessage::IceCandidate {
                session_id,
                candidate,
                sdp_mid,
                sdp_mline_index,
            } => {
                conn.authorize(&session_id)?;
                self.peers
                    .add_ice_candidate(&session_id, &candidate, sdp_mid.as_deref(), sdp_mline_index)
                    .await?;
                Ok(None)
            }
            SignalMessage::Answer { .. } => Err(SignalingError::Unexpected("answer")),
            SignalMessage::Error { .. } => Err(SignalingError::Unexpected("error")),
        }
    }
}
