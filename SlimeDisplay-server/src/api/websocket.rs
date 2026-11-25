//! Signaling WebSocket at `/ws/signaling` (README §6.2). Signaling only: video
//! is never sent over this socket.

use axum::{
    extract::{
        ws::{Message, WebSocket, WebSocketUpgrade},
        Query, State,
    },
    http::StatusCode,
    response::{IntoResponse, Response},
};
use serde::Deserialize;
use tracing::debug;

use super::AppState;
use crate::webrtc::{SignalMessage, SignalingConnection};

#[derive(Debug, Deserialize)]
pub struct SignalingParams {
    token: Option<String>,
}

/// Browsers cannot set headers on WebSocket requests, so the token comes in `?token=`.
pub async fn signaling_handler(
    ws: WebSocketUpgrade,
    Query(params): Query<SignalingParams>,
    State(state): State<AppState>,
) -> Response {
    let authorized = params
        .token
        .as_deref()
        .is_some_and(|token| state.auth.verify_token(token).is_ok());
    if !authorized {
        return StatusCode::UNAUTHORIZED.into_response();
    }

    ws.on_upgrade(move |socket| handle_socket(socket, state))
}

async fn handle_socket(mut socket: WebSocket, state: AppState) {
    let mut conn = SignalingConnection::default();
    debug!("signaling connection opened");

    while let Some(Ok(message)) = socket.recv().await {
        let reply = match message {
            Message::Text(text) => match serde_json::from_str::<SignalMessage>(&text) {
                Ok(msg) => state.signaling.handle(&mut conn, msg).await,
                Err(e) => Some(SignalMessage::error(None, format!("invalid signaling message: {e}"))),
            },
            Message::Binary(_) => Some(SignalMessage::error(None, "binary messages are not supported")),
            Message::Close(_) => break,
            _ => continue,
        };

        if let Some(reply) = reply {
            let Ok(json) = serde_json::to_string(&reply) else {
                continue;
            };
            if socket.send(Message::Text(json)).await.is_err() {
                break;
            }
        }
    }

    state.signaling.disconnect(conn).await;
    debug!("signaling connection closed");
}
