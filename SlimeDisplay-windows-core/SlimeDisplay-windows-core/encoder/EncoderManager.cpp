#include "EncoderManager.h"

#include <windows.h>

#include <d3d11.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <codecapi.h>
#include <icodecapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <system_error>
#include <utility>

using Microsoft::WRL::ComPtr;

namespace {

constexpr size_t kMaximumQueuedFrames = 3;
constexpr GUID kLowLatencyCodecProperty{
    0x9d3ecd55, 0x89e8, 0x490a,
    {0x97, 0x0a, 0x0c, 0x95, 0x48, 0xd5, 0xa5, 0x6e}};
constexpr GUID kBFrameCountCodecProperty{
    0x8d390aac, 0xdc5c, 0x4200,
    {0xb5, 0x7f, 0x81, 0x4d, 0x04, 0xba, 0xba, 0xb2}};
constexpr GUID kQualityVsSpeedCodecProperty{
    0x98332df8, 0x03cd, 0x476b,
    {0x89, 0xfa, 0x3f, 0x9e, 0x44, 0x2d, 0xec, 0x9f}};
constexpr GUID kRateControlModeCodecProperty{
    0x1c0608e9, 0x370c, 0x4710,
    {0x8a, 0x58, 0xcb, 0x61, 0x81, 0xc4, 0x24, 0x23}};

HRESULT CreateVideoType(
    REFGUID subtype,
    uint32_t width,
    uint32_t height,
    uint32_t fps,
    ComPtr<IMFMediaType>& mediaType)
{
    HRESULT hr = MFCreateMediaType(&mediaType);
    if (SUCCEEDED(hr)) {
        hr = mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    }
    if (SUCCEEDED(hr)) {
        hr = mediaType->SetGUID(MF_MT_SUBTYPE, subtype);
    }
    if (SUCCEEDED(hr)) {
        hr = mediaType->SetUINT64(
            MF_MT_FRAME_SIZE,
            Pack2UINT32AsUINT64(width, height));
    }
    if (SUCCEEDED(hr)) {
        hr = mediaType->SetUINT64(
            MF_MT_FRAME_RATE,
            Pack2UINT32AsUINT64(fps, 1));
    }
    if (SUCCEEDED(hr)) {
        hr = mediaType->SetUINT64(
            MF_MT_PIXEL_ASPECT_RATIO,
            Pack2UINT32AsUINT64(1, 1));
    }
    if (SUCCEEDED(hr)) {
        hr = mediaType->SetUINT32(
            MF_MT_INTERLACE_MODE,
            MFVideoInterlace_Progressive);
    }
    if (SUCCEEDED(hr)) {
        hr = mediaType->SetUINT32(MF_LOW_LATENCY, TRUE);
    }
    return hr;
}

HRESULT ConfigureTransform(
    IMFTransform* transform,
    IMFDXGIDeviceManager* deviceManager,
    uint32_t width,
    uint32_t height,
    uint32_t fps,
    bool& isAsync)
{
    ComPtr<IMFAttributes> attributes;
    HRESULT hr = transform->GetAttributes(&attributes);
    if (SUCCEEDED(hr)) {
        UINT32 asyncValue = FALSE;
        if (SUCCEEDED(attributes->GetUINT32(
                MF_TRANSFORM_ASYNC,
                &asyncValue))) {
            isAsync = asyncValue != FALSE;
        }
        if (isAsync) {
            hr = attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
        }
    } else {
        hr = S_OK;
    }

    if (SUCCEEDED(hr) && deviceManager != nullptr) {
        hr = transform->ProcessMessage(
            MFT_MESSAGE_SET_D3D_MANAGER,
            reinterpret_cast<ULONG_PTR>(deviceManager));
    }

    ComPtr<IMFMediaType> outputType;
    if (SUCCEEDED(hr)) {
        hr = CreateVideoType(
            MFVideoFormat_H264,
            width,
            height,
            fps,
            outputType);
    }
    if (SUCCEEDED(hr)) {
        hr = outputType->SetUINT32(MF_MT_AVG_BITRATE, 6000000);
    }
    if (SUCCEEDED(hr)) {
        hr = transform->SetOutputType(0, outputType.Get(), 0);
    }

    ComPtr<IMFMediaType> inputType;
    if (SUCCEEDED(hr)) {
        hr = CreateVideoType(
            MFVideoFormat_NV12,
            width,
            height,
            fps,
            inputType);
    }
    if (SUCCEEDED(hr)) {
        hr = transform->SetInputType(0, inputType.Get(), 0);
    }
    if (SUCCEEDED(hr)) {
        ComPtr<ICodecAPI> codecApi;
        if (SUCCEEDED(transform->QueryInterface(
                __uuidof(ICodecAPI),
                reinterpret_cast<void**>(codecApi.GetAddressOf())))) {
            VARIANT value{};
            value.vt = VT_BOOL;
            value.boolVal = VARIANT_TRUE;
            codecApi->SetValue(&kLowLatencyCodecProperty, &value);
            value.vt = VT_UI4;
            value.ulVal = 0;
            codecApi->SetValue(&kBFrameCountCodecProperty, &value);
            value.ulVal = 100;
            codecApi->SetValue(&kQualityVsSpeedCodecProperty, &value);
            value.ulVal = eAVEncCommonRateControlMode_LowDelayVBR;
            codecApi->SetValue(&kRateControlModeCodecProperty, &value);
        }
    }
    return hr;
}

HRESULT CreateEncoder(
    uint32_t width,
    uint32_t height,
    uint32_t fps,
    IMFDXGIDeviceManager* deviceManager,
    ComPtr<IMFTransform>& transform,
    bool& isAsync)
{
    MFT_REGISTER_TYPE_INFO inputInfo{MFMediaType_Video, MFVideoFormat_NV12};
    MFT_REGISTER_TYPE_INFO outputInfo{MFMediaType_Video, MFVideoFormat_H264};
    const UINT32 flags[] = {
        MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
        MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT |
            MFT_ENUM_FLAG_SORTANDFILTER
    };

    for (UINT32 enumerationFlags : flags) {
        if ((enumerationFlags & MFT_ENUM_FLAG_HARDWARE) != 0 &&
            deviceManager == nullptr) {
            continue;
        }
        IMFActivate** activations = nullptr;
        UINT32 activationCount = 0;
        HRESULT hr = MFTEnumEx(
            MFT_CATEGORY_VIDEO_ENCODER,
            enumerationFlags,
            &inputInfo,
            &outputInfo,
            &activations,
            &activationCount);
        if (FAILED(hr)) {
            continue;
        }

        for (UINT32 index = 0; index < activationCount; ++index) {
            ComPtr<IMFTransform> candidate;
            bool candidateIsAsync = false;
            hr = activations[index]->ActivateObject(
                IID_PPV_ARGS(&candidate));
            if (SUCCEEDED(hr)) {
                hr = ConfigureTransform(
                    candidate.Get(),
                    enumerationFlags & MFT_ENUM_FLAG_HARDWARE
                        ? deviceManager
                        : nullptr,
                    width,
                    height,
                    fps,
                    candidateIsAsync);
            }
            if (SUCCEEDED(hr)) {
                transform = std::move(candidate);
                isAsync = candidateIsAsync;
                for (UINT32 remaining = index; remaining < activationCount;
                     ++remaining) {
                    activations[remaining]->Release();
                }
                for (UINT32 previous = 0; previous < index; ++previous) {
                    activations[previous]->Release();
                }
                CoTaskMemFree(activations);
                return S_OK;
            }
        }

        for (UINT32 index = 0; index < activationCount; ++index) {
            activations[index]->Release();
        }
        CoTaskMemFree(activations);
    }

    return MF_E_TOPO_CODEC_NOT_FOUND;
}

HRESULT CreateDxgiDeviceManager(
    ComPtr<IMFDXGIDeviceManager>& deviceManager)
{
    ComPtr<ID3D11Device> device;
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT |
            D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &device,
        nullptr,
        nullptr);
    if (FAILED(hr)) {
        return hr;
    }

