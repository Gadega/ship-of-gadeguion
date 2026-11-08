// SPDX-License-Identifier: GPL-3.0-or-later
// Native PS3 entry/trace shim.  Keep SDK headers out of Shipwright sources.

#include <fcntl.h>
#include <stdint.h>
#include <sys/file.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/systime.h>
#include <ppu-asm.h>
#include <sysutil/sysutil.h>
#include <io/pad.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/thread.h>

/* Yield the PPU thread (for C++ files that can't include PS3 system headers). */
void sysThreadYieldPs3(void) {
    sysThreadYield();
}

extern int oot_shipwright_main(int argc, char* argv[]);
extern int oot_ps3_shipwright_video_start(void);
extern void oot_ps3_audio_shutdown(void);

/* PPU timebase (mftb) instead of gettimeofday: no syscall, so the per-opcode
 * and per-frame timers stop costing ~1-2 us each. The frequency (79.8 MHz on
 * retail consoles) is read once. Same epoch for every caller in this process. */
unsigned long long oot_ps3_microseconds_now(void) {
    static unsigned long long timebaseHz;
    if (timebaseHz == 0) {
        timebaseHz = sysGetTimebaseFrequency();
        if (timebaseHz == 0) {
            struct timeval now;
            gettimeofday(&now, 0);
            return (unsigned long long)now.tv_sec * 1000000ULL + (unsigned long long)now.tv_usec;
        }
    }
    const unsigned long long tb = __gettime();
    return (tb / timebaseHz) * 1000000ULL + ((tb % timebaseHz) * 1000000ULL) / timebaseHz;
}

static const char* const sTracePaths[] = {
    "/dev_hdd0/oot_ps3_v48_audio_probe_boot.txt",
    "/dev_hdd0/tmp/oot_ps3_v48_audio_probe_boot.txt",
    "/dev_hdd0/game/OOTP30048/USRDIR/audio_probe_boot_trace.txt",
    "oot_ps3_v48_audio_probe_boot.txt",
};
static unsigned int TraceLength(const char* text);

/* Optional USB mirror. Keep its handle open so hundreds of trace lines do
 * not repeatedly mount/open/fsync a removable drive during the boot probe. */
static int sUsbTraceFd = -1;
static int sUsbTraceChecked;
static unsigned int sUsbTraceLines;
static unsigned int sBootTraceLines;
static int sCompactTitleTrace;
static int sCompactLinkSkeletonTrace;
static int sCompactPlayTrace;
static int sFullDetailTrace;
/* Four backup trace targets are opened once during main's trace reset.
 * Reopening all four for every title-frame probe cost thousands of LV2
 * filesystem syscalls during the bounded renderer window. */
static int sTraceDescriptors[] = { -1, -1, -1, -1 };

static void UsbTrace(const char* message, int truncate) {
    if (!sUsbTraceChecked) {
        sUsbTraceFd = open("/dev_usb000/oot_ps3_v48_audio_probe_boot.txt",
                           O_WRONLY | O_CREAT | (truncate ? O_TRUNC : O_APPEND), 0666);
        sUsbTraceChecked = 1;
    }
    if (sUsbTraceFd < 0) return;
    (void)write(sUsbTraceFd, message, TraceLength(message));
    (void)write(sUsbTraceFd, "\n", 1);
    if ((++sUsbTraceLines & 63U) == 0U) (void)fsync(sUsbTraceFd);
}

/* Asynchronous trace: once the writer thread runs, oot_ps3_early_trace only appends to
 * this buffer and the thread writes it to the trace files every 200 ms. Synchronous
 * writes (4 HDD files + USB per line) cost milliseconds each and stalled gameplay,
 * e.g. tree/bush spawners logging every spawn/delete. Exit/watchdog paths flush first. */
enum { TRACE_ASYNC_BYTES = 256 * 1024 };
static char sTraceAsync[TRACE_ASYNC_BYTES];
static char sTraceAsyncOut[TRACE_ASYNC_BYTES];
static volatile unsigned int sTraceAsyncLen;
static volatile int sTraceAsyncLock;
static volatile int sTraceFlushLock;
static volatile int sTraceAsyncOn;
static sys_ppu_thread_t sTraceAsyncThread;

