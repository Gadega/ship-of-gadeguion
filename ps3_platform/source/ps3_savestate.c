// SPDX-License-Identifier: GPL-3.0-or-later
/* Save states (game menu): a C port of Ship of Harkinian's soh/Enhancements/savestates.cpp.
 *
 * A state is the whole 4 MB system heap (game state, actors, Zelda arena), the audio heap
 * and audio engine state, gSaveContext, gGameInfo, the effect context, the static data
 * that SoH registers per file (*_SaveState, SHIP_SAVESTATE_DEFINE) and the transition
 * actor ids. Three slots, each in its own LV2 memory block (allocated on first use), so
 * they don't take room from the game's malloc heap.
 *
 * Slots are also written to USRDIR/savestates/slotN.bin, with the keys of every resource
 * block (ps3_reloc.h) the saved memory points into. Loading recreates those blocks and
 * moves the pointers to their addresses in this run, so a state survives a restart.
 * Loading from another area (or from the title / file select) first warps to the saved
 * entrance with the saved file, then restores once that area is running. A state only
 * loads in the EBOOT that made it (code and static data addresses must match).
 *
 * Requests are queued by the game menu and run by oot_ps3_savestate_tick() on the game
 * thread between updates, with the render thread idle (oot_ps3_render_sync) and the
 * audio engine paused (engine lock + SPU mix finished). Compiled with the game headers. */
#include "global.h"
#include "soh/Enhancements/savestate_serialize.h"
#include "soh/ActorDB.h"
#include "ps3_reloc.h"
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

void oot_ps3_early_trace(const char* message);
void* oot_ps3_big_alloc(unsigned int bytes);   /* ps3_early_entry.c: LV2 memory, 1 MB pages */
void oot_ps3_render_sync(void);                /* shipwright_bootstrap_ps3.cpp */
void oot_ps3_audio_quiesce_begin(void);        /* ps3_audio.c */
void oot_ps3_audio_quiesce_end(void);
void oot_ps3_early_trace_flush_usb(void);         /* ps3_early_entry.c: write the log out now */
int oot_ps3_debug_warp_for_state(int entrance, int age); /* ps3_debug_game.c */

#define STATE_DIR "/dev_hdd0/game/OOTP30048/USRDIR/savestates"
/* Slot files on the HDD (load after a restart) still crash: off. Save states live in RAM
 * only and are cleared by Reset game and by quitting. */
#ifndef PS3_SAVESTATE_DISK
#define PS3_SAVESTATE_DISK 0
#endif
#define STATE_MAGIC "OOTPSST1"
#define STATE_VERSION 6U
/* Same EBOOT only: code/static addresses are stored as-is. */
static const char kBuildId[64] = "Ship of Gadeguion " __DATE__ " " __TIME__;

extern EffectContext sEffectContext;

#define SAVESTATE_FN(Tag) void Tag##_SaveState(SaveStateCtx* ctx);
#define SAVESTATE_LIST(X)                                                                                         \
    X(Matrix) X(Lights) X(DoorWarp1) X(MapMark) X(Camera) X(OnePointCutscene) X(Environment) X(MapExp)          \
    X(AudioOcarina) X(MessagePAL) X(BgDdanKd) X(BgDodoago) X(BgHakaTrap) X(BgHidanRock) X(BgMenkuriEye)         \
    X(BgMoriHineri) X(BgPoEvent) X(BgRelayObjects) X(BgSpot18Basket) X(BossGanon) X(BossGanon2) X(BossMo)        \
    X(BossSst) X(BossTw) X(BossVa) X(Demo6k) X(DemoDu) X(DemoKekkai) X(EnBw) X(EnClearTag) X(EnFr) X(EnGoma)     \
    X(EnInsect) X(EnIshi) X(EnNiw) X(EnPoField) X(EnTakaraMan) X(EnXc) X(EnZf) X(EnZl3) X(ObjectKankyo)         \
    X(EnHeishi1) X(Player)
SAVESTATE_LIST(SAVESTATE_FN)
#define SAVESTATE_PTR(Tag) Tag##_SaveState,
static void (*const kStaticFns[])(SaveStateCtx*) = { SAVESTATE_LIST(SAVESTATE_PTR) };

typedef struct {
    u32 valid;
    void* gameState;                 /* load only into the same game state allocation */
    s16 sceneNum;
    unsigned char sysHeap[SYSTEM_HEAP_SIZE];
    unsigned char audioHeap[AUDIO_HEAP_SIZE];
    AudioContext audioContext;
    SeqScriptState seqScript[4];     /* pointers stored relative to gAudioHeap */
    ActiveSequence activeSeqs[4];
    ActiveSound activeSounds[7][MAX_CHANNELS_PER_BANK];
    u8 soundBankMuted[7];
    u8 d801333F0;
    u8 sfxSwapOff;
    u16 sfxSwapSource[10];
    u16 sfxSwapTarget[10];
    u8 sfxSwapMode[10];
    void (*d801755D0)(void);
    SaveContext saveContext;
    GameInfo gameInfo;
    EffectContext effectContext;
    u8 transitionActorCount;
    s16 transitionActorIds[256];
    size_t staticSize;
    unsigned char staticData[]; /* the *_SaveState blobs */
} SaveStateSlot;

enum { SLOT_COUNT = 3 };

/* A resource block the saved memory points into (key + address when last valid). */
typedef struct {
    u8 kind;
    u32 index;
    u32 extra;
    u64 hash;
    uintptr_t addr;
    size_t size;
    char* name;
} StateBlock;
typedef struct {
    StateBlock* items;
    u32 count, capacity;
} StateBlocks;

