# SlimeDisplay Server – 기술 설계 문서 (Rust 버전)

## 1. 프로젝트 개요

### 1.1 프로젝트 설명

**SlimeDisplay**는 Windows 및 macOS 호스트에서 가상 디스플레이를 생성하고, 해당 디스플레이의 화면을 다른 기기로 실시간 스트리밍하는 크로스 플랫폼 프로젝트다.

호스트는 운영체제의 네이티브 API를 사용하여 가상 디스플레이를 생성하고 화면을 캡처한다. 캡처된 프레임은 H.264 등의 코덱으로 인코딩한 뒤 WebRTC를 통해 클라이언트로 전송한다.

서버의 공통 로직은 Rust로 구현하고, 운영체제별 가상 디스플레이 및 화면 캡처 기능은 네이티브 라이브러리로 분리한다. Rust 서버는 C ABI를 통해 네이티브 라이브러리와 통신한다.

### 1.2 지원 플랫폼

| 구성 요소 | 지원 플랫폼 |
|---|---|
| Host | Windows, macOS |
| Client | Windows, macOS, iOS, Android |
| Client UI | Web (HTML, CSS, TypeScript) |
| Server | Rust |
| Native Display Library | 플랫폼별 네이티브 구현 |
| Video Transport | WebRTC |
| Signaling | WebSocket |
| Discovery | mDNS / DNS-SD |

### 1.3 주요 목표

- Windows 및 macOS에서 가상 디스플레이 생성
- 가상 디스플레이의 화면을 실시간으로 캡처
- H.264 영상 인코딩
- WebRTC를 통한 저지연 영상 스트리밍
- WebSocket을 이용한 클라이언트 연결 및 Signaling
- mDNS를 이용한 로컬 네트워크 내 호스트 검색
- Rust 기반의 크로스 플랫폼 서버 아키텍처
- C ABI를 통한 네이티브 라이브러리와 Rust 서버 간 인터페이스 분리

---

## 2. 시스템 아키텍처

### 2.1 전체 구조

```text
                     SlimeDisplay Client
                  (Windows / macOS / iOS / Android)
                               |
                               |
                         WebRTC Video
                         WebSocket
                               |
                               v
+------------------------------------------------------+
|                  SlimeDisplay Host                  |
|                                                      |
|  +------------------------------------------------+  |
|  |              Rust Server                       |  |
|  |                                                |  |
|  |  +-------------+  +-------------------------+  |  |
|  |  | API Server  |  | Signaling Server        |  |  |
|  |  | (Axum)      |  | (WebSocket)             |  |  |
|  |  +-------------+  +-------------------------+  |  |
|  |                                                |  |
|  |  +-------------+  +-------------------------+  |  |
|  |  | WebRTC      |  | Discovery Service       |  |  |
|  |  | Manager     |  | (mDNS)                  |  |  |
|  |  +-------------+  +-------------------------+  |  |
|  |                                                |  |
|  |  +------------------------------------------+  |  |
|  |  | Native FFI                                |  |  |
|  |  | C ABI                                     |  |  |
|  |  +------------------------------------------+  |  |
|  +------------------------------------------------+  |
|                         |                            |
|                         v                            |
|  +------------------------------------------------+  |
|  |        Platform-specific Native Library       |  |
|  |                                                |  |
|  |  Windows: Virtual Display / Capture / Encoder  |  |
|  |  macOS:   Virtual Display / Capture / Encoder  |  |
|  +------------------------------------------------+  |
+------------------------------------------------------+
```

### 2.2 아키텍처 구성

시스템은 크게 세 부분으로 구성된다.

**1. Rust Server**

플랫폼에 독립적인 서버 로직을 담당한다.

- HTTP API
- WebSocket Signaling
- WebRTC 연결 관리
- 클라이언트 세션 관리
- mDNS 서비스 검색 및 등록
- 네이티브 라이브러리 호출

**2. Native Display Library**

운영체제별 기능을 담당한다.

- 가상 디스플레이 생성 및 제거
- 디스플레이 정보 조회
- 화면 캡처
- 영상 인코딩
- 인코딩된 프레임 전달

**3. Client**

브라우저 또는 네이티브 애플리케이션에서 서버에 연결한다.

- 호스트 검색 및 연결
- WebRTC 세션 생성
- 영상 수신 및 재생
- 스트리밍 세션 관리

---

