# SlimeDisplay Server – Technical Design Document

## 1. Overview

### 1.1 Purpose

SlimeDisplay Server is a cross-platform C++ application that manages virtual displays and streams their contents to remote client devices.

The server runs on both Windows and macOS and uses the SlimeDisplay native library through a shared C ABI.

The primary objectives are:

- Manage virtual display lifecycle.
- Control screen capture and hardware video encoding.
- Stream encoded video to remote clients using WebRTC.
- Discover available devices through mDNS/DNS-SD.
- Manage client connections and streaming sessions.
- Provide a consistent API across Windows and macOS.

### 1.2 System Architecture

```text
                    SlimeDisplay Server
                 (Shared C++ Implementation)
                            |
             +--------------+--------------+
             |              |              |
         REST API       WebSocket       WebRTC
             |          Signaling       Streaming
             |              |              |
             +--------------+--------------+
                            |
                    SlimeDisplay C ABI
                            |
              +-------------+-------------+
              |                           |
          Windows                     macOS
       SlimeDisplay.dll         SlimeDisplay.framework
              |                           |
       Windows IDD              Native macOS APIs
              |                           |
       Screen Capture            Screen Capture
       Hardware Encoder          Hardware Encoder
```

The C++ server is responsible for orchestration and communication. The SlimeDisplay library remains responsible for platform-specific display, capture, and encoding operations.

## 2. Technology Stack

| Component | Technology |
|---|---|
| Language | C++20 |
| Build System | CMake |
| Compiler (Windows) | MSVC |
| Compiler (macOS) | Apple Clang |
| HTTP Server | Boost.Beast |
| Asynchronous I/O | Boost.Asio |
| WebSocket | Boost.Beast |
| WebRTC | Google WebRTC Native API |
| JSON | nlohmann/json |
| Logging | spdlog |
| Device Discovery | DNS-SD / mDNS |
| Frontend | React + Vite |

### 2.1 Cross-platform Requirements

The same C++ source code should compile on Windows and macOS.

Platform-specific functionality must be isolated behind interfaces.

```text
Windows:
    C++ Server
       |
    SlimeDisplay.dll

macOS:
    C++ Server
       |
    SlimeDisplay.framework
```

The server must not depend on Swift-specific APIs.

## 3. Project Structure

```text
SlimeDisplayServer/
├── CMakeLists.txt
│
├── include/
│   ├── server/
│   │   ├── Server.hpp
│   │   └── ServerConfig.hpp
│   │
│   ├── api/
│   │   ├── HttpServer.hpp
│   │   └── WebSocketServer.hpp
│   │
│   ├── display/
│   │   ├── DisplayService.hpp
│   │   └── SlimeDisplayAdapter.hpp
│   │
│   ├── streaming/
│   │   ├── StreamManager.hpp
│   │   └── FrameBuffer.hpp
│   │
│   ├── webrtc/
│   │   ├── PeerConnectionManager.hpp
│   │   └── SignalingService.hpp
│   │
│   ├── discovery/
│   │   └── IDnsService.hpp
│   │
│   └── security/
│       └── AuthenticationService.hpp
│
├── src/
│   ├── main.cpp
│   │
│   ├── api/
│   │   ├── HttpServer.cpp
│   │   └── WebSocketServer.cpp
│   │
│   ├── display/
│   │   ├── DisplayService.cpp
│   │   └── SlimeDisplayAdapter.cpp
│   │
│   ├── streaming/
│   │   ├── StreamManager.cpp
│   │   └── FrameBuffer.cpp
│   │
│   ├── webrtc/
│   │   ├── PeerConnectionManager.cpp
│   │   └── SignalingService.cpp
│   │
│   ├── discovery/
│   │   └── DnsServiceFactory.cpp
│   │
│   ├── security/
│   │   └── AuthenticationService.cpp
│   │
│   └── platform/
│       ├── windows/
│       │   └── DnsServiceWindows.cpp
│       │
│       └── macos/
│           └── DnsServiceMacOS.cpp
│
├── frontend/
│   └── React + Vite Application
│
└── tests/
    ├── DisplayServiceTests.cpp
    ├── StreamManagerTests.cpp
    └── SignalingTests.cpp
```