typedef struct {
    char magic[8];
    char build[64];
    u32 version;
    u32 slotBytes;
    u32 numBlocks;
    u32 unknownPointers;
    u32 numGlobals;
    u32 pad;
} StateFileHeader;
typedef struct {
    u8 kind;
    u8 pad;
    u16 nameLen;
    u32 index;
    u32 extra;
    u32 pad2;
    u64 hash;
    u64 addr;
    u64 size;
} StateFileBlock;

static SaveStateSlot* sSlots[SLOT_COUNT];
static StateBlocks sBlocks[SLOT_COUNT];
static signed char sFileExists[SLOT_COUNT] = { -1, -1, -1 }; /* -1 unknown */
static u32 sUnknown[SLOT_COUNT]; /* unregistered pointers found when the slot was saved */

/* All game-code globals (.data/.bss of the decomp objects, same addresses in every run
 * of this EBOOT). SoH's per-file static blobs only cover the ones SoH knew about; the
 * rest (counters, list indices, flags, pointers set by Play_Init) still match inside
 * one session but not after a restart. A save copies all of them, a load writes them
 * back. The range table is written into the ELF after linking from the linker map
 * (scripts/patch-data-range.py replaces the marker); thread/engine state (graph loop,
 * pad, audio manager, scheduler, render matrix stack, SPU mixer, Fast3D, PS3 platform)
 * is left out. */
#define GAME_DATA_MAX 8192
static volatile u32 gOotPs3GameData[4 + 2 * GAME_DATA_MAX] __attribute__((used, aligned(8))) = {
    0x4F4F5450, 0x53334741, 0x4D454441, 0x54412121 /* "OOTPS3GAMEDATA!!" */
};
static size_t sGameDataBytes; /* total size of the ranges */
static int sPendingSlot = -1;     /* load waiting for the warp to the saved area */
static unsigned int sPendingUpdates, sPendingWait;
static size_t sStaticSize;
static int sRequest = -1, sRequestSlot; /* -1 none, 0 save, 1 load */
static char sStatus[64];
static unsigned int sStatusSerial;

static void SetStatus(const char* text) {
    char line[96];
    snprintf(sStatus, sizeof(sStatus), "%s", text);
    ++sStatusSerial;
    snprintf(line, sizeof(line), "trace: save state: %s", text);
    oot_ps3_early_trace(line);
}

/* For the game menu: the last result and a counter that changes with each new one. */
const char* oot_ps3_savestate_status(unsigned int* serial) {
    if (serial != NULL) *serial = sStatusSerial;
    return sStatus;
}

static void SlotPath(int slot, char* out, size_t size, const char* suffix) {
    snprintf(out, size, STATE_DIR "/slot%d.bin%s", slot + 1, suffix);
}

/* Game menu (render thread): RAM copy or a file on the HDD. */
int oot_ps3_savestate_slot_used(int slot) {
    if (slot < 0 || slot >= SLOT_COUNT) return 0;
    if (sSlots[slot] != NULL && sSlots[slot]->valid) return 1;
    if (!PS3_SAVESTATE_DISK) return 0;
    if (sFileExists[slot] < 0) {
        char path[128];
        struct stat st;
        SlotPath(slot, path, sizeof(path), "");
        sFileExists[slot] = (stat(path, &st) == 0 && st.st_size > (off_t)sizeof(StateFileHeader)) ? 1 : 0;
    }
    return sFileExists[slot] == 1;
}

void oot_ps3_savestate_request(int load, int slot) {
    if (slot < 0 || slot >= SLOT_COUNT) return;
    sRequest = load ? 1 : 0;
    sRequestSlot = slot;
}

static size_t StaticSize(void) {
    SaveStateCtx ctx;
    size_t i;
    memset(&ctx, 0, sizeof(ctx));
    ctx.mode = SHIP_SAVESTATE_MEASURE;
    for (i = 0; i < ARRAY_COUNT(kStaticFns); i++) kStaticFns[i](&ctx);
    return ctx.offset;
}

static void RunStatics(SaveStateSlot* slot, SaveStateMode mode) {
    SaveStateCtx ctx;
    size_t i;
    memset(&ctx, 0, sizeof(ctx));
    ctx.mode = mode;
    ctx.buffer = slot->staticData;
    for (i = 0; i < ARRAY_COUNT(kStaticFns); i++) kStaticFns[i](&ctx);
}

static u8* RelToHeap(u8* p) {
    return (u8*)((uintptr_t)p - (uintptr_t)gAudioHeap);
}
static u8* HeapFromRel(u8* p) {
    return (u8*)((uintptr_t)p + (uintptr_t)gAudioHeap);
}

static size_t SlotBytes(void) {
    return sizeof(SaveStateSlot) + sStaticSize + sGameDataBytes;
}

static void BlocksClear(StateBlocks* b) {
    u32 i;
    for (i = 0; i < b->count; i++) free(b->items[i].name);
    b->count = 0;
}

static StateBlock* BlocksAdd(StateBlocks* b) {
    if (b->count == b->capacity) {
        const u32 cap = b->capacity ? b->capacity * 2 : 256;
        StateBlock* items = (StateBlock*)realloc(b->items, cap * sizeof(StateBlock));
        if (items == NULL) return NULL;
        b->items = items;
        b->capacity = cap;
    }
    memset(&b->items[b->count], 0, sizeof(StateBlock));
    return &b->items[b->count++];
}

static char* DupString(const char* text) {
    const size_t n = text != NULL ? strlen(text) : 0;
    char* copy = (char*)malloc(n + 1);
    if (copy != NULL) {
        if (n) memcpy(copy, text, n);
        copy[n] = '\0';
    }
    return copy;
}

