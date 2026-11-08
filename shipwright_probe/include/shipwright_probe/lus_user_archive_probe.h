// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SHIPWRIGHT_PROBE_LUS_USER_ARCHIVE_PROBE_H
#define SHIPWRIGHT_PROBE_LUS_USER_ARCHIVE_PROBE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OOT_PS3_LUS_USER_FOUND = 1U << 0,
    OOT_PS3_LUS_USER_MOUNTED = 1U << 1,
    OOT_PS3_LUS_USER_VERSION = 1U << 2,
    OOT_PS3_LUS_USER_MANIFEST = 1U << 3,
    OOT_PS3_LUS_USER_HASH_TO_STRING = 1U << 4,
    OOT_PS3_LUS_USER_SAMPLE = 1U << 5,
};

typedef struct OotPs3LusUserArchiveProbeResult {
    uint32_t flags;
    uint32_t path_index;
    uint32_t file_count;
    uint32_t version_value;
    uint32_t manifest_size;
    uint32_t sample_size;
    uint32_t sample_hash_high;
    uint32_t sample_hash_low;
    uint32_t sample_priority;
} OotPs3LusUserArchiveProbeResult;

OotPs3LusUserArchiveProbeResult oot_ps3_lus_user_archive_probe_load(void);

#ifdef __cplusplus
}
#endif

#endif
