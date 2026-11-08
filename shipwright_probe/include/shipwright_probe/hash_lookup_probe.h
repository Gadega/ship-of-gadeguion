// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SHIPWRIGHT_PROBE_HASH_LOOKUP_PROBE_H
#define SHIPWRIGHT_PROBE_HASH_LOOKUP_PROBE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OOT_PS3_HASH_LOOKUP_OPENED = 1U << 0,
    OOT_PS3_HASH_LOOKUP_HASHED = 1U << 1,
    OOT_PS3_HASH_LOOKUP_FOUND = 1U << 2,
    OOT_PS3_HASH_LOOKUP_READ = 1U << 3,
    OOT_PS3_HASH_LOOKUP_CONTENT = 1U << 4,
    OOT_PS3_HASH_LOOKUP_ALL = OOT_PS3_HASH_LOOKUP_OPENED |
                              OOT_PS3_HASH_LOOKUP_HASHED |
                              OOT_PS3_HASH_LOOKUP_FOUND |
                              OOT_PS3_HASH_LOOKUP_READ |
                              OOT_PS3_HASH_LOOKUP_CONTENT,
};

typedef struct OotPs3HashLookupProbeResult {
    uint32_t flags;
    uint32_t hash_low;
    uint32_t hash_high;
} OotPs3HashLookupProbeResult;

OotPs3HashLookupProbeResult oot_ps3_hash_lookup_probe_load(void);

#ifdef __cplusplus
}
#endif

#endif
