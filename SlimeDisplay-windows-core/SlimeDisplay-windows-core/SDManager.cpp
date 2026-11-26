#include "SDManager.h"

SDManager::~SDManager()
{
    stop_capture();
    stop_encoder();
    destroy_virtual_display();
}

SD_Result SDManager::create_virtual_display(const SD_DisplayConfig& config)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (config.width < 320 ||
        config.height < 240 ||
        config.width > 7680 ||
        config.height > 4320 ||
        config.fps == 0 ||
        config.fps > 120) {
        return SD_ERROR_INVALID_ARGUMENT;
    }

    return display_.create_virtual_display(config);
}

SD_Result SDManager::destroy_virtual_display()
{
    const SD_Result captureResult = stop_capture();
    const SD_Result encoderResult = stop_encoder();

    std::lock_guard<std::mutex> lock(mutex_);
    const SD_Result displayResult = display_.destroy_virtual_display();
    if (displayResult != SD_SUCCESS) {
        return displayResult;
    }
    return captureResult != SD_SUCCESS ? captureResult : encoderResult;
}

SD_Result SDManager::get_virtual_display_info(SD_DisplayInfo& info) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return display_.get_virtual_display_info(info);
}

SD_Result SDManager::start_capture()
{
    std::wstring deviceName;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fps = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!display_.has_virtual_display()) {
            return SD_ERROR_INVALID_STATE;
        }
        deviceName = display_.device_name();
        width = display_.width();
        height = display_.height();
        fps = display_.fps();
    }

    return capture_.start(
        deviceName,
        width,
        height,
        fps,
        [this](const std::vector<uint8_t>& frame, int64_t presentationTime) {
            encoder_.enqueue_frame(frame, presentationTime);
        });
}

SD_Result SDManager::stop_capture()
{
    return capture_.stop();
}

SD_Result SDManager::start_encoder()
{
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t fps = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!display_.has_virtual_display()) {
            return SD_ERROR_INVALID_STATE;
        }
        width = display_.width();
        height = display_.height();
        fps = display_.fps();
    }

    return encoder_.start(width, height, fps);
}

SD_Result SDManager::stop_encoder()
{
    return encoder_.stop();
}

SD_Result SDManager::set_encoded_frame_callback(
    SD_EncodedFrameCallback callback,
    void* user_data)
{
    std::lock_guard<std::mutex> lock(mutex_);
    encoded_frame_callback_ = callback;
    encoded_frame_user_data_ = callback == nullptr ? nullptr : user_data;
    encoder_.set_callback(encoded_frame_callback_, encoded_frame_user_data_);
    return SD_SUCCESS;
}
