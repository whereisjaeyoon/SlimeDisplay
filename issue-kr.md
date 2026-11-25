# SlimeDisplay 설계 검토 이슈

설계 문서(README) 검토 결과 정리. 전체 구조는 실현 가능하며, 아래 항목을 보완해야 한다.

- **우선순위:** 🔴 필수(그대로 진행 시 동작하지 않거나 재설계 필요) / 🟡 보완 권장 / 🟢 참고

---

## 확인된 사항

| 항목 | 결과 | 비고 |
| :--- | :--- | :--- |
| VDD 라이선스 | MIT | 재배포 시 라이선스 고지 포함 |
| VDD 서명 | SignPath Foundation 인증서로 서명 | 소스 수정·자체 빌드 시 서명 유지 안 됨 |
| VDD 설치 | Release 설치 프로그램, winget 지원 | `winget install --id=VirtualDrivers.Virtual-Display-Driver -e` |
| VDD 설정 | `C:\VirtualDisplayDriver\vdd_settings.xml` | 해상도 추가/삭제는 이 파일로 관리 |
| macOS `CGVirtualDisplay` | Private API | Mac App Store 배포 불가. DeskPad/BetterDisplay처럼 직접 배포(공증) 방식 |

---

## 🔴 필수

### #1 H.264 레벨 협상

- **문제:** Safari는 보통 `profile-level-id=42e01f`(Level 3.1, 약 720p)로 협상한다. 목표인 1080p60은 Level 4.2가 필요하다.
- **영향:** 영상이 재생되지 않거나 저해상도로 제한될 수 있다.
- **조치:**
  - Host SDP에 `level-asymmetry-allowed=1` 추가
  - 인코더 레벨을 4.2 이상(iPad 네이티브 해상도는 5.1~5.2)으로 설정
  - B-프레임 비활성화, 키프레임마다 SPS/PPS 포함
- **문서 위치:** 5.4 코덱

### #2 Web Client 예시 코드가 iOS Safari에서 동작하지 않음

- **문제:**
  - `<video>`에 `autoplay playsinline muted`가 없으면 iOS에서 `play()`가 거부된다.
  - Host가 msid를 보내지 않으면 `event.streams[0]`이 `undefined`다.
  - 5.3처럼 브라우저가 Offerer인데 수신 트랜시버가 없어 비디오 m-line이 생성되지 않는다.
- **조치:**

```html
<video autoplay playsinline muted></video>
```

```typescript
const pc = new RTCPeerConnection();
pc.addTransceiver("video", { direction: "recvonly" }); // 브라우저가 Offerer일 때 필수

pc.ontrack = (e) => {
  const video = document.querySelector("video")!;
  video.srcObject = e.streams[0] ?? new MediaStream([e.track]);
  video.play().catch(() => {
    // 자동 재생 실패 시 탭하여 재생 버튼 표시
  });
};
```

- **문서 위치:** 5.3 연결 과정, 7.3 영상 수신 예시

### #3 HTTPS/WSS 인증서 검증이 너무 늦음

- **문제:** iOS에서 Self-signed 인증서를 완전히 신뢰하려면 프로파일 설치 후 `설정 → 일반 → 정보 → 인증서 신뢰 설정`까지 거쳐야 한다. UX 부담이 크고, 실패 시 전체 설계가 바뀔 수 있다.
- **조치:**
  - 웹페이지와 WSS를 같은 origin(같은 호스트:포트)에서 제공
  - 경고 화면 1회 통과만으로 페이지, WSS, WebRTC가 모두 동작하는지 실제 iPad Safari에서 확인
  - 이 검증을 **Phase 2로 이동**
- **문서 위치:** 6.3 인증서, 10 개발 단계

### #4 Client 해상도 맞춤 부재

- **문제:** iPad는 4:3 비율인데 목표 해상도가 1920×1080으로 고정되어 있다. 확장 디스플레이로 쓰면 레터박스가 생기고 화면이 흐려진다.
- **충돌:** 2.2에서 "VDD 설정 파일을 직접 수정하지 않는다"고 했지만, VDD 해상도 목록은 `vdd_settings.xml`로 관리된다.
- **조치:**
  - 설치 시 주요 Client 해상도(예: 2048×1536, 2360×1640, 2732×2048)를 `vdd_settings.xml`에 미리 등록
  - 접속 시 Client가 `screen.width × devicePixelRatio` 기준 해상도를 전송
  - Host는 `ChangeDisplaySettingsEx`로 가상 모니터 모드만 변경
  - 해상도 맞춤을 **Phase 6 → Phase 4로 이동**
- **문서 위치:** 2.2, 8 성능 설계, 10 개발 단계

### #5 Host 핵심 기술 스택 미정

- **문제:** Phase 2 시작 전 결정되어야 할 항목이 문서에 없다.
- **결정 필요 항목:**

| 항목 | 후보 | 권장 |
| :--- | :--- | :--- |
| Host 언어 | C++ / Rust | Windows·macOS 공통 코어 고려 |
| WebRTC 라이브러리 | libdatachannel(C++), webrtc-rs(Rust), Pion(Go), libwebrtc | libdatachannel (libwebrtc는 과도하게 무거움) |
| Windows 인코더 | NVENC / AMF / QSV 직접 사용, Media Foundation | 초기 Media Foundation, 이후 벤더 SDK |
| 비트레이트 제어 | 자체 구현 필요 | libdatachannel에 혼잡 제어 없음. RTCP RR/REMB 기반 구현 |

