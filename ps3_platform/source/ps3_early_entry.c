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
#include <sys/stat.h>
#include <time.h>

/* Yield the PPU thread (for C++ files that can't include PS3 system headers). */
void sysThreadYieldPs3(void) {
    sysThreadYield();
}

/* Waiting in a lock loop. sysThreadYield() only hands the CPU to threads of equal or
 * higher priority: a high-priority thread yield-spinning on a lock held by a
 * lower-priority one (audio thread 900 vs game thread 1001, with the render thread
 * keeping the other hardware thread busy) starved the holder for minutes. Spin a
 * little, then really sleep so any thread can run. */
void oot_ps3_lock_backoff(unsigned int* spins) {
    if (++*spins < 32) return;
    usleep(20);
}

/* The game archive (oot.o2r, ~33 MB) copied into RAM by a background thread at boot.
 * Every first use of a resource used to open/seek/read/close the file on the hard
 * drive: a scene change does hundreds of those (50-330 ms hitches). Until the copy is
 * complete the bridge keeps reading the file; afterwards it reads from memory. Memory
 * comes from the system (1 MB pages) next to the 64 MB heap, not from the heap. */
#include <sys/memory.h>
unsigned long long oot_ps3_microseconds_now(void);
void oot_ps3_early_trace(const char* message);
static unsigned char* sArchiveRam;
static unsigned long long sArchiveRamSize;
static volatile int sArchiveRamReady;
static char sArchiveRamPath[256];
static void ArchiveRamThread(void* argument) {
    const unsigned long long t0 = oot_ps3_microseconds_now();
    unsigned long long done = 0;
    char line[160];
    int fd = open(sArchiveRamPath, O_RDONLY);
    (void)argument;
    if (fd >= 0) {
        while (done < sArchiveRamSize) {
            const unsigned long long chunk = sArchiveRamSize - done < (1U << 20) ? sArchiveRamSize - done : (1U << 20);
            const ssize_t got = read(fd, sArchiveRam + done, (size_t)chunk);
            if (got <= 0) break;
            done += (unsigned long long)got;
        }
        close(fd);
    }
    if (done == sArchiveRamSize) {
        __sync_synchronize();
        sArchiveRamReady = 1;
    }
    snprintf(line, sizeof(line), "trace: archive %s in RAM: %llu of %llu bytes in %llu ms", sArchiveRamReady ? "loaded" : "FAILED",
             done, sArchiveRamSize, (oot_ps3_microseconds_now() - t0) / 1000ULL);
    oot_ps3_early_trace(line);
    sysThreadExit(0);
}
int oot_ps3_archive_preload(const char* path, unsigned long long size) {
    sys_mem_addr_t address = 0;
    sys_ppu_thread_t thread;
    const unsigned long long rounded = (size + 0xFFFFFULL) & ~0xFFFFFULL;
    if (sArchiveRam != NULL || size == 0 || size > (48ULL << 20) || strlen(path) >= sizeof(sArchiveRamPath)) return 0;
    if (sysMemoryAllocate(rounded, SYS_MEMORY_PAGE_SIZE_1M, &address) != 0) {
        oot_ps3_early_trace("trace: archive RAM copy: no system memory; reading from the hard drive");
        return 0;
    }
    sArchiveRam = (unsigned char*)(uintptr_t)address;
    sArchiveRamSize = size;
    strcpy(sArchiveRamPath, path);
    if (sysThreadCreate(&thread, ArchiveRamThread, NULL, 1200, 64 * 1024, 0, "oot-archive") != 0) {
        oot_ps3_early_trace("trace: archive RAM copy: thread failed");
        return 0;
    }
    return 1;
}
/* The archive bytes when the RAM copy of `path` is complete, else NULL. */
const unsigned char* oot_ps3_archive_ram(const char* path, unsigned long long* size) {
    if (!sArchiveRamReady || strcmp(path, sArchiveRamPath) != 0) return NULL;
    *size = sArchiveRamSize;
    return sArchiveRam;
}

/* Threads and semaphores for files
 * render thread in shipwright_bootstrap_ps3.cpp). Semaphore handles are LV2 ids. */
