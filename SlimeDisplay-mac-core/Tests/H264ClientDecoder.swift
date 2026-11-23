import CoreMedia
import CoreVideo
import VideoToolbox

final class H264ClientDecoder {
    var onDecodedFrame: ((CVPixelBuffer) -> Void)?

    private let queue = DispatchQueue(label: "slimedisplay.test-client-decoder")
    private var formatConfiguration: H264CodecConfiguration?
    private var formatDescription: CMFormatDescription?
    private var decompressionSession: VTDecompressionSession?

    func decode(_ frame: H264EncodedFrame) {
        queue.async { [weak self] in
            self?.decodeOnQueue(frame)
        }
    }

    func invalidate() {
        queue.sync {
            guard let decompressionSession else { return }
            VTDecompressionSessionWaitForAsynchronousFrames(decompressionSession)
            VTDecompressionSessionInvalidate(decompressionSession)
            self.decompressionSession = nil
            formatConfiguration = nil
            formatDescription = nil
        }
    }

    private func decodeOnQueue(_ frame: H264EncodedFrame) {
        if formatConfiguration != frame.codecConfiguration {
            guard createDecompressionSession(for: frame.codecConfiguration) else {
                return
            }
        }

        guard let decompressionSession,
              let sampleBuffer = makeSampleBuffer(from: frame) else {
            NSLog("[SlimeDisplay Test Client] Could not create H.264 sample buffer")
            return
        }

        let status = VTDecompressionSessionDecodeFrame(
            decompressionSession,
            sampleBuffer: sampleBuffer,
            flags: [],
            frameRefcon: nil,
            infoFlagsOut: nil
        )

        guard status == noErr else {
            NSLog("[SlimeDisplay Test Client] H.264 decode failed: %d", status)
            return
        }
    }

    private func createDecompressionSession(
        for configuration: H264CodecConfiguration
    ) -> Bool {
        guard let formatDescription = makeFormatDescription(from: configuration) else {
            NSLog("[SlimeDisplay Test Client] Invalid H.264 parameter sets")
            return false
        }

        if let decompressionSession {
            VTDecompressionSessionWaitForAsynchronousFrames(decompressionSession)
            VTDecompressionSessionInvalidate(decompressionSession)
            self.decompressionSession = nil
            formatConfiguration = nil
            self.formatDescription = nil
        }

        var callbackRecord = VTDecompressionOutputCallbackRecord(
            decompressionOutputCallback: h264DecompressionOutputCallback,
            decompressionOutputRefCon: Unmanaged.passUnretained(self).toOpaque()
        )
        var newSession: VTDecompressionSession?

        let status = VTDecompressionSessionCreate(
            allocator: kCFAllocatorDefault,
            formatDescription: formatDescription,
            decoderSpecification: nil,
            imageBufferAttributes: [
                kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA
            ] as CFDictionary,
            outputCallback: &callbackRecord,
            decompressionSessionOut: &newSession
        )

        guard status == noErr, let newSession else {
            NSLog("[SlimeDisplay Test Client] Could not create H.264 decoder: %d", status)
            return false
        }

        decompressionSession = newSession
        formatConfiguration = configuration
        self.formatDescription = formatDescription
        return true
    }

    private func makeFormatDescription(
        from configuration: H264CodecConfiguration
    ) -> CMFormatDescription? {
        guard !configuration.sps.isEmpty, !configuration.pps.isEmpty else {
            return nil
        }

        var formatDescription: CMFormatDescription?
        let status = configuration.sps.withUnsafeBytes { spsBytes in
            configuration.pps.withUnsafeBytes { ppsBytes in
                guard let sps = spsBytes.bindMemory(to: UInt8.self).baseAddress,
                      let pps = ppsBytes.bindMemory(to: UInt8.self).baseAddress else {
                    return kCMFormatDescriptionError_InvalidParameter
                }

                let parameterSets = [sps, pps]
                let parameterSetSizes = [
                    configuration.sps.count,
                    configuration.pps.count,
                ]

                return parameterSets.withUnsafeBufferPointer { pointers in
                    parameterSetSizes.withUnsafeBufferPointer { sizes in
                        CMVideoFormatDescriptionCreateFromH264ParameterSets(
                            allocator: kCFAllocatorDefault,
                            parameterSetCount: parameterSets.count,
                            parameterSetPointers: pointers.baseAddress!,
                            parameterSetSizes: sizes.baseAddress!,
                            nalUnitHeaderLength: configuration.nalUnitHeaderLength,
                            formatDescriptionOut: &formatDescription
                        )
                    }
                }
            }
        }

        guard status == noErr else { return nil }
        return formatDescription
    }

    private func makeSampleBuffer(
        from frame: H264EncodedFrame
    ) -> CMSampleBuffer? {
        guard decompressionSession != nil,
              let formatDescription else { return nil }

        var blockBuffer: CMBlockBuffer?
        let blockStatus = CMBlockBufferCreateWithMemoryBlock(
            allocator: kCFAllocatorDefault,
            memoryBlock: nil,
            blockLength: frame.data.count,
            blockAllocator: kCFAllocatorDefault,
            customBlockSource: nil,
            offsetToData: 0,
            dataLength: frame.data.count,
            flags: 0,
            blockBufferOut: &blockBuffer
        )

        guard blockStatus == kCMBlockBufferNoErr,
              let blockBuffer else {
            return nil
        }

        let copyStatus = frame.data.withUnsafeBytes { bytes in
            guard let baseAddress = bytes.baseAddress else {
                return kCMBlockBufferBadLengthParameterErr
            }

            return CMBlockBufferReplaceDataBytes(
                with: baseAddress,
                blockBuffer: blockBuffer,
                offsetIntoDestination: 0,
                dataLength: frame.data.count
            )
        }
        guard copyStatus == kCMBlockBufferNoErr else { return nil }

        var timing = CMSampleTimingInfo(
            duration: .invalid,
            presentationTimeStamp: frame.presentationTime,
            decodeTimeStamp: .invalid
        )
        var sampleSize = frame.data.count
        var sampleBuffer: CMSampleBuffer?
        let sampleStatus = CMSampleBufferCreateReady(
            allocator: kCFAllocatorDefault,
            dataBuffer: blockBuffer,
            formatDescription: formatDescription,
            sampleCount: 1,
            sampleTimingEntryCount: 1,
            sampleTimingArray: &timing,
            sampleSizeEntryCount: 1,
            sampleSizeArray: &sampleSize,
            sampleBufferOut: &sampleBuffer
        )

        guard sampleStatus == noErr, let sampleBuffer else { return nil }

        CMSetAttachment(
            sampleBuffer,
            key: kCMSampleAttachmentKey_NotSync,
            value: frame.isKeyframe ? kCFBooleanFalse : kCFBooleanTrue,
            attachmentMode: kCMAttachmentMode_ShouldPropagate
        )
        return sampleBuffer
    }
}

private let h264DecompressionOutputCallback: VTDecompressionOutputCallback = {
    refcon,
    _,
    status,
    _,
    imageBuffer,
    _,
    _ in

    guard status == noErr,
          let refcon,
          let imageBuffer else {
        return
    }

    let decoder = Unmanaged<H264ClientDecoder>
        .fromOpaque(refcon)
        .takeUnretainedValue()
    decoder.onDecodedFrame?(imageBuffer)
}