## 3. 기술 스택

| 영역 | 기술 |
|---|---|
| Server Language | Rust |
| Async Runtime | Tokio |
| HTTP Server | Axum |
| WebSocket | Axum WebSocket |
| Serialization | Serde |
| WebRTC | webrtc-rs 또는 libwebrtc |
| Discovery | mdns-sd |
| Native Interface | C ABI / FFI |
| FFI Binding | bindgen |
| Logging | tracing |
| Build System | Cargo |
| Client | TypeScript, React, Vite |

### 3.1 Rust

Rust는 서버의 공통 로직을 구현하는 데 사용한다.

주요 역할은 다음과 같다.

- 서버 및 세션 생명주기 관리
- 비동기 네트워크 통신
- WebRTC 연결 관리
- 네이티브 라이브러리와의 FFI
- 클라이언트별 스트리밍 상태 관리

### 3.2 Tokio

Tokio는 비동기 런타임으로 사용한다.

다음 작업을 비동기적으로 처리한다.

- HTTP 요청
- WebSocket 연결
- WebRTC 세션 관리
- mDNS 이벤트 처리
- 클라이언트 연결 및 종료 이벤트

### 3.3 Axum

Axum은 HTTP API와 WebSocket Signaling 서버를 구현하는 데 사용한다.

### 3.4 WebRTC

WebRTC는 실제 영상 데이터 전송을 담당한다.

Signaling을 통해 SDP 및 ICE 정보를 교환하고, 연결이 완료되면 영상 스트림을 전달한다.

---

## 4. 프로젝트 디렉터리 구조

```text
slimedisplay/
│
├── Cargo.toml
├── Cargo.lock
├── README.md
│
├── native/
│   ├── include/
│   │   ├── slimedisplay.h
│   │   └── slimedisplay_types.h
│   │
│   ├── windows/
│   │   └── SlimeDisplay.Native.dll
│   │
│   └── macos/
│       └── libslimedisplay.dylib
│
├── src/
│   ├── main.rs
│   ├── lib.rs
│   │
│   ├── api/
│   │   ├── mod.rs
│   │   ├── routes.rs
│   │   └── handlers.rs
│   │
│   ├── display/
│   │   ├── mod.rs
│   │   ├── manager.rs
│   │   └── config.rs
│   │
│   ├── streaming/
│   │   ├── mod.rs
│   │   ├── manager.rs
│   │   └── frame.rs
│   │
│   ├── webrtc/
│   │   ├── mod.rs
│   │   ├── peer.rs
│   │   ├── session.rs
│   │   └── signaling.rs
│   │
│   ├── discovery/
│   │   ├── mod.rs
│   │   └── mdns.rs
│   │
│   ├── security/
│   │   ├── mod.rs
│   │   └── auth.rs
│   │
│   ├── ffi/
│   │   ├── mod.rs
│   │   ├── bindings.rs
│   │   └── native.rs
│   │
│   └── config/
│       ├── mod.rs
│       └── settings.rs
│
├── frontend/
│   ├── package.json
│   ├── vite.config.ts
│   └── src/
│
└── tests/
    ├── display_test.rs
    ├── streaming_test.rs
    └── signaling_test.rs
```

### 4.1 디렉터리 역할

| 디렉터리 | 역할 |
|---|---|
| `api/` | HTTP API 및 WebSocket 엔드포인트 |
| `display/` | 가상 디스플레이 관리 |
| `streaming/` | 영상 프레임 처리 및 스트리밍 관리 |
| `webrtc/` | PeerConnection 및 Signaling 관리 |
| `discovery/` | mDNS 기반 호스트 검색 |
| `security/` | 인증 및 연결 권한 관리 |
| `ffi/` | C ABI를 통한 네이티브 라이브러리 호출 |
| `config/` | 서버 설정 |
| `native/` | 플랫폼별 네이티브 라이브러리 및 헤더 |
| `frontend/` | 클라이언트 웹 UI |

---

## 5. Native Library와 Rust FFI

### 5.1 설계 방향

운영체제별 가상 디스플레이 및 화면 캡처 기능은 네이티브 라이브러리에서 처리한다.

Rust 서버는 해당 라이브러리의 내부 구현을 직접 알 필요가 없다. 공개된 C ABI만 사용하여 기능을 호출한다.

