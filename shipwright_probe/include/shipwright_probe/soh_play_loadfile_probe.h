// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SHIPWRIGHT_PROBE_SOH_PLAY_LOADFILE_PROBE_H
#define SHIPWRIGHT_PROBE_SOH_PLAY_LOADFILE_PROBE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OOT_PS3_SOH_PLAY_LOAD_STRUCTS = 1U << 0,
    OOT_PS3_SOH_PLAY_LOAD_LIFECYCLE = 1U << 1,
    OOT_PS3_SOH_PLAY_LOAD_FILE = 1U << 2,
    OOT_PS3_SOH_PLAY_LOAD_STATE = 1U << 3,
    OOT_PS3_SOH_PLAY_LOAD_VERSION = 1U << 4,
    OOT_PS3_SOH_PLAY_LOAD_ALL = OOT_PS3_SOH_PLAY_LOAD_STRUCTS |
                                OOT_PS3_SOH_PLAY_LOAD_LIFECYCLE |
                                OOT_PS3_SOH_PLAY_LOAD_FILE |
                                OOT_PS3_SOH_PLAY_LOAD_STATE |
                                OOT_PS3_SOH_PLAY_LOAD_VERSION,
};

typedef struct OotPs3SohPlayLoadFileProbeResult {
    uint32_t flags;
    uint32_t frame_count;
    uint32_t file_size;
    uint32_t byte_sum;
    uint32_t game_version;
    uint32_t scene_hash_high;
    uint32_t scene_hash_low;
} OotPs3SohPlayLoadFileProbeResult;

OotPs3SohPlayLoadFileProbeResult oot_ps3_soh_play_loadfile_probe_load(void);

#ifdef __cplusplus
}
#endif

#endif
