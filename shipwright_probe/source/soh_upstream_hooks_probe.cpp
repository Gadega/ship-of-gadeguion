// SPDX-License-Identifier: GPL-3.0-or-later
#include "shipwright_probe/soh_upstream_hooks_probe.h"

#include "shipwright_probe/soh_runtime_bridge.h"

namespace {

struct HookState {
    uint32_t init_count;
    uint32_t main_count;
    uint32_t deinit_count;
    uint32_t tick_count;
    uint32_t bridge_flags;
    uint32_t last_phase;
    uint32_t frame_accumulator;
    bool initialized;
    bool main_ran;
};

HookState g_hook_state = {0, 0, 0, 0, 0, 0, 0, false, false};

void reset_hook_state() {
    g_hook_state = HookState{0, 0, 0, 0, 0, 0, 0, false, false};
}

} // namespace

extern "C" void InitOTR(int argc, char** argv) {
    (void)argc;
    (void)argv;

    const OotPs3SohRuntimeBridgeResult bridge = oot_ps3_soh_runtime_bridge_probe_load();
    g_hook_state.init_count++;
    g_hook_state.bridge_flags = bridge.flags;
    g_hook_state.last_phase = bridge.phase;
    g_hook_state.frame_accumulator = bridge.frame_accumulator;
    g_hook_state.initialized = (bridge.flags & OOT_PS3_SOH_BRIDGE_ALL) == OOT_PS3_SOH_BRIDGE_ALL;
}

extern "C" void Main(void* arg) {
    (void)arg;

    if (!g_hook_state.initialized) {
        return;
    }

    g_hook_state.main_count++;
    g_hook_state.main_ran = true;
    for (uint32_t i = 0; i < 7U; ++i) {
        g_hook_state.tick_count++;
        g_hook_state.frame_accumulator =
            (g_hook_state.frame_accumulator << 3U) ^
            (g_hook_state.frame_accumulator >> 1U) ^
            (0x4D41494EU + g_hook_state.tick_count);
    }
}

extern "C" void DeinitOTR(void) {
    if (g_hook_state.initialized || g_hook_state.main_ran) {
        g_hook_state.deinit_count++;
    }
    g_hook_state.initialized = false;
}

OotPs3SohUpstreamHooksProbeResult oot_ps3_soh_upstream_hooks_probe_load(void) {
    OotPs3SohUpstreamHooksProbeResult result = {0, 0, 0, 0, 0, 0, 0, 0};
    char arg0[] = "oot_ps3_runtime";
    char* argv[] = {arg0, nullptr};

    reset_hook_state();
    InitOTR(1, argv);
    if (g_hook_state.initialized && g_hook_state.init_count == 1U) {
        result.flags |= OOT_PS3_SOH_HOOK_INIT_OTR;
    }

    Main(nullptr);
    if (g_hook_state.main_ran && g_hook_state.main_count == 1U) {
        result.flags |= OOT_PS3_SOH_HOOK_MAIN;
    }
    if (g_hook_state.tick_count == 7U && g_hook_state.frame_accumulator != 0U) {
        result.flags |= OOT_PS3_SOH_HOOK_FRAME_TICKS;
    }

    DeinitOTR();
    if (!g_hook_state.initialized && g_hook_state.deinit_count == 1U) {
        result.flags |= OOT_PS3_SOH_HOOK_DEINIT_OTR;
    }

    InitOTR(1, argv);
    DeinitOTR();
    if (g_hook_state.init_count == 2U && g_hook_state.deinit_count == 2U) {
        result.flags |= OOT_PS3_SOH_HOOK_REINIT;
    }

    result.init_count = g_hook_state.init_count;
    result.main_count = g_hook_state.main_count;
    result.deinit_count = g_hook_state.deinit_count;
    result.tick_count = g_hook_state.tick_count;
    result.bridge_flags = g_hook_state.bridge_flags;
    result.last_phase = g_hook_state.last_phase;
    result.frame_accumulator = g_hook_state.frame_accumulator;

    return result;
}
