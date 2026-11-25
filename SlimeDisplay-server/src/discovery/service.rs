use std::sync::Mutex;

use anyhow::Result;
use async_trait::async_trait;
use mdns_sd::{ServiceDaemon, ServiceEvent, ServiceInfo};
use tracing::info;

pub const SERVICE_TYPE: &str = "_slimedisplay._tcp.local.";

/// Cross-platform DNS-SD abstraction, isolated from the core server logic.
#[async_trait]
pub trait DnsService: Send + Sync {
    async fn start_advertising(&self, name: &str, port: u16) -> Result<()>;

    async fn start_browsing(&self) -> Result<()>;

    async fn stop(&self) -> Result<()>;
}

/// [`DnsService`] backed by the pure-Rust `mdns-sd` crate (Windows and macOS).
pub struct MdnsDnsService {
    daemon: ServiceDaemon,
    registered: Mutex<Option<String>>,
}

impl MdnsDnsService {
    pub fn new() -> Result<Self> {
        Ok(Self {
            daemon: ServiceDaemon::new()?,
            registered: Mutex::new(None),
        })
    }
}

#[async_trait]
impl DnsService for MdnsDnsService {
    async fn start_advertising(&self, name: &str, port: u16) -> Result<()> {
        let host_name = format!("{}.local.", sanitize_host_label(name));
        // Never put credentials in TXT records: they are visible to the whole LAN.
        let properties = [("protocol", crate::PROTOCOL_VERSION)];

        let service = ServiceInfo::new(SERVICE_TYPE, name, &host_name, "", port, &properties[..])?
            .enable_addr_auto();
        let fullname = service.get_fullname().to_owned();

        self.daemon.register(service)?;
        info!(%fullname, port, "advertising over mDNS");
        *self.registered.lock().unwrap() = Some(fullname);
        Ok(())
    }

    async fn start_browsing(&self) -> Result<()> {
        let events = self.daemon.browse(SERVICE_TYPE)?;
        tokio::spawn(async move {
            while let Ok(event) = events.recv_async().await {
                if let ServiceEvent::ServiceResolved(service) = event {
                    info!(
                        name = service.get_fullname(),
                        port = service.get_port(),
                        "discovered SlimeDisplay device"
                    );
                }
            }
        });
        Ok(())
    }

    async fn stop(&self) -> Result<()> {
        let registered = self.registered.lock().unwrap().take();
        if let Some(fullname) = registered {
            self.daemon.unregister(&fullname)?;
        }
        self.daemon.shutdown()?;
        Ok(())
    }
}

/// Turns a display name into a valid DNS host label.
fn sanitize_host_label(name: &str) -> String {
    let label: String = name
        .chars()
        .map(|c| if c.is_ascii_alphanumeric() { c } else { '-' })
        .collect();
    let label = label.trim_matches('-');
    if label.is_empty() {
        "slimedisplay".to_owned()
    } else {
        label.to_owned()
    }
}
