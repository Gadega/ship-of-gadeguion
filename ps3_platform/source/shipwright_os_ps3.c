// SPDX-License-Identifier: GPL-3.0-or-later
// Native PS3 implementation of the libultra services used by Shipwright's core.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "libultraship/libultra/controller.h"
#include "libultraship/libultra/convert.h"
#include "libultraship/libultra/internal.h"
#include "libultraship/libultra/message.h"
#include "libultraship/libultra/os.h"
#include "libultraship/libultra/pfs.h"
#include "libultraship/libultra/thread.h"

// libultraship exposes BTN_START as an N64 mask while padData uses this name
// as a field. The game-side code uses START_BUTTON, so keep the field visible.
#undef BTN_START

// PSL1GHT and libultraship both publish the short u8/u64 type names. Keep the
// SDK declarations isolated to this translation unit instead of injecting
// ppu-types.h into all 696 game sources (where it also defines quad -> quad_t).
typedef u64 sys_ppu_thread_t;
#define __PPU_TYPES_H__
#include <io/pad.h>
#include <lv2/cond.h>
#include <lv2/mutex.h>
#include <lv2/thread.h>
#include <sys/systime.h>
#include <sys/thread.h>

#define PS3_QUEUE_SLOTS 96
#define PS3_THREAD_SLOTS 32
#define PS3_TIMER_SLOTS 32
#define PS3_THREAD_STACK_SIZE (64U * 1024U)

typedef struct {
    OSMesgQueue* queue;
    volatile int lock;
    int initialized;
} Ps3QueueSlot;

typedef struct {
    OSThread* thread;
    void (*entry)(void*);
    void* argument;
    sys_ppu_thread_t native;
    int started;
    int joined;
    int completed;
} Ps3ThreadSlot;

typedef struct {
    OSTimer* timer;
    OSTime delay;
    OSTime interval;
    OSMesgQueue* queue;
    OSMesg message;
    sys_ppu_thread_t native;
    int active;
} Ps3TimerSlot;

static Ps3QueueSlot sQueues[PS3_QUEUE_SLOTS];
static Ps3ThreadSlot sThreads[PS3_THREAD_SLOTS];
static Ps3TimerSlot sTimers[PS3_TIMER_SLOTS];
static volatile int sRegistryLock;
static int sPadInitialized;
static void* sCurrentFramebuffer;
static void* sNextFramebuffer;
static OSMesgQueue* sViQueue;
static OSMesg sViMessage;
static u32 sViRetraceCount = 1;
static volatile int sViRunning;
static sys_ppu_thread_t sViThread;

u8 __osMaxControllers = MAXCONTROLLERS;
__OSEventState __osEventStateTab[OS_NUM_EVENTS];

static void RegistryLock(void) {
    while (__sync_lock_test_and_set(&sRegistryLock, 1)) {
        sysThreadYield();
    }
}

static void RegistryUnlock(void) {
    __sync_lock_release(&sRegistryLock);
}

extern void oot_ps3_early_trace(const char* message);
static void OsTrace(const char* message) {
    oot_ps3_early_trace(message);
}

static void QueueLock(Ps3QueueSlot* slot) {
    while (__sync_lock_test_and_set(&slot->lock, 1)) {
        sysThreadYield();
    }
}

static void QueueUnlock(Ps3QueueSlot* slot) {
    __sync_lock_release(&slot->lock);
}

static Ps3QueueSlot* FindQueue(OSMesgQueue* queue, int create) {
    Ps3QueueSlot* freeSlot = NULL;
    int i;
    RegistryLock();
    for (i = 0; i < PS3_QUEUE_SLOTS; ++i) {
        if (sQueues[i].queue == queue) {
            RegistryUnlock();
            return &sQueues[i];
        }
        if (freeSlot == NULL && sQueues[i].queue == NULL) {
            freeSlot = &sQueues[i];
        }
    }
    if (create && freeSlot != NULL) {
        memset(freeSlot, 0, sizeof(*freeSlot));
        freeSlot->queue = queue;
        freeSlot->initialized = 1;
    }
    RegistryUnlock();
    return freeSlot;
}

void osCreateMesgQueue(OSMesgQueue* queue, OSMesg* messages, int32_t count) {
    if (queue == NULL) {
        return;
    }
    queue->mtqueue = NULL;
    queue->fullqueue = NULL;
    queue->validCount = 0;
    queue->first = 0;
    queue->msgCount = count;
    queue->msg = messages;
    OsTrace("os: creating atomic message queue");
    (void)FindQueue(queue, 1);
    OsTrace("os: atomic message queue ready");
}