static void TraceSpinLock(volatile int* lock) {
    while (__sync_lock_test_and_set(lock, 1)) sysThreadYield();
}
static void TraceSpinUnlock(volatile int* lock) {
    __sync_lock_release(lock);
}

static void TraceAsyncFlush(void) {
    unsigned int length;
    unsigned int i;
    TraceSpinLock(&sTraceFlushLock);
    TraceSpinLock(&sTraceAsyncLock);
    length = sTraceAsyncLen;
    if (length != 0) memcpy(sTraceAsyncOut, sTraceAsync, length);
    sTraceAsyncLen = 0;
    TraceSpinUnlock(&sTraceAsyncLock);
    if (length != 0) {
        for (i = 0; i < sizeof(sTraceDescriptors) / sizeof(sTraceDescriptors[0]); ++i)
            if (sTraceDescriptors[i] >= 0) (void)write(sTraceDescriptors[i], sTraceAsyncOut, length);
        if (sUsbTraceFd >= 0) (void)write(sUsbTraceFd, sTraceAsyncOut, length);
    }
    TraceSpinUnlock(&sTraceFlushLock);
}

static void TraceAsyncAppend(const char* message) {
    const unsigned int length = TraceLength(message);
    if (length + 1U > TRACE_ASYNC_BYTES) return;
    TraceSpinLock(&sTraceAsyncLock);
    if (sTraceAsyncLen + length + 1U > TRACE_ASYNC_BYTES) {
        TraceSpinUnlock(&sTraceAsyncLock);
        TraceAsyncFlush(); /* full: write out now (rare) */
        TraceSpinLock(&sTraceAsyncLock);
    }
    memcpy(sTraceAsync + sTraceAsyncLen, message, length);
    sTraceAsync[sTraceAsyncLen + length] = '\n';
    sTraceAsyncLen += length + 1U;
    TraceSpinUnlock(&sTraceAsyncLock);
}

static void TraceAsyncThread(void* argument) {
    unsigned int ticks = 0;
    (void)argument;
    for (;;) {
        usleep(200000);
        TraceAsyncFlush();
        if ((++ticks % 10U) == 0U && sUsbTraceFd >= 0) (void)fsync(sUsbTraceFd);
    }
}

static void TraceAsyncStart(void) {
    if (sTraceAsyncOn) return;
    if (!sUsbTraceChecked) UsbTrace("trace: USB mirror check", 0);
    if (sysThreadCreate(&sTraceAsyncThread, TraceAsyncThread, NULL, 1800, 64 * 1024, 0, "oot-trace") == 0) {
        sTraceAsyncOn = 1;
    }
}

void oot_ps3_early_trace_flush_usb(void) {
    unsigned int i;
    if (sTraceAsyncOn) TraceAsyncFlush();
    for (i = 0; i < sizeof(sTraceDescriptors) / sizeof(sTraceDescriptors[0]); ++i)
        if (sTraceDescriptors[i] >= 0) (void)fsync(sTraceDescriptors[i]);
    if (sUsbTraceFd >= 0) (void)fsync(sUsbTraceFd);
}

static unsigned int TraceLength(const char* text) {
    unsigned int length = 0;
    if (text != 0) {
        while (text[length] != '\0') ++length;
    }
    return length;
}

/*
 * This constructor deliberately bypasses newlib's open/write wrappers and
 * invokes LV2 filesystem syscalls directly.  It can therefore leave evidence
 * that the PS3 reached constructor processing even if libc startup or main
 * never completes.
 */
static void RawTraceMessageToPath(const char* path, const char* message,
                                  u64 length, int truncate) {
    s32 descriptor = -1;
    u64 written = 0;
    const s32 flags = SYS_O_WRONLY | SYS_O_CREAT |
                      (truncate ? SYS_O_TRUNC : SYS_O_APPEND);

    if (sysLv2FsOpen(path, flags, &descriptor, 0666, 0, 0) != 0) {
        return;
    }
    (void)sysLv2FsWrite(descriptor, message, length, &written);
    (void)sysLv2FsFsync(descriptor);
    (void)sysLv2FsClose(descriptor);
}

