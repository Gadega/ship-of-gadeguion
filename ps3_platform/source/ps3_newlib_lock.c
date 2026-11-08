// SPDX-License-Identifier: GPL-3.0-or-later
// Constructor-free replacement for PS3 newlib's lock_internal.o.

#include <stddef.h>
#include <lv2/spinlock.h>
#include <sys/mutex.h>

/* BSS zero is the unlocked representation used by PSL1GHT spinlocks.  This
 * avoids newlib's priority-105 metaLock constructor, which aborts during this
 * large multi-TOC executable's startup on HEN. */
static s32 sLockInitGuard;

void __libc_auto_lock_allocate(sys_lwmutex_t* mutex) {
    static const sys_lwmutex_attr_t attributes = {
        SYS_LWMUTEX_PROTOCOL_PRIO,
        SYS_LWMUTEX_ATTR_RECURSIVE,
        "",
    };

    if (mutex == NULL) {
        return;
    }

    sysSpinlockLock(&sLockInitGuard);
    if (mutex->attribute == 0U) {
        /* If creation fails, leave the structure uninitialized.  The caller's
         * normal lock operation will return the LV2 error instead of aborting
         * the entire process before main. */
        (void)sysLwMutexCreate(mutex, &attributes);
    }
    sysSpinlockUnlock(&sLockInitGuard);
}
