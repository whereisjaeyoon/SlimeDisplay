# SlimeDisplay for macOS

This document describes the macOS framework implementation. Its source is under `SlimeDisplay-mac/`; the current backend connects virtual-display creation, screen capture, and H.264 encoding.

See [guide.md](./guide.md) for detailed component responsibilities, frame processing, running the test app, and current limitations.

## Source Layout

```text
SlimeDisplay-mac/
├── SlimeDisplay-mac/
│   ├── Public/       # C ABI and shared types
│   ├── API/          # Objective-C++ bridge from the C API to Swift managers
│   ├── Core/         # Session configuration and coordination
│   ├── Display/      # Virtual display creation and ID management
│   ├── Capture/      # ScreenCaptureKit screen capture
│   ├── Encoder/      # VideoToolbox H.264 encoding
│   └── Adapters/     # Objective-C++ virtual display shim
├── Tests/            # Test app runnable from Xcode
└── SlimeDisplay-mac.xcodeproj/
```

## Data Flow

```text
SD C API
   |
   v
SDManagerBridge / SDManager
   |
   +--> VirtualDisplayManager
   |       `--> SDVirtualDisplayShim.mm
   |             `--> CGVirtualDisplay-family runtime classes
   |
   +--> ScreenCaptureManager
   |       `--> ScreenCaptureKit (BGRA CMSampleBuffer)
   |
   `--> VideoEncoder
           `--> VideoToolbox (H.264)
                  `--> H.264 bytes + codec configuration + PTS + keyframe flag
                         |
                         `--> Test client VideoToolbox decoder
                                `--> Displays decoded frames in a preview window
```

## Implemented Behavior

- `SDConfiguration` limits resolution to 320×240 through 7680×4320 and validates FPS from 1 to 120.
- Virtual display creation validates dimensions and refresh rate, then uses the Objective-C runtime to find and instantiate `CGVirtualDisplayDescriptor`, `CGVirtualDisplay`, `CGVirtualDisplaySettings`, and `CGVirtualDisplayMode`. The shim retains the created display object and currently allows only one virtual display at a time in the process.
- ScreenCaptureKit finds the specified display ID and captures it. Capture output uses BGRA, a frame interval based on the requested FPS, and a queue depth of three frames.
- VideoToolbox creates a real-time H.264 encoding session and disables frame reordering. Its callback supplies encoded data, the presentation timestamp (PTS), and the keyframe flag.
- `SDManager` coordinates the lifecycle of the display, capture, and encoder. `SDAPI.mm` connects the public C functions to the Objective-C/Swift bridge.
- The test app does not send captured raw pixels directly to the UI. It passes H.264 data, timestamps, keyframe flags, and SPS/PPS needed to initialize the decoder to the test client decoder.
- The test client decodes H.264 frames with `VTDecompressionSession` and displays them in the **Virtual Display Stream** window. **Save Decoded Image** also saves the image output by this decoder as a PNG.
- This locally verifies the Host-encode → Client-decode path. WebRTC, network transmission, and inter-process transport are not included yet.

## Current Scope and Notes

- Virtual display creation is experimental and accesses `CGVirtualDisplay`-family types through `NSClassFromString` and the Objective-C runtime. It may be fragile across macOS versions and may have distribution-policy limitations, so compatibility and distribution feasibility need separate verification.
- Screen capture requires macOS Screen Recording permission. Capture cannot start if permission is denied or the display cannot be found.
- The current source does not include WebSocket signaling, WebRTC transport, or a browser connection. End-to-end delivery of encoder output as a real network stream is therefore not yet implemented.
- The public C headers declare encoded-frame callback registration and version lookup, but `SDAPI.mm` does not currently implement those functions. The complete C API implementation and error-code mapping require further verification.

## Build

Run from the repository root on a machine with macOS and Xcode installed:

```sh
xcodebuild \
  -project SlimeDisplay-mac/SlimeDisplay-mac.xcodeproj \
  -scheme SlimeDisplay-mac \
  -configuration Debug \
  build \
  CODE_SIGNING_ALLOWED=NO
```

This verifies framework compilation. Actual behavior, including virtual display creation and screen-capture permissions, must be tested separately on macOS.

## Run from Xcode

Open `SlimeDisplay-mac/SlimeDisplay-mac.xcodeproj` in Xcode, select the `SlimeDisplayMacRunner` scheme, and click **Run**. When the test app creates a virtual display and starts capture, the H.264 test decoder opens a stream window. macOS may request Screen Recording permission the first time capture starts.

The executable can also be built from the command line:

```sh
xcodebuild \
  -project SlimeDisplay-mac/SlimeDisplay-mac.xcodeproj \
  -scheme SlimeDisplayMacRunner \
  -configuration Debug \
  build \
  CODE_SIGNING_ALLOWED=NO
```