static void RawTraceMarker(const char* message, u64 length, int truncate) {
    RawTraceMessageToPath("/dev_hdd0/tmp/oot_ps3_v48_audio_probe_premain.txt",
                          message, length, truncate);
    RawTraceMessageToPath("/dev_hdd0/oot_ps3_v48_audio_probe_premain.txt",
                          message, length, truncate);
}

/* Priority 101 maps to .ctors.65434 on this GCC toolchain.  The legacy
 * constructor walker executes the table in reverse, placing this before the
 * PSL1GHT runtime constructors at priorities 103 and 104. */
static void __attribute__((constructor(101))) OotPs3RawPreMainTrace(void) {
    static const char marker[] = "P101: entered _init before PSL1GHT runtime constructors\n";
    RawTraceMarker(marker, sizeof(marker) - 1U, 1);
}

static void __attribute__((constructor(102))) OotPs3TraceBeforeRuntime(void) {
    static const char marker[] = "P102: priority-101 trace returned successfully\n";
    RawTraceMarker(marker, sizeof(marker) - 1U, 0);
}

/* The legacy constructor table runs in reverse.  These section names sort
 * directly after the runtime constructor they diagnose, so they execute
 * directly after it: 65431z follows sbrk's 65432 at runtime, and 65430z
 * follows syscalls' 65431. */
static void OotPs3TraceAfterSbrk(void) {
    static const char marker[] = "P103: sbrk_init completed\n";
    RawTraceMarker(marker, sizeof(marker) - 1U, 0);
}

static void (*const sTraceAfterSbrk)(void)
    __attribute__((used, section(".ctors.65431z"))) = OotPs3TraceAfterSbrk;

static void OotPs3TraceAfterSyscalls(void) {
    static const char marker[] = "P104: __syscalls_init completed\n";
    RawTraceMarker(marker, sizeof(marker) - 1U, 0);
}

static void (*const sTraceAfterSyscalls)(void)
    __attribute__((used, section(".ctors.65430z"))) = OotPs3TraceAfterSyscalls;

static void __attribute__((constructor(106))) OotPs3TraceAfterCoreRuntime(void) {
    static const char marker[] = "P106: priorities 103-105 completed\n";
    RawTraceMarker(marker, sizeof(marker) - 1U, 0);
}

static void __attribute__((constructor)) OotPs3TraceAfterDefaultRuntime(void) {
    static const char marker[] = "P65535: default constructor stage reached\n";
    RawTraceMarker(marker, sizeof(marker) - 1U, 0);
}

static void TraceToDescriptor(unsigned int index, const char* message, int sync) {
    const int descriptor = sTraceDescriptors[index];
    if (descriptor < 0) return;
    if (message != 0) (void)write(descriptor, message, TraceLength(message));
    (void)write(descriptor, "\n", 1);
    if (sync) (void)fsync(descriptor);
}

/* Stall diagnostics. A thread is identified by r13 (its TLS pointer): no syscall,
 * so it is cheap enough for every malloc. The malloc lock wrappers (linked with
 * --wrap=__malloc_lock/__malloc_unlock) record who holds the allocator lock and
 * since when, so the watchdog can tell a lock deadlock from a loop. */
#include <reent.h>
unsigned long long oot_ps3_thread_key(void) {
    unsigned long long key;
    __asm__ volatile("mr %0,13" : "=r"(key));
    return key;
}
static volatile unsigned long long sMallocOwner;
static volatile unsigned int sMallocDepth;
static volatile unsigned long long sMallocSinceUs;
void __real___malloc_lock(struct _reent* reent);
void __real___malloc_unlock(struct _reent* reent);
void __wrap___malloc_lock(struct _reent* reent) {
    __real___malloc_lock(reent);
    if (sMallocDepth++ == 0) {
        sMallocOwner = oot_ps3_thread_key();
        sMallocSinceUs = oot_ps3_microseconds_now();
    }
}
void __wrap___malloc_unlock(struct _reent* reent) {
    if (sMallocDepth != 0 && --sMallocDepth == 0) sMallocOwner = 0;
    __real___malloc_unlock(reent);
}
void oot_ps3_lock_debug_state(char* out, unsigned size) {
    const unsigned long long now = oot_ps3_microseconds_now();
    snprintf(out, size, "malloc lock owner=%llx depth=%u held %llu ms; trace locks async=%d flush=%d",
             sMallocOwner, sMallocDepth, sMallocOwner != 0 ? (now - sMallocSinceUs) / 1000ULL : 0ULL,
             sTraceAsyncLock, sTraceFlushLock);
}

