//! HTTP control API and WebSocket signaling endpoint (README §6).

pub mod http;
pub mod websocket;

use std::sync::Arc;

use axum::{routing::get, Router};
use tokio::sync::Mutex;

use crate::{
    config::Config,
    display::DisplayService,
    security::AuthService,
    streaming::StreamManager,
    webrtc::{PeerConnectionManager, SignalingService},
};

/// Shared state handed to every handler.
#[derive(Clone)]
pub struct AppState {
    pub config: Arc<Config>,
    pub display: Arc<Mutex<DisplayService>>,
    pub streams: Arc<StreamManager>,
    pub peers: Arc<PeerConnectionManager>,
    pub signaling: Arc<SignalingService>,
    pub auth: Arc<AuthService>,
}

pub fn router(state: AppState) -> Router {
    Router::new()
        .merge(http::routes())
        .route("/ws/signaling", get(websocket::signaling_handler))
        .with_state(state)
}
