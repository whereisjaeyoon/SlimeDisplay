//! Virtual display lifecycle (README §5.1).

pub mod adapter;
pub mod service;

use serde::{Deserialize, Serialize};
use thiserror::Error;

use crate::ffi::SdError;

pub use service::{DisplayService, DisplayState, DisplayStatus};

/// Requested virtual display configuration (`SD_DisplayConfig`).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(default)]
pub struct DisplayConfig {
    pub width: u32,
    pub height: u32,
    pub fps: u32,
}

impl Default for DisplayConfig {
    fn default() -> Self {
        Self {
            width: 1920,
            height: 1080,
            fps: 60,
        }
    }
}

/// Actual virtual display properties (`SD_DisplayInfo`).
///
/// `refresh_rate` is what the display reports and may differ from the requested `fps`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
pub struct DisplayInfo {
    pub id: u64,
    pub width: u32,
    pub height: u32,
    pub refresh_rate: u32,
}

#[derive(Debug, Error)]
pub enum DisplayError {
    #[error("native library error: {0}")]
    Native(#[from] SdError),
    #[error("cannot {op} while display is {state:?}")]
    InvalidState { op: &'static str, state: DisplayState },
}
