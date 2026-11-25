# SlimeDisplay Server – Technical Design Document (Rust Edition)

## 1. Overview

### 1.1 Purpose

SlimeDisplay Server is a cross-platform Rust application that manages virtual displays and streams their contents to remote client devices.

The server runs on both Windows and macOS and uses the SlimeDisplay native library through a stable C ABI.

Primary objectives:

- Manage virtual display lifecycle
- Control screen capture and hardware video encoding
- Stream encoded video to remote clients using WebRTC
- Discover available devices through mDNS / DNS-SD
- Manage client connections and streaming sessions
- Provide a consistent API across Windows and macOS

### 1.2 System Architecture

```text
                    SlimeDisplay Server
                 (Shared Rust Implementation)
                            |
             +--------------+--------------+
             |              |              |
         REST API       WebSocket        WebRTC
             |          Signaling       Streaming
             |              |              |
             +--------------+--------------+
                            |
                    SlimeDisplay C ABI (FFI)
                            |
              +-------------+-------------+
              |                           |
          Windows                      macOS
       SlimeDisplay.dll         SlimeDisplay.framework
              |                           |
         Windows IDD              Native macOS APIs
              |                           |
        Screen Capture              Screen Capture
        Hardware Encoder            Hardware Encoder
```

The Rust server is responsible for orchestration and communication.

The SlimeDisplay library is responsible for platform-specific virtual display, capture, and encoding operations.

---

## 2. Technology Stack

| Component | Technology |
|---|---|
| Language | Rust (Edition 2021 / 2024) |
| Build System | Cargo |
| Async Runtime | Tokio |
| HTTP Server | Axum |
| WebSocket | Axum WebSocket |
| WebRTC | webrtc-rs (or libwebrtc binding) |
| Serialization | serde + serde_json |
| Logging / Tracing | tracing + tracing-subscriber |
| Device Discovery | mdns-sd |
| FFI | bindgen |
| Frontend | React + Vite |

### 2.1 Cross-platform Requirements

The same Rust source code must compile on both Windows and macOS.

Platform-specific functionality is isolated behind traits and conditional compilation (`#[cfg(target_os = "...")]`).

```text
Windows:
    Rust Server (Cargo)
       |
    SlimeDisplay.dll (via FFI)

macOS:
    Rust Server (Cargo)
       |
    SlimeDisplay.framework (via FFI)
```

The server must not depend on Swift-specific APIs.

The native library provides a consistent C ABI regardless of the underlying platform implementation.

---

## 3. Project Structure

```text
slime-display-server/
├── Cargo.toml
├── Cargo.lock
├── build.rs                         # bindgen / platform-specific linking
│
├── src/
│   ├── main.rs
│   ├── lib.rs
│   │
│   ├── api/
│   │   ├── mod.rs
│   │   ├── http.rs                  # Axum routes
│   │   └── websocket.rs             # Signaling WebSocket
│   │
│   ├── display/
│   │   ├── mod.rs
│   │   ├── service.rs               # DisplayService
│   │   └── adapter.rs               # SlimeDisplay FFI adapter
│   │
│   ├── streaming/
│   │   ├── mod.rs
│   │   ├── manager.rs               # StreamManager
│   │   └── frame_buffer.rs
│   │
│   ├── webrtc/
│   │   ├── mod.rs
│   │   ├── peer_connection.rs       # PeerConnectionManager
│   │   └── signaling.rs
│   │
│   ├── discovery/
│   │   ├── mod.rs
│   │   └── service.rs               # DnsService
│   │
│   ├── security/
│   │   ├── mod.rs
│   │   └── auth.rs
│   │
│   ├── ffi/
│   │   ├── mod.rs
│   │   └── slimedisplay.rs          # Generated FFI bindings
│   │
│   └── config.rs
│
├── frontend/                        # React + Vite application
│
└── tests/
    ├── display_service_tests.rs
    ├── stream_manager_tests.rs
    └── signaling_tests.rs
```

---

## 4. SlimeDisplay Library Integration (FFI)

### 4.1 C ABI Integration

The server communicates with the native display library exclusively through the public C ABI.

The public headers are:

```text
include/
├── slimedisplay.h
└── slimedisplay_types.h
```

- `slimedisplay_types.h`: Defines shared types, enums, structures, and the opaque handle.
- `slimedisplay.h`: Defines exported API functions, callbacks, and platform-specific export macros.

Rust bindings are generated using `bindgen` in `build.rs`.

The generated bindings must match the actual C declarations.

The C API uses the following handle creation interface:

```c
SD_Result SD_CALL
sd_create(SD_Handle** handle);
```

The handle is returned through an output parameter. The function itself returns an `SD_Result`, not a pointer.

The corresponding Rust FFI declaration is:

```rust
unsafe extern "C" {
    fn sd_create(handle: *mut *mut SD_Handle) -> SD_Result;

    fn sd_destroy(handle: *mut SD_Handle);
}
```

For manually maintained bindings, C structures and enums must use `#[repr(C)]` to preserve ABI compatibility.

### 4.2 C ABI Type Mapping

| C Type | Rust Type |
|---|---|
| `SD_Handle*` | `*mut SD_Handle` |
| `SD_Handle**` | `*mut *mut SD_Handle` |
| `SD_Result` | `SD_Result` |
| `SD_DisplayConfig` | `SD_DisplayConfig` |
| `SD_DisplayInfo` | `SD_DisplayInfo` |
| `SD_PixelFormat` | `SD_PixelFormat` |
| `uint8_t` | `u8` |
| `uint32_t` | `u32` |
| `uint64_t` | `u64` |
| `int32_t` | `i32` |
| `int64_t` | `i64` |
| `void*` | `*mut std::ffi::c_void` |

`SD_Handle` is an opaque type. Its internal implementation must not be accessed directly by Rust.

### 4.3 Public C ABI Functions

The following table represents the currently implemented public C API.

| Operation | C ABI Function | Return Type |
|---|---|---|
| Create instance | `sd_create()` | `SD_Result` |
| Destroy instance | `sd_destroy()` | `void` |
| Create virtual display | `sd_create_virtual_display()` | `SD_Result` |
| Destroy virtual display | `sd_destroy_virtual_display()` | `SD_Result` |
| Get display information | `sd_get_virtual_display_info()` | `SD_Result` |
| Start capture | `sd_start_capture()` | `SD_Result` |
| Stop capture | `sd_stop_capture()` | `SD_Result` |
| Start encoder | `sd_start_encoder()` | `SD_Result` |
| Stop encoder | `sd_stop_encoder()` | `SD_Result` |
| Register frame callback | `sd_set_encoded_frame_callback()` | `SD_Result` |
| Get library version | `sd_get_version()` | `uint32_t` |

### 4.4 Result Codes

The C ABI defines the following result codes:

| Result | Value | Description |
|---|---:|---|
| `SD_SUCCESS` | 0 | Operation successful |
| `SD_ERROR_INVALID_ARGUMENT` | -1 | Invalid argument |
| `SD_ERROR_INITIALIZATION` | -2 | Initialization failed |
| `SD_ERROR_NOT_SUPPORTED` | -3 | Operation not supported |
| `SD_ERROR_PERMISSION_DENIED` | -4 | Permission denied |
| `SD_ERROR_CAPTURE` | -5 | Capture failure |
| `SD_ERROR_ENCODING` | -6 | Encoding failure |
| `SD_ERROR_TRANSPORT` | -7 | Transport failure |
| `SD_ERROR_INVALID_STATE` | -8 | Invalid lifecycle state |
| `SD_ERROR_INTERNAL` | -9 | Internal error |

All functions returning `SD_Result` must have their return values checked.

The Rust adapter should convert native result codes into Rust `Result<T, E>` values.

### 4.5 Display Configuration and Information

The C ABI defines two separate structures.

Display creation configuration:

```c
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t fps;
} SD_DisplayConfig;
```

Virtual display information:

```c
typedef struct {
    uint64_t id;
    uint32_t width;
    uint32_t height;
    uint32_t refresh_rate;
} SD_DisplayInfo;
```

`SD_DisplayConfig` is passed to `sd_create_virtual_display()`.

`SD_DisplayInfo` is populated by `sd_get_virtual_display_info()`.

The requested FPS and the actual display refresh rate are separate properties and must not be treated as interchangeable.

### 4.6 Display Lifecycle

The native library exposes the following lifecycle operations:

1. `sd_create()` initializes the library instance and returns an opaque handle.
2. `sd_create_virtual_display()` creates a virtual display using `SD_DisplayConfig`.
3. `sd_get_virtual_display_info()` retrieves the created display information.
4. `sd_start_capture()` starts screen capture.
5. `sd_start_encoder()` starts video encoding.
6. `sd_stop_encoder()` stops encoding.
7. `sd_stop_capture()` stops screen capture.
8. `sd_destroy_virtual_display()` destroys the virtual display.
9. `sd_destroy()` releases the library handle.