## 4. SlimeDisplay Library Integration

### 4.1 C ABI Integration

The server communicates with the native display library exclusively through the public C ABI.

```cpp
#include "slimedisplay.h"
```

The server must not directly access Windows IDD internals or macOS Swift implementation details.

### 4.2 Display Lifecycle

The server manages the display lifecycle through the following functions:

| Operation | Library Function |
|---|---|
| Create instance | `sd_create()` |
| Destroy instance | `sd_destroy()` |
| Create virtual display | `sd_create_virtual_display()` |
| Destroy virtual display | `sd_destroy_virtual_display()` |
| Get display information | `sd_get_virtual_display_info()` |

### 4.3 Capture Lifecycle

The server controls capture using:

- `sd_start_capture()`
- `sd_stop_capture()`

### 4.4 Encoder Lifecycle

The server controls the encoder using:

- `sd_start_encoder()`
- `sd_stop_encoder()`

### 4.5 Encoded Frame Callback

The server registers an encoded-frame callback:

```cpp
sd_set_encoded_frame_callback(
    handle,
    on_encoded_frame,
    user_data
);
```

Callback signature:

```cpp
void on_encoded_frame(
    const uint8_t* data,
    uint32_t size,
    int64_t presentation_time,
    int32_t is_keyframe,
    void* user_data
);
```

The callback provides:

| Field | Description |
|---|---|
| `data` | Encoded video frame data |
| `size` | Encoded frame size in bytes |
| `presentation_time` | Presentation timestamp |
| `is_keyframe` | Whether the frame is a keyframe |
| `user_data` | User-defined context |

The callback should perform minimal work.

Recommended processing:

1. Receive the encoded frame.
2. Copy or transfer the data into an owned frame buffer.
3. Attach timestamp and keyframe metadata.
4. Submit the frame to the streaming pipeline.
5. Return immediately.

**Memory ownership requirement:** The callback does not specify ownership or lifetime of `data`. The server must treat the pointer as borrowed memory and copy it before the callback returns unless the library explicitly guarantees a longer lifetime.

The callback may execute on an internal encoder thread. All shared state must be synchronized, and blocking network operations must not occur inside the callback.

## 5. Server Modules

### 5.1 DisplayService

Responsible for managing the virtual display and coordinating the SlimeDisplay library.

Responsibilities:

- Initialize the SlimeDisplay handle.
- Create and destroy virtual displays.
- Retrieve display information.
- Start and stop capture.
- Start and stop encoding.
- Register and unregister frame callbacks.

The service must maintain a consistent lifecycle state.

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

### 5.2 StreamManager

Responsible for distributing encoded frames to active streaming sessions.

Responsibilities:

- Receive encoded frames.
- Manage frame queues.
- Preserve presentation timestamps.
- Handle keyframe delivery.
- Prevent unbounded buffering.
- Distribute frames to WebRTC peers.

The streaming pipeline should use bounded queues.

If a client cannot consume frames quickly enough, the server should prioritize low latency rather than allowing unlimited frame accumulation.

The implementation must also ensure that clients joining an active stream receive an appropriate keyframe before dependent interframes.

### 5.3 PeerConnectionManager

Responsible for managing WebRTC peer connections.

Responsibilities:

- Create peer connections.
- Configure ICE servers.
- Manage ICE candidates.
- Add outgoing video tracks.
- Handle connection state changes.
- Remove disconnected peers.

The server should support multiple clients through independent peer connections.

The implementation must preserve the encoded-frame timestamp and codec configuration when integrating the encoder output with the WebRTC video source.