/* The two LV2 regions the game's own heaps live in, as relocatable blocks. */
static void RegisterHeaps(void) {
    oot_ps3_reloc_register(PS3_RELOC_HEAP, "system", 0, gSystemHeap, SYSTEM_HEAP_SIZE);
    oot_ps3_reloc_register(PS3_RELOC_HEAP, "audio", 0, gAudioHeap, AUDIO_HEAP_SIZE);
}

/* Candidate pointer windows: 4-byte steps (the static blobs are packed), a 64-bit value
 * whose upper half is 0 and whose lower half is inside malloc's block or the game
 * heaps' block. */
typedef struct {
    uintptr_t lo[2], hi[2];
} PtrFilter;

static void MakeFilter(PtrFilter* f) {
    uintptr_t base;
    size_t size;
    oot_ps3_heap_range(&base, &size);
    f->lo[0] = base;
    f->hi[0] = base + size;
    f->lo[1] = (uintptr_t)gAudioHeap < (uintptr_t)gSystemHeap ? (uintptr_t)gAudioHeap : (uintptr_t)gSystemHeap;
    f->hi[1] = f->lo[1] + AUDIO_HEAP_SIZE + SYSTEM_HEAP_SIZE;
}

static int InFilter(const PtrFilter* f, uintptr_t v) {
    return (v >= f->lo[0] && v < f->hi[0]) || (v >= f->lo[1] && v < f->hi[1]);
}

static uintptr_t ReadWindow(const unsigned char* p) {
    u32 hi, lo;
    memcpy(&hi, p, 4);
    memcpy(&lo, p + 4, 4);
    return hi == 0 ? (uintptr_t)lo : 0;
}

/* Where in the game a slot offset lives (for the unregistered-pointer report). */
static void DescribeOffset(PlayState* play, size_t off, char* out, size_t size) {
    const size_t sys = offsetof(SaveStateSlot, sysHeap), aud = offsetof(SaveStateSlot, audioHeap);
    if (off >= sys && off < sys + SYSTEM_HEAP_SIZE) {
        const uintptr_t live = (uintptr_t)gSystemHeap + (off - sys);
        s32 category;
        if (play != NULL && live >= (uintptr_t)play && live < (uintptr_t)play + sizeof(PlayState)) {
            snprintf(out, size, "PlayState+0x%lx", (unsigned long)(live - (uintptr_t)play));
            return;
        }
        for (category = 0; play != NULL && category < ACTORCAT_MAX; category++) {
            Actor* actor = play->actorCtx.actorLists[category].head;
            s32 guard = 0;
            for (; actor != NULL && guard < 1000; actor = actor->next, guard++) {
                const uintptr_t a = (uintptr_t)actor;
                if (live >= a && live < a + ActorDB_Retrieve(actor->id)->instanceSize) {
                    snprintf(out, size, "actor %04X (%s)+0x%lx", (unsigned)(u16)actor->id,
                             ActorDB_Retrieve(actor->id)->name, (unsigned long)(live - a));
                    return;
                }
            }
        }
        snprintf(out, size, "system heap+0x%lx", (unsigned long)(off - sys));
    } else if (off >= aud && off < aud + AUDIO_HEAP_SIZE) {
        snprintf(out, size, "audio heap+0x%lx", (unsigned long)(off - aud));
    } else if (off >= offsetof(SaveStateSlot, audioContext) && off < offsetof(SaveStateSlot, audioContext) + sizeof(AudioContext)) {
        snprintf(out, size, "gAudioContext+0x%lx", (unsigned long)(off - offsetof(SaveStateSlot, audioContext)));
    } else if (off >= offsetof(SaveStateSlot, staticData)) {
        snprintf(out, size, "static blobs+0x%lx", (unsigned long)(off - offsetof(SaveStateSlot, staticData)));
    } else {
        snprintf(out, size, "slot header+0x%lx", (unsigned long)off);
    }
}

/* After a save: collect the blocks the slot points into (and count the pointers that
 * point into malloc memory nobody registered: those can't be moved on load). */
static u32 CollectBlocks(PlayState* play, SaveStateSlot* s, StateBlocks* blocks) {
    const unsigned char* base = (const unsigned char*)s;
    const size_t bytes = SlotBytes();
    PtrFilter filter;
    size_t off;
    u32 unknown = 0;
    char line[200];
    BlocksClear(blocks);
    RegisterHeaps();
    MakeFilter(&filter);
    oot_ps3_reloc_begin_query();
    for (off = 0; off + 8 <= bytes; off += 4) {
        const uintptr_t v = ReadWindow(base + off);
        Ps3RelocInfo info;
        u32 k;
        if (v == 0 || !InFilter(&filter, v)) continue;
        if (!oot_ps3_reloc_find(v, &info)) {
            if (unknown < 40) {
                char where[96];
                DescribeOffset(play, off, where, sizeof(where));
                snprintf(line, sizeof(line), "trace: save state: unregistered pointer %08lx in %s", (unsigned long)v,
                         where);
                oot_ps3_early_trace(line);
            }
            ++unknown;
            continue;
        }
        off += 4; /* the low half can't start another pointer */
        for (k = blocks->count; k > 0; k--) {
            if (blocks->items[k - 1].addr == info.addr && blocks->items[k - 1].size == info.size) break;
        }
        if (k == 0) {
            /* not the most recent ones: full search (blocks are few, a few hundred) */
            for (k = 0; k < blocks->count; k++)
                if (blocks->items[k].addr == info.addr && blocks->items[k].size == info.size) break;
            if (k == blocks->count) {
                StateBlock* b = BlocksAdd(blocks);
                if (b == NULL) continue;
                b->kind = (u8)info.kind;
                b->index = info.index;
                b->extra = info.extra;
                b->hash = info.hash;
                b->addr = info.addr;
                b->size = info.size;
                b->name = DupString(info.name);
            }
        }
    }
    oot_ps3_reloc_end_query();
    snprintf(line, sizeof(line), "trace: save state: %u resource blocks referenced, %u unregistered pointers",
             (unsigned)blocks->count, (unsigned)unknown);
    oot_ps3_early_trace(line);
    return unknown;
}

