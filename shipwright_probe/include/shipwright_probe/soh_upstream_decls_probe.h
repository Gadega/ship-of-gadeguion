// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SHIPWRIGHT_PROBE_SOH_UPSTREAM_DECLS_PROBE_H
#define SHIPWRIGHT_PROBE_SOH_UPSTREAM_DECLS_PROBE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OOT_PS3_SOH_DECLS_CONSTANTS = 1U << 0,
    OOT_PS3_SOH_DECLS_GLOBALS = 1U << 1,
    OOT_PS3_SOH_DECLS_LIFECYCLE = 1U << 2,
    OOT_PS3_SOH_DECLS_RESOURCE_API = 1U << 3,
    OOT_PS3_SOH_DECLS_INIT_SLICE = 1U << 4,
    OOT_PS3_SOH_DECLS_ALL = OOT_PS3_SOH_DECLS_CONSTANTS |
                            OOT_PS3_SOH_DECLS_GLOBALS |
                            OOT_PS3_SOH_DECLS_LIFECYCLE |
                            OOT_PS3_SOH_DECLS_RESOURCE_API |
                            OOT_PS3_SOH_DECLS_INIT_SLICE,
};

typedef struct OotPs3SohUpstreamDeclsProbeResult {
    uint32_t flags;
    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t heap_size;
    uint32_t init_slice_flags;
    uint32_t game_version;
    uint32_t scene_hash_high;
    uint32_t scene_hash_low;
} OotPs3SohUpstreamDeclsProbeResult;

OotPs3SohUpstreamDeclsProbeResult oot_ps3_soh_upstream_decls_probe_load(void);

#ifdef __cplusplus
}
#endif

#endif