```text
Rust Server
     |
     | C ABI / FFI
     v
SlimeDisplay Native Library
     |
     +-- Windows Native Implementation
     |
     +-- macOS Native Implementation
```

이 구조를 사용하면 플랫폼별 구현을 분리하면서 서버의 공통 로직을 재사용할 수 있다.

### 5.2 공개 C ABI

현재 공개 헤더는 다음 두 파일로 구성된다.

- `slimedisplay_types.h`
- `slimedisplay.h`

`slimedisplay_types.h`는 공통 자료형을 정의하고, `slimedisplay.h`는 외부에서 호출할 수 있는 함수를 정의한다.

### 5.3 주요 자료형

#### SD_Handle

```c
typedef struct SD_Handle SD_Handle;
```

`SD_Handle`은 내부 구현을 외부에 노출하지 않는 불투명 핸들(Opaque Handle)이다.

Rust 서버는 이 핸들을 통해 네이티브 라이브러리의 인스턴스를 관리한다.

#### SD_Result

```c
typedef enum {
    SD_SUCCESS = 0,

    SD_ERROR_INVALID_ARGUMENT = -1,
    SD_ERROR_INITIALIZATION = -2,
    SD_ERROR_NOT_SUPPORTED = -3,
    SD_ERROR_PERMISSION_DENIED = -4,
    SD_ERROR_CAPTURE = -5,
    SD_ERROR_ENCODING = -6,
    SD_ERROR_TRANSPORT = -7,
    SD_ERROR_INVALID_STATE = -8,
    SD_ERROR_INTERNAL = -9
} SD_Result;
```

모든 주요 API는 `SD_Result`를 반환하여 작업의 성공 여부와 오류 종류를 전달한다.

단, `sd_destroy`는 `void`를 반환한다.

#### SD_DisplayConfig

```c
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t fps;
} SD_DisplayConfig;
```

가상 디스플레이 생성에 필요한 설정이다.

#### SD_DisplayInfo

```c
typedef struct {
    uint64_t id;
    uint32_t width;
    uint32_t height;
    uint32_t refresh_rate;
} SD_DisplayInfo;
```

생성된 가상 디스플레이의 정보를 나타낸다.

#### SD_PixelFormat

```c
typedef enum {
    SD_PIXEL_FORMAT_UNKNOWN = 0,
    SD_PIXEL_FORMAT_BGRA = 1,
    SD_PIXEL_FORMAT_NV12 = 2,
    SD_PIXEL_FORMAT_I420 = 3
} SD_PixelFormat;
```

지원하는 픽셀 포맷을 나타낸다.

#### SD_FrameInfo

```c
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    SD_PixelFormat format;
    uint64_t timestamp;
} SD_FrameInfo;
```

프레임의 기본 정보를 표현하는 자료형이다.

현재 공개된 인코딩 프레임 콜백에는 `SD_FrameInfo`가 직접 전달되지 않는다.

### 5.4 공개 API

현재 `slimedisplay.h`에 정의된 API는 다음과 같다.

| 함수 | 반환 타입 | 역할 |
|---|---|---|
| `sd_create` | `SD_Result` | 핸들 생성 |
| `sd_destroy` | `void` | 핸들 제거 |
| `sd_create_virtual_display` | `SD_Result` | 가상 디스플레이 생성 |
| `sd_destroy_virtual_display` | `SD_Result` | 가상 디스플레이 제거 |
| `sd_get_virtual_display_info` | `SD_Result` | 디스플레이 정보 조회 |
| `sd_start_capture` | `SD_Result` | 화면 캡처 시작 |
| `sd_stop_capture` | `SD_Result` | 화면 캡처 중지 |
| `sd_start_encoder` | `SD_Result` | 인코더 시작 |
| `sd_stop_encoder` | `SD_Result` | 인코더 중지 |
| `sd_get_version` | `uint32_t` | 라이브러리 버전 조회 |
| `sd_set_encoded_frame_callback` | `SD_Result` | 인코딩 프레임 콜백 등록 |

### 5.5 Rust FFI 바인딩

Rust에서는 C ABI를 사용하여 네이티브 라이브러리의 함수를 호출한다.

바인딩은 `bindgen`으로 생성하거나 C 헤더와 동기화하여 직접 관리할 수 있다.

개념적인 Rust 선언은 다음과 같다.