Each operation must be checked for its corresponding `SD_Result`.

### 4.7 Encoded Frame Callback

The C ABI provides a callback for receiving encoded video frames.

Actual C declaration:

```c
typedef void (*SD_EncodedFrameCallback)(
    const uint8_t* data,
    uint32_t size,
    int64_t presentation_time,
    int32_t is_keyframe,
    void* user_data
);
```

The callback is registered through:

```c
SD_Result SD_CALL
sd_set_encoded_frame_callback(
    SD_Handle* handle,
    SD_EncodedFrameCallback callback,
    void* user_data
);
```

Corresponding Rust callback type:

```rust
type EncodedFrameCallback = Option<
    unsafe extern "C" fn(
        data: *const u8,
        size: u32,
        presentation_time: i64,
        is_keyframe: i32,
        user_data: *mut std::ffi::c_void,
    ),
>;
```

The callback registration function must use the same signature as the C ABI.

The callback receives:

| Parameter | Description |
|---|---|
| `data` | Pointer to encoded frame data |
| `size` | Encoded frame size in bytes |
| `presentation_time` | Presentation timestamp |
| `is_keyframe` | Keyframe indicator |
| `user_data` | User-provided context pointer |

The existing `SD_FrameInfo` structure is not currently passed to this callback.

**Critical safety rules:**

- The callback may execute on an internal encoder thread.
- Treat `data` as borrowed memory.
- Always copy frame data into an owned buffer before returning from the callback.
- Never perform blocking network I/O inside the callback.
- Avoid long-held locks.
- Use a thread-safe channel to forward frames to the streaming pipeline.
- Ensure that the `user_data` context remains valid throughout all callback executions.
- Synchronize callback shutdown before releasing the associated context or library handle.

Recommended processing flow:

1. Receive encoded frame.
2. Copy data into an owned buffer.
3. Attach presentation timestamp and keyframe metadata.
4. Send the frame through a channel.
5. Return immediately.

Callback unregistration must be supported and documented by the native library. If passing `NULL` to the callback parameter is used for unregistration, the native implementation must explicitly guarantee that behavior.

### 4.8 FFI Safety and Ownership

The Rust adapter is responsible for safely managing the native library handle.

Recommended ownership model:

- `DisplayService` owns the native `SD_Handle`.
- The handle is created once during server initialization.
- Native operations are coordinated through `DisplayService`.
- The callback context is owned by the streaming subsystem.
- Callback context destruction is delayed until all in-flight callbacks have completed.

Raw pointers must not be exposed to higher-level application modules.

The native FFI layer should provide safe Rust abstractions around the C API.

---

## 5. Server Modules

### 5.1 DisplayService

Responsible for managing the virtual display and coordinating the SlimeDisplay library.

Responsibilities:

- Initialize the SlimeDisplay handle.
- Create and destroy virtual displays.
- Start and stop screen capture.
- Start and stop video encoding.
- Register and unregister encoded frame callbacks.
- Retrieve virtual display information.
- Maintain a consistent lifecycle state machine.
- Convert native errors into Rust errors.

Suggested states:

```text
Uninitialized
      |
   Created
      |
 DisplayReady
      |
  Capturing
      |
  Encoding
      |
   Stopped
```

The state machine must prevent invalid native API calls.

### 5.2 StreamManager

Responsible for distributing encoded frames to active streaming sessions.

Responsibilities:

- Receive encoded frames from the native callback pipeline.
- Manage bounded frame queues.
- Preserve presentation timestamps.
- Track keyframe availability.
- Ensure newly connected clients receive an appropriate keyframe.
- Prevent unbounded buffering.
- Prioritize low-latency frame delivery.
- Fan out frames to WebRTC peers.

The streaming pipeline should use bounded channels to prevent excessive memory consumption.

### 5.3 PeerConnectionManager

Responsible for managing WebRTC peer connections.

Responsibilities:

- Create peer connections.
- Configure ICE servers.
- Manage ICE candidates.
- Add outgoing video tracks.
- Handle connection state changes.
- Clean up disconnected peers.
- Forward encoded video frames to the appropriate WebRTC video source.

Supports multiple independent peer connections.

### 5.4 SignalingService

Exchanges WebRTC signaling messages over WebSocket.

Message types:

- `offer`
- `answer`
- `ice-candidate`
- `session-create`
- `session-close`
- `error`

Example:

```json
{
    "type": "offer",
    "session_id": "session-123",
    "sdp": "..."
}
```

Signaling is independent of media transport.

### 5.5 DiscoveryService

