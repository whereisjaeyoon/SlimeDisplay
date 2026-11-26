#pragma once

#include "slimedisplay.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

class EncoderManager {
public:
    ~EncoderManager();

    SD_Result start(uint32_t width, uint32_t height, uint32_t fps);
    SD_Result stop();
    SD_Result enqueue_frame(
        const std::vector<uint8_t>& pixels,
        bool is_nv12,
        int64_t presentation_time);
    void set_callback(SD_EncodedFrameCallback callback, void* user_data);

private:
    struct Frame {
        std::vector<uint8_t> pixels;
        bool is_nv12;
        int64_t presentation_time;
    };

    void run(uint32_t width, uint32_t height, uint32_t fps);
    void signal_start(SD_Result result);

    std::atomic<bool> running_{false};
    std::atomic<SD_Result> last_result_{SD_SUCCESS};
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<Frame> frames_;
    SD_EncodedFrameCallback callback_ = nullptr;
    void* callback_user_data_ = nullptr;
    bool startup_complete_ = false;
    SD_Result startup_result_ = SD_ERROR_INITIALIZATION;
    std::thread worker_;
};