    UINT resetToken = 0;
    hr = MFCreateDXGIDeviceManager(&resetToken, &deviceManager);
    if (SUCCEEDED(hr)) {
        hr = deviceManager->ResetDevice(device.Get(), resetToken);
    }
    return hr;
}

void ConvertBgraToNv12(
    const std::vector<uint8_t>& bgra,
    uint32_t width,
    uint32_t height,
    uint8_t* nv12)
{
    const size_t ySize = static_cast<size_t>(width) * height;
    uint8_t* yPlane = nv12;
    uint8_t* uvPlane = nv12 + ySize;

    for (uint32_t row = 0; row < height; ++row) {
        for (uint32_t column = 0; column < width; ++column) {
            const size_t source = (static_cast<size_t>(row) * width + column) * 4;
            const int blue = bgra[source];
            const int green = bgra[source + 1];
            const int red = bgra[source + 2];
            const int value =
                ((66 * red + 129 * green + 25 * blue + 128) >> 8) + 16;
            yPlane[static_cast<size_t>(row) * width + column] =
                static_cast<uint8_t>(std::clamp(value, 16, 235));
        }
    }

    for (uint32_t row = 0; row < height; row += 2) {
        for (uint32_t column = 0; column < width; column += 2) {
            int red = 0;
            int green = 0;
            int blue = 0;
            for (uint32_t y = 0; y < 2; ++y) {
                for (uint32_t x = 0; x < 2; ++x) {
                    const size_t source =
                        (static_cast<size_t>(row + y) * width + column + x) * 4;
                    blue += bgra[source];
                    green += bgra[source + 1];
                    red += bgra[source + 2];
                }
            }
            red /= 4;
            green /= 4;
            blue /= 4;
            const int u = ((-38 * red - 74 * green + 112 * blue + 128) >> 8) + 128;
            const int v = ((112 * red - 94 * green - 18 * blue + 128) >> 8) + 128;
            const size_t chroma =
                static_cast<size_t>(row / 2) * width + column;
            uvPlane[chroma] =
                static_cast<uint8_t>(std::clamp(u, 16, 240));
            uvPlane[chroma + 1] =
                static_cast<uint8_t>(std::clamp(v, 16, 240));
        }
    }
}

