// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SHIPWRIGHT_PROBE_TITLE_BOOT_PROBE_H
#define SHIPWRIGHT_PROBE_TITLE_BOOT_PROBE_H

#include <stdint.h>

#include "ps3_platform/rsx_screen.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OOT_PS3_TITLE_BOOT_ARCHIVES = 1U << 0,
    OOT_PS3_TITLE_BOOT_LOADED = 1U << 1,
    OOT_PS3_TITLE_BOOT_MAGIC = 1U << 2,
    OOT_PS3_TITLE_BOOT_COMMANDS = 1U << 3,
    OOT_PS3_TITLE_BOOT_CHECKSUM = 1U << 4,
    OOT_PS3_TITLE_BOOT_QUEUE = 1U << 5,
    OOT_PS3_TITLE_BOOT_SCENE = 1U << 6,
    OOT_PS3_TITLE_BOOT_ACTORS = 1U << 7,
    OOT_PS3_TITLE_BOOT_ALL = OOT_PS3_TITLE_BOOT_ARCHIVES |
                             OOT_PS3_TITLE_BOOT_LOADED |
                             OOT_PS3_TITLE_BOOT_MAGIC |
                             OOT_PS3_TITLE_BOOT_COMMANDS |
                             OOT_PS3_TITLE_BOOT_CHECKSUM |
                             OOT_PS3_TITLE_BOOT_QUEUE |
                             OOT_PS3_TITLE_BOOT_SCENE |
                             OOT_PS3_TITLE_BOOT_ACTORS,
};

typedef struct OotPs3TitleBootProbeResult {
    uint32_t flags;
    uint32_t file_size;
    uint32_t command_count;
    uint32_t rect_count;
    uint32_t queued_count;
    uint32_t scene_size;
    uint32_t actor_size;
    uint32_t scene_phases;
    uint32_t scene_fps;
    uint32_t actor_count;
    uint32_t checksum;
    uint32_t queue_checksum;
    uint32_t scene_checksum;
    uint32_t actor_checksum;
    uint32_t path_hash_high;
    uint32_t path_hash_low;
} OotPs3TitleBootProbeResult;

OotPs3TitleBootProbeResult oot_ps3_title_boot_probe_load(void);
void oot_ps3_title_boot_draw(OotPs3DisplayBuffer* buffer, uint32_t frame,
                             int32_t x, int32_t y, uint32_t scale);

#ifdef __cplusplus
}
#endif

#endif
