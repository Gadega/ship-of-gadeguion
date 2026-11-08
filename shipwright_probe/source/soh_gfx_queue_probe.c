// SPDX-License-Identifier: GPL-3.0-or-later
#include "shipwright_probe/soh_gfx_queue_probe.h"

#include "shipwright_probe/soh_upstream_compat.h"

#include <stddef.h>

static uint32_t g_graph_frame_count = 0;

static void gfx_queue_push(OotPs3GfxQueue* queue, uint32_t type,
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

void Graph_StartFrame(void) {
    g_graph_frame_count++;
}

void Graph_ProcessGfxCommands(OotPs3GfxQueue* queue) {
    if (queue == NULL) {
        return;
    }

    uint32_t checksum = 0x47505830U;
    for (uint32_t i = 0; i < queue->count; ++i) {
        const OotPs3GfxCommand* command = &queue->commands[i];
        checksum ^= command->type * 33U;
        checksum += command->a + (command->b << 1U) + (command->c << 2U) + (command->d << 3U);
        checksum ^= command->color + (checksum << 5U) + (checksum >> 2U);
    }
    queue->checksum = checksum;
}

OotPs3SohGfxQueueProbeResult oot_ps3_soh_gfx_queue_probe_load(void) {
    static const char kSceneName[] = "scenes/test_scene/room_0/display_list.bin";
    OotPs3SohGfxQueueProbeResult result = {0, 0, 0, 0, 0, 0};
    OotPs3GfxQueue queue = {{{0, 0, 0, 0, 0, 0}}, 0, 0};
    PlayState play = {0, 0, 0, 0};
    RomFile scene_file = {kSceneName, 0, 35U};
    char arg0[] = "oot_ps3_runtime";
    char* argv[] = {arg0, NULL};

    if (sizeof(queue.commands) / sizeof(queue.commands[0]) == 16U &&
        sizeof(OotPs3GfxCommand) == 24U) {
        result.flags |= OOT_PS3_SOH_GFX_QUEUE_STRUCTS;
    }

    InitOTR(1, argv);
    if (Play_LoadFile(&play, &scene_file) != NULL && play.loadedResourceSize == 35U &&
        play.loadedResourceSum == 3390U) {
        result.flags |= OOT_PS3_SOH_GFX_QUEUE_LOADFILE;
    }

    Graph_StartFrame();
    gfx_queue_push(&queue, OOT_PS3_GFX_CMD_CLEAR, 0, 0, 320, 240, 0x00101418U);
    gfx_queue_push(&queue, OOT_PS3_GFX_CMD_RECT, 32, 32, 128, 32, 0x00C0FF40U);
    gfx_queue_push(&queue, OOT_PS3_GFX_CMD_RECT, 32, 80, play.loadedResourceSize, 16, 0x0040FFB0U);
    gfx_queue_push(&queue, OOT_PS3_GFX_CMD_PRESENT, 0, 0, 0, 0, 0U);
    if (queue.count == 4U) {
        result.flags |= OOT_PS3_SOH_GFX_QUEUE_COMMANDS;
    }

    Graph_ProcessGfxCommands(&queue);
    if (queue.checksum != 0U) {
        result.flags |= OOT_PS3_SOH_GFX_QUEUE_PROCESS;
    }

    result.command_count = queue.count;
    result.checksum = queue.checksum;
    result.frame_count = g_graph_frame_count;
    result.resource_size = play.loadedResourceSize;
    result.resource_sum = play.loadedResourceSum;
    if (result.frame_count != 0U) {
        result.flags |= OOT_PS3_SOH_GFX_QUEUE_FRAME;
    }

    DeinitOTR();
    return result;
}
