# SlimeDisplay Server – TODO

Ordered roughly by README §13. First milestone: **one client, Full HD, 60 FPS**.

## 0. Get it building

- [ ] Install Rust (`rustup`), then run `cargo build` and `cargo test` in `SlimeDisplay-server/`.
- [ ] Fix compile errors. The skeleton was never compiled; the most likely spots are the `mdns-sd` and `axum` calls ([discovery/service.rs](src/discovery/service.rs), [api/websocket.rs](src/api/websocket.rs)).
- [ ] Commit the generated `Cargo.lock`.

## 1. Native library integration (FFI)

- [ ] Build `SlimeDisplay.dll` (Windows) / `SlimeDisplay.framework` (macOS) and run with `--features native` and `SLIMEDISPLAY_LIB_DIR` set.
- [ ] Try `--features bindgen` (needs libclang) and check the generated bindings match [ffi/slimedisplay.rs](src/ffi/slimedisplay.rs).
- [ ] Windows: add a public header to `SlimeDisplay-windows-core` (it only has a `.cpp` today), or point `SLIMEDISPLAY_INCLUDE_DIR` at a shared header.
- [ ] Confirm with the native library owners, and document:
  - [ ] Passing `NULL` to `sd_set_encoded_frame_callback` unregisters the callback.
  - [ ] No callback is still running once that call returns. [display/adapter.rs](src/display/adapter.rs) depends on this to free the callback context safely.
  - [ ] Native calls may come from any thread (needed for `unsafe impl Send for NativeAdapter`).
- [ ] Check what `presentation_time` is measured in and whether frames arrive as Annex-B H.264, which WebRTC needs.

## 2. DisplayService

- [ ] Run the full lifecycle against the real library: create → info → capture → callback → encoder → stop → destroy.
- [ ] Decide whether slow native calls should run in `tokio::task::spawn_blocking` (TODO in [api/http.rs](src/api/http.rs)).
- [ ] Decide on auto-creating the display when the first session connects (README §9.1), instead of requiring `POST /api/display`.

## 3. WebRTC media (the main missing piece)

All in [webrtc/peer_connection.rs](src/webrtc/peer_connection.rs), `handle_offer`:

- [ ] Build an `RTCPeerConnection` with `::webrtc::api::APIBuilder` with H.264 registered and the ICE servers from config.
- [ ] Add a `TrackLocalStaticSample` (video/H264).
- [ ] Set the remote offer, create and return the answer.
- [ ] Spawn a task: `streams.subscribe()` → `track.write_sample(...)`; store it in `frame_task`.
- [ ] Wire `add_ice_candidate` to the real connection, and send the server's ICE candidates to the client over signaling. This needs a way to push messages to the WebSocket, not just reply.
- [ ] Track connection state changes; clean up when a peer disconnects.
- [ ] Close the `RTCPeerConnection` in `close_session`.

## 4. Streaming

- [ ] Start frame delivery when a session connects and stop it after the last session leaves, rather than only via `/api/stream/start|stop`.
- [ ] Add a "request keyframe" C ABI call (README §12). Without it, a new client may see artifacts until the next natural keyframe (TODO in [streaming/manager.rs](src/streaming/manager.rs)).
- [ ] Tune `SLIMEDISPLAY_FRAME_QUEUE` for latency.

## 5. Discovery

- [ ] Call `start_browsing` if the server should also discover other devices; otherwise remove it.
- [ ] Test that clients on Windows and macOS can see the advertisement.

## 6. Security

- [ ] Replace the single shared token with a pairing flow and per-client session tokens ([security/auth.rs](src/security/auth.rs)).
- [ ] Decide whether `/api/device` should stay public.
- [ ] Consider TLS for the HTTP API and WebSocket (the token currently travels in plain text, and in the WebSocket URL).

## 7. Frontend

- [ ] Scaffold React + Vite in [frontend/](frontend/), or decide that `SlimeDisplay-client` replaces it.
- [ ] Minimal client: get token → open `/ws/signaling?token=...` → `session-create` → send offer → play the video track.

## 8. Testing & polish

- [ ] Add HTTP API tests (e.g. `tower::ServiceExt::oneshot` against `api::router`).
- [ ] End-to-end test on Windows and macOS.
- [ ] Measure the latency metrics in README §10 (capture→encode, glass-to-glass, frame drops, queue depth).
- [ ] Add a "Getting started" section to README (features, env vars, how to run).
