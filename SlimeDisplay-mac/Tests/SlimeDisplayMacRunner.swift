import AppKit
import CoreImage
import ImageIO
import SwiftUI
import UniformTypeIdentifiers

@main
struct SlimeDisplayMacRunnerApp: App {
    var body: some Scene {
        WindowGroup {
            RunnerView()
                .frame(minWidth: 420, minHeight: 260)
        }
    }
}

@MainActor
final class RunnerModel: ObservableObject {
    @Published private(set) var status = "가상 디스플레이를 생성하세요."
    @Published private(set) var encodedFrameCount = 0
    @Published private(set) var hasDisplay = false
    @Published private(set) var isStreaming = false
    @Published private(set) var isSavingCapture = false

    private let manager = SDManager()
    private let imageContext = CIContext()

    init() {
        manager.onEncodedFrame = { [weak self] _, _, _ in
            Task { @MainActor [weak self] in
                self?.encodedFrameCount += 1
            }
        }
    }

    func createDisplay() {
        do {
            try manager.createVirtualDisplay(width: 1280, height: 720, fps: 30)
            hasDisplay = true
            status = "1280×720 가상 디스플레이를 생성했습니다."
        } catch {
            status = "디스플레이 생성 실패: \(error.localizedDescription)"
        }
    }

    func startCapture() {
        guard hasDisplay else { return }

        Task {
            do {
                try manager.startEncoder()
                try await manager.startCapture()
                isStreaming = true
                status = "화면 캡처 및 H.264 인코딩 중입니다."
            } catch {
                manager.stopCapture()
                manager.stopEncoder()
                status = "캡처 시작 실패: \(error.localizedDescription)"
            }
        }
    }

    func saveCapture() {
        guard isStreaming, !isSavingCapture else { return }

        isSavingCapture = true
        status = "다음 화면 프레임을 캡처하는 중입니다."

        Task {
            defer { isSavingCapture = false }

            do {
                let pixelBuffer = try await manager.captureNextFrame()
                let image = CIImage(cvPixelBuffer: pixelBuffer)
                guard let cgImage = imageContext.createCGImage(image, from: image.extent) else {
                    status = "캡처 프레임을 이미지로 변환하지 못했습니다."
                    return
                }

                let pngData = NSMutableData()
                guard let destination = CGImageDestinationCreateWithData(
                    pngData,
                    UTType.png.identifier as CFString,
                    1,
                    nil
                ) else {
                    status = "PNG 이미지 인코더를 만들지 못했습니다."
                    return
                }

                CGImageDestinationAddImage(destination, cgImage, nil)
                guard CGImageDestinationFinalize(destination) else {
                    status = "PNG 이미지 인코딩에 실패했습니다."
                    return
                }

                let panel = NSSavePanel()
                panel.allowedContentTypes = [.png]
                panel.nameFieldStringValue = "SlimeDisplay-Capture.png"
                panel.canCreateDirectories = true

                guard panel.runModal() == .OK, let url = panel.url else {
                    status = "이미지 저장을 취소했습니다."
                    return
                }

                try Data(referencing: pngData).write(to: url, options: .atomic)
                status = "PNG 저장 완료: \(url.lastPathComponent)"
            } catch {
                status = "이미지 캡처 또는 저장 실패: \(error.localizedDescription)"
            }
        }
    }

    func stopCapture() {
        manager.stopCapture()
        manager.stopEncoder()
        isStreaming = false
        status = "캡처를 중지했습니다."
    }

    func destroyDisplay() {
        manager.destroyVirtualDisplay()
        hasDisplay = false
        isStreaming = false
        status = "가상 디스플레이를 제거했습니다."
    }
}

struct RunnerView: View {
    @StateObject private var model = RunnerModel()

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("SlimeDisplay macOS 테스트")
                .font(.title2.bold())

            Text(model.status)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)

            Text("인코딩된 프레임: \(model.encodedFrameCount)")
                .monospacedDigit()

            HStack {
                Button("가상 디스플레이 생성", action: model.createDisplay)
                    .disabled(model.hasDisplay)

                Button("캡처 시작", action: model.startCapture)
                    .disabled(!model.hasDisplay || model.isStreaming)

                Button("캡처 중지", action: model.stopCapture)
                    .disabled(!model.isStreaming || model.isSavingCapture)

                Button("캡처 이미지 저장", action: model.saveCapture)
                    .disabled(!model.isStreaming || model.isSavingCapture)
            }

            Button("디스플레이 제거", action: model.destroyDisplay)
                .disabled(!model.hasDisplay || model.isSavingCapture)
        }
        .padding(24)
    }
}
