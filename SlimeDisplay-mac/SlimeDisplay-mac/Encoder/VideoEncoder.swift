import Foundation
import VideoToolbox
import CoreMedia
import CoreVideo

final class VideoEncoder {

    private var session: VTCompressionSession?

    var onEncodedFrame: ((
        Data,
        CMTime,
        Bool
    ) -> Void)?

    func start(
        width: Int,
        height: Int,
        fps: Int
    ) throws {

        guard session == nil else {
            throw SDError.invalidState
        }

        let callback: VTCompressionOutputCallback = {
            refcon,
            frameRefcon,
            status,
            infoFlags,
            sampleBuffer in

            guard status == noErr,
                  let sampleBuffer,
                  CMSampleBufferDataIsReady(sampleBuffer),
                  let refcon else {
                return
            }

            let encoder = Unmanaged<VideoEncoder>
                .fromOpaque(refcon)
                .takeUnretainedValue()

            encoder.handleEncodedFrame(sampleBuffer)
        }

        let result = VTCompressionSessionCreate(
            allocator: kCFAllocatorDefault,
            width: Int32(width),
            height: Int32(height),
            codecType: kCMVideoCodecType_H264,
            encoderSpecification: nil,
            imageBufferAttributes: nil,
            compressedDataAllocator: nil,
            outputCallback: callback,
            refcon: UnsafeMutableRawPointer(
                Unmanaged.passUnretained(self).toOpaque()
            ),
            compressionSessionOut: &session
        )

        guard result == noErr, let session else {
            throw SDError.encodingFailed
        }

        VTSessionSetProperty(
            session,
            key: kVTCompressionPropertyKey_RealTime,
            value: kCFBooleanTrue
        )

        VTSessionSetProperty(
            session,
            key: kVTCompressionPropertyKey_AllowFrameReordering,
            value: kCFBooleanFalse
        )

        VTSessionSetProperty(
            session,
            key: kVTCompressionPropertyKey_ProfileLevel,
            value: kVTProfileLevel_H264_Main_AutoLevel
        )

        VTSessionSetProperty(
            session,
            key: kVTCompressionPropertyKey_ExpectedFrameRate,
            value: fps as CFNumber
        )

        VTCompressionSessionPrepareToEncodeFrames(session)
    }

    func encode(_ sampleBuffer: CMSampleBuffer) throws {

        guard let session,
              let imageBuffer =
                CMSampleBufferGetImageBuffer(sampleBuffer) else {
            throw SDError.invalidState
        }

        let pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer)

        let duration = CMSampleBufferGetDuration(sampleBuffer)

        let result = VTCompressionSessionEncodeFrame(
            session,
            imageBuffer: imageBuffer,
            presentationTimeStamp: pts,
            duration: duration,
            frameProperties: nil,
            sourceFrameRefcon: nil,
            infoFlagsOut: nil
        )

        guard result == noErr else {
            throw SDError.encodingFailed
        }
    }

    private func handleEncodedFrame(
        _ sampleBuffer: CMSampleBuffer
    ) {

        guard let blockBuffer =
            CMSampleBufferGetDataBuffer(sampleBuffer) else {
            return
        }

        var length = 0
        var totalLength = 0
        var dataPointer: UnsafeMutablePointer<Int8>?

        let result = CMBlockBufferGetDataPointer(
            blockBuffer,
            atOffset: 0,
            lengthAtOffsetOut: &length,
            totalLengthOut: &totalLength,
            dataPointerOut: &dataPointer
        )

        guard result == kCMBlockBufferNoErr,
              let dataPointer else {
            return
        }

        let data = Data(
            bytes: dataPointer,
            count: totalLength
        )

        let attachments = CMSampleBufferGetSampleAttachmentsArray(
            sampleBuffer,
            createIfNecessary: false
        ) as? [[CFString: Any]]

        let isKeyframe = !(
            attachments?.first?[kCMSampleAttachmentKey_NotSync] as? Bool
            ?? false
        )

        onEncodedFrame?(
            data,
            CMSampleBufferGetPresentationTimeStamp(sampleBuffer),
            isKeyframe
        )
    }

    func stop() {
        guard let session else { return }

        VTCompressionSessionCompleteFrames(
            session,
            untilPresentationTimeStamp: .invalid
        )

        VTCompressionSessionInvalidate(session)
        self.session = nil
    }
}
