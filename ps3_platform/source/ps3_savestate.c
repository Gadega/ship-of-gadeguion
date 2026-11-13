// SPDX-License-Identifier: GPL-3.0-or-later
/* Save states (game menu): a C port of Ship of Harkinian's soh/Enhancements/savestates.cpp.
 *
 * A state is the whole 4 MB system heap (game state, actors, Zelda arena), the audio heap
 * and audio engine state, gSaveContext, gGameInfo, the effect context, the static data
 * that SoH registers per file (*_SaveState, SHIP_SAVESTATE_DEFINE) and the transition
 * actor ids. Three slots, each in its own LV2 memory block (allocated on first use), so
 * they don't take room from the game's malloc heap. Not kept across restarts.
 *
 * Requests are queued by the game menu and run by oot_ps3_savestate_tick() on the game
 * thread between updates, with the render thread idle (oot_ps3_render_sync) and the
 * audio engine paused (engine lock + SPU mix finished). Compiled with the game headers. */
#include "global.h"
#include "soh/Enhancements/savestate_serialize.h"

void oot_ps3_early_trace(const char* message);
void* oot_ps3_big_alloc(unsigned int bytes);   /* ps3_early_entry.c: LV2 memory, 1 MB pages */
void oot_ps3_render_sync(void);                /* shipwright_bootstrap_ps3.cpp */
void oot_ps3_audio_quiesce_begin(void);        /* ps3_audio.c */
void oot_ps3_audio_quiesce_end(void);

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
static SaveStateSlot* sSlots[SLOT_COUNT];
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

int oot_ps3_savestate_slot_used(int slot) {
    return slot >= 0 && slot < SLOT_COUNT && sSlots[slot] != NULL && sSlots[slot]->valid;
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

static void SaveNow(PlayState* play, SaveStateSlot* s) {
    s32 i, k;
    memcpy(s->sysHeap, gSystemHeap, SYSTEM_HEAP_SIZE);
    memcpy(s->audioHeap, gAudioHeap, AUDIO_HEAP_SIZE);
    memcpy(&s->audioContext, &gAudioContext, sizeof(AudioContext));
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
    memcpy(&gAudioContext, &s->audioContext, sizeof(AudioContext));
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

/* Game thread, between updates (graph.c via oot_ps3_debug_game_tick). */
void oot_ps3_savestate_tick(PlayState* play) {
    const int request = sRequest, slotIndex = sRequestSlot;
    char text[64];
    SaveStateSlot* s;
    if (request < 0) return;
    sRequest = -1;
    if (play == NULL || gSaveContext.gameMode != GAMEMODE_NORMAL) {
        SetStatus("Save states only work during gameplay");
        return;
    }
    if (request == 1) {
        s = sSlots[slotIndex];
        if (s == NULL || !s->valid) {
            snprintf(text, sizeof(text), "Slot %d is empty", slotIndex + 1);
            SetStatus(text);
            return;
        }
        if (s->gameState != (void*)play) {
            /* The game state moved in memory since the save (other area order): restoring
             * the heap would leave the running game pointing at the wrong place. */
            snprintf(text, sizeof(text), "Slot %d can't be loaded here (re-enter the area)", slotIndex + 1);
            SetStatus(text);
            return;
        }
    }
    if (sStaticSize == 0) sStaticSize = StaticSize();
    if (sSlots[slotIndex] == NULL) {
        sSlots[slotIndex] = (SaveStateSlot*)oot_ps3_big_alloc((unsigned int)(sizeof(SaveStateSlot) + sStaticSize));
        if (sSlots[slotIndex] == NULL) {
            SetStatus("Not enough memory for a save state");
            return;
        }
        sSlots[slotIndex]->valid = 0;
        sSlots[slotIndex]->staticSize = sStaticSize;
    }
    s = sSlots[slotIndex];
    oot_ps3_render_sync(); /* the render thread reads the heap while it draws */
    oot_ps3_audio_quiesce_begin();
    if (request == 0) {
        SaveNow(play, s);
    } else {
        LoadNow(play, s);
    }
    oot_ps3_audio_quiesce_end();
    snprintf(text, sizeof(text), "%s slot %d (scene %d)", request == 0 ? "Saved to" : "Loaded", slotIndex + 1,
             (int)s->sceneNum);
    SetStatus(text);
}
