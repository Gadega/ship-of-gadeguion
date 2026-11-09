/* Heap guard (diagnostic): finds buffer overruns that corrupt newlib's heap.
 *
 * Linked with --wrap=malloc/free/calloc/realloc. Every block gets a 32-byte
 * header (magic, size, check word, the two return addresses of the allocation)
 * and an 8-byte trailer pattern after the user bytes. Live blocks are kept in a
 * table, so the whole heap can be checked: every 64 game updates and by the stall
 * watchdog (a corrupted heap made malloc loop forever while holding its lock,
 * freezing the game and the audio thread). The report names the code that
 * allocated the damaged block (look the addresses up in oot-native.map) and
 * shows the bytes written past its end.
 *
 * Blocks from memalign or newlib's internal _malloc_r carry no header; free()
 * recognises them by the missing magic and passes them straight through.
 * PS3 system headers only.
 *
 * Release builds leave it off (OOT_PS3_HEAP_GUARD 0): the wrappers pass straight
 * through to newlib. Build with -DOOT_PS3_HEAP_GUARD=1 to hunt heap corruption. */
#ifndef OOT_PS3_HEAP_GUARD
#define OOT_PS3_HEAP_GUARD 1 /* TEMPORARY: render-thread corruption hunt (2026-11-08) */
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/thread.h>

void oot_ps3_early_trace(const char* message);
unsigned int oot_ps3_heap_used_kb(void);
unsigned int oot_ps3_heap_refusals(void);

void* __real_malloc(size_t size);
void __real_free(void* ptr);
void* __real_calloc(size_t count, size_t size);
void* __real_realloc(void* ptr, size_t size);

#define GUARD_MAGIC 0x4F4F5448U /* "OOTH" */
#define GUARD_FREED 0x46524545U /* "FREE" */
#define GUARD_TRAILER 8U
#define GUARD_TABLE_BITS 18
#define GUARD_TABLE_SIZE (1U << GUARD_TABLE_BITS)
#define GUARD_TOMBSTONE ((uintptr_t)1)

typedef struct {
    uint32_t magic;
    uint32_t size;
    uint32_t check;
    uint32_t reserved;
    uint64_t caller0;
    uint64_t caller1;
} GuardHeader; /* 32 bytes: user pointers stay 16-byte aligned */

static const unsigned char kTrailer[GUARD_TRAILER] = { 0xFD, 0xFD, 0xFD, 0xFD, 0xFD, 0xFD, 0xFD, 0xFD };
static uintptr_t sTable[GUARD_TABLE_SIZE]; /* user pointers; 0 empty, 1 tombstone */
static unsigned int sLive;
static volatile int sLock;
static int sTableFull;

/* Damage found at free(): kept here and reported by the next check (free must not trace). */
typedef struct {
    uintptr_t ptr;
    uint32_t size;
    uint64_t caller0, caller1, freeCaller;
    unsigned char after[16];
    int headerBad;
} GuardDamage;
static GuardDamage sFreeDamage[8];

/* Quarantine: freed blocks are filled with 0xDB and kept for a while before the real
 * free. A write into one (use after free) changes the pattern; it is found when the
 * block leaves the quarantine or by a scan, and it can no longer corrupt the heap. */
#define QUARANTINE_ENTRIES 8192
#define QUARANTINE_MAX_BYTES (8U * 1024U * 1024U)
#define QUARANTINE_MAX_BLOCK (1024U * 1024U)
typedef struct {
    unsigned char* user;
    uint32_t size;
    uint64_t freeCaller;
} QuarantineEntry;
static QuarantineEntry sQuarantine[QUARANTINE_ENTRIES];
static unsigned int sQHead, sQCount;
static uint32_t sQBytes;
typedef struct {
    uintptr_t ptr;
    uint32_t size, offset;
    uint64_t caller0, caller1, freeCaller;
    unsigned char bytes[16];
} UafDamage;
static UafDamage sUaf[8];
static unsigned int sUafCount;
static unsigned int sFreeDamageCount;
static unsigned int sReported;