HRESULT CreateInputSample(
    const std::vector<uint8_t>& bgra,
    uint32_t width,
    uint32_t height,
    int64_t presentationTime,
    uint32_t fps,
    ComPtr<IMFSample>& sample)
{
    const size_t ySize = static_cast<size_t>(width) * height;
    const size_t nv12Size = ySize + ySize / 2;
    if (nv12Size > std::numeric_limits<DWORD>::max()) {
        return E_INVALIDARG;
    }

    ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = MFCreateMemoryBuffer(
        static_cast<DWORD>(nv12Size),
        &buffer);
    BYTE* destination = nullptr;
    DWORD maximumLength = 0;
    if (SUCCEEDED(hr)) {
        hr = buffer->Lock(&destination, nullptr, &maximumLength);
    }
    if (SUCCEEDED(hr)) {
        ConvertBgraToNv12(
            bgra,
            width,
            height,
            destination);
        hr = buffer->Unlock();
    }
    if (SUCCEEDED(hr)) {
        hr = buffer->SetCurrentLength(static_cast<DWORD>(nv12Size));
    }
    if (SUCCEEDED(hr)) {
        hr = MFCreateSample(&sample);
    }
    if (SUCCEEDED(hr)) {
        hr = sample->AddBuffer(buffer.Get());
    }
    if (SUCCEEDED(hr)) {
        hr = sample->SetSampleTime(presentationTime);
    }
    if (SUCCEEDED(hr)) {
        hr = sample->SetSampleDuration(10000000LL / fps);
    }
    return hr;
}

