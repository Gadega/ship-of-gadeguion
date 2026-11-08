// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SHIPWRIGHT_PROBE_ENGINE_BOOTSTRAP_PROBE_H
#define SHIPWRIGHT_PROBE_ENGINE_BOOTSTRAP_PROBE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OOT_PS3_ENGINE_BOOT_PLATFORM = 1U << 0,
    OOT_PS3_ENGINE_BOOT_RESOURCES = 1U << 1,
    OOT_PS3_ENGINE_BOOT_STARTUP_SCENE = 1U << 2,
    OOT_PS3_ENGINE_BOOT_TICKABLE = 1U << 3,
    OOT_PS3_ENGINE_BOOT_ALL = OOT_PS3_ENGINE_BOOT_PLATFORM |
                              OOT_PS3_ENGINE_BOOT_RESOURCES |
                              OOT_PS3_ENGINE_BOOT_STARTUP_SCENE |
                              OOT_PS3_ENGINE_BOOT_TICKABLE,
};

typedef struct OotPs3EngineBootstrapProbeResult {
    uint32_t flags;
    uint32_t boot_state;
    uint32_t tick_count;
    uint32_t resource_flags;
    uint32_t scene_hash_high;
    uint32_t scene_hash_low;
} OotPs3EngineBootstrapProbeResult;

OotPs3EngineBootstrapProbeResult oot_ps3_engine_bootstrap_probe_load(void);

#ifdef __cplusplus
}
#endif

#endif