static void Lock(void) {
    while (__sync_lock_test_and_set(&sLock, 1)) sysThreadYield();
}
static void Unlock(void) {
    __sync_lock_release(&sLock);
}

static uint32_t CheckWord(uintptr_t user, uint32_t size) {
    return GUARD_MAGIC ^ size ^ (uint32_t)(user >> 4) ^ 0x5A5A0000U;
}

static unsigned int Hash(uintptr_t p) {
    uint64_t h = (uint64_t)p * 0x9E3779B97F4A7C15ULL;
    return (unsigned int)(h >> (64 - GUARD_TABLE_BITS));
}

static void TableInsert(uintptr_t p) {
    unsigned int i, n;
    if (sTableFull) return;
    if (sLive >= GUARD_TABLE_SIZE - GUARD_TABLE_SIZE / 8) {
        sTableFull = 1; /* stop tracking; guards on free still work */
        return;
    }
    i = Hash(p);
    for (n = 0; n < GUARD_TABLE_SIZE; ++n, i = (i + 1) & (GUARD_TABLE_SIZE - 1)) {
        if (sTable[i] == 0 || sTable[i] == GUARD_TOMBSTONE) {
            sTable[i] = p;
            ++sLive;
            return;
        }
    }
}

static void TableRemove(uintptr_t p) {
    unsigned int i, n;
    i = Hash(p);
    for (n = 0; n < GUARD_TABLE_SIZE; ++n, i = (i + 1) & (GUARD_TABLE_SIZE - 1)) {
        if (sTable[i] == 0) return;
        if (sTable[i] == p) {
            sTable[i] = GUARD_TOMBSTONE;
            --sLive;
            return;
        }
    }
}

/* First changed byte of a quarantined block, or -1 if intact. */
static int QuarantineCheck(const QuarantineEntry* q) {
    uint32_t i;
    for (i = 0; i < q->size; ++i) {
        if (q->user[i] != 0xDB) return (int)i;
    }
    return -1;
}

static void QuarantineRecord(const QuarantineEntry* q, int offset) {
    const GuardHeader* h = (const GuardHeader*)(q->user - sizeof(GuardHeader));
    UafDamage* d;
    if (sUafCount >= sizeof(sUaf) / sizeof(sUaf[0])) return;
    d = &sUaf[sUafCount++];
    d->ptr = (uintptr_t)q->user;
    d->size = q->size;
    d->offset = (uint32_t)offset;
    d->caller0 = h->caller0;
    d->caller1 = h->caller1;
    d->freeCaller = q->freeCaller;
    memcpy(d->bytes, q->user + offset, q->size - (uint32_t)offset < 16 ? q->size - (uint32_t)offset : 16);
}

/* Releases the oldest quarantined block (lock held). */
static void QuarantineEvict(void) {
    QuarantineEntry* q;
    int offset;
    if (sQCount == 0) return;
    q = &sQuarantine[(sQHead + QUARANTINE_ENTRIES - sQCount) % QUARANTINE_ENTRIES];
    offset = QuarantineCheck(q);
    if (offset >= 0) QuarantineRecord(q, offset);
    sQBytes -= q->size;
    --sQCount;
    __real_free(q->user - sizeof(GuardHeader));
}

static void* Guard(void* raw, size_t size, uint64_t c0, uint64_t c1) {
    GuardHeader* h;
    unsigned char* user;
    if (raw == NULL) return NULL;
    h = (GuardHeader*)raw;
    user = (unsigned char*)raw + sizeof(GuardHeader);
    h->magic = GUARD_MAGIC;
    h->size = (uint32_t)size;
    h->check = CheckWord((uintptr_t)user, (uint32_t)size);
    h->reserved = 0;
    h->caller0 = c0;
    h->caller1 = c1;
    memcpy(user + size, kTrailer, GUARD_TRAILER);
    Lock();
    TableInsert((uintptr_t)user);
    Unlock();
    return user;
}

