# SlimeDisplay for Windows

## 1. Project Overview

The Windows version of SlimeDisplay is a module that creates a virtual display using the Windows Indirect Display Driver (IDD), captures frames from that display, and encodes them.

It provides the same C ABI as the macOS version so that the upper-level C++ Server can control virtual displays through the same interface on both platforms.

### Development Environment

| Item | Technology |
|---|---|
| IDE | Visual Studio 2022 |
| Language | C++20 |
| Compiler | MSVC |
| Build system | MSBuild / CMake (possible future integration) |
| Display driver | Windows Indirect Display Driver |
| Driver framework | UMDF 2 / IddCx |
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
             |             |             |
             v             v             v
       DisplayManager  CaptureManager  EncoderManager
             |             |             |
             |             |             |
             v             v             v
          IDD Driver   D3D11 Surface  Media Foundation
             |
             v
      Windows Display Stack
             |
             v
       Virtual Monitor
```

### Module Responsibilities

**SlimeDisplay.dll**

- Provides the C ABI.
- Requests virtual display creation and removal.
- Manages display state.
- Controls frame capture and the encoder.
- Delivers encoded frames through an external callback.

**SlimeDisplayDriver**

- Registers a virtual monitor based on the Windows IDD.
- Manages display modes and resolutions.
- Receives SwapChain frames from the OS.
- Provides Direct3D surfaces.

**CaptureManager**

- Receives frames from the IDD SwapChain.
- Processes GPU surfaces.
- Passes frames to the encoder.

**EncoderManager**

- Performs hardware encoding through Media Foundation.
- Encodes with supported codecs such as H.264.
- Delivers encoded frames through a DLL callback.

## 3. Project Directory Layout

```text
SlimeDisplay/
├── include/
│   ├── slimedisplay.h
│   └── slimedisplay_types.h
│
├── windows/
│   ├── SlimeDisplay.sln
│   │
│   ├── SlimeDisplay/
│   │   ├── SlimeDisplay.vcxproj
│   │   ├── SDAPI.cpp
│   │   ├── SDManager.h
│   │   ├── SDManager.cpp
│   │   │
│   │   ├── display/
│   │   │   ├── DisplayManager.h
│   │   │   └── DisplayManager.cpp
│   │   │
│   │   ├── capture/
│   │   │   ├── CaptureManager.h
│   │   │   └── CaptureManager.cpp
│   │   │
│   │   └── encoder/
│   │       ├── EncoderManager.h
│   │       └── EncoderManager.cpp
│   │
│   ├── SlimeDisplayDriver/
│   │   ├── Driver.cpp
│   │   ├── Driver.h
│   │   ├── Direct3DDevice.cpp
│   │   ├── IndirectDeviceContext.cpp
│   │   ├── IndirectMonitorContext.cpp
│   │   ├── SwapChainProcessor.cpp
│   │   ├── Device.tmh
│   │   └── SlimeDisplayDriver.vcxproj
│   │
│   └── SlimeDisplayTest/
│       ├── main.cpp
│       └── SlimeDisplayTest.vcxproj
│
└── README.md
```

`SlimeDisplayDriver` is based on the Microsoft IDD Sample. Build and distribute the DLL and driver as separate projects.

## 4. DLL Public API

Use the shared `slimedisplay.h` and `slimedisplay_types.h` headers.

| API | Purpose |
|---|---|
| `sd_create` | Create a SlimeDisplay instance |
| `sd_destroy` | Destroy an instance |
| `sd_create_virtual_display` | Create a virtual display |
| `sd_destroy_virtual_display` | Remove a virtual display |
| `sd_get_virtual_display_info` | Retrieve display information |
| `sd_start_capture` | Start frame capture |
| `sd_stop_capture` | Stop frame capture |
| `sd_start_encoder` | Start the encoder |
| `sd_stop_encoder` | Stop the encoder |
| `sd_set_encoded_frame_callback` | Register an encoded-frame callback |
| `sd_get_version` | Retrieve the DLL version |

### API Implementation Principles

- Keep the external interface as a C ABI.
- Separate the internal implementation into C++ classes.
- Use `SD_Handle` as an opaque handle that hides the internal `SDManager` object.
- Do not allow C++ exceptions to cross the C ABI boundary.
- Call callbacks on a separate encoding path so network transmission cannot block it.
- Clearly define the lifetime of frame memory passed to callbacks.

## 5. IDD Driver

A virtual display cannot be created with an ordinary DLL alone. An Indirect Display Driver is required to register the display device with the OS.

IDD is a user-mode driver based on UMDF and communicates with the Windows Display Stack through IddCx.

### Main Components

| Component | Purpose |
|---|---|
| `Direct3DDevice` | Manages the Direct3D device |
| `IndirectDeviceContext` | Manages the indirect display device |
| `IndirectMonitorContext` | Manages virtual monitors and display modes |
| `SwapChainProcessor` | Processes frames provided by the OS |

### Frame Processing Path

```text
Windows Desktop Composition
            |
            v
     IDD SwapChain
            |
            v
   SwapChainProcessor
            |
            v
    Direct3D Surface
            |
            v
      CaptureManager
            |
            v
   Media Foundation Encoder
            |
            v
     Encoded Frame
            |
            v
   SD_EncodedFrameCallback
