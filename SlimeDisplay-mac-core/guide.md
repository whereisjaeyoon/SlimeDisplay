# SlimeDisplay for macOS: Implementation Guide

This guide explains how the current macOS implementation creates a virtual display, captures it, encodes it as H.264, and decodes it again through the test-client path for display in a window.

> **Current verification scope:** The test app connects the Host encoder and Client decoder in the same process. A real WebRTC connection, network transport, and remote Client have not been implemented. The app verifies the compressed-video path from encoder to decoder, but not end-to-end real-time streaming over a network.

## 1. Components

| Component | Location | Responsibility |
|---|---|---|
| Public C types and function declarations | `SlimeDisplay/Public/` | Defines handles, configuration, result codes, and the C API contract |
| C/Objective-C++ API implementation | `SlimeDisplay/API/` | Connects public C functions to the Swift manager |
| Session coordinator | `SlimeDisplay/Core/SDManager.swift` | Coordinates operation order and state for display, capture, and encoder |
| Display manager | `SlimeDisplay/Display/VirtualDisplayManager.swift` | Requests virtual display creation and manages the Display ID |
| Virtual display shim | `SlimeDisplay/Adapters/SDVirtualDisplayShim.mm` | Creates display objects through the Objective-C runtime |
| Screen capture manager | `SlimeDisplay/Capture/ScreenCaptureManager.swift` | Captures the selected display with ScreenCaptureKit |
| H.264 encoder | `SlimeDisplay/Encoder/VideoEncoder.swift` | Encodes VideoToolbox input frames and delivers compressed output |
| Test app | `Tests/SlimeDisplayMacRunner.swift` | Controls the virtual display and capture lifecycle and displays decoder output |
| Test Client decoder | `Tests/H264ClientDecoder.swift` | Decodes encoded H.264 samples with VideoToolbox |

```text
Xcode Test App
    |
    +-- SDManager.createVirtualDisplay()
    |       `-- VirtualDisplayManager
    |               `-- SDVirtualDisplayShim.mm
    |                       `-- CGVirtualDisplay-family runtime objects
    |
    `-- SDManager.startEncoder() -> startCapture()
            |
            `-- ScreenCaptureKit SCStream
                    | BGRA CMSampleBuffer
                    v
                VideoToolbox Encoder
                    | H264EncodedFrame
                    | - compressed frame bytes
                    | - PTS
                    | - keyframe flag
                    | - SPS/PPS and NAL length information
                    v
                Test Client VideoToolbox Decoder
                    | BGRA CVPixelBuffer
                    v
                CIImage -> NSImage -> preview window