```rust
use std::ffi::c_void;

#[repr(C)]
pub struct SD_Handle {
    _private: [u8; 0],
}

#[repr(C)]
pub struct SD_DisplayConfig {
    pub width: u32,
    pub height: u32,
    pub fps: u32,
}

#[repr(C)]
pub struct SD_DisplayInfo {
    pub id: u64,
    pub width: u32,
    pub height: u32,
    pub refresh_rate: u32,
}

pub type SD_EncodedFrameCallback = Option<
    unsafe extern "C" fn(
        data: *const u8,
        size: u32,
        presentation_time: i64,
        is_keyframe: i32,
        user_data: *mut c_void,
    ),
>;

unsafe extern "C" {
    pub fn sd_create(handle: *mut *mut SD_Handle) -> i32;

    pub fn sd_destroy(handle: *mut SD_Handle);

    pub fn sd_create_virtual_display(
        handle: *mut SD_Handle,
        config: *const SD_DisplayConfig,
    ) -> i32;

    pub fn sd_destroy_virtual_display(
        handle: *mut SD_Handle,
    ) -> i32;

    pub fn sd_get_virtual_display_info(
        handle: *mut SD_Handle,
        info: *mut SD_DisplayInfo,
    ) -> i32;

    pub fn sd_start_capture(handle: *mut SD_Handle) -> i32;

    pub fn sd_stop_capture(handle: *mut SD_Handle) -> i32;

    pub fn sd_start_encoder(handle: *mut SD_Handle) -> i32;

    pub fn sd_stop_encoder(handle: *mut SD_Handle) -> i32;

    pub fn sd_get_version() -> u32;

    pub fn sd_set_encoded_frame_callback(
        handle: *mut SD_Handle,
        callback: SD_EncodedFrameCallback,
        user_data: *mut c_void,
    ) -> i32;
}
```

위 코드는 FFI 구조를 설명하기 위한 예시다. 실제 프로젝트에서는 생성된 바인딩을 우선 사용하고, C enum의 ABI 표현 및 함수 선언이 실제 헤더와 일치하는지 확인해야 한다.

Rust Edition 2024에서는 FFI 선언 블록에 `unsafe extern "C"`를 사용한다.

### 5.6 인코딩 프레임 콜백

현재 공개 API는 인코딩된 프레임을 Rust 서버로 전달하기 위해 다음 콜백을 제공한다.

```c
typedef void (*SD_EncodedFrameCallback)(
    const uint8_t* data,
    uint32_t size,
    int64_t presentation_time,
    int32_t is_keyframe,
    void* user_data
);
```

콜백 인자는 다음과 같다.

| 인자 | 타입 | 설명 |
|---|---|---|
| `data` | `const uint8_t*` | 인코딩된 프레임 데이터 |
| `size` | `uint32_t` | 데이터 크기 |
| `presentation_time` | `int64_t` | 프레임 표시 시각 |
| `is_keyframe` | `int32_t` | 키프레임 여부 |
| `user_data` | `void*` | 사용자 정의 컨텍스트 |

Rust에서는 콜백으로 전달된 데이터가 유효한 동안 프레임 데이터를 복사해야 한다.

네이티브 라이브러리가 제공한 포인터는 콜백이 반환된 이후에도 유효하다고 보장되지 않기 때문이다.

권장 데이터 흐름은 다음과 같다.

```text
Native Encoder Thread
        |
        | Encoded Frame Callback
        v
Rust Callback
        |
        | Copy Frame Data
        v
Owned Frame Buffer
        |
        v
Bounded Channel
        |
        v
WebRTC Video Track
```

콜백 내부에서는 다음 작업을 피해야 한다.

- 네트워크 전송
- 오래 걸리는 작업
- 장시간 잠금을 유지하는 작업
- 불필요한 메모리 할당
- 비동기 작업의 직접 실행

콜백은 프레임 데이터를 소유권이 있는 버퍼로 복사한 뒤 제한된 크기의 스레드 안전 채널에 전달하는 역할만 수행하는 것이 바람직하다.

### 5.7 콜백 수명 및 해제

콜백의 `user_data`는 네이티브 라이브러리에서 콜백이 호출될 수 있는 동안 유효해야 한다.

따라서 다음 사항을 보장해야 한다.

1. 콜백 컨텍스트는 콜백이 등록된 동안 유지한다.
2. 인코더를 중지한다.
3. 콜백 해제 기능을 호출한다.
4. 이미 실행 중인 콜백이 모두 종료되었는지 확인한다.
5. 그 이후에 콜백 컨텍스트를 해제한다.