/* The last message passed to the tracer, kept even when the filters below drop
 * it, so the stall watchdog can report where the game stopped. */
static char sPs3LastTrace[192];

const char* oot_ps3_last_trace(void) {
    return sPs3LastTrace;
}

/* Counts every tracer call (filtered or not). The stall watchdog treats new
 * trace calls as progress, so a slow scene load isn't mistaken for a hang. */
static volatile unsigned int sPs3TraceCount;
/* Trace files are written only when USRDIR/trace_on exists (release default: off -
 * they cost 4 HDD files + USB). The watchdog's step bookkeeping runs either way. */
static int sTraceEnabled;
#define OOT_PS3_TRACE_ON_PATH "/dev_hdd0/game/OOTP30048/USRDIR/trace_on"

unsigned int oot_ps3_trace_count(void) {
    return sPs3TraceCount;
}

void oot_ps3_early_trace(const char* message) {
    unsigned int index;
    int activateCompact = 0;
    int activateLink = 0;
    /* The audio thread's periodic reports are not game progress: counting them
     * kept the stall watchdog quiet while the game thread was frozen (pause
     * menu, 2026-10-10) and replaced the "stopped at" step. */
    if (message == 0 || strncmp(message, "graph: Play audio-", 18) != 0) {
        ++sPs3TraceCount;
        if (message != 0) {
            strncpy(sPs3LastTrace, message, sizeof(sPs3LastTrace) - 1);
            sPs3LastTrace[sizeof(sPs3LastTrace) - 1] = '\0';
        }
    }
    /* Per-actor spawn ladders (~28 lines per actor) made scene loads with many
     * actors take 20+ s. The "trace: actor spawned" summary stays; the watchdog
     * still sees these steps through sPs3LastTrace. */
    if (message != 0 &&
        (strncmp(message, "actor-spawn:", 12) == 0 || strncmp(message, "actor-spawn-entry:", 18) == 0 ||
         strncmp(message, "actor-init-instance:", 20) == 0 || strncmp(message, "intro-horse: object lookup", 26) == 0 ||
         strncmp(message, "intro-horse: actor object found", 31) == 0) &&
        strstr(message, "failed") == 0 && strstr(message, "FAILED") == 0 && strstr(message, "invalid") == 0 &&
        strstr(message, "missing") == 0 && strstr(message, "BAD") == 0)
        return;
    if (!sTraceEnabled) return;
    if (!sFullDetailTrace && strncmp(message, "graph:", 6) != 0 &&
        strncmp(message, "rsx-watchdog:", 13) != 0 &&
        strncmp(message, "trace:", 6) != 0 &&
        strncmp(message, "scene-draw:", 11) != 0 &&
        strncmp(message, "play-draw:", 10) != 0 &&
        strncmp(message, "actor-draw", 10) != 0 &&
        strncmp(message, "cutscene", 8) != 0 &&
        strncmp(message, "intro-route", 11) != 0 &&
        strncmp(message, "scene-init", 10) != 0 &&
        strncmp(message, "rsx-present:", 12) != 0)
        return;
    /* Performance: drop the lines that repeat on every Play frame. Each kept
     * line costs ~10 file-system calls (four HDD trace files plus USB). */
    if (sCompactPlayTrace &&
        (strncmp(message, "graph: diagnostic", 17) == 0 ||
         strncmp(message, "graph: Play frame ", 18) == 0 ||
         strncmp(message, "fast3d-rsx: upstream triangles", 29) == 0 ||
         strncmp(message, "actor-draw:", 11) == 0 ||
         strncmp(message, "fast3d-texture:", 15) == 0 ||
         strncmp(message, "fast3d-segment:", 15) == 0))
        return;
    if (sCompactPlayTrace &&
        strncmp(message, "graph: Play", 11) != 0 &&
        strncmp(message, "graph: diagnostic", 17) != 0 &&
        strncmp(message, "graph: holding", 14) != 0 &&
        strncmp(message, "graph: FileChoose", 17) != 0 &&
        strncmp(message, "fast3d-rsx: upstream triangles", 29) != 0 &&
        strncmp(message, "rsx-watchdog:", 13) != 0 &&
        strncmp(message, "trace:", 6) != 0 &&
        strstr(message, "failed") == 0 && strstr(message, "FAILED") == 0 &&
        strstr(message, "error") == 0 &&
        (strstr(message, "fault") == 0 || strstr(message, "default") != 0) && /* "default" is not a fault */
        strstr(message, "unsupported") == 0 && strstr(message, "unresolved") == 0 &&
        strstr(message, "invalid") == 0 && strstr(message, "missing") == 0 &&
        strstr(message, "rejected") == 0 && strstr(message, "timeout") == 0)
        return;
    if (strcmp(message, "skel-draw: entered real Link flex skeleton") == 0) {
        sCompactLinkSkeletonTrace = 1;
        activateLink = 1;
    }
    if (sCompactLinkSkeletonTrace &&
        ((strncmp(message, "player-limb-default:", 20) == 0) ||
         (strncmp(message, "player-limb-common:", 19) == 0) ||
         (strncmp(message, "player-limb-post:", 17) == 0 &&
          strncmp(message, "player-limb-post: right hand", 28) != 0) ||
         (strncmp(message, "skel-draw:", 10) == 0 &&
          strncmp(message, "skel-draw: enter limb=", 22) != 0 &&
          strcmp(message, "skel-draw: entered real Link flex skeleton") != 0 &&
          strcmp(message, "skel-draw: real Link flex skeleton completed") != 0 &&
          strstr(message, "invalid") == 0 && strstr(message, "missing") == 0)))
        return;
    if (sCompactTitleTrace) {
        /* The first complete frame already records the detailed call ladder.
         * Preserve frame boundaries, RSX results, errors and watchdog output
         * without 130 repeated filesystem writes for each subsequent frame. */
        if (strncmp(message, "fast3d-rsx:", 11) != 0 &&
            strncmp(message, "gfx-process: native Fast3D", 26) != 0 &&
            strncmp(message, "graph: title frame", 18) != 0 &&
            strncmp(message, "graph: diagnostic", 17) != 0 &&
            strncmp(message, "graph: before Graph_", 20) != 0 &&
            strncmp(message, "graph: after Graph_", 19) != 0 &&
            strncmp(message, "graph-update: completed", 23) != 0 &&
            strncmp(message, "title-main: before Title_", 25) != 0 &&
            strncmp(message, "title-main: after Title_", 24) != 0 &&
            strncmp(message, "title-calc:", 11) != 0 &&
            strncmp(message, "title-main: native fade", 23) != 0 &&
            strcmp(message, "title-draw: completed") != 0 &&
            strncmp(message, "rsx-watchdog:", 13) != 0 &&
            strncmp(message, "trace:", 6) != 0 &&
            strstr(message, "failed") == 0 && strstr(message, "FAILED") == 0 &&
            strstr(message, "error") == 0 && strstr(message, "fault") == 0)
            return;
    } else if (strcmp(message, "graph: title frame 1 complete; testing frame resume") == 0) {
        activateCompact = 1;
    }
    ++sBootTraceLines;
    if (sTraceAsyncOn) {
        TraceAsyncAppend(message);
    } else {
        const int sync = 0;
        for (index = 0; index < sizeof(sTracePaths) / sizeof(sTracePaths[0]); ++index) {
            TraceToDescriptor(index, message, sync && index == 0U);
        }
        UsbTrace(message, 0);
    }
    if (activateLink)
        oot_ps3_early_trace("trace: compact Link limb success markers active");
    if (sCompactTitleTrace && strcmp(message,
            "graph: diagnostic release Title arena; enter Opening_Init") == 0) {
        sCompactTitleTrace = 0;
        oot_ps3_early_trace("trace: full detail restored for Opening and Play");
    }
    if (activateCompact) {
        sCompactTitleTrace = 1;
        oot_ps3_early_trace("trace: compact title-frame mode active after full frame-one trace");
    }
}