```

Prefer a GPU-based path that passes GPU surfaces to the encoder without unnecessary copies to CPU memory.

## 6. Visual Studio 2022 Project Setup

### SlimeDisplay DLL

Create a project in Visual Studio with these options:

- Create a new project
- Dynamic-Link Library (DLL)
- C++
- Project name: `SlimeDisplay`
- Platform: x64
- Language standard: C++20

Key properties:

| Setting | Value |
|---|---|
| Configuration Type | Dynamic Library (.dll) |
| C++ Language Standard | ISO C++20 |
| Character Set | Use Unicode |
| Runtime Library (Debug) | Multi-threaded Debug DLL (/MDd) |
| Runtime Library (Release) | Multi-threaded DLL (/MD) |
| Precompiled Header | Not Using |
| Target Architecture | x64 |

Preprocessor definitions:

```text
SLIMEDISPLAY_EXPORTS
WIN32_LEAN_AND_MEAN
NOMINMAX
```

Additional include directories:

```text
$(SolutionDir)..\include
```

### IDD Driver

Base the IDD Driver project on the Indirect Display Driver Sample in Microsoft's Windows Driver Samples repository.

Required development components:

- Visual Studio 2022
- Windows SDK
- Windows Driver Kit (WDK)
- UMDF and IddCx development support

Configure the driver project separately from the ordinary DLL project.

## 7. Development Phases

### Phase 1 — Basic DLL Structure

- [ ] Create a Visual Studio solution.
- [ ] Create the SlimeDisplay DLL project.
- [ ] Connect the shared headers.
- [ ] Implement `SDManager`.
- [ ] Implement `sd_create`.
- [ ] Implement `sd_destroy`.
- [ ] Implement `sd_get_version`.
- [ ] Test DLL loading.

### Phase 2 — IDD Driver

- [ ] Build the Microsoft IDD Sample.
- [ ] Install the driver in a test environment.
- [ ] Create a Software Device and connect the driver.
- [ ] Verify virtual monitor creation.
- [ ] Configure resolution and display modes.
- [ ] Verify receipt of SwapChain frames.

### Phase 3 — Capture

- [ ] Connect `SwapChainProcessor` to `CaptureManager`.
- [ ] Pass Direct3D surfaces.
- [ ] Implement frame processing and synchronization.
- [ ] Manage GPU resources.
- [ ] Implement capture start/stop.

### Phase 4 — Encoder

- [ ] Initialize Media Foundation.
- [ ] Connect the H.264 encoder.
- [ ] Verify hardware-encoding support.
- [ ] Implement the encoded-frame callback.
- [ ] Handle frame timestamps.

### Phase 5 — Integration Testing

- [ ] Test the complete DLL API.
- [ ] Repeatedly create and remove virtual displays.
- [ ] Capture at FHD 60 FPS.
- [ ] Verify delivery of encoded frames.
- [ ] Test long-running stability.
- [ ] Integrate with the Server through the C ABI.

## 8. Testing and Distribution Considerations

- Installing an IDD driver requires a driver package and INF configuration.
- Check test-signing and driver-installation policies in the development environment.
- Prepare driver-signing and installation procedures separately for general distribution.
- Manage version compatibility between the driver and DLL.
- Ensure that removing the driver releases the virtual monitor and related resources correctly.

## 9. References

- [Microsoft Windows Driver Samples — Indirect Display Driver](https://github.com/microsoft/Windows-driver-samples/tree/main/video/IndirectDisplay)
- [Microsoft Learn — Indirect Display Driver Model](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/indirect-display-driver-model-overview)
- [Microsoft Learn — IddCx](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/iddcx/)
- [Microsoft Learn — Media Foundation](https://learn.microsoft.com/en-us/windows/win32/medfound/microsoft-media-foundation-sdk)