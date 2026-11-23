import Foundation
import CoreMedia
import CoreGraphics

enum SDError: Error {
    case invalidArgument
    case invalidState
    case displayCreationFailed
    case displayNotFound
    case captureFailed
    case encodingFailed
}

final class SDManager {

    private let displayManager = VirtualDisplayManager()
    private let captureManager = ScreenCaptureManager()
    private let encoder = VideoEncoder()

    private var configuration: SDConfiguration?

    var onEncodedFrame: ((Data, CMTime, Bool) -> Void)?

    private(set) var isCapturing = false
    private(set) var isEncoding = false

    func createVirtualDisplay(
        width: Int,
        height: Int,
        fps: Int
    ) throws {

        let config = SDConfiguration(
            width: width,
            height: height,
            fps: fps
        )

        guard config.isValid else {
            throw SDError.invalidArgument
        }

        try displayManager.create(
            width: width,
            height: height,
            fps: fps
        )

        configuration = config
    }

    func destroyVirtualDisplay() {
        stopEncoder()
        stopCapture()

        displayManager.destroy()
        configuration = nil
    }

    func startCapture() async throws {

        guard let config = configuration else {
            throw SDError.invalidState
        }

        guard !isCapturing else {
            throw SDError.invalidState
        }

        let displayID = try displayManager.getDisplayID()

        captureManager.onFrame = { [weak self] sampleBuffer in
            guard let self, self.isEncoding else { return }

            do {
                try self.encoder.encode(sampleBuffer)
            } catch {
                NSLog("Encoding error: %@", error.localizedDescription)
            }
        }

        try await captureManager.start(
            displayID: displayID,
            width: config.width,
            height: config.height,
            fps: config.fps
        )

        isCapturing = true
    }

    func stopCapture() {
        guard isCapturing else { return }

        Task {
            try? await captureManager.stop()
        }

        isCapturing = false
    }

    func startEncoder() throws {

        guard let config = configuration,
              !isEncoding else {
            throw SDError.invalidState
        }

        try encoder.start(
            width: config.width,
            height: config.height,
            fps: config.fps
        )

        encoder.onEncodedFrame = { [weak self] data, pts, keyframe in
            self?.onEncodedFrame?(data, pts, keyframe)
        }

        isEncoding = true
    }

    func stopEncoder() {
        guard isEncoding else { return }

        encoder.stop()
        isEncoding = false
    }

    func getDisplayInfo() throws -> (
        id: UInt64,
        width: UInt32,
        height: UInt32,
        fps: UInt32
    ) {

        guard let config = configuration else {
            throw SDError.invalidState
        }

        let id = try displayManager.getDisplayID()

        return (
            UInt64(id),
            UInt32(config.width),
            UInt32(config.height),
            UInt32(config.fps)
        )
    }
}

import Foundation

@objc(SDManagerBridge)
public final class SDManagerBridge: NSObject {

    private let manager = SDManager()

    @objc public override init() {
        super.init()
    }

    @objc public func createDisplay(
        _ width: UInt32,
        height: UInt32,
        fps: UInt32
    ) -> Int32 {
        do {
            try manager.createVirtualDisplay(
                width: Int(width),
                height: Int(height),
                fps: Int(fps)
            )
            return 0
        } catch {
            return -1
        }
    }

    @objc public func destroyDisplay() {
        manager.destroyVirtualDisplay()
    }

    @objc public func getDisplayID() -> UInt64 {
        guard let info = try? manager.getDisplayInfo() else {
            return 0
        }

        return info.id
    }

    @objc public func startEncoder() -> Int32 {
        do {
            try manager.startEncoder()
            return 0
        } catch {
            return -1
        }
    }

    @objc public func stopEncoder() {
        manager.stopEncoder()
    }

    @objc public func startCapture(
        _ completion: @escaping (Int32) -> Void
    ) {
        Task {
            do {
                try await manager.startCapture()
                completion(0)
            } catch {
                completion(-1)
            }
        }
    }

    @objc public func stopCapture() {
        manager.stopCapture()
    }
}
