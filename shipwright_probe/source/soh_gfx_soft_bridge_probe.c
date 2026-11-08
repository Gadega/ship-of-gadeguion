// SPDX-License-Identifier: GPL-3.0-or-later
#include "shipwright_probe/soh_gfx_soft_bridge_probe.h"

#include "ps3_platform/software_renderer.h"
#include "shipwright_probe/soh_gfx_queue_probe.h"
#include "shipwright_probe/soh_upstream_compat.h"

#include <stddef.h>

enum {
    kSoftWidth = 64,
    kSoftHeight = 48,
    kSoftPixels = kSoftWidth * kSoftHeight,
};

static uint32_t g_soft_framebuffer[kSoftPixels];

static void queue_push(OotPs3GfxQueue* queue, uint32_t type,
                       uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t color) {
    if (queue == NULL || queue->count >= 16U) {
        return;
    }
    OotPs3GfxCommand* command = &queue->commands[queue->count++];
    command->type = type;
    command->a = a;
    command->b = b;
    command->c = c;
    command->d = d;
    command->color = color;
}

static void soft_clear(uint32_t color) {
    for (uint32_t i = 0; i < kSoftPixels; ++i) {
        g_soft_framebuffer[i] = color;
    }
}

static void soft_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    const uint32_t x_end = (x + w > kSoftWidth) ? kSoftWidth : x + w;
    const uint32_t y_end = (y + h > kSoftHeight) ? kSoftHeight : y + h;
    for (uint32_t py = y; py < y_end; ++py) {
        for (uint32_t px = x; px < x_end; ++px) {
            g_soft_framebuffer[(py * kSoftWidth) + px] = color;
        }
    }
}

static uint32_t soft_checksum(void) {
    uint32_t checksum = 0x534F4654U;
    for (uint32_t i = 0; i < kSoftPixels; ++i) {
        checksum ^= g_soft_framebuffer[i] + (checksum << 5U) + (checksum >> 2U) + i;
    }
    return checksum;
}

static uint32_t count_pixels(uint32_t color) {
    uint32_t count = 0U;
    for (uint32_t i = 0; i < kSoftPixels; ++i) {
        if (g_soft_framebuffer[i] == color) {
            count++;
        }
    }
    return count;
}

static uint32_t process_queue_to_soft_framebuffer(OotPs3GfxQueue* queue, uint32_t* present_count) {
    if (queue == NULL) {
        return 0U;
    }
    if (present_count != NULL) {
        *present_count = 0U;
    }

    for (uint32_t i = 0; i < queue->count; ++i) {
        const OotPs3GfxCommand* command = &queue->commands[i];
        if (command->type == OOT_PS3_GFX_CMD_CLEAR) {
            soft_clear(command->color);
        } else if (command->type == OOT_PS3_GFX_CMD_RECT) {
            soft_rect(command->a, command->b, command->c, command->d, command->color);
        } else if (command->type == OOT_PS3_GFX_CMD_PRESENT && present_count != NULL) {
            (*present_count)++;
        }
    }

    return soft_checksum();
}

