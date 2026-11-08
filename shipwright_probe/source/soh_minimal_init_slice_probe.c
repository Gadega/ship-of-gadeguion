// SPDX-License-Identifier: GPL-3.0-or-later
#include "shipwright_probe/soh_minimal_init_slice_probe.h"

#include "shipwright_probe/soh_upstream_compat.h"

#include <stddef.h>
#include <string.h>

int32_t gScreenWidth = 320;
int32_t gScreenHeight = 240;
size_t gSystemHeapSize = 0;

typedef struct OotPs3MinimalGameState {
    uint32_t initialized;
    uint32_t frame_count;
    uint32_t resource_sum;
    uint32_t shutdown;
} OotPs3MinimalGameState;

static uint32_t sum_bytes(const uint8_t* bytes, uint32_t size) {
    uint32_t sum = 0U;
    for (uint32_t i = 0; i < size; ++i) {
        sum += bytes[i];
    }
    return sum;
}

static void soh_minimal_globals_init(void) {
    gScreenWidth = OOT_PS3_UPSTREAM_SCREEN_WIDTH;
    gScreenHeight = OOT_PS3_UPSTREAM_SCREEN_HEIGHT;
    gSystemHeapSize = OOT_PS3_UPSTREAM_SYSTEM_HEAP_SIZE;
}

static uint32_t soh_minimal_load_startup_scene(uint32_t* hash_high, uint32_t* hash_low, uint32_t* sum) {
    static const char kSceneName[] = "scenes/test_scene/room_0/display_list.bin";
    uint8_t scene[96] = {0};
    const uint64_t scene_hash = ResourceMgr_GetNameHash(kSceneName);
    const uint32_t size = ResourceMgr_LoadFileByHash(scene_hash, scene, (uint32_t)sizeof(scene));

    if (hash_high != NULL) {
        *hash_high = (uint32_t)(scene_hash >> 32U);
    }
    if (hash_low != NULL) {
        *hash_low = (uint32_t)scene_hash;
    }
    if (sum != NULL) {
        *sum = sum_bytes(scene, size);
    }
    return size;
}

static void soh_minimal_frame(OotPs3MinimalGameState* state) {
    if (state == NULL || state->initialized == 0U) {
        return;
    }
    state->frame_count++;
    Main(NULL);
}

OotPs3SohMinimalInitSliceProbeResult oot_ps3_soh_minimal_init_slice_probe_load(void) {
    OotPs3SohMinimalInitSliceProbeResult result = {0, 0, 0, 0, 0, 0, 0, 0xFFFFFFFFU, 0, 0};
    OotPs3MinimalGameState state = {0, 0, 0, 0};
    char arg0[] = "oot_ps3_runtime";
    char* argv[] = {arg0, NULL};

    soh_minimal_globals_init();
    result.screen_width = (uint32_t)gScreenWidth;
    result.screen_height = (uint32_t)gScreenHeight;
    result.heap_size = (uint32_t)gSystemHeapSize;
    if (gScreenWidth == OOT_PS3_UPSTREAM_SCREEN_WIDTH && gScreenHeight == OOT_PS3_UPSTREAM_SCREEN_HEIGHT) {
        result.flags |= OOT_PS3_SOH_INIT_SLICE_GLOBALS;
    }
    if (gSystemHeapSize == OOT_PS3_UPSTREAM_SYSTEM_HEAP_SIZE) {
        result.flags |= OOT_PS3_SOH_INIT_SLICE_HEAP;
    }

    InitOTR(1, argv);
    state.initialized = 1U;
    if (state.initialized != 0U) {
        result.flags |= OOT_PS3_SOH_INIT_SLICE_LIFECYCLE;
    }

    result.resource_size =
        soh_minimal_load_startup_scene(&result.scene_hash_high, &result.scene_hash_low, &state.resource_sum);
    result.resource_sum = state.resource_sum;
    result.game_version = ResourceMgr_GetGameVersion(0U);
    if (result.resource_size == 35U && result.resource_sum == 3390U && result.game_version == 0x78563412U) {
        result.flags |= OOT_PS3_SOH_INIT_SLICE_RESOURCE;
    }

    soh_minimal_frame(&state);
    soh_minimal_frame(&state);
    result.frame_count = state.frame_count;
    if (state.frame_count == 2U) {
        result.flags |= OOT_PS3_SOH_INIT_SLICE_FRAME;
    }

    DeinitOTR();
    state.shutdown = 1U;
    if (state.shutdown != 0U) {
        result.flags |= OOT_PS3_SOH_INIT_SLICE_SHUTDOWN;
    }

    return result;
}