static u32 GameDataRanges(void) {
    const u32 n = gOotPs3GameData[0];
    return (gOotPs3GameData[1] == 0 && n <= GAME_DATA_MAX) ? n : 0;
}

static size_t GameDataBytes(void) {
    u32 r, ranges = GameDataRanges();
    size_t total = 0;
    for (r = 0; r < ranges; r++) total += gOotPs3GameData[5 + 2 * r];
    return total;
}

/* Save: copy every game global into the slot (after the slot struct). */
static void CollectGlobals(SaveStateSlot* s) {
    unsigned char* out = (unsigned char*)s + sizeof(SaveStateSlot) + sStaticSize;
    u32 r, ranges = GameDataRanges();
    char line[128];
    if (ranges == 0) {
        oot_ps3_early_trace("trace: save state: game data table missing (build not patched)");
        return;
    }
    for (r = 0; r < ranges; r++) {
        const u32 size = gOotPs3GameData[5 + 2 * r];
        memcpy(out, (const void*)(uintptr_t)gOotPs3GameData[4 + 2 * r], size);
        out += size;
    }
    snprintf(line, sizeof(line), "trace: save state: %u game globals ranges, %lu bytes", (unsigned)ranges,
             (unsigned long)sGameDataBytes);
    oot_ps3_early_trace(line);
}

static int CompareBlockAddr(const void* a, const void* b);

/* Is v inside one of the slot's (relocated) resource blocks? */
static int InBlocks(const StateBlocks* blocks, uintptr_t v) {
    u32 a = 0, c = blocks->count, k, steps;
    while (a < c) {
        const u32 mid = (a + c) / 2;
        if (blocks->items[mid].addr <= v) a = mid + 1; else c = mid;
    }
    for (k = a, steps = 0; k > 0 && steps < 32; k--, steps++)
        if (v - blocks->items[k - 1].addr < blocks->items[k - 1].size) return 1;
    return 0;
}

/* Every moving-collision actor: which resource block its collision header is in and
 * whether the header looks converted (arrays are real addresses). Logged at save and
 * after a load, to compare. */
static void ReportBgActors(PlayState* play, const char* when) {
    s32 i;
    char line[240];
    oot_ps3_reloc_begin_query();
    for (i = 0; i < BG_ACTOR_MAX; i++) {
        BgActor* bg = &play->colCtx.dyna.bgActors[i];
        CollisionHeader* h;
        Ps3RelocInfo info;
        const char* blockName = "(not a tracked block)";
        char blockText[120];
        if (!(play->colCtx.dyna.bgActorFlags[i] & 1)) continue;
        h = bg->colHeader;
        blockText[0] = '\0';
        if (h != NULL && oot_ps3_reloc_find((uintptr_t)h, &info)) {
            snprintf(blockText, sizeof(blockText), "kind %d #%u +0x%lx %s %016llx", info.kind, info.index,
                     (unsigned long)((uintptr_t)h - info.addr), info.name, info.hash);
            blockName = blockText;
        }
        snprintf(line, sizeof(line), "trace: save state %s: bg %d actor %04X col %p in %s; vtx %p poly %p surf %p",
                 when, (int)i, bg->actor != NULL ? (unsigned)(u16)bg->actor->id : 0xFFFFU, (void*)h, blockName,
                 h != NULL ? (void*)h->vtxList : NULL, h != NULL ? (void*)h->polyList : NULL,
                 h != NULL ? (void*)h->surfaceTypeList : NULL);
        oot_ps3_early_trace(line);
    }
    oot_ps3_reloc_end_query();
}

/* After a load: list the saved values that point into malloc memory no known block
 * covers (they still hold the saving run's addresses), with where they live. */
static void ReportUnknownPointers(PlayState* play, SaveStateSlot* s, const StateBlocks* blocks) {
    const unsigned char* base = (const unsigned char*)s;
    const size_t bytes = sizeof(SaveStateSlot) + sStaticSize; /* game globals handled separately */
    uintptr_t heapBase;
    size_t heapSize, off;
    u32 n = 0;
    char line[200], where[96];
    oot_ps3_heap_range(&heapBase, &heapSize);
    for (off = 0; off + 8 <= bytes; off += 4) {
        const uintptr_t v = ReadWindow(base + off);
        if (v == 0 || v < heapBase || v >= heapBase + heapSize || InBlocks(blocks, v)) continue;
        if (n < 40) {
            DescribeOffset(play, off, where, sizeof(where));
            snprintf(line, sizeof(line), "trace: save state: stale malloc pointer %08lx in %s", (unsigned long)v, where);
            oot_ps3_early_trace(line);
        }
        n++;
        off += 4;
    }
    snprintf(line, sizeof(line), "trace: save state: %u stale malloc pointers after the load", (unsigned)n);
    oot_ps3_early_trace(line);
}

/* Load (after the heaps): write them back; log the first words that differed. A saved
 * pointer into malloc memory that isn't a known resource block (something the game
 * malloc'd itself, e.g. the dungeon map textures) means nothing in this run: the
 * current value is kept for those. */