Advertises the server and discovers compatible devices using DNS-SD / mDNS.

Suggested service type:

```text
_slimedisplay._tcp.local.
```

Advertised information:

- Device name
- Control endpoint port
- Protocol version
- Optional capability metadata

Discovery must not expose reusable authentication credentials.

### 5.6 AuthenticationService

Handles client authentication and session authorization.

Requirements:

- Generate a unique server identity.
- Authenticate clients before privileged operations.
- Validate session ownership.
- Prevent unauthorized display control.
- Avoid exposing reusable credentials in discovery records.

---

## 6. Network Architecture

| Channel | Protocol | Purpose |
|---|---|---|
| Discovery | mDNS / DNS-SD | Find available servers |
| Control API | HTTP (Axum) | Display and session management |
| Signaling | WebSocket | WebRTC negotiation |
| Media | WebRTC | Encoded video transmission |

### 6.1 HTTP API

Suggested endpoints:

| Method | Endpoint | Description |
|---|---|---|
| GET | `/api/health` | Server health |
| GET | `/api/device` | Device information |
| GET | `/api/display` | Current display information |
| POST | `/api/display` | Create virtual display |
| DELETE | `/api/display` | Destroy virtual display |
| POST | `/api/stream/start` | Start streaming |
| POST | `/api/stream/stop` | Stop streaming |
| GET | `/api/sessions` | List active sessions |

These HTTP endpoints are server-level APIs. They are not direct C ABI functions.

The Rust server translates HTTP requests into the corresponding SlimeDisplay library operations.

All state-modifying endpoints require authorization.

### 6.2 WebSocket Signaling

Endpoint:

```text
/ws/signaling
```

Used for WebRTC signaling and session control messages.

Video data must never be sent as WebSocket binary frames.

### 6.3 WebRTC Media

```text
SlimeDisplay Encoder
        |
  Encoded Frames
        |
 Rust Frame Pipeline
        |
 WebRTC Video Source
        |
    RTP / SRTP
        |
 Client Decoder
        |
    Rendering
```

H.264 is the recommended initial codec target.

The encoded frames provided by the native library must be compatible with the WebRTC video source and negotiated codec configuration.

---

## 7. Cross-platform mDNS Implementation

The Rust server should use a cross-platform DNS-SD / mDNS abstraction.

```rust
#[async_trait]
pub trait DnsService: Send + Sync {
    async fn start_advertising(
        &self,
        name: &str,
        port: u16,
    ) -> Result<()>;

    async fn start_browsing(&self) -> Result<()>;

    async fn stop(&self) -> Result<()>;
}
```

| Platform | Implementation |
|---|---|
| Windows | mdns-sd crate or Bonjour SDK |
| macOS | mdns-sd crate or Apple DNS-SD API |

The discovery implementation must be isolated from the core server logic.

---

## 8. Platform-specific Build Configuration

### 8.1 macOS

Link against the SlimeDisplay framework and required system libraries in `build.rs`.

The framework must be available to the Rust application at runtime.

### 8.2 Windows

Link against `SlimeDisplay.dll` and its import library.

Ensure the DLL is discoverable at runtime.

The native library must export the functions declared in `slimedisplay.h`.

### 8.3 Shared Build

```bash
cargo build --release
```

Platform-specific code is selected via `#[cfg]` attributes.

The same Rust application source is used on both platforms.

The native SlimeDisplay library is built separately for each target platform.

---

## 9. Runtime Lifecycle

### 9.1 Startup Sequence

1. Initialize logger / tracing.
2. Load configuration.
3. Initialize SlimeDisplay library (FFI).
4. Create SlimeDisplay handle using `sd_create()`.
5. Initialize DisplayService.
6. Start HTTP server (Axum).
7. Start WebSocket signaling.
8. Start mDNS advertisement.
9. Wait for client connections.

The virtual display does not necessarily need to be created during server startup. It can be created when the first streaming session requests it.

### 9.2 Client Session Sequence

1. Authenticate client.
2. Create virtual display if needed using `sd_create_virtual_display()`.
3. Retrieve display information using `sd_get_virtual_display_info()`.
4. Start screen capture using `sd_start_capture()`.
5. Register the encoded frame callback using `sd_set_encoded_frame_callback()`.
6. Start the encoder using `sd_start_encoder()`.
7. Create WebRTC peer connection.
8. Negotiate SDP and ICE.
9. Begin media transmission.

The callback must be registered before starting the encoder to avoid losing initial encoded frames.

### 9.3 Shutdown Sequence

