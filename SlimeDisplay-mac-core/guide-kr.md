# SlimeDisplay-mac 동작 가이드

이 문서는 현재 macOS 구현이 가상 디스플레이를 만들고, 해당 화면을 캡처하고, H.264로 인코딩한 다음 테스트 클라이언트 경로에서 다시 디코딩해 창에 표시하는 과정을 설명합니다.

> **현재 검증 범위:** 테스트 앱은 Host 인코더와 Client 디코더를 같은 프로세스에서 연결합니다. 실제 WebRTC 연결, 네트워크 전송, 원격 Client는 아직 구현되어 있지 않습니다. 따라서 이 앱은 압축된 영상 데이터가 인코더에서 디코더까지 흐르는 경로를 검증하지만, 네트워크상의 실시간 스트리밍 전체를 검증하지는 않습니다.

## 1. 구성 요소

| 구성 요소 | 위치 | 역할 |
|---|---|---|
| 공개 C 타입과 함수 선언 | `SlimeDisplay/Public/` | 핸들, 설정, 결과 코드와 C API 계약 정의 |
| C/Objective-C++ API 구현 | `SlimeDisplay/API/` | 공개 C 함수를 Swift 매니저에 연결 |
| 세션 조정자 | `SlimeDisplay/Core/SDManager.swift` | 디스플레이, 캡처, 인코더의 실행 순서와 상태 조정 |
| 디스플레이 관리자 | `SlimeDisplay/Display/VirtualDisplayManager.swift` | 가상 디스플레이 생성 요청 및 Display ID 관리 |
| 가상 디스플레이 shim | `SlimeDisplay/Adapters/SDVirtualDisplayShim.mm` | Objective-C 런타임으로 디스플레이 객체 생성 |
| 화면 캡처 관리자 | `SlimeDisplay/Capture/ScreenCaptureManager.swift` | ScreenCaptureKit으로 선택 디스플레이를 캡처 |
| H.264 인코더 | `SlimeDisplay/Encoder/VideoEncoder.swift` | VideoToolbox 입력 프레임 인코딩 및 압축 결과 전달 |
| 테스트 앱 | `Tests/SlimeDisplayMacRunner.swift` | 가상 디스플레이와 캡처 수명주기 제어, 디코더 출력 표시 |
| 테스트 Client 디코더 | `Tests/H264ClientDecoder.swift` | 인코딩된 H.264 샘플을 VideoToolbox로 디코딩 |

```text
Xcode Test App
    |
    +-- SDManager.createVirtualDisplay()
    |       `-- VirtualDisplayManager
    |               `-- SDVirtualDisplayShim.mm
    |                       `-- CGVirtualDisplay 계열 런타임 객체
    |
    `-- SDManager.startEncoder() -> startCapture()
            |
            `-- ScreenCaptureKit SCStream
                    | BGRA CMSampleBuffer
                    v
                VideoToolbox Encoder
                    | H264EncodedFrame
                    | - 압축 프레임 바이트
                    | - PTS
                    | - 키프레임 여부
                    | - SPS/PPS와 NAL 길이 정보
                    v
                Test Client VideoToolbox Decoder
                    | BGRA CVPixelBuffer
                    v
                CIImage -> NSImage -> 미리보기 창