static void RestoreGlobals(SaveStateSlot* s, StateBlocks* blocks) {
    const unsigned char* in = (const unsigned char*)s + sizeof(SaveStateSlot) + sStaticSize;
    u32 r, ranges = GameDataRanges(), changed = 0, kept = 0;
    uintptr_t heapBase;
    size_t heapSize;
    char line[128];
    oot_ps3_heap_range(&heapBase, &heapSize);
    if (blocks->count) qsort(blocks->items, blocks->count, sizeof(StateBlock), CompareBlockAddr);
    for (r = 0; r < ranges; r++) {
        unsigned char* dst = (unsigned char*)(uintptr_t)gOotPs3GameData[4 + 2 * r];
        const u32 size = gOotPs3GameData[5 + 2 * r];
        u32 k;
        for (k = 0; k + 4 <= size; k += 4) {
            if (memcmp(dst + k, in + k, 4) != 0) {
                if (changed < 48) {
                    u32 was, now;
                    memcpy(&was, dst + k, 4);
                    memcpy(&now, in + k, 4);
                    snprintf(line, sizeof(line), "trace: save state: global %08lx was %08x, restored %08x",
                             (unsigned long)((uintptr_t)dst + k), (unsigned)was, (unsigned)now);
                    oot_ps3_early_trace(line);
                }
                changed++;
            }
        }
        {
            /* Copy the range, skipping (keeping) the 8-byte words that hold such pointers. */
            const uintptr_t base = (uintptr_t)dst;
            u32 copied = 0, scan = (u32)(((base + 7) & ~(uintptr_t)7) - base);
            for (; scan + 8 <= size; scan += 8) {
                const uintptr_t v = ReadWindow(in + scan);
                if (v == 0 || v < heapBase || v >= heapBase + heapSize || InBlocks(blocks, v)) continue;
                memcpy(dst + copied, in + copied, scan - copied);
                copied = scan + 8;
                kept++;
            }
            memcpy(dst + copied, in + copied, size - copied);
        }
        in += size;
    }
    snprintf(line, sizeof(line), "trace: save state: game globals restored (%u words differed, %u malloc pointers kept)",
             (unsigned)changed, (unsigned)kept);
    oot_ps3_early_trace(line);
}

static int CompareBlockAddr(const void* a, const void* b) {
    const uintptr_t x = ((const StateBlock*)a)->addr, y = ((const StateBlock*)b)->addr;
    return x < y ? -1 : (x > y ? 1 : 0);
}

/* Before a load: recreate every block in this run and move the slot's pointers. */
static int RelocateSlot(SaveStateSlot* s, StateBlocks* blocks, char* error, size_t errorSize) {
    uintptr_t* newAddr;
    unsigned char* base = (unsigned char*)s;
    const size_t bytes = SlotBytes();
    uintptr_t lo = UINTPTR_MAX, hi = 0;
    u32 i, moved = 0, fixed = 0;
    size_t off;
    char line[200];
    if (blocks->count == 0) return 1;
    RegisterHeaps();
    qsort(blocks->items, blocks->count, sizeof(StateBlock), CompareBlockAddr);
    newAddr = (uintptr_t*)malloc(blocks->count * sizeof(uintptr_t));
    if (newAddr == NULL) {
        snprintf(error, errorSize, "Not enough memory to load");
        return 0;
    }
    for (i = 0; i < blocks->count; i++) {
        StateBlock* b = &blocks->items[i];
        uintptr_t addr = 0;
        size_t size = 0;
        if (!oot_ps3_reloc_resolve(b->kind, b->name, b->hash, b->index, b->extra, &addr, &size) || size != b->size) {
            snprintf(line, sizeof(line), "trace: save state: block %s %016llx #%u (kind %d) %s (size %lu, saved %lu)",
                     b->name, (unsigned long long)b->hash, (unsigned)b->index, (int)b->kind,
                     addr ? "changed" : "missing", (unsigned long)size, (unsigned long)b->size);
            oot_ps3_early_trace(line);
            snprintf(error, errorSize, "Game data differs from the save state");
            free(newAddr);
            return 0;
        }
        newAddr[i] = addr;
        if (addr != b->addr) ++moved;
        if (b->addr < lo) lo = b->addr;
        if (b->addr + b->size > hi) hi = b->addr + b->size;
    }
    if (moved != 0) {
        for (off = 0; off + 8 <= bytes; off += 4) {
            const uintptr_t v = ReadWindow(base + off);
            u32 a = 0, c = blocks->count, best = UINT32_MAX, k, steps;
            if (v < lo || v >= hi) continue;
            while (a < c) { /* last block starting at or below v */
                const u32 mid = (a + c) / 2;
                if (blocks->items[mid].addr <= v) a = mid + 1; else c = mid;
            }
            for (k = a, steps = 0; k > 0 && steps < 32; k--, steps++) {
                const StateBlock* b = &blocks->items[k - 1];
                if (v - b->addr < b->size && (best == UINT32_MAX || b->size < blocks->items[best].size)) best = k - 1;
            }
            if (best == UINT32_MAX) continue;
            {
                const u32 nv = (u32)(v - blocks->items[best].addr + newAddr[best]);
                const u32 zero = 0;
                memcpy(base + off, &zero, 4);
                memcpy(base + off + 4, &nv, 4);
                ++fixed;
                off += 4;
            }
        }
    }
    for (i = 0; i < blocks->count; i++) blocks->items[i].addr = newAddr[i];
    free(newAddr);
    snprintf(line, sizeof(line), "trace: save state: %u blocks, %u moved, %u pointers updated", (unsigned)blocks->count,
             (unsigned)moved, (unsigned)fixed);
    oot_ps3_early_trace(line);
    return 1;
}

