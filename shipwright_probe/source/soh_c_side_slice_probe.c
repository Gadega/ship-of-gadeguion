// SPDX-License-Identifier: GPL-3.0-or-later
#include "shipwright_probe/soh_c_side_slice_probe.h"

#include "shipwright_probe/soh_resource_api_probe.h"

#include <stddef.h>
#include <string.h>

void InitOTR(int argc, char** argv);
void Main(void* arg);
void DeinitOTR(void);

static uint32_t sum_bytes(const uint8_t* bytes, uint32_t size) {
    uint32_t sum = 0U;
    for (uint32_t i = 0; i < size; ++i) {
        sum += bytes[i];
    }
    return sum;
}

OotPs3SohCSideSliceProbeResult oot_ps3_soh_c_side_slice_probe_load(void) {
    static const char kSceneName[] = "scenes/test_scene/room_0/display_list.bin";
    static const char kExpectedScene[] = "override-scene-from-second-archive\n";
    OotPs3SohCSideSliceProbeResult result = {0, 0, 0xFFFFFFFFU, 0, 0, 0xFFFFFFFFU, 0, 0, 0};
    uint8_t by_name[96] = {0};
    uint8_t by_hash[96] = {0};
    char arg0[] = "oot_ps3_runtime";
    char* argv[] = {arg0, NULL};

    InitOTR(1, argv);
    result.lifecycle_cookie ^= 0x494E4954U;

    const uint64_t scene_hash = ResourceMgr_GetNameHash(kSceneName);
    result.scene_hash_high = (uint32_t)(scene_hash >> 32U);
    result.scene_hash_low = (uint32_t)scene_hash;

    if (ResourceMgr_FileExists(kSceneName) != 0U) {
        result.flags |= OOT_PS3_SOH_C_SLICE_FILE_EXISTS;
    }

    const char* resolved = ResourceMgr_HashToString(scene_hash);
    if (resolved != NULL && strcmp(resolved, kSceneName) == 0) {
        result.flags |= OOT_PS3_SOH_C_SLICE_HASH_STRING;
    }

    result.name_size = ResourceMgr_LoadFileByName(kSceneName, by_name, (uint32_t)sizeof(by_name));
    if (result.name_size == (sizeof(kExpectedScene) - 1U) &&
        memcmp(by_name, kExpectedScene, result.name_size) == 0) {
        result.flags |= OOT_PS3_SOH_C_SLICE_LOAD_NAME;
    }

    result.hash_size = ResourceMgr_LoadFileByHash(scene_hash, by_hash, (uint32_t)sizeof(by_hash));
    if (result.hash_size == result.name_size &&
        memcmp(by_hash, kExpectedScene, result.hash_size) == 0) {
        result.flags |= OOT_PS3_SOH_C_SLICE_LOAD_HASH;
    }

    result.byte_sum = sum_bytes(by_hash, result.hash_size);

    result.game_version = ResourceMgr_GetGameVersion(0U);
    if (result.game_version == 0x78563412U) {
        result.flags |= OOT_PS3_SOH_C_SLICE_VERSION;
    }

    result.priority = ResourceMgr_GetFilePriority(scene_hash);
    if (result.priority == 1U) {
        result.flags |= OOT_PS3_SOH_C_SLICE_PRIORITY;
    }

    Main(NULL);
    result.lifecycle_cookie ^= 0x4D41494EU;
    DeinitOTR();
    result.lifecycle_cookie ^= 0x444F4E45U;
    if (result.lifecycle_cookie == (0x494E4954U ^ 0x4D41494EU ^ 0x444F4E45U)) {
        result.flags |= OOT_PS3_SOH_C_SLICE_LIFECYCLE;
    }

    return result;
}
