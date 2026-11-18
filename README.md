# SlimeDisplay 크로스 플랫폼 가상 디스플레이 스트리밍

- **프로젝트명:** SlimeDisplay
- **Host:** Windows / macOS
- **Client:** Windows / macOS / iOS / Android
- **Client 구현:** Web (HTML, CSS, TypeScript)
- **영상 전송:** WebRTC
- **Signaling:** WebSocket
- **LAN Discovery:** mDNS / DNS-SD
- **Windows 가상 디스플레이:** 기존 VDD 재사용
- **macOS 가상 디스플레이:** `CGVirtualDisplay` 기반 실험

---

## 1. 프로젝트 개요

**Beam**은 Windows 또는 macOS 컴퓨터를 Host로 사용하여 가상 확장 디스플레이를 생성하고, 동일 네트워크에 연결된 다른 기기에서 해당 디스플레이를 브라우저로 확인할 수 있도록 하는 프로젝트다.

Host는 OS에서 실제 모니터처럼 인식되는 가상 디스플레이를 생성한다. 생성된 화면을 캡처하고 H.264로 인코딩한 뒤 WebRTC를 통해 Client에 전송한다. Client는 별도의 프로그램 설치 없이 브라우저에서 Host에 접속하여 가상 디스플레이 화면을 표시한다.

### 1.1 핵심 목표

1. Host OS에 가상 확장 디스플레이 생성
2. 가상 디스플레이의 화면 캡처
3. H.264 하드웨어 인코딩
4. 동일 LAN에서 Host 검색 및 연결
5. WebRTC 기반 실시간 영상 스트리밍
6. 모든 Client 플랫폼에서 웹 기반 화면 표시
7. OS별 디스플레이 구현을 독립적인 Backend로 분리

### 1.2 기본 동작 구조

```text
                  Beam Host
                      |
             Virtual Display
                      |
                Frame Capture
                      |
                H.264 Encoder
                      |
                   WebRTC
                      |
              LAN / Wi-Fi
                      |
                Web Browser
                      |
                Video Display
```

> **참고:** 브라우저 Client는 가상 디스플레이를 직접 생성하지 않는다. 가상 디스플레이는 Host OS에서 생성되며, Client는 해당 화면을 영상으로 수신한다.

---

## 2. 가상 디스플레이 구현 전략

Windows와 macOS는 서로 다른 가상 디스플레이 구현 방식을 사용한다.

| 플랫폼      | 구현 방식                   | 개발 전략         |
| :---------- | :-------------------------- | :---------------- |
| **Windows** | IDD / IddCx 기반 VDD        | 기존 VDD 재사용   |
| **macOS**   | `CGVirtualDisplay` 계열 API | 별도 Backend 구현 |

### 2.1 Windows: 기존 VDD 활용

Windows에서는 직접 Indirect Display Driver(IDD)를 개발하지 않고, 이미 구현된 Virtual Display Driver(VDD)를 활용한다.

* **우선 검토할 프로젝트:** Virtual-Display-Driver
  * **Repository:** https://github.com/VirtualDrivers/Virtual-Display-Driver
  * **Releases:** https://github.com/VirtualDrivers/Virtual-Display-Driver/releases
  * **Wiki:** https://github.com/VirtualDrivers/Virtual-Display-Driver/wiki
* **주요 특징:**
  * Windows 10/11 가상 모니터 생성
  * 해상도 및 주사율 설정
  * SDR/HDR 관련 기능
  * Windows 디스플레이 설정에서 모니터로 인식
  * 기존 IDD/IddCx 구현 재사용 가능

#### 기존 VDD를 사용하는 이유

Windows에서 자체 IDD를 개발하면 다음 작업이 필요하다.

* IddCx 기반 드라이버 구현
* 드라이버 빌드 및 테스트
* 드라이버 서명
* 설치 프로그램 및 관리자 권한 처리
* Windows 버전별 호환성 검증

기존 VDD를 사용하면 드라이버 개발을 생략하고 Beam의 핵심 기능인 화면 캡처와 스트리밍에 집중할 수 있다.

> **중요:** VDD는 IDD를 대체하는 별도의 기술이 아니다. VDD 자체가 IDD 기반으로 구현된 가상 디스플레이 드라이버다. 따라서 직접 IDD를 개발하는 대신 기존 IDD 구현과 배포 바이너리를 재사용하는 전략이다.

#### 드라이버 서명 및 배포

기존 VDD의 서명된 바이너리를 그대로 사용하는 경우 직접 드라이버를 빌드하고 서명하는 과정을 피할 수 있다. 다만 다음 사항을 확인해야 한다.