```

## 2. 가상 디스플레이 생성

테스트 앱의 **가상 디스플레이 생성** 버튼은 `SDManager.createVirtualDisplay(width:height:fps:)`를 호출합니다. 앱에서 요청하는 초기 값은 1280×720, 30 FPS입니다.

1. `SDConfiguration.isValid`가 해상도와 FPS를 확인합니다. 현재 허용 범위는 가로 320~7680, 세로 240~4320, FPS 1~120입니다.
2. `VirtualDisplayManager`가 Objective-C++ 함수 `SDVirtualDisplayCreate`를 호출합니다.
3. shim은 `NSClassFromString`으로 `CGVirtualDisplayDescriptor`, `CGVirtualDisplay`, `CGVirtualDisplaySettings`, `CGVirtualDisplayMode`를 찾고 런타임 selector로 객체를 구성합니다.
4. `applySettings:`로 해상도·주사율 설정을 적용하고 생성한 디스플레이 ID를 Swift 계층에 반환합니다.

현재 shim은 생성 객체를 전역으로 보관하므로 프로세스에서 한 번에 하나의 가상 디스플레이만 지원합니다. 이 방식은 공개 API만 사용하는 안정적인 드라이버 구현이 아니라 런타임 클래스와 selector에 의존하는 실험적 구현입니다. macOS 버전에 따라 클래스나 selector가 없거나 동작이 바뀌어 생성에 실패할 수 있습니다.

## 3. ScreenCaptureKit 캡처

`SDManager.startCapture()`는 보관된 Display ID를 `ScreenCaptureManager.start(...)`에 전달합니다.

1. ScreenCaptureKit에서 `SCShareableContent`를 조회합니다.
2. 공유 가능한 디스플레이 중 저장한 ID와 일치하는 디스플레이를 찾습니다. 찾지 못하면 `displayNotFound` 오류가 발생합니다.
3. 해당 디스플레이로 `SCContentFilter`와 `SCStream`을 구성합니다.
4. 스트림 출력 크기를 요청 해상도로, 최소 프레임 간격을 `1 / FPS`로 설정합니다.
5. 픽셀 포맷을 `kCVPixelFormatType_32BGRA`, 큐 깊이를 3으로 설정합니다.
6. ScreenCaptureKit은 스트림 콜백으로 `CMSampleBuffer`를 전달합니다. 코드는 화면 출력이고 샘플이 유효하며 프레임 상태가 `.complete`인 경우만 인코더로 보냅니다. 변경 없음, 빈 화면, 중지 등 완성된 화면 이미지가 아닌 이벤트는 인코딩하지 않습니다.

화면 캡처를 시작하려면 앱에 macOS **화면 및 시스템 오디오 녹음(Screen & System Audio Recording)** 권한이 필요합니다. 권한 요청이 표시되면 허용하고, 이미 거부했다면 시스템 설정의 개인정보 보호 및 보안에서 앱 권한을 확인해야 합니다.

## 4. Host 인코딩 경로

테스트 앱은 `SDManager.startEncoder()`를 먼저 호출하고, 성공하면 `SDManager.startCapture()`를 호출합니다. 이 순서는 캡처 프레임이 도착하기 전에 인코더 세션을 준비하기 위한 것입니다.

### 입력

ScreenCaptureKit에서 받은 `CMSampleBuffer` 안의 `CVPixelBuffer`가 인코더 입력입니다. 현재 캡처 설정은 BGRA이며, 캡처된 프레임을 `VTCompressionSessionEncodeFrame`으로 전달합니다.

### VideoToolbox 설정

`VideoEncoder.start(...)`는 다음 구성으로 VideoToolbox 압축 세션을 생성합니다.

- 코덱: H.264
- 크기: 가상 디스플레이 설정과 동일
- 실시간 인코딩: 활성화
- 프레임 재정렬: 비활성화
- 프로파일: H.264 Main Auto Level
- 기대 프레임 레이트: 설정된 FPS

### 압축 출력 형태

VideoToolbox 콜백은 압축된 `CMSampleBuffer`를 전달합니다. 인코더는 다음 정보를 꺼내 `H264EncodedFrame`으로 묶습니다.

- `data`: `CMBlockBuffer`의 압축된 H.264 액세스 유닛 바이트
- `presentationTime`: 출력 프레임의 PTS
- `isKeyframe`: `kCMSampleAttachmentKey_NotSync` 여부로 판별한 키프레임 표시
- `codecConfiguration`: 포맷 설명에서 추출한 H.264 SPS, PPS 및 NAL unit header 길이

이 바이트는 CoreMedia가 제공한 H.264 샘플 형식입니다. 네트워크 전송 계층에 연결할 때에는 수신 측이 동일한 패킷화 형식을 기대하는지 확인해야 합니다. RTP/WebRTC 패킷화나 Annex-B start code가 자동으로 만들어진다고 가정하면 안 됩니다.

SPS/PPS는 디코더가 영상 크기와 프로파일 등을 설정하는 데 필요합니다. 현재 테스트 구현은 디코더가 시작하거나 설정이 바뀔 때 사용할 수 있도록 각 `H264EncodedFrame`에 코덱 설정을 함께 전달합니다. 실제 프로토콜에서는 설정 정보를 세션 시작 시 별도 메시지로 보내거나, 키프레임과 함께 전달하는 방식 등을 명확히 정해야 합니다.

## 5. Client 디코딩 및 미리보기

테스트 앱의 `manager.onEncodedFrame`은 압축된 `H264EncodedFrame`만 `H264ClientDecoder.decode(...)`로 넘깁니다. 캡처 원본의 BGRA 픽셀 버퍼를 미리보기 UI로 직접 전달하지 않습니다.

1. 디코더는 전용 serial queue에서 프레임을 처리해 VideoToolbox 호출을 순서대로 수행합니다.
2. SPS/PPS와 NAL 길이 정보가 이전 설정과 다르면 `CMVideoFormatDescriptionCreateFromH264ParameterSets`로 H.264 포맷 설명을 만들고 `VTDecompressionSession`을 구성합니다.
3. 압축된 바이트를 `CMBlockBuffer`에 복사하고 PTS, 키프레임 정보를 붙여 `CMSampleBuffer`를 만듭니다.
4. `VTDecompressionSessionDecodeFrame`에 압축 샘플을 전달합니다.
5. VideoToolbox 디코더 콜백으로 복원된 BGRA `CVPixelBuffer`가 도착합니다.
6. 테스트 앱은 그 디코딩 결과를 `CIImage`와 `NSImage`로 변환해 **가상 디스플레이 스트림** 창에 표시합니다.

따라서 미리보기 창은 캡처 원본을 직접 보여주는 창이 아닙니다. 인코딩·디코딩 과정을 통과한 결과를 보여주며, 화면에 **디코딩된 H.264 프레임** 수가 증가하는지로 파이프라인 진행을 확인할 수 있습니다.

## 6. 테스트 앱 사용 순서

1. Xcode에서 `SlimeDisplay-mac.xcodeproj`를 열고 `SlimeDisplayMacRunner` scheme을 선택합니다.
2. **Run**으로 앱을 실행합니다.
3. **가상 디스플레이 생성**을 누릅니다.
4. **캡처 시작**을 누릅니다. 이 동작은 인코더를 먼저 시작한 뒤 ScreenCaptureKit 스트림을 시작하고, 스트림 창도 엽니다.
5. 필요한 경우 시스템 화면 캡처 권한을 허용합니다.
6. **가상 디스플레이 스트림** 창과 메인 창의 디코딩 프레임 수를 확인합니다.
7. **디코딩 이미지 저장**을 누르면 현재 미리보기 이미지가 PNG로 저장됩니다.
8. **캡처 중지** 후 **디스플레이 제거**를 누릅니다.

가상 화면 자체에 표시할 콘텐츠가 없으면 올바르게 동작해도 검정 화면이 보일 수 있습니다. 테스트할 때는 macOS의 디스플레이 설정에서 가상 디스플레이를 확장 화면으로 구성한 뒤 창을 해당 화면으로 옮기거나, 가상 화면에 눈에 띄는 콘텐츠를 표시하십시오.

## 7. 수명주기와 상태

일반적인 시작 순서는 다음과 같습니다.

```text
createVirtualDisplay
    -> startEncoder
        -> startCapture
            -> capture callback
                -> encode frame
                    -> onEncodedFrame
                        -> client decoder
                            -> preview
