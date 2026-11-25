# SlimeDisplay Client – 기술 설계 문서

**버전:** 1.0  
**프레임워크:** Tauri 2  
**프론트엔드:** React + TypeScript  
**백엔드:** Rust  
**미디어 전송:** WebRTC  
**시그널링:** WebSocket

## 1. 개요

SlimeDisplay Client는 SlimeDisplay Host에 연결하여 Host의 가상 디스플레이 화면을 원격으로 표시하는 크로스 플랫폼 데스크톱 애플리케이션이다.

Client는 Tauri 2를 사용하여 React 기반 프론트엔드와 Rust 백엔드를 결합한다. React는 UI와 브라우저 네이티브 WebRTC API를 담당한다. Tauri 백엔드는 mDNS 검색, Endpoint 관리, 로컬 설정 및 데스크톱 네이티브 기능을 담당한다.

### 설계 목표

- 공통 React 프론트엔드로 Windows와 macOS 지원
- mDNS/DNS-SD를 이용한 Host 자동 검색
- IP 주소와 포트를 이용한 수동 연결
- Endpoint 획득 방식과 관계없이 공통 연결 절차 사용
- WebSocket이 아닌 WebRTC를 통한 영상 수신
- Tauri Rust 백엔드에 네이티브 데스크톱 기능 분리

## 2. 기술 스택

| 영역 | 기술 | 역할 |
|---|---|---|
| 데스크톱 프레임워크 | Tauri 2 | 앱 셸 및 네이티브 통합 |
| 프론트엔드 | React | 사용자 인터페이스 |
| 언어 | TypeScript | 프론트엔드 구현 |
| 빌드 도구 | Vite | 프론트엔드 개발 및 빌드 |
| 백엔드 | Rust | Discovery 및 네이티브 기능 |
| 직렬화 | Serde / serde_json | Rust 구조체와 JSON 변환 |
| Discovery | mDNS/DNS-SD (`mdns-sd`) | Host 검색 |
| Signaling | WebSocket | SDP 및 ICE 교환 |
| 미디어 | WebRTC | 실시간 영상 전송 |
| 렌더링 | HTML video | 원격 영상 재생 |

Client는 우선 플랫폼 WebView의 WebRTC API를 사용한다. Windows는 WebView2, macOS는 WKWebView를 사용한다. 두 플랫폼 모두 H.264 및 WebRTC 호환성을 검증해야 한다.

## 3. 프로젝트 구조

```text
client/
├── package.json
├── vite.config.ts
├── index.html
├── src/
│   ├── main.tsx
│   ├── App.tsx
│   ├── components/
│   │   ├── ServerList.tsx
│   │   ├── ManualConnectionForm.tsx
│   │   ├── ConnectionStatus.tsx
│   │   └── VideoViewer.tsx
│   ├── pages/
│   │   ├── HomePage.tsx
│   │   └── ViewerPage.tsx
│   ├── hooks/
│   │   ├── useDiscovery.ts
│   │   ├── useConnection.ts
│   │   └── useWebRTC.ts
│   ├── api/
│   ├── discovery/
│   ├── signaling/
│   ├── webrtc/
│   ├── input/
│   └── state/
└── src-tauri/
    ├── Cargo.toml
    ├── tauri.conf.json
    ├── capabilities/
    └── src/
        ├── main.rs
        ├── lib.rs
        ├── commands/
        ├── discovery/
        ├── connection/
        └── preferences/
```

## 4. 애플리케이션 역할

### React 프론트엔드

- 애플리케이션 UI 렌더링
- 검색된 Host 표시 및 수동 Endpoint 입력
- 브라우저 `RTCPeerConnection` 관리
- WebSocket을 통한 Signaling 메시지 교환
- 수신한 `MediaStream` 렌더링
- 원격 마우스 및 키보드 입력 수집
- 연결 상태 및 오류 표시

### Tauri Rust 백엔드

- mDNS/DNS-SD 검색 수행
- 검색 결과를 정규화된 Endpoint로 변환
- Tauri Command/Event로 Discovery 제공
- 비밀 정보가 아닌 로컬 설정 관리
- 데스크톱 네이티브 기능 제공

Tauri 백엔드는 Client 백엔드이며 SlimeDisplay Host가 아니다. 가상 디스플레이 생성, Host 화면 캡처 및 인코딩을 수행하지 않는다.

