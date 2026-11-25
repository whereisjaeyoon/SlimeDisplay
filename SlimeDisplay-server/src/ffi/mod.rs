//! SlimeDisplay C ABI. Raw bindings live in [`slimedisplay`]; this module adds
//! conversion of native result codes into Rust errors.

pub mod slimedisplay;

use thiserror::Error;

use self::slimedisplay as sys;

/// Error codes from `SD_Result` (README §4.4).
#[derive(Debug, Error, Clone, Copy, PartialEq, Eq)]
pub enum SdError {
    #[error("invalid argument")]
    InvalidArgument,
    #[error("initialization failed")]
    Initialization,
    #[error("operation not supported")]
    NotSupported,
    #[error("permission denied")]
    PermissionDenied,
    #[error("capture failure")]
    Capture,
    #[error("encoding failure")]
    Encoding,
    #[error("transport failure")]
    Transport,
    #[error("invalid lifecycle state")]
    InvalidState,
    #[error("internal error")]
    Internal,
    #[error("unknown native result code {0}")]
    Unknown(i32),
}

/// Converts an `SD_Result` into a Rust `Result`.
pub fn check(code: sys::SD_Result) -> Result<(), SdError> {
    match code {
        sys::SD_SUCCESS => Ok(()),
        sys::SD_ERROR_INVALID_ARGUMENT => Err(SdError::InvalidArgument),
        sys::SD_ERROR_INITIALIZATION => Err(SdError::Initialization),
        sys::SD_ERROR_NOT_SUPPORTED => Err(SdError::NotSupported),
        sys::SD_ERROR_PERMISSION_DENIED => Err(SdError::PermissionDenied),
        sys::SD_ERROR_CAPTURE => Err(SdError::Capture),
        sys::SD_ERROR_ENCODING => Err(SdError::Encoding),
        sys::SD_ERROR_TRANSPORT => Err(SdError::Transport),
        sys::SD_ERROR_INVALID_STATE => Err(SdError::InvalidState),
        sys::SD_ERROR_INTERNAL => Err(SdError::Internal),
        other => Err(SdError::Unknown(other as i32)),
    }
}
