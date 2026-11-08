// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SHIPWRIGHT_PROBE_SOH_GFX_SOFT_BRIDGE_PROBE_H
#define SHIPWRIGHT_PROBE_SOH_GFX_SOFT_BRIDGE_PROBE_H

#include <stdint.h>

#include "ps3_platform/rsx_screen.h"
#include "shipwright_probe/resource_bootstrap_probe.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OOT_PS3_SOH_GFX_SOFT_QUEUE = 1U << 0,
    OOT_PS3_SOH_GFX_SOFT_CLEAR = 1U << 1,
    OOT_PS3_SOH_GFX_SOFT_RECT = 1U << 2,
    OOT_PS3_SOH_GFX_SOFT_PRESENT = 1U << 3,
    OOT_PS3_SOH_GFX_SOFT_CHECKSUM = 1U << 4,
    OOT_PS3_SOH_GFX_SOFT_ALL = OOT_PS3_SOH_GFX_SOFT_QUEUE |
                               OOT_PS3_SOH_GFX_SOFT_CLEAR |
                               OOT_PS3_SOH_GFX_SOFT_RECT |
                               OOT_PS3_SOH_GFX_SOFT_PRESENT |
                               OOT_PS3_SOH_GFX_SOFT_CHECKSUM,
};

typedef struct OotPs3SohGfxSoftBridgeProbeResult {
    uint32_t flags;
    uint32_t command_count;
    uint32_t queue_checksum;
    uint32_t framebuffer_checksum;
    uint32_t clear_pixels;
    uint32_t rect_pixels;
    uint32_t present_count;
} OotPs3SohGfxSoftBridgeProbeResult;

OotPs3SohGfxSoftBridgeProbeResult oot_ps3_soh_gfx_soft_bridge_probe_load(void);
void oot_ps3_soh_gfx_soft_bridge_draw_demo(OotPs3DisplayBuffer* buffer, uint32_t frame,
                                           int32_t x, int32_t y, uint32_t scale);
void oot_ps3_soh_gfx_soft_bridge_draw_resource_demo(OotPs3DisplayBuffer* buffer, uint32_t frame,
                                                    int32_t x, int32_t y, uint32_t scale,
                                                    const OotPs3ResourceBootstrapProbeResult* resources);

#ifdef __cplusplus
}
#endif

#endif
