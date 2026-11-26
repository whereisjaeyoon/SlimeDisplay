#include "CaptureManager.h"

#include <windows.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <chrono>
#include <cstring>
#include <limits>
#include <system_error>
#include <utility>

namespace {

using Microsoft::WRL::ComPtr;

struct MonitorSearch {
    const std::wstring* device_name;
    RECT bounds;
    bool found;
};

struct DxgiCaptureResources {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIOutputDuplication> duplication;
    ComPtr<ID3D11Texture2D> staging_texture;
    ComPtr<ID3D11VideoDevice> video_device;
    ComPtr<ID3D11VideoContext> video_context;
    ComPtr<ID3D11VideoProcessorEnumerator> processor_enumerator;
    ComPtr<ID3D11VideoProcessor> processor;
    ComPtr<ID3D11Texture2D> nv12_texture;
    ComPtr<ID3D11Texture2D> nv12_staging_texture;
    ComPtr<ID3D11VideoProcessorOutputView> nv12_output_view;
    bool nv12_enabled = false;
};

BOOL CALLBACK FindMonitor(
    HMONITOR monitor,
    HDC,
    LPRECT,
    LPARAM data)
{
    auto* search = reinterpret_cast<MonitorSearch*>(data);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, &info)) {
        return TRUE;
    }

    if (_wcsicmp(info.szDevice, search->device_name->c_str()) == 0) {
        search->bounds = info.rcMonitor;
        search->found = true;
        return FALSE;
    }

    return TRUE;
}

