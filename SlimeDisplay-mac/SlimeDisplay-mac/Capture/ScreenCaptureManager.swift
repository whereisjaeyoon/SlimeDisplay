import Foundation
import ScreenCaptureKit
import CoreMedia
import CoreVideo

final class ScreenCaptureManager:
    NSObject,
    SCStreamOutput,
    SCStreamDelegate
{
    private var stream: SCStream?
    private let snapshotLock = NSLock()
    private var snapshotContinuation: CheckedContinuation<CVPixelBuffer, Error>?

    var onFrame: ((CMSampleBuffer) -> Void)?

    func start(
        displayID: CGDirectDisplayID,
        width: Int,
        height: Int,
        fps: Int
    ) async throws {

        let content = try await SCShareableContent
            .excludingDesktopWindows(
                false,
                onScreenWindowsOnly: true
            )

        guard let display = content.displays.first(
            where: { $0.displayID == displayID }
        ) else {
            throw SDError.displayNotFound
        }

        let filter = SCContentFilter(
            display: display,
            excludingWindows: []
        )

        let configuration = SCStreamConfiguration()

        configuration.width = width
        configuration.height = height

        configuration.minimumFrameInterval =
            CMTime(value: 1, timescale: Int32(fps))

        configuration.queueDepth = 3
        configuration.pixelFormat = kCVPixelFormatType_32BGRA

        let stream = SCStream(
            filter: filter,
            configuration: configuration,
            delegate: self
        )

        try stream.addStreamOutput(
            self,
            type: .screen,
            sampleHandlerQueue: DispatchQueue(label: "slimedisplay.capture")
        )

        self.stream = stream

        try await stream.startCapture()
    }

    func stop() async throws {
        guard let stream else { return }

        let continuation = snapshotLock.withLock {
            defer { snapshotContinuation = nil }
            return snapshotContinuation
        }
        continuation?.resume(throwing: SDError.captureStopped)

        try await stream.stopCapture()
        self.stream = nil
    }

    func captureNextFrame() async throws -> CVPixelBuffer {
        try await withCheckedThrowingContinuation { continuation in
            let canCapture = snapshotLock.withLock {
                guard stream != nil, snapshotContinuation == nil else {
                    return false
                }

                snapshotContinuation = continuation
                return true
            }

            guard canCapture else {
                continuation.resume(throwing: SDError.invalidState)
                return
            }
        }
    }

    func stream(
        _ stream: SCStream,
        didOutputSampleBuffer sampleBuffer: CMSampleBuffer,
        of outputType: SCStreamOutputType
    ) {
        guard outputType == .screen,
              sampleBuffer.isValid else {
            return
        }

        guard let attachments = CMSampleBufferGetSampleAttachmentsArray(
            sampleBuffer,
            createIfNecessary: false
        ) as? [[SCStreamFrameInfo: Any]],
              let statusValue = attachments.first?[.status] as? Int,
              let frameStatus = SCFrameStatus(rawValue: statusValue),
              frameStatus == .complete else {
            return
        }

        guard let pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer) else {
            NSLog("[SlimeDisplay] Complete screen-capture frame has no image buffer")
            let continuation = snapshotLock.withLock {
                defer { snapshotContinuation = nil }
                return snapshotContinuation
            }
            continuation?.resume(throwing: SDError.captureFrameHasNoImageBuffer)
            return
        }

        snapshotLock.lock()
        let continuation = snapshotContinuation
        snapshotContinuation = nil
        snapshotLock.unlock()
        continuation?.resume(returning: pixelBuffer)

        onFrame?(sampleBuffer)
    }

    func stream(
        _ stream: SCStream,
        didStopWithError error: Error
    ) {
        NSLog("ScreenCaptureKit stopped: %@", error.localizedDescription)
        let continuation = snapshotLock.withLock {
            defer { snapshotContinuation = nil }
            return snapshotContinuation
        }
        continuation?.resume(throwing: error)
    }
}
