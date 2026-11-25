# SlimeDisplay Client – Technical Design Document

**Version:** 1.0  
**Framework:** Tauri 2  
**Frontend:** React + TypeScript  
**Backend:** Rust  
**Media Transport:** WebRTC  
**Signaling:** WebSocket

## 1. Overview

SlimeDisplay Client is a cross-platform desktop application that connects to a SlimeDisplay Host and displays its virtual display remotely.

The Client uses Tauri 2 to combine a React-based frontend with a Rust backend. React handles the UI and browser-native WebRTC APIs. The Tauri backend handles mDNS discovery, endpoint management, local preferences, and native desktop integration.

### Design Goals

- Support Windows and macOS through a shared React frontend.
- Discover Hosts automatically using mDNS/DNS-SD.
- Support manual connection using IP address and port.
- Use a unified connection workflow regardless of endpoint source.
- Receive video through WebRTC rather than WebSocket.
- Keep native desktop functionality in the Tauri Rust backend.

## 2. Technology Stack

| Area | Technology | Responsibility |
|---|---|---|
| Desktop Framework | Tauri 2 | Application shell and native integration |
| Frontend | React | User interface |
| Language | TypeScript | Frontend implementation |
| Build Tool | Vite | Frontend development and build |
| Backend | Rust | Discovery and native operations |
| Serialization | Serde / serde_json | Rust structures and JSON |
| Discovery | mDNS/DNS-SD (`mdns-sd`) | Host discovery |
| Signaling | WebSocket | SDP and ICE exchange |
| Media | WebRTC | Real-time video transport |
| Rendering | HTML video | Remote video playback |

The Client initially uses the WebRTC APIs provided by the platform WebView: WebView2 on Windows and WKWebView on macOS. H.264 and WebRTC compatibility must be validated on both platforms.

## 3. Project Structure

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

## 4. Application Responsibilities

### React Frontend

- Render the application UI.
- Display discovered Hosts and accept manual endpoints.
- Manage the browser `RTCPeerConnection`.
- Exchange signaling messages over WebSocket.
- Render the received `MediaStream`.
- Capture remote mouse and keyboard input.
- Display connection status and errors.

### Tauri Rust Backend

- Perform mDNS/DNS-SD discovery.
- Convert discovery results into normalized endpoints.
- Expose discovery through Tauri commands and events.
- Manage local, non-secret preferences.
- Provide native desktop integration.

The Tauri backend is the Client backend, not the SlimeDisplay Host. It does not create virtual displays or capture and encode Host video.

## 5. Discovery and Connection

The proposed DNS-SD service type is:

```text
_slimedisplay._tcp.local.
```

The Host advertises its control endpoint. DNS-SD TXT records must not contain passwords, tokens, or other secrets.

Both mDNS and manual input produce the same endpoint model:

```typescript
export interface ServerEndpoint {
  host: string;
  port: number;
  source: "mdns" | "manual" | "recent";
  displayName?: string;
}
```

The connection workflow is:

```text
mDNS Discovery       Manual Input
      |                    |
      +---------+----------+
                |
         ServerEndpoint
                |
        Connection Manager
                |
          Host API/Auth
                |
       WebSocket Signaling
                |
         WebRTC Session
```

Both paths share endpoint validation, authentication, HTTP API, signaling, WebRTC setup, error handling, and reconnection logic. If discovery fails or a discovered Host is unreachable, manual IP and port entry remains available.

## 6. Connection Lifecycle

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

Connection procedure:

1. Obtain an endpoint through mDNS, manual input, or recent endpoints.
2. Validate the endpoint and construct the Host API URL.
3. Authenticate with the Host.
4. Establish the signaling WebSocket.
5. Create an `RTCPeerConnection`.
6. Exchange SDP Offer/Answer and ICE candidates.
7. Wait for the WebRTC connection and remote media track.
8. Attach the received `MediaStream` to a video element.

The signaling protocol must define one initial offerer to avoid simultaneous offer creation.

## 7. WebSocket Signaling

WebSocket is used to exchange WebRTC setup information. It is not the video transport.

Typical messages:

- SDP Offer
- SDP Answer
- ICE Candidate
- Session Close
- Error / Status

Illustrative message type:

```typescript
export type SignalingMessage =
  | { type: "offer"; sdp: string }
  | { type: "answer"; sdp: string }
  | { type: "ice-candidate"; candidate: RTCIceCandidateInit }
  | { type: "session-close" }
  | { type: "error"; code: string; message: string };
```