현재 공개 헤더에는 콜백 해제 동작이 명시되어 있지 않다.

따라서 `NULL` 콜백을 전달했을 때 등록이 해제되는지 임의로 가정해서는 안 된다. 네이티브 구현에서 해당 동작을 보장하고 문서화하거나, 별도의 콜백 해제 API를 추가하는 것이 좋다.

### 5.8 권장 초기화 순서

```text
1. sd_create
2. sd_create_virtual_display
3. sd_start_capture
4. sd_set_encoded_frame_callback
5. sd_start_encoder
```

인코더가 시작되기 전에 콜백을 등록하여 초기 프레임이 유실되는 상황을 방지한다.

### 5.9 권장 종료 순서

```text
1. sd_stop_encoder
2. Unregister Callback
3. Wait for In-flight Callbacks
4. sd_stop_capture
5. sd_destroy_virtual_display
6. sd_destroy
```

콜백 해제 및 실행 중인 콜백의 종료를 보장한 뒤 관련 메모리를 해제해야 한다.

---

## 6. Rust Server 모듈 설계

### 6.1 DisplayService

가상 디스플레이의 생명주기를 관리한다.

주요 기능:

- 디스플레이 생성
- 디스플레이 제거
- 디스플레이 정보 조회
- 캡처 시작 및 중지
- 인코더 시작 및 중지

내부적으로 FFI 모듈을 호출한다.

### 6.2 StreamManager

영상 스트리밍의 전체 흐름을 관리한다.

주요 기능:

- 인코딩 프레임 수신
- 프레임 버퍼 관리
- 클라이언트별 스트림 관리
- WebRTC Track에 프레임 전달
- 스트리밍 시작 및 종료

### 6.3 PeerConnectionManager

WebRTC PeerConnection을 관리한다.

주요 기능:

- PeerConnection 생성
- SDP Offer / Answer 처리
- ICE Candidate 교환
- 연결 상태 모니터링
- 연결 종료 및 리소스 해제

### 6.4 SignalingService

WebSocket을 이용한 Signaling을 처리한다.

주요 기능:

- 클라이언트 연결 관리
- 세션 생성 및 종료
- SDP 교환
- ICE Candidate 전달
- 연결 상태 이벤트 전달

### 6.5 DiscoveryService

mDNS를 사용하여 로컬 네트워크에서 SlimeDisplay Host를 검색할 수 있도록 한다.

주요 기능:

- 서비스 등록
- 서비스 검색
- Host 정보 광고
- 서비스 종료 및 등록 해제

### 6.6 AuthenticationService

클라이언트 연결에 대한 인증 및 권한 관리를 담당한다.

주요 기능:

- 클라이언트 인증
- 세션 접근 권한 확인
- 연결 요청 검증
- 비인가 연결 차단

---

## 7. 네트워크 아키텍처

### 7.1 전체 통신 구조

```text
Client                         Host
  |                              |
  |------ HTTP Request --------->|
  |                              |
  |<----- Host Information ------|
  |                              |
  |------ WebSocket Connect ---->|
  |                              |
  |<------ SDP Offer/Answer ---->|
  |<------ ICE Candidates ------>|
  |                              |
  |======== WebRTC Video =======>|
  |                              |
```

### 7.2 HTTP API

초기 API는 다음과 같이 구성할 수 있다.

| Method | Endpoint | 설명 |
|---|---|---|
| `GET` | `/api/info` | 서버 정보 조회 |
| `GET` | `/api/display` | 디스플레이 정보 조회 |
| `POST` | `/api/display` | 가상 디스플레이 생성 |
| `DELETE` | `/api/display` | 가상 디스플레이 제거 |
| `POST` | `/api/stream/start` | 스트리밍 시작 |
| `POST` | `/api/stream/stop` | 스트리밍 중지 |

위 항목은 Rust 서버에서 제공할 HTTP 엔드포인트의 설계 예시이며, 네이티브 C ABI 함수와는 별개의 인터페이스다.

### 7.3 WebSocket Signaling

WebSocket은 클라이언트와 서버 간 연결 설정 정보를 교환하는 데 사용한다.

주요 메시지:

- `offer`
- `answer`
- `ice-candidate`
- `join`
- `leave`
- `error`

