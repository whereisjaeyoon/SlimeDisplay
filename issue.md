# SlimeDisplay Design Review Issues

Summary of the design document (README) review. The overall design is feasible, but the following items need to be addressed.

- **Priority:** 🔴 Required (the design will not work as-is or needs redesign) / 🟡 Recommended improvement / 🟢 Informational

---

## Confirmed Findings

| Item | Finding | Notes |
| :--- | :--- | :--- |
| VDD license | MIT | Include the license notice when redistributing |
| VDD signing | Signed with a SignPath Foundation certificate | Changes to the source or a self-built driver will not retain this signature |
| VDD installation | Release installer; winget supported | `winget install --id=VirtualDrivers.Virtual-Display-Driver -e` |
| VDD settings | `C:\VirtualDisplayDriver\vdd_settings.xml` | Add or remove resolutions through this file |
| macOS `CGVirtualDisplay` | Private API | Not suitable for Mac App Store distribution; use direct distribution and notarization, as DeskPad and BetterDisplay do |

---

## 🔴 Required

### #1 H.264 Level Negotiation

- **Problem:** Safari commonly negotiates `profile-level-id=42e01f` (Level 3.1, approximately 720p), while the target of 1080p60 requires Level 4.2.
- **Impact:** Video may fail to play or may be limited to a lower resolution.
- **Actions:**
  - Add `level-asymmetry-allowed=1` to the Host SDP.
  - Set the encoder level to 4.2 or higher (5.1–5.2 for native iPad resolutions).
  - Disable B-frames and include SPS/PPS with every keyframe.
- **Document sections:** 5.4 Codec

### #2 The Web Client Example Does Not Work in iOS Safari

- **Problems:**
  - Without `autoplay playsinline muted` on `<video>`, iOS may reject `play()`.
  - If the Host does not send an `msid`, `event.streams[0]` is `undefined`.
  - As in section 5.3, when the browser is the offerer, no video m-line is created unless a receiving transceiver is added.
- **Action:**

```html
<video autoplay playsinline muted></video>
```

```typescript
const pc = new RTCPeerConnection();
pc.addTransceiver("video", { direction: "recvonly" }); // Required when the browser is the offerer.

pc.ontrack = (e) => {
  const video = document.querySelector("video")!;
  video.srcObject = e.streams[0] ?? new MediaStream([e.track]);
  video.play().catch(() => {
    // Show a play button if autoplay fails.
  });
};
```

- **Document sections:** 5.3 Connection Flow, 7.3 Video Reception Example

### #3 HTTPS/WSS Certificate Validation Happens Too Late

- **Problem:** To fully trust a self-signed certificate on iOS, the user must install a profile and then go to **Settings → General → About → Certificate Trust Settings**. This creates significant UX friction, and failure may require redesigning the system.
- **Actions:**
  - Serve the webpage and WSS from the same origin (same host and port).
  - Verify on a real iPad running Safari that the page, WSS, and WebRTC all work after accepting the warning once.
  - Move this verification to **Phase 2**.
- **Document sections:** 6.3 Certificates, 10 Development Phases

### #4 No Client-Matched Resolution

- **Problem:** iPad displays have a 4:3 aspect ratio, but the target resolution is fixed at 1920×1080. As an extended display this creates letterboxing and a blurry image.
- **Conflict:** Section 2.2 says not to edit the VDD settings file directly, but VDD resolutions are managed in `vdd_settings.xml`.
- **Actions:**
  - Pre-register common Client resolutions (for example 2048×1536, 2360×1640, and 2732×2048) in `vdd_settings.xml` during installation.
  - On connection, have the Client send a resolution based on `screen.width × devicePixelRatio`.
  - Have the Host change only the virtual monitor mode with `ChangeDisplaySettingsEx`.
  - Move resolution matching from **Phase 6 to Phase 4**.
- **Document sections:** 2.2, 8 Performance Design, 10 Development Phases

### #5 Host Core Technology Stack Is Undecided

- **Problem:** Decisions that must be made before Phase 2 are not documented.
- **Decisions required:**

| Item | Candidates | Recommendation |
| :--- | :--- | :--- |
| Host language | C++ / Rust | Consider a shared Windows/macOS core |
| WebRTC library | libdatachannel (C++), webrtc-rs (Rust), Pion (Go), libwebrtc | libdatachannel; libwebrtc is excessively heavy |
| Windows encoder | Direct NVENC / AMF / QSV, Media Foundation | Start with Media Foundation, then consider vendor SDKs |
| Bitrate control | Custom implementation required | libdatachannel has no congestion control; implement using RTCP RR/REMB |