The production protocol should include session identifiers, validate payloads, and define message versioning. Use secure WebSocket transport (`wss://`) where required by the deployment security model.

## 8. WebRTC Video Reception

The frontend creates and manages the `RTCPeerConnection`. ICE configuration may include STUN and TURN servers. TURN is necessary in network environments where a direct peer-to-peer path cannot be established.

The remote stream is attached to an HTML video element:

```tsx
<video
  ref={videoRef}
  autoPlay
  playsInline
  muted
/>
```

Initial codec target: H.264. Validate profile, level, packetization mode, SDP parameters, keyframe behavior, resolution, frame rate, and decoding on WebView2 and WKWebView.

Video frames must not be transported through WebSocket or Tauri IPC. WebSocket is for signaling; WebRTC is for media.

## 9. Remote Input

Remote input is a planned capability requiring Host-side support. Potential events include mouse movement, mouse buttons, scrolling, key down/up, and modifier keys.

Use one defined authenticated transport, such as a WebRTC DataChannel or a dedicated Host control channel. The Host must validate and authorize every input event. Browser-reserved shortcuts may not be capturable by the Client.

## 10. Tauri IPC and Security

Use Tauri commands/events for native operations such as mDNS discovery and preference access. Do not send high-frequency video frames through IPC.

Apply least-privilege Tauri capabilities. mDNS discovery is not authentication. Manually entered and discovered Hosts must both pass the same authentication and authorization process. Never store secrets in ordinary preferences or DNS-SD TXT records.

Tauri uses WebView2 on Windows and WKWebView on macOS. Test local-network requests, WebSocket, WebRTC, H.264, autoplay, and permissions on both platforms. Native mDNS avoids depending on browser JavaScript mDNS APIs, but does not bypass firewall or multicast restrictions.

## 11. Preferences

The Client may persist the last-used endpoint and non-secret UI preferences. Credentials and tokens require an appropriate secure storage mechanism. Recent endpoints must still go through endpoint validation and authentication.

## 12. Error Handling and Diagnostics

Distinguish discovery failure, no Hosts found, invalid endpoint, Host unreachable, authentication failure, API version mismatch, WebSocket failure, SDP failure, ICE failure, codec incompatibility, playback failure, and Host-initiated session closure.

Logs should include timestamps and safe connection identifiers. Never log passwords or access tokens.

## 13. Performance Requirements

| Metric | Initial Target |
|---|---|
| Resolution | 1920 × 1080 |
| Frame Rate | Up to 60 FPS |
| Codec | H.264 |
| Decoding | Hardware decoding when available |
| Active viewing sessions | One initially |
| Media transport | WebRTC |
| Buffering | Minimize for low latency |

Measure end-to-end latency, dropped frames, decode time, jitter, packet loss, and reconnection time under documented test conditions.

## 14. Build and Distribution

Create the application using the Tauri 2 React + TypeScript template.

```bash
cd client
npm install
npm run tauri dev
```

Build and validate separately on Windows and macOS. Configure Windows code signing and installer packaging, macOS signing and notarization, WebView2 availability, minimum macOS version, firewall guidance, and Host/Client protocol compatibility.

## 15. Development Plan

1. **Foundation:** Tauri application, React UI, endpoint and API types.
2. **Discovery:** mDNS/DNS-SD, Tauri IPC, manual endpoint fallback.
3. **Connection:** Host API, authentication, WebSocket signaling.
4. **Media:** PeerConnection, SDP/ICE, H.264 playback on both platforms.
5. **Reliability:** Remote input, reconnection, diagnostics, NAT/TURN tests.
6. **Release:** Windows/macOS packaging, signing, notarization, documentation.

## 16. Final Architecture Decision

SlimeDisplay Client uses **Tauri 2 + React + TypeScript**. React implements the UI and browser-native WebRTC client. The Tauri Rust backend provides native desktop features, especially mDNS/DNS-SD discovery and local preferences.

mDNS and manual IP/port entry are two endpoint acquisition methods that converge on one connection, authentication, signaling, and WebRTC workflow. HTTP and WebSocket handle API, session control, and signaling; WebRTC carries video media.

The initial release targets Windows and macOS. H.264 and WebRTC behavior must be validated on WebView2 and WKWebView before release.