/* 1 when `user` carries a valid header (a guarded block). */
static int IsGuarded(const void* user) {
    const GuardHeader* h = (const GuardHeader*)((const unsigned char*)user - sizeof(GuardHeader));
    return h->magic == GUARD_MAGIC && h->check == CheckWord((uintptr_t)user, h->size);
}

static void RecordDamage(const unsigned char* user, const GuardHeader* h, int headerBad, uint64_t freeCaller) {
    GuardDamage* d;
    if (sFreeDamageCount >= sizeof(sFreeDamage) / sizeof(sFreeDamage[0])) return;
    d = &sFreeDamage[sFreeDamageCount++];
    d->ptr = (uintptr_t)user;
    d->size = h->size;
    d->caller0 = h->caller0;
    d->caller1 = h->caller1;
    d->freeCaller = freeCaller;
    d->headerBad = headerBad;
    memcpy(d->after, user + (headerBad ? 0 : h->size), sizeof(d->after));
}

void* __wrap_malloc(size_t size) {
    if (!OOT_PS3_HEAP_GUARD) return __real_malloc(size);
    if (size > 0x7FFFFFF0U) return NULL;
    return Guard(__real_malloc(size + sizeof(GuardHeader) + GUARD_TRAILER), size,
                 (uint64_t)(uintptr_t)__builtin_return_address(0), (uint64_t)(uintptr_t)__builtin_return_address(1));
}

void* __wrap_calloc(size_t count, size_t size) {
    size_t total;
    void* p;
    if (!OOT_PS3_HEAP_GUARD) return __real_calloc(count, size);
    if (size != 0 && count > (size_t)0x7FFFFFF0U / size) return NULL;
    total = count * size;
    p = Guard(__real_malloc(total + sizeof(GuardHeader) + GUARD_TRAILER), total,
              (uint64_t)(uintptr_t)__builtin_return_address(0), (uint64_t)(uintptr_t)__builtin_return_address(1));
    if (p != NULL) memset(p, 0, total);
    return p;
}

void __wrap_free(void* ptr) {
    unsigned char* user = (unsigned char*)ptr;
    GuardHeader* h;
    if (!OOT_PS3_HEAP_GUARD) {
        __real_free(ptr);
        return;
    }
    if (ptr == NULL) return;
    if (!IsGuarded(ptr)) {
        h = (GuardHeader*)(user - sizeof(GuardHeader));
        if (h->magic == GUARD_FREED) {
            /* double free of a guarded block: don't pass it on */
            Lock();
            RecordDamage(user, h, 1, (uint64_t)(uintptr_t)__builtin_return_address(0));
            Unlock();
            return;
        }
        __real_free(ptr); /* not ours (memalign, newlib-internal) */
        return;
    }
    h = (GuardHeader*)(user - sizeof(GuardHeader));
    Lock();
    if (memcmp(user + h->size, kTrailer, GUARD_TRAILER) != 0)
        RecordDamage(user, h, 0, (uint64_t)(uintptr_t)__builtin_return_address(0));
    TableRemove((uintptr_t)user);
    h->magic = GUARD_FREED;
    if (h->size <= QUARANTINE_MAX_BLOCK) {
        QuarantineEntry* q;
        memset(user, 0xDB, h->size);
        while (sQCount > 0 && (sQCount == QUARANTINE_ENTRIES || sQBytes + h->size > QUARANTINE_MAX_BYTES)) {
            QuarantineEvict();
        }
        q = &sQuarantine[sQHead];
        q->user = user;
        q->size = h->size;
        q->freeCaller = (uint64_t)(uintptr_t)__builtin_return_address(0);
        sQHead = (sQHead + 1) % QUARANTINE_ENTRIES;
        ++sQCount;
        sQBytes += h->size;
        Unlock();
        return;
    }
    Unlock();
    __real_free(h);
}

