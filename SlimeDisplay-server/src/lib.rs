//! SlimeDisplay Server: orchestrates the SlimeDisplay native library and streams
//! encoded frames to remote clients over WebRTC.

pub mod api;
pub mod config;
pub mod discovery;
pub mod display;
pub mod ffi;
pub mod security;
pub mod streaming;
pub mod webrtc;

use std::sync::Arc;

use anyhow::Context;
use tokio::{
    net::TcpListener,
    sync::{oneshot, Mutex},
};
use tracing::{info, warn};

use crate::{
    api::AppState,
    config::Config,
    discovery::{DnsService, MdnsDnsService},
    display::DisplayService,
    security::AuthService,
    streaming::StreamManager,
    webrtc::{PeerConnectionManager, SignalingService},
};

/// Version of the HTTP / signaling protocol, advertised over mDNS.
pub const PROTOCOL_VERSION: &str = "1";

/// Runs the server until Ctrl+C, following the startup and shutdown sequences
/// in README §9.
pub async fn run(config: Config) -> anyhow::Result<()> {
    let config = Arc::new(config);

    // 3–5. Initialize the native library and DisplayService.
    let backend = display::adapter::default_backend()
        .context("failed to initialize the SlimeDisplay library")?;
    info!(version = backend.version(), "SlimeDisplay library initialized");
    let display = Arc::new(Mutex::new(DisplayService::new(backend)));

    let streams = Arc::new(StreamManager::new(config.frame_queue_capacity));
    let peers = Arc::new(PeerConnectionManager::new(streams.clone(), config.ice_servers.clone()));
    let signaling = Arc::new(SignalingService::new(peers.clone()));
    let auth = Arc::new(AuthService::new(config.access_token.clone()));

    let state = AppState {
        config: config.clone(),
        display: display.clone(),
        streams,
        peers: peers.clone(),
        signaling,
        auth,
    };

    // 6–7. HTTP API and WebSocket signaling.
    let listener = TcpListener::bind(config.http_addr)
        .await
        .with_context(|| format!("failed to bind {}", config.http_addr))?;
    let local_addr = listener.local_addr()?;
    info!(addr = %local_addr, "HTTP / WebSocket server listening");

    let (stop_tx, stop_rx) = oneshot::channel::<()>();
    let server = tokio::spawn(async move {
        axum::serve(listener, api::router(state))
            .with_graceful_shutdown(async move {
                let _ = stop_rx.await;
            })
            .await
    });

    // 8. mDNS advertisement.
    let discovery = MdnsDnsService::new()?;
    discovery
        .start_advertising(&config.device_name, local_addr.port())
        .await?;

    tokio::signal::ctrl_c().await?;
    info!("shutting down");

    // Shutdown sequence (README §9.3).
    peers.begin_shutdown();
    peers.close_all().await;
    if let Err(e) = display.lock().await.shutdown() {
        warn!(error = %e, "display shutdown failed");
    }
    if let Err(e) = discovery.stop().await {
        warn!(error = %e, "failed to stop mDNS");
    }
    let _ = stop_tx.send(());
    server.await??;

    Ok(())
}
