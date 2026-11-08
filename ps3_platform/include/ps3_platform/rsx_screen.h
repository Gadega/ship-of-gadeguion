// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OOT_PS3_RSX_SCREEN_H
#define OOT_PS3_RSX_SCREEN_H

#include <ppu-types.h>
#include <rsx/rsx.h>

#define OOT_PS3_BUFFER_COUNT 2

typedef struct {
    u32* pixels;
    u32 offset;
    u16 width;
    u16 height;
    s32 id;
} OotPs3DisplayBuffer;

typedef struct {
    gcmContextData* context;
    void* host_memory;
    OotPs3DisplayBuffer buffers[OOT_PS3_BUFFER_COUNT];
    s32 current_buffer;
} OotPs3Screen;

int oot_ps3_screen_init(OotPs3Screen* screen);
void oot_ps3_screen_fill(OotPs3DisplayBuffer* buffer, u32 color, u32 frame);
void oot_ps3_screen_rect(OotPs3DisplayBuffer* buffer, u16 x, u16 y, u16 width, u16 height, u32 color);
void oot_ps3_screen_present(OotPs3Screen* screen);
void oot_ps3_screen_shutdown(OotPs3Screen* screen);

#endif