int32_t osSendMesg(OSMesgQueue* queue, OSMesg message, int32_t flag) {
    /* Same semantics as libultraship (os_mesg.cpp), which SoH is written against:
     * queue operations never block, OS_MESG_BLOCK included. A blocking wait here
     * hung every place that waits for a message nothing on PC/PS3 ever sends
     * (audio DMA "done", RCP null task, scheduler frame-done...). */
    flag = OS_MESG_NOBLOCK;
    Ps3QueueSlot* slot = FindQueue(queue, 1);
    int32_t index;
    if (slot == NULL || queue == NULL || queue->msg == NULL || queue->msgCount <= 0) {
        return -1;
    }
    for (;;) {
        QueueLock(slot);
        if (queue->validCount < queue->msgCount) {
            break;
        }
        QueueUnlock(slot);
        if (flag == OS_MESG_NOBLOCK) {
            return -1;
        }
        sysThreadYield();
    }
    index = (queue->first + queue->validCount) % queue->msgCount;
    queue->msg[index] = message;
    ++queue->validCount;
    QueueUnlock(slot);
    return 0;
}

int32_t osJamMesg(OSMesgQueue* queue, OSMesg message, int32_t flag) {
    /* Same semantics as libultraship (os_mesg.cpp), which SoH is written against:
     * queue operations never block, OS_MESG_BLOCK included. A blocking wait here
     * hung every place that waits for a message nothing on PC/PS3 ever sends
     * (audio DMA "done", RCP null task, scheduler frame-done...). */
    flag = OS_MESG_NOBLOCK;
    Ps3QueueSlot* slot = FindQueue(queue, 1);
    if (slot == NULL || queue == NULL || queue->msg == NULL || queue->msgCount <= 0) {
        return -1;
    }
    for (;;) {
        QueueLock(slot);
        if (queue->validCount < queue->msgCount) {
            break;
        }
        QueueUnlock(slot);
        if (flag == OS_MESG_NOBLOCK) {
            return -1;
        }
        sysThreadYield();
    }
    queue->first = (queue->first + queue->msgCount - 1) % queue->msgCount;
    queue->msg[queue->first] = message;
    ++queue->validCount;
    QueueUnlock(slot);
    return 0;
}

int32_t osRecvMesg(OSMesgQueue* queue, OSMesg* message, int32_t flag) {
    /* Same semantics as libultraship (os_mesg.cpp), which SoH is written against:
     * queue operations never block, OS_MESG_BLOCK included. A blocking wait here
     * hung every place that waits for a message nothing on PC/PS3 ever sends
     * (audio DMA "done", RCP null task, scheduler frame-done...). */
    flag = OS_MESG_NOBLOCK;
    Ps3QueueSlot* slot = FindQueue(queue, 1);
    if (slot == NULL || queue == NULL || queue->msg == NULL || queue->msgCount <= 0) {
        return -1;
    }
    for (;;) {
        QueueLock(slot);
        if (queue->validCount > 0) {
            break;
        }
        QueueUnlock(slot);
        if (flag == OS_MESG_NOBLOCK) {
            return -1;
        }
        sysThreadYield();
    }
    if (message != NULL) {
        *message = queue->msg[queue->first];
    }
    queue->first = (queue->first + 1) % queue->msgCount;
    --queue->validCount;
    QueueUnlock(slot);
    return 0;
}

void osSetEventMesg(OSEvent event, OSMesgQueue* queue, OSMesg message) {
    OsTrace("os: set event message entered");
    if (event < OS_NUM_EVENTS) {
        __osEventStateTab[event].queue = queue;
        __osEventStateTab[event].msg = message;
    }
    OsTrace("os: set event message completed");
}

static Ps3ThreadSlot* FindThread(OSThread* thread, int create) {
    Ps3ThreadSlot* freeSlot = NULL;
    int i;
    RegistryLock();
    for (i = 0; i < PS3_THREAD_SLOTS; ++i) {
        if (sThreads[i].thread == thread) {
            RegistryUnlock();
            return &sThreads[i];
        }
        if (freeSlot == NULL && sThreads[i].thread == NULL) {
            freeSlot = &sThreads[i];
        }
    }
    if (create && freeSlot != NULL) {
        memset(freeSlot, 0, sizeof(*freeSlot));
        freeSlot->thread = thread;
    }
    RegistryUnlock();
    return freeSlot;
}

static int Ps3Priority(OSPri priority) {
    int native = 2000 - priority * 4;
    if (native < 400) native = 400;
    if (native > 2800) native = 2800;
    return native;
}

