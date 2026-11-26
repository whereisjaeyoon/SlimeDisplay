#ifndef SLIMEDISPLAY_TYPES_H
#define SLIMEDISPLAY_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SD_Handle SD_Handle;

typedef enum {
    SD_SUCCESS = 0,
    SD_ERROR_INVALID_ARGUMENT = -1,
    SD_ERROR_INITIALIZATION = -2,
    SD_ERROR_NOT_SUPPORTED = -3,
    SD_ERROR_PERMISSION_DENIED = -4,
    SD_ERROR_CAPTURE = -5,
    SD_ERROR_ENCODING = -6,
    SD_ERROR_TRANSPORT = -7,
    SD_ERROR_INVALID_STATE = -8,
    SD_ERROR_INTERNAL = -9
} SD_Result;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t fps;
} SD_DisplayConfig;

typedef struct {
    uint64_t id;
    uint32_t width;
    uint32_t height;
    uint32_t refresh_rate;
} SD_DisplayInfo;

typedef enum {
    SD_PIXEL_FORMAT_UNKNOWN = 0,
    SD_PIXEL_FORMAT_BGRA = 1,
    SD_PIXEL_FORMAT_NV12 = 2,
    SD_PIXEL_FORMAT_I420 = 3
} SD_PixelFormat;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    SD_PixelFormat format;
    uint64_t timestamp;
} SD_FrameInfo;

#ifdef __cplusplus
}
#endif

#endif