- **Document sections:** 3 System Architecture, 5 Signaling and WebRTC

---

## 🟡 Recommended Improvements

### #6 Inconsistent Project Name

- The title says **SlimeDisplay**, while the body, service type (`_beam._tcp.local.`), and repository (`beam/`) use **Beam**.
- Standardize on one name.

### #7 Schedule Touch/Pen Input Support

- Disabling remote input by default is sensible for security, but an extended display is much less useful if it cannot be controlled from that screen.
- Specify it as the first phase after the MVP and reserve a DataChannel input-message schema in `protocol/`:
  - Pointer type (`touch` / `pen` / `mouse`), normalized coordinates, pressure, and tilt.
  - Windows injection: `SendInput` for mouse input and `InjectSyntheticPointerInput` for pen pressure.
  - DataChannel: use `unordered` with `maxRetransmits: 0` for movement events, and a reliable channel for clicks and keys.

### #8 Windows Graphics Capture Details

- Remove the yellow capture border with `IsBorderRequired = false` (Windows 11).
- Configure cursor inclusion with `IsCursorCaptureEnabled`.
- Separate the Capture Backend behind an interface so it can be replaced with Desktop Duplication API if latency is a problem.

### #9 Connection Options Besides QR

- iPad and Android users can scan a QR code with the camera, but this is inconvenient on Windows/macOS Clients.
- Display the **connection URL and a short pairing code** alongside the QR code.

### #10 Concurrent Connection Policy

- Define whether multiple Clients may view one virtual display or whether only one is allowed.
- The decision affects the number of encoder instances and the resolution-matching policy in #4.

### #11 Add macOS Verification

- ScreenCaptureKit has been reported to confuse a display created with `CGVirtualDisplay` with another display.
- Add this to Phase 5 verification.

---

## 🟢 Informational

### #12 Windows mDNS Advertisement

- Windows 10 1809 and later can advertise services with `DnsServiceRegister`, without a separate library.

### #13 Measure Latency in the Browser

- Use `video.requestVideoFrameCallback()` metadata to measure decode and render timing (supported by Safari).
- Document this as the method for measuring Decoding/Rendering Latency in section 8.1.

### #14 WebRTC Latency

- Keep Host transmission intervals consistent, even when the screen has not changed; irregular intervals can increase the browser jitter buffer.
- Base RTP timestamps on actual capture times.
- Consider encoders such as NVENC that support intra-refresh instead of large periodic keyframes.
- Treat Chrome's `jitterBufferTarget` as optional because Safari support is uncertain.

### #15 ICE Candidates

- Safari hides its own Host candidate as `xxxx.local` (mDNS). When the Host receives Safari's STUN check, it learns the address as a peer-reflexive candidate, so this is not a problem for direct LAN connections.

### #16 VDD Operational Notes

- VDD recommends removal before major GPU/chipset driver updates. Include this in the installation/removal UX documentation (Phase 6).
- Check the Microsoft Visual C++ Redistributable dependency.

---

## Proposed Development-Phase Changes

| Phase | Change |
| :--- | :--- |
| Phase 1 | No change |
| Phase 2 | Add **real iPad Safari testing**, **HTTPS/WSS verification (#3)**, and **H.264 level verification (#1)**. Confirm the technology stack (#5) before starting. |
| Phase 3 | Add displaying the URL and pairing code (#9). |
| Phase 4 | Move **Client resolution matching (#4)** here. |
| Phase 5 | Add verification for ScreenCaptureKit display confusion (#11). |
| Phase 6 | Remove resolution-change work (moved to #4). Include VDD operational notes (#16). |
| Phase 7 (new) | Add DataChannel support for touch/pen input (#7). |

---

## References

- Virtual-Display-Driver: https://github.com/VirtualDrivers/Virtual-Display-Driver
- go-macos/virtualdisplay (notes on the `CGVirtualDisplay` private API): https://pkg.go.dev/github.com/go-macos/virtualdisplay
- Apple Developer Forums, notarization and private APIs: https://developer.apple.com/forums/thread/702740
- Apple Developer Forums, ScreenCaptureKit virtual-display confusion: https://developer.apple.com/forums/thread/786829
