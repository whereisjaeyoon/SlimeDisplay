# SlimeDisplay-mac

macOS 프레임워크 구현 문서입니다. 소스는 `SlimeDisplay-mac/` 아래에 있으며, 현재 가상 디스플레이 생성, 화면 캡처 및 H.264 인코딩을 연결하는 초기 백엔드 단계입니다.

## 소스 구성

```text
SlimeDisplay-mac/
├── SlimeDisplay-mac/
│   ├── Public/       # C ABI 및 공통 타입
│   ├── API/          # C API를 Swift 매니저에 연결하는 Objective-C++
│   ├── Core/         # 세션 구성 및 기능 조정
│   ├── Display/      # 가상 디스플레이 생성과 ID 관리
│   ├── Capture/      # ScreenCaptureKit 화면 캡처
│   ├── Encoder/      # VideoToolbox H.264 인코딩
│   └── Adapters/     # 가상 디스플레이 Objective-C++ shim
├── Tests/            # Xcode에서 실행 가능한 테스트 앱
└── SlimeDisplay-mac.xcodeproj/
```

## 데이터 흐름

```text
SD C API
   |
   v
SDManagerBridge / SDManager
   |
   +--> VirtualDisplayManager
   |       `--> SDVirtualDisplayShim.mm
   |             `--> CGVirtualDisplay 계열 런타임 클래스
   |
   +--> ScreenCaptureManager
   |       `--> ScreenCaptureKit (BGRA CMSampleBuffer)
   |
   `--> VideoEncoder
           `--> VideoToolbox (H.264)
                  `--> 인코딩 바이트, PTS, 키프레임 여부 콜백
```

## 구현된 동작

- `SDConfiguration`은 해상도를 최소 320×240, 최대 7680×4320으로 제한하고 FPS를 1~120으로 검증합니다.
- 가상 디스플레이 생성은 크기와 주사율을 검사한 뒤 Objective-C 런타임에서 `CGVirtualDisplayDescriptor`, `CGVirtualDisplay`, `CGVirtualDisplaySettings`, `CGVirtualDisplayMode` 클래스를 찾아 생성합니다. 생성된 디스플레이 객체는 shim에서 보관하며, 현재 프로세스에서 한 번에 하나의 가상 디스플레이만 허용합니다.
- ScreenCaptureKit은 지정된 디스플레이 ID를 찾아 캡처합니다. 캡처 출력은 BGRA 포맷이며, 요청된 FPS에 맞춘 프레임 간격과 3개 프레임의 큐 깊이를 사용합니다.
- VideoToolbox는 실시간 H.264 인코딩 세션을 만들고 프레임 재정렬을 비활성화합니다. 결과 콜백은 인코딩된 데이터, presentation timestamp(PTS), 키프레임 여부를 전달합니다.
- `SDManager`는 디스플레이, 캡처 및 인코더의 수명주기를 조정합니다. `SDAPI.mm`은 공개 C 함수를 Objective-C/Swift 브리지에 연결합니다.
- 테스트 앱에서 캡처 중 **캡처 이미지 저장**을 누르면 다음 화면 프레임을 PNG로 변환해 저장 위치를 선택할 수 있습니다.

## 현재 범위와 주의사항

- 가상 디스플레이 생성은 `NSClassFromString`과 Objective-C 런타임을 통해 `CGVirtualDisplay` 계열 타입에 접근하는 실험적 구현입니다. macOS 버전 변경에 취약할 수 있고, App Store 배포 등에서 정책상 제약이 있을 수 있으므로 호환성 및 배포 가능성을 별도로 검증해야 합니다.
- 화면 캡처에는 macOS의 화면 기록 권한이 필요합니다. 권한 거부나 디스플레이를 찾지 못하는 상황에서는 캡처를 시작할 수 없습니다.
- 현재 소스에는 WebSocket 시그널링, WebRTC 전송, 브라우저 연결이 포함되어 있지 않습니다. 따라서 인코더 출력을 실제 네트워크 스트림으로 전달하는 엔드투엔드 기능은 아직 구현 범위가 아닙니다.
- 공개 C 헤더에는 인코딩 프레임 콜백 등록 및 버전 조회 선언도 있지만, 현재 `SDAPI.mm`에는 해당 함수 구현이 없습니다. 이를 포함한 C API 전체의 구현 및 오류 코드 매핑은 추가 검증이 필요합니다.

## 빌드

macOS 및 Xcode가 설치된 환경에서 저장소 루트에서 실행합니다.

```sh
xcodebuild \
  -project SlimeDisplay-mac/SlimeDisplay-mac.xcodeproj \
  -scheme SlimeDisplay-mac \
  -configuration Debug \
  build \
  CODE_SIGNING_ALLOWED=NO
```

이 빌드는 프레임워크 컴파일을 확인합니다. 가상 디스플레이 생성과 화면 캡처 권한을 포함한 실제 동작은 macOS 환경에서 별도로 실행해 검증해야 합니다.

## Xcode에서 실행

`SlimeDisplay-mac/SlimeDisplay-mac.xcodeproj`를 Xcode에서 열고 scheme을 `SlimeDisplayMacRunner`로 선택한 다음 **Run**을 누릅니다. 테스트 앱에서 가상 디스플레이를 생성하고 캡처 및 H.264 인코딩을 시작하거나 중지할 수 있습니다. 처음 화면 캡처를 시작할 때 macOS가 화면 기록 권한을 요청할 수 있습니다.

명령줄에서도 실행 파일을 빌드할 수 있습니다.

```sh
xcodebuild \
  -project SlimeDisplay-mac/SlimeDisplay-mac.xcodeproj \
  -scheme SlimeDisplayMacRunner \
  -configuration Debug \
  build \
  CODE_SIGNING_ALLOWED=NO
```
