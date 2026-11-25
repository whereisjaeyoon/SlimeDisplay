use bytes::Bytes;
use slime_display_server::{
    display::{adapter::MockAdapter, DisplayConfig, DisplayError, DisplayService, DisplayState},
    streaming::EncodedFrame,
};
use tokio::sync::mpsc;

fn service() -> (DisplayService, MockAdapter) {
    let mock = MockAdapter::default();
    (DisplayService::new(Box::new(mock.clone())), mock)
}

#[test]
fn starting_a_stream_requires_a_display() {
    let (mut svc, mock) = service();
    let (tx, _rx) = mpsc::channel(1);

    assert!(matches!(
        svc.start_stream(tx),
        Err(DisplayError::InvalidState { state: DisplayState::Created, .. })
    ));
    assert!(mock.calls().is_empty());
}

#[test]
fn create_display_reports_info() {
    let (mut svc, _) = service();

    let info = svc.create_display(DisplayConfig::default()).unwrap();

    assert_eq!((info.width, info.height, info.refresh_rate), (1920, 1080, 60));
    assert_eq!(svc.state(), DisplayState::DisplayReady);
    assert_eq!(svc.status().info, Some(info));
}

#[test]
fn full_lifecycle_follows_native_call_order() {
    let (mut svc, mock) = service();
    let (tx, _rx) = mpsc::channel(4);

    svc.create_display(DisplayConfig::default()).unwrap();
    svc.start_stream(tx).unwrap();
    assert_eq!(svc.state(), DisplayState::Encoding);
    svc.shutdown().unwrap();
    assert_eq!(svc.state(), DisplayState::Stopped);

    assert_eq!(
        mock.calls(),
        [
            "create_virtual_display",
            "display_info",
            "start_capture",
            "set_frame_sink", // registered before the encoder starts
            "start_encoder",
            "stop_encoder",
            "clear_frame_sink",
            "stop_capture",
            "destroy_virtual_display",
        ]
    );
}

#[test]
fn frames_reach_the_registered_sink() {
    let (mut svc, mock) = service();
    let (tx, mut rx) = mpsc::channel(4);
    svc.create_display(DisplayConfig::default()).unwrap();
    svc.start_stream(tx).unwrap();

    let frame = EncodedFrame {
        data: Bytes::from_static(b"\x00\x00\x00\x01"),
        pts: 42,
        is_keyframe: true,
    };
    assert!(mock.emit(frame.clone()));
    assert_eq!(rx.try_recv().unwrap(), frame);

    svc.stop_stream().unwrap();
    assert!(!mock.emit(frame));
}

#[test]
fn shutdown_is_idempotent() {
    let (mut svc, _) = service();
    svc.shutdown().unwrap();
    svc.shutdown().unwrap();
    assert_eq!(svc.library_version(), None);
}