/* Unused while osStartThread bypasses native threads; kept for when it no longer does. */
__attribute__((unused)) static void ThreadTrampoline(void* argument) {
    Ps3ThreadSlot* slot = (Ps3ThreadSlot*)argument;
    OsTrace("os: native thread trampoline entered");
    slot->thread->state = OS_STATE_RUNNING;
    slot->entry(slot->argument);
    OsTrace("os: native thread trampoline returned");
    slot->completed = 1;
    slot->thread->state = OS_STATE_STOPPED;
    sysThreadExit(0);
}

void osCreateThread(OSThread* thread, OSId id, void (*entry)(void*), void* argument, void* stack, OSPri priority) {
    Ps3ThreadSlot* slot;
    (void)stack;
    if (thread == NULL || entry == NULL) return;
    OsTrace("os: create thread entered");
    memset(thread, 0, sizeof(*thread));
    thread->id = id;
    thread->priority = priority;
    thread->state = OS_STATE_STOPPED;
    slot = FindThread(thread, 1);
    if (slot != NULL) {
        slot->entry = entry;
        slot->argument = argument;
    }
    OsTrace("os: create thread completed");
}

void osStartThread(OSThread* thread) {
    Ps3ThreadSlot* slot = FindThread(thread, 0);
    if (slot == NULL || slot->started) return;
    OsTrace("os: start thread entered");
    slot->started = 1;
    thread->state = OS_STATE_RUNNABLE;
    OsTrace("os: start thread bypassed for diagnostic boot");
}

void osStopThread(OSThread* thread) {
    if (thread != NULL) thread->state = OS_STATE_STOPPED;
}

void osDestroyThread(OSThread* thread) {
    Ps3ThreadSlot* slot = FindThread(thread, 0);
    if (slot != NULL && slot->started && !slot->joined && slot->completed) {
        u64 result = 0;
        if (sysThreadJoin(slot->native, &result) == 0) slot->joined = 1;
    }
}

OSId osGetThreadId(OSThread* thread) {
    return thread != NULL ? thread->id : 0;
}

OSPri osGetThreadPri(OSThread* thread) {
    return thread != NULL ? thread->priority : 0;
}

void osSetThreadPri(OSThread* thread, OSPri priority) {
    Ps3ThreadSlot* slot;
    if (thread == NULL) return;
    thread->priority = priority;
    slot = FindThread(thread, 0);
    if (slot != NULL && slot->started) {
        (void)sysThreadSetPriority(slot->native, Ps3Priority(priority));
    }
}

static s8 ScaleStick(u16 raw) {
    int value = ((int)raw - 128) * 80 / 127;
    if (value > 80) value = 80;
    if (value < -80) value = -80;
    if (value > -6 && value < 6) value = 0;
    return (s8)value;
}

int oot_ps3_debug_input(unsigned int buttons); /* ps3_debug_overlay.c */

static void FillPad(OSContPad* output, const padData* input) {
    output->button = 0;
    if (input->BTN_CROSS) output->button |= A_BUTTON;
    if (input->BTN_SQUARE) output->button |= B_BUTTON;
    if (input->BTN_START) output->button |= START_BUTTON;
    if (input->BTN_UP) output->button |= U_JPAD;
    if (input->BTN_DOWN) output->button |= D_JPAD;
    if (input->BTN_LEFT) output->button |= L_JPAD;
    if (input->BTN_RIGHT) output->button |= R_JPAD;
    if (input->BTN_L1) output->button |= L_TRIG;
    if (input->BTN_R1) output->button |= R_TRIG;
    if (input->BTN_L2) output->button |= Z_TRIG;
    if (input->BTN_TRIANGLE || input->ANA_R_V < 80) output->button |= U_CBUTTONS;
    if (input->BTN_CIRCLE || input->ANA_R_H > 176) output->button |= R_CBUTTONS;
    if (input->ANA_R_H < 80) output->button |= L_CBUTTONS;
    if (input->BTN_R2 || input->ANA_R_V > 176) output->button |= D_CBUTTONS;
    output->stick_x = ScaleStick(input->ANA_L_H);
    output->stick_y = (s8)-ScaleStick(input->ANA_L_V);
    output->right_stick_x = ScaleStick(input->ANA_R_H);
    output->right_stick_y = (s8)-ScaleStick(input->ANA_R_V);
    output->err_no = 0;
    {
        /* Debug menu (R3): while it is open the game gets no input. */
        unsigned int dbg = 0;
        if (input->BTN_R3) dbg |= 0x0001U;
        if (input->BTN_UP) dbg |= 0x0002U;
        if (input->BTN_DOWN) dbg |= 0x0004U;
        if (input->BTN_LEFT) dbg |= 0x0008U;
        if (input->BTN_RIGHT) dbg |= 0x0010U;
        if (input->BTN_CROSS) dbg |= 0x0020U;
        if (input->BTN_CIRCLE) dbg |= 0x0040U;
        if (input->BTN_L1) dbg |= 0x0080U;
        if (input->BTN_R1) dbg |= 0x0100U;
        if (oot_ps3_debug_input(dbg)) {
            output->button = 0;
            output->stick_x = output->stick_y = 0;
            output->right_stick_x = output->right_stick_y = 0;
        }
    }
}

