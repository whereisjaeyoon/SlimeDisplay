#pragma once

#include "capture/CaptureManager.h"
#include "display/DisplayManager.h"
#include "encoder/EncoderManager.h"
#include "slimedisplay.h"

#include <mutex>

class SDManager {
public:
    ~SDManager();

    SD_Result create_virtual_display(const SD_DisplayConfig& config);
    SD_Result destroy_virtual_display();
    SD_Result get_virtual_display_info(SD_DisplayInfo& info) const;
    SD_Result start_capture();
    SD_Result stop_capture();
    SD_Result start_encoder();
    SD_Result stop_encoder();
    SD_Result set_encoded_frame_callback(
        SD_EncodedFrameCallback callback,
        void* user_data);

private:
    mutable std::mutex mutex_;
    DisplayManager display_;
    CaptureManager capture_;
    EncoderManager encoder_;
    SD_EncodedFrameCallback encoded_frame_callback_ = nullptr;
    void* encoded_frame_user_data_ = nullptr;
};
