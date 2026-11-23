
#ifndef SD_VIRTUAL_DISPLAY_SHIM_H
#define SD_VIRTUAL_DISPLAY_SHIM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 Creates a macOS virtual display.

 @param width Display width in pixels.
 @param height Display height in pixels.
 @param refreshRate Display refresh rate.
 @param displayID Output display identifier.
 @return 0 on success, negative value on failure.
 */
int32_t SDVirtualDisplayCreate(
    uint32_t width,
    uint32_t height,
    double refreshRate,
    uint32_t* displayID
);

/**
 Destroys the currently created virtual display.
 */
void SDVirtualDisplayDestroy(void);

#ifdef __cplusplus
}
#endif

#endif
