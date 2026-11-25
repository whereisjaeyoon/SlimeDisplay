use std::{env, net::SocketAddr};

use anyhow::Context;

/// Server configuration, loaded from `SLIMEDISPLAY_*` environment variables.
#[derive(Debug, Clone)]
pub struct Config {
    /// `SLIMEDISPLAY_HTTP_ADDR` — address for the HTTP API and WebSocket signaling.
    pub http_addr: SocketAddr,
    /// `SLIMEDISPLAY_DEVICE_NAME` — name advertised over mDNS.
    pub device_name: String,
    /// `SLIMEDISPLAY_TOKEN` — access token for privileged operations. Generated at startup if unset.
    pub access_token: Option<String>,
    /// `SLIMEDISPLAY_FRAME_QUEUE` — capacity of the bounded frame queues.
    pub frame_queue_capacity: usize,
    /// `SLIMEDISPLAY_ICE_SERVERS` — comma-separated ICE server URLs.
    pub ice_servers: Vec<String>,
}

impl Default for Config {
    fn default() -> Self {
        let device_name = env::var("COMPUTERNAME")
            .or_else(|_| env::var("HOSTNAME"))
            .unwrap_or_else(|_| "SlimeDisplay".to_owned());

        Self {
            http_addr: SocketAddr::from(([0, 0, 0, 0], 8080)),
            device_name,
            access_token: None,
            frame_queue_capacity: 8,
            ice_servers: Vec::new(),
        }
    }
}

impl Config {
    pub fn from_env() -> anyhow::Result<Self> {
        let mut config = Self::default();

        if let Ok(addr) = env::var("SLIMEDISPLAY_HTTP_ADDR") {
            config.http_addr = addr
                .parse()
                .with_context(|| format!("invalid SLIMEDISPLAY_HTTP_ADDR: {addr}"))?;
        }
        if let Ok(name) = env::var("SLIMEDISPLAY_DEVICE_NAME") {
            config.device_name = name;
        }
        if let Ok(token) = env::var("SLIMEDISPLAY_TOKEN") {
            config.access_token = Some(token);
        }
        if let Ok(capacity) = env::var("SLIMEDISPLAY_FRAME_QUEUE") {
            config.frame_queue_capacity = capacity
                .parse()
                .with_context(|| format!("invalid SLIMEDISPLAY_FRAME_QUEUE: {capacity}"))?;
        }
        if let Ok(servers) = env::var("SLIMEDISPLAY_ICE_SERVERS") {
            config.ice_servers = servers
                .split(',')
                .map(str::trim)
                .filter(|s| !s.is_empty())
                .map(str::to_owned)
                .collect();
        }

        config.frame_queue_capacity = config.frame_queue_capacity.max(1);
        Ok(config)
    }
}
