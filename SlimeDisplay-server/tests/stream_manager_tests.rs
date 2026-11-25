use std::time::Duration;

use bytes::Bytes;
use slime_display_server::streaming::{EncodedFrame, StreamManager};
use tokio::time::timeout;

fn frame(pts: i64, is_keyframe: bool) -> EncodedFrame {
    EncodedFrame {
        data: Bytes::from_static(b"\x00\x00\x00\x01"),
        pts,
        is_keyframe,
    }
}

#[tokio::test]
async fn new_subscriber_waits_for_a_keyframe() {
    let streams = StreamManager::new(8);
    let mut sub = streams.subscribe();

    streams.publish(frame(1, false));
    streams.publish(frame(2, true));
    streams.publish(frame(3, false));

    assert_eq!(sub.recv().await.unwrap().pts, 2);
    assert_eq!(sub.recv().await.unwrap().pts, 3);
}

#[tokio::test]
async fn late_subscriber_gets_the_cached_keyframe_first() {
    let streams = StreamManager::new(8);
    streams.publish(frame(1, true));
    streams.publish(frame(2, false));

    let mut sub = streams.subscribe();
    streams.publish(frame(3, false));

    assert_eq!(sub.recv().await.unwrap().pts, 1);
    assert_eq!(sub.recv().await.unwrap().pts, 3);
}

#[tokio::test]
async fn lagging_subscriber_resyncs_on_the_next_keyframe() {
    let streams = StreamManager::new(2);
    let mut sub = streams.subscribe();

    for pts in 0..5 {
        streams.publish(frame(pts, pts == 0));
    }
    streams.publish(frame(5, true));
    streams.publish(frame(6, false));

    assert_eq!(sub.recv().await.unwrap().pts, 5);
    assert_eq!(sub.recv().await.unwrap().pts, 6);
}

#[tokio::test]
async fn frame_sink_feeds_subscribers() {
    let streams = StreamManager::new(8);
    let mut sub = streams.subscribe();

    streams.frame_sink().try_send(frame(7, true)).unwrap();

    let received = timeout(Duration::from_secs(1), sub.recv()).await.unwrap();
    assert_eq!(received.unwrap().pts, 7);
}
