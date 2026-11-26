#pragma once

#include "slimedisplay_types.h"

#include <string>
#include <windows.h>

class DisplayManager {
public:
    SD_Result create_virtual_display(const SD_DisplayConfig& config);
    SD_Result destroy_virtual_display();
    SD_Result get_virtual_display_info(SD_DisplayInfo& info) const;

    bool has_virtual_display() const noexcept;
    const std::wstring& device_name() const noexcept;
    uint32_t width() const noexcept;
    uint32_t height() const noexcept;
    uint32_t fps() const noexcept;

private:
    std::wstring device_name_;
    std::wstring monitor_device_id_;
    DEVMODEW original_mode_{};
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    uint32_t fps_ = 0;
    bool has_virtual_display_ = false;
};