1. Reject new sessions.
2. Close WebRTC peer connections.
3. Stop frame delivery to WebRTC peers.
4. Stop the encoder using `sd_stop_encoder()`.
5. Unregister the frame callback according to the native library's documented callback unregistration semantics.
6. Wait for all in-flight callbacks to complete.
7. Stop screen capture using `sd_stop_capture()`.
8. Destroy the virtual display using `sd_destroy_virtual_display()`.
9. Destroy the SlimeDisplay handle using `sd_destroy()`.
10. Stop mDNS advertisement.
11. Stop HTTP and WebSocket servers.

Callback shutdown must be synchronized before destroying the callback context or library handle.

The native library must guarantee that no callback can access released memory after callback shutdown completes.

---

## 10. Performance Requirements

| Metric | Initial Target |
|---|---|
| Resolution | 1920 × 1080 |
| Frame Rate | 60 FPS |
| Video Codec | H.264 |
| Encoding | Hardware accelerated |
| Transport | WebRTC |
| Client Count | Initially 1 |
| Frame Buffering | Bounded |
| Streaming Priority | Low latency |

Future targets:

- 1440p / 4K
- 120 FPS
- Multiple simultaneous clients

Key metrics to measure:

- Capture-to-encode latency
- Encoder processing time
- Network transmission latency
- End-to-end glass-to-glass latency
- Frame drops
- Frame queue depth
- CPU and GPU utilization

---

## 11. Important Design Constraints

### 11.1 Separate Display and Server Responsibilities

Virtual display creation, screen capture, and hardware encoding belong to the SlimeDisplay native library.

The Rust server handles orchestration and network communication.

### 11.2 Separate Signaling and Media Transport

- WebSocket: Signaling only.
- WebRTC: Video transmission.

### 11.3 Keep Platform Dependencies Isolated

Core server code must not contain direct Windows IDD or macOS display implementation details.

All platform-specific functionality must be accessed through the SlimeDisplay C ABI.

### 11.4 Callback Safety

Encoded-frame callbacks must not perform blocking operations.

Frame memory must be copied into an owned buffer before the callback returns.

Callback context must remain valid until all callback executions have completed.

### 11.5 Client Independence

The server must not depend on a specific client implementation.

Web and native clients should work with the same network protocols.

### 11.6 C ABI Compatibility

The Rust FFI layer must always match the actual public C headers.

Changes to C structures, enums, function signatures, or callback definitions must be reflected in the generated bindings.

The public ABI should remain stable across library versions whenever possible.

---

## 12. Future C ABI Extensions

The current public C ABI provides basic virtual display, capture, encoder lifecycle, and encoded-frame callback functionality.

It does not currently expose detailed encoder configuration.

Potential future extensions include:

- Codec selection (H.264 / HEVC / AV1)
- Bitrate configuration
- Encoding resolution
- Target frame rate
- Keyframe interval
- Encoder profile and level
- Requesting an immediate keyframe
- Encoder capability queries

These features should be introduced through additional public C ABI functions rather than accessing platform-specific encoder internals from Rust.

For example, a future API could introduce:

```c
typedef enum {
    SD_CODEC_UNKNOWN = 0,
    SD_CODEC_H264 = 1,
    SD_CODEC_HEVC = 2,
    SD_CODEC_AV1 = 3
} SD_Codec;
```

This is a proposed extension and is not part of the current public ABI.

---

## 13. Development Strategy

Recommended implementation order:

1. Create the cross-platform Cargo project.
2. Integrate the SlimeDisplay C ABI (FFI + bindgen).
3. Implement the native handle lifecycle.
4. Implement DisplayService.
5. Implement the encoded-frame callback pipeline.
6. Implement HTTP + WebSocket servers (Axum).
7. Integrate WebRTC media transmission.
8. Implement mDNS service advertisement / discovery.
9. Implement authentication and session management.
10. Build the React web client.
11. Test on Windows and macOS.
12. Optimize latency and frame delivery.

**First milestone:** Single-client Full HD 60 FPS streaming session.

---

## Final Architecture Decision

**Shared Rust Server + Platform-specific SlimeDisplay Library + WebRTC Client**

- The server is implemented once in Rust and compiled separately for Windows and macOS.
- The SlimeDisplay native library handles platform-specific virtual display, capture, and encoding operations through a stable C ABI.
- The Rust server manages application lifecycle, networking, signaling, and streaming sessions.
- Encoded video frames are delivered to Rust through the native callback interface.
- Clients remain independent of the server implementation and can be developed as web applications or native applications.
