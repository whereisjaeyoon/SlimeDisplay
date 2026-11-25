//! mDNS / DNS-SD advertisement and discovery (README §5.5, §7).

pub mod service;

pub use service::{DnsService, MdnsDnsService, SERVICE_TYPE};
