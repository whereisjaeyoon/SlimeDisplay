# SlimeDisplay for Windows

## 1. 프로젝트 개요

Windows 버전 SlimeDisplay는 Windows의 Indirect Display Driver (IDD)를 이용해 가상 디스플레이를 생성하고, 해당 디스플레이의 프레임을 캡처하여 인코딩하는 모듈이다.

macOS 버전과 동일한 C ABI를 제공하여 상위 C++ Server가 플랫폼에 관계없이 동일한 인터페이스로 가상 디스플레이를 제어할 수 있도록 한다.

### 개발 환경

| 항목 | 기술 |
|---|---|
| IDE | Visual Studio 2022 |
| Language | C++20 |
| Compiler | MSVC |
| Build System | MSBuild / CMake (추후 통합 가능) |
| Display Driver | Windows Indirect Display Driver |
| Driver Framework | UMDF 2 / IddCx |
| Graphics API | Direct3D 11 |
| Encoder | Media Foundation |
| Architecture | x64 |
| Public Interface | C ABI |

## 2. 전체 아키텍처

```text
                  SlimeDisplay Server
                           |
                           | C ABI
                           v
                 SlimeDisplay.dll
                           |
             +-------------+-------------+
             |             |             |
             v             v             v
       DisplayManager  CaptureManager  EncoderManager
             |             |             |
             |             |             |
             v             v             v
          IDD Driver   D3D11 Surface  Media Foundation
             |
             v
      Windows Display Stack
             |
             v
       Virtual Monitor
```

### 모듈별 책임

**SlimeDisplay.dll**

- C ABI 제공
- 가상 디스플레이 생성 및 제거 요청
- 디스플레이 상태 관리
- 프레임 캡처 및 인코더 제어
- 인코딩된 프레임을 외부 콜백으로 전달

**SlimeDisplayDriver**

- Windows IDD 기반 가상 모니터 등록
- 디스플레이 모드 및 해상도 관리
- OS에서 전달하는 SwapChain 프레임 수신
- Direct3D Surface 제공

**CaptureManager**

- IDD SwapChain에서 프레임 수신
- GPU Surface 처리
- 인코더로 프레임 전달

**EncoderManager**

- Media Foundation 기반 하드웨어 인코딩
- H.264 등 지원 코덱으로 인코딩
- 인코딩된 프레임을 DLL 콜백으로 전달

## 3. 프로젝트 디렉터리 구조

```text
SlimeDisplay/
├── include/
│   ├── slimedisplay.h
│   └── slimedisplay_types.h
│
├── windows/
│   ├── SlimeDisplay.sln
│   │
│   ├── SlimeDisplay/
│   │   ├── SlimeDisplay.vcxproj
│   │   ├── SDAPI.cpp
│   │   ├── SDManager.h
│   │   ├── SDManager.cpp
│   │   │
│   │   ├── display/
│   │   │   ├── DisplayManager.h
│   │   │   └── DisplayManager.cpp
│   │   │
│   │   ├── capture/
│   │   │   ├── CaptureManager.h
│   │   │   └── CaptureManager.cpp
│   │   │
│   │   └── encoder/
│   │       ├── EncoderManager.h
│   │       └── EncoderManager.cpp
│   │
│   ├── SlimeDisplayDriver/
│   │   ├── Driver.cpp
│   │   ├── Driver.h
│   │   ├── Direct3DDevice.cpp
│   │   ├── IndirectDeviceContext.cpp
│   │   ├── IndirectMonitorContext.cpp
│   │   ├── SwapChainProcessor.cpp
│   │   ├── Device.tmh
│   │   └── SlimeDisplayDriver.vcxproj
│   │
│   └── SlimeDisplayTest/
│       ├── main.cpp
│       └── SlimeDisplayTest.vcxproj
│
└── README.md
```

`SlimeDisplayDriver`는 Microsoft IDD Sample을 기반으로 구현한다. DLL과 드라이버는 별도의 프로젝트로 빌드하고 배포한다.

## 4. DLL Public API

공통 헤더인 `slimedisplay.h`와 `slimedisplay_types.h`를 그대로 사용한다.

주요 API:

| API | 역할 |
|---|---|
| `sd_create` | SlimeDisplay 인스턴스 생성 |
| `sd_destroy` | 인스턴스 제거 |
| `sd_create_virtual_display` | 가상 디스플레이 생성 |
| `sd_destroy_virtual_display` | 가상 디스플레이 제거 |
| `sd_get_virtual_display_info` | 디스플레이 정보 조회 |
| `sd_start_capture` | 프레임 캡처 시작 |
| `sd_stop_capture` | 프레임 캡처 중지 |
| `sd_start_encoder` | 인코더 시작 |
| `sd_stop_encoder` | 인코더 중지 |
| `sd_set_encoded_frame_callback` | 인코딩 프레임 콜백 등록 |
| `sd_get_version` | DLL 버전 조회 |

### API 구현 원칙

- 외부 인터페이스는 C ABI로 유지한다.
- 내부 구현은 C++ 클래스로 분리한다.
- `SD_Handle`은 내부 `SDManager` 객체를 감추는 opaque handle로 사용한다.
- C++ 예외가 C ABI 경계를 넘어가지 않도록 한다.
- 콜백은 별도 인코딩 처리 경로에서 호출하고 네트워크 전송 때문에 블로킹되지 않도록 한다.
- 콜백으로 전달되는 프레임 메모리의 유효 기간을 명확히 정의한다.