* 실제 사용할 Release 버전의 드라이버 서명 상태
* Windows 10/11 및 아키텍처 호환성
* 드라이버 인증서 체인
* 드라이버 재배포 라이선스
* 설치 및 제거 절차
* 향후 드라이버 업데이트 정책

VDD 소스를 수정하거나 자체 빌드한 드라이버를 배포하면 기존 서명이 유지되지 않는다. 초기 버전에서는 VDD를 별도 설치하도록 하고 Beam은 설치 여부를 확인하는 방식으로 구현한다.

### 2.2 Windows VDD 연동 구조

```text
              Beam Host
                  |
           Display Manager
                  |
          Windows VDD Backend
                  |
          Existing VDD Driver
                  |
          Windows Display System
                  |
             Virtual Monitor
                  |
          Windows Graphics Capture
                  |
             D3D Texture
                  |
           Hardware Encoder
                  |
                WebRTC
```

Beam은 VDD의 내부 드라이버 코드를 수정하지 않는다.

#### Beam이 담당하는 기능

1. VDD 설치 여부 확인
2. Windows 디스플레이 장치 열거
3. 가상 모니터 식별
4. 스트리밍할 디스플레이 선택
5. 선택된 디스플레이 캡처
6. 인코딩 및 전송

초기 버전에서는 VDD의 설정 파일을 직접 수정하기보다는 VDD가 생성한 디스플레이를 Windows API로 열거하고 선택하는 방식을 우선한다.

#### 디스플레이 식별

모니터 인덱스만으로 대상을 식별하면 재부팅이나 실제 모니터 연결 변경 후 다른 모니터를 선택할 수 있다. 따라서 다음 정보를 활용한다.

* Display Device Path
* Device Name
* Monitor Device Information
* 현재 해상도
* 활성화 상태

가능하면 안정적인 장치 식별자를 저장하고, 식별에 실패하면 사용자가 다시 선택하도록 한다.

### 2.3 Windows 화면 캡처

* **캡처 기술 후보:**
  * Windows Graphics Capture
  * Desktop Duplication API
* **권장 데이터 흐름:**

```text
Virtual Monitor
      |
Windows Display Composition
      |
Windows Graphics Capture
      |
D3D Texture
      |
Hardware H.264 Encoder
      |
WebRTC Video Track
```

GPU Surface 기반으로 처리하여 CPU 메모리로 프레임을 복사하는 과정을 최소화한다. 초기 구현에서는 선택한 VDD 모니터만 캡처할 수 있는지, 캡처 결과가 GPU 인코더로 전달되는지 검증해야 한다.

### 2.4 macOS: CGVirtualDisplay

macOS에서는 Windows VDD를 사용할 수 없으므로 별도의 Virtual Display Backend를 구현한다.

* **검토할 API:**
  * `CGVirtualDisplay`
  * `CGVirtualDisplayDescriptor`
  * `CGVirtualDisplaySettings`
  * `CGVirtualDisplayMode`

주의할 점은 `CGVirtualDisplay` 계열 API가 일반적인 공개 API와 동일한 안정성을 보장하지 않는다는 것이다. Private API 의존성과 macOS 업데이트에 따른 호환성 문제를 고려해야 한다.

* **예상 구조:**

```text
CGVirtualDisplay
      |
macOS WindowServer
      |
ScreenCaptureKit
      |
VideoToolbox
      |
WebRTC
```

macOS에서는 다음 사항을 별도로 검증한다.

* 가상 디스플레이 생성 및 제거
* macOS 버전별 호환성
* 화면 캡처 권한
* VideoToolbox 인코딩
* Private API 사용에 따른 배포 제한
* 절전 및 재부팅 후 복구

Windows와 macOS의 구현은 서로 독립적으로 유지한다.

---

## 3. 전체 시스템 아키텍처

