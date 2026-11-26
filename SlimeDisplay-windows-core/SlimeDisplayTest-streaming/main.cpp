#define NOMINMAX
#include "slimedisplay.h"

#include <windows.h>

#include <codecapi.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mf.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

using Microsoft::WRL::ComPtr;

namespace {

    constexpr wchar_t kWindowClassName[] = L"SlimeDisplayStreamViewerClass";
    constexpr size_t kMaxEncodedQueueSize = 8;

    using CreateFunction = SD_Result(SD_CALL*)(SD_Handle**);
    using DestroyFunction = void(SD_CALL*)(SD_Handle*);
    using CreateDisplayFunction =
        SD_Result(SD_CALL*)(SD_Handle*, const SD_DisplayConfig*);
    using DestroyDisplayFunction = SD_Result(SD_CALL*)(SD_Handle*);
    using GetDisplayInfoFunction =
        SD_Result(SD_CALL*)(SD_Handle*, SD_DisplayInfo*);
    using StartFunction = SD_Result(SD_CALL*)(SD_Handle*);
    using StopFunction = SD_Result(SD_CALL*)(SD_Handle*);
    using GetVersionFunction = uint32_t(SD_CALL*)();
    using SetCallbackFunction =
        SD_Result(SD_CALL*)(SD_Handle*, SD_EncodedFrameCallback, void*);

    struct CoreApi {
        HMODULE module = nullptr;
        CreateFunction create = nullptr;
        DestroyFunction destroy = nullptr;
        CreateDisplayFunction create_display = nullptr;
        DestroyDisplayFunction destroy_display = nullptr;
        GetDisplayInfoFunction get_display_info = nullptr;
        StartFunction start_capture = nullptr;
        StopFunction stop_capture = nullptr;
        StartFunction start_encoder = nullptr;
        StopFunction stop_encoder = nullptr;
        GetVersionFunction get_version = nullptr;
        SetCallbackFunction set_callback = nullptr;
    };

    struct EncodedPacket {
        std::vector<uint8_t> data;
        int64_t timestamp = 0;
        bool is_keyframe = false;
    };

    struct ViewerContext {
        std::atomic<bool> running{ false };
        std::atomic<HWND> hwnd{ nullptr };

        std::mutex packet_mutex;
        std::condition_variable packet_cv;
        std::deque<EncodedPacket> packets;
        bool waiting_for_keyframe = true;

        std::mutex frame_mutex;
        std::vector<uint8_t> bgra_frame;
        uint32_t frame_width = 0;
        uint32_t frame_height = 0;
        uint64_t decoded_frames = 0;
        std::atomic<bool> gpu_displaying{ false };
    };

    template <typename Function>
    Function LoadFunction(HMODULE module, const char* name)
    {
        return reinterpret_cast<Function>(GetProcAddress(module, name));
    }

    bool LoadCoreApi(CoreApi& api)
    {
#ifdef _DEBUG
        constexpr wchar_t kConfiguration[] = L"Debug";
#else
        constexpr wchar_t kConfiguration[] = L"Release";
#endif
        wchar_t executablePath[MAX_PATH]{};
        const DWORD pathLength =
            GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
        if (pathLength == 0 || pathLength == MAX_PATH) {
            return false;
        }

        const std::filesystem::path exeDir =
            std::filesystem::path(executablePath).parent_path();

        // 실행 파일 옆에 DLL이 있으면 우선 사용하고, 없으면 기존 빌드 트리 경로 탐색
        std::filesystem::path dllPath = exeDir / L"SlimeDisplay-windows-core.dll";
        if (!std::filesystem::exists(dllPath)) {
            dllPath = exeDir.parent_path().parent_path().parent_path() /
                L"SlimeDisplay-windows-core" / L"x64" / kConfiguration /
                L"SlimeDisplay-windows-core.dll";
        }

        api.module = LoadLibraryW(dllPath.c_str());
        if (api.module == nullptr) {
            std::wcerr << L"Failed to load DLL: " << dllPath.c_str() << L"\n";
            return false;
        }

        api.create = LoadFunction<CreateFunction>(api.module, "sd_create");
        api.destroy = LoadFunction<DestroyFunction>(api.module, "sd_destroy");
        api.create_display =
            LoadFunction<CreateDisplayFunction>(api.module, "sd_create_virtual_display");
        api.destroy_display =
            LoadFunction<DestroyDisplayFunction>(api.module, "sd_destroy_virtual_display");
        api.get_display_info =
            LoadFunction<GetDisplayInfoFunction>(api.module, "sd_get_virtual_display_info");
        api.start_capture =
            LoadFunction<StartFunction>(api.module, "sd_start_capture");
        api.stop_capture =
            LoadFunction<StopFunction>(api.module, "sd_stop_capture");
        api.start_encoder =
            LoadFunction<StartFunction>(api.module, "sd_start_encoder");
        api.stop_encoder =
            LoadFunction<StopFunction>(api.module, "sd_stop_encoder");
        api.get_version =
            LoadFunction<GetVersionFunction>(api.module, "sd_get_version");
        api.set_callback =
            LoadFunction<SetCallbackFunction>(api.module, "sd_set_encoded_frame_callback");

        return api.create && api.destroy && api.create_display &&
            api.destroy_display && api.get_display_info &&
            api.start_capture && api.stop_capture &&
            api.start_encoder && api.stop_encoder &&
            api.get_version && api.set_callback;
    }

