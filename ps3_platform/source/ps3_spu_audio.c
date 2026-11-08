/* Audio mixing on an SPU (ps3_platform/spu/oot_audio_spu.c): starts one SPU thread
 * and runs soh/mixer.c's recorded command lists on it. The audio thread sleeps on
 * the event queue while the SPU works, so the PPU is free for the game and the
 * renderer. USRDIR/spu_audio_off keeps the PPU mixer. PS3 system headers only. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/event_queue.h>
#include <sys/spu.h>

#include "oot-audio-spu-bin.h" /* generated: oot_audio_spu_bin[] (SPU ELF) */

void oot_ps3_early_trace(const char* message);
unsigned long long oot_ps3_microseconds_now(void);

#define OOT_SPU_PORT 12
#define OOT_SPU_TIMEOUT_US 500000ULL

static sysSpuImage sImage;
static sys_spu_thread_t sThread;
static sys_spu_group_t sGroup;
static sys_event_queue_t sQueue;
static int sReady;
static unsigned long long sRunUs, sMaxRunUs;
static unsigned int sRuns;

static int Fail(const char* step, int ret) {
    char line[128];
    snprintf(line, sizeof(line), "trace: SPU audio %s failed (0x%08x); PPU mixer used", step, (unsigned)ret);
    oot_ps3_early_trace(line);
    return 0;
}

int oot_ps3_spu_audio_init(void) {
    static const char groupName[] = "oot-audio";
    static const char threadName[] = "oot-mixer";
    sysSpuThreadGroupAttribute groupAttr;
    sysSpuThreadAttribute threadAttr;
    sysSpuThreadArgument args;
    sys_event_queue_attr_t queueAttr = { SYS_EVENT_QUEUE_FIFO, SYS_EVENT_QUEUE_PPU, "ootmixq" };
    FILE* off;
    s32 ret;

    if (sReady) return 1;
    off = fopen("/dev_hdd0/game/OOTP30048/USRDIR/spu_audio_off", "rb");
    if (off != NULL) {
        fclose(off);
        oot_ps3_early_trace("trace: SPU audio off (USRDIR/spu_audio_off); PPU mixer used");
        return 0;
    }
    (void)sysSpuInitialize(6, 0); /* an error here only means it was initialized already */
    ret = sysSpuImageImport(&sImage, oot_audio_spu_bin, 0);
    if (ret != 0) return Fail("image import", ret);
    memset(&groupAttr, 0, sizeof(groupAttr));
    groupAttr.nsize = sizeof(groupName);
    groupAttr.name = groupName;
    groupAttr.type = 0;
    ret = sysSpuThreadGroupCreate(&sGroup, 1, 100, &groupAttr);
    if (ret != 0) return Fail("group create", ret);
    ret = sysEventQueueCreate(&sQueue, &queueAttr, SYS_EVENT_QUEUE_KEY_LOCAL, 4);
    if (ret != 0) return Fail("event queue", ret);
    memset(&threadAttr, 0, sizeof(threadAttr));
    threadAttr.name = threadName;
    threadAttr.nsize = sizeof(threadName);
    threadAttr.option = SPU_THREAD_ATTR_NONE;
    memset(&args, 0, sizeof(args));
    ret = sysSpuThreadInitialize(&sThread, sGroup, 0, &sImage, &threadAttr, &args);
    if (ret != 0) return Fail("thread init", ret);
    ret = sysSpuThreadSetConfiguration(sThread, SPU_SIGNAL1_OVERWRITE | SPU_SIGNAL2_OVERWRITE);
    if (ret != 0) return Fail("thread config", ret);
    ret = sysSpuThreadConnectEvent(sThread, sQueue, SPU_THREAD_EVENT_USER, OOT_SPU_PORT);
    if (ret != 0) return Fail("event connect", ret);
    ret = sysSpuThreadGroupStart(sGroup);
    if (ret != 0) return Fail("group start", ret);
    sReady = 1;
    oot_ps3_early_trace("trace: SPU audio mixer running (USRDIR/spu_audio_off turns it off)");
    return 1;
}

/* Runs one recorded list (soh/mixer.c) and waits for it. 0: the SPU didn't answer in
 * time; the caller switches to the PPU mixer. */
int oot_ps3_spu_audio_run(const void* list) {
    sys_event_t event;
    unsigned long long t0, spent;
    s32 ret;
    if (!sReady || list == NULL) return 0;
    t0 = oot_ps3_microseconds_now();
    ret = sysSpuThreadWriteSignal(sThread, 0, (u32)(uintptr_t)list);
    if (ret != 0) {
        sReady = 0;
        return Fail("signal", ret);
    }
    ret = sysEventQueueReceive(sQueue, &event, OOT_SPU_TIMEOUT_US);
    if (ret != 0) {
        sReady = 0;
        return Fail("run (no answer)", ret);
    }
    spent = oot_ps3_microseconds_now() - t0;
    sRunUs += spent;
    if (spent > sMaxRunUs) sMaxRunUs = spent;
    ++sRuns;
    return 1;
}

void oot_ps3_spu_audio_take_stats(unsigned int* runs, unsigned long long* totalUs, unsigned long long* maxUs) {
    *runs = sRuns;
    *totalUs = sRunUs;
    *maxUs = sMaxRunUs;
    sRuns = 0;
    sRunUs = sMaxRunUs = 0;
}

int oot_ps3_spu_audio_active(void) {
    return sReady;
}

/* At exit: let the SPU thread end (signal 0). The process exit releases the group. */
void oot_ps3_spu_audio_shutdown(void) {
    if (!sReady) return;
    sReady = 0;
    (void)sysSpuThreadWriteSignal(sThread, 0, 0);
}
