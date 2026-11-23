import Foundation
import VideoToolbox
import CoreMedia
import CoreVideo

struct H264CodecConfiguration: Equatable {
    let sps: Data
    let pps: Data
    let nalUnitHeaderLength: Int32
}

struct H264EncodedFrame {
    let data: Data
    let presentationTime: CMTime
    let isKeyframe: Bool
    let codecConfiguration: H264CodecConfiguration
}

final class VideoEncoder {

    private var session: VTCompressionSession?
    private var codecConfiguration: H264CodecConfiguration?

    var onEncodedFrame: ((H264EncodedFrame) -> Void)?

    func start(
        width: Int,
        height: Int,
        fps: Int
    ) throws {

        guard session == nil else {
            throw SDError.invalidState
        }
        codecConfiguration = nil

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

        guard let formatDescription = CMSampleBufferGetFormatDescription(sampleBuffer),
              let codecConfiguration = extractCodecConfiguration(from: formatDescription) else {
            NSLog("[SlimeDisplay] Unable to read H.264 codec configuration")
            return
        }
        self.codecConfiguration = codecConfiguration

        let attachments = CMSampleBufferGetSampleAttachmentsArray(
            sampleBuffer,
            createIfNecessary: false
        ) as? [[CFString: Any]]

        let isKeyframe = !(
            attachments?.first?[kCMSampleAttachmentKey_NotSync] as? Bool
            ?? false
        )

        onEncodedFrame?(
            H264EncodedFrame(
                data: data,
                presentationTime: CMSampleBufferGetPresentationTimeStamp(sampleBuffer),
                isKeyframe: isKeyframe,
                codecConfiguration: codecConfiguration
            )
        )
    }

    private func extractCodecConfiguration(
        from formatDescription: CMFormatDescription
    ) -> H264CodecConfiguration? {
        var spsPointer: UnsafePointer<UInt8>?
        var spsSize = 0
        var parameterSetCount = 0
        var nalUnitHeaderLength: Int32 = 0

        guard CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
            formatDescription,
            parameterSetIndex: 0,
            parameterSetPointerOut: &spsPointer,
            parameterSetSizeOut: &spsSize,
            parameterSetCountOut: &parameterSetCount,
            nalUnitHeaderLengthOut: &nalUnitHeaderLength
        ) == noErr,
              parameterSetCount >= 2,
              let spsPointer else {
            return nil
        }

        var ppsPointer: UnsafePointer<UInt8>?
        var ppsSize = 0
        guard CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
            formatDescription,
            parameterSetIndex: 1,
            parameterSetPointerOut: &ppsPointer,
            parameterSetSizeOut: &ppsSize,
            parameterSetCountOut: nil,
            nalUnitHeaderLengthOut: nil
        ) == noErr,
              let ppsPointer else {
            return nil
        }

        return H264CodecConfiguration(
            sps: Data(bytes: spsPointer, count: spsSize),
            pps: Data(bytes: ppsPointer, count: ppsSize),
            nalUnitHeaderLength: nalUnitHeaderLength
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
        codecConfiguration = nil
    }
}
