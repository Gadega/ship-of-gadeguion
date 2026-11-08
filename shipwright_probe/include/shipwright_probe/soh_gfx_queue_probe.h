// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SHIPWRIGHT_PROBE_SOH_GFX_QUEUE_PROBE_H
#define SHIPWRIGHT_PROBE_SOH_GFX_QUEUE_PROBE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OOT_PS3_SOH_GFX_QUEUE_STRUCTS = 1U << 0,
    OOT_PS3_SOH_GFX_QUEUE_LOADFILE = 1U << 1,
    OOT_PS3_SOH_GFX_QUEUE_COMMANDS = 1U << 2,
    OOT_PS3_SOH_GFX_QUEUE_PROCESS = 1U << 3,
    OOT_PS3_SOH_GFX_QUEUE_FRAME = 1U << 4,
    OOT_PS3_SOH_GFX_QUEUE_ALL = OOT_PS3_SOH_GFX_QUEUE_STRUCTS |
                                OOT_PS3_SOH_GFX_QUEUE_LOADFILE |
                                OOT_PS3_SOH_GFX_QUEUE_COMMANDS |
                                OOT_PS3_SOH_GFX_QUEUE_PROCESS |
                                OOT_PS3_SOH_GFX_QUEUE_FRAME,
};

typedef struct OotPs3SohGfxQueueProbeResult {
    uint32_t flags;
    uint32_t command_count;
    uint32_t checksum;
    uint32_t frame_count;
    uint32_t resource_size;
    uint32_t resource_sum;
} OotPs3SohGfxQueueProbeResult;

OotPs3SohGfxQueueProbeResult oot_ps3_soh_gfx_queue_probe_load(void);

#ifdef __cplusplus
}
#endif

#endif