void oot_ps3_trace_compact_play(void) {
    /* Full detail is only for a state's setup and first frame. Leaving it on let
     * per-frame lines containing "missing"/"failed" through the compact filter
     * (title stub: 2 player-limb-ik lines per frame, 30-60 ms/frame of trace I/O). */
    sFullDetailTrace = 0;
    sCompactPlayTrace = 1;
    oot_ps3_early_trace("trace: compact Play sequence logging active after full first frame");
}

void oot_ps3_trace_full_detail(void) {
    sCompactPlayTrace = 0;
    sFullDetailTrace = 1;
    oot_ps3_early_trace("trace: full detail logging re-enabled for late Play frames");
}

static void OotPs3TraceReset(const char* message) {
    unsigned int index;
    sBootTraceLines = 0;
    sCompactTitleTrace = 0;
    sCompactLinkSkeletonTrace = 0;
    sCompactPlayTrace = 0;
    for (index = 0; index < sizeof(sTracePaths) / sizeof(sTracePaths[0]); ++index) {
        if (sTraceDescriptors[index] >= 0) (void)close(sTraceDescriptors[index]);
        sTraceDescriptors[index] = open(sTracePaths[index], O_WRONLY | O_CREAT | O_TRUNC, 0666);
        TraceToDescriptor(index, message, 1);
    }
    UsbTrace(message, 1);
}

