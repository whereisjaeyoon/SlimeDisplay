use thiserror::Error;
use tracing::info;
use uuid::Uuid;

#[derive(Debug, Error, PartialEq, Eq)]
pub enum AuthError {
    #[error("missing credentials")]
    Missing,
    #[error("invalid credentials")]
    Invalid,
}

/// Bearer-token authentication for privileged operations.
// TODO: replace the shared token with a pairing flow and per-client session tokens.
pub struct AuthService {
    server_id: Uuid,
    access_token: String,
}

impl AuthService {
    /// Uses `access_token` if given, otherwise generates one and prints it to
    /// the server console so the user can pair a client.
    pub fn new(access_token: Option<String>) -> Self {
        let access_token = access_token.unwrap_or_else(|| {
            let token = Uuid::new_v4().simple().to_string();
            info!(token = %token, "generated access token (set SLIMEDISPLAY_TOKEN to fix it)");
            token
        });

        Self {
            server_id: Uuid::new_v4(),
            access_token,
        }
    }

    /// Unique server identity. Public; not a credential.
    pub fn server_id(&self) -> Uuid {
        self.server_id
    }

    /// Validates an `Authorization: Bearer <token>` header value.
    pub fn verify_bearer(&self, header: Option<&str>) -> Result<(), AuthError> {
        let token = header
            .ok_or(AuthError::Missing)?
            .strip_prefix("Bearer ")
            .ok_or(AuthError::Invalid)?;
        self.verify_token(token)
    }

    pub fn verify_token(&self, token: &str) -> Result<(), AuthError> {
        if constant_time_eq(token.as_bytes(), self.access_token.as_bytes()) {
            Ok(())
        } else {
            Err(AuthError::Invalid)
        }
    }
}

fn constant_time_eq(a: &[u8], b: &[u8]) -> bool {
    a.len() == b.len() && a.iter().zip(b).fold(0u8, |acc, (x, y)| acc | (x ^ y)) == 0
}