```text
+----------------------------------------------------------+
|                       Beam Host                          |
|                                                          |
|  +-------------------+                                   |
|  | Session Manager   |                                   |
|  +-------------------+                                   |
|           |                                              |
|  +-------------------+       +------------------------+  |
|  | Display Manager   |       | Signaling Server       |  |
|  +-------------------+       | WebSocket              |  |
|           |                  +------------------------+  |
|           |                            |                 |
|     +-----+------+                     |                 |
|     |            |                     |                 |
|     v            v                     |                 |
|  Windows       macOS                   |                 |
|  VDD Backend   VirtualDisplay          |                 |
|     |            |                     |                 |
|     +------+-----+                     |                 |
|            |                           |                 |
|       Frame Capture                    |                 |
|            |                           |                 |
|       H.264 Encoder                    |                 |
|            |                           |                 |
|          WebRTC -------------------------+                 |
|                                                          |
+----------------------------------------------------------+
                       |
                  LAN / Wi-Fi
                       |
                       v
+----------------------------------------------------------+
|                     Web Client                           |
|                                                          |
|  +-------------------+                                   |
|  | Connection UI     |                                   |
|  +-------------------+                                   |
|           |                                              |
|  +-------------------+                                   |
|  | WebSocket         |                                   |
|  | Signaling         |                                   |
|  +-------------------+                                   |
|           |                                              |
|  +-------------------+                                   |
|  | RTCPeerConnection |                                   |
|  +-------------------+                                   |
|           |                                              |
|       HTML Video                                         |
|                                                          |
+----------------------------------------------------------+
```

### 3.1 모듈별 역할

| 모듈                              | 역할                               |
| :-------------------------------- | :--------------------------------- |
| **Display Manager**               | 가상 디스플레이 생성 및 상태 관리  |
| **Windows VDD Backend**           | 기존 VDD와 Windows 디스플레이 연동 |
| **macOS Virtual Display Backend** | macOS 가상 디스플레이 생성         |
| **Capture Backend**               | 선택된 디스플레이 화면 캡처        |
| **Encoder**                       | H.264 영상 인코딩                  |
| **Discovery**                     | LAN에서 Host 검색                  |
| **Signaling**                     | WebSocket 기반 연결 협상           |
| **WebRTC Transport**              | 영상 스트리밍                      |
| **Web Client**                    | 영상 표시 및 연결 UI               |

---

## 4. LAN Discovery

Host와 Client는 동일 네트워크에 연결되어 있을 때 서로를 검색하고 연결할 수 있어야 한다.

### 4.1 mDNS / DNS-SD

LAN 검색에는 mDNS/DNS-SD를 사용한다.

* **서비스 타입:** `_beam._tcp.local.`
* **Host 광고 정보:**
  * Host ID
  * Host Name
  * Signaling Port
  * Protocol Version
  * 지원 코덱
  * 디스플레이 사용 가능 여부

mDNS는 Host 검색에만 사용하며 인증 용도로 사용하지 않는다.

### 4.2 브라우저 검색 제약

일반 브라우저 JavaScript에는 mDNS 서비스를 직접 검색하는 범용 API가 없다. 따라서 Host가 mDNS를 광고하더라도 브라우저에서 자동으로 주변 Host 목록을 검색할 수 있는 것은 아니다.

초기 MVP에서는 **QR Code 기반 접속**을 사용한다.

```text
Host
 |
 +-- mDNS Advertisement
 |
 +-- Generate QR Code
          |
          v
      Web Client
          |
     Scan QR Code
          |
     Connect to Host
```

QR 코드에는 Host 주소와 일회용 Pairing Token을 포함할 수 있다. 추후 네이티브 Discovery Helper를 개발하면 Bonjour 검색 결과를 브라우저에 전달하는 방식으로 확장할 수 있다.

### 4.3 네트워크 고려사항

* Wi-Fi AP Isolation
* Guest Network
* VLAN
* Windows Firewall
* macOS Local Network Permission
* VPN 및 다른 서브넷 환경

mDNS 검색 성공과 WebRTC 연결 성공은 별개다.

---

## 5. Signaling 및 WebRTC

### 5.1 WebSocket의 역할

WebSocket은 영상 전송이 아니라 세션 관리와 WebRTC 연결 협상에 사용한다.

* Host/Client 연결
* Pairing 및 인증
* SDP Offer / Answer 전달
* ICE Candidate 교환
* 스트리밍 시작 및 종료
* 해상도 및 FPS 변경
* 연결 상태 전달

### 5.2 WebRTC의 역할

WebRTC는 실제 영상 스트림을 전달한다.

* H.264 Video Track
* RTP / SRTP
* ICE
* DTLS
* 향후 DataChannel을 통한 입력 제어

### 5.3 연결 과정

```text
Browser Client                 Beam Host
      |                            |
      |------ WebSocket ---------->|
      |                            |
      |------ SDP Offer ---------->|
      |<----- SDP Answer ----------|
      |                            |
      |<---- ICE Candidates ------>|
      |                            |
      |======= WebRTC =============|
      |                            |
      |       Video Stream         |
      |                            |
```

초기 LAN 환경에서는 직접 연결을 우선 구현한다. 다른 네트워크나 NAT 환경까지 지원하려면 STUN/TURN 서버를 추가한다.

