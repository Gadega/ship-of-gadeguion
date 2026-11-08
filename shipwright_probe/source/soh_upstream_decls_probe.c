// SPDX-License-Identifier: GPL-3.0-or-later
#include "shipwright_probe/soh_upstream_decls_probe.h"

#include "shipwright_probe/soh_minimal_init_slice_probe.h"
#include "shipwright_probe/soh_upstream_compat.h"

#include <stddef.h>

OotPs3SohUpstreamDeclsProbeResult oot_ps3_soh_upstream_decls_probe_load(void) {
    static const char kSceneName[] = "scenes/test_scene/room_0/display_list.bin";
    OotPs3SohUpstreamDeclsProbeResult result = {0, 0, 0, 0, 0, 0xFFFFFFFFU, 0, 0};
    char arg0[] = "oot_ps3_runtime";
    char* argv[] = {arg0, NULL};

    if (OOT_PS3_UPSTREAM_SCREEN_WIDTH == 320 &&
        OOT_PS3_UPSTREAM_SCREEN_HEIGHT == 240 &&
        OOT_PS3_UPSTREAM_SYSTEM_HEAP_SIZE == 4U * 1024U * 1024U) {
        result.flags |= OOT_PS3_SOH_DECLS_CONSTANTS;
    }

    gScreenWidth = OOT_PS3_UPSTREAM_SCREEN_WIDTH;
    gScreenHeight = OOT_PS3_UPSTREAM_SCREEN_HEIGHT;
    gSystemHeapSize = OOT_PS3_UPSTREAM_SYSTEM_HEAP_SIZE;
    result.screen_width = (uint32_t)gScreenWidth;
    result.screen_height = (uint32_t)gScreenHeight;
    result.heap_size = (uint32_t)gSystemHeapSize;
    if (result.screen_width == 320U && result.screen_height == 240U && result.heap_size == 4194304U) {
        result.flags |= OOT_PS3_SOH_DECLS_GLOBALS;
    }

    InitOTR(1, argv);
    Main(NULL);
    DeinitOTR();
    result.flags |= OOT_PS3_SOH_DECLS_LIFECYCLE;

    const uint64_t scene_hash = ResourceMgr_GetNameHash(kSceneName);
    result.scene_hash_high = (uint32_t)(scene_hash >> 32U);
    result.scene_hash_low = (uint32_t)scene_hash;
    result.game_version = ResourceMgr_GetGameVersion(0U);
    if (ResourceMgr_FileExists(kSceneName) != 0U &&
        ResourceMgr_HashToString(scene_hash) != NULL &&
        result.game_version == 0x78563412U) {
        result.flags |= OOT_PS3_SOH_DECLS_RESOURCE_API;
    }

    const OotPs3SohMinimalInitSliceProbeResult init_slice = oot_ps3_soh_minimal_init_slice_probe_load();
    result.init_slice_flags = init_slice.flags;
    if ((init_slice.flags & OOT_PS3_SOH_INIT_SLICE_ALL) == OOT_PS3_SOH_INIT_SLICE_ALL) {
        result.flags |= OOT_PS3_SOH_DECLS_INIT_SLICE;
    }

    return result;
}
