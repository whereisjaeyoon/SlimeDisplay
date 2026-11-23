# SlimeDisplay Project Structure

## 1. Architecture Overview

SlimeDisplay uses a shared host/session/transport architecture with
platform-specific display and media backends.

-   **Common layer**: shared API contracts, session lifecycle, and
    protocol definitions.
-   **WebRTC transport**: shared real-time media transport used by both
    Windows and macOS.
-   **Platform backends**: native display creation, screen capture, and
    hardware encoding.
-   **Web client**: browser-based client that receives the media stream
    and exchanges signaling messages.

``` text
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

``` text
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

This is a logical repository layout. The macOS implementation can remain
an Xcode Framework project, and the Windows implementation can remain a
DLL project. Shared C/C++ code can be built as a common library and
linked into each platform target.

## 3. Module Responsibilities

  -----------------------------------------------------------------------
  Module                              Responsibility
  ----------------------------------- -----------------------------------
  `common/api`                        Stable cross-platform C ABI and
                                      shared data types

  `common/session`                    Session creation, configuration,
                                      state, and shutdown

  `common/protocol`                   Shared signaling message and
                                      session protocol definitions

  `transport/webrtc`                  PeerConnection, media tracks, data
                                      channels, ICE, and WebRTC lifecycle

  `signaling`                         WebSocket signaling server;
                                      exchanges SDP and ICE candidates

  `host/windows`                      Windows virtual display, capture,
                                      and encoder integration

  `host/macos`                        macOS display, capture, and encoder
                                      integration

  `client/web`                        Browser UI, WebRTC receiver, and
                                      signaling client

  `tests`                             Unit and end-to-end tests
  -----------------------------------------------------------------------

## 4. Media Data Flow

The platform-specific backend is responsible for obtaining and encoding
frames. The shared WebRTC transport is responsible for sending the
media.

### macOS

``` text
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

``` text
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

Both platform backends should provide frames through a shared media
interface. The interface should distinguish raw frames from encoded
frames: a raw-frame path carries pixel data and format metadata, while
an encoded-frame path carries codec data, timestamps, keyframe
information, and relevant codec metadata.

Do not assume that arbitrary H.264 byte buffers can be passed directly
to every WebRTC API. Integrate with the WebRTC library's supported
encoded-frame or encoder interfaces, and define buffer ownership and
lifetime explicitly.

## 5. Signaling and WebRTC Responsibilities

WebRTC media transport and signaling are separate concerns.

### Signaling over WebSocket

The signaling service is responsible for:

-   Establishing a signaling connection between Host and Web Client.
-   Exchanging SDP offers and answers.
-   Exchanging ICE candidates.
-   Communicating session setup, teardown, and connection state.

### Media and data over WebRTC

The WebRTC transport is responsible for:

-   PeerConnection lifecycle.
-   ICE connectivity checks and candidate handling.
-   STUN/TURN configuration.
-   Video track transport.
-   Optional DataChannel for control messages or other low-latency data.

The WebSocket signaling channel is used to establish the WebRTC
connection. Video media should flow over WebRTC rather than being
relayed through the signaling WebSocket.

## 6. Recommended Interfaces

A shared C ABI can provide a stable entry point for both platform
implementations. Keep the public header platform-neutral and use
fixed-width integer types.

Illustrative API:

``` c
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

This header is an initial contract sketch, not a complete production
API. In particular, define callback registration, asynchronous operation
completion, capability discovery, error reporting, and thread-safety
before freezing the ABI.

## 7. Design Boundaries

Keep these boundaries explicit:

1.  **Platform backend does not own WebRTC.** It creates/manages the
    display, captures frames, and provides encoded or raw media to the
    common transport layer.
2.  **WebRTC transport does not own display creation.** It accepts media
    from the backend and manages network transport.
3.  **Signaling does not carry the video stream.** It exchanges
    connection metadata such as SDP and ICE candidates.
4.  **Session Manager coordinates modules.** It starts and stops the
    backend, signaling, and WebRTC transport in the correct order.
5.  **The public C API remains stable.** Platform-specific
    implementation details stay behind the API.

## 8. Suggested Implementation Order

1.  Define the shared C API and data types.
2.  Implement the macOS capture-and-encode pipeline independently.
3.  Implement the Windows capture-and-encode pipeline against the same
    media contract.
4.  Integrate the WebRTC transport with one platform first.
5.  Add WebSocket signaling and browser-side negotiation.
6.  Connect the second platform to the same transport layer.
7.  Add DataChannel-based control messages if needed.
8.  Add integration tests for connection setup, stream start/stop,
    reconnect, and cleanup.

## 9. Platform Capability Notes

The common API should not imply that every feature is available on every
operating system. Provide capability queries or return
`SD_ERROR_NOT_SUPPORTED` when a platform cannot implement a requested
operation.

For macOS in particular, virtual display creation requires a separate
feasibility and distribution review. Keep virtual display support
isolated from the public capture/encoding pipeline so that ordinary
display capture and streaming can be developed and tested independently.

---

# 한국어 버전

## 1. 아키텍처 개요

SlimeDisplay는 공통 호스트/세션/전송 계층과 플랫폼별 디스플레이 및 미디어 백엔드를 사용하는 구조입니다.

- **공통 계층(Common Layer)**: 공유 API 계약, 세션 생명주기 및 프로토콜 정의
- **WebRTC 전송 계층**: Windows와 macOS 모두에서 사용하는 실시간 미디어 전송 계층
- **플랫폼 백엔드**: 네이티브 디스플레이 생성, 화면 캡처 및 하드웨어 인코딩
- **웹 클라이언트**: 미디어 스트림을 수신하고 시그널링 메시지를 교환하는 브라우저 클라이언트

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

## 2. 저장소 구성

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

위 구조는 논리적 저장소 구성 예시입니다. macOS 구현은 Xcode 프레임워크 프로젝트로 유지하고, Windows 구현은 DLL 프로젝트로 유지할 수 있습니다. 공통 C/C++ 코드는 공통 라이브러리로 빌드한 뒤 각 플랫폼 대상에 링크할 수 있습니다.

## 3. 모듈별 책임

| 모듈 | 책임 |
|---|---|
| `common/api` | 안정적인 크로스 플랫폼 C ABI 및 공통 데이터 타입 |
| `common/session` | 세션 생성, 설정, 상태 관리 및 종료 |
| `common/protocol` | 공통 시그널링 메시지 및 세션 프로토콜 정의 |
| `transport/webrtc` | PeerConnection, 미디어 트랙, 데이터 채널, ICE 및 WebRTC 생명주기 |
| `signaling` | WebSocket 시그널링 서버; SDP와 ICE 후보 교환 |
| `host/windows` | Windows 가상 디스플레이, 캡처 및 인코더 연동 |
| `host/macos` | macOS 디스플레이, 캡처 및 인코더 연동 |
| `client/web` | 브라우저 UI, WebRTC 수신 및 시그널링 클라이언트 |
| `tests` | 단위 테스트 및 종단 간 테스트 |

## 4. 미디어 데이터 흐름

플랫폼별 백엔드는 프레임을 가져오고 인코딩하는 역할을 담당합니다. 공통 WebRTC 전송 계층은 미디어를 전송하는 역할을 담당합니다.

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

두 플랫폼 백엔드는 공통 미디어 인터페이스를 통해 프레임을 제공해야 합니다. 이 인터페이스는 원시 프레임과 인코딩된 프레임을 구분해야 합니다. 원시 프레임 경로는 픽셀 데이터와 포맷 메타데이터를 전달하고, 인코딩된 프레임 경로는 코덱 데이터, 타임스탬프, 키프레임 정보 및 관련 코덱 메타데이터를 전달합니다.

임의의 H.264 바이트 버퍼를 모든 WebRTC API에 직접 전달한다고 가정해서는 안 됩니다. WebRTC 라이브러리가 지원하는 인코딩 프레임 또는 인코더 인터페이스와 연동하고, 버퍼 소유권과 수명을 명확하게 정의해야 합니다.

## 5. 시그널링과 WebRTC 책임

WebRTC 미디어 전송과 시그널링은 서로 분리된 관심사입니다.

### WebSocket 시그널링

시그널링 서비스의 책임은 다음과 같습니다.

- 호스트와 웹 클라이언트 사이의 시그널링 연결을 설정합니다.
- SDP offer와 answer를 교환합니다.
- ICE 후보를 교환합니다.
- 세션 설정, 종료 및 연결 상태를 전달합니다.

### WebRTC를 통한 미디어 및 데이터 전송

WebRTC 전송 계층의 책임은 다음과 같습니다.

- PeerConnection 생명주기 관리
- ICE 연결 확인 및 후보 처리
- STUN/TURN 설정
- 비디오 트랙 전송
- 제어 메시지 또는 저지연 데이터용 선택적 DataChannel 제공

WebSocket 시그널링 채널은 WebRTC 연결을 설정하는 데 사용합니다. 비디오 미디어는 시그널링 WebSocket을 통해 중계하지 않고 WebRTC를 통해 전달해야 합니다.

## 6. 권장 인터페이스

공유 C ABI는 두 플랫폼 구현 모두에 안정적인 진입점을 제공할 수 있습니다. 공개 헤더는 플랫폼 중립적으로 유지하고 고정 너비 정수 타입을 사용합니다.

예시 API:

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

이 헤더는 초기 계약 초안이며 완전한 프로덕션 API가 아닙니다. 특히 콜백 등록, 비동기 작업 완료, 기능 탐색, 오류 보고 및 스레드 안전성을 ABI를 확정하기 전에 정의해야 합니다.

## 7. 설계 경계

다음 경계를 명확하게 유지합니다.

1. **플랫폼 백엔드는 WebRTC를 소유하지 않습니다.** 디스플레이를 생성·관리하고 프레임을 캡처하여 인코딩 또는 원시 미디어를 공통 전송 계층에 제공합니다.
2. **WebRTC 전송 계층은 디스플레이 생성을 소유하지 않습니다.** 백엔드로부터 미디어를 받아 네트워크 전송을 관리합니다.
3. **시그널링은 비디오 스트림을 전달하지 않습니다.** SDP와 ICE 후보 등의 연결 메타데이터만 교환합니다.
4. **세션 매니저는 모듈들을 조율합니다.** 올바른 순서로 백엔드, 시그널링 및 WebRTC 전송을 시작하고 종료합니다.
5. **공개 C API는 안정적으로 유지합니다.** 플랫폼별 구현 세부 사항은 API 뒤에 숨깁니다.

## 8. 권장 구현 순서

1. 공통 C API와 데이터 타입을 정의합니다.
2. macOS 캡처 및 인코딩 파이프라인을 독립적으로 구현합니다.
3. 동일한 미디어 계약에 맞춰 Windows 캡처 및 인코딩 파이프라인을 구현합니다.
4. 먼저 하나의 플랫폼과 WebRTC 전송 계층을 연동합니다.
5. WebSocket 시그널링과 브라우저 측 협상을 추가합니다.
6. 두 번째 플랫폼을 동일한 전송 계층에 연결합니다.
7. 필요한 경우 DataChannel 기반 제어 메시지를 추가합니다.
8. 연결 설정, 스트림 시작/종료, 재접속 및 정리를 위한 통합 테스트를 추가합니다.

## 9. 플랫폼 기능 참고 사항

공통 API는 모든 기능을 모든 운영 체제에서 사용할 수 있다고 암시해서는 안 됩니다. 기능 조회를 제공하거나 플랫폼이 요청을 구현할 수 없는 경우 `SD_ERROR_NOT_SUPPORTED`를 반환해야 합니다.

특히 macOS에서 가상 디스플레이 생성에는 별도의 기술적 실현 가능성 검토와 배포 검토가 필요합니다. 일반 디스플레이 캡처와 스트리밍은 독립적으로 개발하고 테스트할 수 있도록 가상 디스플레이 지원을 공개 캡처/인코딩 파이프라인과 분리해야 합니다.