static int WriteSlotFile(int slotIndex) {
    const SaveStateSlot* s = sSlots[slotIndex];
    const StateBlocks* blocks = &sBlocks[slotIndex];
    char path[128], tmp[128], line[160];
    StateFileHeader h;
    FILE* f;
    u32 i;
    int ok = 1;
    (void)mkdir(STATE_DIR, 0777);
    SlotPath(slotIndex, path, sizeof(path), "");
    SlotPath(slotIndex, tmp, sizeof(tmp), ".tmp");
    f = fopen(tmp, "wb");
    if (f == NULL) return 0;
    memset(&h, 0, sizeof(h));
    memcpy(h.magic, STATE_MAGIC, 8);
    memcpy(h.build, kBuildId, sizeof(h.build));
    h.version = STATE_VERSION;
    h.slotBytes = (u32)SlotBytes();
    h.numBlocks = blocks->count;
    h.unknownPointers = sUnknown[slotIndex];
    h.numGlobals = 0;
    ok &= fwrite(&h, sizeof(h), 1, f) == 1;
    for (i = 0; i < blocks->count && ok; i++) {
        const StateBlock* b = &blocks->items[i];
        StateFileBlock fb;
        memset(&fb, 0, sizeof(fb));
        fb.kind = b->kind;
        fb.nameLen = (u16)strlen(b->name);
        fb.index = b->index;
        fb.extra = b->extra;
        fb.hash = b->hash;
        fb.addr = (u64)b->addr;
        fb.size = (u64)b->size;
        ok &= fwrite(&fb, sizeof(fb), 1, f) == 1;
        ok &= fwrite(b->name, 1, fb.nameLen, f) == fb.nameLen;
    }
    ok &= fwrite(s, 1, SlotBytes(), f) == SlotBytes();
    ok &= fclose(f) == 0;
    if (ok) {
        (void)unlink(path);
        ok = rename(tmp, path) == 0;
    } else {
        (void)unlink(tmp);
    }
    snprintf(line, sizeof(line), "trace: save state: %s %s", path, ok ? "written" : "WRITE FAILED");
    oot_ps3_early_trace(line);
    if (ok) sFileExists[slotIndex] = 1;
    return ok;
}

/* 1 ok, 0 no file, -1 unusable (message in error). */
static int ReadSlotFile(int slotIndex, char* error, size_t errorSize) {
    SaveStateSlot* s = sSlots[slotIndex];
    StateBlocks* blocks = &sBlocks[slotIndex];
    char path[128], line[200];
    StateFileHeader h;
    FILE* f;
    u32 i;
    int ok = 1;
    SlotPath(slotIndex, path, sizeof(path), "");
    f = fopen(path, "rb");
    if (f == NULL) return 0;
    if (fread(&h, sizeof(h), 1, f) != 1 || memcmp(h.magic, STATE_MAGIC, 8) != 0 || h.version != STATE_VERSION) {
        fclose(f);
        snprintf(error, errorSize, "Slot %d: not a save state file", slotIndex + 1);
        return -1;
    }
    if (memcmp(h.build, kBuildId, sizeof(h.build)) != 0 || h.slotBytes != SlotBytes()) {
        fclose(f);
        snprintf(line, sizeof(line), "trace: save state: %s made by '%.64s'", path, h.build);
        oot_ps3_early_trace(line);
        snprintf(error, errorSize, "Slot %d was made by another version", slotIndex + 1);
        return -1;
    }
    BlocksClear(blocks);
    for (i = 0; i < h.numBlocks && ok; i++) {
        StateFileBlock fb;
        StateBlock* b;
        ok &= fread(&fb, sizeof(fb), 1, f) == 1;
        if (!ok) break;
        b = BlocksAdd(blocks);
        if (b == NULL) {
            ok = 0;
            break;
        }
        b->kind = fb.kind;
        b->index = fb.index;
        b->extra = fb.extra;
        b->hash = fb.hash;
        b->addr = (uintptr_t)fb.addr;
        b->size = (size_t)fb.size;
        b->name = (char*)malloc((size_t)fb.nameLen + 1);
        if (b->name == NULL) {
            ok = 0;
            break;
        }
        ok &= fread(b->name, 1, fb.nameLen, f) == fb.nameLen;
        b->name[fb.nameLen] = '\0';
    }
    if (ok) ok = fread(s, 1, SlotBytes(), f) == SlotBytes();
    fclose(f);
    if (!ok) {
        s->valid = 0;
        BlocksClear(blocks);
        snprintf(error, errorSize, "Slot %d: file is damaged", slotIndex + 1);
        return -1;
    }
    sUnknown[slotIndex] = h.unknownPointers;
    snprintf(line, sizeof(line), "trace: save state: %s read (%u blocks, %u unregistered pointers, scene %d)", path,
             (unsigned)h.numBlocks, (unsigned)h.unknownPointers, (int)s->sceneNum);
    oot_ps3_early_trace(line);
    return s->valid ? 1 : -1;
}

static SaveStateSlot* EnsureSlotMemory(int slotIndex) {
    if (sStaticSize == 0) {
        sStaticSize = StaticSize();
        sGameDataBytes = GameDataBytes();
    }
    if (sSlots[slotIndex] == NULL) {
        sSlots[slotIndex] = (SaveStateSlot*)oot_ps3_big_alloc((unsigned int)SlotBytes());
        if (sSlots[slotIndex] == NULL) return NULL;
        sSlots[slotIndex]->valid = 0;
        sSlots[slotIndex]->staticSize = sStaticSize;
    }
    return sSlots[slotIndex];
}

