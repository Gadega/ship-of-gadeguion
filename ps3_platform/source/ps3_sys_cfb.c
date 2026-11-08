// SPDX-License-Identifier: GPL-3.0-or-later
// Native PS3 backing storage for OoT's two CPU-side color framebuffers.

#include <stddef.h>
#include <stdint.h>
#include <sys/memory.h>

#define OOT_CFB_WIDTH 320U
#define OOT_CFB_HEIGHT 240U
#define OOT_CFB_BYTES_PER_PIXEL 4U
#define OOT_CFB_BYTES (OOT_CFB_WIDTH * OOT_CFB_HEIGHT * OOT_CFB_BYTES_PER_PIXEL)
#define OOT_CFB_REGION_SIZE (1U * 1024U * 1024U)

extern void oot_ps3_early_trace(const char* message);
extern void FB_CreateFramebuffers(void);

uintptr_t sSysCfbFbPtr[2];
uintptr_t sSysCfbEnd;

static sys_mem_addr_t sCfbRegionAddress;

void SysCfb_Init(int32_t n64dd) {
    sys_mem_addr_t address = 0;
    unsigned char* base;
    size_t i;

    (void)n64dd;
    oot_ps3_early_trace("syscfb: requesting native 1 MiB LV2 block");
    if (sysMemoryAllocate(OOT_CFB_REGION_SIZE, SYS_MEMORY_PAGE_SIZE_1M, &address) != 0) {
        sSysCfbFbPtr[0] = 0;
        sSysCfbFbPtr[1] = 0;
        sSysCfbEnd = 0;
        oot_ps3_early_trace("syscfb: native LV2 allocation FAILED");
        return;
    }

    sCfbRegionAddress = address;
    base = (unsigned char*)(uintptr_t)address;
    for (i = 0; i < OOT_CFB_BYTES * 2U; ++i) {
        base[i] = 0;
    }

    sSysCfbFbPtr[0] = (uintptr_t)base;
    sSysCfbFbPtr[1] = (uintptr_t)(base + OOT_CFB_BYTES);

    // Main() uses this as the beginning of its 64 KiB debug arena.  The rest
    // of the 1 MiB region leaves substantially more than that after both CFBs.
    sSysCfbEnd = (uintptr_t)(base + (OOT_CFB_BYTES * 2U));
    oot_ps3_early_trace("syscfb: native color buffers ready");

    FB_CreateFramebuffers();
    oot_ps3_early_trace("syscfb: logical framebuffer handles ready");
}

void SysCfb_Reset(void) {
    if (sCfbRegionAddress != 0) {
        (void)sysMemoryFree(sCfbRegionAddress);
        sCfbRegionAddress = 0;
    }
    sSysCfbFbPtr[0] = 0;
    sSysCfbFbPtr[1] = 0;
    sSysCfbEnd = 0;
}

uintptr_t SysCfb_GetFbPtr(int32_t index) {
    return (index >= 0 && index < 2) ? sSysCfbFbPtr[index] : 0;
}

uintptr_t SysCfb_GetFbEnd(void) {
    return sSysCfbEnd;
}
