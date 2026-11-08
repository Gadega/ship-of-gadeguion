// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SHIPWRIGHT_PROBE_ARCHIVE_MANAGER_PROBE_H
#define SHIPWRIGHT_PROBE_ARCHIVE_MANAGER_PROBE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OOT_PS3_MANAGER_OPENED_BASE = 1U << 0,
    OOT_PS3_MANAGER_OPENED_OVERRIDE = 1U << 1,
    OOT_PS3_MANAGER_INDEXED = 1U << 2,
    OOT_PS3_MANAGER_BASE_READ = 1U << 3,
    OOT_PS3_MANAGER_OVERRIDE_READ = 1U << 4,
    OOT_PS3_MANAGER_PRIORITY = 1U << 5,
    OOT_PS3_MANAGER_ALL = OOT_PS3_MANAGER_OPENED_BASE |
                          OOT_PS3_MANAGER_OPENED_OVERRIDE |
                          OOT_PS3_MANAGER_INDEXED |
                          OOT_PS3_MANAGER_BASE_READ |
                          OOT_PS3_MANAGER_OVERRIDE_READ |
                          OOT_PS3_MANAGER_PRIORITY,
};

typedef struct OotPs3ArchiveManagerProbeResult {
    uint32_t flags;
    uint32_t archive_count;
    uint32_t indexed_count;
} OotPs3ArchiveManagerProbeResult;

OotPs3ArchiveManagerProbeResult oot_ps3_archive_manager_probe_load(void);

#ifdef __cplusplus
}
#endif

#endif
