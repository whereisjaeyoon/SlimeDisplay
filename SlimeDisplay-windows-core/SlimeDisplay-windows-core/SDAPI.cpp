#include "SDManager.h"

#include <new>
#include <windows.h>

namespace {

template<typename Operation>
SD_Result Invoke(Operation&& operation) noexcept
{
    try {
        return operation();
    } catch (...) {
        OutputDebugStringA(
            "SlimeDisplay C ABI: caught a C++ exception.\n");
        return SD_ERROR_INTERNAL;
    }
}

} // namespace

struct SD_Handle {
    SDManager manager;
};

extern "C" {

SD_Result SD_CALL sd_create(SD_Handle** handle)
{
    if (handle == nullptr) {
        return SD_ERROR_INVALID_ARGUMENT;
    }

    *handle = nullptr;
    try {
        *handle = new (std::nothrow) SD_Handle{};
    } catch (...) {
        OutputDebugStringA("sd_create: failed to construct SD_Handle.\n");
        return SD_ERROR_INTERNAL;
    }
    return *handle == nullptr ? SD_ERROR_INTERNAL : SD_SUCCESS;
}

void SD_CALL sd_destroy(SD_Handle* handle)
{
    if (handle != nullptr) {
        try {
            handle->manager.stop_capture();
            handle->manager.stop_encoder();
            handle->manager.destroy_virtual_display();
        } catch (...) {
            OutputDebugStringA(
                "sd_destroy: cleanup raised a C++ exception.\n");
        }
    }
    delete handle;
}

SD_Result SD_CALL sd_create_virtual_display(
    SD_Handle* handle,
    const SD_DisplayConfig* config)
{
    if (handle == nullptr || config == nullptr) {
        return SD_ERROR_INVALID_ARGUMENT;
    }

    return Invoke([&] {
        return handle->manager.create_virtual_display(*config);
    });
}

SD_Result SD_CALL sd_destroy_virtual_display(SD_Handle* handle)
{
    return handle == nullptr
        ? SD_ERROR_INVALID_ARGUMENT
        : Invoke([&] {
            return handle->manager.destroy_virtual_display();
        });
}

SD_Result SD_CALL sd_get_virtual_display_info(
    SD_Handle* handle,
    SD_DisplayInfo* info)
{
    if (handle == nullptr || info == nullptr) {
        return SD_ERROR_INVALID_ARGUMENT;
    }

    return Invoke([&] {
        return handle->manager.get_virtual_display_info(*info);
    });
}

SD_Result SD_CALL sd_start_capture(SD_Handle* handle)
{
    return handle == nullptr
        ? SD_ERROR_INVALID_ARGUMENT
        : Invoke([&] {
            return handle->manager.start_capture();
        });
}

SD_Result SD_CALL sd_stop_capture(SD_Handle* handle)
{
    return handle == nullptr
        ? SD_ERROR_INVALID_ARGUMENT
        : Invoke([&] {
            return handle->manager.stop_capture();
        });
}

SD_Result SD_CALL sd_start_encoder(SD_Handle* handle)
{
    return handle == nullptr
        ? SD_ERROR_INVALID_ARGUMENT
        : Invoke([&] {
            return handle->manager.start_encoder();
        });
}


SD_Result SD_CALL sd_stop_encoder(SD_Handle* handle)
{
    return handle == nullptr
        ? SD_ERROR_INVALID_ARGUMENT
        : Invoke([&] {
            return handle->manager.stop_encoder();
        });
}

uint32_t SD_CALL sd_get_version(void)
{
    // No shared numeric version format has been defined yet.
    return 0;
}

SD_Result SD_CALL sd_set_encoded_frame_callback(
    SD_Handle* handle,
    SD_EncodedFrameCallback callback,
    void* user_data)
{
    if (handle == nullptr) {
        return SD_ERROR_INVALID_ARGUMENT;
    }

    return Invoke([&] {
        return handle->manager.set_encoded_frame_callback(
            callback,
            user_data);
    });
}

}