#include <sys/sem.h>
int oot_ps3_thread_start(void (*entry)(void*), void* arg, int priority, unsigned int stackBytes, const char* name) {
    sys_ppu_thread_t thread;
    return sysThreadCreate(&thread, entry, arg, priority, stackBytes, 0, (char*)name) == 0;
}
unsigned int oot_ps3_sem_create(int initial, const char* name) {
    sys_sem_attr_t attr;
    sys_sem_t sem = 0;
    memset(&attr, 0, sizeof(attr));
    attr.attr_protocol = SYS_SEM_ATTR_PROTOCOL;
    attr.attr_pshared = SYS_SEM_ATTR_PSHARED;
    strncpy(attr.name, name, sizeof(attr.name) - 1);
    if (sysSemCreate(&sem, &attr, initial, 1) != 0) return 0;
    return sem;
}
void oot_ps3_thread_exit(void) {
    sysThreadExit(0);
}
void oot_ps3_sem_wait(unsigned int sem) {
    sysSemWait(sem, 0);
}
void oot_ps3_sem_post(unsigned int sem) {
    sysSemPost(sem, 1);
}

/* The resource bridge's lock (both the game thread and the render thread load
 * resources). Recursive, owned by the PPU thread pointer (r13); a spin lock
 * because the bridge is entered hundreds of times per frame and is almost never
 * contended (a system call each time would cost more than the lookup). */
static volatile unsigned long long sBridgeOwner;
static unsigned int sBridgeDepth;
static inline unsigned long long Ps3ThreadToken(void) {
    unsigned long long r13;
    __asm__ volatile("mr %0, 13" : "=r"(r13));
    return r13;
}
void oot_ps3_bridge_lock(void) {
    const unsigned long long me = Ps3ThreadToken();
    unsigned int spins = 0;
    if (sBridgeOwner == me) {
        ++sBridgeDepth;
        return;
    }
    while (!__sync_bool_compare_and_swap(&sBridgeOwner, 0ULL, me)) oot_ps3_lock_backoff(&spins);
    sBridgeDepth = 1;
}
void oot_ps3_bridge_unlock(void) {
    if (--sBridgeDepth == 0) {
        __sync_synchronize();
        sBridgeOwner = 0;
    }
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

/* ---- Player logs (2026-11-12) ----
 * Always on (USRDIR/logs/log_off disables): the current session goes to
 * USRDIR/logs/latest.txt (latest-part1.txt holds the earlier 8 MB when it rolls over).
 * At boot the last session becomes previous.txt; if it never reached a clean exit
 * (crash, freeze, power off) it is also kept as logs/crash-<UTC date>.txt (newest 5).
 * Any USB drive (/dev_usb000..007, checked at boot and every 5 s) gets a live copy,
 * ShipOfGadeguion-log.txt, and every crash log as ShipOfGadeguion-crash-<date>.txt,
 * so players can just drag a file off the stick. */
#define OOT_PS3_USRDIR "/dev_hdd0/game/OOTP30048/USRDIR"
#define OOT_PS3_LOG_DIR OOT_PS3_USRDIR "/logs"
#define OOT_PS3_LOG_LATEST OOT_PS3_LOG_DIR "/latest.txt"
#define OOT_PS3_LOG_PART1 OOT_PS3_LOG_DIR "/latest-part1.txt"
#define OOT_PS3_LOG_PREVIOUS OOT_PS3_LOG_DIR "/previous.txt"
#define OOT_PS3_LOG_RUNNING OOT_PS3_LOG_DIR "/session-running"
#define OOT_PS3_LOG_OFF OOT_PS3_LOG_DIR "/log_off"
#define OOT_PS3_USB_LOG_NAME "ShipOfGadeguion-log.txt"
#define OOT_PS3_USB_CRASH_PREFIX "ShipOfGadeguion-"
#ifndef OOT_PS3_APP_VER
#define OOT_PS3_APP_VER "dev"
#endif
enum { LOG_ROLL_BYTES = 8 * 1024 * 1024, LOG_KEEP_CRASHES = 5 };

static const char* const sTracePaths[] = {
    OOT_PS3_LOG_LATEST,
};
static unsigned int TraceLength(const char* text);

static int sUsbTraceFd = -1;
static char sUsbRoot[16];
static unsigned long long sLogBytes;
static unsigned int sBootTraceLines;
static int sCompactTitleTrace;
static int sCompactLinkSkeletonTrace;
static int sCompactPlayTrace;
static int sFullDetailTrace;
static int sTraceDescriptors[] = { -1 };

static int PathExists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static void CopyFileToFd(const char* path, int out) {
    static char buffer[64 * 1024];
    const int in = open(path, O_RDONLY);
    int n;
    if (in < 0) return;
    while ((n = (int)read(in, buffer, sizeof(buffer))) > 0) {
        if (write(out, buffer, (size_t)n) != n) break;
    }
    (void)close(in);
}

/* The current session's log (both parts) into a new file. */
static int WriteSessionCopy(const char* path, const char* header, const char* part1, const char* latest) {
    const int out = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out < 0) return 0;
    if (header != 0) (void)write(out, header, TraceLength(header));
    if (part1 != 0) CopyFileToFd(part1, out);
    if (latest != 0) CopyFileToFd(latest, out);
    (void)fsync(out);
    (void)close(out);
    return 1;
}

