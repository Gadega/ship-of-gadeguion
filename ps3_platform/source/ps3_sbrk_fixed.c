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
#include <sys/memory.h>
#include <sys/reent.h>
#include <sys/types.h>

#define OOT_PS3_BOOT_HEAP_SIZE (64U * 1024U * 1024U)

static unsigned char* sHeapBase;
static ptrdiff_t sHeapOffset;
static unsigned int sHeapRefusals; /* sbrk calls refused because the 64 MiB block is used up */

/* Heap usage for the heap-guard report (sbrk runs under malloc's lock: it can't trace). */
unsigned int oot_ps3_heap_used_kb(void) { return (unsigned int)(sHeapOffset / 1024); }
unsigned int oot_ps3_heap_refusals(void) { return sHeapRefusals; }

caddr_t __librt_sbrk_r(struct _reent* reent, ptrdiff_t increment) {
    ptrdiff_t next_offset;
    unsigned char* previous;

    if (sHeapBase == NULL) {
        sys_mem_addr_t address = 0;
        if (sysMemoryAllocate(OOT_PS3_BOOT_HEAP_SIZE,
                              SYS_MEMORY_PAGE_SIZE_1M, &address) != 0) {
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
