use std::sync::Arc;

use slime_display_server::{
    streaming::StreamManager,
    webrtc::{PeerConnectionManager, SignalMessage, SignalingConnection, SignalingService},
};

fn setup() -> (SignalingService, Arc<PeerConnectionManager>) {
    let streams = Arc::new(StreamManager::new(4));
    let peers = Arc::new(PeerConnectionManager::new(streams, Vec::new()));
    (SignalingService::new(peers.clone()), peers)
}

async fn create_session(signaling: &SignalingService, conn: &mut SignalingConnection) -> String {
    let reply = signaling
        .handle(conn, SignalMessage::SessionCreate { session_id: None })
        .await;
    let Some(SignalMessage::SessionCreate { session_id: Some(id) }) = reply else {
        panic!("unexpected reply: {reply:?}");
    };
    id
}

#[test]
fn parses_the_readme_offer_example() {
    let msg: SignalMessage =
        serde_json::from_str(r#"{ "type": "offer", "session_id": "session-123", "sdp": "..." }"#)
            .unwrap();

    assert_eq!(
        msg,
        SignalMessage::Offer {
            session_id: "session-123".into(),
            sdp: "...".into(),
        }
    );
}

#[test]
fn message_types_are_kebab_case() {
    let msg = SignalMessage::IceCandidate {
        session_id: "s".into(),
        candidate: "candidate:1".into(),
        sdp_mid: Some("0".into()),
        sdp_mline_index: Some(0),
    };

    let json = serde_json::to_value(&msg).unwrap();
    assert_eq!(json["type"], "ice-candidate");
}

#[tokio::test]
async fn session_create_and_close() {
    let (signaling, peers) = setup();
    let mut conn = SignalingConnection::default();

    let id = create_session(&signaling, &mut conn).await;
    assert_eq!(peers.list_sessions().await.len(), 1);

    let reply = signaling
        .handle(&mut conn, SignalMessage::SessionClose { session_id: id })
        .await;
    assert_eq!(reply, None);
    assert!(peers.list_sessions().await.is_empty());
}

#[tokio::test]
async fn other_connections_cannot_touch_a_session() {
    let (signaling, peers) = setup();
    let mut owner = SignalingConnection::default();
    let mut intruder = SignalingConnection::default();

    let id = create_session(&signaling, &mut owner).await;
    let reply = signaling
        .handle(&mut intruder, SignalMessage::SessionClose { session_id: id.clone() })
        .await;

    assert!(matches!(reply, Some(SignalMessage::Error { session_id: Some(s), .. }) if s == id));
    assert_eq!(peers.list_sessions().await.len(), 1);
}

#[tokio::test]
async fn disconnect_closes_owned_sessions() {
    let (signaling, peers) = setup();
    let mut conn = SignalingConnection::default();
    create_session(&signaling, &mut conn).await;

    signaling.disconnect(conn).await;

    assert!(peers.list_sessions().await.is_empty());
}