OotPs3SohGfxSoftBridgeProbeResult oot_ps3_soh_gfx_soft_bridge_probe_load(void) {
    OotPs3SohGfxSoftBridgeProbeResult result = {0, 0, 0, 0, 0, 0, 0};
    OotPs3GfxQueue queue = {{{0, 0, 0, 0, 0, 0}}, 0, 0};
    const uint32_t clear_color = 0x00101418U;
    const uint32_t rect_color = 0x00A080FFU;

    queue_push(&queue, OOT_PS3_GFX_CMD_CLEAR, 0, 0, kSoftWidth, kSoftHeight, clear_color);
    queue_push(&queue, OOT_PS3_GFX_CMD_RECT, 8, 8, 20, 12, rect_color);
    queue_push(&queue, OOT_PS3_GFX_CMD_PRESENT, 0, 0, 0, 0, 0U);
    Graph_ProcessGfxCommands(&queue);

    result.command_count = queue.count;
    result.queue_checksum = queue.checksum;
    if (queue.count == 3U && queue.checksum != 0U) {
        result.flags |= OOT_PS3_SOH_GFX_SOFT_QUEUE;
    }

    result.framebuffer_checksum = process_queue_to_soft_framebuffer(&queue, &result.present_count);
    result.clear_pixels = count_pixels(clear_color);
    result.rect_pixels = count_pixels(rect_color);

    if (result.clear_pixels == kSoftPixels - (20U * 12U)) {
        result.flags |= OOT_PS3_SOH_GFX_SOFT_CLEAR;
    }
    if (result.rect_pixels == 20U * 12U) {
        result.flags |= OOT_PS3_SOH_GFX_SOFT_RECT;
    }
    if (result.present_count == 1U) {
        result.flags |= OOT_PS3_SOH_GFX_SOFT_PRESENT;
    }
    if (result.framebuffer_checksum != 0U) {
        result.flags |= OOT_PS3_SOH_GFX_SOFT_CHECKSUM;
    }

    return result;
}

void oot_ps3_soh_gfx_soft_bridge_draw_demo(OotPs3DisplayBuffer* buffer, uint32_t frame,
                                           int32_t x, int32_t y, uint32_t scale) {
    OotPs3GfxQueue queue = {{{0, 0, 0, 0, 0, 0}}, 0, 0};
    const uint32_t panel_w = (uint32_t)kSoftWidth * scale;
    const uint32_t panel_h = (uint32_t)kSoftHeight * scale;
    const uint32_t clear_color = 0x00101418U;
    const uint32_t rect_color = 0x00A080FFU;
    const uint32_t load_color = 0x0040FFB0U;
    const uint32_t pulse_color = (frame & 32U) ? 0x00FFFFFFU : 0x00C0FF40U;
    const uint32_t resource_width = 35U + (frame % 37U);

    if (buffer == NULL || scale == 0U) {
        return;
    }

    queue_push(&queue, OOT_PS3_GFX_CMD_CLEAR, 0, 0, kSoftWidth, kSoftHeight, clear_color);
    queue_push(&queue, OOT_PS3_GFX_CMD_RECT, 8, 8, 20, 12, rect_color);
    queue_push(&queue, OOT_PS3_GFX_CMD_RECT, 8, 26, resource_width, 4, load_color);
    queue_push(&queue, OOT_PS3_GFX_CMD_RECT, 48, 34, 8, 8, pulse_color);
    queue_push(&queue, OOT_PS3_GFX_CMD_PRESENT, 0, 0, 0, 0, 0U);
    Graph_ProcessGfxCommands(&queue);

    oot_ps3_renderer_rect(buffer, x - 2, y - 2, (u16)(panel_w + 4U), (u16)(panel_h + 4U), 0x00FFFFFFU);
    oot_ps3_renderer_rect(buffer, x, y, (u16)panel_w, (u16)panel_h, clear_color);
    for (uint32_t i = 0; i < queue.count; ++i) {
        const OotPs3GfxCommand* command = &queue.commands[i];
        if (command->type == OOT_PS3_GFX_CMD_RECT) {
            oot_ps3_renderer_rect(buffer,
                                  x + (int32_t)(command->a * scale),
                                  y + (int32_t)(command->b * scale),
                                  (u16)(command->c * scale),
                                  (u16)(command->d * scale),
                                  command->color);
        }
    }
}

