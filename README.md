# SlimeDisplay Cross-Platform Virtual Display Streaming

- **Project:** SlimeDisplay
- **Host:** Windows / macOS
- **Client:** Windows / macOS / iOS / Android
- **Client implementation:** Web (HTML, CSS, TypeScript)
- **Video transport:** WebRTC
- **Signaling:** WebSocket
- **LAN discovery:** mDNS / DNS-SD
- **Windows virtual display:** Reuse an existing VDD
- **macOS virtual display:** Experiment with `CGVirtualDisplay`

---

## 1. Project Overview

**Beam** is a project that uses a Windows or macOS computer as a Host to create a virtual extended display, then lets another device on the same network view that display in a browser.

The Host creates a virtual display recognized by the OS as a physical monitor. It captures the display, encodes it as H.264, and sends it to the Client over WebRTC. The Client connects to the Host in a browser and displays the virtual screen without installing a separate application.

### 1.1 Core Goals

1. Create a virtual extended display on the Host OS.
2. Capture the virtual display.
3. Hardware-encode video as H.264.
4. Discover and connect to the Host on the same LAN.
5. Stream video in real time using WebRTC.
6. Display the video in a web app on every Client platform.
7. Separate OS-specific display implementations into independent backends.

### 1.2 Basic Operation

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

> **Note:** The browser Client does not create a virtual display. The Host OS creates it, and the Client receives its image as video.

---

## 2. Virtual Display Implementation Strategy

Windows and macOS use different approaches to virtual display implementation.

| Platform | Implementation | Strategy |
|---|---|---|
| **Windows** | IDD / IddCx-based VDD | Reuse an existing VDD |
| **macOS** | `CGVirtualDisplay`-family APIs | Implement a separate backend |

### 2.1 Windows: Use an Existing VDD

On Windows, use an existing Virtual Display Driver (VDD) instead of developing an Indirect Display Driver (IDD) from scratch.

- **Project to evaluate first:** Virtual-Display-Driver
  - **Repository:** https://github.com/VirtualDrivers/Virtual-Display-Driver
  - **Releases:** https://github.com/VirtualDrivers/Virtual-Display-Driver/releases
  - **Wiki:** https://github.com/VirtualDrivers/Virtual-Display-Driver/wiki
- **Key features:**
  - Creates virtual monitors on Windows 10/11.
  - Supports resolution and refresh-rate configuration.
  - Includes SDR/HDR-related features.
  - Appears as a monitor in Windows Display settings.
  - Reuses an existing IDD/IddCx implementation.

#### Why Use an Existing VDD?

Developing an IDD on Windows requires:

- Implementing a driver based on IddCx.
- Building and testing the driver.
- Signing the driver.
- Handling the installer and administrator privileges.
- Verifying compatibility across Windows versions.

Using an existing VDD avoids driver development and lets the project focus on Beam's core features: capture and streaming.

> **Important:** A VDD is not a technology separate from an IDD. A VDD is itself a virtual display driver implemented using IDD. The strategy is to reuse an existing IDD implementation and its distributed binaries instead of writing an IDD from scratch.

#### Driver Signing and Distribution

Using the existing signed VDD binaries as-is can avoid building and signing a driver. Still, verify:

- The signing status of the Release driver that will actually be used.
- Compatibility with Windows 10/11 and the target architecture.
- The driver certificate chain.
- The license terms for redistributing the driver.
- Installation and removal procedures.
- The policy for future driver updates.

Modifying the VDD source or distributing a self-built driver will not retain the existing signature. For the initial version, install the VDD separately and have Beam check whether it is installed.

### 2.2 Windows VDD Integration

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

Beam does not modify the VDD's internal driver code.

#### Beam Responsibilities

1. Check whether the VDD is installed.
2. Enumerate Windows display devices.
3. Identify the virtual monitor.
4. Select the display to stream.
5. Capture the selected display.
6. Encode and transmit the video.

For the initial version, prefer enumerating and selecting the display created by the VDD through Windows APIs rather than directly editing the VDD settings file.

#### Display Identification

Identifying a display only by monitor index can select a different monitor after a reboot or physical-monitor change. Use information such as:

- Display Device Path
- Device Name
- Monitor Device Information
- Current resolution
- Active state

Store a stable device identifier where possible, and ask the user to select again if identification fails.

### 2.3 Windows Screen Capture

- **Capture technology candidates:**
  - Windows Graphics Capture
  - Desktop Duplication API
- **Recommended data flow:**

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

Use GPU surfaces to minimize copies of frames into CPU memory. In the initial implementation, verify that only the selected VDD monitor can be captured and that the capture output can be passed to a GPU encoder.

### 2.4 macOS: CGVirtualDisplay

macOS cannot use the Windows VDD, so implement a separate Virtual Display Backend.