## 5. Discovery 및 연결

제안하는 DNS-SD 서비스 타입:

```text
_slimedisplay._tcp.local.
```

Host는 제어용 Endpoint를 광고한다. DNS-SD TXT 레코드에 비밀번호, Token 또는 기타 비밀 정보를 포함해서는 안 된다.

mDNS와 수동 입력은 동일한 Endpoint 모델을 생성한다.

```typescript
export interface ServerEndpoint {
  host: string;
  port: number;
  source: "mdns" | "manual" | "recent";
  displayName?: string;
}
```

연결 흐름:

```text
mDNS 검색            수동 입력
    |                    |
    +---------+----------+
              |
       ServerEndpoint
              |
      Connection Manager
              |
         Host API/인증
              |
      WebSocket Signaling
              |
        WebRTC Session
```

두 경로는 Endpoint 검증, 인증, HTTP API, Signaling, WebRTC 설정, 오류 처리 및 재연결 로직을 공유한다. 검색에 실패하거나 검색된 Host에 연결할 수 없더라도 수동 IP/Port 입력을 제공한다.

## 6. 연결 생명주기

```text
Disconnected
    |
Discovering / Awaiting Endpoint
    |
Connecting
    |
Authenticating
    |
Negotiating WebRTC
    |
Connected
    |
Reconnecting / Failed
    |
Disconnected
```

연결 절차:

1. mDNS, 수동 입력 또는 최근 Endpoint에서 주소를 획득한다.
2. Endpoint를 검증하고 Host API URL을 구성한다.
3. Host에 인증한다.
4. Signaling WebSocket을 연결한다.
5. `RTCPeerConnection`을 생성한다.
6. SDP Offer/Answer와 ICE Candidate를 교환한다.
7. WebRTC 연결 및 원격 미디어 트랙 수신을 기다린다.
8. 수신한 `MediaStream`을 video element에 연결한다.

동시에 Offer가 생성되는 상황을 방지하도록 초기 Offerer를 프로토콜에서 명확히 지정한다.

## 7. WebSocket Signaling

WebSocket은 WebRTC 연결 설정 정보를 교환하는 용도이며 영상 전송 수단이 아니다.

주요 메시지:

- SDP Offer
- SDP Answer
- ICE Candidate
- Session Close
- Error / Status

메시지 타입 예시:

```typescript
export type SignalingMessage =
  | { type: "offer"; sdp: string }
  | { type: "answer"; sdp: string }
  | { type: "ice-candidate"; candidate: RTCIceCandidateInit }
  | { type: "session-close" }
  | { type: "error"; code: string; message: string };
```

실제 프로토콜에는 세션 ID를 포함하고 페이로드를 검증하며 메시지 버전을 관리해야 한다. 배포 보안 모델에 따라 `wss://`를 사용한다.

## 8. WebRTC 영상 수신

프론트엔드는 `RTCPeerConnection`을 생성하고 관리한다. ICE 설정에는 STUN 및 TURN 서버가 포함될 수 있다. 직접적인 Peer-to-Peer 연결이 불가능한 네트워크에서는 TURN이 필요하다.

원격 스트림은 HTML video element에 연결한다.

```tsx
<video
  ref={videoRef}
  autoPlay
  playsInline
  muted
/>
```

초기 목표 코덱은 H.264다. WebView2와 WKWebView에서 Profile, Level, Packetization Mode, SDP 파라미터, Keyframe 동작, 해상도, FPS 및 디코딩을 검증한다.

영상 프레임을 WebSocket이나 Tauri IPC로 전송해서는 안 된다. WebSocket은 Signaling, WebRTC는 미디어 전송에 사용한다.

## 9. 원격 입력

원격 입력은 Host 측 지원이 필요한 향후 기능이다. 마우스 이동, 버튼, 스크롤, 키보드 Key Down/Up 및 Modifier Key 등을 지원할 수 있다.

WebRTC DataChannel 또는 별도의 Host 제어 채널 중 하나를 인증된 전송 방식으로 선택한다. Host는 모든 입력 이벤트를 검증하고 권한을 확인해야 한다. 브라우저 예약 단축키는 Client에서 가로채지 못할 수 있다.