- **문서 위치:** 3 전체 시스템 아키텍처, 5 Signaling 및 WebRTC

---

## 🟡 보완 권장

### #6 프로젝트명 불일치

- 제목은 **SlimeDisplay**, 본문·서비스 타입(`_beam._tcp.local.`)·저장소(`beam/`)는 **Beam**.
- 하나로 통일한다.

### #7 입력(터치/펜) 기능 일정 명시

- 원격 입력 기본 비활성화는 보안상 타당하나, 확장 디스플레이는 해당 화면에서 조작할 수 없으면 활용도가 크게 떨어진다.
- MVP 직후 단계로 명시하고, `protocol/`에 DataChannel 입력 메시지 스키마 자리를 미리 확보한다.
  - 포인터 종류(`touch` / `pen` / `mouse`), 정규화 좌표, 압력, 기울기
  - Windows 주입: `SendInput`(마우스), `InjectSyntheticPointerInput`(펜 압력)
  - DataChannel: 이동 이벤트는 `unordered` + `maxRetransmits: 0`, 클릭/키는 reliable 채널

### #8 Windows Graphics Capture 세부 설정

- 노란 캡처 테두리 제거: `IsBorderRequired = false` (Windows 11)
- 커서 포함 여부: `IsCursorCaptureEnabled`
- 지연 문제 시 Desktop Duplication API로 교체할 수 있도록 Capture Backend를 인터페이스로 분리

### #9 QR 외 접속 수단

- iPad/Android는 카메라로 QR을 바로 스캔하지만 Windows/macOS Client에서는 불편하다.
- QR과 함께 **접속 URL + 짧은 페어링 코드**를 표시한다.

### #10 동시 접속 정책

- 한 가상 디스플레이를 여러 Client가 볼 수 있는지, 1대만 허용하는지 정의한다.
- 인코더 인스턴스 수, 해상도 맞춤 정책(#4)이 이 결정에 따라 달라진다.

### #11 macOS 검증 항목 추가

- ScreenCaptureKit이 `CGVirtualDisplay`로 만든 화면을 다른 디스플레이와 혼동하는 문제가 보고되어 있다.
- Phase 5 검증 항목에 추가한다.

---

## 🟢 참고

### #12 Windows mDNS 광고

- 별도 라이브러리 없이 `DnsServiceRegister`(Windows 10 1809+)로 광고 가능.

### #13 브라우저 쪽 지연 측정

- `video.requestVideoFrameCallback()` 메타데이터로 디코딩·렌더링 시점 측정 가능(Safari 지원).
- 8.1 성능 측정의 Decoding/Rendering Latency 측정 방법으로 명시.

### #14 WebRTC 지연 관련

- Host 송신 간격을 일정하게 유지(화면 변화가 없어도 일정 간격 송신). 불규칙하면 브라우저 지터 버퍼가 커진다.
- RTP 타임스탬프는 실제 캡처 시각 기준.
- 큰 주기적 키프레임 대신 intra-refresh를 지원하는 인코더(NVENC 등) 사용 검토.
- Chrome의 `jitterBufferTarget`은 Safari 지원이 불확실하므로 선택 사항으로 취급.

### #15 ICE 후보

- Safari는 자기 Host 후보를 `xxxx.local`(mDNS)로 숨긴다. Host가 Safari의 STUN 체크를 받으면 peer-reflexive 후보로 주소를 알게 되므로 LAN 직접 연결에는 문제없다.

### #16 VDD 운영 주의

- VDD는 주요 GPU/칩셋 드라이버 업데이트 전 제거를 권장한다. 설치·제거 UX 문서(Phase 6)에 포함한다.
- Microsoft Visual C++ Redistributable 의존성 확인.

---

## 수정 제안: 개발 단계

| Phase | 변경 내용 |
| :--- | :--- |
| Phase 1 | 변경 없음 |
| Phase 2 | **실제 iPad Safari 테스트**, **HTTPS/WSS 동작 확인(#3)**, **H.264 레벨 확인(#1)** 추가. 기술 스택(#5) 확정 후 시작 |
| Phase 3 | URL + 페어링 코드 표시(#9) 추가 |
| Phase 4 | **Client 해상도 맞춤(#4)** 이동 |
| Phase 5 | ScreenCaptureKit 디스플레이 혼동 검증(#11) 추가 |
| Phase 6 | 해상도 변경 항목 제거(#4로 이동). VDD 운영 주의(#16) 반영 |
| Phase 7 (신규) | 입력(터치/펜) DataChannel 지원(#7) |

---

## 참고 링크

- Virtual-Display-Driver: https://github.com/VirtualDrivers/Virtual-Display-Driver
- go-macos/virtualdisplay (CGVirtualDisplay Private API 안내): https://pkg.go.dev/github.com/go-macos/virtualdisplay
- Apple Developer Forums, Private API 공증 관련: https://developer.apple.com/forums/thread/702740
- Apple Developer Forums, ScreenCaptureKit 가상 디스플레이 혼동: https://developer.apple.com/forums/thread/786829