- **APIs to investigate:**
  - `CGVirtualDisplay`
  - `CGVirtualDisplayDescriptor`
  - `CGVirtualDisplaySettings`
  - `CGVirtualDisplayMode`

The `CGVirtualDisplay` family of APIs may not provide the same stability as ordinary public APIs. Account for private API dependencies and compatibility issues after macOS updates.

- **Expected flow:**

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

Verify the following separately on macOS:

- Virtual display creation and removal.
- Compatibility across macOS versions.
- Screen-capture permissions.
- VideoToolbox encoding.
- Distribution restrictions resulting from private API use.
- Recovery after sleep and reboot.

Keep the Windows and macOS implementations independent.

---

## 3. Overall System Architecture

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
|          WebRTC -------------------------+               |
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

### 3.1 Module Responsibilities

| Module | Responsibility |
|---|---|
| **Display Manager** | Creates virtual displays and manages their state |
| **Windows VDD Backend** | Integrates the existing VDD with Windows displays |
| **macOS Virtual Display Backend** | Creates virtual displays on macOS |
| **Capture Backend** | Captures the selected display |
| **Encoder** | Encodes H.264 video |
| **Discovery** | Finds the Host on the LAN |
| **Signaling** | Coordinates connections over WebSocket |
| **WebRTC Transport** | Streams video |
| **Web Client** | Displays video and provides the connection UI |

---

## 4. LAN Discovery

The Host and Client must be able to discover and connect to each other when connected to the same network.

### 4.1 mDNS / DNS-SD

Use mDNS/DNS-SD for LAN discovery.

- **Service type:** `_beam._tcp.local.`
- **Host advertisement information:**
  - Host ID
  - Host Name
  - Signaling Port
  - Protocol Version
  - Supported codecs
  - Display availability

Use mDNS only to discover Hosts, not for authentication.

### 4.2 Browser Discovery Limitations

Ordinary browser JavaScript has no general-purpose API for directly discovering mDNS services. Therefore, advertising mDNS does not let the browser automatically list nearby Hosts.

For the initial MVP, use **QR-code-based connection**.

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

The QR code can contain the Host address and a one-time Pairing Token. A future native Discovery Helper could pass Bonjour search results to the browser.

### 4.3 Network Considerations

- Wi-Fi AP Isolation
- Guest Network
- VLAN
- Windows Firewall
- macOS Local Network Permission
- VPN and other-subnet environments

Successful mDNS discovery does not guarantee a successful WebRTC connection.

---

## 5. Signaling and WebRTC

### 5.1 WebSocket Responsibilities

WebSocket is used for session management and WebRTC connection negotiation, not for video transmission.

- Host/Client connection
- Pairing and authentication
- SDP Offer / Answer exchange
- ICE Candidate exchange
- Starting and stopping a stream
- Changing resolution and FPS
- Communicating connection state

### 5.2 WebRTC Responsibilities

WebRTC carries the actual video stream.

- H.264 Video Track
- RTP / SRTP
- ICE
- DTLS
- Future input control through a DataChannel

### 5.3 Connection Flow

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

Prioritize direct connections in the initial LAN environment. Add STUN/TURN servers if other networks or NAT environments must be supported.

### 5.4 Codec

Use **H.264** as the initial codec.

- **Host:**
  - Windows: Hardware H.264 encoder
  - macOS: VideoToolbox
- **Client:**
  - Browser WebRTC decoder
  - `HTMLVideoElement`

The browser connects the `MediaStream` received through the `RTCPeerConnection` `ontrack` event to a `<video>` element.

---

## 6. Security Design

Do not assume that every device on the same LAN is trusted.

### 6.1 Pairing

1. The Host generates a one-time Pairing Token.
2. The Host creates a QR code.
3. The Client scans the QR code.
4. The Client requests a connection to the Host.
5. The Host approves the connection.
6. The Host issues a Session Token.
7. The WebRTC connection starts.

### 6.2 Security Requirements

- WSS-based signaling
- WebRTC DTLS-SRTP
- Pairing Token expiration
- Preventing token reuse
- Host-side connection approval
- Managing connected Clients
- Session termination
- No secrets in mDNS records

Remote keyboard and mouse control is disabled by default in the initial version.

### 6.3 Certificates

Use HTTPS/WSS to meet browser secure-context requirements. If using a self-signed certificate on a local network, each device may need to be configured to trust it. Design the certificate issuance and installation UX separately.

---

## 7. Web Client

All Clients use the same web application.

- **Technology stack:** HTML, CSS, TypeScript, WebSocket, WebRTC API

### 7.1 Supported Platforms

| Platform | Browser |
|---|---|
| **Windows** | Chrome / Edge |
| **macOS** | Safari / Chrome |
| **iOS** | Safari |
| **Android** | Chrome |

### 7.2 Main Features

