//! Axum routes for the control API (README §6.1).

use axum::{
    extract::State,
    http::{header::AUTHORIZATION, HeaderMap, StatusCode},
    response::{IntoResponse, Response},
    routing::{get, post},
    Json, Router,
};
use serde::Serialize;
use serde_json::{json, Value};
use uuid::Uuid;

use super::AppState;
use crate::{
    display::{DisplayConfig, DisplayError, DisplayInfo, DisplayStatus},
    security::AuthError,
    webrtc::SessionSummary,
};

pub fn routes() -> Router<AppState> {
    Router::new()
        .route("/api/health", get(health))
        .route("/api/device", get(device))
        .route(
            "/api/display",
            get(get_display).post(create_display).delete(destroy_display),
        )
        .route("/api/stream/start", post(start_stream))
        .route("/api/stream/stop", post(stop_stream))
        .route("/api/sessions", get(list_sessions))
}

#[derive(Debug)]
pub enum ApiError {
    Unauthorized,
    Conflict(String),
    Internal(String),
}

impl IntoResponse for ApiError {
    fn into_response(self) -> Response {
        let (status, message) = match self {
            ApiError::Unauthorized => (StatusCode::UNAUTHORIZED, "unauthorized".to_owned()),
            ApiError::Conflict(message) => (StatusCode::CONFLICT, message),
            ApiError::Internal(message) => (StatusCode::INTERNAL_SERVER_ERROR, message),
        };
        (status, Json(json!({ "error": message }))).into_response()
    }
}

impl From<DisplayError> for ApiError {
    fn from(e: DisplayError) -> Self {
        match &e {
            DisplayError::InvalidState { .. } => ApiError::Conflict(e.to_string()),
            DisplayError::Native(_) => ApiError::Internal(e.to_string()),
        }
    }
}

impl From<AuthError> for ApiError {
    fn from(_: AuthError) -> Self {
        ApiError::Unauthorized
    }
}

fn authorize(state: &AppState, headers: &HeaderMap) -> Result<(), ApiError> {
    let header = headers.get(AUTHORIZATION).and_then(|v| v.to_str().ok());
    state.auth.verify_bearer(header)?;
    Ok(())
}

async fn health() -> Json<Value> {
    Json(json!({ "status": "ok" }))
}

#[derive(Serialize)]
struct DeviceResponse {
    name: String,
    server_id: Uuid,
    protocol_version: &'static str,
    library_version: Option<u32>,
}

async fn device(State(state): State<AppState>) -> Json<DeviceResponse> {
    let library_version = state.display.lock().await.library_version();
    Json(DeviceResponse {
        name: state.config.device_name.clone(),
        server_id: state.auth.server_id(),
        protocol_version: crate::PROTOCOL_VERSION,
        library_version,
    })
}

async fn get_display(State(state): State<AppState>) -> Json<DisplayStatus> {
    Json(state.display.lock().await.status())
}

// TODO: native calls block; move them to spawn_blocking if they turn out to be slow.
async fn create_display(
    State(state): State<AppState>,
    headers: HeaderMap,
    Json(config): Json<DisplayConfig>,
) -> Result<Json<DisplayInfo>, ApiError> {
    authorize(&state, &headers)?;
    let info = state.display.lock().await.create_display(config)?;
    state.streams.reset();
    Ok(Json(info))
}

async fn destroy_display(
    State(state): State<AppState>,
    headers: HeaderMap,
) -> Result<StatusCode, ApiError> {
    authorize(&state, &headers)?;
    state.display.lock().await.destroy_display()?;
    Ok(StatusCode::NO_CONTENT)
}

async fn start_stream(
    State(state): State<AppState>,
    headers: HeaderMap,
) -> Result<StatusCode, ApiError> {
    authorize(&state, &headers)?;
    let sink = state.streams.frame_sink();
    state.display.lock().await.start_stream(sink)?;
    Ok(StatusCode::NO_CONTENT)
}

async fn stop_stream(
    State(state): State<AppState>,
    headers: HeaderMap,
) -> Result<StatusCode, ApiError> {
    authorize(&state, &headers)?;
    state.display.lock().await.stop_stream()?;
    Ok(StatusCode::NO_CONTENT)
}

async fn list_sessions(
    State(state): State<AppState>,
    headers: HeaderMap,
) -> Result<Json<Vec<SessionSummary>>, ApiError> {
    authorize(&state, &headers)?;
    Ok(Json(state.peers.list_sessions().await))
}