```

정지 시에는 캡처를 멈추고 인코더를 정리한 뒤 가상 디스플레이를 제거합니다. `SDManager.destroyVirtualDisplay()`도 인코더와 캡처를 정리하도록 구성되어 있지만, 현재 `stopCapture()`는 실제 `SCStream.stopCapture()` 호출을 `Task`로 비동기 실행합니다. 빠른 반복 실행이나 제거 과정의 완전한 종료 순서가 중요한 경우, 호출자가 stop 완료를 기다리는 async API로 정리할 필요가 있습니다.

## 8. 오류 확인

| 증상 | 확인할 항목 |
|---|---|
| 가상 디스플레이 생성 실패 | macOS에서 필요한 런타임 클래스·selector가 제공되는지, 해당 API 동작이 지원되는지 확인 |
| 캡처할 디스플레이를 찾지 못함 | 생성된 Display ID가 ScreenCaptureKit의 디스플레이 목록에 나타나는지 확인 |
| 캡처가 시작되지 않음 | 시스템 화면 캡처 권한, 디스플레이 상태, Xcode 콘솔의 ScreenCaptureKit 오류 확인 |
| 미리보기 검정 화면 | 가상 화면에 실제 콘텐츠가 표시되고 있는지, 디스플레이가 확장 화면으로 활성화됐는지 확인 |
| 디코딩 프레임 수가 증가하지 않음 | Xcode 콘솔에서 H.264 SPS/PPS 추출, 포맷 설명 생성, `VTDecompressionSession` 오류 확인 |
| 캡처는 되지만 영상 전송이 안 됨 | 현재 저장소에 WebRTC 및 네트워크 전송 경로가 아직 없음을 확인 |

## 9. 아직 구현하지 않은 것

- Host와 Client 사이의 WebRTC 또는 기타 네트워크 transport
- SDP/ICE 시그널링 및 세션 연결 관리
- 프레임 손실·재전송·혼잡 제어 및 비트레이트 조정
- 독립 프로세스에서 실행되는 Client와 실제 네트워크 수신 테스트
- 공개 C API의 모든 선언에 대한 구현 및 인코딩 콜백의 C ABI 연결
- `CGVirtualDisplay` 런타임 접근의 OS 버전 호환성 및 배포 가능성 검증

## 10. 빌드 및 실행

저장소 루트 기준 프레임워크 빌드:

```sh
xcodebuild \
  -project SlimeDisplay-mac/SlimeDisplay-mac.xcodeproj \
  -scheme SlimeDisplay-mac \
  -configuration Debug \
  build \
  CODE_SIGNING_ALLOWED=NO
```

테스트 앱 빌드:

```sh
xcodebuild \
  -project SlimeDisplay-mac/SlimeDisplay-mac.xcodeproj \
  -scheme SlimeDisplayMacRunner \
  -configuration Debug \
  build \
  CODE_SIGNING_ALLOWED=NO
```

명령줄 빌드는 컴파일만 검증합니다. 가상 디스플레이 생성, 권한 승인, 실제 화면 캡처 및 H.264 디코딩은 macOS에서 테스트 앱을 실행해 확인해야 합니다.