### 5.4 코덱

초기 코덱은 **H.264**를 사용한다.

* **Host:**
  * Windows: 하드웨어 H.264 인코더
  * macOS: VideoToolbox
* **Client:**
  * Browser WebRTC Decoder
  * `HTMLVideoElement`

브라우저는 `RTCPeerConnection`의 `ontrack` 이벤트로 수신한 `MediaStream`을 `<video>`에 연결한다.

---

## 6. 보안 설계

동일 LAN이라고 해서 모든 장치를 신뢰해서는 안 된다.

### 6.1 Pairing

1. Host가 일회용 Pairing Token 생성
2. QR Code 생성
3. Client가 QR Code 스캔
4. Host에 연결 요청
5. Host가 연결 승인
6. Session Token 발급
7. WebRTC 연결 시작

### 6.2 보안 요구사항

* WSS 기반 Signaling
* WebRTC DTLS-SRTP
* Pairing Token 만료
* Token 재사용 제한
* Host 측 연결 승인
* 연결된 Client 관리
* 세션 종료 기능
* mDNS에 비밀 정보 저장 금지

원격 키보드 및 마우스 제어 기능은 초기 버전에서 기본 비활성화한다.

### 6.3 인증서

브라우저의 보안 컨텍스트 정책을 고려해 HTTPS/WSS를 사용한다. 로컬 네트워크에서 Self-signed Certificate를 사용하는 경우 기기별 인증서 신뢰 설정이 필요할 수 있으므로 인증서 발급 및 설치 UX를 별도로 설계해야 한다.

---

## 7. Web Client

모든 Client는 동일한 웹 애플리케이션을 사용한다.

* **기술 스택:** HTML, CSS, TypeScript, WebSocket, WebRTC API

### 7.1 지원 플랫폼

| 플랫폼      | 브라우저        |
| :---------- | :-------------- |
| **Windows** | Chrome / Edge   |
| **macOS**   | Safari / Chrome |
| **iOS**     | Safari          |
| **Android** | Chrome          |

### 7.2 주요 기능

* Host 연결
* QR Code Pairing
* 연결 상태 표시
* 실시간 영상 재생
* 전체 화면 전환
* 해상도 및 FPS 표시
* 연결 종료

### 7.3 영상 수신 예시

```typescript
const pc = new RTCPeerConnection();

pc.ontrack = (event) => {
    const video = document.querySelector("video");
    if (video) {
        video.srcObject = event.streams[0];
        video.play();
    }
};
```

---

## 8. 성능 설계

### 초기 목표

| 항목         | 목표              |
| :----------- | :---------------- |
| **해상도**   | 1920 × 1080       |
| **FPS**      | 최대 60           |
| **코덱**     | H.264             |
| **인코딩**   | Hardware Encoding |
| **네트워크** | LAN               |
| **Client**   | Browser           |

이는 초기 테스트 목표이며 실제 성능은 하드웨어, 네트워크, 브라우저에 따라 달라진다.

### 8.1 성능 측정

다음 구간을 개별적으로 측정한다.

1. Capture Latency
2. Encoding Latency
3. Network Latency
4. Decoding Latency
5. Rendering Latency

평균 지연시간뿐 아니라 Frame Drop, Jitter, 장시간 실행 안정성도 측정한다.

### 8.2 최적화

* GPU Surface 기반 처리
* CPU 메모리 복사 최소화
* Hardware Encoder 사용
* Frame Queue 제한
* 오래된 프레임 Drop
* 네트워크 상태에 따른 Bitrate 조절
* 필요 시 FPS 및 해상도 조절

---

## 9. 저장소 구조

```text
beam/
|
+-- host/
|   |
|   +-- common/
|   |   +-- session/
|   |   +-- signaling/
|   |   +-- discovery/
|   |   +-- transport/
|   |   +-- config/
|   |
|   +-- windows/
|   |   +-- vdd/
|   |   +-- display/
|   |   +-- capture/
|   |   +-- encoder/
|   |
|   +-- macos/
|       +-- virtual_display/
|       +-- capture/
|       +-- encoder/
|
+-- client/
|   |
|   +-- web/
|       +-- src/
|           +-- connection/
|           +-- signaling/
|           +-- webrtc/
|           +-- display/
|           +-- ui/
|
+-- protocol/
|   +-- signaling-schema/
|   +-- versioning/
|
+-- docs/
|   +-- architecture.md
|   +-- protocol.md
|   +-- security.md
|   +-- windows-vdd.md
|
+-- tests/
    +-- discovery/
    +-- streaming/
    +-- compatibility/
```

