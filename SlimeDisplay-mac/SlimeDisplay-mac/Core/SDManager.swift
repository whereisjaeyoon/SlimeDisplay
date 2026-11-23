import Foundation
import CoreMedia
import CoreGraphics
import CoreVideo

enum SDError: LocalizedError {
    case invalidArgument
    case invalidState
    case displayCreationFailed
    case displayNotFound
    case captureFailed
    case captureStopped
    case captureFrameHasNoImageBuffer
    case encodingFailed

    var errorDescription: String? {
        switch self {
        case .invalidArgument:
            return "입력한 설정값이 올바르지 않습니다."
        case .invalidState:
            return "현재 상태에서는 이 작업을 수행할 수 없습니다."
        case .displayCreationFailed:
            return "가상 디스플레이를 생성하지 못했습니다."
        case .displayNotFound:
            return "캡처할 디스플레이를 찾지 못했습니다."
        case .captureFailed:
            return "화면 캡처에 실패했습니다."
        case .captureStopped:
            return "이미지를 가져오기 전에 화면 캡처가 중지되었습니다. 캡처를 다시 시작한 뒤 저장하세요."
        case .captureFrameHasNoImageBuffer:
            return "ScreenCaptureKit이 완전한 프레임을 알렸지만 이미지 버퍼가 없습니다. 다시 캡처해 주세요."
        case .encodingFailed:
            return "비디오 프레임 인코딩에 실패했습니다."
        }
    }
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

    func captureNextFrame() async throws -> CVPixelBuffer {
        guard isCapturing else {
            throw SDError.invalidState
        }

        return try await captureManager.captureNextFrame()
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