The actual WebRTC integration must verify compatibility between the SlimeDisplay encoder output and the selected WebRTC codec configuration.

### 5.4 SignalingService

Responsible for exchanging WebRTC signaling messages.

Signaling is carried over WebSocket.

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

The signaling protocol must be independent of the underlying operating system.

### 5.5 DiscoveryService

Responsible for advertising the SlimeDisplay Server and discovering other compatible devices.

The service uses DNS-SD over mDNS.

Suggested service type:

```text
_slimedisplay._tcp.local.
```

Example service advertisement:

```text
Service Name: SlimeDisplay
Service Type: _slimedisplay._tcp
Port: 8080
```

The advertised port represents the server's TCP control endpoint, not the mDNS port.

mDNS uses UDP port 5353.

The server should advertise:

- Device name.
- Service type.
- Control endpoint port.
- Protocol version.
- Optional capability metadata.

mDNS discovery is intended for local-network discovery. It does not replace WebRTC ICE connectivity checks.

### 5.6 AuthenticationService

Responsible for client authentication and session authorization.

Requirements:

- Generate a unique server identity.
- Authenticate clients before creating privileged sessions.
- Validate session ownership.
- Prevent unauthorized display control.
- Avoid exposing reusable credentials in discovery records.

Device discovery should not automatically grant access to the display.

## 6. Network Architecture

The server uses separate communication channels for control, signaling, and media.

| Channel | Protocol | Purpose |
|---|---|---|
| Discovery | mDNS / DNS-SD | Find available servers |
| Control API | HTTP | Display and session management |
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

All endpoints that modify display state must require authorization.

### 6.2 WebSocket Signaling

Suggested endpoint:

```text
/ws/signaling
```

The WebSocket connection is used only for signaling and control messages.

Video data must be transmitted through WebRTC rather than being sent as WebSocket binary messages.

### 6.3 WebRTC Media

The server acts as the WebRTC sender.

```text
SlimeDisplay Encoder
        |
Encoded Frames
        |
WebRTC Video Source
        |
RTP / SRTP
        |
Client Decoder
        |
Video Rendering
```

The server should support the codec configurations provided by the native encoder.

H.264 is a reasonable initial target because it is widely supported by browsers and native platforms.

The implementation must define how codec configuration, keyframes, timestamps, and frame boundaries are passed from the encoder to WebRTC.

## 7. Cross-platform mDNS Implementation

The discovery interface should remain platform-independent.

```cpp
class IDnsService {
public:
    virtual ~IDnsService() = default;

    virtual bool startAdvertising(
        const std::string& name,
        uint16_t port
    ) = 0;

    virtual bool startBrowsing() = 0;

    virtual void stop() = 0;
};
```

Platform implementations:

| Platform | Implementation |
|---|---|
| macOS | Apple DNS-SD API (`dns_sd.h`) |
| Windows | Bonjour SDK or a supported cross-platform DNS-SD implementation |

On macOS, the C++ server can call Apple's DNS-SD API directly. A separate Swift module is not required.

On Windows, the project must select and package an appropriate DNS-SD implementation. Windows should not be assumed to provide Apple's Bonjour API by default.

The discovery implementation should be isolated from the server's core logic.

## 8. Platform-specific Build Configuration

### 8.1 macOS

Development environment:

- VS Code
- Apple Clang
- CMake
- Ninja
- Xcode Command Line Tools

The server links against the macOS SlimeDisplay framework and the required Apple system libraries.

Example CMake configuration:

```cmake
if(APPLE)
    target_link_libraries(SlimeDisplayServer
        PRIVATE
        "-framework Foundation"
        "-framework CoreFoundation"
    )
endif()
```

The exact framework and library dependencies must match the APIs used by the implementation.

### 8.2 Windows

Development environment:

- VS Code or Visual Studio
- MSVC
- CMake
- Ninja

The server links against the Windows SlimeDisplay DLL and its import library.

Example:

```cmake
if(WIN32)
    target_link_libraries(SlimeDisplayServer
        PRIVATE
        SlimeDisplay
    )
endif()
```

The runtime must be able to locate `SlimeDisplay.dll`.

### 8.3 Shared Build System

The project should use one CMake configuration.

```bash
cmake -S . -B build
cmake --build build
```

Platform-specific dependencies should be selected using CMake conditions.

The same application source code should be used on both platforms.

## 9. Runtime Lifecycle

The expected server startup sequence:

```text
1. Initialize Logger
2. Load Configuration
3. Initialize SlimeDisplay Library
4. Create SlimeDisplay Handle
5. Start HTTP Server
6. Start WebSocket Signaling Server
7. Start mDNS Advertisement
8. Wait for Client Connections
```

When a client requests a display session:

```text
1. Authenticate Client
2. Create Virtual Display (if not already created)
3. Start Capture
4. Start Encoder
5. Register / activate Frame Callback
6. Create WebRTC Peer Connection
7. Negotiate SDP and ICE
8. Begin Media Transmission
```

Shutdown sequence:

```text
1. Reject New Sessions
2. Close WebRTC Peer Connections
3. Stop Frame Delivery
4. Unregister Frame Callback
5. Stop Encoder
6. Stop Capture
7. Destroy Virtual Display
8. Destroy SlimeDisplay Handle
9. Stop mDNS Advertisement
10. Stop HTTP and WebSocket Servers
```

The implementation must ensure callback shutdown is synchronized before destroying the underlying library handle.

## 10. Performance Requirements

The initial target is:

| Metric | Initial Target |
|---|---|
| Resolution | 1920 × 1080 |
| Frame Rate | 60 FPS |
| Video Codec | H.264 |
| Encoding | Hardware accelerated |
| Transport | WebRTC |
| Client Count | Initially one client |
| Frame Buffering | Bounded |
| Streaming Priority | Low latency |

The architecture should allow future support for:

- 2560 × 1440
- 3840 × 2160
- 120 FPS
- Multiple simultaneous clients

Performance testing should measure:

- Capture-to-encode latency.
- Encoder processing time.
- Network transmission latency.
- Client decoding time.
- Render presentation latency.
- Frame drops.
- End-to-end glass-to-glass latency.

## 11. Important Design Constraints

### 11.1 Separate Display and Server Responsibilities

The server must not implement virtual display creation or screen capture itself.

These operations belong to the SlimeDisplay library.

### 11.2 Separate Signaling and Media Transport

WebSocket is used for signaling.

WebRTC is used for video transmission.

### 11.3 Keep Platform Dependencies Isolated

The core server must not contain direct dependencies on Windows IDD or macOS-specific display implementation details.

### 11.4 Callback Safety

Encoded frame callbacks must not block on network transmission.

Frame memory must remain valid throughout the processing pipeline.

### 11.5 Client Independence

The server must not depend on a specific client implementation.

A web client and a native client should be able to connect using the same signaling and media protocols.

## 12. Development Strategy

Recommended implementation order:

1. Create the cross-platform CMake project.
2. Integrate the SlimeDisplay C ABI.
3. Implement DisplayService.
4. Implement the encoded-frame callback pipeline.
5. Implement HTTP and WebSocket servers.
6. Integrate WebRTC media transmission.
7. Implement mDNS service advertisement and discovery.
8. Implement authentication and session management.
9. Build the React web client.
10. Test on Windows and macOS.
11. Optimize latency and frame delivery.

The first milestone should be a single-client FHD 60 FPS streaming session.

---

## Final Architecture Decision

**Shared C++ Server + Platform-specific SlimeDisplay Library + WebRTC Client**

The server is implemented once in C++ and compiled separately for Windows and macOS.

The SlimeDisplay library handles platform-specific display, capture, and encoding operations through a stable C ABI.

The client remains independent of the server implementation and can be developed as either a web application or a native application.