WebSocket은 Signaling에 사용하고, 실제 영상 데이터는 WebRTC를 통해 전송한다.

---

## 8. mDNS Discovery

### 8.1 목적

클라이언트가 같은 로컬 네트워크에 있는 SlimeDisplay Host를 자동으로 검색할 수 있도록 한다.

별도의 IP 주소 입력 없이 Host를 발견하고 연결하는 것이 목표다.

### 8.2 동작 방식

Host는 mDNS를 통해 SlimeDisplay 서비스를 광고한다.

```text
Service Type:
_sl ime-display._tcp.local
```

실제 서비스 타입은 프로젝트의 고정된 규칙으로 정의해야 하며, 위 표기는 개념적인 예시다.

서비스 정보에는 다음 항목을 포함할 수 있다.

- Host 이름
- IP 주소
- Signaling 포트
- 서버 버전
- 지원 기능

### 8.3 Rust 구현

`mdns-sd` 라이브러리를 사용하여 서비스 등록 및 검색을 구현한다.

운영체제별 mDNS 구현에 직접 의존하지 않고 공통 인터페이스를 유지한다.

---

## 9. 플랫폼별 빌드 및 라이브러리 연결

### 9.1 Windows

Windows에서는 플랫폼별 네이티브 라이브러리를 DLL 형태로 제공한다.

```text
SlimeDisplay/
├── native/
│   ├── include/
│   │   ├── slimedisplay.h
│   │   └── slimedisplay_types.h
│   └── windows/
│       └── SlimeDisplay.Native.dll
```

Rust 서버는 DLL을 로드하거나 링크하여 C ABI 함수를 호출한다.

Windows 빌드에서는 DLL과 import library의 배치, 런타임 의존성, 아키텍처(x64 등)를 일치시켜야 한다.

### 9.2 macOS

macOS에서는 네이티브 라이브러리를 동적 라이브러리 형태로 제공한다.

```text
native/
└── macos/
    └── libslimedisplay.dylib
```

Rust 서버는 해당 라이브러리를 링크하거나 런타임에 로드한다.

macOS의 가상 디스플레이 및 화면 캡처 기능은 macOS 전용 구현에서 처리한다.

### 9.3 플랫폼 추상화

Rust 서버는 Windows 또는 macOS의 구체적인 API를 직접 호출하지 않는다.

플랫폼별 차이는 Native Display Library 내부에서 처리하고, Rust 서버에는 동일한 C ABI를 제공한다.

---

## 10. 런타임 생명주기

### 10.1 서버 시작

```text
Server Start
     |
     v
Load Configuration
     |
     v
Initialize Native Handle
     |
     v
Create Virtual Display
     |
     v
Start Capture
     |
     v
Register Encoded Frame Callback
     |
     v
Start Encoder
     |
     v
Start HTTP / WebSocket Server
     |
     v
Start mDNS Discovery
```

### 10.2 클라이언트 연결

```text
Client Connect
     |
     v
WebSocket Signaling
     |
     v
Create PeerConnection
     |
     v
Exchange SDP
     |
     v
Exchange ICE Candidates
     |
     v
WebRTC Connected
     |
     v
Send Encoded Video Frames
```

### 10.3 서버 종료

```text
Stop Accepting Connections
     |
     v
Close Client Sessions
     |
     v
Stop Encoder
     |
     v
Unregister Callback
     |
     v
Wait for Active Callbacks
     |
     v
Stop Capture
     |
     v
Destroy Virtual Display
     |
     v
Destroy Native Handle
     |
     v
Shutdown Server
```

실제 구현에서는 각 단계의 실패 및 부분 초기화 상태도 고려해야 한다. 이미 생성된 리소스는 실패가 발생하더라도 안전하게 정리할 수 있도록 Rust의 소유권 및 RAII 패턴을 활용한다.

---

## 11. 성능 및 안정성 요구사항

### 11.1 영상 처리

- 실시간 영상 스트리밍 지원
- 불필요한 프레임 복사 최소화
- 프레임 버퍼 재사용
- 제한된 크기의 프레임 큐 사용
- 지연이 누적될 경우 오래된 프레임 처리 정책 적용

### 11.2 네트워크

- WebRTC를 통한 저지연 영상 전송
- ICE를 통한 연결 설정
- 네트워크 연결 상태 모니터링
- 연결 해제 및 재연결 처리

