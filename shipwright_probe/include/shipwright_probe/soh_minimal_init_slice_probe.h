// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SHIPWRIGHT_PROBE_SOH_MINIMAL_INIT_SLICE_PROBE_H
#define SHIPWRIGHT_PROBE_SOH_MINIMAL_INIT_SLICE_PROBE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OOT_PS3_SOH_INIT_SLICE_GLOBALS = 1U << 0,
    OOT_PS3_SOH_INIT_SLICE_LIFECYCLE = 1U << 1,
    OOT_PS3_SOH_INIT_SLICE_HEAP = 1U << 2,
    OOT_PS3_SOH_INIT_SLICE_RESOURCE = 1U << 3,
    OOT_PS3_SOH_INIT_SLICE_FRAME = 1U << 4,
    OOT_PS3_SOH_INIT_SLICE_SHUTDOWN = 1U << 5,
    OOT_PS3_SOH_INIT_SLICE_ALL = OOT_PS3_SOH_INIT_SLICE_GLOBALS |
                                 OOT_PS3_SOH_INIT_SLICE_LIFECYCLE |
                                 OOT_PS3_SOH_INIT_SLICE_HEAP |
                                 OOT_PS3_SOH_INIT_SLICE_RESOURCE |
                                 OOT_PS3_SOH_INIT_SLICE_FRAME |
                                 OOT_PS3_SOH_INIT_SLICE_SHUTDOWN,
};

typedef struct OotPs3SohMinimalInitSliceProbeResult {
    uint32_t flags;
    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t heap_size;
    uint32_t frame_count;
    uint32_t resource_size;
    uint32_t resource_sum;
    uint32_t game_version;
    uint32_t scene_hash_high;
    uint32_t scene_hash_low;
} OotPs3SohMinimalInitSliceProbeResult;

OotPs3SohMinimalInitSliceProbeResult oot_ps3_soh_minimal_init_slice_probe_load(void);

#ifdef __cplusplus
}
#endif

#endif
