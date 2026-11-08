// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OOT_PS3_SOFTWARE_RENDERER_H
#define OOT_PS3_SOFTWARE_RENDERER_H

#include <stdbool.h>
#include <ppu-types.h>

#include "ps3_platform/rsx_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    u16 width;
    u16 height;
    u32* pixels;
} OotPs3Texture;

void oot_ps3_renderer_clear(OotPs3DisplayBuffer* buffer, u32 color, u32 frame);
void oot_ps3_renderer_rect(OotPs3DisplayBuffer* buffer, s32 x, s32 y, u16 width, u16 height, u32 color);
void oot_ps3_renderer_texture(OotPs3DisplayBuffer* buffer, const OotPs3Texture* texture, s32 x, s32 y);

#ifdef __cplusplus
}
#endif

#endif