### 11.3 메모리 및 스레드 안전성

- FFI 포인터의 수명 관리
- 콜백 컨텍스트의 안전한 해제
- 콜백 내부의 불필요한 작업 최소화
- 공유 상태에 대한 동시 접근 제어
- 스트리밍 종료 시 관련 리소스 정리

### 11.4 인코딩 및 WebRTC 연동

현재 C ABI는 인코딩된 프레임 데이터를 전달하지만, WebRTC에 바로 넣을 수 있는 형식인지는 실제 인코더 출력 형식에 따라 달라진다.

다음 항목을 별도로 확인해야 한다.

- H.264 출력 형식 (Annex B 또는 AVCC)
- SPS/PPS 전달 방식
- RTP H.264 패킷화
- Presentation Timestamp의 시간 단위
- 키프레임 처리
- WebRTC의 코덱 및 프로파일 협상

현재 공개 ABI에는 코덱, 비트레이트, 키프레임 간격 등을 설정하는 함수가 포함되어 있지 않으므로, 이 부분은 향후 API 확장 대상이다.

---

## 12. 향후 C ABI 확장

현재 ABI는 디스플레이 생성, 캡처, 인코더 실행 및 인코딩 프레임 콜백에 필요한 기본 기능을 제공한다.

다만 스트리밍 설정을 유연하게 관리하려면 다음 기능을 추가하는 것이 좋다.

| 기능 | 설명 |
|---|---|
| Codec Selection | H.264 등 코덱 선택 |
| Bitrate Configuration | 인코딩 비트레이트 설정 |
| Encoding Resolution | 인코딩 해상도 설정 |
| Target FPS | 목표 프레임 레이트 설정 |
| Keyframe Interval | 키프레임 간격 설정 |
| Profile / Level | H.264 프로파일 및 레벨 설정 |
| Request Keyframe | 키프레임 요청 |
| Capability Query | 플랫폼별 지원 기능 조회 |

이러한 기능은 기존 ABI와의 호환성을 고려하여 별도의 설정 구조체 또는 확장 API로 추가하는 것이 바람직하다.

---

## 13. 개발 단계

### Phase 1 – Native Library Integration

- C ABI 헤더 확정
- Rust FFI 바인딩 생성
- `sd_create` 및 `sd_destroy` 연동
- 가상 디스플레이 생성 및 제거
- 디스플레이 정보 조회

### Phase 2 – Capture and Encoding

- 화면 캡처 시작 및 중지
- 인코더 시작 및 중지
- 인코딩 프레임 콜백 등록
- Rust에서 프레임 수신
- 콜백 및 메모리 수명 검증

### Phase 3 – WebRTC

- PeerConnection 구현
- H.264 영상 트랙 연결
- 프레임 전달
- SDP 및 ICE 처리
- 클라이언트 영상 재생 확인

### Phase 4 – Signaling Server

- Axum HTTP 서버
- WebSocket 연결
- 클라이언트 세션 관리
- Signaling 메시지 구현

### Phase 5 – Discovery

- mDNS 서비스 등록
- 클라이언트 검색 기능
- Host 정보 전달

### Phase 6 – Client Integration

- 웹 클라이언트 구현
- Host 검색 및 연결
- WebRTC 영상 수신
- 스트리밍 제어 UI

### Phase 7 – Optimization

- 프레임 복사 최소화
- 지연 시간 측정
- 다중 클라이언트 지원
- 연결 안정성 개선
- 플랫폼별 성능 최적화

---

## 14. 최종 설계 방향

SlimeDisplay는 다음 구조를 최종 아키텍처로 사용한다.

**Shared Rust Server + Platform-specific Native Library + WebRTC Client**

- Rust Server는 플랫폼에 독립적인 네트워크 및 스트리밍 로직을 담당한다.
- Native Library는 Windows 및 macOS의 가상 디스플레이, 화면 캡처, 영상 인코딩을 담당한다.
- 두 구성 요소는 C ABI를 통해 통신한다.
- WebRTC는 클라이언트로 영상 데이터를 전송한다.
- WebSocket은 Signaling을 담당한다.
- mDNS는 로컬 네트워크에서 Host를 자동으로 검색하는 데 사용한다.

이 구조를 통해 운영체제별 구현을 분리하면서도 서버의 공통 로직과 클라이언트 연결 구조를 재사용할 수 있다.