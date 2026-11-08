// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SHIPWRIGHT_PROBE_SOH_UPSTREAM_HOOKS_PROBE_H
#define SHIPWRIGHT_PROBE_SOH_UPSTREAM_HOOKS_PROBE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OOT_PS3_SOH_HOOK_INIT_OTR = 1U << 0,
    OOT_PS3_SOH_HOOK_MAIN = 1U << 1,
    OOT_PS3_SOH_HOOK_FRAME_TICKS = 1U << 2,
    OOT_PS3_SOH_HOOK_DEINIT_OTR = 1U << 3,
    OOT_PS3_SOH_HOOK_REINIT = 1U << 4,
    OOT_PS3_SOH_HOOK_ALL = OOT_PS3_SOH_HOOK_INIT_OTR |
                           OOT_PS3_SOH_HOOK_MAIN |
                           OOT_PS3_SOH_HOOK_FRAME_TICKS |
                           OOT_PS3_SOH_HOOK_DEINIT_OTR |
                           OOT_PS3_SOH_HOOK_REINIT,
};

typedef struct OotPs3SohUpstreamHooksProbeResult {
    uint32_t flags;
    uint32_t init_count;
    uint32_t main_count;
    uint32_t deinit_count;
    uint32_t tick_count;
    uint32_t bridge_flags;
    uint32_t last_phase;
    uint32_t frame_accumulator;
} OotPs3SohUpstreamHooksProbeResult;

OotPs3SohUpstreamHooksProbeResult oot_ps3_soh_upstream_hooks_probe_load(void);

#ifdef __cplusplus
}
#endif

#endif
