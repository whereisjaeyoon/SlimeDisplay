# SlimeDisplay Windows Core Guide

이 문서는 Windows 코어가 프로그램 시작부터 화면 복원까지 어떻게 동작하는지, 현재 구현된 범위와 실제 검증 범위를 설명한다. Windows 코어는 자체 가상 디스플레이 드라이버를 설치하지 않고, 별도로 설치된 [Virtual Display Driver (VDD)](https://github.com/VirtualDrivers/Virtual-Display-Driver)를 사용한다.

## 1. 한눈에 보는 처리 흐름

```text
호출자 / Server
      |
      | 공통 C ABI: slimedisplay.h
      v
SDAPI.cpp -> SDManager
                 |
       +---------+----------+
       |                    |
       v                    v
DisplayManager        CaptureManager
       |                    |
       v                    v
Windows 디스플레이 API   DXGI Desktop Duplication
       |                 (초기화 실패 시 GDI)
       v                    |
외부 VDD 가상 모니터       BGRA 프레임
                            |
                            v
                     제한된 프레임 큐
                            |
                            v
                    BGRA -> NV12 변환
                            |
                            v
                 Media Foundation H.264
                            |
                            v
                 SD_EncodedFrameCallback
```

화면 캡처는 DXGI의 D3D11 출력 복제 기능을 우선 사용한다. 복제 경로를 만들 수 없는 경우에는 GDI 캡처를 시도한다. 두 경로 모두 현재 CPU 메모리의 BGRA 프레임을 만든다. 인코더는 이를 NV12로 변환해 Media Foundation에 전달한다.

## 2. 소스 파일별 역할

| 파일 | 역할 |
|---|---|
| [`include/slimedisplay.h`](../include/slimedisplay.h) | 플랫폼 공용 C ABI 함수와 인코딩 콜백 선언 |
| [`include/slimedisplay_types.h`](../include/slimedisplay_types.h) | 핸들, 결과 코드, 디스플레이 설정 및 정보 구조체 |
| [`SlimeDisplay-windows-core/SDAPI.cpp`](./SlimeDisplay-windows-core/SDAPI.cpp) | DLL에서 외부로 내보내는 함수. 입력 확인, `SD_Handle` 관리, 예외가 C ABI 밖으로 넘어가지 않게 처리 |
| [`SlimeDisplay-windows-core/SDManager.h`](./SlimeDisplay-windows-core/SDManager.h) / [`SDManager.cpp`](./SlimeDisplay-windows-core/SDManager.cpp) | 한 인스턴스의 디스플레이·캡처·인코더 수명주기 연결 |
| [`SlimeDisplay-windows-core/display/DisplayManager.h`](./SlimeDisplay-windows-core/display/DisplayManager.h) / [`DisplayManager.cpp`](./SlimeDisplay-windows-core/display/DisplayManager.cpp) | VDD 모니터 검색, 지원 모드 선택, 현재 모드 적용 및 원래 모드 복원 |
| [`SlimeDisplay-windows-core/capture/CaptureManager.h`](./SlimeDisplay-windows-core/capture/CaptureManager.h) / [`CaptureManager.cpp`](./SlimeDisplay-windows-core/capture/CaptureManager.cpp) | DXGI Desktop Duplication 또는 GDI로 화면을 캡처하고 프레임 콜백 실행 |
| [`SlimeDisplay-windows-core/encoder/EncoderManager.h`](./SlimeDisplay-windows-core/encoder/EncoderManager.h) / [`EncoderManager.cpp`](./SlimeDisplay-windows-core/encoder/EncoderManager.cpp) | 프레임 큐, BGRA-to-NV12 변환, Media Foundation H.264 인코딩 및 결과 콜백 |
| [`SlimeDisplayTest/main.cpp`](./SlimeDisplayTest/main.cpp) | 빌드된 DLL을 로드해 C ABI와 VDD 인코딩 경로를 확인하는 스모크 테스트 |

`SD_Handle`은 C 호출자에게 내부 C++ 객체를 노출하지 않는 불투명 핸들이다. `SDManager`는 공개 API 요청을 담당 매니저로 전달한다. 디스플레이 설정 변경과 정보 조회는 mutex로 보호한다.

## 3. API 호출의 전체 순서

정상적인 사용 순서는 다음과 같다.

1. `sd_create`로 핸들을 만든다.
2. `sd_create_virtual_display`로 VDD 모니터와 모드를 선택한다.
3. 필요하면 `sd_get_virtual_display_info`로 적용된 모드를 확인한다.
4. `sd_set_encoded_frame_callback`으로 H.264 콜백을 등록한다.
5. `sd_start_encoder`로 Media Foundation 인코더를 준비한다.
6. `sd_start_capture`로 프레임 생성을 시작한다.
7. 콜백에서 인코딩된 프레임을 처리하거나, 다른 스레드로 복사해 전달한다.
8. `sd_stop_capture`, `sd_stop_encoder` 순서로 중지한다.
9. `sd_destroy_virtual_display`로 원래 화면 모드를 복원한다.
10. `sd_destroy`로 핸들을 해제한다.

인코더를 먼저 시작하는 순서는 테스트 프로그램이 사용하는 순서다. 캡처가 먼저 시작되어도 인코더가 실행되기 전 프레임은 인코딩되지 않는다.

### 3.1 핸들 생성 및 오류 경계

`sd_create`는 `SD_Handle`을 만들고 주소를 반환한다. 핸들이나 필요한 포인터가 null이면 해당 API는 `SD_ERROR_INVALID_ARGUMENT`를 반환한다. 나머지 C ABI 진입점은 내부 C++ 예외를 받아 `SD_ERROR_INTERNAL`로 변환하며, 예외가 C ABI 경계를 넘지 않도록 한다.

`SD_Result`는 공통 헤더에 정의되어 있다. 예를 들어 유효하지 않은 인자, 지원하지 않는 VDD 모드, 잘못된 상태, 캡처 실패, 인코딩 실패를 각각 구별할 수 있다. 화면이 생성되지 않은 상태에서 정보 조회나 캡처·인코더 시작을 요청하면 `SD_ERROR_INVALID_STATE`가 반환된다.

### 3.2 VDD 검색과 모드 변경

`DisplayManager`는 Windows의 `EnumDisplayDevicesW`로 어댑터를 열거한다. 이름에 `Virtual Display Driver`가 포함되거나 장치 ID에 `MTTVDD`가 포함되고, 데스크톱에 연결된 모니터를 가진 어댑터를 VDD 대상으로 선택한다. 현재 구현은 조건을 만족하는 첫 VDD 모니터를 사용한다.

요청한 너비·높이·주사율이 VDD가 열거한 지원 모드와 모두 일치해야 한다. 그 전에 현재 모드를 저장하고, `ChangeDisplaySettingsExW`로 요청 모드를 적용한다. 지원 모드를 찾지 못하거나 Windows가 모드 변경을 거부하면 적절한 오류를 반환한다. `SDManager`는 너비 320–7680, 높이 240–4320, FPS 1–120 범위를 검사한다.

`sd_get_virtual_display_info`는 현재 Windows 모드의 너비, 높이, 주사율과 모니터 장치 ID에서 계산한 식별자를 반환한다. `sd_destroy_virtual_display`와 `sd_destroy`는 캡처와 인코더를 중지한 다음 저장해 둔 원래 모드를 복원한다. 외부 VDD 드라이버 자체를 설치하거나 제거하지 않는다.

### 3.3 캡처 단계

`CaptureManager`는 디스플레이 장치 이름과 요청 크기를 사용해 DXGI 어댑터·출력을 찾는다. 대상 출력에서 `IDXGIOutputDuplication`을 만들고 D3D11 staging texture를 생성한다. 프레임이 갱신되면 복제된 GPU 텍스처를 staging texture로 복사하고 CPU에서 읽어 BGRA 버퍼를 만든다.

Desktop Duplication 초기화가 불가능하면 GDI 경로를 사용한다. 이 경로는 대상 모니터의 화면 영역을 DIB로 복사한다. 캡처 루프는 요청 FPS에 맞춰 프레임 전달을 시도한다. DXGI가 새 화면을 반환하지 않는 반복에서는 마지막 픽셀 내용을 유지해 전달할 수 있다.

캡처 타임스탬프는 캡처 시작 시점부터 경과한 시간이며, 100ns 단위로 인코더에 전달된다. 현재 캡처 버퍼는 CPU 메모리다. 따라서 DXGI를 사용하더라도 GPU Surface를 인코더까지 무복사로 전달하는 구조는 아니다.

### 3.4 H.264 인코딩과 콜백

`EncoderManager`는 별도 작업 스레드에서 COM과 Media Foundation을 초기화한다. 먼저 D3D11 장치 관리자와 Media Foundation H.264 하드웨어 변환기를 준비하고, 사용할 수 없으면 로컬 동기식 Media Foundation 변환기를 찾는다. 비동기 하드웨어 변환기는 Media Foundation 이벤트(`NeedInput`, `HaveOutput`, drain 완료)를 처리한다.

캡처 프레임은 최대 3개까지 큐에 보관한다. 큐가 가득 차면 오래된 프레임을 버려 누적 지연을 제한한다. 인코딩 준비 시 BGRA 픽셀을 CPU에서 NV12로 변환하고, 너비와 높이가 짝수인지 검사한다. 출력은 H.264이며 인코딩된 데이터의 크기, 타임스탬프, 키프레임 여부를 `SD_EncodedFrameCallback`에 전달한다.

콜백은 인코더 작업 스레드에서 호출된다. 전달된 `data` 버퍼는 콜백이 실행되는 동안에만 유효하므로, 콜백 이후 사용할 데이터는 콜백 안에서 별도 메모리로 복사해야 한다. 콜백 코드와 `user_data`는 다른 스레드에서 `sd_stop_encoder`가 반환할 때까지 유효해야 한다. 콜백 내부에서 핸들을 파괴하지 않는다. 콜백에서 인코더를 직접 중지하면 `SD_ERROR_INVALID_STATE`가 반환될 수 있으므로 외부 스레드에서 중지한다.

## 4. 입력과 제약

### `SD_DisplayConfig`

- `width`: 지원되는 VDD 모드의 너비
- `height`: 지원되는 VDD 모드의 높이
- `fps`: 지원되는 모드의 주사율

범위 검사를 통과해도 VDD가 해당 조합을 제공하지 않으면 사용할 수 없다. 현재 구현은 다른 모드로 자동 대체하지 않는다.

### `SD_DisplayInfo`

- `id`: 모니터 장치 ID를 기반으로 만든 식별자
- `width`, `height`, `refresh_rate`: Windows에서 확인한 현재 모드

### 인코딩 콜백

콜백 인자는 다음과 같다.

- `data`, `size`: 콜백 실행 중에 유효한 H.264 출력 바이트
- `presentation_time`: 100ns 단위 타임스탬프
- `is_keyframe`: 키프레임이면 1, 아니면 0
- `user_data`: 호출자가 등록한 사용자 포인터

공용 헤더에는 `SD_PixelFormat`과 `SD_FrameInfo`도 정의되어 있지만, 현재 Windows 콜백은 원시 BGRA 입력 프레임이나 이 구조체를 외부에 전달하지 않는다.

## 5. 현재 구현 및 검증 상태

| 항목 | 상태 | 설명 |
|---|---|---|
| 공용 C ABI | 구현·검증 | DLL 테스트에서 주요 심볼을 동적으로 찾고 호출 |
| 외부 VDD 검색 | 구현·검증 | 현재 설치된 VDD에서 연결된 모니터 검색 |
| 모드 적용·정보 조회·복원 | 구현·검증 | 테스트는 VDD의 현재 모드를 요청하고 정보와 종료 결과를 확인 |
| DXGI Desktop Duplication | 구현·검증 | 현재 설치된 VDD로 캡처 시작 확인 |
| GDI 대체 캡처 | 구현 | DXGI 초기화가 불가능할 때 선택하는 경로; 별도 런타임 검증은 하지 않음 |
| Media Foundation H.264 하드웨어 경로 | 구현·검증 | 현재 시스템에서 인코더 시작 및 프레임 콜백 확인 |
| 동기식 Media Foundation 대체 인코더 | 구현 | 코드 경로는 있으나 현재 검증에서 별도 동작·성능을 입증하지 않음 |
| FHD 60 FPS 지속 처리 | 미검증 | 스모크 테스트는 프레임 전달 여부만 확인 |
| 장시간 안정성·반복 모드 전환 | 미검증 | 장시간 및 반복 시나리오 테스트는 하지 않음 |
| Server 또는 네트워크 전송 | 미구현/미검증 | 현재 코어는 인코딩 콜백까지 제공 |

### 수행한 검증

- Visual Studio 18의 v145 도구 집합으로 `SlimeDisplay.sln`을 Debug 및 Release 구성에서 빌드했다.
- 두 구성의 `SlimeDisplayTest`를 실행했고 모두 종료 코드 0으로 끝났다.
- 테스트는 DLL의 C ABI 심볼, `sd_get_version`의 현재 값(0), 연결된 VDD의 현재 모드, 디스플레이 정보, 캡처 중 H.264 프레임 콜백, 정상 중지 및 디스플레이 해제를 확인한다.
- `git diff --check`에서 공백 오류가 없음을 확인했다.

스모크 테스트는 실제 프레임이 도착하는지만 확인한다. 출력 스트림의 디코딩 가능성, 영상 품질, 정확한 처리 FPS, 장시간 실행은 검증하지 않았다. 이 때문에 “FHD 60 FPS 지원이 검증됐다”고 해석하면 안 된다.

## 6. 빌드와 스모크 테스트

외부 VDD를 설치하고 모니터 하나를 데스크톱에 연결한 뒤 Visual Studio 18 Developer PowerShell에서 실행한다.

```powershell
Set-Location .\SlimeDisplay-windows-core
msbuild .\SlimeDisplay.sln /t:Build /p:Configuration=Debug /p:Platform=x64
.\x64\Debug\SlimeDisplayTest.exe
```

Release 구성은 `Configuration=Release`와 `.\x64\Release\SlimeDisplayTest.exe`를 사용한다. 테스트는 VDD의 현재 해상도와 주사율을 그대로 요청하고 종료 시 원래 모드를 복원한다. VDD가 없거나 현재 모드를 열거할 수 없으면 테스트가 성공으로 처리되지 않는다.

## 7. 관련 구현 문서

- [프로젝트 README](./README.md)
- [프로젝트 README 한국어판](./README-kr.md)
- [공개 C ABI 헤더](../include/slimedisplay.h)
- [공개 타입 헤더](../include/slimedisplay_types.h)