int32_t osContInit(OSMesgQueue* queue, uint8_t* controllerBits, OSContStatus* status) {
    padInfo info;
    int result;
    int i;
    (void)queue;
    result = ioPadInit(MAX_PORT_NUM);
    if (result == PAD_OK || result == (int)PAD_ERROR_ALREADY_INITIALIZED) sPadInitialized = 1;
    if (controllerBits != NULL) *controllerBits = 0;
    memset(&info, 0, sizeof(info));
    if (sPadInitialized) (void)ioPadGetInfo(&info);
    for (i = 0; i < MAXCONTROLLERS; ++i) {
        if (status != NULL) {
            status[i].type = CONT_TYPE_NORMAL;
            status[i].status = info.status[i] ? CONT_CARD_ON : 0;
            status[i].err_no = info.status[i] ? 0 : CONT_ERR_NO_CONTROLLER;
        }
        if (controllerBits != NULL && info.status[i]) *controllerBits |= (uint8_t)(1U << i);
    }
    return sPadInitialized ? 0 : -1;
}

int32_t osContStartReadData(OSMesgQueue* queue) {
    if (queue != NULL) (void)osSendMesg(queue, OS_MESG_32(0), OS_MESG_NOBLOCK);
    return 0;
}

// ioPadGetData returns len == 0 when nothing changed since the previous read, and
// each change is delivered only once. Keep the last state per port so the game
// (osContGetReadData) and the SELECT exit check share it without stealing events.
static padData sPs3LastPad[MAXCONTROLLERS];
static int sPs3HavePad[MAXCONTROLLERS];

void oot_ps3_pad_refresh(void) {
    padInfo info;
    int i;
    if (!sPadInitialized) {
        const int result = ioPadInit(MAX_PORT_NUM);
        if (result == PAD_OK || result == (int)PAD_ERROR_ALREADY_INITIALIZED) sPadInitialized = 1;
    }
    if (!sPadInitialized || ioPadGetInfo(&info) != PAD_OK) return;
    for (i = 0; i < MAXCONTROLLERS; ++i) {
        padData data;
        if (!info.status[i]) {
            sPs3HavePad[i] = 0;
            continue;
        }
        if (ioPadGetData(i, &data) == PAD_OK && data.len > 0) {
            sPs3LastPad[i] = data;
            sPs3HavePad[i] = 1;
        }
    }
}

int oot_ps3_pad_select_held(void) {
    int i;
    for (i = 0; i < MAXCONTROLLERS; ++i) {
        if (sPs3HavePad[i] && sPs3LastPad[i].BTN_SELECT) return 1;
    }
    return 0;
}

void osContGetReadData(OSContPad* pads) {
    int i;
    if (pads == NULL) return;
    memset(pads, 0, sizeof(*pads) * MAXCONTROLLERS);
    oot_ps3_pad_refresh();
    for (i = 0; i < MAXCONTROLLERS; ++i) {
        if (!sPs3HavePad[i]) {
            // What an empty N64 port reports; PadMgr_ProcessInputs crashes on any
            // value other than 0, 4 or 8 (CONT_ERR_NO_CONTROLLER is 1).
            pads[i].err_no = CONT_NO_RESPONSE_ERROR;
            continue;
        }
        FillPad(&pads[i], &sPs3LastPad[i]);
    }
}

int32_t osMotorInit(OSMesgQueue* queue, OSPfs* pfs, int32_t channel) {
    (void)queue;
    if (pfs == NULL || channel < 0 || channel >= MAXCONTROLLERS) return -1;
    pfs->channel = channel;
    return 0;
}

