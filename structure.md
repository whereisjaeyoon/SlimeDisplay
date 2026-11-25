# SlimeDisplay Project Structure

## 1. Architecture Overview

SlimeDisplay uses a shared host/session/transport architecture with platform-specific display and media backends.

- **Common layer:** Shared API contracts, session lifecycle, and protocol definitions.
- **WebRTC transport:** Shared real-time media transport for Windows and macOS.
- **Platform backends:** Native display creation, screen capture, and hardware encoding.
- **Web client:** Browser-based Client that receives the media stream and exchanges signaling messages.

```text
                         SlimeDisplay Host
                                 |
                 +---------------+----------------+
                 |                                |
           Common Layer                     Platform Backend
                 |                                |
          +------+-------+                  +-----+------+
          |              |                  |            |
     Session Manager  WebRTC             Windows       macOS
                      Transport           Backend      Backend
                          |                  |            |
                  +-------+-------+          |            |
                  |       |       |          |            |
              PeerConn  Video   Data       Display      Display
                       Track  Channel     Capture      Capture
                  |
             ICE / STUN / TURN
```

## 2. Repository Layout

```text
SlimeDisplay/
│
├── common/
│   ├── api/
│   │   ├── slimedisplay.h
│   │   └── slimedisplay_types.h
│   │
│   ├── session/
│   │   ├── SessionManager.h
│   │   ├── SessionManager.cpp
│   │   ├── SessionConfig.h
│   │   └── SessionConfig.cpp
│   │
│   └── protocol/
│       ├── SignalingMessage.h
│       ├── SignalingMessage.cpp
│       └── SessionProtocol.h
│
├── transport/
│   └── webrtc/
│       ├── WebRTCFactory.h
│       ├── WebRTCFactory.cpp
│       ├── PeerConnectionManager.h
│       ├── PeerConnectionManager.cpp
│       ├── VideoTrack.h
│       ├── VideoTrack.cpp
│       ├── DataChannel.h
│       ├── DataChannel.cpp
│       ├── ICEManager.h
│       └── ICEManager.cpp
│
├── host/
│   ├── windows/
│   │   ├── display/
│   │   ├── capture/
│   │   ├── encoder/
│   │   └── WindowsHost/
│   │
│   └── macos/
│       ├── display/
│       ├── capture/
│       ├── encoder/
│       └── SlimeDisplayMacBackend/
│
├── signaling/
│   ├── WebSocketServer.h
│   ├── WebSocketServer.cpp
│   ├── SignalingService.h
│   └── SignalingService.cpp
│
├── client/
│   └── web/
│       ├── src/
│       ├── public/
│       └── package.json
│
├── tests/
│   ├── common/
│   ├── transport/
│   └── integration/
│
└── docs/
    └── structure.md
```

This is a logical repository layout. The macOS implementation can remain an Xcode Framework project, and the Windows implementation can remain a DLL project. Shared C/C++ code can be built as a common library and linked into each platform target.

## 3. Module Responsibilities

| Module | Responsibility |
|---|---|
| `common/api` | Stable cross-platform C ABI and shared data types |
| `common/session` | Session creation, configuration, state, and shutdown |
| `common/protocol` | Shared signaling-message and session-protocol definitions |
| `transport/webrtc` | PeerConnection, media tracks, data channels, ICE, and WebRTC lifecycle |
| `signaling` | WebSocket signaling server; exchanges SDP and ICE candidates |
| `host/windows` | Windows virtual display, capture, and encoder integration |
| `host/macos` | macOS display, capture, and encoder integration |
| `client/web` | Browser UI, WebRTC receiver, and signaling client |
| `tests` | Unit and end-to-end tests |

## 4. Media Data Flow

The platform-specific backend obtains and encodes frames. The shared WebRTC transport sends the media.

### macOS

```text
ScreenCaptureKit
      |
      v
VideoToolbox Encoder
      |
      v
Encoded H.264 Frames
      |
      v
WebRTC Video Source / Encoder Integration
      |
      v
PeerConnection
      |
      v
Web Client
```

### Windows

```text
Windows Graphics Capture / Desktop Duplication
      |
      v
Windows Hardware Encoder
      |
      v
Encoded H.264 Frames
      |
      v
WebRTC Video Source / Encoder Integration
      |
      v
PeerConnection
      |
      v
Web Client
```

Both platform backends should provide frames through a shared media interface. The interface should distinguish raw frames from encoded frames: a raw-frame path carries pixel data and format metadata, while an encoded-frame path carries codec data, timestamps, keyframe information, and relevant codec metadata.