## 5. IDD Driver

Windows의 가상 디스플레이는 일반적인 DLL만으로 생성할 수 없다. OS에 디스플레이 장치를 등록하기 위해 Indirect Display Driver가 필요하다.

IDD는 UMDF 기반의 사용자 모드 드라이버이며, IddCx를 통해 Windows Display Stack과 통신한다.

### 주요 구성 요소

| 구성 요소 | 역할 |
|---|---|
| `Direct3DDevice` | Direct3D 디바이스 관리 |
| `IndirectDeviceContext` | 간접 디스플레이 장치 관리 |
| `IndirectMonitorContext` | 가상 모니터 및 디스플레이 모드 관리 |
| `SwapChainProcessor` | OS가 전달하는 프레임 처리 |

### 프레임 처리 경로

```text
Windows Desktop Composition
            |
            v
     IDD SwapChain
            |
            v
   SwapChainProcessor
            |
            v
    Direct3D Surface
            |
            v
      CaptureManager
            |
            v
   Media Foundation Encoder
            |
            v
     Encoded Frame
            |
            v
   SD_EncodedFrameCallback
```

가능하면 GPU Surface를 CPU 메모리로 불필요하게 복사하지 않고 인코더까지 전달하는 GPU 기반 경로를 우선한다.

## 6. Visual Studio 2022 프로젝트 설정

### SlimeDisplay DLL

Visual Studio에서 다음 설정으로 프로젝트를 생성한다.

- Create a new project
- Dynamic-Link Library (DLL)
- C++
- Project name: `SlimeDisplay`
- Platform: x64
- Language Standard: C++20

주요 속성:

| 설정 | 값 |
|---|---|
| Configuration Type | Dynamic Library (.dll) |
| C++ Language Standard | ISO C++20 |
| Character Set | Use Unicode |
| Runtime Library (Debug) | Multi-threaded Debug DLL (/MDd) |
| Runtime Library (Release) | Multi-threaded DLL (/MD) |
| Precompiled Header | Not Using |
| Target Architecture | x64 |

Preprocessor Definitions:

```text
SLIMEDISPLAY_EXPORTS
WIN32_LEAN_AND_MEAN
NOMINMAX
```

Additional Include Directories:

```text
$(SolutionDir)..\include
```

### IDD Driver

IDD Driver 프로젝트는 Microsoft의 Windows Driver Samples 저장소에 있는 Indirect Display Driver Sample을 기반으로 구성한다.

필요한 개발 구성 요소:

- Visual Studio 2022
- Windows SDK
- Windows Driver Kit (WDK)
- UMDF 및 IddCx 개발 지원

드라이버 프로젝트는 일반 DLL 프로젝트와 별도로 구성한다.

## 7. 개발 단계

### Phase 1 — DLL 기본 구조

- [ ] Visual Studio Solution 생성
- [ ] SlimeDisplay DLL 프로젝트 생성
- [ ] 공통 헤더 연결
- [ ] `SDManager` 구현
- [ ] `sd_create` 구현
- [ ] `sd_destroy` 구현
- [ ] `sd_get_version` 구현
- [ ] DLL 로딩 테스트

### Phase 2 — IDD Driver

- [ ] Microsoft IDD Sample 빌드
- [ ] 테스트 환경에서 드라이버 설치
- [ ] Software Device 생성 및 드라이버 연결
- [ ] 가상 모니터 생성 확인
- [ ] 해상도 및 디스플레이 모드 설정
- [ ] SwapChain 프레임 수신 확인

### Phase 3 — Capture

- [ ] SwapChainProcessor와 CaptureManager 연결
- [ ] Direct3D Surface 전달
- [ ] 프레임 처리 및 동기화
- [ ] GPU 리소스 관리
- [ ] Capture Start / Stop 구현

### Phase 4 — Encoder

- [ ] Media Foundation 초기화
- [ ] H.264 인코더 연결
- [ ] 하드웨어 인코딩 지원 확인
- [ ] 인코딩 프레임 콜백 구현
- [ ] 프레임 타임스탬프 처리

### Phase 5 — 통합 테스트

- [ ] DLL API 전체 테스트
- [ ] 가상 디스플레이 생성 및 제거 반복 테스트
- [ ] FHD 60 FPS 캡처
- [ ] 인코딩 프레임 전달 확인
- [ ] 장시간 실행 안정성 테스트
- [ ] Server와 C ABI 연동

## 8. 테스트 및 배포 고려사항

- IDD 드라이버 설치에는 드라이버 패키지와 INF 구성이 필요하다.
- 개발 환경에서는 테스트 서명 및 드라이버 설치 정책을 확인해야 한다.
- 일반적인 사용자 환경에 배포할 때는 드라이버 서명 및 설치 절차를 별도로 준비해야 한다.
- 드라이버와 DLL의 버전 호환성을 관리해야 한다.
- 드라이버 제거 시 가상 모니터 및 관련 리소스가 정상적으로 해제되어야 한다.

## 9. 참고 자료

- [Microsoft Windows Driver Samples — Indirect Display Driver](https://github.com/microsoft/Windows-driver-samples/tree/main/video/IndirectDisplay)
- [Microsoft Learn — Indirect Display Driver Model](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/indirect-display-driver-model-overview)
- [Microsoft Learn — IddCx](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/iddcx/)
- [Microsoft Learn — Media Foundation](https://learn.microsoft.com/en-us/windows/win32/medfound/microsoft-media-foundation-sdk)