static void UtcStamp(char* out, unsigned int size) {
    static unsigned int counter;
    time_t now = time(0);
    struct tm parts;
    if (now > 0 && gmtime_r(&now, &parts) != 0) {
        snprintf(out, size, "%04d%02d%02d-%02d%02d%02d", parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday,
                 parts.tm_hour, parts.tm_min, parts.tm_sec);
        if (counter++ != 0) {
            const unsigned int len = TraceLength(out);
            snprintf(out + len, size - len, "-%u", counter);
        }
    } else {
        snprintf(out, size, "unknown-time-%u", ++counter);
    }
}

/* Names in logs/ starting with "crash-": keep the newest LOG_KEEP_CRASHES (names sort by date). */
static void PruneCrashLogs(void) {
    char names[32][64];
    unsigned int count = 0, i, j;
    s32 dir;
    sysFSDirent entry;
    u64 got = 0;
    if (sysLv2FsOpenDir(OOT_PS3_LOG_DIR, &dir) != 0) return;
    while (sysLv2FsReadDir(dir, &entry, &got) == 0 && got != 0 && count < 32) {
        if (strncmp(entry.d_name, "crash-", 6) == 0 && TraceLength(entry.d_name) < 64) {
            strcpy(names[count++], entry.d_name);
        }
    }
    (void)sysLv2FsCloseDir(dir);
    for (i = 0; i < count; ++i)
        for (j = i + 1; j < count; ++j)
            if (strcmp(names[j], names[i]) < 0) {
                char swap[64];
                strcpy(swap, names[i]);
                strcpy(names[i], names[j]);
                strcpy(names[j], swap);
            }
    for (i = 0; i + LOG_KEEP_CRASHES < count; ++i) {
        char path[128];
        snprintf(path, sizeof(path), "%s/%s", OOT_PS3_LOG_DIR, names[i]);
        (void)unlink(path);
    }
}

static int UsbFindRoot(char* root, unsigned int size) {
    int i;
    for (i = 0; i < 8; ++i) {
        struct stat st;
        snprintf(root, size, "/dev_usb%03d", i);
        if (stat(root, &st) == 0) return 1;
    }
    return 0;
}

/* Every logs/crash-*.txt not yet on the stick -> <usb>/ShipOfGadeguion-crash-*.txt */
static void UsbDeliverCrashLogs(const char* root) {
    s32 dir;
    sysFSDirent entry;
    u64 got = 0;
    if (sysLv2FsOpenDir(OOT_PS3_LOG_DIR, &dir) != 0) return;
    while (sysLv2FsReadDir(dir, &entry, &got) == 0 && got != 0) {
        char from[160], to[160];
        if (strncmp(entry.d_name, "crash-", 6) != 0 || TraceLength(entry.d_name) > 100) continue;
        snprintf(to, sizeof(to), "%s/%s%.100s", root, OOT_PS3_USB_CRASH_PREFIX, entry.d_name);
        if (PathExists(to)) continue;
        snprintf(from, sizeof(from), "%s/%.100s", OOT_PS3_LOG_DIR, entry.d_name);
        (void)WriteSessionCopy(to, 0, 0, from);
    }
    (void)sysLv2FsCloseDir(dir);
}

