import AppKit
import CoreImage
import SwiftUI
import UniformTypeIdentifiers

@main
struct SlimeDisplayMacRunnerApp: App {
    @StateObject private var model = RunnerModel()

    var body: some Scene {
        WindowGroup {
            RunnerView(model: model)
                .frame(minWidth: 420, minHeight: 260)
        }

        Window("가상 디스플레이 스트림", id: "stream-preview") {
            StreamPreviewView(model: model)
        }
        .defaultSize(width: 1280, height: 720)
    }
}

@MainActor
final class RunnerModel: ObservableObject {
    @Published private(set) var status = "가상 디스플레이를 생성하세요."
    @Published private(set) var decodedFrameCount = 0
    @Published private(set) var hasDisplay = false
    @Published private(set) var isStreaming = false
    @Published private(set) var isSavingCapture = false
    @Published private(set) var previewImage: NSImage?

    private let manager = SDManager()
    private let decoder = H264ClientDecoder()
    private let imageContext = CIContext()

    init() {
        manager.onEncodedFrame = { [weak decoder] frame in
            decoder?.decode(frame)
        }

        decoder.onDecodedFrame = { [weak self] pixelBuffer in
            guard let self else { return }

            let image = CIImage(cvPixelBuffer: pixelBuffer)
            guard let cgImage = self.imageContext.createCGImage(image, from: image.extent) else {
                return
            }

            let preview = NSImage(
                cgImage: cgImage,
                size: NSSize(width: cgImage.width, height: cgImage.height)
            )
            Task { @MainActor [weak self] in
                self?.previewImage = preview
                self?.decodedFrameCount += 1
            }
        }
    }

    func createDisplay() {
        do {
            try manager.createVirtualDisplay(width: 1280, height: 720, fps: 60)
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
        guard isStreaming,
              !isSavingCapture,
              let previewImage else {
            return
        }

        isSavingCapture = true
        status = "디코딩된 스트림 프레임을 PNG로 저장합니다."

        Task {
            defer { isSavingCapture = false }

            do {
                guard let tiffData = previewImage.tiffRepresentation,
                      let bitmap = NSBitmapImageRep(data: tiffData),
                      let pngData = bitmap.representation(
                        using: .png,
                        properties: [:]
                      ) else {
                    status = "디코딩된 프레임을 PNG로 변환하지 못했습니다."
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

                try pngData.write(to: url, options: .atomic)
                status = "PNG 저장 완료: \(url.lastPathComponent)"
            } catch {
                status = "디코딩 프레임 저장 실패: \(error.localizedDescription)"
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
        decoder.invalidate()
        hasDisplay = false
        isStreaming = false
        previewImage = nil
        decodedFrameCount = 0
        status = "가상 디스플레이를 제거했습니다."
    }
}

struct RunnerView: View {
    @ObservedObject var model: RunnerModel
    @Environment(\.openWindow) private var openWindow

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("SlimeDisplay macOS 테스트")
                .font(.title2.bold())

            Text(model.status)
                .foregroundStyle(.secondary)
                .fixedSize(horizontal: false, vertical: true)

            Text("디코딩된 H.264 프레임: \(model.decodedFrameCount)")
                .monospacedDigit()

            Button("가상 디스플레이 스트림 창 열기") {
                openWindow(id: "stream-preview")
            }
            .disabled(!model.hasDisplay)

            HStack {
                Button("가상 디스플레이 생성", action: model.createDisplay)
                    .disabled(model.hasDisplay)

                Button("캡처 시작") {
                    openWindow(id: "stream-preview")
                    model.startCapture()
                }
                .disabled(!model.hasDisplay || model.isStreaming)

                Button("캡처 중지", action: model.stopCapture)
                    .disabled(!model.isStreaming || model.isSavingCapture)

                Button("디코딩 이미지 저장", action: model.saveCapture)
                    .disabled(!model.isStreaming || model.isSavingCapture || model.previewImage == nil)
            }

            Button("디스플레이 제거", action: model.destroyDisplay)
                .disabled(!model.hasDisplay || model.isSavingCapture)
        }
        .padding(24)
    }
}

struct StreamPreviewView: View {
    @ObservedObject var model: RunnerModel

    var body: some View {
        ZStack {
            Color.black

            if let previewImage = model.previewImage {
                Image(nsImage: previewImage)
                    .resizable()
                    .aspectRatio(contentMode: .fit)
            } else {
                VStack(spacing: 12) {
                    Image(systemName: "display")
                        .font(.system(size: 48))
                    Text(model.isStreaming
                         ? "가상 디스플레이 프레임을 기다리는 중..."
                         : "가상 디스플레이를 생성하고 캡처를 시작하세요.")
                        .multilineTextAlignment(.center)
                }
                .foregroundStyle(.white.opacity(0.8))
                .padding()
            }

            if model.isStreaming {
                VStack {
                    HStack {
                        Circle()
                            .fill(.red)
                            .frame(width: 8, height: 8)
                        Text("LIVE")
                            .font(.caption.bold())
                        Spacer()
                    }
                    Spacer()
                }
                .foregroundStyle(.white)
                .padding(12)
            }
        }
        .frame(minWidth: 640, minHeight: 360)
        .background(.black)
    }
}
