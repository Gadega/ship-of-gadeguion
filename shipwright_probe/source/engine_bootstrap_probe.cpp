// SPDX-License-Identifier: GPL-3.0-or-later
#include "shipwright_probe/engine_bootstrap_probe.h"

#include "shipwright_probe/resource_bootstrap_probe.h"
#include "shipwright_probe/soh_runtime_bridge.h"

namespace {

enum BootState {
    BOOT_STATE_PLATFORM_READY = 1U,
    BOOT_STATE_RESOURCES_READY = 2U,
    BOOT_STATE_STARTUP_SCENE_READY = 3U,
    BOOT_STATE_TICKABLE = 4U,
};

} // namespace

OotPs3EngineBootstrapProbeResult oot_ps3_engine_bootstrap_probe_load(void) {
    OotPs3EngineBootstrapProbeResult result = {0, 0, 0, 0, 0, 0};

    result.boot_state = BOOT_STATE_PLATFORM_READY;
    result.flags |= OOT_PS3_ENGINE_BOOT_PLATFORM;

    const OotPs3ResourceBootstrapProbeResult resources = oot_ps3_resource_bootstrap_probe_load();
    result.resource_flags = resources.flags;
    result.scene_hash_high = resources.scene_hash_high;
    result.scene_hash_low = resources.scene_hash_low;

    if ((resources.flags & OOT_PS3_RESOURCE_BOOT_ALL) != OOT_PS3_RESOURCE_BOOT_ALL) {
        return result;
    }
    result.boot_state = BOOT_STATE_RESOURCES_READY;
    result.flags |= OOT_PS3_ENGINE_BOOT_RESOURCES;

    if (resources.scene_size == 0U || resources.scene_priority != 1U) {
        return result;
    }
    result.boot_state = BOOT_STATE_STARTUP_SCENE_READY;
    result.flags |= OOT_PS3_ENGINE_BOOT_STARTUP_SCENE;

    const OotPs3SohRuntimeBridgeResult bridge = oot_ps3_soh_runtime_bridge_probe_load();
    result.tick_count = bridge.tick_count;
    if ((bridge.flags & OOT_PS3_SOH_BRIDGE_ALL) == OOT_PS3_SOH_BRIDGE_ALL && result.tick_count >= 5U) {
        result.boot_state = BOOT_STATE_TICKABLE;
        result.flags |= OOT_PS3_ENGINE_BOOT_TICKABLE;
    }

    return result;
}
