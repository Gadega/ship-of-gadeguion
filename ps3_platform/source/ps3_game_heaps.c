// SPDX-License-Identifier: GPL-3.0-or-later
// Native backing storage for OoT's audio and system heap managers.

#include <stddef.h>
#include <stdint.h>
#include <sys/memory.h>

#define OOT_AUDIO_REGION_SIZE (4U * 1024U * 1024U)
#define OOT_SYSTEM_REGION_SIZE (4U * 1024U * 1024U)
#define OOT_GAME_REGIONS_SIZE (OOT_AUDIO_REGION_SIZE + OOT_SYSTEM_REGION_SIZE)

extern void oot_ps3_early_trace(const char* message);

unsigned char* gAudioHeap;
unsigned char* gSystemHeap;

static sys_mem_addr_t sGameRegionsAddress;

void Heaps_Alloc(void) {
    sys_mem_addr_t address = 0;

    oot_ps3_early_trace("heaps: requesting native 8 MiB LV2 block");
    if (sysMemoryAllocate(OOT_GAME_REGIONS_SIZE, SYS_MEMORY_PAGE_SIZE_1M, &address) != 0) {
        gAudioHeap = NULL;
        gSystemHeap = NULL;
        oot_ps3_early_trace("heaps: native LV2 allocation FAILED");
        return;
    }

    sGameRegionsAddress = address;
    gAudioHeap = (unsigned char*)(uintptr_t)address;
    gSystemHeap = gAudioHeap + OOT_AUDIO_REGION_SIZE;
    oot_ps3_early_trace("heaps: native audio/system regions ready");
}

void Heaps_Free(void) {
    if (sGameRegionsAddress != 0) {
        (void)sysMemoryFree(sGameRegionsAddress);
        sGameRegionsAddress = 0;
    }
    gAudioHeap = NULL;
    gSystemHeap = NULL;
}