HRESULT CreateDxgiCapture(
    const std::wstring& deviceName,
    uint32_t width,
    uint32_t height,
    uint32_t fps,
    DxgiCaptureResources& resources)
{
    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        return hr;
    }

    for (UINT adapterIndex = 0;; ++adapterIndex) {
        ComPtr<IDXGIAdapter1> adapter;
        hr = factory->EnumAdapters1(adapterIndex, &adapter);
        if (hr == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (FAILED(hr)) {
            return hr;
        }

        for (UINT outputIndex = 0;; ++outputIndex) {
            ComPtr<IDXGIOutput> output;
            hr = adapter->EnumOutputs(outputIndex, &output);
            if (hr == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            if (FAILED(hr)) {
                return hr;
            }

            DXGI_OUTPUT_DESC outputDescription{};
            hr = output->GetDesc(&outputDescription);
            if (FAILED(hr) ||
                _wcsicmp(outputDescription.DeviceName, deviceName.c_str()) != 0) {
                continue;
            }

            ComPtr<IDXGIOutput1> output1;
            hr = output.As(&output1);
            if (FAILED(hr)) {
                return hr;
            }
            hr = D3D11CreateDevice(
                adapter.Get(),
                D3D_DRIVER_TYPE_UNKNOWN,
                nullptr,
                D3D11_CREATE_DEVICE_BGRA_SUPPORT |
                    D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
                nullptr,
                0,
                D3D11_SDK_VERSION,
                &resources.device,
                nullptr,
                &resources.context);
            if (FAILED(hr)) {
                return hr;
            }
            hr = output1->DuplicateOutput(
                resources.device.Get(),
                &resources.duplication);
            if (FAILED(hr)) {
                return hr;
            }

            DXGI_OUTDUPL_DESC duplicationDescription{};
            resources.duplication->GetDesc(&duplicationDescription);
            if (duplicationDescription.ModeDesc.Width != width ||
                duplicationDescription.ModeDesc.Height != height ||
                duplicationDescription.ModeDesc.Format !=
                    DXGI_FORMAT_B8G8R8A8_UNORM) {
                return DXGI_ERROR_UNSUPPORTED;
            }

            D3D11_TEXTURE2D_DESC textureDescription{};
            textureDescription.Width = width;
            textureDescription.Height = height;
            textureDescription.MipLevels = 1;
            textureDescription.ArraySize = 1;
            textureDescription.Format = duplicationDescription.ModeDesc.Format;
            textureDescription.SampleDesc.Count = 1;
            textureDescription.Usage = D3D11_USAGE_STAGING;
            textureDescription.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            hr = resources.device->CreateTexture2D(
                &textureDescription,
                nullptr,
                &resources.staging_texture);
            if (FAILED(hr)) {
                return hr;
            }

            if ((width & 1) == 0 && (height & 1) == 0) {
                HRESULT videoHr = resources.device.As(
                    &resources.video_device);
                if (SUCCEEDED(videoHr)) {
                    videoHr = resources.context.As(
                        &resources.video_context);
                }

                if (SUCCEEDED(videoHr)) {
                    D3D11_VIDEO_PROCESSOR_CONTENT_DESC contentDescription{};
                    contentDescription.InputFrameFormat =
                        D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
                    contentDescription.InputFrameRate = {fps, 1};
                    contentDescription.InputWidth = width;
                    contentDescription.InputHeight = height;
                    contentDescription.OutputFrameRate = {fps, 1};
                    contentDescription.OutputWidth = width;
                    contentDescription.OutputHeight = height;
                    contentDescription.Usage =
                        D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
                    videoHr =
                        resources.video_device->CreateVideoProcessorEnumerator(
                            &contentDescription,
                            &resources.processor_enumerator);
                }
                UINT formatSupport = 0;
                if (SUCCEEDED(videoHr)) {
                    videoHr =
                        resources.processor_enumerator->CheckVideoProcessorFormat(
                            duplicationDescription.ModeDesc.Format,
                            &formatSupport);
                    if (SUCCEEDED(videoHr) &&
                        (formatSupport &
                            D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT) == 0) {
                        videoHr = DXGI_ERROR_UNSUPPORTED;
                    }
                }
                if (SUCCEEDED(videoHr)) {
                    videoHr =
                        resources.processor_enumerator->CheckVideoProcessorFormat(
                            DXGI_FORMAT_NV12,
                            &formatSupport);
                    if (SUCCEEDED(videoHr) &&
                        (formatSupport &
                            D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT) == 0) {
                        videoHr = DXGI_ERROR_UNSUPPORTED;
                    }
                }
                if (SUCCEEDED(videoHr)) {
                    videoHr = resources.video_device->CreateVideoProcessor(
                        resources.processor_enumerator.Get(),
                        0,
                        &resources.processor);
                }

                D3D11_TEXTURE2D_DESC nv12Description{};
                nv12Description.Width = width;
                nv12Description.Height = height;
                nv12Description.MipLevels = 1;
                nv12Description.ArraySize = 1;
                nv12Description.Format = DXGI_FORMAT_NV12;
                nv12Description.SampleDesc.Count = 1;
                nv12Description.Usage = D3D11_USAGE_DEFAULT;
                nv12Description.BindFlags = D3D11_BIND_RENDER_TARGET;
                if (SUCCEEDED(videoHr)) {
                    videoHr = resources.device->CreateTexture2D(
                        &nv12Description,
                        nullptr,
                        &resources.nv12_texture);
                }
                if (SUCCEEDED(videoHr)) {
                    nv12Description.Usage = D3D11_USAGE_STAGING;
                    nv12Description.BindFlags = 0;
                    nv12Description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                    videoHr = resources.device->CreateTexture2D(
                        &nv12Description,
                        nullptr,
                        &resources.nv12_staging_texture);
                }
                if (SUCCEEDED(videoHr)) {
                    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC outputViewDescription{};
                    outputViewDescription.ViewDimension =
                        D3D11_VPOV_DIMENSION_TEXTURE2D;
                    videoHr =
                        resources.video_device->CreateVideoProcessorOutputView(
                            resources.nv12_texture.Get(),
                            resources.processor_enumerator.Get(),
                            &outputViewDescription,
                            &resources.nv12_output_view);
                }
                resources.nv12_enabled = SUCCEEDED(videoHr);
            }

            return S_OK;
        }
    }

    return DXGI_ERROR_NOT_FOUND;
}

HRESULT CaptureDxgiFrame(
    DxgiCaptureResources& resources,
    uint32_t width,
    uint32_t height,
    std::vector<uint8_t>& pixels,
    bool& isNv12)
{
    DXGI_OUTDUPL_FRAME_INFO frameInfo{};
    ComPtr<IDXGIResource> desktopResource;
    HRESULT hr = resources.duplication->AcquireNextFrame(
        0,
        &frameInfo,
        &desktopResource);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        return S_FALSE;
    }
    if (FAILED(hr)) {
        return hr;
    }

    ComPtr<ID3D11Texture2D> desktopTexture;
    hr = desktopResource.As(&desktopTexture);
    if (SUCCEEDED(hr) && resources.nv12_enabled) {
        ComPtr<ID3D11VideoProcessorInputView> inputView;
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC inputViewDescription{};
        inputViewDescription.ViewDimension =
            D3D11_VPIV_DIMENSION_TEXTURE2D;
        hr = resources.video_device->CreateVideoProcessorInputView(
            desktopTexture.Get(),
            resources.processor_enumerator.Get(),
            &inputViewDescription,
            &inputView);
        if (SUCCEEDED(hr)) {
            D3D11_VIDEO_PROCESSOR_STREAM stream{};
            stream.Enable = TRUE;
            stream.pInputSurface = inputView.Get();
            hr = resources.video_context->VideoProcessorBlt(
                resources.processor.Get(),
                resources.nv12_output_view.Get(),
                0,
                1,
                &stream);
        }
        if (SUCCEEDED(hr)) {
            resources.context->CopyResource(
                resources.nv12_staging_texture.Get(),
                resources.nv12_texture.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            hr = resources.context->Map(
                resources.nv12_staging_texture.Get(),
                0,
                D3D11_MAP_READ,
                0,
                &mapped);
            if (SUCCEEDED(hr)) {
                const size_t rowBytes = width;
                const size_t ySize = static_cast<size_t>(width) * height;
                pixels.resize(ySize + ySize / 2);
                const auto* source =
                    static_cast<const uint8_t*>(mapped.pData);
                for (uint32_t row = 0; row < height; ++row) {
                    std::memcpy(
                        pixels.data() + static_cast<size_t>(row) * rowBytes,
                        source + static_cast<size_t>(row) * mapped.RowPitch,
                        rowBytes);
                }
                const uint8_t* sourceUv =
                    source + static_cast<size_t>(mapped.RowPitch) * height;
                uint8_t* destinationUv = pixels.data() + ySize;
                for (uint32_t row = 0; row < height / 2; ++row) {
                    std::memcpy(
                        destinationUv + static_cast<size_t>(row) * rowBytes,
                        sourceUv + static_cast<size_t>(row) * mapped.RowPitch,
                        rowBytes);
                }
                resources.context->Unmap(
                    resources.nv12_staging_texture.Get(),
                    0);
                isNv12 = true;
            }
        }
    }

    if (FAILED(hr) && resources.nv12_enabled && desktopTexture != nullptr) {
        resources.nv12_enabled = false;
        hr = S_OK;
    }

    if (SUCCEEDED(hr) && desktopTexture != nullptr && !isNv12) {
        pixels.resize(static_cast<size_t>(width) * height * 4);
        resources.context->CopyResource(
            resources.staging_texture.Get(),
            desktopTexture.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        hr = resources.context->Map(
            resources.staging_texture.Get(),
            0,
            D3D11_MAP_READ,
            0,
            &mapped);
        if (SUCCEEDED(hr)) {
            const size_t rowBytes = static_cast<size_t>(width) * 4;
            for (uint32_t row = 0; row < height; ++row) {
                std::memcpy(
                    pixels.data() + static_cast<size_t>(row) * rowBytes,
                    static_cast<const uint8_t*>(mapped.pData) +
                        static_cast<size_t>(row) * mapped.RowPitch,
                    rowBytes);
            }
            resources.context->Unmap(
                resources.staging_texture.Get(),
                0);
            isNv12 = false;
        }
    }

    const HRESULT releaseResult = resources.duplication->ReleaseFrame();
    return FAILED(hr) ? hr : releaseResult;
}

class CaptureResources {
public:
    ~CaptureResources()
    {
        if (memory_dc != nullptr && previous_bitmap != nullptr) {
            SelectObject(memory_dc, previous_bitmap);
        }
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        if (memory_dc != nullptr) {
            DeleteDC(memory_dc);
        }
        if (screen_dc != nullptr) {
            ReleaseDC(nullptr, screen_dc);
        }
    }

    HDC screen_dc = nullptr;
    HDC memory_dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous_bitmap = nullptr;
    void* bits = nullptr;
};

} // namespace

CaptureManager::~CaptureManager()
{
    stop();
}

SD_Result CaptureManager::start(
    const std::wstring& deviceName,
    uint32_t width,
    uint32_t height,
    uint32_t fps,
    FrameCallback callback)
{
    if (deviceName.empty() ||
        width == 0 ||
        height == 0 ||
        fps == 0 ||
        !callback ||
        static_cast<uint64_t>(width) * height >
            std::numeric_limits<size_t>::max() / 4) {
        return SD_ERROR_INVALID_ARGUMENT;
    }

    if (running_) {
        return SD_ERROR_INVALID_STATE;
    }
    if (worker_.joinable()) {
        worker_.join();
    }

    std::vector<uint8_t> pixels(
        static_cast<size_t>(width) * height * 4);
    {
        std::lock_guard<std::mutex> lock(startup_mutex_);
        startup_complete_ = false;
        startup_result_ = SD_ERROR_INITIALIZATION;
    }
    last_result_ = SD_SUCCESS;
    running_ = true;

    try {
        worker_ = std::thread(
            &CaptureManager::run,
            this,
            deviceName,
            width,
            height,
            fps,
            std::move(pixels),
            std::move(callback));
    } catch (const std::system_error&) {
        running_ = false;
        return SD_ERROR_INITIALIZATION;
    } catch (...) {
        running_ = false;
        throw;
    }

    std::unique_lock<std::mutex> lock(startup_mutex_);
    startup_condition_.wait(lock, [this] { return startup_complete_; });
    const SD_Result result = startup_result_;
    lock.unlock();

    if (result != SD_SUCCESS && worker_.joinable()) {
        worker_.join();
    }
    return result;
}

SD_Result CaptureManager::stop()
{
    running_ = false;
    if (worker_.joinable() &&
        worker_.get_id() != std::this_thread::get_id()) {
        worker_.join();
    }
    return last_result_;
}

void CaptureManager::signal_start(SD_Result result)
{
    if (result != SD_SUCCESS) {
        last_result_ = result;
    }
    {
        std::lock_guard<std::mutex> lock(startup_mutex_);
        startup_result_ = result;
        startup_complete_ = true;
    }
    startup_condition_.notify_one();
}

void CaptureManager::run(
    std::wstring deviceName,
    uint32_t width,
    uint32_t height,
    uint32_t fps,
    std::vector<uint8_t> pixels,
    FrameCallback callback)
{
    MonitorSearch search{&deviceName, {}, false};
    EnumDisplayMonitors(
        nullptr,
        nullptr,
        FindMonitor,
        reinterpret_cast<LPARAM>(&search));
    DxgiCaptureResources dxgi;
    const bool useDxgi =
        SUCCEEDED(CreateDxgiCapture(deviceName, width, height, fps, dxgi));
    if (useDxgi) {
        OutputDebugStringW(
            dxgi.nv12_enabled
                ? L"CaptureManager: GPU NV12 conversion enabled.\n"
                : L"CaptureManager: using BGRA CPU fallback.\n");
    }
    CaptureResources resources;
    if (!useDxgi) {
        if (!search.found ||
            static_cast<uint64_t>(search.bounds.right - search.bounds.left) != width ||
            static_cast<uint64_t>(search.bounds.bottom - search.bounds.top) != height) {
            running_ = false;
            signal_start(SD_ERROR_INVALID_STATE);
            return;
        }

        resources.screen_dc = GetDC(nullptr);
        if (resources.screen_dc != nullptr) {
            resources.memory_dc = CreateCompatibleDC(resources.screen_dc);
        }

        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader);
        bitmapInfo.bmiHeader.biWidth = static_cast<LONG>(width);
        bitmapInfo.bmiHeader.biHeight = -static_cast<LONG>(height);
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;

        if (resources.memory_dc != nullptr) {
            resources.bitmap = CreateDIBSection(
                resources.screen_dc,
                &bitmapInfo,
                DIB_RGB_COLORS,
                &resources.bits,
                nullptr,
                0);
        }
        if (resources.bitmap != nullptr) {
            resources.previous_bitmap =
                SelectObject(resources.memory_dc, resources.bitmap);
        }
        if (resources.screen_dc == nullptr ||
            resources.memory_dc == nullptr ||
            resources.bitmap == nullptr ||
            resources.bits == nullptr ||
            resources.previous_bitmap == nullptr ||
            resources.previous_bitmap == HGDI_ERROR) {
            running_ = false;
            signal_start(SD_ERROR_INITIALIZATION);
            return;
        }
    }

    signal_start(SD_SUCCESS);

    const auto startTime = std::chrono::steady_clock::now();
    const auto frameInterval = std::chrono::nanoseconds(
        1000000000ULL / fps);
    auto nextFrame = startTime;
    while (running_) {
        bool isNv12 = false;
        if (useDxgi) {
            const HRESULT result =
                CaptureDxgiFrame(dxgi, width, height, pixels, isNv12);
            if (result == S_FALSE) {
                std::this_thread::sleep_for(frameInterval);
                continue;
            }
            if (FAILED(result)) {
                OutputDebugStringW(
                    L"CaptureManager: DXGI Desktop Duplication failed.\n");
                last_result_ = SD_ERROR_CAPTURE;
                running_ = false;
                break;
            }
        } else {
            if (!BitBlt(
                    resources.memory_dc,
                    0,
                    0,
                    static_cast<int>(width),
                    static_cast<int>(height),
                    resources.screen_dc,
                    search.bounds.left,
                    search.bounds.top,
                    SRCCOPY | CAPTUREBLT)) {
                OutputDebugStringW(L"CaptureManager: BitBlt failed.\n");
                last_result_ = SD_ERROR_CAPTURE;
                running_ = false;
                break;
            }

            GdiFlush();
            std::memcpy(
                pixels.data(),
                resources.bits,
                pixels.size());
        }

        if (!isNv12) {
            for (size_t alpha = 3; alpha < pixels.size(); alpha += 4) {
                pixels[alpha] = 0xff;
            }
        }

        const auto elapsed =
            std::chrono::steady_clock::now() - startTime;
        const int64_t presentationTime =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                elapsed).count() / 100;
        try {
            callback(pixels, isNv12, presentationTime);
        } catch (...) {
            OutputDebugStringW(
                L"CaptureManager: frame callback threw an exception.\n");
            last_result_ = SD_ERROR_INTERNAL;
            running_ = false;
            break;
        }

        nextFrame += frameInterval;
        const auto now = std::chrono::steady_clock::now();
        if (nextFrame < now) {
            nextFrame = now;
        }
        std::this_thread::sleep_until(nextFrame);
    }
}
