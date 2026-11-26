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
                D3D11_CREATE_DEVICE_BGRA_SUPPORT,
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
            return resources.device->CreateTexture2D(
                &textureDescription,
                nullptr,
                &resources.staging_texture);
        }
    }

    return DXGI_ERROR_NOT_FOUND;
}

HRESULT CaptureDxgiFrame(
    DxgiCaptureResources& resources,
    uint32_t width,
    uint32_t height,
    std::vector<uint8_t>& pixels)
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
    if (SUCCEEDED(hr)) {
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
        SUCCEEDED(CreateDxgiCapture(deviceName, width, height, dxgi));
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
    for (size_t alpha = 3; alpha < pixels.size(); alpha += 4) {
        pixels[alpha] = 0xff;
    }

    while (running_) {
        if (useDxgi) {
            const HRESULT result =
                CaptureDxgiFrame(dxgi, width, height, pixels);
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

        for (size_t alpha = 3; alpha < pixels.size(); alpha += 4) {
            pixels[alpha] = 0xff;
        }

        const auto elapsed =
            std::chrono::steady_clock::now() - startTime;
        const int64_t presentationTime =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                elapsed).count() / 100;
        try {
            callback(pixels, presentationTime);
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