int main(int argc, char* argv[]) {
    {
        const int flag = open(OOT_PS3_TRACE_ON_PATH, O_RDONLY);
        sTraceEnabled = flag >= 0;
        if (flag >= 0) (void)close(flag);
    }
    if (sTraceEnabled) {
        OotPs3TraceReset("v48 audio-defer probe: native main wrapper entered");
        oot_ps3_early_trace("trace: persistent HDD handles and USB mirror active (USRDIR/trace_on)");
        TraceAsyncStart();
        oot_ps3_early_trace(sTraceAsyncOn ? "trace: asynchronous writer thread active (200 ms)"
                                          : "trace: asynchronous writer unavailable; writing synchronously");
    }
    oot_ps3_early_trace("P200: before early isolated RSX startup");
    if (!oot_ps3_shipwright_video_start()) {
        oot_ps3_early_trace("P200: early isolated RSX startup failed; exiting");
        return 0;
    }
    oot_ps3_early_trace("P200: early isolated RSX startup succeeded");
    oot_ps3_early_trace("P200: native main wrapper entering Shipwright");
    return oot_shipwright_main(argc, argv);
}

/* The GameInteractor hook registry is never constructed on PS3; calling into it
 * hangs. The final link wraps GameInteractor_Should to this function, so every
 * hook query in the game returns its vanilla default result instead. */
_Bool __wrap_GameInteractor_Should(int flag, unsigned int result, ...) {
    (void)flag;
    return result != 0U;
}

/* Clean exit: XMB "Quit Game" (SYSUTIL_EXIT_GAME) or SELECT on any controller.
 * Without this the system forced a shutdown (three beeps and a restart). */
void oot_ps3_early_trace_flush_usb(void);
void oot_ps3_pad_refresh(void);
int oot_ps3_pad_select_held(void);
static volatile int sPs3ExitRequested = 0;
static void Ps3SysutilHandler(u64 status, u64 param, void* userdata) {
    (void)param;
    (void)userdata;
    if (status == SYSUTIL_EXIT_GAME) sPs3ExitRequested = 1;
}
void oot_ps3_poll_exit(void) {
    static int registered = 0;
    static int padReady = 0;
    if (!registered) {
        sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT0, Ps3SysutilHandler, NULL);
        registered = 1;
    }
    sysUtilCheckCallback();
    if (!padReady) {
        ioPadInit(7);
        padReady = 1;
    }
    /* Shared pad state (shipwright_os_ps3.c): reading the pad here directly
     * would consume button changes the game needs. */
    oot_ps3_pad_refresh();
    if (oot_ps3_pad_select_held()) {
        oot_ps3_early_trace("xmb: SELECT pressed");
        sPs3ExitRequested = 1;
    }
    if (sPs3ExitRequested) {
        oot_ps3_early_trace("xmb: exit requested; leaving cleanly");
        oot_ps3_audio_shutdown();
        oot_ps3_early_trace_flush_usb();
        exit(0);
    }
}