/* Caller holds the flush lock (or the writer thread isn't running yet). */
static void UsbAttach(void) {
    char root[16], path[96];
    int fd;
    if (sUsbTraceFd >= 0 || !UsbFindRoot(root, sizeof(root))) return;
    UsbDeliverCrashLogs(root);
    snprintf(path, sizeof(path), "%s/%s", root, OOT_PS3_USB_LOG_NAME);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) return;
    if (sTraceDescriptors[0] >= 0) {
        CopyFileToFd(OOT_PS3_LOG_PART1, fd);
        CopyFileToFd(OOT_PS3_LOG_LATEST, fd);
    }
    (void)fsync(fd);
    strcpy(sUsbRoot, root);
    sUsbTraceFd = fd;
}

static void UsbDetach(void) {
    if (sUsbTraceFd >= 0) (void)close(sUsbTraceFd);
    sUsbTraceFd = -1;
    sUsbRoot[0] = '\0';
}

static void UsbWrite(const char* data, unsigned int length) {
    if (sUsbTraceFd < 0 || length == 0) return;
    if (write(sUsbTraceFd, data, length) != (int)length) UsbDetach(); /* stick pulled out or full */
}

static void UsbWriteLine(const char* message) {
    UsbWrite(message, TraceLength(message));
    UsbWrite("\n", 1);
}

/* latest.txt reached LOG_ROLL_BYTES: it becomes latest-part1.txt (the older part is dropped). */
static void LogRollOver(void) {
    static const char note[] = "log: continued; the previous 8 MB of this session are in latest-part1.txt\n";
    if (sTraceDescriptors[0] < 0) return;
    (void)close(sTraceDescriptors[0]);
    (void)unlink(OOT_PS3_LOG_PART1);
    (void)rename(OOT_PS3_LOG_LATEST, OOT_PS3_LOG_PART1);
    sTraceDescriptors[0] = open(OOT_PS3_LOG_LATEST, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (sTraceDescriptors[0] >= 0) (void)write(sTraceDescriptors[0], note, sizeof(note) - 1U);
    sLogBytes = 0;
    if (sUsbTraceFd >= 0) { /* keep the stick copy = part1 + latest */
        UsbDetach();
        UsbAttach();
    }
}

/* Asynchronous trace: once the writer thread runs, oot_ps3_early_trace only appends to
 * this buffer and the thread writes it to the log files every 200 ms. Exit/watchdog
 * paths flush first. */
enum { TRACE_ASYNC_BYTES = 256 * 1024 };
static char sTraceAsync[TRACE_ASYNC_BYTES];
static char sTraceAsyncOut[TRACE_ASYNC_BYTES];
static volatile unsigned int sTraceAsyncLen;
static volatile int sTraceAsyncLock;
static volatile int sTraceFlushLock;
static volatile int sTraceAsyncOn;
static sys_ppu_thread_t sTraceAsyncThread;

static void TraceSpinLock(volatile int* lock) {
    unsigned int spins = 0;
    while (__sync_lock_test_and_set(lock, 1)) oot_ps3_lock_backoff(&spins);
}
static void TraceSpinUnlock(volatile int* lock) {
    __sync_lock_release(lock);
}

static void TraceAsyncFlush(void) {
    unsigned int length;
    TraceSpinLock(&sTraceFlushLock);
    TraceSpinLock(&sTraceAsyncLock);
    length = sTraceAsyncLen;
    if (length != 0) memcpy(sTraceAsyncOut, sTraceAsync, length);
    sTraceAsyncLen = 0;
    TraceSpinUnlock(&sTraceAsyncLock);
    if (length != 0) {
        if (sTraceDescriptors[0] >= 0) (void)write(sTraceDescriptors[0], sTraceAsyncOut, length);
        UsbWrite(sTraceAsyncOut, length);
        sLogBytes += length;
        if (sLogBytes >= LOG_ROLL_BYTES) LogRollOver();
    }
    TraceSpinUnlock(&sTraceFlushLock);
}

static void TraceAsyncAppend(const char* message) {
    const unsigned int length = TraceLength(message);
    /* "[seconds.milliseconds] " since boot in front of every line: how long things took. */
    char stamp[24];
    static unsigned long long sTraceT0;
    unsigned long long now = oot_ps3_microseconds_now();
    unsigned int stampLen;
    if (sTraceT0 == 0) sTraceT0 = now;
    now -= sTraceT0;
    stampLen = (unsigned int)snprintf(stamp, sizeof(stamp), "[%llu.%03llu] ", now / 1000000ULL, (now / 1000ULL) % 1000ULL);
    if (stampLen >= sizeof(stamp)) stampLen = 0;
    if (length + stampLen + 1U > TRACE_ASYNC_BYTES) return;
    TraceSpinLock(&sTraceAsyncLock);
    if (sTraceAsyncLen + stampLen + length + 1U > TRACE_ASYNC_BYTES) {
        TraceSpinUnlock(&sTraceAsyncLock);
        TraceAsyncFlush(); /* full: write out now (rare) */
        TraceSpinLock(&sTraceAsyncLock);
    }
    memcpy(sTraceAsync + sTraceAsyncLen, stamp, stampLen);
    sTraceAsyncLen += stampLen;
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
        ++ticks;
        if ((ticks % 10U) == 0U) {
            TraceSpinLock(&sTraceFlushLock);
            if (sTraceDescriptors[0] >= 0) (void)fsync(sTraceDescriptors[0]);
            if (sUsbTraceFd >= 0) (void)fsync(sUsbTraceFd);
            TraceSpinUnlock(&sTraceFlushLock);
        }
        if ((ticks % 25U) == 0U && sUsbTraceFd < 0) { /* a USB drive plugged in later */
            TraceSpinLock(&sTraceFlushLock);
            UsbAttach();
            TraceSpinUnlock(&sTraceFlushLock);
            if (sUsbTraceFd >= 0) oot_ps3_early_trace("trace: USB drive found; log copied to it");
        }
    }
}