int32_t __osMotorAccess(OSPfs* pfs, uint32_t vibrate) {
    padActParam motors;
    if (pfs == NULL || pfs->channel < 0 || pfs->channel >= MAXCONTROLLERS) return -1;
    memset(&motors, 0, sizeof(motors));
    motors.small_motor = vibrate ? 1 : 0;
    motors.large_motor = vibrate ? 192 : 0;
    return ioPadSetActDirect(pfs->channel, &motors) == PAD_OK ? 0 : -1;
}

static void ViThread(void* argument) {
    u32 counter = 0;
    (void)argument;
    while (sViRunning) {
        sysUsleep(16667);
        ++counter;
        if (sViQueue != NULL && counter >= sViRetraceCount) {
            counter = 0;
            (void)osSendMesg(sViQueue, sViMessage, OS_MESG_NOBLOCK);
        }
        if (__osEventStateTab[OS_EVENT_VI].queue != NULL && __osEventStateTab[OS_EVENT_VI].queue != sViQueue) {
            (void)osSendMesg(__osEventStateTab[OS_EVENT_VI].queue,
                             __osEventStateTab[OS_EVENT_VI].msg, OS_MESG_NOBLOCK);
        }
    }
    sysThreadExit(0);
}

void osCreateViManager(OSPri priority) {
    if (sViRunning) return;
    sViRunning = 1;
    if (sysThreadCreate(&sViThread, ViThread, NULL, Ps3Priority(priority), 32U * 1024U,
                        0, "oot-vi") != 0) {
        sViRunning = 0;
    }
}

void osViSetEvent(OSMesgQueue* queue, OSMesg message, u32 retraceCount) {
    OsTrace("os: VI event setup entered");
    sViQueue = queue;
    sViMessage = message;
    sViRetraceCount = retraceCount != 0 ? retraceCount : 1;
    osSetEventMesg(OS_EVENT_VI, queue, message);
    OsTrace("os: VI event setup completed");
}

void osViSwapBuffer(void* address) {
    sCurrentFramebuffer = sNextFramebuffer;
    sNextFramebuffer = address;
}

void* osViGetCurrentFramebuffer(void) { return sCurrentFramebuffer; }
void* osViGetNextFramebuffer(void) { return sNextFramebuffer; }
void osViBlack(uint8_t active) { (void)active; }
void osViSetMode(OSViMode* mode) { (void)mode; }
void osViSetSpecialFeatures(u32 features) { (void)features; }
void osViSetXScale(f32 scale) { (void)scale; }
void osViSetYScale(f32 scale) { (void)scale; }

static void TimerThread(void* argument) {
    Ps3TimerSlot* slot = (Ps3TimerSlot*)argument;
    OSTime wait = slot->delay != 0 ? slot->delay : slot->interval;
    do {
        u64 usec = OS_CYCLES_TO_USEC(wait);
        while (usec > 0) {
            u32 slice = usec > 0xFFFFFFFFULL ? 0xFFFFFFFFU : (u32)usec;
            sysUsleep(slice);
            usec -= slice;
        }
        if (slot->active) (void)osSendMesg(slot->queue, slot->message, OS_MESG_NOBLOCK);
        wait = slot->interval;
    } while (slot->active && wait != 0);
    slot->active = 0;
    sysThreadExit(0);
}

int osSetTimer(OSTimer* timer, OSTime countdown, OSTime interval, OSMesgQueue* queue, OSMesg message) {
    int i;
    if (timer == NULL || queue == NULL) return -1;
    timer->next = NULL;
    timer->prev = NULL;
    timer->value = countdown != 0 ? countdown : interval;
    timer->interval = interval;
    timer->mq = queue;
    timer->msg = message;
    RegistryLock();
    for (i = 0; i < PS3_TIMER_SLOTS; ++i) {
        if (!sTimers[i].active) {
            Ps3TimerSlot* slot = &sTimers[i];
            memset(slot, 0, sizeof(*slot));
            slot->timer = timer;
            slot->delay = countdown;
            slot->interval = interval;
            slot->queue = queue;
            slot->message = message;
            slot->active = 1;
            RegistryUnlock();
            if (sysThreadCreate(&slot->native, TimerThread, slot, 1800, 16U * 1024U,
                                0, "oot-timer") == 0) return 0;
            slot->active = 0;
            return -1;
        }
    }
    RegistryUnlock();
    return -1;
}

int32_t osEPiStartDma(OSPiHandle* handle, OSIoMesg* message, int32_t direction) {
    (void)handle;
    (void)direction;
    if (message == NULL) return -1;
    if (message->hdr.retQueue != NULL) {
        return osSendMesg(message->hdr.retQueue, OS_MESG_PTR(message), OS_MESG_NOBLOCK);
    }
    return 0;
}