## 10. Tauri IPC 및 보안

mDNS 검색 및 설정 접근 등 네이티브 작업에는 Tauri Command/Event를 사용한다. 고주파 영상 프레임은 IPC로 전송하지 않는다.

최소 권한 원칙으로 Tauri Capability를 구성한다. mDNS 검색은 인증이 아니다. 검색된 Host와 수동 입력 Host 모두 동일한 인증 및 권한 확인을 거쳐야 한다. 일반 설정이나 DNS-SD TXT 레코드에 비밀 정보를 저장하지 않는다.

Tauri는 Windows에서 WebView2, macOS에서 WKWebView를 사용한다. 두 플랫폼에서 로컬 네트워크 요청, WebSocket, WebRTC, H.264, Autoplay 및 권한을 테스트해야 한다. 네이티브 mDNS는 브라우저 JavaScript mDNS API 의존성을 제거하지만 방화벽이나 멀티캐스트 제한을 우회하지는 않는다.

## 11. 사용자 설정

마지막으로 사용한 Endpoint와 비밀 정보가 아닌 UI 설정을 저장할 수 있다. 인증 정보와 Token은 저장이 필요하다면 적절한 보안 저장소를 사용한다. 최근 Endpoint도 검증 및 인증 절차를 거쳐야 한다.

## 12. 오류 처리 및 진단

Discovery 실패, Host 없음, 잘못된 Endpoint, Host 연결 불가, 인증 실패, API 버전 불일치, WebSocket 실패, SDP 실패, ICE 실패, 코덱 비호환, 재생 실패 및 Host에 의한 세션 종료를 구분한다.

로그에는 타임스탬프와 안전한 연결 식별자를 포함한다. 비밀번호나 Access Token을 기록하지 않는다.

## 13. 성능 요구사항

| 항목 | 초기 목표 |
|---|---|
| 해상도 | 1920 × 1080 |
| 프레임 레이트 | 최대 60 FPS |
| 코덱 | H.264 |
| 디코딩 | 가능한 경우 하드웨어 디코딩 |
| 활성 시청 세션 | 초기 1개 |
| 미디어 전송 | WebRTC |
| 버퍼링 | 저지연을 위해 최소화 |

정의된 테스트 환경에서 End-to-End Latency, Dropped Frame, Decode Time, Jitter, Packet Loss 및 재연결 시간을 측정한다.

## 14. 빌드 및 배포

공식 Tauri 2 React + TypeScript 템플릿으로 애플리케이션을 생성한다.

```bash
cd client
npm install
npm run tauri dev
```

Windows와 macOS에서 별도로 빌드하고 검증한다. Windows 코드 서명 및 설치 프로그램, macOS 서명 및 공증, WebView2 사용 가능 여부, 최소 macOS 버전, 방화벽 안내, Host/Client 프로토콜 호환성을 구성한다.

## 15. 개발 계획

1. **기반 구축:** Tauri 앱, React UI, Endpoint 및 API 타입
2. **Discovery:** mDNS/DNS-SD, Tauri IPC, 수동 Endpoint fallback
3. **연결:** Host API, 인증, WebSocket Signaling
4. **미디어:** PeerConnection, SDP/ICE, 양 플랫폼 H.264 재생
5. **안정성:** 원격 입력, 재연결, 진단, NAT/TURN 테스트
6. **배포:** Windows/macOS 패키징, 서명, 공증, 문서화

## 16. 최종 아키텍처 결정

SlimeDisplay Client는 **Tauri 2 + React + TypeScript**를 사용한다. React는 UI와 브라우저 네이티브 WebRTC Client를 구현한다. Tauri Rust 백엔드는 특히 mDNS/DNS-SD 검색과 로컬 설정 등 네이티브 데스크톱 기능을 제공한다.

mDNS와 수동 IP/Port 입력은 Endpoint를 획득하는 두 가지 방식이며, 하나의 연결, 인증, Signaling 및 WebRTC 흐름으로 합쳐진다. HTTP와 WebSocket은 API, 세션 제어 및 Signaling에 사용하고 WebRTC는 영상 미디어를 전달한다.

초기 배포 대상은 Windows와 macOS다. 배포 전에 WebView2와 WKWebView에서 H.264 및 WebRTC 동작을 검증해야 한다.