static void TraceAsyncStart(void) {
    if (sTraceAsyncOn) return;
    UsbAttach();
    if (sUsbTraceFd >= 0) {
        char line[96];
        snprintf(line, sizeof(line), "trace: log also written to %s/%s", sUsbRoot, OOT_PS3_USB_LOG_NAME);
        oot_ps3_early_trace(line);
    }
    if (sysThreadCreate(&sTraceAsyncThread, TraceAsyncThread, NULL, 1800, 64 * 1024, 0, "oot-trace") == 0) {
        sTraceAsyncOn = 1;
    }
}

void oot_ps3_early_trace_flush_usb(void) {
    if (sTraceAsyncOn) TraceAsyncFlush();
    TraceSpinLock(&sTraceFlushLock);
    if (sTraceDescriptors[0] >= 0) (void)fsync(sTraceDescriptors[0]);
    if (sUsbTraceFd >= 0) (void)fsync(sUsbTraceFd);
    TraceSpinUnlock(&sTraceFlushLock);
}

/* The game is about to leave because of a freeze (watchdog): keep this session as a
 * crash log on the HDD and on the USB drive right away. */
void oot_ps3_log_save_crash(const char* reason) {
    char stamp[48], path[160], header[256];
    if (sTraceDescriptors[0] < 0) return;
    oot_ps3_early_trace_flush_usb();
    TraceSpinLock(&sTraceFlushLock);
    UtcStamp(stamp, sizeof(stamp));
    snprintf(header, sizeof(header), "Ship of Gadeguion %s crash log (%s). Please share this file.\n",
             OOT_PS3_APP_VER, reason != 0 ? reason : "crash");
    snprintf(path, sizeof(path), "%s/crash-%s.txt", OOT_PS3_LOG_DIR, stamp);
    (void)WriteSessionCopy(path, header, OOT_PS3_LOG_PART1, OOT_PS3_LOG_LATEST);
    PruneCrashLogs();
    if (sUsbTraceFd < 0) UsbAttach();
    if (sUsbRoot[0] != '\0') UsbDeliverCrashLogs(sUsbRoot);
    (void)unlink(OOT_PS3_LOG_RUNNING); /* already saved: not again at the next boot */
    TraceSpinUnlock(&sTraceFlushLock);
}

/* Clean exit (SELECT / XMB Quit Game). */
static void LogSessionEndedCleanly(void) {
    (void)unlink(OOT_PS3_LOG_RUNNING);
}

