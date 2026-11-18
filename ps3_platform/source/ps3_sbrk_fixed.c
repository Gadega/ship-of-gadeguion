// SPDX-License-Identifier: GPL-3.0-or-later
// PS3 Shipwright bootstrap heap.
//
// PSL1GHT's default sbrk constructor reserves a 256 MiB mapper window before
// main.  The native Shipwright image crashes inside that constructor on HEN.
// This replacement supplies the symbol expected by crt1 without requiring a
// constructor: it lazily allocates one contiguous 64 MiB LV2 memory block and
// implements the traditional monotonically growing sbrk interface within it.

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <ppu-lv2.h>
#include <sys/memory.h>
#include <sys/reent.h>
#include <sys/types.h>

/* The heap is sized at the first sbrk from the user memory LV2 still has free:
 * everything except a reserve for what is allocated later (archive RAM copy <= 48 MB,
 * game heaps 8, framebuffer 1, save data container 5, save-state slots 3 x 9,
 * thread stacks / SPU / system modules). Never less than the old fixed 64 MiB. */
#define OOT_PS3_HEAP_MIN (64U * 1024U * 1024U)
#define OOT_PS3_HEAP_MAX (128U * 1024U * 1024U)
#define OOT_PS3_HEAP_RESERVE (112U * 1024U * 1024U)

static unsigned int sHeapSize = OOT_PS3_HEAP_MIN;
static unsigned int sUserMemTotal, sUserMemFreeAtStart;
#define OOT_PS3_BOOT_HEAP_SIZE sHeapSize

typedef struct { uint32_t total; uint32_t available; } Ps3UserMemoryInfo;
static int Ps3GetUserMemory(Ps3UserMemoryInfo* info) {
    lv2syscall1(352, (u64)(uintptr_t)info);
    return_to_user_prog(int);
}
/* Free LV2 user memory now (KB), 0 if the query fails. */
unsigned int oot_ps3_system_free_kb(void) {
    Ps3UserMemoryInfo info __attribute__((aligned(8))) = { 0, 0 };
    if (Ps3GetUserMemory(&info) != 0) return 0;
    return info.available / 1024U;
}
unsigned int oot_ps3_heap_size_kb(void) { return sHeapSize / 1024U; }
unsigned int oot_ps3_system_total_kb(void) { return sUserMemTotal / 1024U; }
unsigned int oot_ps3_system_free_at_start_kb(void) { return sUserMemFreeAtStart / 1024U; }

static unsigned char* sHeapBase;
static ptrdiff_t sHeapOffset;
static unsigned int sHeapRefusals; /* sbrk calls refused because the heap block is used up */

/* Heap usage for the heap-guard report (sbrk runs under malloc's lock: it can't trace). */
unsigned int oot_ps3_heap_used_kb(void) { return (unsigned int)(sHeapOffset / 1024); }
unsigned int oot_ps3_heap_refusals(void) { return sHeapRefusals; }

caddr_t __librt_sbrk_r(struct _reent* reent, ptrdiff_t increment) {
    ptrdiff_t next_offset;
    unsigned char* previous;

    if (sHeapBase == NULL) {
        sys_mem_addr_t address = 0;
        Ps3UserMemoryInfo info __attribute__((aligned(8))) = { 0, 0 };
        if (Ps3GetUserMemory(&info) == 0 && info.available > OOT_PS3_HEAP_RESERVE) {
            unsigned int size = (info.available - OOT_PS3_HEAP_RESERVE) & ~0xFFFFFU;
            if (size > OOT_PS3_HEAP_MAX) size = OOT_PS3_HEAP_MAX;
            if (size > OOT_PS3_HEAP_MIN) sHeapSize = size;
            sUserMemTotal = info.total;
            sUserMemFreeAtStart = info.available;
        }
        if (sysMemoryAllocate(OOT_PS3_BOOT_HEAP_SIZE, SYS_MEMORY_PAGE_SIZE_1M, &address) != 0) {
            /* bigger block refused: fall back to the old fixed size */
            sHeapSize = OOT_PS3_HEAP_MIN;
            address = 0;
        }
        if (address == 0 && sysMemoryAllocate(OOT_PS3_BOOT_HEAP_SIZE, SYS_MEMORY_PAGE_SIZE_1M, &address) != 0) {
            if (reent != NULL) {
                reent->_errno = ENOMEM;
            }
            return (caddr_t)-1;
        }
        sHeapBase = (unsigned char*)(uintptr_t)address;
        sHeapOffset = 0;
    }

    if ((increment > 0 && sHeapOffset > (ptrdiff_t)OOT_PS3_BOOT_HEAP_SIZE - increment) ||
        (increment < 0 && sHeapOffset < -increment)) {
        ++sHeapRefusals;
        if (reent != NULL) {
            reent->_errno = ENOMEM;
        }
        return (caddr_t)-1;
    }

    next_offset = sHeapOffset + increment;
    if (next_offset < 0 || next_offset > (ptrdiff_t)OOT_PS3_BOOT_HEAP_SIZE) {
        if (reent != NULL) {
            reent->_errno = ENOMEM;
        }
        return (caddr_t)-1;
    }

    previous = sHeapBase + sHeapOffset;
    sHeapOffset = next_offset;
    return (caddr_t)previous;
}

/* For save states (ps3_reloc.h): where malloc's memory lives. */
void oot_ps3_heap_range(uintptr_t* base, size_t* size) {
    if (base != NULL) *base = (uintptr_t)sHeapBase;
    if (size != NULL) *size = sHeapBase != NULL ? (size_t)OOT_PS3_BOOT_HEAP_SIZE : 0;
}
