// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SHIPWRIGHT_PROBE_SOH_RESOURCE_API_PROBE_H
#define SHIPWRIGHT_PROBE_SOH_RESOURCE_API_PROBE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OOT_PS3_SOH_RESOURCE_API_FILE_EXISTS = 1U << 0,
    OOT_PS3_SOH_RESOURCE_API_HASH_RESOLVE = 1U << 1,
    OOT_PS3_SOH_RESOURCE_API_LOAD_BY_NAME = 1U << 2,
    OOT_PS3_SOH_RESOURCE_API_LOAD_BY_HASH = 1U << 3,
    OOT_PS3_SOH_RESOURCE_API_GAME_VERSION = 1U << 4,
    OOT_PS3_SOH_RESOURCE_API_PRIORITY = 1U << 5,
    OOT_PS3_SOH_RESOURCE_API_HOOK_COMPAT = 1U << 6,
    OOT_PS3_SOH_RESOURCE_API_ALL = OOT_PS3_SOH_RESOURCE_API_FILE_EXISTS |
                                   OOT_PS3_SOH_RESOURCE_API_HASH_RESOLVE |
                                   OOT_PS3_SOH_RESOURCE_API_LOAD_BY_NAME |
                                   OOT_PS3_SOH_RESOURCE_API_LOAD_BY_HASH |
                                   OOT_PS3_SOH_RESOURCE_API_GAME_VERSION |
                                   OOT_PS3_SOH_RESOURCE_API_PRIORITY |
                                   OOT_PS3_SOH_RESOURCE_API_HOOK_COMPAT,
};

typedef struct OotPs3SohResourceApiProbeResult {
    uint32_t flags;
    uint32_t game_version;
    uint32_t name_size;
    uint32_t hash_size;
    uint32_t priority;
    uint32_t hook_flags;
    uint32_t scene_hash_high;
    uint32_t scene_hash_low;
} OotPs3SohResourceApiProbeResult;

uint8_t ResourceMgr_FileExists(const char* file_path);
uint32_t ResourceMgr_GetGameVersion(uint32_t index);
uint64_t ResourceMgr_GetNameHash(const char* file_path);
const char* ResourceMgr_HashToString(uint64_t hash);
uint32_t ResourceMgr_GetFilePriority(uint64_t hash);
uint32_t ResourceMgr_LoadFileByName(const char* file_path, uint8_t* output, uint32_t capacity);
uint32_t ResourceMgr_LoadFileByHash(uint64_t hash, uint8_t* output, uint32_t capacity);

OotPs3SohResourceApiProbeResult oot_ps3_soh_resource_api_probe_load(void);

#ifdef __cplusplus
}
#endif

#endif