Do not assume arbitrary H.264 byte buffers can be passed directly to every WebRTC API. Integrate with the WebRTC library's supported encoded-frame or encoder interfaces, and define buffer ownership and lifetime explicitly.

## 5. Signaling and WebRTC Responsibilities

WebRTC media transport and signaling are separate concerns.

### Signaling over WebSocket

The signaling service is responsible for:

- Establishing a signaling connection between Host and Web Client.
- Exchanging SDP offers and answers.
- Exchanging ICE candidates.
- Communicating session setup, teardown, and connection state.

### Media and Data over WebRTC

The WebRTC transport is responsible for:

- PeerConnection lifecycle.
- ICE connectivity checks and candidate handling.
- STUN/TURN configuration.
- Video track transport.
- Optional DataChannel for control messages or other low-latency data.

The WebSocket signaling channel establishes the WebRTC connection. Video media should flow over WebRTC rather than being relayed through the signaling WebSocket.

## 6. Recommended Interfaces

A shared C ABI can provide a stable entry point for both platform implementations. Keep the public header platform-neutral and use fixed-width integer types.

Illustrative API:

```c
#ifndef SLIMEDISPLAY_H
#define SLIMEDISPLAY_H

#include <stdint.h>

#ifdef _WIN32
    #ifdef SLIMEDISPLAY_EXPORTS
        #define SD_API __declspec(dllexport)
    #else
        #define SD_API __declspec(dllimport)
    #endif
    #define SD_CALL __cdecl
#else
    #define SD_API __attribute__((visibility("default")))
    #define SD_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SD_Handle SD_Handle;

typedef enum {
    SD_SUCCESS = 0,
    SD_ERROR_INVALID_ARGUMENT = -1,
    SD_ERROR_INITIALIZATION = -2,
    SD_ERROR_NOT_SUPPORTED = -3,
    SD_ERROR_PERMISSION_DENIED = -4,
    SD_ERROR_CAPTURE = -5,
    SD_ERROR_ENCODING = -6,
    SD_ERROR_TRANSPORT = -7
} SD_Result;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t fps;
} SD_DisplayConfig;

SD_API SD_Result SD_CALL sd_create(SD_Handle **handle);
SD_API void SD_CALL sd_destroy(SD_Handle *handle);

SD_API SD_Result SD_CALL
sd_create_virtual_display(
    SD_Handle *handle,
    const SD_DisplayConfig *config
);

SD_API SD_Result SD_CALL
sd_destroy_virtual_display(SD_Handle *handle);

SD_API SD_Result SD_CALL sd_start_capture(SD_Handle *handle);
SD_API SD_Result SD_CALL sd_stop_capture(SD_Handle *handle);

SD_API SD_Result SD_CALL sd_start_stream(SD_Handle *handle);
SD_API SD_Result SD_CALL sd_stop_stream(SD_Handle *handle);

#ifdef __cplusplus
}
#endif

#endif
```

This header is an initial contract sketch, not a complete production API. Define callback registration, asynchronous-operation completion, capability discovery, error reporting, and thread safety before freezing the ABI.

## 7. Design Boundaries

Keep these boundaries explicit:

1. **The platform backend does not own WebRTC.** It creates and manages the display, captures frames, and provides encoded or raw media to the common transport layer.
2. **The WebRTC transport does not own display creation.** It accepts media from the backend and manages network transport.
3. **Signaling does not carry the video stream.** It exchanges connection metadata such as SDP and ICE candidates.
4. **The Session Manager coordinates modules.** It starts and stops the backend, signaling, and WebRTC transport in the correct order.
5. **The public C API remains stable.** Platform-specific implementation details stay behind the API.

## 8. Suggested Implementation Order

1. Define the shared C API and data types.
2. Implement the macOS capture-and-encode pipeline independently.
3. Implement the Windows capture-and-encode pipeline against the same media contract.
4. Integrate the WebRTC transport with one platform first.
5. Add WebSocket signaling and browser-side negotiation.
6. Connect the second platform to the same transport layer.
7. Add DataChannel-based control messages if needed.
8. Add integration tests for connection setup, stream start/stop, reconnection, and cleanup.

## 9. Platform Capability Notes

The common API should not imply that every feature is available on every operating system. Provide capability queries or return `SD_ERROR_NOT_SUPPORTED` when a platform cannot implement a requested operation.

For macOS, virtual display creation requires a separate feasibility and distribution review. Keep virtual-display support isolated from the public capture/encoding pipeline so ordinary display capture and streaming can be developed and tested independently.
