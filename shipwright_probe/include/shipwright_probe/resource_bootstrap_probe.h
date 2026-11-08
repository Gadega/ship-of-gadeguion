// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SHIPWRIGHT_PROBE_RESOURCE_BOOTSTRAP_PROBE_H
#define SHIPWRIGHT_PROBE_RESOURCE_BOOTSTRAP_PROBE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OOT_PS3_RESOURCE_BOOT_MOUNTED = 1U << 0,
    OOT_PS3_RESOURCE_BOOT_VERSION = 1U << 1,
    OOT_PS3_RESOURCE_BOOT_MANIFEST = 1U << 2,
    OOT_PS3_RESOURCE_BOOT_SCENE_HASH = 1U << 3,
    OOT_PS3_RESOURCE_BOOT_HASH_STRING = 1U << 4,
    OOT_PS3_RESOURCE_BOOT_PRIORITY = 1U << 5,
    OOT_PS3_RESOURCE_BOOT_ALL = OOT_PS3_RESOURCE_BOOT_MOUNTED |
                                OOT_PS3_RESOURCE_BOOT_VERSION |
                                OOT_PS3_RESOURCE_BOOT_MANIFEST |
                                OOT_PS3_RESOURCE_BOOT_SCENE_HASH |
                                OOT_PS3_RESOURCE_BOOT_HASH_STRING |
                                OOT_PS3_RESOURCE_BOOT_PRIORITY,
};

typedef struct OotPs3ResourceBootstrapProbeResult {
    uint32_t flags;
    uint32_t archive_count;
    uint32_t file_count;
    uint32_t version_value;
    uint32_t manifest_size;
    uint32_t scene_size;
    uint32_t scene_priority;
    uint32_t scene_hash_high;
    uint32_t scene_hash_low;
} OotPs3ResourceBootstrapProbeResult;

OotPs3ResourceBootstrapProbeResult oot_ps3_resource_bootstrap_probe_load(void);

#ifdef __cplusplus
}
#endif

#endif