```

## 2. Creating a Virtual Display

The test app's **Create Virtual Display** button calls `SDManager.createVirtualDisplay(width:height:fps:)`. The initial values requested by the app are 1280×720 at 30 FPS.

1. `SDConfiguration.isValid` checks the resolution and FPS. The current supported range is 320–7680 pixels wide, 240–4320 pixels high, and 1–120 FPS.
2. `VirtualDisplayManager` calls the Objective-C++ function `SDVirtualDisplayCreate`.
3. The shim uses `NSClassFromString` to find `CGVirtualDisplayDescriptor`, `CGVirtualDisplay`, `CGVirtualDisplaySettings`, and `CGVirtualDisplayMode`, then constructs objects using runtime selectors.
4. It applies the resolution and refresh rate with `applySettings:` and returns the created display ID to the Swift layer.

The current shim stores the created object globally, so the process supports only one virtual display at a time. This is experimental and relies on runtime classes and selectors rather than a stable driver implementation using only public APIs. Creation may fail if a class or selector is unavailable or changes behavior in a macOS release.

## 3. ScreenCaptureKit Capture

`SDManager.startCapture()` passes the stored Display ID to `ScreenCaptureManager.start(...)`.

1. Query `SCShareableContent` from ScreenCaptureKit.
2. Find a shareable display whose ID matches the stored ID. If none is found, the operation returns `displayNotFound`.
3. Configure an `SCContentFilter` and `SCStream` for that display.
4. Set the stream output size to the requested resolution and the minimum frame interval to `1 / FPS`.
5. Set the pixel format to `kCVPixelFormatType_32BGRA` and the queue depth to 3.
6. ScreenCaptureKit delivers `CMSampleBuffer` values through the stream callback. The code forwards only valid screen output whose frame status is `.complete`; events that do not contain a complete screen image, such as no change, blank output, or stop events, are not encoded.

The app requires macOS **Screen & System Audio Recording** permission to start capture. Grant permission when prompted. If it was previously denied, check the app's permission in **System Settings → Privacy & Security**.

## 4. Host Encoding Path

The test app calls `SDManager.startEncoder()` first and calls `SDManager.startCapture()` only if that succeeds. This prepares the encoder session before capture frames arrive.

### Input

The encoder input is the `CVPixelBuffer` inside the `CMSampleBuffer` received from ScreenCaptureKit. The current capture configuration is BGRA, and captured frames are passed to `VTCompressionSessionEncodeFrame`.

### VideoToolbox Configuration

`VideoEncoder.start(...)` creates a VideoToolbox compression session with:

- Codec: H.264
- Dimensions: same as the virtual display configuration
- Real-time encoding: enabled
- Frame reordering: disabled
- Profile: H.264 Main Auto Level
- Expected frame rate: configured FPS

### Compressed Output

The VideoToolbox callback supplies a compressed `CMSampleBuffer`. The encoder extracts the following into `H264EncodedFrame`:

- `data`: compressed H.264 access-unit bytes from the `CMBlockBuffer`
- `presentationTime`: output frame PTS
- `isKeyframe`: keyframe flag derived from `kCMSampleAttachmentKey_NotSync`
- `codecConfiguration`: H.264 SPS, PPS, and NAL-unit header length from the format description

These bytes use the H.264 sample format provided by CoreMedia. When connecting a network transport, verify that the receiver expects the same packetization format. Do not assume RTP/WebRTC packetization or Annex-B start codes are produced automatically.

SPS/PPS are required for the decoder to configure the video dimensions, profile, and related properties. The current test implementation supplies codec configuration with every `H264EncodedFrame` so the decoder can use it at startup or when configuration changes. A real protocol must define whether configuration is sent separately at session start, with keyframes, or by another method.

## 5. Client Decoding and Preview

The test app's `manager.onEncodedFrame` passes only the compressed `H264EncodedFrame` to `H264ClientDecoder.decode(...)`. It does not send the captured BGRA source pixel buffer directly to the preview UI.

1. The decoder processes frames on a dedicated serial queue so VideoToolbox calls remain ordered.
2. If SPS/PPS or NAL-length information differs from the previous configuration, it creates an H.264 format description with `CMVideoFormatDescriptionCreateFromH264ParameterSets` and configures a `VTDecompressionSession`.
3. It copies the compressed bytes into a `CMBlockBuffer` and creates a `CMSampleBuffer` containing the PTS and keyframe information.
4. It submits the compressed sample to `VTDecompressionSessionDecodeFrame`.
5. The VideoToolbox decoder callback returns the decoded BGRA `CVPixelBuffer`.
6. The test app converts the decoded result to `CIImage` and `NSImage` and displays it in the **Virtual Display Stream** window.

The preview window therefore does not show the raw captured image. It shows the result after encoding and decoding. The pipeline is progressing when the **Decoded H.264 Frames** count increases in the UI.

## 6. Using the Test App

1. Open `SlimeDisplay-mac.xcodeproj` in Xcode and select the `SlimeDisplayMacRunner` scheme.
2. Run the app.
3. Click **Create Virtual Display**.
4. Click **Start Capture**. This starts the encoder, starts the ScreenCaptureKit stream, and opens the stream window.
5. Grant system screen-capture permission if prompted.
6. Check the **Virtual Display Stream** window and decoded-frame count in the main window.
7. Click **Save Decoded Image** to save the current preview image as a PNG.
8. Click **Stop Capture**, then **Remove Display**.

The virtual display may appear black when it has no content, even when the implementation is working correctly. For testing, configure the virtual display as an extended display in macOS Display settings, move a window onto it, or show visible content on it.

## 7. Lifecycle and State

The normal start sequence is:

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

To stop, stop capture, shut down the encoder, and then remove the virtual display. `SDManager.destroyVirtualDisplay()` also stops the encoder and capture. However, the current `stopCapture()` starts the actual `SCStream.stopCapture()` call asynchronously in a `Task`. If repeated runs or complete teardown ordering are important, this should be changed to an async API that lets the caller wait for stop to finish.

## 8. Troubleshooting

| Symptom | Check |
|---|---|
| Virtual display creation fails | Verify the required runtime classes and selectors exist on macOS and that their behavior is supported |
| Capture cannot find the display | Check whether the created Display ID appears in ScreenCaptureKit's display list |
| Capture does not start | Check system screen-capture permission, display state, and ScreenCaptureKit errors in the Xcode console |
| Preview is black | Check whether the virtual display contains visible content and is active as an extended display |
| Decoded-frame count does not increase | Check H.264 SPS/PPS extraction, format-description creation, and `VTDecompressionSession` errors in the Xcode console |
| Capture works but video is not transmitted | The repository does not yet include WebRTC or network transport |

## 9. Not Yet Implemented

- WebRTC or other network transport between Host and Client
- SDP/ICE signaling and session connection management
- Frame-loss handling, retransmission, congestion control, and bitrate adjustment
- A Client in a separate process and real network-receiver tests
- Implementation of every public C API declaration and C ABI connection for the encoded-frame callback
- Verification of OS-version compatibility and distribution feasibility for runtime access to `CGVirtualDisplay`

## 10. Build and Run

Build the framework from the repository root:

```sh
xcodebuild \
  -project SlimeDisplay-mac/SlimeDisplay-mac.xcodeproj \
  -scheme SlimeDisplay-mac \
  -configuration Debug \
  build \
  CODE_SIGNING_ALLOWED=NO
```

Build the test app:

```sh
xcodebuild \
  -project SlimeDisplay-mac/SlimeDisplay-mac.xcodeproj \
  -scheme SlimeDisplayMacRunner \
  -configuration Debug \
  build \
  CODE_SIGNING_ALLOWED=NO
```

Command-line builds verify compilation only. Test virtual display creation, permission approval, actual screen capture, and H.264 decoding by running the test app on macOS.
