#pragma once

#include "slimedisplay_types.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class CaptureManager {
public:
    using FrameCallback =
        std::function<void(const std::vector<uint8_t>&, int64_t)>;

    ~CaptureManager();

    SD_Result start(
        const std::wstring& deviceName,
        uint32_t width,
        uint32_t height,
        uint32_t fps,
        FrameCallback callback);
    SD_Result stop();

private:
    void run(
        std::wstring deviceName,
        uint32_t width,
        uint32_t height,
        uint32_t fps,
        std::vector<uint8_t> pixels,
        FrameCallback callback);
    void signal_start(SD_Result result);

    std::atomic<bool> running_{false};
    std::atomic<SD_Result> last_result_{SD_SUCCESS};
    mutable std::mutex startup_mutex_;
    std::condition_variable startup_condition_;
    bool startup_complete_ = false;
    SD_Result startup_result_ = SD_ERROR_INITIALIZATION;
    std::thread worker_;
};