static void SaveNow(PlayState* play, SaveStateSlot* s) {
    s32 i, k;
    memcpy(s->sysHeap, gSystemHeap, SYSTEM_HEAP_SIZE);
    memcpy(s->audioHeap, gAudioHeap, AUDIO_HEAP_SIZE);
    memcpy(&s->audioContext, &gAudioContext, sizeof(AudioContext));
    /* malloc'd once at boot (SoH): this run's tables stay in place on load */
    s->audioContext.fontLoadStatus = NULL;
    s->audioContext.seqLoadStatus = NULL;
    memcpy(s->activeSeqs, gActiveSeqs, sizeof(s->activeSeqs));
    for (i = 0; i < 4; i++) {
        s->seqScript[i] = gAudioContext.seqPlayers[i].scriptState;
        s->seqScript[i].pc = RelToHeap(gAudioContext.seqPlayers[i].scriptState.pc);
        for (k = 0; k < 4; k++) s->seqScript[i].stack[k] = RelToHeap(gAudioContext.seqPlayers[i].scriptState.stack[k]);
    }
    memcpy(s->activeSounds, gActiveSounds, sizeof(s->activeSounds));
    memcpy(s->soundBankMuted, gSoundBankMuted, sizeof(s->soundBankMuted));
    s->d801333F0 = D_801333F0;
    s->sfxSwapOff = gAudioSfxSwapOff;
    memcpy(s->sfxSwapSource, gAudioSfxSwapSource, sizeof(s->sfxSwapSource));
    memcpy(s->sfxSwapTarget, gAudioSfxSwapTarget, sizeof(s->sfxSwapTarget));
    memcpy(s->sfxSwapMode, gAudioSfxSwapMode, sizeof(s->sfxSwapMode));
    s->d801755D0 = D_801755D0;
    memcpy(&s->saveContext, &gSaveContext, sizeof(gSaveContext));
    memcpy(&s->gameInfo, gGameInfo, sizeof(*gGameInfo));
    memcpy(&s->effectContext, &sEffectContext, sizeof(sEffectContext));
    RunStatics(s, SHIP_SAVESTATE_SAVE);
    s->transitionActorCount = play->transiActorCtx.numActors;
    for (i = 0; i < s->transitionActorCount; i++) s->transitionActorIds[i] = play->transiActorCtx.list[i].id;
    s->gameState = play;
    s->sceneNum = play->sceneNum;
    s->valid = 1;
}

static void LoadNow(PlayState* play, SaveStateSlot* s) {
    s32 i, k;
    u32 n;
    memcpy(gSystemHeap, s->sysHeap, SYSTEM_HEAP_SIZE);
    memcpy(gAudioHeap, s->audioHeap, AUDIO_HEAP_SIZE);
    {
        u8* const fontLoadStatus = gAudioContext.fontLoadStatus;
        u8* const seqLoadStatus = gAudioContext.seqLoadStatus;
        memcpy(&gAudioContext, &s->audioContext, sizeof(AudioContext));
        gAudioContext.fontLoadStatus = fontLoadStatus;
        gAudioContext.seqLoadStatus = seqLoadStatus;
    }
    memcpy(gActiveSeqs, s->activeSeqs, sizeof(s->activeSeqs));
    for (i = 0; i < 4; i++) {
        gAudioContext.seqPlayers[i].scriptState = s->seqScript[i];
        gAudioContext.seqPlayers[i].scriptState.pc = HeapFromRel(s->seqScript[i].pc);
        for (k = 0; k < 4; k++) gAudioContext.seqPlayers[i].scriptState.stack[k] = HeapFromRel(s->seqScript[i].stack[k]);
    }
    memcpy(&gSaveContext, &s->saveContext, sizeof(gSaveContext));
    memcpy(gGameInfo, &s->gameInfo, sizeof(*gGameInfo));
    memcpy(&sEffectContext, &s->effectContext, sizeof(sEffectContext));
    memcpy(gActiveSounds, s->activeSounds, sizeof(s->activeSounds));
    memcpy(gSoundBankMuted, s->soundBankMuted, sizeof(s->soundBankMuted));
    D_801333F0 = s->d801333F0;
    gAudioSfxSwapOff = s->sfxSwapOff;
    memcpy(gAudioSfxSwapSource, s->sfxSwapSource, sizeof(s->sfxSwapSource));
    memcpy(gAudioSfxSwapTarget, s->sfxSwapTarget, sizeof(s->sfxSwapTarget));
    memcpy(gAudioSfxSwapMode, s->sfxSwapMode, sizeof(s->sfxSwapMode));
    D_801755D0 = s->d801755D0;
    RunStatics(s, SHIP_SAVESTATE_LOAD);
    n = s->transitionActorCount < play->transiActorCtx.numActors ? s->transitionActorCount
                                                                   : play->transiActorCtx.numActors;
    for (i = 0; i < (s32)n; i++) play->transiActorCtx.list[i].id = s->transitionActorIds[i];
}

/* The game is quiet enough to restore into: gameplay, no transition. */
static int PlayIsSettled(PlayState* play) {
    return play != NULL && gSaveContext.gameMode == GAMEMODE_NORMAL && play->transitionTrigger == TRANS_TRIGGER_OFF &&
           play->transitionMode == TRANS_MODE_OFF;
}