HRESULT DrainOutput(
    IMFTransform* transform,
    uint32_t width,
    uint32_t height,
    uint32_t fps,
    SD_EncodedFrameCallback callback,
    void* userData)
{
    MFT_OUTPUT_STREAM_INFO streamInfo{};
    HRESULT hr = transform->GetOutputStreamInfo(0, &streamInfo);
    if (FAILED(hr)) {
        return hr;
    }

    for (;;) {
        ComPtr<IMFSample> outputSample;
        if ((streamInfo.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) == 0) {
            const DWORD sampleSize = std::max<DWORD>(
                streamInfo.cbSize,
                static_cast<DWORD>(std::max<uint64_t>(
                    1024 * 1024,
                    static_cast<uint64_t>(width) * height)));
            ComPtr<IMFMediaBuffer> outputBuffer;
            hr = MFCreateMemoryBuffer(sampleSize, &outputBuffer);
            if (SUCCEEDED(hr)) {
                hr = MFCreateSample(&outputSample);
            }
            if (SUCCEEDED(hr)) {
                hr = outputSample->AddBuffer(outputBuffer.Get());
            }
            if (FAILED(hr)) {
                return hr;
            }
        }

        MFT_OUTPUT_DATA_BUFFER output{};
        output.dwStreamID = 0;
        output.pSample = outputSample.Get();
        DWORD status = 0;
        hr = transform->ProcessOutput(0, 1, &output, &status);
        if (output.pEvents != nullptr) {
            output.pEvents->Release();
        }
        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
            return S_OK;
        }
        if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
            ComPtr<IMFMediaType> availableType;
            for (DWORD typeIndex = 0;; ++typeIndex) {
                hr = transform->GetOutputAvailableType(
                    0,
                    typeIndex,
                    &availableType);
                if (FAILED(hr)) {
                    return hr;
                }
                GUID subtype{};
                hr = availableType->GetGUID(MF_MT_SUBTYPE, &subtype);
                if (SUCCEEDED(hr) && subtype == MFVideoFormat_H264) {
                    hr = transform->SetOutputType(0, availableType.Get(), 0);
                    break;
                }
                availableType.Reset();
            }
            if (FAILED(hr)) {
                return hr;
            }
            hr = transform->GetOutputStreamInfo(0, &streamInfo);
            if (FAILED(hr)) {
                return hr;
            }
            continue;
        }
        if (FAILED(hr)) {
            return hr;
        }

        if (output.pSample != nullptr &&
            output.pSample != outputSample.Get()) {
            outputSample.Reset();
            outputSample.Attach(output.pSample);
        } else if (!outputSample && output.pSample != nullptr) {
            outputSample.Attach(output.pSample);
        }
        if (outputSample && callback != nullptr) {
            ComPtr<IMFMediaBuffer> encodedBuffer;
            hr = outputSample->ConvertToContiguousBuffer(&encodedBuffer);
            if (FAILED(hr)) {
                return hr;
            }

            BYTE* bytes = nullptr;
            DWORD length = 0;
            hr = encodedBuffer->Lock(&bytes, nullptr, &length);
            if (FAILED(hr)) {
                return hr;
            }

            LONGLONG sampleTime = 0;
            outputSample->GetSampleTime(&sampleTime);
            UINT32 cleanPoint = FALSE;
            outputSample->GetUINT32(MFSampleExtension_CleanPoint, &cleanPoint);
            try {
                callback(
                    bytes,
                    length,
                    sampleTime,
                    cleanPoint ? 1 : 0,
                    userData);
            } catch (...) {
                OutputDebugStringW(
                    L"EncoderManager: encoded-frame callback threw an exception.\n");
                encodedBuffer->Unlock();
                return E_FAIL;
            }

            const HRESULT unlockResult = encodedBuffer->Unlock();
            if (FAILED(unlockResult)) {
                return unlockResult;
            }
        }

        if ((output.dwStatus & MFT_OUTPUT_DATA_BUFFER_INCOMPLETE) == 0) {
            return S_OK;
        }
    }
}

SD_Result ToResult(HRESULT hr)
{
    if (hr == MF_E_TOPO_CODEC_NOT_FOUND ||
        hr == MF_E_INVALIDMEDIATYPE ||
        hr == MF_E_INVALIDSTREAMNUMBER) {
        return SD_ERROR_NOT_SUPPORTED;
    }
    return SD_ERROR_ENCODING;
}

} // namespace

EncoderManager::~EncoderManager()
{
    stop();
}

SD_Result EncoderManager::start(
    uint32_t width,
    uint32_t height,
    uint32_t fps)
{
    if (width == 0 || height == 0 || fps == 0 ||
        (width & 1) != 0 || (height & 1) != 0) {
        return SD_ERROR_INVALID_ARGUMENT;
    }
    if (running_) {
        return SD_ERROR_INVALID_STATE;
    }
    if (worker_.joinable()) {
        worker_.join();
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        startup_complete_ = false;
        startup_result_ = SD_ERROR_INITIALIZATION;
        frames_.clear();
    }
    last_result_ = SD_SUCCESS;
    running_ = true;
    try {
        worker_ = std::thread(&EncoderManager::run, this, width, height, fps);
    } catch (const std::system_error&) {
        running_ = false;
        return SD_ERROR_INITIALIZATION;
    } catch (...) {
        running_ = false;
        throw;
    }

    std::unique_lock<std::mutex> lock(mutex_);
    condition_.wait(lock, [this] { return startup_complete_; });
    const SD_Result result = startup_result_;
    lock.unlock();
    if (result != SD_SUCCESS && worker_.joinable()) {
        worker_.join();
    }
    return result;
}

