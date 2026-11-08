// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SHIPWRIGHT_PROBE_SOH_RUNTIME_BRIDGE_H
#define SHIPWRIGHT_PROBE_SOH_RUNTIME_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OOT_PS3_SOH_BRIDGE_INIT_OTR = 1U << 0,
    OOT_PS3_SOH_BRIDGE_HEAP_READY = 1U << 1,
    OOT_PS3_SOH_BRIDGE_MAIN_READY = 1U << 2,
    OOT_PS3_SOH_BRIDGE_FIRST_SCENE = 1U << 3,
    OOT_PS3_SOH_BRIDGE_FRAME_TICKS = 1U << 4,
    OOT_PS3_SOH_BRIDGE_SHUTDOWN_READY = 1U << 5,
    OOT_PS3_SOH_BRIDGE_ALL = OOT_PS3_SOH_BRIDGE_INIT_OTR |
                             OOT_PS3_SOH_BRIDGE_HEAP_READY |
                             OOT_PS3_SOH_BRIDGE_MAIN_READY |
                             OOT_PS3_SOH_BRIDGE_FIRST_SCENE |
                             OOT_PS3_SOH_BRIDGE_FRAME_TICKS |
                             OOT_PS3_SOH_BRIDGE_SHUTDOWN_READY,
};

typedef struct OotPs3SohRuntimeBridgeResult {
    uint32_t flags;
    uint32_t phase;
    uint32_t tick_count;
    uint32_t frame_accumulator;
    uint32_t resource_flags;
    uint32_t game_version;
    uint32_t scene_size;
    uint32_t scene_hash_high;
    uint32_t scene_hash_low;
} OotPs3SohRuntimeBridgeResult;

OotPs3SohRuntimeBridgeResult oot_ps3_soh_runtime_bridge_probe_load(void);

#ifdef __cplusplus
}
#endif

#endif
