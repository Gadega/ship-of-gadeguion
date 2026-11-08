// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SHIPWRIGHT_PROBE_LUS_SHELL_PROBE_H
#define SHIPWRIGHT_PROBE_LUS_SHELL_PROBE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OOT_PS3_LUS_SHELL_OPENED = 1U << 0,
    OOT_PS3_LUS_SHELL_VERSION = 1U << 1,
    OOT_PS3_LUS_SHELL_MANIFEST = 1U << 2,
    OOT_PS3_LUS_SHELL_HASH_LOAD = 1U << 3,
    OOT_PS3_LUS_SHELL_PRIORITY = 1U << 4,
    OOT_PS3_LUS_SHELL_ALL = OOT_PS3_LUS_SHELL_OPENED |
                            OOT_PS3_LUS_SHELL_VERSION |
                            OOT_PS3_LUS_SHELL_MANIFEST |
                            OOT_PS3_LUS_SHELL_HASH_LOAD |
                            OOT_PS3_LUS_SHELL_PRIORITY,
};

typedef struct OotPs3LusShellProbeResult {
    uint32_t flags;
    uint32_t archive_count;
    uint32_t file_count;
    uint32_t version_value;
    uint32_t manifest_size;
    uint32_t scene_size;
    uint32_t scene_hash_high;
    uint32_t scene_hash_low;
} OotPs3LusShellProbeResult;

OotPs3LusShellProbeResult oot_ps3_lus_shell_probe_load(void);

#ifdef __cplusplus
}
#endif

#endif