/* Boot: last session -> previous.txt (+ crash copy if it didn't end cleanly). */
static void LogBootRotate(void) {
    const int crashed = PathExists(OOT_PS3_LOG_RUNNING);
    (void)mkdir(OOT_PS3_LOG_DIR, 0777);
    if (PathExists(OOT_PS3_LOG_LATEST) || PathExists(OOT_PS3_LOG_PART1)) {
        (void)WriteSessionCopy(OOT_PS3_LOG_PREVIOUS, 0, OOT_PS3_LOG_PART1, OOT_PS3_LOG_LATEST);
        (void)unlink(OOT_PS3_LOG_PART1);
        (void)unlink(OOT_PS3_LOG_LATEST);
        if (crashed) {
            char stamp[48], path[160];
            UtcStamp(stamp, sizeof(stamp));
            snprintf(path, sizeof(path), "%s/crash-%s.txt", OOT_PS3_LOG_DIR, stamp);
            (void)WriteSessionCopy(path,
                                   "Ship of Gadeguion crash log: this session did not end cleanly "
                                   "(crash, freeze or power off). Please share this file.\n",
                                   OOT_PS3_LOG_PREVIOUS, 0);
            PruneCrashLogs();
        }
    }
    {
        const int marker = open(OOT_PS3_LOG_RUNNING, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (marker >= 0) (void)close(marker);
    }
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
/* The last message per thread (first 4 threads that trace), for the watchdog: with
 * the render thread the global "last trace" only shows whichever thread wrote last. */
#define TRACE_THREAD_SLOTS 4
static volatile unsigned long long sThreadSlotToken[TRACE_THREAD_SLOTS];
static char sThreadSlotTrace[TRACE_THREAD_SLOTS][128];
static void TraceThreadSlot(const char* message, unsigned int n) {
    const unsigned long long me = Ps3ThreadToken();
    unsigned int i;
    for (i = 0; i < TRACE_THREAD_SLOTS; ++i) {
        if (sThreadSlotToken[i] == me) break;
        if (sThreadSlotToken[i] == 0 && __sync_bool_compare_and_swap(&sThreadSlotToken[i], 0ULL, me)) break;
    }
    if (i == TRACE_THREAD_SLOTS) return;
    if (n > sizeof(sThreadSlotTrace[i]) - 1) n = sizeof(sThreadSlotTrace[i]) - 1;
    memcpy(sThreadSlotTrace[i], message, n);
    sThreadSlotTrace[i][n] = '\0';
}
void oot_ps3_trace_thread_steps(char* out, unsigned int size) {
    unsigned int i, used = 0;
    out[0] = '\0';
    for (i = 0; i < TRACE_THREAD_SLOTS && used < size; ++i) {
        if (sThreadSlotToken[i] == 0) continue;
        used += (unsigned int)snprintf(out + used, size - used, "[%llx] %s  ", sThreadSlotToken[i], sThreadSlotTrace[i]);
    }
}

const char* oot_ps3_last_trace(void) {
    return sPs3LastTrace;
}

/* Counts every tracer call (filtered or not). The stall watchdog treats new
 * trace calls as progress, so a slow scene load isn't mistaken for a hang. */
static volatile unsigned int sPs3TraceCount;
/* Which trace lines are called most (by message address; z_play.c reports the top 3
 * every 64 updates). */
#define TRACE_TOP_SLOTS 256
static const char* sTraceTopPtr[TRACE_TOP_SLOTS];
static unsigned int sTraceTopCount[TRACE_TOP_SLOTS];
static void TraceTopCount(const char* message) {
    unsigned int h = (unsigned int)(((uintptr_t)message >> 3) * 2654435761U) % TRACE_TOP_SLOTS;
    unsigned int probe;
    for (probe = 0; probe < 8; ++probe, h = (h + 1) % TRACE_TOP_SLOTS) {
        if (sTraceTopPtr[h] == message) { ++sTraceTopCount[h]; return; }
        if (sTraceTopPtr[h] == 0) { sTraceTopPtr[h] = message; sTraceTopCount[h] = 1; return; }
    }
}
void oot_ps3_trace_top(char* out, unsigned int size) {
    unsigned int best[3] = { TRACE_TOP_SLOTS, TRACE_TOP_SLOTS, TRACE_TOP_SLOTS };
    unsigned int i, k, used = 0;
    for (i = 0; i < TRACE_TOP_SLOTS; ++i) {
        if (sTraceTopPtr[i] == 0) continue;
        for (k = 0; k < 3; ++k) {
            if (best[k] == TRACE_TOP_SLOTS || sTraceTopCount[i] > sTraceTopCount[best[k]]) {
                unsigned int m;
                for (m = 2; m > k; --m) best[m] = best[m - 1];
                best[k] = i;
                break;
            }
        }
    }
    out[0] = '\0';
    for (k = 0; k < 3 && best[k] != TRACE_TOP_SLOTS && used + 8 < size; ++k) {
        char text[48];
        unsigned int n = 0;
        const char* message = sTraceTopPtr[best[k]];
        while (n < sizeof(text) - 1 && message[n] >= 32 && message[n] < 127) { text[n] = message[n]; ++n; }
        text[n] = '\0';
        used += (unsigned int)snprintf(out + used, size - used, "%u x '%s' ", sTraceTopCount[best[k]], text);
        if (used >= size) break;
    }
    memset(sTraceTopPtr, 0, sizeof(sTraceTopPtr));
    memset(sTraceTopCount, 0, sizeof(sTraceTopCount));
}
/* Log files are on by default (USRDIR/logs/log_off turns them off). The watchdog's
 * step bookkeeping runs either way. */
static int sTraceEnabled;

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
        if (message != 0) TraceTopCount(message);
        if (message != 0) {
            /* Copy only the message (strncpy zero-filled the whole buffer on each of the
             * ~1000 calls per update). Kept even with trace files off: the watchdog's
             * "stopped at" step. */
            unsigned int n = 0;
            while (n < sizeof(sPs3LastTrace) - 1 && message[n] != '\0') ++n;
            memcpy(sPs3LastTrace, message, n);
            sPs3LastTrace[n] = '\0';
            TraceThreadSlot(message, n);
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
        UsbWriteLine(message);
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
    sLogBytes = 0;
}

static void LogStart(const char* reason) {
    char line[160];
    OotPs3TraceReset("log: Ship of Gadeguion " OOT_PS3_APP_VER " (EBOOT built " __DATE__ " " __TIME__ ")");
    oot_ps3_early_trace(reason);
    {
        char stamp[48];
        UtcStamp(stamp, sizeof(stamp));
        snprintf(line, sizeof(line), "trace: session started %s UTC; log in USRDIR/logs/latest.txt", stamp);
        oot_ps3_early_trace(line);
    }
    TraceAsyncStart();
    oot_ps3_early_trace(sTraceAsyncOn ? "trace: asynchronous writer thread active (200 ms)"
                                      : "trace: asynchronous writer unavailable; writing synchronously");
}

/* Debug menu: log files on/off while running, remembered with USRDIR/logs/log_off. */
int oot_ps3_trace_enabled(void) {
    return sTraceEnabled;
}

void oot_ps3_trace_set_enabled(int on) {
    if (on && !sTraceEnabled) {
        (void)unlink(OOT_PS3_LOG_OFF);
        sTraceEnabled = 1;
        if (!sTraceAsyncOn) {
            LogBootRotate();
            LogStart("trace: log files enabled from the debug menu");
        }
        oot_ps3_early_trace("trace: log files on (debug menu)");
    } else if (!on && sTraceEnabled) {
        oot_ps3_early_trace("trace: log files off (debug menu)");
        oot_ps3_early_trace_flush_usb();
        sTraceEnabled = 0;
        {
            const int flag = open(OOT_PS3_LOG_OFF, O_WRONLY | O_CREAT, 0666);
            if (flag >= 0) (void)close(flag);
        }
        LogSessionEndedCleanly();
    }
}

void oot_ps3_setup_screen(void); /* ps3_setup_screen.c */

int main(int argc, char* argv[]) {
    (void)mkdir(OOT_PS3_LOG_DIR, 0777);
    sTraceEnabled = !PathExists(OOT_PS3_LOG_OFF);
    if (sTraceEnabled) {
        LogBootRotate();
        LogStart("trace: player log active (USRDIR/logs, plus any USB drive)");
    }
    oot_ps3_early_trace("P200: before early isolated RSX startup");
    if (!oot_ps3_shipwright_video_start()) {
        oot_ps3_early_trace("P200: early isolated RSX startup failed; exiting");
        return 0;
    }
    oot_ps3_early_trace("P200: early isolated RSX startup succeeded");
    oot_ps3_setup_screen(); /* no oot.o2r yet (or TRIANGLE held): install it from USB */
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
        oot_ps3_early_trace("log: session ended cleanly");
        oot_ps3_early_trace_flush_usb();
        if (sTraceEnabled) LogSessionEndedCleanly();
        exit(0);
    }
}
