#import "SDAPI.h"
#import "SlimeDisplay_mac-Swift.h"

#include <new>

struct SD_Handle {
    SDManagerBridge* manager;
};

SD_Result sd_create(SD_Handle** handle)
{
    if (handle == nullptr) {
        return SD_ERROR_INVALID_ARGUMENT;
    }

    *handle = nullptr;

    @try {
        SD_Handle* instance = new (std::nothrow) SD_Handle();

        if (instance == nullptr) {
            return SD_ERROR_INTERNAL;
        }

        instance->manager = [[SDManagerBridge alloc] init];

        if (instance->manager == nil) {
            delete instance;
            return SD_ERROR_INITIALIZATION;
        }

        *handle = instance;
        return SD_SUCCESS;

    } @catch (NSException* exception) {
        return SD_ERROR_INITIALIZATION;
    }
}

void sd_destroy(SD_Handle* handle)
{
    if (handle == nullptr) {
        return;
    }

    [handle->manager destroyDisplay];

    handle->manager = nil;
    delete handle;
}

SD_Result sd_create_virtual_display(
    SD_Handle* handle,
    const SD_DisplayConfig* config)
{
    if (handle == nullptr || config == nullptr) {
        return SD_ERROR_INVALID_ARGUMENT;
    }

    if (config->width == 0 ||
        config->height == 0 ||
        config->fps == 0) {
        return SD_ERROR_INVALID_ARGUMENT;
    }

    int32_t result = [handle->manager
        createDisplay:config->width
        height:config->height
        fps:config->fps];

    return result == 0
        ? SD_SUCCESS
        : SD_ERROR_INITIALIZATION;
}

SD_Result sd_destroy_virtual_display(SD_Handle* handle)
{
    if (handle == nullptr) {
        return SD_ERROR_INVALID_ARGUMENT;
    }

    [handle->manager destroyDisplay];

    return SD_SUCCESS;
}

SD_Result sd_get_virtual_display_info(
    SD_Handle* handle,
    SD_DisplayInfo* info)
{
    if (handle == nullptr || info == nullptr) {
        return SD_ERROR_INVALID_ARGUMENT;
    }

    uint64_t displayID = [handle->manager getDisplayID];

    if (displayID == 0) {
        return SD_ERROR_INVALID_STATE;
    }

    info->id = displayID;

    return SD_SUCCESS;
}

SD_Result sd_start_capture(SD_Handle* handle)
{
    if (handle == nullptr) {
        return SD_ERROR_INVALID_ARGUMENT;
    }

    __block SD_Result result = SD_ERROR_CAPTURE;

    dispatch_semaphore_t semaphore =
        dispatch_semaphore_create(0);

    [handle->manager startCapture:^(int32_t status) {
        result = status == 0
            ? SD_SUCCESS
            : SD_ERROR_CAPTURE;

        dispatch_semaphore_signal(semaphore);
    }];

    dispatch_semaphore_wait(
        semaphore,
        DISPATCH_TIME_FOREVER
    );

    return result;
}

SD_Result sd_stop_capture(SD_Handle* handle)
{
    if (handle == nullptr) {
        return SD_ERROR_INVALID_ARGUMENT;
    }

    [handle->manager stopCapture];

    return SD_SUCCESS;
}

SD_Result sd_start_encoder(SD_Handle* handle)
{
    if (handle == nullptr) {
        return SD_ERROR_INVALID_ARGUMENT;
    }

    int32_t result = [handle->manager startEncoder];

    return result == 0
        ? SD_SUCCESS
        : SD_ERROR_ENCODING;
}

SD_Result sd_stop_encoder(SD_Handle* handle)
{
    if (handle == nullptr) {
        return SD_ERROR_INVALID_ARGUMENT;
    }

    [handle->manager stopEncoder];

    return SD_SUCCESS;
}
