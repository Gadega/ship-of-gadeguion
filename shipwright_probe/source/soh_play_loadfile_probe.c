// SPDX-License-Identifier: GPL-3.0-or-later
#include "shipwright_probe/soh_play_loadfile_probe.h"

#include "shipwright_probe/soh_upstream_compat.h"

#include <stddef.h>
#include <string.h>

static uint8_t g_play_load_buffer[128];

static uint32_t sum_bytes(const uint8_t* bytes, uint32_t size) {
    uint32_t sum = 0U;
    for (uint32_t i = 0; i < size; ++i) {
        sum += bytes[i];
    }
    return sum;
}

void* Play_LoadFile(PlayState* play, RomFile* file) {
    if (play == NULL || file == NULL || file->name == NULL) {
        return NULL;
    }

    const uint64_t hash = ResourceMgr_GetNameHash(file->name);
    const uint32_t size = ResourceMgr_LoadFileByHash(hash, g_play_load_buffer, (uint32_t)sizeof(g_play_load_buffer));
    if (size == 0U) {
        return NULL;
    }

    play->loadedResourceSize = size;
    play->loadedResourceSum = sum_bytes(g_play_load_buffer, size);
    play->loadedResourceHash = hash;
    return g_play_load_buffer;
}

OotPs3SohPlayLoadFileProbeResult oot_ps3_soh_play_loadfile_probe_load(void) {
    static const char kSceneName[] = "scenes/test_scene/room_0/display_list.bin";
    static const char kExpectedScene[] = "override-scene-from-second-archive\n";
    OotPs3SohPlayLoadFileProbeResult result = {0, 0, 0, 0, 0xFFFFFFFFU, 0, 0};
    PlayState play = {0, 0, 0, 0};
    RomFile scene_file = {kSceneName, 0, sizeof(kExpectedScene) - 1U};
    char arg0[] = "oot_ps3_runtime";
    char* argv[] = {arg0, NULL};

    if (scene_file.name != NULL && scene_file.vromEnd > scene_file.vromStart) {
        result.flags |= OOT_PS3_SOH_PLAY_LOAD_STRUCTS;
    }

    InitOTR(1, argv);
    Main(&play);
    play.frameCount++;
    if (play.frameCount == 1U) {
        result.flags |= OOT_PS3_SOH_PLAY_LOAD_LIFECYCLE;
    }

    const uint8_t* loaded = (const uint8_t*)Play_LoadFile(&play, &scene_file);
    if (loaded != NULL && play.loadedResourceSize == sizeof(kExpectedScene) - 1U &&
        memcmp(loaded, kExpectedScene, play.loadedResourceSize) == 0) {
        result.flags |= OOT_PS3_SOH_PLAY_LOAD_FILE;
    }

    result.frame_count = play.frameCount;
    result.file_size = play.loadedResourceSize;
    result.byte_sum = play.loadedResourceSum;
    result.scene_hash_high = (uint32_t)(play.loadedResourceHash >> 32U);
    result.scene_hash_low = (uint32_t)play.loadedResourceHash;
    if (play.loadedResourceSum == 3390U && play.loadedResourceHash != 0U) {
        result.flags |= OOT_PS3_SOH_PLAY_LOAD_STATE;
    }

    result.game_version = ResourceMgr_GetGameVersion(0U);
    if (result.game_version == 0x78563412U) {
        result.flags |= OOT_PS3_SOH_PLAY_LOAD_VERSION;
    }

    DeinitOTR();
    return result;
}
