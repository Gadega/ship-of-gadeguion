// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SHIPWRIGHT_PROBE_SOH_C_SIDE_SLICE_PROBE_H
#define SHIPWRIGHT_PROBE_SOH_C_SIDE_SLICE_PROBE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OOT_PS3_SOH_C_SLICE_LIFECYCLE = 1U << 0,
    OOT_PS3_SOH_C_SLICE_FILE_EXISTS = 1U << 1,
    OOT_PS3_SOH_C_SLICE_LOAD_NAME = 1U << 2,
    OOT_PS3_SOH_C_SLICE_LOAD_HASH = 1U << 3,
    OOT_PS3_SOH_C_SLICE_HASH_STRING = 1U << 4,
    OOT_PS3_SOH_C_SLICE_VERSION = 1U << 5,
    OOT_PS3_SOH_C_SLICE_PRIORITY = 1U << 6,
    OOT_PS3_SOH_C_SLICE_ALL = OOT_PS3_SOH_C_SLICE_LIFECYCLE |
                              OOT_PS3_SOH_C_SLICE_FILE_EXISTS |
                              OOT_PS3_SOH_C_SLICE_LOAD_NAME |
                              OOT_PS3_SOH_C_SLICE_LOAD_HASH |
                              OOT_PS3_SOH_C_SLICE_HASH_STRING |
                              OOT_PS3_SOH_C_SLICE_VERSION |
                              OOT_PS3_SOH_C_SLICE_PRIORITY,
};

typedef struct OotPs3SohCSideSliceProbeResult {
    uint32_t flags;
    uint32_t lifecycle_cookie;
    uint32_t game_version;
    uint32_t name_size;
    uint32_t hash_size;
    uint32_t priority;
    uint32_t scene_hash_high;
    uint32_t scene_hash_low;
    uint32_t byte_sum;
} OotPs3SohCSideSliceProbeResult;

OotPs3SohCSideSliceProbeResult oot_ps3_soh_c_side_slice_probe_load(void);

#ifdef __cplusplus
}
#endif

#endif