void* __wrap_realloc(void* ptr, size_t size) {
    unsigned char* user = (unsigned char*)ptr;
    void* fresh;
    size_t old;
    if (!OOT_PS3_HEAP_GUARD) return __real_realloc(ptr, size);
    if (ptr == NULL) {
        return Guard(__real_malloc(size + sizeof(GuardHeader) + GUARD_TRAILER), size,
                     (uint64_t)(uintptr_t)__builtin_return_address(0),
                     (uint64_t)(uintptr_t)__builtin_return_address(1));
    }
    if (!IsGuarded(ptr)) return __real_realloc(ptr, size);
    if (size == 0) {
        __wrap_free(ptr);
        return NULL;
    }
    old = ((GuardHeader*)(user - sizeof(GuardHeader)))->size;
    fresh = Guard(__real_malloc(size + sizeof(GuardHeader) + GUARD_TRAILER), size,
                  (uint64_t)(uintptr_t)__builtin_return_address(0), (uint64_t)(uintptr_t)__builtin_return_address(1));
    if (fresh == NULL) return NULL;
    memcpy(fresh, ptr, old < size ? old : size);
    __wrap_free(ptr);
    return fresh;
}

static void ReportDamage(const char* where, uintptr_t ptr, uint32_t size, uint64_t c0, uint64_t c1,
                         uint64_t freeCaller, const unsigned char* after, int headerBad) {
    char line[320];
    char ascii[17];
    unsigned int i;
    for (i = 0; i < 16; ++i) ascii[i] = (after[i] >= 0x20 && after[i] < 0x7F) ? (char)after[i] : '.';
    ascii[16] = '\0';
    snprintf(line, sizeof(line),
             "graph: Play heap-guard %s: block %p size=%u allocated from %llx <- %llx%s%llx: %s damaged; "
             "bytes %02x%02x%02x%02x%02x%02x%02x%02x %02x%02x%02x%02x%02x%02x%02x%02x '%s'",
             where, (void*)ptr, (unsigned)size, (unsigned long long)c0, (unsigned long long)c1,
             freeCaller ? ", freed from " : "", (unsigned long long)freeCaller,
             headerBad ? "header (or double free)" : "trailer", after[0], after[1], after[2], after[3], after[4],
             after[5], after[6], after[7], after[8], after[9], after[10], after[11], after[12], after[13], after[14],
             after[15], ascii);
    oot_ps3_early_trace(line);
}

/* Checks every live guarded block; reports up to `maxReports` damaged ones (plus
 * damage found at free). Safe to call from the watchdog thread while the game is
 * stuck: it only reads, and skips the lock if another thread holds it. */