SD_Result EncoderManager::stop()
{
    running_ = false;
    condition_.notify_all();
    if (worker_.joinable()) {
        if (worker_.get_id() == std::this_thread::get_id()) {
            return SD_ERROR_INVALID_STATE;
        }
        worker_.join();
    }
    return last_result_;
}

SD_Result EncoderManager::enqueue_frame(
    const std::vector<uint8_t>& bgra,
    int64_t presentationTime)
{
    if (!running_) {
        return SD_SUCCESS;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) {
            return SD_SUCCESS;
        }
        if (frames_.size() == kMaximumQueuedFrames) {
            frames_.pop_front();
        }
        frames_.push_back(Frame{bgra, presentationTime});
    }
    condition_.notify_one();
    return SD_SUCCESS;
}

void EncoderManager::set_callback(
    SD_EncodedFrameCallback callback,
    void* userData)
{
    std::lock_guard<std::mutex> lock(mutex_);
    callback_ = callback;
    callback_user_data_ = callback == nullptr ? nullptr : userData;
}

void EncoderManager::signal_start(SD_Result result)
{
    if (result != SD_SUCCESS) {
        last_result_ = result;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        startup_result_ = result;
        startup_complete_ = true;
    }
    condition_.notify_all();
}

void EncoderManager::run(uint32_t width, uint32_t height, uint32_t fps)
{
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitializeCom = SUCCEEDED(comResult);
    if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) {
        running_ = false;
        signal_start(SD_ERROR_INITIALIZATION);
        return;
    }

    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    const bool mediaFoundationStarted = SUCCEEDED(hr);
    if (!mediaFoundationStarted) {
        running_ = false;
        signal_start(SD_ERROR_INITIALIZATION);
        if (uninitializeCom) {
            CoUninitialize();
        }
        return;
    }

    ComPtr<IMFTransform> transform;
    ComPtr<IMFDXGIDeviceManager> deviceManager;
    bool isAsync = false;
    if (FAILED(CreateDxgiDeviceManager(deviceManager))) {
        deviceManager.Reset();
    }
    hr = CreateEncoder(
        width,
        height,
        fps,
        deviceManager.Get(),
        transform,
        isAsync);
    if (SUCCEEDED(hr)) {
        hr = transform->ProcessMessage(
            MFT_MESSAGE_NOTIFY_BEGIN_STREAMING,
            0);
    }
    if (SUCCEEDED(hr)) {
        hr = transform->ProcessMessage(
            MFT_MESSAGE_NOTIFY_START_OF_STREAM,
            0);
    }

    if (FAILED(hr)) {
        running_ = false;
        signal_start(ToResult(hr));
        transform.Reset();
        deviceManager.Reset();
        MFShutdown();
        if (uninitializeCom) {
            CoUninitialize();
        }
        return;
    }
    signal_start(SD_SUCCESS);

    if (isAsync) {
        ComPtr<IMFMediaEventGenerator> eventGenerator;
        hr = transform.As(&eventGenerator);
        bool needInput = false;
        bool drainSent = false;
        bool drainComplete = false;

        while (SUCCEEDED(hr) && !drainComplete) {
            bool receivedEvent = false;
            for (;;) {
                ComPtr<IMFMediaEvent> event;
                const HRESULT eventResult = eventGenerator->GetEvent(
                    MF_EVENT_FLAG_NO_WAIT,
                    &event);
                if (eventResult == MF_E_NO_EVENTS_AVAILABLE) {
                    break;
                }
                if (FAILED(eventResult)) {
                    hr = eventResult;
                    break;
                }
                receivedEvent = true;

                MediaEventType eventType;
                hr = event->GetType(&eventType);
                if (FAILED(hr)) {
                    break;
                }
                if (eventType == METransformNeedInput) {
                    needInput = true;
                } else if (eventType == METransformHaveOutput) {
                    SD_EncodedFrameCallback callback;
                    void* userData;
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        callback = callback_;
                        userData = callback_user_data_;
                    }
                    hr = DrainOutput(
                        transform.Get(),
                        width,
                        height,
                        fps,
                        callback,
                        userData);
                    if (FAILED(hr)) {
                        break;
                    }
                } else if (eventType == METransformDrainComplete) {
                    drainComplete = true;
                } else if (eventType == MEError) {
                    HRESULT eventStatus = S_OK;
                    hr = event->GetStatus(&eventStatus);
                    if (SUCCEEDED(hr)) {
                        hr = eventStatus;
                    }
                    break;
                }
            }
            if (FAILED(hr) || drainComplete) {
                break;
            }

            Frame frame;
            bool haveFrame = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (needInput && !frames_.empty()) {
                    frame = std::move(frames_.front());
                    frames_.pop_front();
                    haveFrame = true;
                    needInput = false;
                }
            }
            if (haveFrame) {
                ComPtr<IMFSample> inputSample;
                hr = CreateInputSample(
                    frame.bgra,
                    width,
                    height,
                    frame.presentation_time,
                    fps,
                    inputSample);
                if (SUCCEEDED(hr)) {
                    hr = transform->ProcessInput(0, inputSample.Get(), 0);
                }
                if (hr == MF_E_NOTACCEPTING) {
                    hr = S_OK;
                    needInput = false;
                }
                if (FAILED(hr)) {
                    break;
                }
            }

            bool queueEmpty = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                queueEmpty = frames_.empty();
            }
            if (!running_ && queueEmpty && !drainSent) {
                hr = transform->ProcessMessage(
                    MFT_MESSAGE_NOTIFY_END_OF_STREAM,
                    0);
                if (SUCCEEDED(hr)) {
                    hr = transform->ProcessMessage(
                        MFT_MESSAGE_COMMAND_DRAIN,
                        0);
                }
                drainSent = SUCCEEDED(hr);
            }

            if (SUCCEEDED(hr) && !receivedEvent && !haveFrame) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }
    } else {
        for (;;) {
            Frame frame;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [this] {
                    return !running_ || !frames_.empty();
                });
                if (!running_ && frames_.empty()) {
                    break;
                }
                frame = std::move(frames_.front());
                frames_.pop_front();
            }

            ComPtr<IMFSample> inputSample;
            hr = CreateInputSample(
                frame.bgra,
                width,
                height,
                frame.presentation_time,
                fps,
                inputSample);
            if (SUCCEEDED(hr)) {
                hr = transform->ProcessInput(0, inputSample.Get(), 0);
            }
            if (hr == MF_E_NOTACCEPTING) {
                SD_EncodedFrameCallback callback;
                void* userData;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    callback = callback_;
                    userData = callback_user_data_;
                }
                hr = DrainOutput(
                    transform.Get(),
                    width,
                    height,
                    fps,
                    callback,
                    userData);
                if (SUCCEEDED(hr)) {
                    hr = transform->ProcessInput(0, inputSample.Get(), 0);
                }
            }
            if (SUCCEEDED(hr)) {
                SD_EncodedFrameCallback callback;
                void* userData;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    callback = callback_;
                    userData = callback_user_data_;
                }
                hr = DrainOutput(
                    transform.Get(),
                    width,
                    height,
                    fps,
                    callback,
                    userData);
            }
            if (FAILED(hr)) {
                OutputDebugStringW(
                    L"EncoderManager: Media Foundation frame processing failed.\n");
                last_result_ = SD_ERROR_ENCODING;
                running_ = false;
                std::lock_guard<std::mutex> lock(mutex_);
                frames_.clear();
            }
        }

        transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        transform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
        SD_EncodedFrameCallback callback;
        void* userData;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callback = callback_;
            userData = callback_user_data_;
        }
        const HRESULT drainResult = DrainOutput(
            transform.Get(),
            width,
            height,
            fps,
            callback,
            userData);
        if (FAILED(drainResult)) {
            last_result_ = SD_ERROR_ENCODING;
        }
    }

    if (FAILED(hr)) {
        OutputDebugStringW(
            L"EncoderManager: Media Foundation frame processing failed.\n");
        last_result_ = SD_ERROR_ENCODING;
        running_ = false;
    }
    transform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
    transform.Reset();
    deviceManager.Reset();
    MFShutdown();
    if (uninitializeCom) {
        CoUninitialize();
    }
}