    bool FindVddMode(SD_DisplayConfig& config)
    {
        for (DWORD adapterIndex = 0;; ++adapterIndex) {
            DISPLAY_DEVICEW adapter{};
            adapter.cb = sizeof(adapter);
            if (!EnumDisplayDevicesW(nullptr, adapterIndex, &adapter, 0)) {
                return false;
            }

            if (wcsstr(adapter.DeviceString, L"Virtual Display Driver") == nullptr &&
                wcsstr(adapter.DeviceID, L"MTTVDD") == nullptr) {
                continue;
            }
            if ((adapter.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == 0) {
                continue;
            }

            DEVMODEW mode{};
            mode.dmSize = sizeof(mode);
            if (!EnumDisplaySettingsExW(
                adapter.DeviceName,
                ENUM_CURRENT_SETTINGS,
                &mode,
                0)) {
                continue;
            }

            config.width = mode.dmPelsWidth;
            config.height = mode.dmPelsHeight;
            config.fps = mode.dmDisplayFrequency;
            return config.width != 0 && config.height != 0 && config.fps != 0;
        }
    }
    bool FindPrimaryDisplayMode(SD_DisplayConfig& config)
    {
        for (DWORD adapterIndex = 0;; ++adapterIndex) {
            DISPLAY_DEVICEW adapter{};
            adapter.cb = sizeof(adapter);

            if (!EnumDisplayDevicesW(
                nullptr, adapterIndex, &adapter, 0)) {
                return false;
            }

            if ((adapter.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) == 0) {
                continue;
            }

            if ((adapter.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == 0) {
                continue;
            }

            DEVMODEW mode{};
            mode.dmSize = sizeof(mode);

            if (!EnumDisplaySettingsExW(
                adapter.DeviceName,
                ENUM_CURRENT_SETTINGS,
                &mode,
                0)) {
                continue;
            }

            config.width = mode.dmPelsWidth;
            config.height = mode.dmPelsHeight;
            config.fps = mode.dmDisplayFrequency;

            return config.width > 0 &&
                config.height > 0 &&
                config.fps > 0;
        }
    }
    void SD_CALL OnEncodedFrame(
        const uint8_t* data,
        uint32_t size,
        int64_t timestamp,
        int32_t isKeyframe,
        void* userData)
    {
        if (data == nullptr || size == 0 || userData == nullptr) {
            return;
        }

        auto* ctx = static_cast<ViewerContext*>(userData);
        if (!ctx->running.load(std::memory_order_acquire)) {
            return;
        }

        EncodedPacket packet;
        packet.data.assign(data, data + size);
        packet.timestamp = timestamp;
        packet.is_keyframe = (isKeyframe != 0);

        {
            std::lock_guard<std::mutex> lock(ctx->packet_mutex);
            if (ctx->packets.size() >= kMaxEncodedQueueSize) {
                ctx->packets.clear();
                ctx->waiting_for_keyframe = true;
            }
            if (ctx->waiting_for_keyframe) {
                if (!packet.is_keyframe) {
                    return;
                }
                ctx->waiting_for_keyframe = false;
            }
            ctx->packets.push_back(std::move(packet));
        }
        ctx->packet_cv.notify_one();
    }

    void ConvertNv12ToBgra(
        const uint8_t* nv12,
        uint32_t width,
        uint32_t height,
        uint32_t stride,
        std::vector<uint8_t>& bgra)
    {
        bgra.resize(static_cast<size_t>(width) * height * 4);
        const uint8_t* yPlane = nv12;
        const uint8_t* uvPlane = nv12 + static_cast<size_t>(stride) * height;

        for (uint32_t row = 0; row < height; ++row) {
            const uint8_t* yRow = yPlane + static_cast<size_t>(row) * stride;
            const uint8_t* uvRow = uvPlane + static_cast<size_t>(row / 2) * stride;
            uint8_t* dstRow = bgra.data() + static_cast<size_t>(row) * width * 4;

            for (uint32_t col = 0; col < width; ++col) {
                const int y = std::max(0, static_cast<int>(yRow[col]) - 16);
                const size_t uvIndex = (col / 2) * 2;
                const int u = static_cast<int>(uvRow[uvIndex]) - 128;
                const int v = static_cast<int>(uvRow[uvIndex + 1]) - 128;

                const int c = 298 * y;
                const int r = (c + 409 * v + 128) >> 8;
                const int g = (c - 100 * u - 208 * v + 128) >> 8;
                const int b = (c + 516 * u + 128) >> 8;

                dstRow[col * 4 + 0] = static_cast<uint8_t>(std::clamp(b, 0, 255));
                dstRow[col * 4 + 1] = static_cast<uint8_t>(std::clamp(g, 0, 255));
                dstRow[col * 4 + 2] = static_cast<uint8_t>(std::clamp(r, 0, 255));
                dstRow[col * 4 + 3] = 0xFF;
            }
        }
    }

    class GpuNv12Converter {
    public:
        HRESULT convert(
            const uint8_t* nv12,
            uint32_t width,
            uint32_t height,
            uint32_t stride,
            HWND hwnd)
        {
            if (nv12 == nullptr || width == 0 || height == 0 ||
                (width & 1) != 0 || (height & 1) != 0 ||
                stride < width || hwnd == nullptr || !IsWindow(hwnd)) {
                return E_INVALIDARG;
            }
            if (disabled_) {
                return E_NOTIMPL;
            }

            if (width != width_ || height != height_) {
                Reset();
                HRESULT hr = Initialize(width, height, hwnd);
                if (FAILED(hr)) {
                    disabled_ = true;
                    OutputDebugStringW(
                        L"StreamViewer: GPU NV12 conversion unavailable; using CPU fallback.\n");
                    return hr;
                }
            }

            context_->UpdateSubresource(
                nv12_texture_.Get(),
                0,
                nullptr,
                nv12,
                stride,
                0);

            const float black[] = {0.0f, 0.0f, 0.0f, 1.0f};
            context_->ClearRenderTargetView(
                render_target_.Get(),
                black);
            RECT destinationRect{0, 0, 1920, 1080};
            const double sourceAspect =
                static_cast<double>(width) / height;
            const double targetAspect = 1920.0 / 1080.0;
            if (targetAspect > sourceAspect) {
                destinationRect.right = static_cast<LONG>(
                    1080.0 * sourceAspect);
                destinationRect.left =
                    (1920 - destinationRect.right) / 2;
            } else {
                destinationRect.bottom = static_cast<LONG>(
                    1920.0 / sourceAspect);
                destinationRect.top =
                    (1080 - destinationRect.bottom) / 2;
            }
            video_context_->VideoProcessorSetOutputTargetRect(
                processor_.Get(),
                TRUE,
                &destinationRect);

            D3D11_VIDEO_PROCESSOR_STREAM stream{};
            stream.Enable = TRUE;
            stream.pInputSurface = input_view_.Get();
            HRESULT hr = video_context_->VideoProcessorBlt(
                processor_.Get(),
                output_view_.Get(),
                0,
                1,
                &stream);
            if (FAILED(hr)) {
                Reset();
                disabled_ = true;
                OutputDebugStringW(
                    L"StreamViewer: GPU NV12 conversion failed; using CPU fallback.\n");
                return hr;
            }

            hr = swap_chain_->Present(1, 0);
            if (FAILED(hr)) {
                Reset();
                disabled_ = true;
                OutputDebugStringW(
                    L"StreamViewer: GPU NV12 conversion failed; using CPU fallback.\n");
                return hr;
            }

            return S_OK;
        }

    private:
        void Reset()
        {
            input_view_.Reset();
            output_view_.Reset();
            render_target_.Reset();
            nv12_texture_.Reset();
            output_texture_.Reset();
            swap_chain_.Reset();
            processor_.Reset();
            processor_enumerator_.Reset();
            video_context_.Reset();
            video_device_.Reset();
            context_.Reset();
            device_.Reset();
            width_ = 0;
            height_ = 0;
            disabled_ = false;
        }

        HRESULT Initialize(uint32_t width, uint32_t height, HWND hwnd)
        {
            HRESULT hr = D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_HARDWARE,
                nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT |
                    D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                nullptr,
                0,
                D3D11_SDK_VERSION,
                &device_,
                nullptr,
                &context_);
            if (FAILED(hr)) {
                return hr;
            }
            hr = device_.As(&video_device_);
            if (SUCCEEDED(hr)) {
                hr = context_.As(&video_context_);
            }

            D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDescription{};
            contentDescription.InputFrameFormat =
                D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
            contentDescription.InputFrameRate = {60, 1};
            contentDescription.InputWidth = width;
            contentDescription.InputHeight = height;
            contentDescription.OutputFrameRate = {60, 1};
            contentDescription.OutputWidth = 1920;
            contentDescription.OutputHeight = 1080;
            contentDescription.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
            if (SUCCEEDED(hr)) {
                hr = video_device_->CreateVideoProcessorEnumerator(
                    &contentDescription,
                    &processor_enumerator_);
            }

            UINT formatSupport = 0;
            if (SUCCEEDED(hr)) {
                hr = processor_enumerator_->CheckVideoProcessorFormat(
                    DXGI_FORMAT_NV12,
                    &formatSupport);
                if (SUCCEEDED(hr) &&
                    (formatSupport &
                        D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT) == 0) {
                    hr = DXGI_ERROR_UNSUPPORTED;
                }
            }
            if (SUCCEEDED(hr)) {
                hr = processor_enumerator_->CheckVideoProcessorFormat(
                    DXGI_FORMAT_B8G8R8A8_UNORM,
                    &formatSupport);
                if (SUCCEEDED(hr) &&
                    (formatSupport &
                        D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT) == 0) {
                    hr = DXGI_ERROR_UNSUPPORTED;
                }
            }
            if (SUCCEEDED(hr)) {
                hr = video_device_->CreateVideoProcessor(
                    processor_enumerator_.Get(),
                    0,
                    &processor_);
            }

            ComPtr<IDXGIDevice> dxgiDevice;
            ComPtr<IDXGIAdapter> adapter;
            ComPtr<IDXGIFactory2> factory;
            if (SUCCEEDED(hr)) {
                hr = device_.As(&dxgiDevice);
            }
            if (SUCCEEDED(hr)) {
                hr = dxgiDevice->GetAdapter(&adapter);
            }
            if (SUCCEEDED(hr)) {
                hr = adapter->GetParent(IID_PPV_ARGS(&factory));
            }
            DXGI_SWAP_CHAIN_DESC1 swapChainDescription{};
            swapChainDescription.Width = 1920;
            swapChainDescription.Height = 1080;
            swapChainDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            swapChainDescription.SampleDesc.Count = 1;
            swapChainDescription.BufferUsage =
                DXGI_USAGE_RENDER_TARGET_OUTPUT;
            swapChainDescription.BufferCount = 2;
            swapChainDescription.Scaling = DXGI_SCALING_STRETCH;
            swapChainDescription.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            swapChainDescription.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
            if (SUCCEEDED(hr)) {
                hr = factory->CreateSwapChainForHwnd(
                    device_.Get(),
                    hwnd,
                    &swapChainDescription,
                    nullptr,
                    nullptr,
                    &swap_chain_);
            }

            D3D11_TEXTURE2D_DESC inputDescription{};
            inputDescription.Width = width;
            inputDescription.Height = height;
            inputDescription.MipLevels = 1;
            inputDescription.ArraySize = 1;
            inputDescription.Format = DXGI_FORMAT_NV12;
            inputDescription.SampleDesc.Count = 1;
            inputDescription.Usage = D3D11_USAGE_DEFAULT;
            inputDescription.BindFlags = D3D11_BIND_DECODER;
            if (SUCCEEDED(hr)) {
                hr = device_->CreateTexture2D(
                    &inputDescription,
                    nullptr,
                    &nv12_texture_);
            }

            D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDescription{};
            inputViewDescription.ViewDimension =
                D3D11_VPIV_DIMENSION_TEXTURE2D;
            if (SUCCEEDED(hr)) {
                hr = video_device_->CreateVideoProcessorInputView(
                    nv12_texture_.Get(),
                    processor_enumerator_.Get(),
                    &inputViewDescription,
                    &input_view_);
            }

            if (SUCCEEDED(hr)) {
                hr = swap_chain_->GetBuffer(
                    0,
                    IID_PPV_ARGS(&output_texture_));
            }
            if (SUCCEEDED(hr)) {
                hr = device_->CreateRenderTargetView(
                    output_texture_.Get(),
                    nullptr,
                    &render_target_);
            }
            if (SUCCEEDED(hr)) {
                D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputViewDescription{};
                outputViewDescription.ViewDimension =
                    D3D11_VPOV_DIMENSION_TEXTURE2D;
                hr = video_device_->CreateVideoProcessorOutputView(
                    output_texture_.Get(),
                    processor_enumerator_.Get(),
                    &outputViewDescription,
                    &output_view_);
            }

            if (SUCCEEDED(hr)) {
                width_ = width;
                height_ = height;
                OutputDebugStringW(
                    L"StreamViewer: GPU NV12 swap-chain rendering enabled.\n");
            }
            return hr;
        }

        ComPtr<ID3D11Device> device_;
        ComPtr<ID3D11DeviceContext> context_;
        ComPtr<ID3D11VideoDevice> video_device_;
        ComPtr<ID3D11VideoContext> video_context_;
        ComPtr<ID3D11VideoProcessorEnumerator> processor_enumerator_;
        ComPtr<ID3D11VideoProcessor> processor_;
        ComPtr<ID3D11Texture2D> nv12_texture_;
        ComPtr<ID3D11VideoProcessorInputView> input_view_;
        ComPtr<ID3D11Texture2D> output_texture_;
        ComPtr<ID3D11VideoProcessorOutputView> output_view_;
        ComPtr<ID3D11RenderTargetView> render_target_;
        ComPtr<IDXGISwapChain1> swap_chain_;
        uint32_t width_ = 0;
        uint32_t height_ = 0;
        bool disabled_ = false;
    };

    HRESULT SelectDecoderNv12Output(IMFTransform* decoder)
    {
        for (DWORD index = 0;; ++index) {
            ComPtr<IMFMediaType> outputType;
            HRESULT hr = decoder->GetOutputAvailableType(0, index, &outputType);
            if (FAILED(hr)) {
                return hr;
            }

            GUID subtype{};
            if (SUCCEEDED(outputType->GetGUID(MF_MT_SUBTYPE, &subtype)) &&
                subtype == MFVideoFormat_NV12) {
                return decoder->SetOutputType(0, outputType.Get(), 0);
            }
        }
    }

    HRESULT CreateH264Decoder(
        uint32_t width,
        uint32_t height,
        uint32_t fps,
        ComPtr<IMFTransform>& decoder)
    {
        MFT_REGISTER_TYPE_INFO inputInfo{ MFMediaType_Video, MFVideoFormat_H264 };
        MFT_REGISTER_TYPE_INFO outputInfo{ MFMediaType_Video, MFVideoFormat_NV12 };

        IMFActivate** activations = nullptr;
        UINT32 count = 0;
        HRESULT hr = MFTEnumEx(
            MFT_CATEGORY_VIDEO_DECODER,
            MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT |
            MFT_ENUM_FLAG_HARDWARE | MFT_ENUM_FLAG_SORTANDFILTER,
            &inputInfo,
            &outputInfo,
            &activations,
            &count);
        if (FAILED(hr) || count == 0) {
            return FAILED(hr) ? hr : MF_E_TOPO_CODEC_NOT_FOUND;
        }

        for (UINT32 i = 0; i < count; ++i) {
            ComPtr<IMFTransform> candidate;
            hr = activations[i]->ActivateObject(IID_PPV_ARGS(&candidate));
            if (SUCCEEDED(hr)) {
                ComPtr<IMFAttributes> attrs;
                if (SUCCEEDED(candidate->GetAttributes(&attrs))) {
                    UINT32 async = FALSE;
                    if (SUCCEEDED(attrs->GetUINT32(MF_TRANSFORM_ASYNC, &async)) &&
                        async) {
                        // 동기 루프 단순화를 위해 비동기 전용 MFT는 건너뜀
                        candidate.Reset();
                        continue;
                    }
                    attrs->SetUINT32(CODECAPI_AVLowLatencyMode, TRUE);
                }

                ComPtr<IMFMediaType> inputType;
                hr = MFCreateMediaType(&inputType);
                if (SUCCEEDED(hr)) {
                    hr = inputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                }
                if (SUCCEEDED(hr)) {
                    hr = inputType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
                }
                if (SUCCEEDED(hr)) {
                    hr = inputType->SetUINT64(
                        MF_MT_FRAME_SIZE,
                        Pack2UINT32AsUINT64(width, height));
                }
                if (SUCCEEDED(hr)) {
                    hr = inputType->SetUINT64(
                        MF_MT_FRAME_RATE,
                        Pack2UINT32AsUINT64(fps, 1));
                }
                if (SUCCEEDED(hr)) {
                    hr = candidate->SetInputType(0, inputType.Get(), 0);
                }
                if (SUCCEEDED(hr)) {
                    hr = SelectDecoderNv12Output(candidate.Get());
                }
                if (SUCCEEDED(hr)) {
                    decoder = std::move(candidate);
                    break;
                }
            }
        }

        for (UINT32 i = 0; i < count; ++i) {
            activations[i]->Release();
        }
        CoTaskMemFree(activations);

        return decoder ? S_OK : MF_E_TOPO_CODEC_NOT_FOUND;
    }

    void DrainDecoderOutput(
        IMFTransform* decoder,
        ViewerContext& ctx,
        uint32_t& width,
        uint32_t& height,
        uint32_t& stride,
        GpuNv12Converter& gpuConverter)
    {
        MFT_OUTPUT_STREAM_INFO streamInfo{};
        if (FAILED(decoder->GetOutputStreamInfo(0, &streamInfo))) {
            return;
        }

        for (;;) {
            ComPtr<IMFSample> outSample;
            if ((streamInfo.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) == 0) {
                const DWORD bufSize = std::max<DWORD>(
                    streamInfo.cbSize,
                    static_cast<DWORD>(stride * height * 3 / 2));
                ComPtr<IMFMediaBuffer> outBuffer;
                if (FAILED(MFCreateMemoryBuffer(bufSize, &outBuffer)) ||
                    FAILED(MFCreateSample(&outSample)) ||
                    FAILED(outSample->AddBuffer(outBuffer.Get()))) {
                    return;
                }
            }

            MFT_OUTPUT_DATA_BUFFER output{};
            output.dwStreamID = 0;
            output.pSample = outSample.Get();
            DWORD status = 0;

            const HRESULT hr = decoder->ProcessOutput(0, 1, &output, &status);
            if (output.pEvents != nullptr) {
                output.pEvents->Release();
            }
            if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
                return;
            }
            if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
                if (FAILED(SelectDecoderNv12Output(decoder))) {
                    return;
                }
                ComPtr<IMFMediaType> currentType;
                if (SUCCEEDED(decoder->GetOutputCurrentType(0, &currentType))) {
                    UINT32 newW = 0;
                    UINT32 newH = 0;
                    if (SUCCEEDED(MFGetAttributeSize(
                        currentType.Get(),
                        MF_MT_FRAME_SIZE,
                        &newW,
                        &newH)) &&
                        newW > 0 && newH > 0) {
                        width = newW;
                        height = newH;
                    }
                    UINT32 defaultStride = 0;
                    if (SUCCEEDED(currentType->GetUINT32(
                        MF_MT_DEFAULT_STRIDE,
                        &defaultStride)) &&
                        defaultStride > 0) {
                        stride = defaultStride;
                    }
                    else {
                        stride = width;
                    }
                }
                decoder->GetOutputStreamInfo(0, &streamInfo);
                continue;
            }
            if (FAILED(hr)) {
                return;
            }

            if (output.pSample != nullptr &&
                output.pSample != outSample.Get()) {
                outSample.Reset();
                outSample.Attach(output.pSample);
            }

            if (outSample) {
                ComPtr<IMFMediaBuffer> buffer;
                if (SUCCEEDED(outSample->ConvertToContiguousBuffer(&buffer))) {
                    BYTE* nv12Bytes = nullptr;
                    DWORD nv12Len = 0;
                    if (SUCCEEDED(buffer->Lock(&nv12Bytes, nullptr, &nv12Len))) {
                        const size_t required =
                            static_cast<size_t>(stride) * height * 3 / 2;
                        if (nv12Len >= required) {
                            std::vector<uint8_t> converted;
                            HWND window =
                                ctx.hwnd.load(std::memory_order_acquire);
                            const HRESULT gpuResult = gpuConverter.convert(
                                nv12Bytes,
                                width,
                                height,
                                stride,
                                window);
                            if (SUCCEEDED(gpuResult)) {
                                ctx.gpu_displaying.store(
                                    true,
                                    std::memory_order_release);
                                std::lock_guard<std::mutex> lock(
                                    ctx.frame_mutex);
                                ctx.frame_width = width;
                                ctx.frame_height = height;
                                ++ctx.decoded_frames;
                            } else {
                                ctx.gpu_displaying.store(
                                    false,
                                    std::memory_order_release);
                                ConvertNv12ToBgra(
                                    nv12Bytes,
                                    width,
                                    height,
                                    stride,
                                    converted);
                                {
                                    std::lock_guard<std::mutex> lock(
                                        ctx.frame_mutex);
                                    ctx.bgra_frame = std::move(converted);
                                    ctx.frame_width = width;
                                    ctx.frame_height = height;
                                    ++ctx.decoded_frames;
                                }
                                if (window != nullptr) {
                                    InvalidateRect(window, nullptr, FALSE);
                                }
                            }
                        }
                        buffer->Unlock();
                    }
                }
            }

            if ((output.dwStatus & MFT_OUTPUT_DATA_BUFFER_INCOMPLETE) == 0) {
                return;
            }
        }
    }

    void DecoderThreadMain(
        ViewerContext* ctx,
        uint32_t initialWidth,
        uint32_t initialHeight,
        uint32_t fps)
    {
        const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_FULL))) {
            if (SUCCEEDED(comHr)) {
                CoUninitialize();
            }
            return;
        }

        ComPtr<IMFTransform> decoder;
        if (FAILED(CreateH264Decoder(initialWidth, initialHeight, fps, decoder))) {
            std::cerr << "Failed to initialize Media Foundation H.264 decoder.\n";
            MFShutdown();
            if (SUCCEEDED(comHr)) {
                CoUninitialize();
            }
            return;
        }

        decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
        decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);

        uint32_t width = initialWidth;
        uint32_t height = initialHeight;
        uint32_t stride = initialWidth;
        GpuNv12Converter gpuConverter;

        while (ctx->running.load(std::memory_order_acquire)) {
            EncodedPacket packet;
            {
                std::unique_lock<std::mutex> lock(ctx->packet_mutex);
                ctx->packet_cv.wait(lock, [ctx] {
                    return !ctx->running.load(std::memory_order_acquire) ||
                        !ctx->packets.empty();
                    });
                if (!ctx->running.load(std::memory_order_acquire)) {
                    break;
                }
                packet = std::move(ctx->packets.front());
                ctx->packets.pop_front();
            }

            ComPtr<IMFMediaBuffer> inBuffer;
            if (FAILED(MFCreateMemoryBuffer(
                static_cast<DWORD>(packet.data.size()),
                &inBuffer))) {
                continue;
            }

            BYTE* dst = nullptr;
            if (SUCCEEDED(inBuffer->Lock(&dst, nullptr, nullptr))) {
                std::memcpy(dst, packet.data.data(), packet.data.size());
                inBuffer->Unlock();
                inBuffer->SetCurrentLength(static_cast<DWORD>(packet.data.size()));
            }

            ComPtr<IMFSample> inSample;
            if (FAILED(MFCreateSample(&inSample)) ||
                FAILED(inSample->AddBuffer(inBuffer.Get()))) {
                continue;
            }
            inSample->SetSampleTime(packet.timestamp);

            HRESULT hr = decoder->ProcessInput(0, inSample.Get(), 0);
            if (hr == MF_E_NOTACCEPTING) {
                DrainDecoderOutput(
                    decoder.Get(),
                    *ctx,
                    width,
                    height,
                    stride,
                    gpuConverter);
                hr = decoder->ProcessInput(0, inSample.Get(), 0);
            }
            if (SUCCEEDED(hr)) {
                DrainDecoderOutput(
                    decoder.Get(),
                    *ctx,
                    width,
                    height,
                    stride,
                    gpuConverter);
            }
        }

        decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
        decoder->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0);
        decoder->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
        decoder.Reset();

        MFShutdown();
        if (SUCCEEDED(comHr)) {
            CoUninitialize();
        }
    }

    void PaintFrame(HWND hwnd, ViewerContext& ctx)
    {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);

        if (ctx.gpu_displaying.load(std::memory_order_acquire)) {
            EndPaint(hwnd, &ps);
            return;
        }

        RECT client{};
        GetClientRect(hwnd, &client);

        const int clientWidth = client.right - client.left;
        const int clientHeight = client.bottom - client.top;

        if (clientWidth <= 0 || clientHeight <= 0) {
            EndPaint(hwnd, &ps);
            return;
        }

        FillRect(hdc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

        std::lock_guard<std::mutex> lock(ctx.frame_mutex);

        if (ctx.bgra_frame.empty() ||
            ctx.frame_width == 0 ||
            ctx.frame_height == 0) {
            EndPaint(hwnd, &ps);
            return;
        }

        const double srcAspect =
            static_cast<double>(ctx.frame_width) / ctx.frame_height;

        const double dstAspect =
            static_cast<double>(clientWidth) / clientHeight;

        int drawWidth = clientWidth;
        int drawHeight = clientHeight;
        int drawX = 0;
        int drawY = 0;

        if (dstAspect > srcAspect) {
            drawWidth = static_cast<int>(clientHeight * srcAspect);
            drawX = (clientWidth - drawWidth) / 2;
        }
        else {
            drawHeight = static_cast<int>(clientWidth / srcAspect);
            drawY = (clientHeight - drawHeight) / 2;
        }

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = static_cast<LONG>(ctx.frame_width);
        bmi.bmiHeader.biHeight = -static_cast<LONG>(ctx.frame_height);
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        SetStretchBltMode(hdc, COLORONCOLOR);

        StretchDIBits(
            hdc,
            drawX,
            drawY,
            drawWidth,
            drawHeight,
            0,
            0,
            static_cast<int>(ctx.frame_width),
            static_cast<int>(ctx.frame_height),
            ctx.bgra_frame.data(),
            &bmi,
            DIB_RGB_COLORS,
            SRCCOPY);

        EndPaint(hwnd, &ps);
    }

    LRESULT CALLBACK WindowProc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam)
    {
        auto* ctx = reinterpret_cast<ViewerContext*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (message) {
        case WM_NCCREATE: {
            const auto* createStruct =
                reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(
                hwnd,
                GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
            return TRUE;
        }
        case WM_ERASEBKGND:
            return 1; // 깜빡임 방지
        case WM_PAINT:
            if (ctx != nullptr) {
                PaintFrame(hwnd, *ctx);
                return 0;
            }
            break;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_DESTROY:
            if (ctx != nullptr) {
                ctx->running.store(false, std::memory_order_release);
                ctx->packet_cv.notify_all();
            }
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

} // namespace

int wmain()
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    CoreApi api{};
    if (!LoadCoreApi(api)) {
        std::cerr << "Failed to load SlimeDisplay-windows-core.dll exports.\n";
        return 1;
    }
    if (api.get_version() != 0) {
        std::cerr << "Unexpected DLL ABI version.\n";
        FreeLibrary(api.module);
        return 1;
    }

    SD_DisplayConfig config{};
    if (!FindVddMode(config)) {
        std::cerr << "No active Virtual Display Driver monitor found.\n";
        FreeLibrary(api.module);
        return 2;
    }

    SD_Handle* handle = nullptr;
    if (api.create(&handle) != SD_SUCCESS || handle == nullptr) {
        std::cerr << "sd_create failed.\n";
        FreeLibrary(api.module);
        return 1;
    }

    if (api.create_display(handle, &config) != SD_SUCCESS) {
        std::cerr << "sd_create_virtual_display failed.\n";
        api.destroy(handle);
        FreeLibrary(api.module);
        return 1;
    }

    SD_DisplayInfo info{};
    if (api.get_display_info(handle, &info) != SD_SUCCESS) {
        std::cerr << "sd_get_virtual_display_info failed.\n";
        api.destroy_display(handle);
        api.destroy(handle);
        FreeLibrary(api.module);
        return 1;
    }

    ViewerContext ctx;
    ctx.running.store(true, std::memory_order_release);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClassName;
    RegisterClassExW(&wc);

    const std::wstring windowTitle =
        L"SlimeDisplay Stream Viewer (" +
        std::to_wstring(info.width) + L"x" +
        std::to_wstring(info.height) + L" @ " +
        std::to_wstring(info.refresh_rate) + L"Hz) - ESC to Quit";

    HWND hwnd = CreateWindowExW(
        0,
        kWindowClassName,
        windowTitle.c_str(),
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1920,
        1080,
        nullptr,
        nullptr,
        wc.hInstance,
        &ctx);
    if (hwnd == nullptr) {
        api.destroy_display(handle);
        api.destroy(handle);
        FreeLibrary(api.module);
        return 1;
    }
    ctx.hwnd.store(hwnd, std::memory_order_release);

    std::thread decoderThread(
        DecoderThreadMain,
        &ctx,
        info.width,
        info.height,
        info.refresh_rate);

    api.set_callback(handle, OnEncodedFrame, &ctx);
    if (api.start_encoder(handle) != SD_SUCCESS ||
        api.start_capture(handle) != SD_SUCCESS) {
        std::cerr << "Failed to start encoder or capture.\n";
        ctx.running.store(false, std::memory_order_release);
        ctx.packet_cv.notify_all();
        decoderThread.join();
        DestroyWindow(hwnd);
        api.stop_capture(handle);
        api.stop_encoder(handle);
        api.destroy_display(handle);
        api.destroy(handle);
        FreeLibrary(api.module);
        return 1;
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    ctx.hwnd.store(nullptr, std::memory_order_release);
    ctx.running.store(false, std::memory_order_release);
    ctx.packet_cv.notify_all();

    api.stop_capture(handle);
    api.stop_encoder(handle);
    api.set_callback(handle, nullptr, nullptr);

    if (decoderThread.joinable()) {
        decoderThread.join();
    }

    api.destroy_display(handle);
    api.destroy(handle);
    FreeLibrary(api.module);
    return 0;
}