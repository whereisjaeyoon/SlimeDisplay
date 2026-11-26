# SlimeDisplay for Windows

## 1. Project Overview

The Windows version of SlimeDisplay uses the existing [Virtual Display Driver (VDD)](https://github.com/VirtualDrivers/Virtual-Display-Driver) to provide virtual monitors, then captures and encodes a selected monitor. SlimeDisplay does not implement or install its own IDD.

It provides the same C ABI as the macOS version so that the upper-level C++ Server can control virtual displays through the same interface on both platforms.

### Development Environment

| Item | Technology |
|---|---|
| IDE | Visual Studio 18 (v145 toolset) |
| Language | C++20 |
| Compiler | MSVC |
| Build system | MSBuild |
| Display driver | VirtualDrivers Virtual Display Driver (installed separately) |
| Display integration | Windows display configuration APIs |
| Capture API | DXGI Desktop Duplication (GDI fallback) |
| Graphics API | Direct3D 11 |
| Encoder | Media Foundation |
| Architecture | x64 |
| Public interface | C ABI |

## 2. Overall Architecture

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
 Existing VirtualDrivers VDD       D3D11 staging texture
             |                           |
             v                           v
      Virtual Monitor            EncoderManager
                                         |
                                         v
                                Media Foundation
```

### Module Responsibilities

**SlimeDisplay.dll**

- Provides the C ABI.
- Selects and configures an existing VDD virtual monitor.
- Reports `SD_ERROR_NOT_SUPPORTED` when no usable VDD monitor is available.
- Manages display state.
- Controls frame capture and the encoder.
- Delivers encoded frames through an external callback.

**VirtualDrivers VDD (external dependency)**

- Installs and exposes virtual monitors to the Windows Display Stack.
- Is configured and updated using its own installer and Virtual Driver Control.
- Does not expose a SlimeDisplay-specific C API; the DLL uses Windows display and capture APIs.

**CaptureManager**

- Captures the selected VDD monitor with DXGI Desktop Duplication.
- Falls back to GDI capture when Desktop Duplication is unavailable.
- Converts the captured monitor to BGRA frames for the encoder.
- Passes frames to the encoder.

**EncoderManager**

- Uses the Media Foundation H.264 hardware encoder when available, with a
  synchronous Media Foundation encoder fallback.
- Converts BGRA frames to NV12 and bounds its queue to keep latency controlled.
- Delivers encoded frames through a DLL callback.

## 3. Active Solution Structure

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

The shared headers under the repository-level `include/` directory are included by the DLL project. The external VDD is not part of this repository and must be installed separately.

## 4. DLL Public API

Use the shared `slimedisplay.h` and `slimedisplay_types.h` headers.

| API | Purpose |
|---|---|
| `sd_create` | Create a SlimeDisplay instance |
| `sd_destroy` | Destroy an instance |
| `sd_create_virtual_display` | Select/configure an existing VDD monitor |
| `sd_destroy_virtual_display` | Release the selected monitor; does not uninstall the VDD |
| `sd_get_virtual_display_info` | Retrieve display information |
| `sd_start_capture` | Start frame capture |
| `sd_stop_capture` | Stop frame capture |
| `sd_start_encoder` | Start the encoder |
| `sd_stop_encoder` | Stop the encoder |
| `sd_set_encoded_frame_callback` | Register an encoded-frame callback |
| `sd_get_version` | Retrieve the DLL version |

### API Implementation Principles

- Keep the external interface as a C ABI.
- Export the same function signatures declared by the macOS public `slimedisplay.h`.
- Separate the internal implementation into C++ classes.
- Use `SD_Handle` as an opaque handle that hides the internal `SDManager` object.
- Do not allow C++ exceptions to cross the C ABI boundary.
- Call callbacks on a separate encoding path so network transmission cannot block it.
- Clearly define the lifetime of frame memory passed to callbacks.

The Windows DLL exports the shared C ABI. It selects an already-installed VDD
monitor, applies a supported mode, and restores the original mode when the
display is destroyed. Capture uses Desktop Duplication where available, with
GDI as a fallback; H.264 encoding uses a Media Foundation hardware transform
when available and otherwise a synchronous Media Foundation transform.

The encoded-frame callback receives H.264 data whose buffer is valid only for
the duration of that callback. The Windows smoke test uses the VDD's current
mode, checks the reported display info, and waits for an encoded frame.
The callback runs on the encoder worker. Keep its code and `user_data` valid
until `sd_stop_encoder` returns on another thread; do not destroy the handle
from the callback.
`sd_get_version` returns `0` until a shared library-version scheme is defined.

## 5. Existing Virtual Display Driver

Use the existing [VirtualDrivers Virtual Display Driver](https://github.com/VirtualDrivers/Virtual-Display-Driver) instead of building a new IDD. Install and configure it separately with its installer or Virtual Driver Control, and make sure at least one virtual monitor is enabled before using SlimeDisplay.

The VDD creates virtual monitor devices and publishes their supported resolutions to Windows. SlimeDisplay must enumerate and select an existing VDD monitor through Windows display APIs. The VDD does not expose a SlimeDisplay C ABI, and its monitor count and available modes are configured through the VDD's own control/configuration tools.

Do not install, build, or manage a second IDD from the SlimeDisplay DLL. Driver installation, updates, signing, and removal remain the responsibility of the VDD package.

### Frame Processing Path

```text
Existing VDD Virtual Monitor
            |
            v
 DXGI Desktop Duplication
            |
            v
 D3D11 staging texture
            |
            v
  BGRA frame (CPU)
            |
            v
  BGRA-to-NV12 conversion
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

Desktop Duplication captures through the Windows display stack; the current
implementation reads frames back to CPU memory for BGRA-to-NV12 conversion.
It does not access the VDD driver's internal SwapChain.

## 6. Build

- Open the existing `SlimeDisplay.sln` in Visual Studio 18 with the v145
  toolset.
- Select `x64` and build the `SlimeDisplay-windows-core` project.
- The solution builds the DLL and `SlimeDisplayTest`.
- Install and enable the external VDD before running `SlimeDisplayTest`. The
  test uses its current display mode and restores that mode on exit.

## 7. Development Phases

### Phase 1 — Existing VDD Integration

- [ ] Install and enable the external VirtualDrivers VDD.
- [x] Enumerate the VDD virtual monitors through Windows display APIs.
- [x] Select an existing virtual monitor and configure its display mode.
- [x] Verify monitor capture through DXGI Desktop Duplication on the installed VDD.

### Phase 2 — Capture

- [x] Connect Desktop Duplication to `CaptureManager`.
- [x] Read back frames for BGRA-to-NV12 conversion.
- [x] Implement frame processing and synchronization.
- [x] Manage capture and GPU resources.
- [x] Implement capture start/stop.

### Phase 3 — Encoder

- [x] Initialize Media Foundation.
- [x] Connect the H.264 encoder with a synchronous fallback.
- [x] Verify hardware-encoding support on the current system.
- [x] Implement the encoded-frame callback.
- [x] Handle frame timestamps.

### Phase 4 — Integration Testing

- [ ] Test the complete DLL API.
- [ ] Repeatedly select and release an existing VDD virtual monitor.
- [ ] Capture at FHD 60 FPS.
- [x] Verify delivery of encoded frames on the current system.
- [ ] Test long-running stability.
- [ ] Integrate with the Server through the C ABI.

## 8. References

- [VirtualDrivers — Virtual Display Driver](https://github.com/VirtualDrivers/Virtual-Display-Driver)
- [Microsoft Learn — Desktop Duplication](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/desktop-dup-api)
- [Microsoft Learn — QueryDisplayConfig](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-querydisplayconfig)
- [Microsoft Learn — Media Foundation](https://learn.microsoft.com/en-us/windows/win32/medfound/microsoft-media-foundation-sdk)