---

## 10. 개발 단계

### Phase 1. Windows VDD 검증

* Virtual-Display-Driver 설치
* Windows 디스플레이 설정에서 가상 모니터 확인
* 재부팅 후 디스플레이 유지 확인
* 해상도 변경 확인
* 가상 모니터 식별 방법 확인
* 가상 모니터 화면 캡처
* GPU 기반 캡처 경로 검증
* **완료 조건:** 기존 VDD를 통해 생성된 모니터를 식별하고 화면을 캡처할 수 있다.

### Phase 2. WebRTC PoC

* 테스트 영상 입력
* H.264 하드웨어 인코딩
* WebSocket Signaling
* WebRTC 연결
* 브라우저 영상 재생
* 1080p/60 FPS 성능 측정
* **완료 조건:** 동일 LAN의 브라우저에서 실시간 영상 재생이 가능하다.

### Phase 3. LAN Discovery

* mDNS 광고
* QR Code 생성
* Client QR 연결
* Pairing Token
* Host 승인 및 세션 종료
* **완료 조건:** QR Code를 이용해 Host에 연결할 수 있다.

### Phase 4. Windows 통합

* VDD 설치 상태 확인
* 가상 디스플레이 열거
* 디스플레이 선택 UI
* 선택한 화면 캡처
* H.264 인코딩
* WebRTC 전송
* **완료 조건:** Windows VDD 기반 가상 모니터 화면을 브라우저로 스트리밍할 수 있다.

### Phase 5. macOS 지원

* `CGVirtualDisplay` 조사
* 가상 디스플레이 생성
* ScreenCaptureKit 연동
* VideoToolbox 인코딩
* WebRTC 연결
* macOS 버전별 호환성 테스트
* **완료 조건:** macOS Host의 가상 디스플레이 생성과 스트리밍 가능성을 검증한다.

### Phase 6. 안정화

* 연결 복구
* 해상도 변경
* FPS 및 Bitrate 조정
* 장시간 스트리밍
* Client 호환성 테스트
* 설치 및 제거 UX
* 드라이버 라이선스 및 재배포 검토

---

## 11. 주요 기술적 리스크

| 리스크                      | 대응                                |
| :-------------------------- | :---------------------------------- |
| **VDD 드라이버 서명 변경**  | 배포할 Release 바이너리 서명 검증   |
| **VDD 업데이트 호환성**     | 드라이버 내부 구현 의존 최소화      |
| **디스플레이 식별자 변경**  | 장치 정보 기반 재탐색               |
| **macOS Private API 변경**  | 별도 Backend로 격리                 |
| **브라우저 mDNS 검색 제한** | QR Code 기반 연결                   |
| **AP Isolation**            | 네트워크 진단 기능                  |
| **H.264 호환성**            | 플랫폼별 브라우저 테스트            |
| **HTTPS 인증서 문제**       | 인증서 신뢰 및 설치 UX              |
| **GPU 인코딩 지연**         | 하드웨어 인코더 및 GPU Surface 활용 |

---

## 12. MVP 범위 및 최종 방향

초기 MVP는 **Windows Host와 기존 VDD**를 중심으로 개발한다.

* Windows에서 기존 VDD를 설치해 가상 모니터 생성
* Beam Host에서 가상 모니터 식별
* 선택된 모니터 화면 캡처
* H.264 하드웨어 인코딩
* WebRTC 영상 전송
* QR Code로 Client 연결
* Windows/macOS/iOS/Android 브라우저에서 영상 표시

macOS Host는 `CGVirtualDisplay` 관련 기술 및 배포 가능성을 검증한 후 추가한다. 핵심 전략은 Windows IDD를 직접 개발하지 않고 기존 VDD를 재사용함으로써 드라이버 구현과 서명 문제를 초기 개발 범위에서 제외하는 것이다.

---

## 13. 참고 자료

* **Windows VDD**
  * Virtual-Display-Driver GitHub
  * Virtual-Display-Driver Releases
  * Microsoft IDD Model Overview
  * Microsoft IDD Sample
* **Windows Capture**
  * Windows Graphics Capture
* **WebRTC**
  * WebRTC Peer Connections
  * MDN RTCPeerConnection
  * W3C WebRTC Specification
* **macOS**
  * Apple ScreenCaptureKit
  * Apple VideoToolbox

> **설계상 주의:** VDD의 서명 상태, 재배포 권한, 설치 인터페이스 및 macOS Private API의 배포 가능성은 실제 사용할 버전과 배포 환경에서 별도로 검증해야 한다.