void oot_ps3_heap_check(int maxReports, int fromWatchdog) {
    unsigned int i, reports = 0, live = 0;
    int locked = 0;
    if (!OOT_PS3_HEAP_GUARD) {
        if (fromWatchdog) {
            char line[128];
            snprintf(line, sizeof(line), "graph: Play heap used %u of 65536 KB, refused %u (heap guard off)",
                     oot_ps3_heap_used_kb(), oot_ps3_heap_refusals());
            oot_ps3_early_trace(line);
        }
        return;
    }
    if (!fromWatchdog) {
        Lock();
        locked = 1;
    } else if (!__sync_lock_test_and_set(&sLock, 1)) {
        locked = 1;
    }
    for (i = 0; i < sFreeDamageCount && sReported < 32; ++i, ++sReported) {
        const GuardDamage* d = &sFreeDamage[i];
        ReportDamage("at free", d->ptr, d->size, d->caller0, d->caller1, d->freeCaller, d->after, d->headerBad);
    }
    sFreeDamageCount = 0;
    {
        unsigned int k;
        /* Walking all quarantined blocks (up to 24 MB) only when the watchdog asks;
         * otherwise blocks are checked when they leave the quarantine. */
        for (k = 0; fromWatchdog && k < sQCount; ++k) {
            const QuarantineEntry* q = &sQuarantine[(sQHead + QUARANTINE_ENTRIES - sQCount + k) % QUARANTINE_ENTRIES];
            const int offset = QuarantineCheck(q);
            if (offset >= 0) {
                QuarantineRecord(q, offset);
                memset(q->user, 0xDB, q->size); /* report each write once */
            }
        }
        for (k = 0; k < sUafCount && sReported < 48; ++k, ++sReported) {
            const UafDamage* d = &sUaf[k];
            char line[384];
            snprintf(line, sizeof(line),
                     "graph: Play heap-guard USE AFTER FREE: block %p size=%u allocated from %llx <- %llx, freed from "
                     "%llx; written at offset %u: %02x%02x%02x%02x%02x%02x%02x%02x %02x%02x%02x%02x%02x%02x%02x%02x",
                     (void*)d->ptr, (unsigned)d->size, (unsigned long long)d->caller0,
                     (unsigned long long)d->caller1, (unsigned long long)d->freeCaller, (unsigned)d->offset,
                     d->bytes[0], d->bytes[1], d->bytes[2], d->bytes[3], d->bytes[4], d->bytes[5], d->bytes[6],
                     d->bytes[7], d->bytes[8], d->bytes[9], d->bytes[10], d->bytes[11], d->bytes[12], d->bytes[13],
                     d->bytes[14], d->bytes[15]);
            oot_ps3_early_trace(line);
        }
        sUafCount = 0;
    }
    for (i = 0; i < GUARD_TABLE_SIZE && (int)reports < maxReports; ++i) {
        const uintptr_t p = sTable[i];
        const unsigned char* user;
        const GuardHeader* h;
        int headerBad;
        if (p == 0 || p == GUARD_TOMBSTONE) continue;
        ++live;
        user = (const unsigned char*)p;
        h = (const GuardHeader*)(user - sizeof(GuardHeader));
        headerBad = !(h->magic == GUARD_MAGIC && h->check == CheckWord(p, h->size));
        if (headerBad || memcmp(user + h->size, kTrailer, GUARD_TRAILER) != 0) {
            unsigned char after[16];
            memcpy(after, headerBad ? user - sizeof(GuardHeader) : user + h->size, sizeof(after));
            ReportDamage(fromWatchdog ? "watchdog scan" : "scan", p, headerBad ? 0U : h->size,
                         headerBad ? 0ULL : h->caller0, headerBad ? 0ULL : h->caller1, 0, after, headerBad);
            sTable[i] = GUARD_TOMBSTONE; /* report each damaged block once */
            --sLive;
            ++reports;
            ++sReported;
        }
    }
    if (locked) Unlock();
    if (fromWatchdog) {
        char line[256];
        snprintf(line, sizeof(line),
                 "graph: Play heap-guard watchdog scan: %u live blocks, %u damaged, %u quarantined (%u KB)%s; "
                 "heap used %u of 65536 KB, refused %u",
                 sLive, reports, sQCount, sQBytes / 1024U, sTableFull ? " (table full: newest blocks untracked)" : "",
                 oot_ps3_heap_used_kb(), oot_ps3_heap_refusals());
        oot_ps3_early_trace(line);
    } else {
        /* Memory use about every 50 s (called every 64 updates), and whenever sbrk refused. */
        static unsigned int sCalls, sLastRefusals;
        if (++sCalls % 16 == 1 || oot_ps3_heap_refusals() != sLastRefusals) {
            char line[192];
            sLastRefusals = oot_ps3_heap_refusals();
            snprintf(line, sizeof(line),
                     "graph: Play heap-guard memory: heap used %u of 65536 KB, refused %u, live blocks %u, "
                     "quarantined %u (%u KB)",
                     oot_ps3_heap_used_kb(), sLastRefusals, sLive, sQCount, sQBytes / 1024U);
            oot_ps3_early_trace(line);
        }
    }
    (void)live;
}
