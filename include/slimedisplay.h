#ifndef SLIMEDISPLAY_H
#define SLIMEDISPLAY_H

#include "slimedisplay_types.h"

#if defined(_WIN32)
    #ifdef SLIMEDISPLAY_EXPORTS
        #define SD_API __declspec(dllexport)
    #else
        #define SD_API __declspec(dllimport)
    #endif
    #define SD_CALL __cdecl
#else
    #define SD_API __attribute__((visibility("default")))
    #define SD_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

SD_API SD_Result SD_CALL
sd_create(SD_Handle** handle);

SD_API void SD_CALL
sd_destroy(SD_Handle* handle);

SD_API SD_Result SD_CALL
sd_create_virtual_display(
    SD_Handle* handle,
    const SD_DisplayConfig* config
);

SD_API SD_Result SD_CALL
sd_destroy_virtual_display(
    SD_Handle* handle
);

SD_API SD_Result SD_CALL
sd_get_virtual_display_info(
    SD_Handle* handle,
    SD_DisplayInfo* info
);

SD_API SD_Result SD_CALL
sd_start_capture(SD_Handle* handle);

SD_API SD_Result SD_CALL
sd_stop_capture(SD_Handle* handle);

SD_API SD_Result SD_CALL
sd_start_encoder(SD_Handle* handle);

SD_API SD_Result SD_CALL
sd_stop_encoder(SD_Handle* handle);

SD_API uint32_t SD_CALL
sd_get_version(void);

typedef void (*SD_EncodedFrameCallback)(
    const uint8_t* data,
    uint32_t size,
    int64_t presentation_time,
    int32_t is_keyframe,
    void* user_data
);

SD_API SD_Result SD_CALL
sd_set_encoded_frame_callback(
    SD_Handle* handle,
    SD_EncodedFrameCallback callback,
    void* user_data
);

#ifdef __cplusplus
}
#endif

#endif