/* Restore slot into the running Play state (same scene). */
static void RestoreInto(PlayState* play, int slotIndex) {
    SaveStateSlot* s = sSlots[slotIndex];
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    char text[64], error[64];
    error[0] = '\0';
    if (!RelocateSlot(s, &sBlocks[slotIndex], error, sizeof(error))) {
        snprintf(text, sizeof(text), "Slot %d: %s", slotIndex + 1, error);
        SetStatus(text);
        return;
    }
    if (s->gameState != (void*)play) {
        char line[160];
        snprintf(line, sizeof(line), "trace: save state: saved game state %p, running %p", s->gameState, (void*)play);
        oot_ps3_early_trace(line);
        snprintf(text, sizeof(text), "Slot %d can't be loaded here (re-enter the area)", slotIndex + 1);
        SetStatus(text);
        return;
    }
    oot_ps3_render_sync(); /* the render thread reads the heap while it draws */
    oot_ps3_audio_quiesce_begin();
    LoadNow(play, s);
    RestoreGlobals(s, &sBlocks[slotIndex]);
    play->state.gfxCtx = gfxCtx; /* lives outside the saved memory */
    ReportUnknownPointers(play, s, &sBlocks[slotIndex]);
    ReportBgActors(play, "after load");
    oot_ps3_early_trace_flush_usb();
    oot_ps3_audio_quiesce_end();
    snprintf(text, sizeof(text), "Loaded slot %d", slotIndex + 1);
    SetStatus(text);
}

/* ps3_debug_game.c, when it carries out the warp for a pending load: the saved file
 * (flags, age, time of day) decides which version of the area gets built. */
void oot_ps3_savestate_prepare_warp(void) {
    if (sPendingSlot < 0 || sSlots[sPendingSlot] == NULL) return;
    memcpy(&gSaveContext, &sSlots[sPendingSlot]->saveContext, sizeof(gSaveContext));
    gSaveContext.gameMode = GAMEMODE_NORMAL;
}

/* Game thread, between updates (graph.c via oot_ps3_debug_game_tick). */
static void ClearIfRequested(void);
void oot_ps3_savestate_tick(PlayState* play) {
    ClearIfRequested();
    const int request = sRequest, slotIndex = sRequestSlot;
    char text[64], error[64];
    SaveStateSlot* s;

    if (sPendingSlot >= 0) {
        s = sSlots[sPendingSlot];
        if (++sPendingWait > 1200) { /* ~60 s */
            snprintf(text, sizeof(text), "Slot %d: couldn't reach the saved area", sPendingSlot + 1);
            SetStatus(text);
            sPendingSlot = -1;
        } else if (PlayIsSettled(play) && play->sceneNum == s->sceneNum) {
            if (++sPendingUpdates >= 10) {
                const int slot = sPendingSlot;
                sPendingSlot = -1;
                RestoreInto(play, slot);
            }
        } else {
            sPendingUpdates = 0;
        }
    }

    if (request < 0) return;
    sRequest = -1;
    if (EnsureSlotMemory(slotIndex) == NULL) {
        SetStatus("Not enough memory for a save state");
        return;
    }
    s = sSlots[slotIndex];

    if (request == 0) {
        if (!PlayIsSettled(play)) {
            SetStatus("Save states only work during gameplay");
            return;
        }
        oot_ps3_render_sync();
        oot_ps3_audio_quiesce_begin();
        SaveNow(play, s);
        CollectGlobals(s);
        ReportBgActors(play, "at save");
        oot_ps3_audio_quiesce_end();
        sUnknown[slotIndex] = CollectBlocks(play, s, &sBlocks[slotIndex]);
        if (!PS3_SAVESTATE_DISK) {
            snprintf(text, sizeof(text), "Saved to slot %d", slotIndex + 1);
        } else if (WriteSlotFile(slotIndex)) {
            snprintf(text, sizeof(text), "Saved to slot %d", slotIndex + 1);
        } else {
            snprintf(text, sizeof(text), "Saved to slot %d (HDD write failed)", slotIndex + 1);
        }
        SetStatus(text);
        return;
    }

    /* Load: this session's copy, else the file. */
    error[0] = '\0';
    if (!s->valid) {
        const int r = PS3_SAVESTATE_DISK ? ReadSlotFile(slotIndex, error, sizeof(error)) : 0;
        if (r <= 0) {
            if (r == 0) snprintf(error, sizeof(error), "Slot %d is empty", slotIndex + 1);
            SetStatus(error);
            return;
        }
    }
    if (PlayIsSettled(play) && play->sceneNum == s->sceneNum) {
        RestoreInto(play, slotIndex);
        return;
    }
    /* Somewhere else (another area, title screen, file select): go to the saved area. */
    if (!oot_ps3_debug_warp_for_state(s->saveContext.entranceIndex, s->saveContext.linkAge)) {
        snprintf(text, sizeof(text), "Slot %d can't be loaded right now", slotIndex + 1);
        SetStatus(text);
        return;
    }
    sPendingSlot = slotIndex;
    sPendingUpdates = 0;
    sPendingWait = 0;
    snprintf(text, sizeof(text), "Loading slot %d...", slotIndex + 1);
    SetStatus(text);
}

/* Game menu "Reset game" (render thread): save states belong to the session that made
 * them. The game thread clears them at its next tick (it is the one using the slots). */
static volatile int sClearRequested;
void oot_ps3_savestate_clear_all(void) { sClearRequested = 1; }
static void ClearIfRequested(void) {
    int i;
    if (!sClearRequested) return;
    sClearRequested = 0;
    for (i = 0; i < SLOT_COUNT; ++i)
        if (sSlots[i] != NULL) sSlots[i]->valid = 0;
    sPendingSlot = -1;
    sRequest = -1;
    oot_ps3_early_trace("trace: save states cleared (game reset)");
}
