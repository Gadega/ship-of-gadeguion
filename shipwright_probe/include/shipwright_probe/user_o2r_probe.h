// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SHIPWRIGHT_PROBE_USER_O2R_PROBE_H
#define SHIPWRIGHT_PROBE_USER_O2R_PROBE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OOT_PS3_USER_O2R_FOUND = 1U << 0,
    OOT_PS3_USER_O2R_EOCD = 1U << 1,
    OOT_PS3_USER_O2R_INDEXED = 1U << 2,
    OOT_PS3_USER_O2R_HAS_DEFLATE = 1U << 3,
    OOT_PS3_USER_O2R_METHODS_OK = 1U << 4,
    OOT_PS3_USER_O2R_HAS_VERSION = 1U << 5,
    OOT_PS3_USER_O2R_READ_VERSION = 1U << 6,
    OOT_PS3_USER_O2R_HAS_MANIFEST = 1U << 7,
    OOT_PS3_USER_O2R_READ_MANIFEST = 1U << 8,
    OOT_PS3_USER_O2R_SAMPLED_SMALL = 1U << 9,
};

typedef struct OotPs3UserO2rProbeResult {
    uint32_t flags;
    uint32_t path_index;
    uint32_t entry_count;
    uint32_t stored_count;
    uint32_t deflated_count;
    uint32_t unsupported_count;
    uint32_t directory_count;
    uint32_t largest_uncompressed;
    uint32_t first_hash_high;
    uint32_t first_hash_low;
    uint32_t version_value;
    uint32_t manifest_size;
    uint32_t manifest_name_hash;
    uint32_t sampled_size;
    uint32_t sampled_hash_high;
    uint32_t sampled_hash_low;
} OotPs3UserO2rProbeResult;

OotPs3UserO2rProbeResult oot_ps3_user_o2r_probe_scan(void);

#ifdef __cplusplus
}
#endif

#endif
