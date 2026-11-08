// SPDX-License-Identifier: GPL-3.0-or-later
#include "ps3_platform/software_renderer.h"

void oot_ps3_renderer_clear(OotPs3DisplayBuffer* buffer, u32 color, u32 frame) {
    oot_ps3_screen_fill(buffer, color, frame);
}

void oot_ps3_renderer_rect(OotPs3DisplayBuffer* buffer, s32 x, s32 y, u16 width, u16 height, u32 color) {
    const s32 start_x = (x < 0) ? 0 : x;
    const s32 start_y = (y < 0) ? 0 : y;
    const s32 end_x = ((s32)x + width > buffer->width) ? buffer->width : (s32)x + width;
    const s32 end_y = ((s32)y + height > buffer->height) ? buffer->height : (s32)y + height;

    if (start_x >= end_x || start_y >= end_y) {
        return;
    }

    for (s32 row_y = start_y; row_y < end_y; ++row_y) {
        u32* row = buffer->pixels + ((u32)row_y * buffer->width);
        for (s32 col_x = start_x; col_x < end_x; ++col_x) {
            row[col_x] = color;
        }
    }
}

void oot_ps3_renderer_texture(OotPs3DisplayBuffer* buffer, const OotPs3Texture* texture, s32 x, s32 y) {
    if (texture == NULL || texture->pixels == NULL) {
        return;
    }

    for (u16 src_y = 0; src_y < texture->height; ++src_y) {
        const s32 dst_y = y + src_y;
        if (dst_y < 0 || dst_y >= buffer->height) {
            continue;
        }

        u32* dst_row = buffer->pixels + ((u32)dst_y * buffer->width);
        const u32* src_row = texture->pixels + ((u32)src_y * texture->width);

        for (u16 src_x = 0; src_x < texture->width; ++src_x) {
            const s32 dst_x = x + src_x;
            if (dst_x < 0 || dst_x >= buffer->width) {
                continue;
            }

            const u32 pixel = src_row[src_x];
            if ((pixel & 0xFF000000U) == 0U) {
                continue;
            }
            dst_row[dst_x] = pixel & 0x00FFFFFFU;
        }
    }
}
