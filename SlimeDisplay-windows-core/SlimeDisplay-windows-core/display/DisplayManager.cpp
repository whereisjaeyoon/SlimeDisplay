#include "DisplayManager.h"

#include <algorithm>
#include <cwctype>
#include <string>

namespace {

bool ContainsInsensitive(const std::wstring& value, const std::wstring& search)
{
    if (search.empty() || value.size() < search.size()) {
        return false;
    }

    return std::search(
        value.begin(),
        value.end(),
        search.begin(),
        search.end(),
        [](wchar_t left, wchar_t right) {
            return std::towlower(left) == std::towlower(right);
        }) != value.end();
}

uint64_t GetStableDisplayId(const std::wstring& deviceId)
{
    uint64_t hash = 14695981039346656037ULL;
    for (wchar_t character : deviceId) {
        hash ^= static_cast<uint16_t>(character);
        hash *= 1099511628211ULL;
    }
    return hash == 0 ? 1 : hash;
}

bool FindVirtualDisplay(
    std::wstring& deviceName,
    std::wstring& monitorDeviceId)
{
    for (DWORD adapterIndex = 0;; ++adapterIndex) {
        DISPLAY_DEVICEW adapter{};
        adapter.cb = sizeof(adapter);
        if (!EnumDisplayDevicesW(nullptr, adapterIndex, &adapter, 0)) {
            break;
        }

        const bool isVdd =
            ContainsInsensitive(adapter.DeviceString, L"Virtual Display Driver") ||
            ContainsInsensitive(adapter.DeviceID, L"MTTVDD");
        if (!isVdd ||
            (adapter.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == 0) {
            continue;
        }

        for (DWORD monitorIndex = 0;; ++monitorIndex) {
            DISPLAY_DEVICEW monitor{};
            monitor.cb = sizeof(monitor);
            if (!EnumDisplayDevicesW(
                    adapter.DeviceName,
                    monitorIndex,
                    &monitor,
                    0)) {
                break;
            }

            if ((monitor.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) != 0) {
                deviceName = adapter.DeviceName;
                monitorDeviceId = monitor.DeviceID;
                return true;
            }
        }
    }

    return false;
}

bool FindDisplayMode(
    const std::wstring& deviceName,
    const SD_DisplayConfig& config,
    DEVMODEW& mode)
{
    for (DWORD modeIndex = 0;; ++modeIndex) {
        DEVMODEW candidate{};
        candidate.dmSize = sizeof(candidate);
        if (!EnumDisplaySettingsExW(
                deviceName.c_str(),
                modeIndex,
                &candidate,
                0)) {
            break;
        }

        if (candidate.dmPelsWidth == config.width &&
            candidate.dmPelsHeight == config.height &&
            candidate.dmDisplayFrequency == config.fps) {
            mode = candidate;
            return true;
        }
    }

    return false;
}

} // namespace

SD_Result DisplayManager::create_virtual_display(
    const SD_DisplayConfig& config)
{
    if (has_virtual_display_) {
        return SD_ERROR_INVALID_STATE;
    }

    std::wstring deviceName;
    std::wstring monitorDeviceId;
    if (!FindVirtualDisplay(deviceName, monitorDeviceId)) {
        return SD_ERROR_NOT_SUPPORTED;
    }

    DEVMODEW originalMode{};
    originalMode.dmSize = sizeof(originalMode);
    if (!EnumDisplaySettingsExW(
            deviceName.c_str(),
            ENUM_CURRENT_SETTINGS,
            &originalMode,
            0)) {
        return SD_ERROR_INTERNAL;
    }

    DEVMODEW requestedMode{};
    if (!FindDisplayMode(deviceName, config, requestedMode)) {
        return SD_ERROR_NOT_SUPPORTED;
    }

    requestedMode.dmFields =
        DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
    requestedMode.dmPelsWidth = config.width;
    requestedMode.dmPelsHeight = config.height;
    requestedMode.dmDisplayFrequency = config.fps;

    const LONG result = ChangeDisplaySettingsExW(
        deviceName.c_str(),
        &requestedMode,
        nullptr,
        CDS_FULLSCREEN,
        nullptr);
    if (result != DISP_CHANGE_SUCCESSFUL) {
        return result == DISP_CHANGE_BADMODE
            ? SD_ERROR_NOT_SUPPORTED
            : SD_ERROR_INTERNAL;
    }

    device_name_ = std::move(deviceName);
    monitor_device_id_ = std::move(monitorDeviceId);
    original_mode_ = originalMode;
    width_ = config.width;
    height_ = config.height;
    fps_ = config.fps;
    has_virtual_display_ = true;
    return SD_SUCCESS;
}

SD_Result DisplayManager::destroy_virtual_display()
{
    if (!has_virtual_display_) {
        return SD_SUCCESS;
    }

    const LONG result = ChangeDisplaySettingsExW(
        device_name_.c_str(),
        &original_mode_,
        nullptr,
        0,
        nullptr);

    device_name_.clear();
    monitor_device_id_.clear();
    original_mode_ = {};
    width_ = 0;
    height_ = 0;
    fps_ = 0;
    has_virtual_display_ = false;

    return result == DISP_CHANGE_SUCCESSFUL
        ? SD_SUCCESS
        : SD_ERROR_INTERNAL;
}

SD_Result DisplayManager::get_virtual_display_info(SD_DisplayInfo& info) const
{
    if (!has_virtual_display_) {
        return SD_ERROR_INVALID_STATE;
    }

    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsExW(
            device_name_.c_str(),
            ENUM_CURRENT_SETTINGS,
            &mode,
            0)) {
        return SD_ERROR_INTERNAL;
    }

    info.id = GetStableDisplayId(monitor_device_id_);
    info.width = mode.dmPelsWidth;
    info.height = mode.dmPelsHeight;
    info.refresh_rate = mode.dmDisplayFrequency;
    return SD_SUCCESS;
}

bool DisplayManager::has_virtual_display() const noexcept
{
    return has_virtual_display_;
}

const std::wstring& DisplayManager::device_name() const noexcept
{
    return device_name_;
}

uint32_t DisplayManager::width() const noexcept
{
    return width_;
}

uint32_t DisplayManager::height() const noexcept
{
    return height_;
}

uint32_t DisplayManager::fps() const noexcept
{
    return fps_;
}
