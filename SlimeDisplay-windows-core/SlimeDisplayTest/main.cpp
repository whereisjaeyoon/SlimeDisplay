#include "slimedisplay.h"

#include <windows.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <mutex>

namespace {

struct TestState {
    std::mutex mutex;
    std::condition_variable condition;
    uint32_t frame_count = 0;
};

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

void SD_CALL OnEncodedFrame(
    const uint8_t* data,
    uint32_t size,
    int64_t,
    int32_t,
    void* userData)
{
    if (data == nullptr || size == 0 || userData == nullptr) {
        return;
    }

    auto* state = static_cast<TestState*>(userData);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        ++state->frame_count;
    }
    state->condition.notify_one();
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

template<typename Function>
Function LoadFunction(HMODULE module, const char* name)
{
    return reinterpret_cast<Function>(GetProcAddress(module, name));
}

} // namespace

int wmain()
{
#ifdef _DEBUG
    constexpr wchar_t configuration[] = L"Debug";
#else
    constexpr wchar_t configuration[] = L"Release";
#endif
    wchar_t executablePath[MAX_PATH]{};
    const DWORD pathLength = GetModuleFileNameW(
        nullptr,
        executablePath,
        MAX_PATH);
    if (pathLength == 0 || pathLength == MAX_PATH) {
        std::cerr << "Unable to resolve the test executable path.\n";
        return 1;
    }

    const std::filesystem::path dllPath =
        std::filesystem::path(executablePath)
            .parent_path()
            .parent_path()
            .parent_path()
            .parent_path() /
        L"SlimeDisplay-windows-core" / L"x64" / configuration /
        L"SlimeDisplay-windows-core.dll";
    HMODULE module = LoadLibraryW(dllPath.c_str());
    if (module == nullptr) {
        std::wcerr << L"Unable to load " << dllPath.c_str() << L".\n";
        return 1;
    }

    const auto create = LoadFunction<CreateFunction>(module, "sd_create");
    const auto destroy = LoadFunction<DestroyFunction>(module, "sd_destroy");
    const auto createDisplay =
        LoadFunction<CreateDisplayFunction>(module, "sd_create_virtual_display");
    const auto destroyDisplay =
        LoadFunction<DestroyDisplayFunction>(module, "sd_destroy_virtual_display");
    const auto getDisplayInfo =
        LoadFunction<GetDisplayInfoFunction>(
            module,
            "sd_get_virtual_display_info");
    const auto startCapture =
        LoadFunction<StartFunction>(module, "sd_start_capture");
    const auto stopCapture =
        LoadFunction<StopFunction>(module, "sd_stop_capture");
    const auto startEncoder =
        LoadFunction<StartFunction>(module, "sd_start_encoder");
    const auto stopEncoder =
        LoadFunction<StopFunction>(module, "sd_stop_encoder");
    const auto getVersion =
        LoadFunction<GetVersionFunction>(module, "sd_get_version");
    const auto setCallback =
        LoadFunction<SetCallbackFunction>(
            module,
            "sd_set_encoded_frame_callback");
    if (create == nullptr || destroy == nullptr ||
        createDisplay == nullptr || destroyDisplay == nullptr ||
        getDisplayInfo == nullptr ||
        startCapture == nullptr || stopCapture == nullptr ||
        startEncoder == nullptr || stopEncoder == nullptr ||
        getVersion == nullptr || setCallback == nullptr) {
        std::cerr << "The DLL is missing one or more C ABI exports.\n";
        FreeLibrary(module);
        return 1;
    }
    if (getVersion() != 0) {
        std::cerr << "sd_get_version returned an unexpected version.\n";
        FreeLibrary(module);
        return 1;
    }

    SD_Handle* handle = nullptr;
    if (create(&handle) != SD_SUCCESS || handle == nullptr) {
        std::cerr << "sd_create failed.\n";
        FreeLibrary(module);
        return 1;
    }

    SD_DisplayConfig config{};
    if (!FindVddMode(config)) {
        std::cerr << "No active Virtual Display Driver mode was found.\n";
        destroy(handle);
        FreeLibrary(module);
        return 2;
    }

    bool displayCreated = false;
    bool encoderStarted = false;
    bool captureStarted = false;
    bool testPassed = true;
    uint32_t framesDuringCapture = 0;
    SD_Result result = createDisplay(handle, &config);
    if (result != SD_SUCCESS) {
        std::cerr << "sd_create_virtual_display failed: " << result << ".\n";
        testPassed = false;
    } else {
        displayCreated = true;
    }

    TestState state;
    if (displayCreated) {
        SD_DisplayInfo info{};
        result = getDisplayInfo(handle, &info);
        if (result != SD_SUCCESS ||
            info.width != config.width ||
            info.height != config.height ||
            info.refresh_rate != config.fps) {
            std::cerr << "sd_get_virtual_display_info returned an unexpected mode.\n";
            testPassed = false;
            displayCreated = false;
            destroyDisplay(handle);
        }
    }

    if (displayCreated) {
        result = setCallback(handle, OnEncodedFrame, &state);
        if (result != SD_SUCCESS) {
            std::cerr << "sd_set_encoded_frame_callback failed: "
                      << result << ".\n";
            testPassed = false;
        }
    }
    if (displayCreated && result == SD_SUCCESS) {
        result = startEncoder(handle);
        if (result == SD_SUCCESS) {
            encoderStarted = true;
        } else {
            std::cerr << "sd_start_encoder failed: " << result << ".\n";
            testPassed = false;
        }
        if (encoderStarted) {
            result = startCapture(handle);
            if (result == SD_SUCCESS) {
                captureStarted = true;
                std::unique_lock<std::mutex> lock(state.mutex);
                state.condition.wait_for(
                    lock,
                    std::chrono::seconds(5),
                    [&state] { return state.frame_count != 0; });
                framesDuringCapture = state.frame_count;
            } else {
                std::cerr << "sd_start_capture failed: " << result << ".\n";
                testPassed = false;
            }
        }
    }

    if (captureStarted) {
        const SD_Result stopResult = stopCapture(handle);
        if (stopResult != SD_SUCCESS) {
            std::cerr << "sd_stop_capture failed: " << stopResult << ".\n";
            testPassed = false;
        }
    }
    if (encoderStarted) {
        const SD_Result stopResult = stopEncoder(handle);
        if (stopResult != SD_SUCCESS) {
            std::cerr << "sd_stop_encoder failed: " << stopResult << ".\n";
            testPassed = false;
        }
    }
    if (displayCreated) {
        const SD_Result destroyResult = destroyDisplay(handle);
        if (destroyResult != SD_SUCCESS) {
            std::cerr << "sd_destroy_virtual_display failed: "
                      << destroyResult << ".\n";
            testPassed = false;
        }
    }
    uint32_t totalFrames = 0;
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        totalFrames = state.frame_count;
    }
    destroy(handle);
    FreeLibrary(module);

    if (framesDuringCapture == 0) {
        std::cerr << "No encoded frames were received while capture was active; "
                  << totalFrames << " arrived before shutdown completed.\n";
        testPassed = false;
    }
    return testPassed ? 0 : 1;
}