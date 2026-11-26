# SlimeDisplay for Windows

## 1. 프로젝트 개요

Windows 버전 SlimeDisplay는 기존 [Virtual Display Driver (VDD)](https://github.com/VirtualDrivers/Virtual-Display-Driver)가 제공하는 가상 모니터를 선택해 프레임을 캡처하고 인코딩한다. SlimeDisplay 자체 IDD를 구현하거나 설치하지 않는다.

macOS 버전과 동일한 C ABI를 제공하여 상위 C++ Server가 플랫폼에 관계없이 동일한 인터페이스로 가상 디스플레이를 제어할 수 있도록 한다.

### 개발 환경

| 항목 | 기술 |
|---|---|
| IDE | Visual Studio 18 (v145 도구 집합) |
| Language | C++20 |
| Compiler | MSVC |
| Build System | MSBuild |
| Display Driver | VirtualDrivers Virtual Display Driver (별도 설치) |
| 디스플레이 연동 | Windows 디스플레이 구성 API |
| 캡처 API | DXGI Desktop Duplication (GDI 대체 경로) |
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
             |                           |
             v                           v
       DisplayManager               CaptureManager
             |                           |
             v                           v
 Windows Display APIs       DXGI Desktop Duplication
             |                           |
             v                           v
 기존 VirtualDrivers VDD       D3D11 Video Processor
             |                           |
             v                           v
        가상 모니터              NV12 staging readback
                                         |
                                         v
                                   EncoderManager
                                         |
                                         v
                                Media Foundation
```

### 모듈별 책임

**SlimeDisplay.dll**

- C ABI 제공
- 기존 VDD 가상 모니터 선택 및 설정
- 사용 가능한 VDD 모니터가 없으면 `SD_ERROR_NOT_SUPPORTED` 반환
- 디스플레이 상태 관리
- 프레임 캡처 및 인코더 제어
- 인코딩된 프레임을 외부 콜백으로 전달

**VirtualDrivers VDD (외부 의존성)**

- Windows Display Stack에 가상 모니터 등록 및 제공
- 자체 설치 프로그램과 Virtual Driver Control로 설정 및 업데이트
- SlimeDisplay 전용 C API는 제공하지 않으며 DLL은 Windows 디스플레이·캡처 API 사용

**CaptureManager**

- DXGI Desktop Duplication으로 선택된 VDD 모니터 캡처
- 지원되면 D3D11 Video Processor로 GPU에서 NV12 변환 후 CPU로 읽기
- Desktop Duplication을 사용할 수 없으면 GDI로 대체
- GPU 변환을 쓸 수 없으면 BGRA 프레임으로 대체
- 인코더로 프레임 전달

**EncoderManager**

- Media Foundation H.264 하드웨어 인코더 우선 사용, 불가하면 동기식
  Media Foundation 인코더 사용
- NV12 프레임을 우선 사용하고, BGRA 대체 경로는 CPU에서 NV12로 변환
- 제한된 큐로 지연을 제어
- 인코딩된 프레임을 DLL 콜백으로 전달

## 3. 활성 솔루션 구조

```text
SlimeDisplay/
├── include/
│   ├── slimedisplay.h
│   └── slimedisplay_types.h
│
└── SlimeDisplay-windows-core/
    ├── README.md
    ├── README-kr.md
    ├── SlimeDisplay.sln
    │
    ├── SlimeDisplay-windows-core/
    │   ├── SlimeDisplay-windows-core.vcxproj
    │   ├── SDAPI.cpp
    │   ├── SDManager.h
    │   ├── SDManager.cpp
    │   ├── display/
    │   │   ├── DisplayManager.h
    │   │   └── DisplayManager.cpp
    │   ├── capture/
    │   │   ├── CaptureManager.h
    │   │   └── CaptureManager.cpp
    │   └── encoder/
    │       ├── EncoderManager.h
    │       └── EncoderManager.cpp
    │
    └── SlimeDisplayTest/
        ├── SlimeDisplayTest.vcxproj
        └── main.cpp
```

공통 헤더는 저장소 루트의 `include/` 디렉터리에 있으며 DLL 프로젝트에서 참조한다. 외부 VDD는 이 저장소에 포함되지 않으며 별도로 설치한다.

## 4. DLL Public API

공통 헤더인 `slimedisplay.h`와 `slimedisplay_types.h`를 그대로 사용한다.

주요 API:

| API | 역할 |
|---|---|
| `sd_create` | SlimeDisplay 인스턴스 생성 |
| `sd_destroy` | 인스턴스 제거 |
| `sd_create_virtual_display` | 기존 VDD 모니터 선택 및 설정 |
| `sd_destroy_virtual_display` | 선택한 모니터 해제 (VDD 설치 제거는 하지 않음) |
| `sd_get_virtual_display_info` | 디스플레이 정보 조회 |
| `sd_start_capture` | 프레임 캡처 시작 |
| `sd_stop_capture` | 프레임 캡처 중지 |
| `sd_start_encoder` | 인코더 시작 |
| `sd_stop_encoder` | 인코더 중지 |
| `sd_set_encoded_frame_callback` | 인코딩 프레임 콜백 등록 |
| `sd_get_version` | DLL 버전 조회 |

### API 구현 원칙

- 외부 인터페이스는 C ABI로 유지한다.
- macOS 공개 `slimedisplay.h`와 동일한 함수 시그니처를 내보낸다.
- 내부 구현은 C++ 클래스로 분리한다.
- `SD_Handle`은 내부 `SDManager` 객체를 감추는 opaque handle로 사용한다.
- C++ 예외가 C ABI 경계를 넘어가지 않도록 한다.
- 콜백은 별도 인코딩 처리 경로에서 호출하고 네트워크 전송 때문에 블로킹되지 않도록 한다.
- 콜백으로 전달되는 프레임 메모리의 유효 기간을 명확히 정의한다.

Windows DLL은 공통 C ABI를 내보낸다. 이미 설치된 VDD 모니터를 선택해
지원 모드를 적용하고, 디스플레이를 해제할 때 원래 모드로 복원한다.
캡처는 Desktop Duplication을 우선 사용하고 사용할 수 없으면 GDI로
대체한다. H.264 인코딩은 Media Foundation 하드웨어 변환기를 우선 사용하며,
사용할 수 없으면 동기식 Media Foundation 변환기를 사용한다.

인코딩 콜백에 전달되는 H.264 데이터 버퍼는 콜백 실행 중에만 유효하다.
Windows smoke test는 VDD의 현재 모드를 사용하고 디스플레이 정보 및
인코딩 프레임 전달을 확인한다. 공통 라이브러리 버전 규칙이 정해지기
전까지 `sd_get_version`은 `0`을 반환한다.
콜백은 인코더 worker thread에서 실행된다. 다른 thread에서
`sd_stop_encoder`가 반환할 때까지 콜백 코드와 `user_data`를 유효하게
유지하고, 콜백 안에서 handle을 제거하지 않는다.

## 5. 기존 Virtual Display Driver 사용

새 IDD를 만드는 대신 기존 [VirtualDrivers Virtual Display Driver](https://github.com/VirtualDrivers/Virtual-Display-Driver)를 사용한다. 공식 설치 프로그램 또는 Virtual Driver Control로 VDD를 별도 설치·설정하고, SlimeDisplay를 실행하기 전에 가상 모니터를 하나 이상 활성화한다.

VDD는 가상 모니터를 Windows에 등록하고 지원 해상도를 제공한다. SlimeDisplay는 Windows 디스플레이 API를 통해 이미 존재하는 VDD 모니터를 열거하고 선택해야 한다. VDD는 SlimeDisplay 전용 C ABI를 제공하지 않으며, 모니터 수와 지원 모드는 VDD의 자체 제어 도구 및 설정으로 관리한다.

SlimeDisplay DLL에서 별도의 IDD를 설치·빌드·관리하지 않는다. 드라이버 설치, 업데이트, 서명 및 제거는 VDD 패키지가 담당한다.

### 프레임 처리 경로

```text
기존 VDD 가상 모니터
            |
            v
 DXGI Desktop Duplication
            |
            v
 D3D11 Video Processor
            |
            v
  NV12 staging texture
            |
            v
    NV12 frame (CPU)
             |
             v
 Media Foundation H.264
            |
            v
     Encoded Frame
            |
            v
   SD_EncodedFrameCallback
```

DXGI 경로는 지원되는 GPU에서 BGRA-to-NV12 색 변환을 GPU에서 수행하고
NV12 프레임만 CPU로 읽는다. GPU 비디오 프로세서가 없으면 BGRA readback과
CPU 변환으로 대체한다. 현재는 NV12 텍스처를 Media Foundation 인코더까지
무복사로 전달하지 않으며, VDD 드라이버 내부 SwapChain에도 직접 접근하지
않는다. 스트리밍 테스트 뷰어는 지원되는 GPU에서 NV12를 DXGI swap chain에
직접 렌더링하고, 지원되지 않으면 기존 CPU 변환과 GDI 표시를 사용한다.

## 6. 빌드

- Visual Studio 18에서 v145 도구 집합을 사용해 기존 `SlimeDisplay.sln`을 연다.
- `x64`를 선택하고 `SlimeDisplay-windows-core` 프로젝트를 빌드한다.
- 솔루션은 DLL과 `SlimeDisplayTest`를 빌드한다.
- `SlimeDisplayTest` 실행 전 외부 VDD를 설치하고 활성화한다. 테스트는
  현재 디스플레이 모드를 사용하고 종료 시 원래 모드로 복원한다.

## 7. 개발 단계

### Phase 1 — 기존 VDD 연동

- [ ] 외부 VirtualDrivers VDD 설치 및 활성화
- [x] Windows 디스플레이 API로 VDD 가상 모니터 열거
- [x] 기존 가상 모니터 선택 및 디스플레이 모드 설정
- [x] 설치된 VDD에서 DXGI Desktop Duplication 캡처 확인

### Phase 2 — Capture

- [x] Desktop Duplication과 CaptureManager 연결
- [x] GPU Video Processor NV12 변환 및 BGRA 대체 경로
- [x] 프레임 처리 및 동기화
- [x] 캡처 및 GPU 리소스 관리
- [x] Capture Start / Stop 구현

### Phase 3 — Encoder

- [x] Media Foundation 초기화
- [x] 동기식 대체 경로를 포함한 H.264 인코더 연결
- [x] 현재 시스템의 하드웨어 인코딩 확인
- [x] 인코딩 프레임 콜백 구현
- [x] 프레임 타임스탬프 처리

### Phase 4 — 통합 테스트

- [ ] DLL API 전체 테스트
- [ ] 기존 VDD 가상 모니터 선택 및 해제 반복 테스트
- [ ] FHD 60 FPS 캡처
- [x] 현재 시스템에서 인코딩 프레임 전달 확인
- [ ] 장시간 실행 안정성 테스트
- [ ] Server와 C ABI 연동

## 8. 참고 자료

- [VirtualDrivers — Virtual Display Driver](https://github.com/VirtualDrivers/Virtual-Display-Driver)
- [Microsoft Learn — Desktop Duplication](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/desktop-dup-api)
- [Microsoft Learn — QueryDisplayConfig](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-querydisplayconfig)
- [Microsoft Learn — Media Foundation](https://learn.microsoft.com/en-us/windows/win32/medfound/microsoft-media-foundation-sdk)