- Connect to a Host
- QR-code pairing
- Display connection state
- Play video in real time
- Toggle fullscreen
- Display resolution and FPS
- Disconnect

### 7.3 Video Reception Example

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

## 8. Performance Design

### Initial Targets

| Item | Target |
|---|---|
| **Resolution** | 1920 × 1080 |
| **FPS** | Up to 60 |
| **Codec** | H.264 |
| **Encoding** | Hardware encoding |
| **Network** | LAN |
| **Client** | Browser |

These are initial test targets. Actual performance depends on hardware, network, and browser.

### 8.1 Performance Measurement

Measure each of the following separately:

1. Capture Latency
2. Encoding Latency
3. Network Latency
4. Decoding Latency
5. Rendering Latency

Measure frame drops, jitter, and long-running stability in addition to average latency.

### 8.2 Optimization

- Use GPU-surface-based processing.
- Minimize CPU-memory copies.
- Use a hardware encoder.
- Limit the frame queue.
- Drop stale frames.
- Adjust bitrate based on network conditions.
- Adjust FPS and resolution when needed.

---

## 9. Repository Layout

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

## 10. Development Phases

### Phase 1. Validate the Windows VDD

- Install Virtual-Display-Driver.
- Verify the virtual monitor in Windows Display settings.
- Verify the display remains after reboot.
- Verify resolution changes.
- Confirm how to identify the virtual monitor.
- Capture the virtual monitor.
- Verify the GPU-based capture path.
- **Exit criteria:** Identify the monitor created by the existing VDD and capture its screen.

### Phase 2. WebRTC Proof of Concept

- Test-video input
- H.264 hardware encoding
- WebSocket signaling
- WebRTC connection
- Browser video playback
- 1080p/60 FPS performance measurement
- **Exit criteria:** Play real-time video in a browser on the same LAN.

### Phase 3. LAN Discovery

- mDNS advertisement
- QR-code generation
- Client QR connection
- Pairing Token
- Host approval and session termination
- **Exit criteria:** Connect to the Host using a QR code.

### Phase 4. Windows Integration

- Check VDD installation state.
- Enumerate virtual displays.
- Add display-selection UI.
- Capture the selected display.
- Encode with H.264.
- Transmit over WebRTC.
- **Exit criteria:** Stream the Windows VDD virtual-monitor screen to a browser.

### Phase 5. macOS Support

- Investigate `CGVirtualDisplay`.
- Create a virtual display.
- Integrate ScreenCaptureKit.
- Encode with VideoToolbox.
- Connect through WebRTC.
- Test compatibility across macOS versions.
- **Exit criteria:** Verify the feasibility of creating and streaming a macOS Host virtual display.

### Phase 6. Stabilization

- Connection recovery
- Resolution changes
- FPS and bitrate adjustment
- Long-running streaming
- Client compatibility testing
- Installation and removal UX
- Driver-license and redistribution review

---

## 11. Key Technical Risks

| Risk | Mitigation |
|---|---|
| **VDD driver signature changes** | Verify the signature of the Release binary used for distribution |
| **VDD update compatibility** | Minimize dependence on internal driver implementation |
| **Display identifier changes** | Rediscover the display using device information |
| **macOS private API changes** | Isolate the implementation in a separate backend |
| **Browser mDNS discovery limitations** | Use QR-code-based connection |
| **AP Isolation** | Provide network diagnostics |
| **H.264 compatibility** | Test browsers on each platform |
| **HTTPS certificate issues** | Design certificate-trust and installation UX |
| **GPU encoding latency** | Use hardware encoders and GPU surfaces |

---

## 12. MVP Scope and Direction

The initial MVP focuses on a **Windows Host and an existing VDD**.

- Install an existing VDD on Windows to create a virtual monitor.
- Identify the virtual monitor in the Beam Host.
- Capture the selected monitor.
- Hardware-encode H.264.
- Transmit video over WebRTC.
- Connect the Client with a QR code.
- Display video in a browser on Windows/macOS/iOS/Android.

Add a macOS Host after validating the technology and distribution feasibility of `CGVirtualDisplay`. The core strategy is to reuse an existing VDD rather than develop a Windows IDD, keeping driver implementation and signing out of the initial development scope.

---

## 13. References

- **Windows VDD**
  - Virtual-Display-Driver GitHub
  - Virtual-Display-Driver Releases
  - Microsoft IDD Model Overview
  - Microsoft IDD Sample
- **Windows Capture**
  - Windows Graphics Capture
- **WebRTC**
  - WebRTC Peer Connections
  - MDN RTCPeerConnection
  - W3C WebRTC Specification
- **macOS**
  - Apple ScreenCaptureKit
  - Apple VideoToolbox

> **Design note:** Verify the VDD's signing status, redistribution rights, installation interface, and distribution feasibility of macOS private APIs separately for the versions and distribution environments that will actually be used.