void oot_ps3_soh_gfx_soft_bridge_draw_resource_demo(OotPs3DisplayBuffer* buffer, uint32_t frame,
                                                    int32_t x, int32_t y, uint32_t scale,
                                                    const OotPs3ResourceBootstrapProbeResult* resources) {
    OotPs3GfxQueue queue = {{{0, 0, 0, 0, 0, 0}}, 0, 0};
    const uint32_t panel_w = (uint32_t)kSoftWidth * scale;
    const uint32_t panel_h = (uint32_t)kSoftHeight * scale;
    const uint32_t clear_color = 0x00081012U;
    const uint32_t fail_color = 0x00B00000U;
    const uint32_t mount_color = 0x0000D060U;
    const uint32_t scene_color = 0x0040FFB0U;
    const uint32_t priority_color = 0x00FFC040U;
    const uint32_t version_color = 0x00A080FFU;
    const uint32_t pulse_color = (frame & 32U) ? 0x00FFFFFFU : 0x00C0FF40U;
    uint32_t scene_width = 4U;
    uint32_t manifest_width = 4U;
    uint32_t version_width = 4U;
    uint32_t archive_width = 4U;
    uint32_t priority_width = 4U;

    if (buffer == NULL || scale == 0U) {
        return;
    }

    if (resources != NULL) {
        scene_width = 4U + (resources->scene_size % 52U);
        manifest_width = 4U + (resources->manifest_size % 52U);
        version_width = 4U + (resources->version_value & 0x1FU);
        archive_width = 4U + (resources->archive_count * 8U);
        priority_width = 4U + ((resources->scene_priority == 0xFFFFFFFFU ? 0U : resources->scene_priority + 1U) * 12U);
    }

    queue_push(&queue, OOT_PS3_GFX_CMD_CLEAR, 0, 0, kSoftWidth, kSoftHeight, clear_color);
    queue_push(&queue, OOT_PS3_GFX_CMD_RECT, 6, 6, archive_width, 5,
               (resources != NULL && (resources->flags & OOT_PS3_RESOURCE_BOOT_MOUNTED)) ? mount_color : fail_color);
    queue_push(&queue, OOT_PS3_GFX_CMD_RECT, 6, 15, manifest_width, 5,
               (resources != NULL && (resources->flags & OOT_PS3_RESOURCE_BOOT_MANIFEST)) ? version_color : fail_color);
    queue_push(&queue, OOT_PS3_GFX_CMD_RECT, 6, 24, scene_width, 5,
               (resources != NULL && (resources->flags & OOT_PS3_RESOURCE_BOOT_SCENE_HASH)) ? scene_color : fail_color);
    queue_push(&queue, OOT_PS3_GFX_CMD_RECT, 6, 33, priority_width, 5,
               (resources != NULL && (resources->flags & OOT_PS3_RESOURCE_BOOT_PRIORITY)) ? priority_color : fail_color);
    queue_push(&queue, OOT_PS3_GFX_CMD_RECT, 50, 6 + ((frame >> 4U) % 30U), version_width > 10U ? 10U : version_width, 5,
               pulse_color);
    queue_push(&queue, OOT_PS3_GFX_CMD_PRESENT, 0, 0, 0, 0, 0U);
    Graph_ProcessGfxCommands(&queue);

    oot_ps3_renderer_rect(buffer, x - 2, y - 2, (u16)(panel_w + 4U), (u16)(panel_h + 4U),
                          (resources != NULL && (resources->flags & OOT_PS3_RESOURCE_BOOT_ALL) == OOT_PS3_RESOURCE_BOOT_ALL)
                              ? 0x0000FF80U
                              : 0x00FFFFFFU);
    oot_ps3_renderer_rect(buffer, x, y, (u16)panel_w, (u16)panel_h, clear_color);
    for (uint32_t i = 0; i < queue.count; ++i) {
        const OotPs3GfxCommand* command = &queue.commands[i];
        if (command->type == OOT_PS3_GFX_CMD_RECT) {
            oot_ps3_renderer_rect(buffer,
                                  x + (int32_t)(command->a * scale),
                                  y + (int32_t)(command->b * scale),
                                  (u16)(command->c * scale),
                                  (u16)(command->d * scale),
                                  command->color);
        }
    }
}
