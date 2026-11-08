// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SHIPWRIGHT_PROBE_O2R_PROBE_H
#define SHIPWRIGHT_PROBE_O2R_PROBE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OOT_PS3_O2R_OPENED = 1U << 0,
    OOT_PS3_O2R_HEADER = 1U << 1,
    OOT_PS3_O2R_TABLE = 1U << 2,
    OOT_PS3_O2R_LOOKUP = 1U << 3,
    OOT_PS3_O2R_CRC = 1U << 4,
    OOT_PS3_O2R_CONTENT = 1U << 5,
    OOT_PS3_O2R_ALL = OOT_PS3_O2R_OPENED |
                      OOT_PS3_O2R_HEADER |
                      OOT_PS3_O2R_TABLE |
                      OOT_PS3_O2R_LOOKUP |
                      OOT_PS3_O2R_CRC |
                      OOT_PS3_O2R_CONTENT,
};

typedef struct OotPs3O2rProbeResult {
    uint32_t flags;
    uint32_t resource_count;
    uint32_t payload_checksum;
} OotPs3O2rProbeResult;

OotPs3O2rProbeResult oot_ps3_o2r_probe_load(void);

#ifdef __cplusplus
}
#endif

#endif
