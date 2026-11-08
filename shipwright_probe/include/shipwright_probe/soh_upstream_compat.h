// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SHIPWRIGHT_PROBE_SOH_UPSTREAM_COMPAT_H
#define SHIPWRIGHT_PROBE_SOH_UPSTREAM_COMPAT_H

#include <stddef.h>
#include <stdint.h>

#include "shipwright_probe/soh_resource_api_probe.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    OOT_PS3_UPSTREAM_SCREEN_WIDTH = 320,
    OOT_PS3_UPSTREAM_SCREEN_HEIGHT = 240,
    OOT_PS3_UPSTREAM_SYSTEM_HEAP_SIZE = 4U * 1024U * 1024U,
};

extern int32_t gScreenWidth;
extern int32_t gScreenHeight;
extern size_t gSystemHeapSize;

typedef struct RomFile {
    const char* name;
    uint32_t vromStart;
    uint32_t vromEnd;
} RomFile;

typedef struct PlayState {
    uint32_t frameCount;
    uint32_t loadedResourceSize;
    uint32_t loadedResourceSum;
    uint64_t loadedResourceHash;
} PlayState;

typedef enum OotPs3GfxCommandType {
    OOT_PS3_GFX_CMD_CLEAR = 1,
    OOT_PS3_GFX_CMD_RECT = 2,
    OOT_PS3_GFX_CMD_PRESENT = 3,
} OotPs3GfxCommandType;

typedef struct OotPs3GfxCommand {
    uint32_t type;
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t color;
} OotPs3GfxCommand;

typedef struct OotPs3GfxQueue {
    OotPs3GfxCommand commands[16];
    uint32_t count;
    uint32_t checksum;
} OotPs3GfxQueue;

void InitOTR(int argc, char** argv);
void Main(void* arg);
void DeinitOTR(void);
void* Play_LoadFile(PlayState* play, RomFile* file);
void Graph_StartFrame(void);
void Graph_ProcessGfxCommands(OotPs3GfxQueue* queue);

#ifdef __cplusplus
}
#endif

#endif
