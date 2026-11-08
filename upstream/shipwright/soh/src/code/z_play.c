#include "global.h"
#include <stdio.h>
#include "vt.h"

#include <string.h>

#include "soh/Enhancements/gameconsole.h"
#include "soh/frame_interpolation.h"
#include <overlays/actors/ovl_En_Niw/z_en_niw.h>
#include <overlays/misc/ovl_kaleido_scope/z_kaleido_scope.h>
#include "soh/Enhancements/enhancementTypes.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "soh/OTRGlobals.h"
#include "soh/ResourceManagerHelpers.h"
#include "soh/SaveManager.h"
#include "soh/framebuffer_effects.h"

#include <time.h>
#include <assert.h>

#ifdef __PS3__
extern void oot_ps3_early_trace(const char* message);
extern void oot_ps3_video_begin_play_probe(void);
#define PLAY_PS3_TRACE(message) oot_ps3_early_trace(message)
#else
#define PLAY_PS3_TRACE(message) ((void)0)
#endif

TransitionUnk sTrnsnUnk;
s32 gTrnsnUnkState;
VisMono gPlayVisMono;
Color_RGBA8_u32 gVisMonoColor;

FaultClient D_801614B8;

s16 sTransitionFillTimer;

void* gDebugCutsceneScript = NULL;
UNK_TYPE D_8012D1F4 = 0; // unused

Input* D_8012D1F8 = NULL;

PlayState* gPlayState;
s16 firstInit = 0;

void Play_SpawnScene(PlayState* play, s32 sceneId, s32 spawn);

// This macro prints the number "1" with a file and line number if R_ENABLE_PLAY_LOGS is enabled.
// For example, it can be used to trace the play state execution at a high level.
// SOHTODO: Revert log statements everywhere back to authentic, and deal with dynamic line/file names via macro
#define PLAY_LOG(line)                                  \
    do {                                                \
        if (1 & HREG(63)) {                             \
            LOG_NUM("1", 1 /*, "../z_play.c", line */); \
        }                                               \
    } while (0)

void enableBetaQuest();
void disableBetaQuest();

void OTRPlay_SpawnScene(PlayState* play, s32 sceneId, s32 spawn);

void Play_RequestViewpointBgCam(PlayState* play) {
    Camera_ChangeDataIdx(GET_ACTIVE_CAM(play), play->unk_1242B - 1);
}

void Play_SetViewpoint(PlayState* play, s16 viewpoint) {
    assert(viewpoint == 1 || viewpoint == 2);

    play->unk_1242B = viewpoint;

    if ((YREG(15) != 0x10) && (gSaveContext.cutsceneIndex < 0xFFF0)) {
        Audio_PlaySfxGeneral((viewpoint == 1) ? NA_SE_SY_CAMERA_ZOOM_DOWN : NA_SE_SY_CAMERA_ZOOM_UP, &gSfxDefaultPos, 4,
                             &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
    }

    Play_RequestViewpointBgCam(play);
}

/**
 * @return true if the currently set viewpoint is the same as the one provided in the argument
 */
s32 Play_CheckViewpoint(PlayState* play, s16 viewpoint) {
    return (viewpoint == play->unk_1242B);
}

/**
 * If the scene is a shop, set the viewpoint that will set the bgCamIndex
 * to toggle the camera into a "browsing item selection" setting.
 */
void Play_SetShopBrowsingViewpoint(PlayState* play) {
    osSyncPrintf("Game_play_shop_pr_vr_switch_set()\n");

    if (YREG(15) == 0x10) {
        play->unk_1242B = 2;
    }
}

void Gameplay_SetupTransition(PlayState* play, s32 transitionType) {
    TransitionContext* transitionCtx = &play->transitionCtx;

    memset(transitionCtx, 0, sizeof(TransitionContext));

    transitionCtx->transitionType = transitionType;

    // Circle Transition Types
    if ((transitionCtx->transitionType >> 5) == 1) {
        transitionCtx->init = TransitionCircle_Init;
        transitionCtx->destroy = TransitionCircle_Destroy;
        transitionCtx->start = TransitionCircle_Start;
        transitionCtx->isDone = TransitionCircle_IsDone;
        transitionCtx->draw = TransitionCircle_Draw;
        transitionCtx->update = TransitionCircle_Update;
        transitionCtx->setType = TransitionCircle_SetType;
        transitionCtx->setColor = TransitionCircle_SetColor;
        transitionCtx->setEnvColor = TransitionCircle_SetEnvColor;
    } else {
        switch (transitionCtx->transitionType) {
            case TRANS_TYPE_TRIFORCE:
                transitionCtx->init = TransitionTriforce_Init;
                transitionCtx->destroy = TransitionTriforce_Destroy;
                transitionCtx->start = TransitionTriforce_Start;
                transitionCtx->isDone = TransitionTriforce_IsDone;
                transitionCtx->draw = TransitionTriforce_Draw;
                transitionCtx->update = TransitionTriforce_Update;
                transitionCtx->setType = TransitionTriforce_SetType;
                transitionCtx->setColor = TransitionTriforce_SetColor;
                transitionCtx->setEnvColor = NULL;
                break;

            case TRANS_TYPE_WIPE:
            case TRANS_TYPE_WIPE_FAST:
                transitionCtx->init = TransitionWipe_Init;
                transitionCtx->destroy = TransitionWipe_Destroy;
                transitionCtx->start = TransitionWipe_Start;
                transitionCtx->isDone = TransitionWipe_IsDone;
                transitionCtx->draw = TransitionWipe_Draw;
                transitionCtx->update = TransitionWipe_Update;
                transitionCtx->setType = TransitionWipe_SetType;
                transitionCtx->setColor = TransitionWipe_SetColor;
                transitionCtx->setEnvColor = NULL;
                break;

            case TRANS_TYPE_FADE_BLACK:
            case TRANS_TYPE_FADE_WHITE:
            case TRANS_TYPE_FADE_BLACK_FAST:
            case TRANS_TYPE_FADE_WHITE_FAST:
            case TRANS_TYPE_FADE_BLACK_SLOW:
            case TRANS_TYPE_FADE_WHITE_SLOW:
            case TRANS_TYPE_FADE_WHITE_CS_DELAYED:
            case TRANS_TYPE_FADE_WHITE_INSTANT:
            case TRANS_TYPE_FADE_GREEN:
            case TRANS_TYPE_FADE_BLUE:
                transitionCtx->init = TransitionFade_Init;
                transitionCtx->destroy = TransitionFade_Destroy;
                transitionCtx->start = TransitionFade_Start;
                transitionCtx->isDone = TransitionFade_IsDone;
                transitionCtx->draw = TransitionFade_Draw;
                transitionCtx->update = TransitionFade_Update;
                transitionCtx->setType = TransitionFade_SetType;
                transitionCtx->setColor = TransitionFade_SetColor;
                transitionCtx->setEnvColor = NULL;
                break;

            case TRANS_TYPE_FILL_WHITE2:
            case TRANS_TYPE_FILL_WHITE:
                play->transitionMode = TRANS_MODE_FILL_WHITE_INIT;
                break;

            case TRANS_TYPE_INSTANT:
                play->transitionMode = TRANS_MODE_INSTANT;
                break;

            case TRANS_TYPE_FILL_BROWN:
                play->transitionMode = TRANS_MODE_FILL_BROWN_INIT;
                break;

            case TRANS_TYPE_SANDSTORM_PERSIST:
                play->transitionMode = TRANS_MODE_SANDSTORM_INIT;
                break;

            case TRANS_TYPE_SANDSTORM_END:
                play->transitionMode = TRANS_MODE_SANDSTORM_END_INIT;
                break;

            case TRANS_TYPE_CS_BLACK_FILL:
                play->transitionMode = TRANS_MODE_CS_BLACK_FILL_INIT;
                break;

            default:
                Fault_AddHungupAndCrash(__FILE__, __LINE__);
                break;
        }
    }
}

void func_800BC88C(PlayState* play) {
    play->transitionCtx.transitionType = -1;
}

Gfx* Play_SetFog(PlayState* play, Gfx* gfx) {
    return Gfx_SetFog2(gfx, play->lightCtx.fogColor[0], play->lightCtx.fogColor[1], play->lightCtx.fogColor[2], 0,
                       play->lightCtx.fogNear, 1000);
}

void Play_Destroy(GameState* thisx) {
    PlayState* play = (PlayState*)thisx;
    Player* player = GET_PLAYER(play);

    GameInteractor_ExecuteOnPlayDestroy();

    play->state.gfxCtx->callback = NULL;
    play->state.gfxCtx->callbackParam = 0;

    SREG(91) = 0;
    R_PAUSE_MENU_MODE = 0;

    PreRender_Destroy(&play->pauseBgPreRender);
    Effect_DeleteAll(play);
    EffectSs_ClearAll(play);
    CollisionCheck_DestroyContext(play, &play->colChkCtx);

    if (gTrnsnUnkState == 3) {
        TransitionUnk_Destroy(&sTrnsnUnk);
        gTrnsnUnkState = 0;
    }

    if (play->transitionMode == TRANS_MODE_INSTANCE_RUNNING) {
        play->transitionCtx.destroy(&play->transitionCtx.data);
        func_800BC88C(play);
        play->transitionMode = TRANS_MODE_OFF;
    }

    ShrinkWindow_Destroy();
    TransitionFade_Destroy(&play->transitionFade);
    VisMono_Destroy(&gPlayVisMono);

    if (gSaveContext.linkAge != play->linkAgeOnLoad) {
        Inventory_SwapAgeEquipment();
        Player_SetEquipmentData(play, player);
    }

    func_80031C3C(&play->actorCtx, play);
    func_80110990(play);
    KaleidoScopeCall_Destroy(play);
    KaleidoManager_Destroy();
    ZeldaArena_Cleanup();

    Fault_RemoveClient(&D_801614B8);

    disableBetaQuest();

    gPlayState = NULL;
}

u8 CheckStoneCount() {
    u8 stoneCount = 0;

    if (CHECK_QUEST_ITEM(QUEST_KOKIRI_EMERALD)) {
        stoneCount++;
    }

    if (CHECK_QUEST_ITEM(QUEST_GORON_RUBY)) {
        stoneCount++;
    }

    if (CHECK_QUEST_ITEM(QUEST_ZORA_SAPPHIRE)) {
        stoneCount++;
    }

    return stoneCount;
}

u8 CheckMedallionCount() {
    u8 medallionCount = 0;

    if (CHECK_QUEST_ITEM(QUEST_MEDALLION_FOREST)) {
        medallionCount++;
    }

    if (CHECK_QUEST_ITEM(QUEST_MEDALLION_FIRE)) {
        medallionCount++;
    }

    if (CHECK_QUEST_ITEM(QUEST_MEDALLION_WATER)) {
        medallionCount++;
    }

    if (CHECK_QUEST_ITEM(QUEST_MEDALLION_SHADOW)) {
        medallionCount++;
    }

    if (CHECK_QUEST_ITEM(QUEST_MEDALLION_SPIRIT)) {
        medallionCount++;
    }

    if (CHECK_QUEST_ITEM(QUEST_MEDALLION_LIGHT)) {
        medallionCount++;
    }

    return medallionCount;
}

u8 CheckDungeonCount() {
    u8 dungeonCount = 0;

    if (Flags_GetEventChkInf(EVENTCHKINF_USED_DEKU_TREE_BLUE_WARP)) {
        dungeonCount++;
    }

    if (Flags_GetEventChkInf(EVENTCHKINF_USED_DODONGOS_CAVERN_BLUE_WARP)) {
        dungeonCount++;
    }

    if (Flags_GetEventChkInf(EVENTCHKINF_USED_JABU_JABUS_BELLY_BLUE_WARP)) {
        dungeonCount++;
    }

    if (Flags_GetEventChkInf(EVENTCHKINF_USED_FOREST_TEMPLE_BLUE_WARP)) {
        dungeonCount++;
    }

    if (Flags_GetEventChkInf(EVENTCHKINF_USED_FIRE_TEMPLE_BLUE_WARP)) {
        dungeonCount++;
    }

    if (Flags_GetEventChkInf(EVENTCHKINF_USED_WATER_TEMPLE_BLUE_WARP)) {
        dungeonCount++;
    }

    if (Flags_GetRandomizerInf(RAND_INF_DUNGEONS_DONE_SPIRIT_TEMPLE)) {
        dungeonCount++;
    }

    if (Flags_GetRandomizerInf(RAND_INF_DUNGEONS_DONE_SHADOW_TEMPLE)) {
        dungeonCount++;
    }

    if (Flags_GetRandomizerInf(RAND_INF_DUNGEONS_DONE_GANONS_TOWER)) {
        dungeonCount++;
    }

    return dungeonCount;
}

u8 CheckBridgeRewardCount() {
    u8 bridgeRewardCount = 0;

    switch (Randomizer_GetSettingValue(RSK_BRIDGE_OPTIONS)) {
        case RO_BRIDGE_WILDCARD_REWARD:
            if (Flags_GetRandomizerInf(RAND_INF_GREG_FOUND)) {
                bridgeRewardCount += 1;
            }
            break;
        case RO_BRIDGE_GREG_REWARD:
            if (Flags_GetRandomizerInf(RAND_INF_GREG_FOUND)) {
                bridgeRewardCount += 1;
            }
            break;
    }
    return bridgeRewardCount;
}

#ifdef __PS3__
// Set by Play_Init: the next Play_Update applies the Environment_Init defaults.
// (A new PlayState can reuse the previous one's address, so the pointer can't tell.)
static s32 sPs3SceneEnvPending = 0;
static u32 sPs3PlayInitCount = 0; // Play states reuse the same address
static void Play_Ps3ApplySceneLighting(PlayState* play);
// Play_Update profiler: microseconds per section, reported every 64 updates.
extern unsigned long long oot_ps3_microseconds_now(void);
extern unsigned long long gPs3ActorUpdateUs[ACTOR_ID_MAX];
static unsigned long long sPs3Prof[8];
#define PS3_PROF(slot, stmt)                                                   \
    do {                                                                       \
        const unsigned long long ps3ProfT0 = oot_ps3_microseconds_now();       \
        stmt;                                                                  \
        sPs3Prof[slot] += oot_ps3_microseconds_now() - ps3ProfT0;              \
    } while (0)
static void Play_Ps3ProfileReport(void) {
    static u32 sFrames = 0;
    char line[256];
    s32 top[3] = { -1, -1, -1 };
    s32 i, k;
    if (++sFrames % 64 != 0) return;
    for (i = 0; i < ACTOR_ID_MAX; i++) {
        for (k = 0; k < 3; k++) {
            if (top[k] < 0 || gPs3ActorUpdateUs[i] > gPs3ActorUpdateUs[top[k]]) {
                s32 m;
                for (m = 2; m > k; m--) top[m] = top[m - 1];
                top[k] = i;
                break;
            }
        }
    }
    snprintf(line, sizeof(line),
             "graph: Play profile/64 room=%llu col=%llu actors=%llu cs=%llu fx=%llu ui=%llu anim=%llu cam=%llu us "
             "top %04x=%llu %04x=%llu %04x=%llu",
             sPs3Prof[0], sPs3Prof[1], sPs3Prof[2], sPs3Prof[3], sPs3Prof[4], sPs3Prof[5], sPs3Prof[6], sPs3Prof[7],
             (unsigned)top[0], gPs3ActorUpdateUs[top[0]], (unsigned)top[1], gPs3ActorUpdateUs[top[1]],
             (unsigned)top[2], gPs3ActorUpdateUs[top[2]]);
    oot_ps3_early_trace(line);
    memset(sPs3Prof, 0, sizeof(sPs3Prof));
    memset(gPs3ActorUpdateUs, 0, sizeof(unsigned long long) * ACTOR_ID_MAX);
}
// The Hyrule Field title intro uses the PS3 title stub (shipwright_vanilla_ps3.cpp);
// the other title demo scenes (Spirit Temple, Death Mountain Crater, cutscene map)
// go through the general scene loader like gameplay.
// 0 (default): the title intro is loaded by the general scene loader like every other
// scene (all 28 layer-7 actors: trees, bushes, drawbridge, river sound, En_Mag logo).
// 1: the old hand-made stub (Link, Epona and a manually spawned logo only).
// Keep in sync with OOT_PS3_USE_TITLE_STUB in shipwright_vanilla_ps3.cpp.
#define PLAY_PS3_USE_TITLE_STUB 0
#define PLAY_PS3_TITLE_STUB(play)                                                       \
    (PLAY_PS3_USE_TITLE_STUB && gSaveContext.gameMode == GAMEMODE_TITLE_SCREEN &&      \
     (play)->sceneNum == SCENE_HYRULE_FIELD)
void Environment_Ps3ResetLightning(void);
void Camera_Ps3ResetStatics(void);
#endif

void Play_Init(GameState* thisx) {
    PlayState* play = (PlayState*)thisx;
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    uintptr_t zAlloc;
    uintptr_t zAllocAligned;
    size_t zAllocSize;
    Player* player;
    s32 playerStartBgCamIndex;
    s32 i;
    u8 baseSceneLayer;
    s32 pad[2];

#ifdef __PS3__
    sPs3SceneEnvPending = 1;
    sPs3PlayInitCount++;
#endif
    PLAY_PS3_TRACE("play-init: entered");
    enableBetaQuest();
    PLAY_PS3_TRACE("play-init: after beta quest setup");

    // Properly initialize the frame counter so it doesn't use garbage data
    if (!firstInit) {
        play->gameplayFrames = 0;
        firstInit = 1;
    }

    // Invalid entrance, so immediately exit the game to opening title
    if (gSaveContext.entranceIndex == ENTR_LOAD_OPENING) {
        PLAY_PS3_TRACE("play-init: entrance requests return to Opening");
        gSaveContext.entranceIndex = 0;
        play->state.running = false;
        SET_NEXT_GAMESTATE(&play->state, Opening_Init, OpeningContext);
        GameInteractor_ExecuteOnExitGame(gSaveContext.fileNum);
        return;
    }

    gPlayState = play;

    PLAY_PS3_TRACE("play-init: before SystemArena_Display");
    SystemArena_Display();
    PLAY_PS3_TRACE("play-init: before GameState_Realloc");

    // OTRTODO allocate double the normal amount of memory
    // This is to avoid some parts of the game, like loading actors, causing OoM
    // This is potionally unavoidable due to struct size differences, but is x2 the right amount?
    GameState_Realloc(&play->state, 0x1D4790 * 2);
    PLAY_PS3_TRACE("play-init: after GameState_Realloc");
    PLAY_PS3_TRACE("play-init: before KaleidoManager_Init");
    KaleidoManager_Init(play);
    PLAY_PS3_TRACE("play-init: after KaleidoManager_Init");
    View_Init(&play->view, gfxCtx);
    Audio_SetExtraFilter(0);
    Quake_Init();

    PLAY_PS3_TRACE("play-init: before camera initialization");
    for (i = 0; i < ARRAY_COUNT(play->cameraPtrs); i++) {
        play->cameraPtrs[i] = NULL;
    }

    Camera_Init(&play->mainCamera, &play->view, &play->colCtx, play);
    Camera_ChangeStatus(&play->mainCamera, CAM_STAT_ACTIVE);

    for (i = 0; i < 3; i++) {
        Camera_Init(&play->subCameras[i], &play->view, &play->colCtx, play);
        Camera_ChangeStatus(&play->subCameras[i], CAM_STAT_UNK100);
    }

    play->cameraPtrs[CAM_ID_MAIN] = &play->mainCamera;
    play->cameraPtrs[CAM_ID_MAIN]->uid = 0;
    play->activeCamera = CAM_ID_MAIN;
    func_8005AC48(&play->mainCamera, 0xFF);
    PLAY_PS3_TRACE("play-init: before interface and effects initialization");
    // Sram_Init(this, &this->sramCtx);
    PLAY_PS3_TRACE("play-init: before Regs_InitData");
    Regs_InitData(play);
    PLAY_PS3_TRACE("play-init: after Regs_InitData");
    PLAY_PS3_TRACE("play-init: before Message_Init");
    Message_Init(play);
    PLAY_PS3_TRACE("play-init: after Message_Init");
    PLAY_PS3_TRACE("play-init: before GameOver_Init");
    GameOver_Init(play);
    PLAY_PS3_TRACE("play-init: after GameOver_Init");
    PLAY_PS3_TRACE("play-init: before SoundSource_InitAll");
    SoundSource_InitAll(play);
    PLAY_PS3_TRACE("play-init: after SoundSource_InitAll");
    PLAY_PS3_TRACE("play-init: before Effect_InitContext");
    Effect_InitContext(play);
    PLAY_PS3_TRACE("play-init: after Effect_InitContext");
    PLAY_PS3_TRACE("play-init: before EffectSs_InitInfo");
    EffectSs_InitInfo(play, 0x55);
    PLAY_PS3_TRACE("play-init: after EffectSs_InitInfo");
    PLAY_PS3_TRACE("play-init: before CollisionCheck_InitContext");
    CollisionCheck_InitContext(play, &play->colChkCtx);
    PLAY_PS3_TRACE("play-init: after CollisionCheck_InitContext");
    PLAY_PS3_TRACE("play-init: before AnimationContext_Reset");
    AnimationContext_Reset(&play->animationCtx);
    PLAY_PS3_TRACE("play-init: after AnimationContext_Reset");
    PLAY_PS3_TRACE("play-init: before func_8006450C");
    func_8006450C(play, &play->csCtx);
    PLAY_PS3_TRACE("play-init: after func_8006450C");
    PLAY_PS3_TRACE("play-init: before save and entrance setup");

    if (gSaveContext.nextCutsceneIndex != 0xFFEF) {
        gSaveContext.cutsceneIndex = gSaveContext.nextCutsceneIndex;
        gSaveContext.nextCutsceneIndex = 0xFFEF;
    }

    if (gSaveContext.cutsceneIndex == 0xFFFD) {
        gSaveContext.cutsceneIndex = 0;
    }

    if (gSaveContext.nextDayTime != 0xFFFF) {
        gSaveContext.dayTime = gSaveContext.nextDayTime;
        gSaveContext.skyboxTime = gSaveContext.nextDayTime;
    }

    if (gSaveContext.dayTime > 0xC000 || gSaveContext.dayTime < 0x4555) {
        gSaveContext.nightFlag = 1;
    } else {
        gSaveContext.nightFlag = 0;
    }

    PLAY_PS3_TRACE("play-init: before conditional cutscene triggers");
    Cutscene_HandleConditionalTriggers(play);
    PLAY_PS3_TRACE("play-init: after conditional cutscene triggers");

    if (gSaveContext.gameMode != GAMEMODE_NORMAL || gSaveContext.cutsceneIndex >= 0xFFF0) {
        gSaveContext.nayrusLoveTimer = 0;
        PLAY_PS3_TRACE("play-init: before Magic_Reset");
        Magic_Reset(play);
        PLAY_PS3_TRACE("play-init: after Magic_Reset");
        gSaveContext.sceneLayer = SCENE_LAYER_CUTSCENE_FIRST + (gSaveContext.cutsceneIndex & 0xF);
    } else if (!LINK_IS_ADULT && IS_DAY) {
        gSaveContext.sceneLayer = SCENE_LAYER_CHILD_DAY;
    } else if (!LINK_IS_ADULT && !IS_DAY) {
        gSaveContext.sceneLayer = SCENE_LAYER_CHILD_NIGHT;
    } else if (LINK_IS_ADULT && IS_DAY) {
        gSaveContext.sceneLayer = SCENE_LAYER_ADULT_DAY;
    } else {
        gSaveContext.sceneLayer = SCENE_LAYER_ADULT_NIGHT;
    }

    PLAY_PS3_TRACE("play-init: scene layer selected");
    // save the base scene layer (before accounting for the special cases below) to use later for the transition type
    baseSceneLayer = gSaveContext.sceneLayer;

    if ((gEntranceTable[((void)0, gSaveContext.entranceIndex)].scene == SCENE_HYRULE_FIELD) && !LINK_IS_ADULT &&
        !IS_CUTSCENE_LAYER) {
        if (CHECK_QUEST_ITEM(QUEST_KOKIRI_EMERALD) && CHECK_QUEST_ITEM(QUEST_GORON_RUBY) &&
            CHECK_QUEST_ITEM(QUEST_ZORA_SAPPHIRE)) {
            gSaveContext.sceneLayer = 1;
        } else {
            gSaveContext.sceneLayer = 0;
        }
    } else if ((gEntranceTable[((void)0, gSaveContext.entranceIndex)].scene == SCENE_KOKIRI_FOREST) && LINK_IS_ADULT &&
               !IS_CUTSCENE_LAYER) {
        gSaveContext.sceneLayer = (Flags_GetEventChkInf(EVENTCHKINF_USED_FOREST_TEMPLE_BLUE_WARP)) ? 3 : 2;
    }

    PLAY_PS3_TRACE("play-init: entrance lookup completed");
    PLAY_PS3_TRACE("play-init: before Play_SpawnScene");
    Play_SpawnScene(play,
                    gEntranceTable[((void)0, gSaveContext.entranceIndex) + ((void)0, gSaveContext.sceneLayer)].scene,
                    gEntranceTable[((void)0, gSaveContext.sceneLayer) + ((void)0, gSaveContext.entranceIndex)].spawn);
    PLAY_PS3_TRACE("play-init: after Play_SpawnScene");

    osSyncPrintf("\nSCENE_NO=%d COUNTER=%d\n", ((void)0, gSaveContext.entranceIndex), gSaveContext.sceneLayer);

#if 0
    // When entering Gerudo Valley in the credits, trigger the GC emulator to play the ending movie.
    // The emulator constantly checks whether PC is 0x81000000, so this works even though it's not a valid address.
    if ((gEntranceTable[((void)0, gSaveContext.save.entranceIndex)].sceneId == SCENE_GERUDO_VALLEY) &&
        gSaveContext.sceneLayer == 6) {
        PRINTF("エンディングはじまるよー\n"); // "The ending starts"
        ((void (*)(void))0x81000000)();
        PRINTF("出戻り？\n"); // "Return?"
    }
#endif

    PLAY_PS3_TRACE("play-init: before entrance cutscene triggers");
    Cutscene_HandleEntranceTriggers(play);
    PLAY_PS3_TRACE("play-init: after entrance cutscene triggers");
    KaleidoScopeCall_Init(play);
    PLAY_PS3_TRACE("play-init: after KaleidoScopeCall_Init");
    func_801109B0(play);
    PLAY_PS3_TRACE("play-init: after scene cutscene and kaleidoscope setup");

    if (gSaveContext.nextDayTime != 0xFFFF) {
        if (gSaveContext.nextDayTime == 0x8001) {
            gSaveContext.totalDays++;
            gSaveContext.bgsDayCount++;
            gSaveContext.dogIsLost = true;

            if (Inventory_HatchWeirdEgg(play) || Inventory_HatchPocketCucco(play)) {
                Message_StartTextbox(play, 0x3066, NULL);
            }

            gSaveContext.nextDayTime = 0xFFFE;
        } else {
            gSaveContext.nextDayTime = 0xFFFD;
        }
    }

    SREG(91) = -1;
    R_PAUSE_MENU_MODE = 0;
    PreRender_Init(&play->pauseBgPreRender);
    PreRender_SetValuesSave(&play->pauseBgPreRender, SCREEN_WIDTH, SCREEN_HEIGHT, NULL, NULL, NULL);
    PreRender_SetValues(&play->pauseBgPreRender, SCREEN_WIDTH, SCREEN_HEIGHT, NULL, NULL);
    gTrnsnUnkState = 0;
    play->transitionMode = TRANS_MODE_OFF;
    FrameAdvance_Init(&play->frameAdvCtx);
    Rand_Seed((u32)osGetTime());
    Matrix_Init(&play->state);
    play->state.main = Play_Main;
    play->state.destroy = Play_Destroy;
    play->transitionTrigger = TRANS_TRIGGER_END;
    play->unk_11E16 = 0xFF;
    play->bgCoverAlpha = 0;
    play->haltAllActors = false;

    if (gSaveContext.gameMode != GAMEMODE_TITLE_SCREEN) {
        if (gSaveContext.nextTransitionType == TRANS_NEXT_TYPE_DEFAULT) {
            play->transitionType = ENTRANCE_INFO_END_TRANS_TYPE(
                gEntranceTable[((void)0, gSaveContext.entranceIndex) + baseSceneLayer].field); // Fade In
        } else {
            play->transitionType = gSaveContext.nextTransitionType;
            gSaveContext.nextTransitionType = TRANS_NEXT_TYPE_DEFAULT;
        }
    } else {
        play->transitionType = TRANS_TYPE_FADE_BLACK_SLOW;
    }

    ShrinkWindow_Init();
    TransitionFade_Init(&play->transitionFade);
    TransitionFade_SetType(&play->transitionFade, 3);
    TransitionFade_SetColor(&play->transitionFade, RGBA8(160, 160, 160, 255));
    TransitionFade_Start(&play->transitionFade);
    VisMono_Init(&gPlayVisMono);
    gVisMonoColor.a = 0;
    Flags_UnsetAllEnv(play);

    osSyncPrintf("ZELDA ALLOC SIZE=%x\n", THA_GetSize(&play->state.tha));
    zAllocSize = THA_GetSize(&play->state.tha);
    zAlloc = (uintptr_t)GAMESTATE_ALLOC_MC(&play->state, zAllocSize);
    zAllocAligned = (zAlloc + 8) & ~0xF;
    ZeldaArena_Init((void*)zAllocAligned, zAllocSize - (zAllocAligned - zAlloc));
    PLAY_PS3_TRACE("play-init: after ZeldaArena_Init");
    // "Zelda Heap"
    osSyncPrintf("ゼルダヒープ %08x-%08x\n", zAllocAligned,
                 (u8*)zAllocAligned + zAllocSize - (s32)(zAllocAligned - zAlloc));

    Fault_AddClient(&D_801614B8, ZeldaArena_Display, NULL, NULL);

    // Handle Rocs Feather requirement
    gItemAgeReqs[ITEM_ROCS_FEATHER] = AGE_REQ_NONE;
    gSlotAgeReqs[SLOT_NAYRUS_LOVE] = AGE_REQ_NONE;

    PLAY_PS3_TRACE("play-init: before Actor_InitContext");
    Actor_InitContext(play, &play->actorCtx, play->linkActorEntry);
    PLAY_PS3_TRACE("play-init: after Actor_InitContext");

    PLAY_PS3_TRACE("play-init: before room load wait loop");
    while (!func_800973FC(play, &play->roomCtx)) {
        ; // Empty Loop
    }
    PLAY_PS3_TRACE("play-init: after room load wait loop");

    PLAY_PS3_TRACE("play-init: before Camera_InitPlayerSettings");
    player = GET_PLAYER(play);
#ifdef __PS3__
    if (PLAY_PS3_TITLE_STUB(play)) {
        // The title stub installs its collision differently; keep its camera path.
        PLAY_PS3_TRACE("play-init: deferring player camera settings until scene collision loads");
    } else {
        Camera_InitPlayerSettings(&play->mainCamera, player);
    }
#else
    Camera_InitPlayerSettings(&play->mainCamera, player);
#endif
    PLAY_PS3_TRACE("play-init: after Camera_InitPlayerSettings; before Camera_RequestMode");
    Camera_RequestMode(&play->mainCamera, CAM_MODE_NORMAL);
    PLAY_PS3_TRACE("play-init: after Camera_RequestMode");

    // OTRTODO: Bounds check cameraDataList to guard against scenes spawning the player with
    // an out of bounds background camera index. This requires adding an extra field to the
    // CollisionHeader struct to save the length of cameraDataList.
    // Fixes Dodongo's Cavern blue warp crash.
    {
        PLAY_PS3_TRACE("play-init: before scene collision header lookup");
#ifdef __PS3__
        CollisionHeader* colHeader = PLAY_PS3_TITLE_STUB(play)
                                         ? NULL
                                         : BgCheck_GetCollisionHeader(&play->colCtx, BGCHECK_SCENE);
#else
        CollisionHeader* colHeader = BgCheck_GetCollisionHeader(&play->colCtx, BGCHECK_SCENE);
#endif
        PLAY_PS3_TRACE("play-init: after scene collision header lookup");

        u8 camId = player->actor.params & 0xFF;
        // If the player's start cam is out of bounds, set it to 0xFF so it isn't used.
        if (colHeader != NULL && (camId != 0xFF) && (camId >= colHeader->cameraDataListLen)) {
            player->actor.params |= 0xFF;
        }
    }

    PLAY_PS3_TRACE("play-init: before player start camera selection");
    playerStartBgCamIndex = player->actor.params & 0xFF;
    if (playerStartBgCamIndex != 0xFF) {
        osSyncPrintf("player has start camera ID (" VT_FGCOL(BLUE) "%d" VT_RST ")\n", playerStartBgCamIndex);
        PLAY_PS3_TRACE("play-init: before Camera_ChangeDataIdx");
        Camera_ChangeDataIdx(&play->mainCamera, playerStartBgCamIndex);
        PLAY_PS3_TRACE("play-init: after Camera_ChangeDataIdx");
    }

    if (YREG(15) == 32) {
        play->unk_1242B = 2;
    } else if (YREG(15) == 16) {
        play->unk_1242B = 1;
    } else {
        play->unk_1242B = 0;
    }
    PLAY_PS3_TRACE("play-init: before final scene initialization");
    PLAY_PS3_TRACE("play-init-final: before Interface_SetSceneRestrictions");
    Interface_SetSceneRestrictions(play);
    PLAY_PS3_TRACE("play-init-final: after Interface_SetSceneRestrictions");
    Environment_PlaySceneSequence(play);
    PLAY_PS3_TRACE("play-init-final: after Environment_PlaySceneSequence");
    gSaveContext.seqId = play->sequenceCtx.seqId;
    gSaveContext.natureAmbienceId = play->sequenceCtx.natureAmbienceId;
    PLAY_PS3_TRACE("play-init-final: before Actor_InitPlayerHorse");
#ifdef __PS3__
    // Horse skeleton loading consumes the end of the startup budget. Start
    // the existing bounded Play budget before the genuine horse spawn.
    PLAY_PS3_TRACE("play-init-final: activating Play budget before genuine horse spawn");
    oot_ps3_video_begin_play_probe();
    extern int oot_ps3_stage_title_horse_object(PlayState* play);
    if (!PLAY_PS3_TITLE_STUB(play) ||
        oot_ps3_stage_title_horse_object(play)) {
        Actor_InitPlayerHorse(play, GET_PLAYER(play));
    } else {
        PLAY_PS3_TRACE("intro-horse: horse spawn deferred until native object bank is available");
    }
#else
    Actor_InitPlayerHorse(play, GET_PLAYER(play));
#endif
    PLAY_PS3_TRACE("play-init-final: before AnimationContext_Update");
    AnimationContext_Update(play, &play->animationCtx);
    PLAY_PS3_TRACE("play-init-final: after AnimationContext_Update");
    gSaveContext.respawnFlag = 0;

    // #region SOH [Stats]
    if (gSaveContext.ship.stats.sceneNum != gPlayState->sceneNum) {
        u16 idx = gSaveContext.ship.stats.tsIdx;
        gSaveContext.ship.stats.sceneTimestamps[idx].sceneTime = gSaveContext.ship.stats.sceneTimer / 2;
        gSaveContext.ship.stats.sceneTimestamps[idx].roomTime = gSaveContext.ship.stats.roomTimer / 2;
        gSaveContext.ship.stats.sceneTimestamps[idx].scene = gSaveContext.ship.stats.sceneNum;
        gSaveContext.ship.stats.sceneTimestamps[idx].room = gSaveContext.ship.stats.roomNum;
        gSaveContext.ship.stats.sceneTimestamps[idx].isRoom =
            gPlayState->sceneNum == gSaveContext.ship.stats.sceneTimestamps[idx].scene &&
            gPlayState->roomCtx.curRoom.num != gSaveContext.ship.stats.sceneTimestamps[idx].room;
        gSaveContext.ship.stats.tsIdx++;
        gSaveContext.ship.stats.sceneTimer = 0;
        gSaveContext.ship.stats.roomTimer = 0;
    } else if (gSaveContext.ship.stats.roomNum != gPlayState->roomCtx.curRoom.num) {
        u16 idx = gSaveContext.ship.stats.tsIdx;
        gSaveContext.ship.stats.sceneTimestamps[idx].roomTime = gSaveContext.ship.stats.roomTimer / 2;
        gSaveContext.ship.stats.sceneTimestamps[idx].scene = gSaveContext.ship.stats.sceneNum;
        gSaveContext.ship.stats.sceneTimestamps[idx].room = gSaveContext.ship.stats.roomNum;
        gSaveContext.ship.stats.sceneTimestamps[idx].isRoom =
            gPlayState->sceneNum == gSaveContext.ship.stats.sceneTimestamps[idx].scene &&
            gPlayState->roomCtx.curRoom.num != gSaveContext.ship.stats.sceneTimestamps[idx].room;
        gSaveContext.ship.stats.tsIdx++;
        gSaveContext.ship.stats.roomTimer = 0;
    }

    gSaveContext.ship.stats.sceneNum = gPlayState->sceneNum;
    gSaveContext.ship.stats.roomNum = gPlayState->roomCtx.curRoom.num;
    // #endregion

#if 0
    if (R_USE_DEBUG_CUTSCENE) {
        static u64 sDebugCutsceneScriptBuf[0xA00];

        gDebugCutsceneScript = sDebugCutsceneScriptBuf;
        PRINTF("\nkawauso_data=[%x]", gDebugCutsceneScript);

        // This hardcoded ROM address extends past the end of the ROM file.
        // Presumably the ROM was larger at a previous point in development when this debug feature was used.
        DmaMgr_DmaRomToRam(0x03FEB000, gDebugCutsceneScript, sizeof(sDebugCutsceneScriptBuf));
    }
#endif

    // nextEntranceIndex was not initialized, so the previous value was carried over during soft resets.
    gPlayState->nextEntranceIndex = gSaveContext.entranceIndex;
#ifdef __PS3__
    // Environment_Init's defaults belong to scene setup, before the first
    // cutscene command runs (frame 0 may turn on a black screen fill).
    Play_Ps3ApplySceneLighting(play);
#endif
    PLAY_PS3_TRACE("play-init: completed");
}

#ifdef __PS3__
// PS3: the lighting part of Environment_Init/Environment_Update, without the
// sky, time progression and audio work the full functions also do.
int oot_ps3_room_time_settings(unsigned char* hour, unsigned char* minute, unsigned char* increment);

// Room command 0x10, applied like Scene_CommandTimeSettings (soh/z_scene_otr.cpp).
// The title intro layer sets 4:00 AM with time speed 5; the sky, lights and
// sun/moon then follow the clock, which Play_Ps3UpdateTime advances.
static void Play_Ps3ApplyTimeSettings(PlayState* play) {
    unsigned char hour = 0xFF, minute = 0xFF, increment = 0xFF;
    char ps3Line[128];

    if (!oot_ps3_room_time_settings(&hour, &minute, &increment) &&
        PLAY_PS3_TITLE_STUB(play)) {
        hour = 4, minute = 0, increment = 5; /* intro layer values */
        oot_ps3_early_trace("graph: Play no room time settings; using intro default 4:00 speed 5");
    }
    if ((hour != 0xFF) && (minute != 0xFF)) {
        gSaveContext.skyboxTime = gSaveContext.dayTime =
            (u16)(((hour + (minute / 60.0f)) * 60.0f) / ((f32)(24 * 60) / 0x10000));
    }
    play->envCtx.timeIncrement = (increment != 0xFF) ? increment : 0;
    gTimeSpeed = play->envCtx.timeIncrement;
    play->envCtx.sunPos.x = -(Math_SinS(gSaveContext.dayTime - 0x8000) * 120.0f) * 25.0f;
    play->envCtx.sunPos.y = (Math_CosS(gSaveContext.dayTime - 0x8000) * 120.0f) * 25.0f;
    play->envCtx.sunPos.z = (Math_CosS(gSaveContext.dayTime - 0x8000) * 20.0f) * 25.0f;
    gSaveContext.nightFlag = (gSaveContext.dayTime > 0xC000 || gSaveContext.dayTime < 0x4555) ? 1 : 0;
    snprintf(ps3Line, sizeof(ps3Line), "graph: Play time settings hour=%u minute=%u speed=%u dayTime=%04x",
             (unsigned)hour, (unsigned)minute, (unsigned)gTimeSpeed, (unsigned)gSaveContext.dayTime);
    oot_ps3_early_trace(ps3Line);
}

// The clock part of Environment_Update (which does not run on PS3).
static void Play_Ps3UpdateTime(PlayState* play) {
    static u8 sPs3LastSky1 = 0xFF;
    static u8 sPs3LastSky2 = 0xFF;

    if ((play->pauseCtx.state == 0) && (play->gameOverCtx.state == GAMEOVER_INACTIVE) &&
        (((play->msgCtx.msgLength == 0) && (play->msgCtx.msgMode == 0)) ||
         (((void)0, gSaveContext.gameMode) == GAMEMODE_END_CREDITS)) &&
        (play->envCtx.unk_1A == 0) && !FrameAdvance_IsEnabled(play) &&
        (play->transitionMode == TRANS_MODE_OFF || ((void)0, gSaveContext.gameMode) != GAMEMODE_NORMAL)) {
        if (IS_DAY || gTimeSpeed >= 0x190) {
            gSaveContext.dayTime += gTimeSpeed;
        } else {
            gSaveContext.dayTime += gTimeSpeed * 2; // time moves twice as fast at night
        }
    } else {
        static s32 sPs3ClockBlockedLogs = 0;
        if (sPs3ClockBlockedLogs < 8 && (play->state.frames & 63) == 0) {
            char ps3Line[192];
            snprintf(ps3Line, sizeof(ps3Line),
                     "graph: Play clock blocked pause=%d gameover=%d msgLen=%d msgMode=%d unk_1A=%d frameAdv=%d "
                     "transMode=%d gameMode=%d",
                     (int)play->pauseCtx.state, (int)play->gameOverCtx.state, (int)play->msgCtx.msgLength,
                     (int)play->msgCtx.msgMode, (int)play->envCtx.unk_1A, (int)FrameAdvance_IsEnabled(play),
                     (int)play->transitionMode, (int)gSaveContext.gameMode);
            oot_ps3_early_trace(ps3Line);
            ++sPs3ClockBlockedLogs;
        }
    }
    if (((((void)0, gSaveContext.sceneLayer) >= 5 || gTimeSpeed != 0) &&
         ((void)0, gSaveContext.dayTime) > gSaveContext.skyboxTime) ||
        (((void)0, gSaveContext.dayTime) < 0xAAB)) {
        gSaveContext.skyboxTime = ((void)0, gSaveContext.dayTime);
    }
    gSaveContext.nightFlag = (gSaveContext.dayTime > 0xC000 || gSaveContext.dayTime < 0x4555) ? 1 : 0;

    if (play->envCtx.skybox1Index != sPs3LastSky1 || play->envCtx.skybox2Index != sPs3LastSky2) {
        char ps3Line[128];
        snprintf(ps3Line, sizeof(ps3Line), "graph: Play skybox %u/%u blend=%u skyboxTime=%04x dayTime=%04x",
                 (unsigned)play->envCtx.skybox1Index, (unsigned)play->envCtx.skybox2Index,
                 (unsigned)play->envCtx.skyboxBlend, (unsigned)gSaveContext.skyboxTime,
                 (unsigned)gSaveContext.dayTime);
        oot_ps3_early_trace(ps3Line);
        sPs3LastSky1 = play->envCtx.skybox1Index;
        sPs3LastSky2 = play->envCtx.skybox2Index;
    }
}

static void Play_Ps3ApplySceneLighting(PlayState* play) {
    EnvLightSettings* list = play->envCtx.lightSettingsList;
    EnvLightSettings* ls;
    u16 time = ((void)0, gSaveContext.dayTime);
    s32 idx;
    s32 i;

    if (list == NULL || play->envCtx.numLightSettings == 0) {
        return;
    }
    if (sPs3SceneEnvPending) {
        sPs3SceneEnvPending = 0;
        Lights_DirectionalSetInfo(&play->envCtx.dirLight1, 80, 80, 80, 80, 80, 80);
        LightContext_InsertLight(play, &play->lightCtx, &play->envCtx.dirLight1);
        Lights_DirectionalSetInfo(&play->envCtx.dirLight2, 80, 80, 80, 80, 80, 80);
        LightContext_InsertLight(play, &play->lightCtx, &play->envCtx.dirLight2);
        // Visual environment defaults from Environment_Init (which does not run on PS3).
        // Leftover values here drew full-screen overlays (gray over the sky) and storms.
        play->envCtx.gloomySkyMode = 0;
        play->envCtx.lightningMode = LIGHTNING_MODE_OFF;
        play->envCtx.fillScreen = false;
        play->envCtx.screenFillColor[0] = play->envCtx.screenFillColor[1] = 0;
        play->envCtx.screenFillColor[2] = play->envCtx.screenFillColor[3] = 0;
        play->envCtx.customSkyboxFilter = false;
        play->envCtx.skyboxFilterColor[0] = play->envCtx.skyboxFilterColor[1] = 0;
        play->envCtx.skyboxFilterColor[2] = play->envCtx.skyboxFilterColor[3] = 0;
        play->envCtx.sandstormState = SANDSTORM_OFF;
        play->envCtx.sandstormPrimA = 0;
        play->envCtx.sandstormEnvA = 0;
        play->envCtx.adjFogColor[2] = play->envCtx.adjFogNear = play->envCtx.adjFogFar = 0;
        play->envCtx.windDirection.x = play->envCtx.windDirection.y = play->envCtx.windDirection.z = 80;
        play->envCtx.blendIndoorLights = false;
        // Skybox/clock state from Environment_Init. unk_1A (skybox change timer)
        // gates the clock in Environment_Update; leftover values froze time.
        // Sky table row (scene command 0x11 weather; spot00 intro layer: 0) and
        // "nothing loaded yet" indices so the first Environment_UpdateSkybox loads
        // the textures for the current time, as after Environment_Init.
        if (PLAY_PS3_TITLE_STUB(play)) {
            play->envCtx.unk_17 = play->envCtx.unk_18 = 0; // the general scene loader sets these from 0x11
        }
        play->envCtx.skybox1Index = 99;
        play->envCtx.skybox2Index = 99;
        play->envCtx.unk_19 = 0;
        play->envCtx.unk_1A = 0;
        play->envCtx.unk_21 = 0;
        play->envCtx.unk_22 = 0;
        play->envCtx.skyboxDmaState = SKYBOX_DMA_INACTIVE;
        play->envCtx.unk_1F = 0;
        play->envCtx.unk_20 = 0;
        play->envCtx.unk_BD = 0;
        play->envCtx.unk_BE = 0;
        play->envCtx.unk_D8 = 1.0f;
        play->envCtx.unk_DC = 0;
        play->envCtx.unk_DE = 0;
        play->envCtx.unk_E0 = 0;
        // Effect counts (rain, Object_Kankyo fairies/snow) index fixed 64-entry arrays;
        // a leftover unk_EE[3] made Object_Kankyo write past its effects into the
        // next actor (Kokiri Forest: Bg_Treemouth's list links -> draw loop hang).
        play->envCtx.unk_EE[0] = play->envCtx.unk_EE[1] = 0;
        play->envCtx.unk_EE[2] = play->envCtx.unk_EE[3] = 0;
        play->envCtx.unk_F2[0] = 0;
        play->envCtx.unk_84 = 0.0f;
        play->envCtx.unk_88 = 0.0f;
        play->envCtx.unk_D6 = 0xFFFF;
        play->envCtx.adjAmbientColor[0] = play->envCtx.adjAmbientColor[1] = play->envCtx.adjAmbientColor[2] = 0;
        play->envCtx.adjLight1Color[0] = play->envCtx.adjLight1Color[1] = play->envCtx.adjLight1Color[2] = 0;
        play->envCtx.adjFogColor[0] = play->envCtx.adjFogColor[1] = 0;
        if (gSaveContext.retainWeatherMode == 0) {
            gWeatherMode = 0;
        }
        gSaveContext.retainWeatherMode = 0;
        play->roomCtx.unk_74[0] = play->roomCtx.unk_74[1] = 0;
        Environment_Ps3ResetLightning();
        // Environment_Init clears this every scene. A value left by the previous cutscene
        // (intro nightmare) let TRANS_TYPE_FADE_WHITE_CS_DELAYED stop waiting at once, so the
        // grey screen behind the Deku Tree's "Navi... where art thou?" faded away too early.
        gSaveContext.cutsceneTransitionControl = 0;
        Camera_Ps3ResetStatics();
        for (s32 ps3Cue = 0; ps3Cue < ARRAY_COUNT(play->csCtx.npcActions); ps3Cue++) {
            play->csCtx.npcActions[ps3Cue] = NULL;
        }
        gSaveContext.sunsSongState = SUNSSONG_INACTIVE;
        oot_ps3_early_trace("graph: Play PS3 environment visual defaults applied");
        Play_Ps3ApplyTimeSettings(play);
        oot_ps3_early_trace("graph: Play PS3 scene lighting lights inserted");
    }
    if (time < 0x4555 || time > 0xC000) {
        idx = 3; // night
    } else if (time < 0x5555) {
        idx = 0; // dawn
    } else if (time < 0xB555) {
        idx = 1; // day
    } else {
        idx = 2; // dusk
    }
    // Cutscene CS_CMD_SET_LIGHTING sets lightSettingOverride; Environment_Update
    // (not run on PS3) would switch to it. Honor it here.
    if (play->envCtx.unk_BF != 0xFF) {
        idx = play->envCtx.unk_BF;
    } else if (play->envCtx.indoors) {
        idx = play->envCtx.unk_BD; // indoor scenes use their light setting, not the time of day
    }
    if (idx >= play->envCtx.numLightSettings) {
        idx = 0;
    }
    {
        static s32 ps3LastIdx = -1;
        static s32 ps3EnvLogged = 0;
        if (idx != ps3LastIdx) {
            char ps3Line[128];
            snprintf(ps3Line, sizeof(ps3Line), "graph: Play light setting -> %d (override=%d dayTime=%04x)",
                     (int)idx, (int)play->envCtx.unk_BF, (unsigned)gSaveContext.dayTime);
            oot_ps3_early_trace(ps3Line);
            ps3LastIdx = idx;
        }
        if ((play->envCtx.fillScreen || play->envCtx.lightningMode != 0) && ps3EnvLogged < 40) {
            char ps3Line[160];
            snprintf(ps3Line, sizeof(ps3Line), "graph: Play env fill=%d color=(%d,%d,%d,%d) lightning=%d",
                     (int)play->envCtx.fillScreen, play->envCtx.screenFillColor[0], play->envCtx.screenFillColor[1],
                     play->envCtx.screenFillColor[2], play->envCtx.screenFillColor[3], (int)play->envCtx.lightningMode);
            oot_ps3_early_trace(ps3Line);
            ++ps3EnvLogged;
        }
    }
    ls = &list[idx];
    // Normally cleared by Environment_Init / scene command 0x12 (not run on PS3).
    if (PLAY_PS3_TITLE_STUB(play)) {
        // The title stub skips scene command 0x12; other scenes set these themselves.
        play->envCtx.sunMoonDisabled = 0;
        play->envCtx.lightningMode = 0; /* LIGHTNING_MODE_OFF: no leftover storm */
    }
    {
        static s32 ps3RegLogged = 0;
        if (!ps3RegLogged) {
            ps3RegLogged = 1;
            oot_ps3_early_trace(HREG(80) == 10 ? "graph: Play HREG80=10 (debug draw gate active)"
                                               : "graph: Play HREG80!=10 (sun/moon gate open)");
        }
    }
    for (i = 0; i < 3; i++) {
        play->lightCtx.ambientColor[i] = ls->ambientColor[i];
        play->envCtx.dirLight1.params.dir.color[i] = ls->light1Color[i];
        play->envCtx.dirLight2.params.dir.color[i] = ls->light2Color[i];
        play->lightCtx.fogColor[i] = ls->fogColor[i];
    }
    play->envCtx.dirLight1.params.dir.x = ls->light1Dir[0];
    play->envCtx.dirLight1.params.dir.y = ls->light1Dir[1];
    play->envCtx.dirLight1.params.dir.z = ls->light1Dir[2];
    play->envCtx.dirLight2.params.dir.x = ls->light2Dir[0];
    play->envCtx.dirLight2.params.dir.y = ls->light2Dir[1];
    play->envCtx.dirLight2.params.dir.z = ls->light2Dir[2];
    play->lightCtx.fogNear = ls->fogNear & 0x3FF;
    play->lightCtx.fogFar = ls->fogFar;
}
#endif

#ifdef __PS3__
// PS3: the scene stub spawns no scene actors, so the title logo actor
// (En_Mag: logo, fade-in, PRESS START) is spawned here, once, on the title screen.
void oot_ps3_trace_full_detail(void);
static void Play_Ps3SpawnTitleLogo(PlayState* play) {
    static u32 sLogoDoneFor = 0; // sPs3PlayInitCount of the Play state that got the logo
    static s32 sLogoWaitFrames = 0;
    s32 objIndex;
    Actor* logo;

    if (sLogoDoneFor == sPs3PlayInitCount || !PLAY_PS3_TITLE_STUB(play)) {
        return;
    }
    objIndex = Object_GetIndex(&play->objectCtx, OBJECT_MAG);
    if (objIndex < 0) {
        objIndex = Object_Spawn(&play->objectCtx, OBJECT_MAG);
        oot_ps3_early_trace("graph: Play title logo object requested");
    }
    if (objIndex < 0 || !Object_IsLoaded(&play->objectCtx, objIndex)) {
        if (++sLogoWaitFrames > 60) {
            sLogoDoneFor = sPs3PlayInitCount;
            oot_ps3_early_trace("graph: Play title logo object never loaded; giving up");
        }
        return;
    }
    sLogoDoneFor = sPs3PlayInitCount;
    sLogoWaitFrames = 0;
    oot_ps3_early_trace("graph: Play title logo object loaded; before Actor_Spawn (full detail on)");
    logo = Actor_Spawn(&play->actorCtx, play, ACTOR_EN_MAG, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0);
    oot_ps3_early_trace(logo != NULL ? "graph: Play title logo actor spawned (En_Mag)"
                                     : "graph: Play title logo actor spawn FAILED");
}
#endif

void Play_Update(PlayState* play) {
    PLAY_PS3_TRACE("play-update: entered");
    Input* input = play->state.input;
    s32 isPaused;
    s32 pad1;

    if ((SREG(1) < 0) || (DREG(0) != 0)) {
        SREG(1) = 0;
        ZeldaArena_Display();
    }

    if ((HREG(80) == 18) && (HREG(81) < 0)) {
        u32 i;
        s32 pad2;

        HREG(81) = 0;
        osSyncPrintf("object_exchange_rom_address %u\n", gObjectTableSize);
        osSyncPrintf("RomStart RomEnd   Size\n");

        for (i = 0; i < gObjectTableSize; i++) {
            ptrdiff_t size = gObjectTable[i].vromEnd - gObjectTable[i].vromStart;

            osSyncPrintf("%08x-%08x %08x(%8.3fKB)\n", gObjectTable[i].vromStart, gObjectTable[i].vromEnd, size,
                         size / 1024.0f);
        }

        osSyncPrintf("\n");
    }

    if ((HREG(81) == 18) && (HREG(82) < 0)) {
        HREG(82) = 0;
        // ActorOverlayTable_LogPrint();
    }

    PLAY_PS3_TRACE("play-update: before FreeLook CVar");
    if (CVarGetInteger(CVAR_SETTING("FreeLook.Enabled"), 0) && Player_InCsMode(play)) {
        play->manualCamera = false;
    }

    PLAY_PS3_TRACE("play-update: before segments and FrameAdvance_Update");
    gSegments[4] = VIRTUAL_TO_PHYSICAL(play->objectCtx.status[play->objectCtx.mainKeepIndex].segment);
    gSegments[5] = VIRTUAL_TO_PHYSICAL(play->objectCtx.status[play->objectCtx.subKeepIndex].segment);
    gSegments[2] = VIRTUAL_TO_PHYSICAL(play->sceneSegment);

    PLAY_PS3_TRACE("play-update: before FrameAdvance_Update");
    if (FrameAdvance_Update(&play->frameAdvCtx, &input[1])) {
        PLAY_PS3_TRACE("play-update: FrameAdvance_Update returned true");
        if ((play->transitionMode == TRANS_MODE_OFF) && (play->transitionTrigger != TRANS_TRIGGER_OFF)) {
            play->transitionMode = TRANS_MODE_SETUP;
        }

        // #region SOH [Stats] Gameplay stats: Count button presses
        if (!gSaveContext.ship.stats.gameComplete) {
            if (CHECK_BTN_ALL(input[0].press.button, BTN_A)) {
                gSaveContext.ship.stats.count[COUNT_BUTTON_PRESSES_A]++;
            }
            if (CHECK_BTN_ALL(input[0].press.button, BTN_B)) {
                gSaveContext.ship.stats.count[COUNT_BUTTON_PRESSES_B]++;
            }
            if (CHECK_BTN_ALL(input[0].press.button, BTN_CUP)) {
                gSaveContext.ship.stats.count[COUNT_BUTTON_PRESSES_CUP]++;
            }
            if (CHECK_BTN_ALL(input[0].press.button, BTN_CRIGHT)) {
                gSaveContext.ship.stats.count[COUNT_BUTTON_PRESSES_CRIGHT]++;
            }
            if (CHECK_BTN_ALL(input[0].press.button, BTN_CLEFT)) {
                gSaveContext.ship.stats.count[COUNT_BUTTON_PRESSES_CLEFT]++;
            }
            if (CHECK_BTN_ALL(input[0].press.button, BTN_CDOWN)) {
                gSaveContext.ship.stats.count[COUNT_BUTTON_PRESSES_CDOWN]++;
            }
            if (CHECK_BTN_ALL(input[0].press.button, BTN_DUP)) {
                gSaveContext.ship.stats.count[COUNT_BUTTON_PRESSES_DUP]++;
            }
            if (CHECK_BTN_ALL(input[0].press.button, BTN_DRIGHT)) {
                gSaveContext.ship.stats.count[COUNT_BUTTON_PRESSES_DRIGHT]++;
            }
            if (CHECK_BTN_ALL(input[0].press.button, BTN_DDOWN)) {
                gSaveContext.ship.stats.count[COUNT_BUTTON_PRESSES_DDOWN]++;
            }
            if (CHECK_BTN_ALL(input[0].press.button, BTN_DLEFT)) {
                gSaveContext.ship.stats.count[COUNT_BUTTON_PRESSES_DLEFT]++;
            }
            if (CHECK_BTN_ALL(input[0].press.button, BTN_L)) {
                gSaveContext.ship.stats.count[COUNT_BUTTON_PRESSES_L]++;
            }
            if (CHECK_BTN_ALL(input[0].press.button, BTN_R)) {
                gSaveContext.ship.stats.count[COUNT_BUTTON_PRESSES_R]++;
            }
            if (CHECK_BTN_ALL(input[0].press.button, BTN_Z)) {
                gSaveContext.ship.stats.count[COUNT_BUTTON_PRESSES_Z]++;
            }
            if (CHECK_BTN_ALL(input[0].press.button, BTN_START)) {
                gSaveContext.ship.stats.count[COUNT_BUTTON_PRESSES_START]++;
            }

            // Start RTA timing on first non-c-up input after intro cutscene
            if (!gSaveContext.ship.stats.firstInput && !Player_InCsMode(play) &&
                ((input[0].press.button && input[0].press.button != 0x8) || input[0].rel.stick_x != 0 ||
                 input[0].rel.stick_y != 0)) {
                gSaveContext.ship.stats.firstInput = GetUnixTimestamp();
            }
        }
        // #endregion

        if (gTrnsnUnkState != 0) {
            switch (gTrnsnUnkState) {
                case 2:
                    if (TransitionUnk_Init(&sTrnsnUnk, 10, 7) == NULL) {
                        osSyncPrintf("fbdemo_init呼出し失敗！\n"); // "fbdemo_init call failed!"
                        gTrnsnUnkState = 0;
                    } else {
                        sTrnsnUnk.zBuffer = (u16*)gZBuffer;
                        gTrnsnUnkState = 3;
                        R_UPDATE_RATE = 1;
                    }
                    break;
                case 3:
                    func_800B23E8(&sTrnsnUnk);
                    break;
            }
        }

        if ((u32)play->transitionMode != TRANS_MODE_OFF) {
            switch (play->transitionMode) {
                case TRANS_MODE_SETUP:
                    if (play->transitionTrigger != TRANS_TRIGGER_END) {
                        s16 sceneLayer = 0;
                        Interface_ChangeHudVisibilityMode(1);

                        if (gSaveContext.cutsceneIndex >= 0xFFF0) {
                            sceneLayer = SCENE_LAYER_CUTSCENE_FIRST + (gSaveContext.cutsceneIndex & 0xF);
                        }

                        // fade out bgm if "continue bgm" flag is not set
                        if (!(gEntranceTable[play->nextEntranceIndex + sceneLayer].field &
                              ENTRANCE_INFO_CONTINUE_BGM_FLAG)) {
                            // "Sound initalized. 111"
                            osSyncPrintf("\n\n\nサウンドイニシャル来ました。111");
                            if ((play->transitionType < TRANS_TYPE_MAX) && !Environment_IsForcedSequenceDisabled()) {
                                // "Sound initalized. 222"
                                osSyncPrintf("\n\n\nサウンドイニシャル来ました。222");
                                func_800F6964(0x14);
                                gSaveContext.seqId = (u8)NA_BGM_DISABLED;
                                gSaveContext.natureAmbienceId = NATURE_ID_DISABLED;
                            }
                        }
                    }

                    if (!R_TRANS_DBG_ENABLED) {
                        Gameplay_SetupTransition(play, play->transitionType);
                    } else {
                        Gameplay_SetupTransition(play, R_TRANS_DBG_TYPE);
                    }
#ifdef __PS3__
                    {
                        char ps3Line[128];
                        snprintf(ps3Line, sizeof(ps3Line),
                                 "graph: Play transition setup trigger=%d type=%d mode=%d csFrames=%d",
                                 (int)play->transitionTrigger, (int)play->transitionType,
                                 (int)play->transitionMode, (int)play->csCtx.frames);
                        oot_ps3_early_trace(ps3Line);
                    }
#endif

                    if (play->transitionMode >= TRANS_MODE_FILL_WHITE_INIT) {
                        // non-instance modes break out of this switch
                        break;
                    }
                    FALLTHROUGH;
                case TRANS_MODE_INSTANCE_INIT:
                    play->transitionCtx.init(&play->transitionCtx.data);

                    // Circle Transition Types
                    if ((play->transitionCtx.transitionType >> 5) == 1) {
                        play->transitionCtx.setType(&play->transitionCtx.data,
                                                    play->transitionCtx.transitionType | TC_SET_PARAMS);
                    }

                    gSaveContext.transWipeSpeed = 14;

                    if ((play->transitionCtx.transitionType == TRANS_TYPE_WIPE_FAST) ||
                        (play->transitionCtx.transitionType == TRANS_TYPE_FILL_WHITE2)) {
                        //! @bug TRANS_TYPE_FILL_WHITE2 will never reach this code.
                        //! It is a non-instance type transition which doesn't run this case.
                        gSaveContext.transWipeSpeed = 28;
                    }

                    gSaveContext.transFadeDuration = 60;

                    if ((play->transitionCtx.transitionType == TRANS_TYPE_FADE_BLACK_FAST) ||
                        (play->transitionCtx.transitionType == TRANS_TYPE_FADE_WHITE_FAST)) {
                        gSaveContext.transFadeDuration = 20;
                    } else if ((play->transitionCtx.transitionType == TRANS_TYPE_FADE_BLACK_SLOW) ||
                               (play->transitionCtx.transitionType == TRANS_TYPE_FADE_WHITE_SLOW)) {
                        gSaveContext.transFadeDuration = 150;
                    } else if (play->transitionCtx.transitionType == TRANS_TYPE_FADE_WHITE_INSTANT) {
                        gSaveContext.transFadeDuration = 2;
                    }

                    if ((play->transitionCtx.transitionType == TRANS_TYPE_FADE_WHITE) ||
                        (play->transitionCtx.transitionType == TRANS_TYPE_FADE_WHITE_FAST) ||
                        (play->transitionCtx.transitionType == TRANS_TYPE_FADE_WHITE_SLOW) ||
                        (play->transitionCtx.transitionType == TRANS_TYPE_FADE_WHITE_CS_DELAYED) ||
                        (play->transitionCtx.transitionType == TRANS_TYPE_FADE_WHITE_INSTANT)) {
                        play->transitionCtx.setColor(&play->transitionCtx.data, RGBA8(160, 160, 160, 255));

                        if (play->transitionCtx.setEnvColor != NULL) {
                            play->transitionCtx.setEnvColor(&play->transitionCtx.data, RGBA8(160, 160, 160, 255));
                        }
                    } else if (play->transitionCtx.transitionType == TRANS_TYPE_FADE_GREEN) {
                        play->transitionCtx.setColor(&play->transitionCtx.data, RGBA8(140, 140, 100, 255));

                        if (play->transitionCtx.setEnvColor != NULL) {
                            play->transitionCtx.setEnvColor(&play->transitionCtx.data, RGBA8(140, 140, 100, 255));
                        }
                    } else if (play->transitionCtx.transitionType == TRANS_TYPE_FADE_BLUE) {
                        play->transitionCtx.setColor(&play->transitionCtx.data, RGBA8(70, 100, 110, 255));

                        if (play->transitionCtx.setEnvColor != NULL) {
                            play->transitionCtx.setEnvColor(&play->transitionCtx.data, RGBA8(70, 100, 110, 255));
                        }
                    } else {
                        play->transitionCtx.setColor(&play->transitionCtx.data, RGBA8(0, 0, 0, 0));

                        if (play->transitionCtx.setEnvColor != NULL) {
                            play->transitionCtx.setEnvColor(&play->transitionCtx.data, RGBA8(0, 0, 0, 0));
                        }
                    }

                    if (play->transitionTrigger == TRANS_TRIGGER_END) {
                        play->transitionCtx.setType(&play->transitionCtx.data, 1);
                    } else {
                        play->transitionCtx.setType(&play->transitionCtx.data, 2);
                    }

                    play->transitionCtx.start(&play->transitionCtx);

                    if (play->transitionCtx.transitionType == TRANS_TYPE_FADE_WHITE_CS_DELAYED) {
                        play->transitionMode = TRANS_MODE_INSTANCE_WAIT;
                    } else {
                        play->transitionMode = TRANS_MODE_INSTANCE_RUNNING;
                    }
                    break;

                case TRANS_MODE_INSTANCE_RUNNING:
                    if (play->transitionCtx.isDone(&play->transitionCtx.data)) {
                        if (play->transitionCtx.transitionType >= TRANS_TYPE_MAX) {
                            if (play->transitionTrigger == TRANS_TRIGGER_END) {
                                play->transitionCtx.destroy(&play->transitionCtx.data);
                                func_800BC88C(play);
                                play->transitionMode = TRANS_MODE_OFF;
                            }
                        } else if (play->transitionTrigger != TRANS_TRIGGER_END) {
                            play->state.running = false;

                            if (gSaveContext.gameMode != GAMEMODE_FILE_SELECT) {
                                SET_NEXT_GAMESTATE(&play->state, Play_Init, PlayState);
                                gSaveContext.entranceIndex = play->nextEntranceIndex;

                                if (gSaveContext.minigameState == 1) {
                                    gSaveContext.minigameState = 3;
                                }
                            } else {
                                SET_NEXT_GAMESTATE(&play->state, FileChoose_Init, FileChooseContext);
                            }
                        } else {
                            PLAY_PS3_TRACE("play-update: fade finished; before transition destroy");
                            play->transitionCtx.destroy(&play->transitionCtx.data);
                            PLAY_PS3_TRACE("play-update: after transition destroy");
                            func_800BC88C(play);
                            PLAY_PS3_TRACE("play-update: after func_800BC88C");
                            play->transitionMode = TRANS_MODE_OFF;

                            if (gTrnsnUnkState == 3) {
                                TransitionUnk_Destroy(&sTrnsnUnk);
                                gTrnsnUnkState = 0;
                                R_UPDATE_RATE = 3;
                            }

                            // Transition end for standard transitions
#ifdef __PS3__
                            PLAY_PS3_TRACE("play-update: PS3 skips transition-end hooks; fade complete");
#else
                            GameInteractor_ExecuteOnTransitionEndHooks(play->sceneNum);
#endif
                        }

                        play->transitionTrigger = TRANS_TRIGGER_OFF;
                    } else {
                        play->transitionCtx.update(&play->transitionCtx.data, R_UPDATE_RATE);
                    }
                    break;
            }

            // update non-instance transitions
            switch (play->transitionMode) {
                case TRANS_MODE_FILL_WHITE_INIT:
                    sTransitionFillTimer = 0;
                    play->envCtx.fillScreen = true;
                    play->envCtx.screenFillColor[0] = 160;
                    play->envCtx.screenFillColor[1] = 160;
                    play->envCtx.screenFillColor[2] = 160;

                    if (play->transitionTrigger != TRANS_TRIGGER_END) {
                        play->envCtx.screenFillColor[3] = 0;
                        play->transitionMode = TRANS_MODE_FILL_IN;
                    } else {
                        play->envCtx.screenFillColor[3] = 255;
                        play->transitionMode = TRANS_MODE_FILL_OUT;
                    }
                    break;

                case TRANS_MODE_FILL_IN:
                    play->envCtx.screenFillColor[3] = (sTransitionFillTimer / 20.0f) * 255.0f;

                    if (sTransitionFillTimer >= 20) {
                        play->state.running = false;
                        SET_NEXT_GAMESTATE(&play->state, Play_Init, PlayState);
                        gSaveContext.entranceIndex = play->nextEntranceIndex;
                        play->transitionTrigger = TRANS_TRIGGER_OFF;
                        play->transitionMode = TRANS_MODE_OFF;
                    } else {
                        sTransitionFillTimer++;
                    }
                    break;

                case TRANS_MODE_FILL_OUT:
                    play->envCtx.screenFillColor[3] = (1 - sTransitionFillTimer / 20.0f) * 255.0f;

                    if (sTransitionFillTimer >= 20) {
                        gTrnsnUnkState = 0;
                        R_UPDATE_RATE = 3;
                        play->transitionTrigger = TRANS_TRIGGER_OFF;
                        play->transitionMode = TRANS_MODE_OFF;
                        play->envCtx.fillScreen = false;
                    } else {
                        sTransitionFillTimer++;
                    }
                    break;

                case TRANS_MODE_FILL_BROWN_INIT:
                    sTransitionFillTimer = 0;
                    play->envCtx.fillScreen = true;
                    play->envCtx.screenFillColor[0] = 170;
                    play->envCtx.screenFillColor[1] = 160;
                    play->envCtx.screenFillColor[2] = 150;

                    if (play->transitionTrigger != TRANS_TRIGGER_END) {
                        play->envCtx.screenFillColor[3] = 0;
                        play->transitionMode = TRANS_MODE_FILL_IN;
                    } else {
                        play->envCtx.screenFillColor[3] = 255;
                        play->transitionMode = TRANS_MODE_FILL_OUT;
                    }
                    break;

                case TRANS_MODE_INSTANT:
                    if (play->transitionTrigger != TRANS_TRIGGER_END) {
                        play->state.running = 0;
                        SET_NEXT_GAMESTATE(&play->state, Play_Init, PlayState);
                        gSaveContext.entranceIndex = play->nextEntranceIndex;
                        play->transitionTrigger = TRANS_TRIGGER_OFF;
                        play->transitionMode = TRANS_MODE_OFF;
                    } else {
                        gTrnsnUnkState = 0;
                        R_UPDATE_RATE = 3;
                        play->transitionTrigger = TRANS_TRIGGER_OFF;
                        play->transitionMode = TRANS_MODE_OFF;
                    }
                    break;

                case TRANS_MODE_INSTANCE_WAIT:
                    if (gSaveContext.cutsceneTransitionControl != 0) {
                        play->transitionMode = TRANS_MODE_INSTANCE_RUNNING;
                    }
                    break;

                case TRANS_MODE_SANDSTORM_INIT:
                    if (play->transitionTrigger != TRANS_TRIGGER_END) {
                        play->envCtx.sandstormState = SANDSTORM_FILL;
                        play->transitionMode = TRANS_MODE_SANDSTORM;
                    } else {
                        play->envCtx.sandstormState = SANDSTORM_UNFILL;
                        play->envCtx.sandstormPrimA = 255;
                        play->envCtx.sandstormEnvA = 255;
                        play->transitionMode = TRANS_MODE_SANDSTORM;
                    }
                    break;

                case TRANS_MODE_SANDSTORM:
                    Audio_PlaySfxGeneral(NA_SE_EV_SAND_STORM - SFX_FLAG, &gSfxDefaultPos, 4,
                                         &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);

                    if (play->transitionTrigger == TRANS_TRIGGER_END) {
                        if (play->envCtx.sandstormPrimA < 110) {
                            gTrnsnUnkState = 0;
                            R_UPDATE_RATE = 3;
                            play->transitionTrigger = TRANS_TRIGGER_OFF;
                            play->transitionMode = TRANS_MODE_OFF;

                            // Transition end for sandstorm effect (delayed until effect is finished)
                            GameInteractor_ExecuteOnTransitionEndHooks(play->sceneNum);
                        }
                    } else {
                        if (play->envCtx.sandstormEnvA == 255) {
                            play->state.running = false;
                            SET_NEXT_GAMESTATE(&play->state, Play_Init, PlayState);
                            gSaveContext.entranceIndex = play->nextEntranceIndex;
                            play->transitionTrigger = TRANS_TRIGGER_OFF;
                            play->transitionMode = TRANS_MODE_OFF;
                        }
                    }
                    break;

                case TRANS_MODE_SANDSTORM_END_INIT:
                    if (play->transitionTrigger == TRANS_TRIGGER_END) {
                        play->envCtx.sandstormState = SANDSTORM_DISSIPATE;
                        play->envCtx.sandstormPrimA = 255;
                        play->envCtx.sandstormEnvA = 255;
                        // "It's here!!!!!!!!!"
                        LOG_STRING("来た!!!!!!!!!!!!!!!!!!!!!");
                        play->transitionMode = TRANS_MODE_SANDSTORM_END;
                    } else {
                        play->transitionMode = TRANS_MODE_SANDSTORM_INIT;
                    }
                    break;

                case TRANS_MODE_SANDSTORM_END:
                    Audio_PlaySfxGeneral(NA_SE_EV_SAND_STORM - SFX_FLAG, &gSfxDefaultPos, 4,
                                         &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);

                    if (play->transitionTrigger == TRANS_TRIGGER_END) {
                        if (play->envCtx.sandstormPrimA <= 0) {
                            gTrnsnUnkState = 0;
                            R_UPDATE_RATE = 3;
                            play->transitionTrigger = TRANS_TRIGGER_OFF;
                            play->transitionMode = TRANS_MODE_OFF;

                            // Transition end for sandstorm effect (delayed until effect is finished)
                            GameInteractor_ExecuteOnTransitionEndHooks(play->sceneNum);
                        }
                    }
                    break;

                case TRANS_MODE_CS_BLACK_FILL_INIT:
                    sTransitionFillTimer = 0;
                    play->envCtx.fillScreen = true;
                    play->envCtx.screenFillColor[0] = 0;
                    play->envCtx.screenFillColor[1] = 0;
                    play->envCtx.screenFillColor[2] = 0;
                    play->envCtx.screenFillColor[3] = 255;
                    play->transitionMode = TRANS_MODE_CS_BLACK_FILL;
                    break;

                case TRANS_MODE_CS_BLACK_FILL:
                    if (gSaveContext.cutsceneTransitionControl != 0) {
                        play->envCtx.screenFillColor[3] = gSaveContext.cutsceneTransitionControl;

                        if (gSaveContext.cutsceneTransitionControl <= 100) {
                            gTrnsnUnkState = 0;
                            R_UPDATE_RATE = 3;
                            play->transitionTrigger = TRANS_TRIGGER_OFF;
                            play->transitionMode = TRANS_MODE_OFF;
                        }
                    }
                    break;
            }
        }

        PLAY_LOG(3533);

        if (1 && (gTrnsnUnkState != 3)) {
            PLAY_LOG(3542);

            if ((gSaveContext.gameMode == GAMEMODE_NORMAL) && (play->msgCtx.msgMode == MSGMODE_NONE) &&
                (play->gameOverCtx.state == GAMEOVER_INACTIVE)) {
                KaleidoSetup_Update(play);
            }

            PLAY_LOG(3551);
            isPaused = (play->pauseCtx.state != 0) || (play->pauseCtx.debugState != 0);

            PLAY_LOG(3555);
            AnimationContext_Reset(&play->animationCtx);

            PLAY_LOG(3561);
            Object_UpdateBank(&play->objectCtx);

            PLAY_LOG(3577);

            if (!isPaused && (IREG(72) == 0)) {
                PLAY_LOG(3580);

                play->gameplayFrames++;
                func_800AA178(true);

                // Gameplay stat tracking
                if (!gSaveContext.ship.stats.gameComplete &&
                    (!IS_BOSS_RUSH || !gSaveContext.ship.quest.data.bossRush.isPaused)) {
                    gSaveContext.ship.stats.playTimer++;
                    gSaveContext.ship.stats.sceneTimer++;
                    gSaveContext.ship.stats.roomTimer++;
                }

                if (play->actorCtx.freezeFlashTimer && (play->actorCtx.freezeFlashTimer-- < 5)) {
                    if (GameInteractor_Should(VB_FLASH_SCREEN_FOR_FINISHING_BLOW, true)) {
                        osSyncPrintf("FINISH=%d\n", play->actorCtx.freezeFlashTimer);

                        if ((play->actorCtx.freezeFlashTimer > 0) && ((play->actorCtx.freezeFlashTimer % 2) != 0)) {
                            play->envCtx.fillScreen = true;
                            play->envCtx.screenFillColor[0] = play->envCtx.screenFillColor[1] =
                                play->envCtx.screenFillColor[2] = 150;
                            play->envCtx.screenFillColor[3] = 80;
                        } else {
                            play->envCtx.fillScreen = false;
                        }
                    }
                } else {
                    PLAY_LOG(3606);
                    PS3_PROF(0, func_800973FC(play, &play->roomCtx));

                    PLAY_LOG(3612);
                    PLAY_PS3_TRACE("play-update: before CollisionCheck_AT");
                    PS3_PROF(1, {
                        CollisionCheck_AT(play, &play->colChkCtx);
                        CollisionCheck_OC(play, &play->colChkCtx);
                        CollisionCheck_Damage(play, &play->colChkCtx);
                        CollisionCheck_ClearContext(play, &play->colChkCtx);
                    });

                    PLAY_LOG(3637);

                    if (!play->haltAllActors) {
                        PLAY_PS3_TRACE("play-update: before Actor_UpdateAll");
                        PS3_PROF(2, Actor_UpdateAll(play, &play->actorCtx));
                        PLAY_PS3_TRACE("play-update: after Actor_UpdateAll");
                    }

                    PLAY_LOG(3643);
                    PLAY_PS3_TRACE("play-update: before cutscene update 64558");
                    PS3_PROF(3, func_80064558(play, &play->csCtx));
                    PLAY_PS3_TRACE("play-update: after cutscene update 64558");

                    PLAY_LOG(3648);
                    PLAY_PS3_TRACE("play-update: before cutscene update 645A0");
                    PS3_PROF(3, func_800645A0(play, &play->csCtx));
                    PLAY_PS3_TRACE("play-update: after cutscene update 645A0");

                    PLAY_LOG(3651);
                    PLAY_PS3_TRACE("play-update: before Effect_UpdateAll");
                    PS3_PROF(4, Effect_UpdateAll(play));
                    PLAY_PS3_TRACE("play-update: after Effect_UpdateAll");

                    PLAY_LOG(3657);
                    PLAY_PS3_TRACE("play-update: before EffectSs_UpdateAll");
                    PS3_PROF(4, EffectSs_UpdateAll(play));
                    PLAY_PS3_TRACE("play-update: after EffectSs_UpdateAll");

                    PLAY_LOG(3662);
                }
            } else {
                func_800AA178(false);
            }

            PLAY_LOG(3672);
            PLAY_PS3_TRACE("play-update: before room updates");
            func_80095AA0(play, &play->roomCtx.curRoom, &input[1], 0);

            PLAY_LOG(3675);
            func_80095AA0(play, &play->roomCtx.prevRoom, &input[1], 1);

            PLAY_LOG(3677);

            if (play->unk_1242B != 0) {
                if (CHECK_BTN_ALL(input[0].press.button, BTN_CUP)) {
                    if ((play->pauseCtx.state != 0) || (play->pauseCtx.debugState != 0)) {
                        // "Changing viewpoint is prohibited due to the kaleidoscope"
                        osSyncPrintf(VT_FGCOL(CYAN) "カレイドスコープ中につき視点変更を禁止しております\n" VT_RST);
                    } else if (Player_InCsMode(play)) {
                        // "Changing viewpoint is prohibited during the cutscene"
                        osSyncPrintf(VT_FGCOL(CYAN) "デモ中につき視点変更を禁止しております\n" VT_RST);
                    } else if (YREG(15) == 0x10) {
                        Audio_PlaySfxGeneral(NA_SE_SY_ERROR, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale,
                                             &gSfxDefaultFreqAndVolScale, &gSfxDefaultReverb);
                    } else {
                        // C-Up toggle for houses, move between pivot camera and fixed camera
                        // Toggle viewpoint between VIEWPOINT_LOCKED and VIEWPOINT_PIVOT
                        Play_SetViewpoint(play, play->unk_1242B ^ 3);
                    }
                }

                Play_RequestViewpointBgCam(play);
            }

            PLAY_LOG(3708);
            PLAY_PS3_TRACE("play-update: before skybox update");
            SkyboxDraw_Update(&play->skyboxCtx);

            PLAY_LOG(3716);

            PLAY_PS3_TRACE("play-update: before pause, game over, message update");
            if ((play->pauseCtx.state != 0) || (play->pauseCtx.debugState != 0)) {
                PLAY_LOG(3721);
                KaleidoScopeCall_Update(play);
            } else if (play->gameOverCtx.state != GAMEOVER_INACTIVE) {
                PLAY_LOG(3727);
                GameOver_Update(play);
            } else {
                PLAY_LOG(3733);
                PS3_PROF(5, Message_Update(play));
            }

            PLAY_LOG(3737);

            PLAY_LOG(3742);
            PLAY_PS3_TRACE("play-update: before Interface_Update");
#ifdef __PS3__
            if (gSaveContext.gameMode == GAMEMODE_TITLE_SCREEN) {
                PLAY_PS3_TRACE("intro-route: title HUD update deferred; scene update continues");
            } else {
                PS3_PROF(5, Interface_Update(play));
            }
#else
            Interface_Update(play);
#endif

            PLAY_LOG(3765);
            PLAY_PS3_TRACE("play-update: before AnimationContext_Update");
            PS3_PROF(6, AnimationContext_Update(play, &play->animationCtx));

            PLAY_LOG(3771);
            PLAY_PS3_TRACE("play-update: before SoundSource_UpdateAll");
            PS3_PROF(6, SoundSource_UpdateAll(play));

            PLAY_LOG(3777);
            ShrinkWindow_Update(R_UPDATE_RATE);

            PLAY_LOG(3783);
            TransitionFade_Update(&play->transitionFade, R_UPDATE_RATE);
        } else {
            goto skip;
        }
    }

    PLAY_LOG(3799);

skip:
    PLAY_LOG(3801);

    PLAY_PS3_TRACE("play-update: before camera state hook");
    GameInteractor_ExecuteOnCameraState(play);
    PLAY_PS3_TRACE("play-update: after camera state hook");

    if (!isPaused || gDbgCamEnabled) {
        s32 i;

        play->nextCamera = play->activeCamera;

        PLAY_LOG(3806);

        for (i = 0; i < NUM_CAMS; i++) {
            if ((i != play->nextCamera) && (play->cameraPtrs[i] != NULL)) {
                PLAY_LOG(3809);
                PS3_PROF(7, Camera_Update(play->cameraPtrs[i]));
            }
        }

        PLAY_PS3_TRACE("play-update: before active Camera_Update");
        PS3_PROF(7, Camera_Update(play->cameraPtrs[play->nextCamera]));
        PLAY_PS3_TRACE("play-update: after active Camera_Update");
        Play_Ps3ProfileReport();

        PLAY_LOG(3814);
    }

    PLAY_LOG(3816);
    PLAY_PS3_TRACE("play-update: before Environment_Update");
#ifdef __PS3__
    // V48 has only decoded the player spawn and room reference. Keep the
    // first frame bounded until scene environment commands are installed.
    PLAY_PS3_TRACE("play-update: PS3 defers Environment_Update until scene commands load");
    Play_Ps3UpdateTime(play);
    Play_Ps3ApplySceneLighting(play);
    Play_Ps3SpawnTitleLogo(play);
    // The title intro's exit (cutscene destination 104) now goes on to the next
    // title demo scene like the original game instead of replaying this intro.
#else
    Environment_Update(play, &play->envCtx, &play->lightCtx, &play->pauseCtx, &play->msgCtx, &play->gameOverCtx,
                       play->state.gfxCtx);
#endif
    PLAY_PS3_TRACE("play-update: completed");
}

void Play_DrawOverlayElements(PlayState* play) {
    if ((play->pauseCtx.state != 0) || (play->pauseCtx.debugState != 0)) {
        KaleidoScopeCall_Draw(play);
    }

    if (gSaveContext.gameMode == GAMEMODE_NORMAL) {
        Interface_Draw(play);
    }

    Message_Draw(play);

    if (play->gameOverCtx.state != GAMEOVER_INACTIVE) {
        GameOver_FadeInLights(play);
    }
}

void Play_Draw(PlayState* play) {
    PLAY_PS3_TRACE("play-draw: entered");
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    Lights* sp228;
    Vec3f sp21C;

    // #region SOH [Port] Frame buffer effects for pause menu
    // Track render size when paused and that a copy was performed
    static u32 lastPauseWidth;
    static u32 lastPauseHeight;
    static bool lastAltAssets;
    static bool hasCapturedPauseBuffer;
    bool recapturePauseBuffer = false;

    // If the size has changed, alt assets toggled, or dropped frames leading to the buffer not being copied,
    // set the prerender state back to setup to copy a new frame.
    // This requires not rendering kaleido during this copy to avoid kaleido itself being copied too.
    if ((R_PAUSE_MENU_MODE == 2 || R_PAUSE_MENU_MODE == 3) &&
        (lastPauseWidth != OTRGetGameRenderWidth() || lastPauseHeight != OTRGetGameRenderHeight() ||
         lastAltAssets != ResourceMgr_IsAltAssetsEnabled() || !hasCapturedPauseBuffer)) {
        R_PAUSE_MENU_MODE = 1;
        recapturePauseBuffer = true;
    }
    // #endregion

    PLAY_PS3_TRACE("play-draw: before opening display lists");
    OPEN_DISPS(gfxCtx);
    PLAY_PS3_TRACE("play-draw: display lists opened");

    PLAY_PS3_TRACE("play-draw: before main keep segment conversion");
    gSegments[4] = VIRTUAL_TO_PHYSICAL(play->objectCtx.status[play->objectCtx.mainKeepIndex].segment);
    PLAY_PS3_TRACE("play-draw: before sub keep segment conversion");
    gSegments[5] = VIRTUAL_TO_PHYSICAL(play->objectCtx.status[play->objectCtx.subKeepIndex].segment);
    PLAY_PS3_TRACE("play-draw: before scene segment conversion");
    gSegments[2] = VIRTUAL_TO_PHYSICAL(play->sceneSegment);
    PLAY_PS3_TRACE("play-draw: segments converted; before null segment commands");

    gSPSegment(POLY_OPA_DISP++, 0x00, NULL);
    gSPSegment(POLY_XLU_DISP++, 0x00, NULL);
    gSPSegment(OVERLAY_DISP++, 0x00, NULL);
    PLAY_PS3_TRACE("play-draw: null segment commands emitted");

    PLAY_PS3_TRACE("play-draw: before main keep segment commands");
    gSPSegment(POLY_OPA_DISP++, 0x04, play->objectCtx.status[play->objectCtx.mainKeepIndex].segment);
    gSPSegment(POLY_XLU_DISP++, 0x04, play->objectCtx.status[play->objectCtx.mainKeepIndex].segment);
    gSPSegment(OVERLAY_DISP++, 0x04, play->objectCtx.status[play->objectCtx.mainKeepIndex].segment);

    PLAY_PS3_TRACE("play-draw: before sub keep segment commands");
    gSPSegment(POLY_OPA_DISP++, 0x05, play->objectCtx.status[play->objectCtx.subKeepIndex].segment);
    gSPSegment(POLY_XLU_DISP++, 0x05, play->objectCtx.status[play->objectCtx.subKeepIndex].segment);
    gSPSegment(OVERLAY_DISP++, 0x05, play->objectCtx.status[play->objectCtx.subKeepIndex].segment);

    PLAY_PS3_TRACE("play-draw: before scene segment commands");
    PLAY_PS3_TRACE("play-draw: before opaque scene segment command");
    gSPSegment(POLY_OPA_DISP++, 0x02, play->sceneSegment);
    PLAY_PS3_TRACE("play-draw: before translucent scene segment command");
    gSPSegment(POLY_XLU_DISP++, 0x02, play->sceneSegment);
    PLAY_PS3_TRACE("play-draw: before overlay scene segment command");
    gSPSegment(OVERLAY_DISP++, 0x02, play->sceneSegment);
    PLAY_PS3_TRACE("play-draw: scene segment commands emitted");

    PLAY_PS3_TRACE("play-draw: before Gfx_SetupFrame");
    Gfx_SetupFrame(gfxCtx, 0, 0, 0);
    PLAY_PS3_TRACE("play-draw: after Gfx_SetupFrame");

    if ((HREG(80) != 10) || (HREG(82) != 0)) {
#ifdef __PS3__
        PLAY_PS3_TRACE("play-draw: PS3 skips optional draw-begin hook");
#else
        GameInteractor_ExecuteOnPlayDrawBegin();
#endif

        POLY_OPA_DISP = Play_SetFog(play, POLY_OPA_DISP);
        POLY_XLU_DISP = Play_SetFog(play, POLY_XLU_DISP);

        func_800AA460(&play->view, play->view.fovy, play->view.zNear, play->lightCtx.fogFar);
        func_800AAA50(&play->view, 15);

        // Flip the projections and invert culling for the OPA and XLU display buffers
        // These manage the world and effects when we are not drawing kaleido
        if (R_PAUSE_MENU_MODE <= 1 && CVarGetInteger(CVAR_ENHANCEMENT("MirroredWorld"), 0)) {
            gSPSetExtraGeometryMode(POLY_OPA_DISP++, G_EX_INVERT_CULLING);
            gSPSetExtraGeometryMode(POLY_XLU_DISP++, G_EX_INVERT_CULLING);
            gSPMatrix(POLY_OPA_DISP++, play->view.projectionFlippedPtr, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
            gSPMatrix(POLY_XLU_DISP++, play->view.projectionFlippedPtr, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION);
            gSPMatrix(POLY_OPA_DISP++, play->view.viewingPtr, G_MTX_NOPUSH | G_MTX_MUL | G_MTX_PROJECTION);
            gSPMatrix(POLY_XLU_DISP++, play->view.viewingPtr, G_MTX_NOPUSH | G_MTX_MUL | G_MTX_PROJECTION);
        }

        // The billboard matrix temporarily stores the viewing matrix
        Matrix_MtxToMtxF(&play->view.viewing, &play->billboardMtxF);
        Matrix_MtxToMtxF(&play->view.projection, &play->viewProjectionMtxF);
        Matrix_Mult(&play->viewProjectionMtxF, MTXMODE_NEW);
        // The billboard is still a viewing matrix at this stage
        Matrix_Mult(&play->billboardMtxF, MTXMODE_APPLY);
        Matrix_Get(&play->viewProjectionMtxF);
        play->billboardMtxF.mf[0][3] = play->billboardMtxF.mf[1][3] = play->billboardMtxF.mf[2][3] =
            play->billboardMtxF.mf[3][0] = play->billboardMtxF.mf[3][1] = play->billboardMtxF.mf[3][2] = 0.0f;
        // This transpose is where the viewing matrix is properly converted into a billboard matrix
        Matrix_Transpose(&play->billboardMtxF);
        play->billboardMtx =
            Matrix_MtxFToMtx(MATRIX_CHECKFLOATS(&play->billboardMtxF), Graph_Alloc(gfxCtx, sizeof(Mtx)));

        gSPSegment(POLY_OPA_DISP++, 0x01, play->billboardMtx);

        if ((HREG(80) != 10) || (HREG(92) != 0)) {
            Gfx* gfxP;
            Gfx* sp1CC = POLY_OPA_DISP;

            gfxP = Graph_GfxPlusOne(sp1CC);
            gSPDisplayList(OVERLAY_DISP++, gfxP);
            gSPGrayscale(gfxP++, false);

            if ((play->transitionMode == TRANS_MODE_INSTANCE_RUNNING) ||
                (play->transitionMode == TRANS_MODE_INSTANCE_WAIT) ||
                (play->transitionCtx.transitionType >= TRANS_TYPE_MAX)) {
                View view;

                View_Init(&view, gfxCtx);
                view.flags = 2 | 8;

                SET_FULLSCREEN_VIEWPORT(&view);

                func_800AB9EC(&view, 15, &gfxP);
                play->transitionCtx.draw(&play->transitionCtx.data, &gfxP);
            }

            TransitionFade_Draw(&play->transitionFade, &gfxP);

            if (gVisMonoColor.a > 0) {
                gPlayVisMono.vis.primColor.rgba = gVisMonoColor.rgba;
                VisMono_Draw(&gPlayVisMono, &gfxP);
            }

            gSPEndDisplayList(gfxP++);
            Graph_BranchDlist(sp1CC, gfxP);
            POLY_OPA_DISP = gfxP;
        }

        if (gTrnsnUnkState == 3) {
            Gfx* sp88 = POLY_OPA_DISP;

            TransitionUnk_Draw(&sTrnsnUnk, &sp88);
            POLY_OPA_DISP = sp88;
            goto Play_Draw_DrawOverlayElements;
        }

        PreRender_SetValues(&play->pauseBgPreRender, SCREEN_WIDTH, SCREEN_HEIGHT, gfxCtx->curFrameBuffer, gZBuffer);
        PLAY_PS3_TRACE("play-draw: before pause-mode and skybox dispatch");

        if (R_PAUSE_MENU_MODE == 2) {
            // Wait for the previous frame's display list to be processed,
            // so that `pauseBgPreRender.fbufSave` and `pauseBgPreRender.cvgSave` are filled with the appropriate
            // content and can be used by `PreRender_ApplyFilters` below.
            MsgEvent_SendNullTask();

            PreRender_Calc(&play->pauseBgPreRender);

            R_PAUSE_MENU_MODE = 3;
        } else if (R_PAUSE_MENU_MODE >= 4) {
            R_PAUSE_MENU_MODE = 0;
        }

        if (R_PAUSE_MENU_MODE == 3) {
            Gfx* gfxP = POLY_OPA_DISP;

            // SOH [Port] Draw game framebuffer using our custom handling
            // func_800C24BC(&play->pauseBgPreRender, &gfxP);
            FB_DrawFromFramebuffer(&gfxP, gPauseFrameBuffer, 255);
            POLY_OPA_DISP = gfxP;

            goto Play_Draw_DrawOverlayElements;
        }

        if ((HREG(80) != 10) || (HREG(83) != 0)) {
            if (play->skyboxId && (play->skyboxId != SKYBOX_UNSET_1D) && !play->envCtx.skyboxDisabled) {
                if ((play->skyboxId == SKYBOX_NORMAL_SKY) || (play->skyboxId == SKYBOX_CUTSCENE_MAP)) {
                    PLAY_PS3_TRACE("play-draw: before Environment_UpdateSkybox");
                    Environment_UpdateSkybox(play, play->skyboxId, &play->envCtx, &play->skyboxCtx);
                    SkyboxDraw_Draw(&play->skyboxCtx, gfxCtx, play->skyboxId, play->envCtx.skyboxBlend,
                                    play->view.eye.x, play->view.eye.y, play->view.eye.z);
                } else if (play->skyboxCtx.unk_140 == 0) {
                    SkyboxDraw_Draw(&play->skyboxCtx, gfxCtx, play->skyboxId, 0, play->view.eye.x, play->view.eye.y,
                                    play->view.eye.z);
                }
            }
        }

        PLAY_PS3_TRACE("play-draw: before sun-moon-lightning-lights block");
        if ((HREG(80) != 10) || (HREG(90) & 2)) {
            if (!play->envCtx.sunMoonDisabled) {
                Environment_DrawSunAndMoon(play);
                PLAY_PS3_TRACE("play-draw: after Environment_DrawSunAndMoon");
            }
        }

        if ((HREG(80) != 10) || (HREG(90) & 1)) {
            Environment_DrawSkyboxFilters(play);
            PLAY_PS3_TRACE("play-draw: after Environment_DrawSkyboxFilters");
        }

        if ((HREG(80) != 10) || (HREG(90) & 4)) {
            Environment_UpdateLightningStrike(play);
            PLAY_PS3_TRACE("play-draw: after Environment_UpdateLightningStrike");
            Environment_DrawLightning(play, 0);
            PLAY_PS3_TRACE("play-draw: after Environment_DrawLightning");
        }

        if ((HREG(80) != 10) || (HREG(90) & 8)) {
            sp228 = LightContext_NewLights(&play->lightCtx, gfxCtx);
            Lights_BindAll(sp228, play->lightCtx.listHead, NULL);
            Lights_Draw(sp228, gfxCtx);
            PLAY_PS3_TRACE("play-draw: after Lights_Draw");
        }

        if ((HREG(80) != 10) || (HREG(84) != 0)) {
            if (VREG(94) == 0) {
                s32 roomDrawFlags;

                if (HREG(80) != 10) {
                    roomDrawFlags = 3;
                } else {
                    roomDrawFlags = HREG(84);
                }
#ifdef __PS3__
                if (play->sceneSegment == NULL) {
                    // Hyrule Field's draw config uses the frame counter/time,
                    // not sceneSegment. It must populate segments 8/9/A before
                    // the room lists run; actor rendering reuses those segments.
                    if (play->roomCtx.curRoom.segment != NULL &&
                        play->roomCtx.curRoom.meshHeader != NULL) {
                        if (play->sceneNum == SCENE_HYRULE_FIELD && play->sceneConfig == 1) {
                            PLAY_PS3_TRACE("play-draw: restoring Hyrule Field Scene_Draw before room lists");
                            Scene_Draw(play);
                        }
                        PLAY_PS3_TRACE("play-draw: before upstream Room_Draw of decoded O2R mesh");
                        Room_Draw(play, &play->roomCtx.curRoom, roomDrawFlags & 3);
                        PLAY_PS3_TRACE("play-draw: after upstream Room_Draw of decoded O2R mesh");
                    } else {
                        PLAY_PS3_TRACE("play-draw: scene and room draw deferred until O2R headers load");
                    }
                } else
#endif
                {
                    PLAY_PS3_TRACE("play-draw: before Scene_Draw");
                    Scene_Draw(play);
                    PLAY_PS3_TRACE("play-draw: after Scene_Draw; before Room_Draw");
                    Room_Draw(play, &play->roomCtx.curRoom, roomDrawFlags & 3);
                    Room_Draw(play, &play->roomCtx.prevRoom, roomDrawFlags & 3);
                }
            }
        }

        if ((HREG(80) != 10) || (HREG(83) != 0)) {
            if ((play->skyboxCtx.unk_140 != 0) && (GET_ACTIVE_CAM(play)->setting != CAM_SET_PREREND_FIXED)) {
                Vec3f quakeOffset;

                Camera_GetSkyboxOffset(&quakeOffset, GET_ACTIVE_CAM(play));
                SkyboxDraw_Draw(&play->skyboxCtx, gfxCtx, play->skyboxId, 0, play->view.eye.x + quakeOffset.x,
                                play->view.eye.y + quakeOffset.y, play->view.eye.z + quakeOffset.z);
            }
        }

        if (play->envCtx.unk_EE[1] != 0) {
            Environment_DrawRain(play, &play->view, gfxCtx);
        }

        if ((HREG(80) != 10) || (HREG(84) != 0)) {
            Environment_FillScreen(gfxCtx, 0, 0, 0, play->bgCoverAlpha, FILL_SCREEN_OPA);
        }

        if ((HREG(80) != 10) || (HREG(85) != 0)) {
            PLAY_PS3_TRACE("play-draw: before Actor_DrawAll");
            Actor_DrawAll(play, &play->actorCtx);
            PLAY_PS3_TRACE("play-draw: after Actor_DrawAll");
        }

#ifdef __PS3__
        if (play->sceneSegment == NULL) {
            PLAY_PS3_TRACE("play-draw: scene-dependent environment and overlays deferred; preserving real actor display lists");
            Environment_DrawSunAndMoon(play);
            goto Play_Draw_skip;
        }
#endif
        if ((HREG(80) != 10) || (HREG(86) != 0)) {
            if (!play->envCtx.sunMoonDisabled) {
                sp21C.x = play->view.eye.x + play->envCtx.sunPos.x;
                sp21C.y = play->view.eye.y + play->envCtx.sunPos.y;
                sp21C.z = play->view.eye.z + play->envCtx.sunPos.z;
                Environment_DrawSunLensFlare(play, &play->envCtx, &play->view, gfxCtx, sp21C, 0);
            }
            Environment_DrawCustomLensFlare(play);
        }

        if ((HREG(80) != 10) || (HREG(87) != 0)) {
            if (MREG(64) != 0) {
                Environment_FillScreen(gfxCtx, MREG(65), MREG(66), MREG(67), MREG(68),
                                       FILL_SCREEN_OPA | FILL_SCREEN_XLU);
            }

            switch (play->envCtx.fillScreen) {
                case 1:
                    Environment_FillScreen(gfxCtx, play->envCtx.screenFillColor[0], play->envCtx.screenFillColor[1],
                                           play->envCtx.screenFillColor[2], play->envCtx.screenFillColor[3],
                                           FILL_SCREEN_OPA | FILL_SCREEN_XLU);
                    break;
                default:
                    break;
            }
        }

        if ((HREG(80) != 10) || (HREG(88) != 0)) {
            if (play->envCtx.sandstormState != SANDSTORM_OFF) {
                Environment_DrawSandstorm(play, play->envCtx.sandstormState);
            }
        }

        if ((HREG(80) != 10) || (HREG(93) != 0)) {
            DebugDisplay_DrawObjects(play);
        }

        if ((R_PAUSE_MENU_MODE == 1) || (gTrnsnUnkState == 1)) {
            Gfx* gfxP = OVERLAY_DISP;

            // Copy the frame buffer contents at this point in the display list to the zbuffer
            // The zbuffer must then stay untouched until unpausing
            play->pauseBgPreRender.fbuf = gfxCtx->curFrameBuffer;
            play->pauseBgPreRender.fbufSave = (u16*)gZBuffer;
            // SOH [Port] Use our custom copy method instead of the prerender system
            // func_800C1F20(&play->pauseBgPreRender, &gfxP);
            if (R_PAUSE_MENU_MODE == 1) {
                play->pauseBgPreRender.cvgSave = (u8*)gfxCtx->curFrameBuffer;
                // func_800C20B4(&play->pauseBgPreRender, &gfxP);
                R_PAUSE_MENU_MODE = 2;

                // #region SOH [Port] Custom handling for pause prerender background capture
                lastPauseWidth = OTRGetGameRenderWidth();
                lastPauseHeight = OTRGetGameRenderHeight();
                lastAltAssets = ResourceMgr_IsAltAssetsEnabled();
                hasCapturedPauseBuffer = false;

                FB_CopyToFramebuffer(&gfxP, 0, gPauseFrameBuffer, false, &hasCapturedPauseBuffer);

                // Set the state back to ready after the recapture is done
                if (recapturePauseBuffer) {
                    R_PAUSE_MENU_MODE = 3;
                }
                // #endregion
            } else {
                gTrnsnUnkState = 2;
            }
            OVERLAY_DISP = gfxP;
            play->unk_121C7 = 2;
            SREG(33) |= 1;

            // SOH [Port] Continue to render the post world for pausing to avoid flashing the HUD
            if (gTrnsnUnkState == 2) {
                goto Play_Draw_skip;
            }
        }

        // Draw Enhancements that need to be placed in the world. This happens before the PostWorldDraw
        // so that they aren't drawn when the pause menu is up (e.g. collision viewer, actor name tags)
#ifdef __PS3__
        PLAY_PS3_TRACE("play-draw: PS3 skips optional draw-end hook");
#else
        GameInteractor_ExecuteOnPlayDrawEnd();
#endif

    Play_Draw_DrawOverlayElements:
        if ((HREG(80) != 10) || (HREG(89) != 0)) {
            Play_DrawOverlayElements(play);
        }

        // Reset the inverted culling
        if (CVarGetInteger(CVAR_ENHANCEMENT("MirroredWorld"), 0)) {
            gSPClearExtraGeometryMode(POLY_OPA_DISP++, G_EX_INVERT_CULLING);
            gSPClearExtraGeometryMode(POLY_XLU_DISP++, G_EX_INVERT_CULLING);
        }
    }

Play_Draw_skip:

    if (play->view.unk_124 != 0) {
        Camera_Update(GET_ACTIVE_CAM(play));
        func_800AB944(&play->view);
        play->view.unk_124 = 0;
        if (play->skyboxId && (play->skyboxId != SKYBOX_UNSET_1D) && !play->envCtx.skyboxDisabled) {
            SkyboxDraw_UpdateMatrix(&play->skyboxCtx, play->view.eye.x, play->view.eye.y, play->view.eye.z);
        }
    }

    PLAY_PS3_TRACE("play-draw: before camera finish");
    Camera_Finish(GET_ACTIVE_CAM(play));
    PLAY_PS3_TRACE("play-draw: after camera finish");

    PLAY_PS3_TRACE("play-draw: before closing frame display lists");
    CLOSE_DISPS(gfxCtx);
    PLAY_PS3_TRACE("play-draw: after closing frame display lists");

    PLAY_PS3_TRACE("play-draw: before gameplay timer draw");
#ifdef __PS3__
    if (play->sceneSegment == NULL) {
        PLAY_PS3_TRACE("play-draw: gameplay timer deferred until first scene loads");
    } else
#endif
    Interface_DrawTotalGameplayTimer(play);
    PLAY_PS3_TRACE("play-draw: completed");
}

time_t Play_GetRealTime() {
    time_t t1, t2;
    struct tm* tms;
    time(&t1);
    tms = localtime(&t1);
    tms->tm_hour = 0;
    tms->tm_min = 0;
    tms->tm_sec = 0;
    t2 = mktime(tms);
    return t1 - t2;
}

void Play_Main(GameState* thisx) {
    PlayState* play = (PlayState*)thisx;

#ifdef __PS3__
    PLAY_PS3_TRACE("play-main: entered; PS3 skips optional snow enhancement");
#else
    if (play->envCtx.unk_EE[2] == 0 && CVarGetInteger(CVAR_GENERAL("LetItSnow"), 0)) {
        play->envCtx.unk_EE[3] = 64;
        Actor_Spawn(&gPlayState->actorCtx, gPlayState, ACTOR_OBJECT_KANKYO, 0, 0, 0, 0, 0, 0, 3);
    }
#endif

    D_8012D1F8 = &play->state.input[0];

    DebugDisplay_Init();

    PLAY_LOG(4556);

    if ((HREG(80) == 10) && (HREG(94) != 10)) {
        HREG(81) = 1;
        HREG(82) = 1;
        HREG(83) = 1;
        HREG(84) = 3;
        HREG(85) = 1;
        HREG(86) = 1;
        HREG(87) = 1;
        HREG(88) = 1;
        HREG(89) = 1;
        HREG(90) = 15;
        HREG(91) = 1;
        HREG(92) = 1;
        HREG(93) = 1;
        HREG(94) = 10;
    }

    if ((HREG(80) != 10) || (HREG(81) != 0)) {
        PLAY_PS3_TRACE("play-main: before Play_Update");
        Play_Update(play);
        PLAY_PS3_TRACE("play-main: after Play_Update");
    }

    PLAY_LOG(4583);

    PLAY_PS3_TRACE("play-main: before frame interpolation");
    FrameInterpolation_StartRecord();
    PLAY_PS3_TRACE("play-main: before Play_Draw");
    Play_Draw(play);
    PLAY_PS3_TRACE("play-main: after Play_Draw");
    FrameInterpolation_StopRecord();

    PLAY_LOG(4587);

#ifndef __PS3__
    if (CVarGetInteger(CVAR_CHEAT("TimeSync"), 0)) {
        const int maxRealDaySeconds = 86400;
        const int maxInGameDayTicks = 65536;

        int secs = (int)Play_GetRealTime();
        float percent = (float)secs / (float)maxRealDaySeconds;

        int newIngameTime = maxInGameDayTicks * percent;

        gSaveContext.dayTime = newIngameTime;
    }
#endif
    PLAY_PS3_TRACE("play-main: completed");
}

u8 PlayerGrounded(Player* player) {
    return player->actor.bgCheckFlags & 1;
}

// original name: "Game_play_demo_mode_check"
s32 Play_InCsMode(PlayState* play) {
    return (play->csCtx.state != CS_STATE_IDLE) || Player_InCsMode(play);
}

f32 func_800BFCB8(PlayState* play, MtxF* mf, Vec3f* pos) {
    CollisionPoly poly;
    f32 temp1;
    f32 temp2;
    f32 temp3;
    f32 floorY = BgCheck_AnyRaycastFloor1(&play->colCtx, &poly, pos);

    if (floorY > BGCHECK_Y_MIN) {
        f32 nx = COLPOLY_GET_NORMAL(poly.normal.x);
        f32 ny = COLPOLY_GET_NORMAL(poly.normal.y);
        f32 nz = COLPOLY_GET_NORMAL(poly.normal.z);
        s32 pad[5];

        temp1 = sqrtf(1.0f - SQ(nx));

        if (temp1 != 0.0f) {
            temp2 = ny * temp1;
            temp3 = -nz * temp1;
        } else {
            temp3 = 0.0f;
            temp2 = 0.0f;
        }

        mf->xx = temp1;
        mf->yx = -nx * temp2;
        mf->zx = nx * temp3;
        mf->xy = nx;
        mf->yy = ny;
        mf->zy = nz;
        mf->yz = temp3;
        mf->zz = temp2;
        mf->wx = 0.0f;
        mf->wy = 0.0f;
        mf->xz = 0.0f;
        mf->wz = 0.0f;
        mf->xw = pos->x;
        mf->yw = floorY;
        mf->zw = pos->z;
        mf->ww = 1.0f;
    } else {
        mf->xy = 0.0f;
        mf->zx = 0.0f;
        mf->yx = 0.0f;
        mf->xx = 0.0f;
        mf->wz = 0.0f;
        mf->xz = 0.0f;
        mf->wy = 0.0f;
        mf->wx = 0.0f;
        mf->zz = 0.0f;
        mf->yz = 0.0f;
        mf->zy = 0.0f;
        mf->yy = 1.0f;
        mf->xw = pos->x;
        mf->yw = pos->y;
        mf->zw = pos->z;
        mf->ww = 1.0f;
    }

    return floorY;
}

void* Play_LoadFile(PlayState* play, RomFile* file) {
    size_t size;
    void* allocp;

    size = file->vromEnd - file->vromStart;
    allocp = GAMESTATE_ALLOC_MC(&play->state, size);
    DmaMgr_SendRequest1(allocp, file->vromStart, size, __FILE__, __LINE__);

    return allocp;
}

void Play_InitEnvironment(PlayState* play, s16 skyboxId) {
    Skybox_Init(&play->state, &play->skyboxCtx, skyboxId);
    Environment_Init(play, &play->envCtx, 0);
}

void Play_InitScene(PlayState* play, s32 spawn) {
    play->curSpawn = spawn;

    play->linkActorEntry = NULL;
    play->unk_11DFC = NULL;
    play->setupEntranceList = NULL;
    play->setupExitList = NULL;
    play->cUpElfMsgs = NULL;
    play->setupPathList = NULL;

    play->numSetupActors = 0;

    Object_InitBank(play, &play->objectCtx);
    LightContext_Init(play, &play->lightCtx);
    TransitionActor_InitContext(&play->state, &play->transiActorCtx);
    func_80096FD4(play, &play->roomCtx.curRoom);
    YREG(15) = 0;
    gSaveContext.worldMapArea = 0;
    Scene_ExecuteCommands(play, play->sceneSegment);
    Play_InitEnvironment(play, play->skyboxId);
}

void Play_SpawnScene(PlayState* play, s32 sceneId, s32 spawn) {
    uint8_t mqMode = CVarGetInteger(CVAR_GENERAL("BetterDebugWarpScreenMQMode"), WARP_MODE_OVERRIDE_OFF);
    int16_t mqModeScene = CVarGetInteger(CVAR_GENERAL("BetterDebugWarpScreenMQModeScene"), -1);
    if (mqMode != WARP_MODE_OVERRIDE_OFF && sceneId != mqModeScene) {
        CVarClear(CVAR_GENERAL("BetterDebugWarpScreenMQMode"));
        CVarClear(CVAR_GENERAL("BetterDebugWarpScreenMQModeScene"));
    }

    OTRPlay_SpawnScene(play, sceneId, spawn);
}

void func_800C016C(PlayState* play, Vec3f* src, Vec3f* dest) {
    f32 w;

    Matrix_Mult(&play->viewProjectionMtxF, MTXMODE_NEW);
    Matrix_MultVec3f(src, dest);

    w = play->viewProjectionMtxF.ww + (play->viewProjectionMtxF.wx * src->x + play->viewProjectionMtxF.wy * src->y +
                                       play->viewProjectionMtxF.wz * src->z);

    dest->x = (SCREEN_WIDTH / 2) + ((dest->x / w) * (SCREEN_WIDTH / 2));
    dest->y = (SCREEN_HEIGHT / 2) - ((dest->y / w) * (SCREEN_HEIGHT / 2));
}

s16 Play_CreateSubCamera(PlayState* play) {
    s16 i;

    for (i = CAM_ID_SUB_FIRST; i < NUM_CAMS; i++) {
        if (play->cameraPtrs[i] == NULL) {
            break;
        }
    }

    if (i == NUM_CAMS) {
        osSyncPrintf(VT_COL(RED, WHITE) "camera control: error: fulled sub camera system area\n" VT_RST);
        return SUBCAM_NONE;
    }

    osSyncPrintf("camera control: " VT_BGCOL(CYAN) " " VT_COL(WHITE, BLUE) " create new sub camera [%d] " VT_BGCOL(
                     CYAN) " " VT_RST "\n",
                 i);

    play->cameraPtrs[i] = &play->subCameras[i - CAM_ID_SUB_FIRST];
    Camera_Init(play->cameraPtrs[i], &play->view, &play->colCtx, play);
    play->cameraPtrs[i]->thisIdx = i;

    return i;
}

s16 Play_GetActiveCamId(PlayState* play) {
    return play->activeCamera;
}

s16 Play_ChangeCameraStatus(PlayState* play, s16 camId, s16 status) {
    s16 camIdx = (camId == SUBCAM_ACTIVE) ? play->activeCamera : camId;

    if (status == CAM_STAT_ACTIVE) {
        play->activeCamera = camIdx;
    }

    return Camera_ChangeStatus(play->cameraPtrs[camIdx], status);
}

void Play_ClearCamera(PlayState* play, s16 camId) {
    s16 camIdx = (camId == SUBCAM_ACTIVE) ? play->activeCamera : camId;

    if (camIdx == CAM_ID_MAIN) {
        osSyncPrintf(VT_COL(RED, WHITE) "camera control: error: never clear camera !!\n" VT_RST);
    }

    if (play->cameraPtrs[camIdx] != NULL) {
        Camera_ChangeStatus(play->cameraPtrs[camIdx], CAM_STAT_UNK100);
        play->cameraPtrs[camIdx] = NULL;
        osSyncPrintf("camera control: " VT_BGCOL(CYAN) " " VT_COL(WHITE, BLUE) " clear sub camera [%d] " VT_BGCOL(
                         CYAN) " " VT_RST "\n",
                     camIdx);
    } else {
        osSyncPrintf(VT_COL(RED, WHITE) "camera control: error: camera No.%d already cleared\n" VT_RST, camIdx);
    }
}

void Play_ClearAllSubCameras(PlayState* play) {
    s16 i;

    for (i = CAM_ID_SUB_FIRST; i < NUM_CAMS; i++) {
        if (play->cameraPtrs[i] != NULL) {
            Play_ClearCamera(play, i);
        }
    }

    play->activeCamera = CAM_ID_MAIN;
}

Camera* Play_GetCamera(PlayState* play, s16 camId) {
    s16 camIdx = (camId == SUBCAM_ACTIVE) ? play->activeCamera : camId;

    return play->cameraPtrs[camIdx];
}

s32 Play_CameraSetAtEye(PlayState* play, s16 camId, Vec3f* at, Vec3f* eye) {
    s32 ret = 0;
    s16 camIdx = (camId == SUBCAM_ACTIVE) ? play->activeCamera : camId;
    Camera* camera = play->cameraPtrs[camIdx];
    Player* player;

    ret |= Camera_SetParam(camera, 1, at);
    ret <<= 1;
    ret |= Camera_SetParam(camera, 2, eye);

    camera->dist = Math3D_Vec3f_DistXYZ(at, eye);

    player = camera->player;
    if (player != NULL) {
        camera->posOffset.x = at->x - player->actor.world.pos.x;
        camera->posOffset.y = at->y - player->actor.world.pos.y;
        camera->posOffset.z = at->z - player->actor.world.pos.z;
    } else {
        camera->posOffset.x = camera->posOffset.y = camera->posOffset.z = 0.0f;
    }

    camera->atLERPStepScale = 0.01f;

    return ret;
}

s32 Play_CameraSetAtEyeUp(PlayState* play, s16 camId, Vec3f* at, Vec3f* eye, Vec3f* up) {
    s32 ret = 0;
    s16 camIdx = (camId == SUBCAM_ACTIVE) ? play->activeCamera : camId;
    Camera* camera = play->cameraPtrs[camIdx];
    Player* player;

    ret |= Camera_SetParam(camera, 1, at);
    ret <<= 1;
    ret |= Camera_SetParam(camera, 2, eye);
    ret <<= 1;
    ret |= Camera_SetParam(camera, 4, up);

    camera->dist = Math3D_Vec3f_DistXYZ(at, eye);

    player = camera->player;
    if (player != NULL) {
        camera->posOffset.x = at->x - player->actor.world.pos.x;
        camera->posOffset.y = at->y - player->actor.world.pos.y;
        camera->posOffset.z = at->z - player->actor.world.pos.z;
    } else {
        camera->posOffset.x = camera->posOffset.y = camera->posOffset.z = 0.0f;
    }

    camera->atLERPStepScale = 0.01f;

    return ret;
}

s32 Play_CameraSetFov(PlayState* play, s16 camId, f32 fov) {
    s32 ret = Camera_SetParam(play->cameraPtrs[camId], 0x20, &fov) & 1;

    return ret;
}

s32 Play_SetCameraRoll(PlayState* play, s16 camId, s16 roll) {
    s16 camIdx = (camId == SUBCAM_ACTIVE) ? play->activeCamera : camId;
    Camera* camera = play->cameraPtrs[camIdx];

    camera->roll = roll;

    return 1;
}

void Play_CopyCamera(PlayState* play, s16 camId1, s16 camId2) {
    s16 camIdx2 = (camId2 == SUBCAM_ACTIVE) ? play->activeCamera : camId2;
    s16 camIdx1 = (camId1 == SUBCAM_ACTIVE) ? play->activeCamera : camId1;

    Camera_Copy(play->cameraPtrs[camIdx1], play->cameraPtrs[camIdx2]);
}

s32 func_800C0808(PlayState* play, s16 camId, Player* player, s16 setting) {
    Camera* camera;
    s16 camIdx = (camId == SUBCAM_ACTIVE) ? play->activeCamera : camId;

    camera = play->cameraPtrs[camIdx];
    Camera_InitPlayerSettings(camera, player);
    return Camera_RequestSetting(camera, setting);
}

s32 Play_CameraChangeSetting(PlayState* play, s16 camId, s16 setting) {
    return Camera_RequestSetting(Play_GetCamera(play, camId), setting);
}

void func_800C08AC(PlayState* play, s16 camId, s16 arg2) {
    s16 camIdx = (camId == SUBCAM_ACTIVE) ? play->activeCamera : camId;
    s16 i;

    Play_ClearCamera(play, camIdx);

    for (i = CAM_ID_SUB_FIRST; i < NUM_CAMS; i++) {
        if (play->cameraPtrs[i] != NULL) {
            osSyncPrintf(
                VT_COL(RED, WHITE) "camera control: error: return to main, other camera left. %d cleared!!\n" VT_RST,
                i);
            Play_ClearCamera(play, i);
        }
    }

    if (arg2 <= 0) {
        Play_ChangeCameraStatus(play, CAM_ID_MAIN, CAM_STAT_ACTIVE);
        play->cameraPtrs[CAM_ID_MAIN]->childCamIdx = play->cameraPtrs[CAM_ID_MAIN]->parentCamIdx = SUBCAM_FREE;
    } else {
        OnePointCutscene_Init(play, 1020, arg2, NULL, CAM_ID_MAIN);
    }
}

s16 Play_CameraGetUID(PlayState* play, s16 camId) {
    Camera* camera = play->cameraPtrs[camId];

    if (camera != NULL) {
        return camera->uid;
    } else {
        return -1;
    }
}

s16 func_800C09D8(PlayState* play, s16 camId, s16 arg2) {
    Camera* camera = play->cameraPtrs[camId];

    if (camera != NULL) {
        return 0;
    } else if (camera->uid != arg2) {
        return 0;
    } else if (camera->status != CAM_STAT_ACTIVE) {
        return 2;
    } else {
        return 1;
    }
}

void Play_SaveSceneFlags(PlayState* play) {
    SavedSceneFlags* savedSceneFlags = &gSaveContext.sceneFlags[play->sceneNum];

    savedSceneFlags->chest = play->actorCtx.flags.chest;
    savedSceneFlags->swch = play->actorCtx.flags.swch;
    savedSceneFlags->clear = play->actorCtx.flags.clear;
    savedSceneFlags->collect = play->actorCtx.flags.collect;
}

void Play_SetRespawnData(PlayState* play, s32 respawnMode, s16 entranceIndex, s32 roomIndex, s32 playerParams,
                         Vec3f* pos, s16 yaw) {
    RespawnData* respawnData = &gSaveContext.respawn[respawnMode];

    respawnData->entranceIndex = entranceIndex;
    respawnData->roomIndex = roomIndex;
    respawnData->pos = *pos;
    respawnData->yaw = yaw;
    respawnData->playerParams = playerParams;
    respawnData->tempSwchFlags = play->actorCtx.flags.tempSwch;
    respawnData->tempCollectFlags = play->actorCtx.flags.tempCollect;
}

void Play_SetupRespawnPoint(PlayState* play, s32 respawnMode, s32 playerParams) {
    Player* player = GET_PLAYER(play);
    s32 entranceIndex;
    s8 roomIndex;

    if ((play->sceneNum != SCENE_FAIRYS_FOUNTAIN) && (play->sceneNum != SCENE_GROTTOS)) {
        roomIndex = play->roomCtx.curRoom.num;
        entranceIndex = gSaveContext.entranceIndex;
        Play_SetRespawnData(play, respawnMode, entranceIndex, roomIndex, playerParams, &player->actor.world.pos,
                            player->actor.shape.rot.y);
    }
}

void Play_TriggerVoidOut(PlayState* play) {
    gSaveContext.respawn[RESPAWN_MODE_DOWN].tempSwchFlags = play->actorCtx.flags.tempSwch;
    gSaveContext.respawn[RESPAWN_MODE_DOWN].tempCollectFlags = play->actorCtx.flags.tempCollect;
    gSaveContext.respawnFlag = 1;
    play->transitionTrigger = TRANS_TRIGGER_START;
    play->nextEntranceIndex = gSaveContext.respawn[RESPAWN_MODE_DOWN].entranceIndex;
    play->transitionType = TRANS_TYPE_FADE_BLACK;
}

void Play_LoadToLastEntrance(PlayState* play) {
    gSaveContext.respawnFlag = -1;
    play->transitionTrigger = TRANS_TRIGGER_START;

    if ((play->sceneNum == SCENE_GANONS_TOWER_COLLAPSE_INTERIOR) ||
        (play->sceneNum == SCENE_GANONS_TOWER_COLLAPSE_EXTERIOR) ||
        (play->sceneNum == SCENE_INSIDE_GANONS_CASTLE_COLLAPSE) || (play->sceneNum == SCENE_GANON_BOSS)) {
        play->nextEntranceIndex = ENTR_GANONS_TOWER_COLLAPSE_EXTERIOR_0;
        Item_Give(play, ITEM_SWORD_MASTER);
    } else if ((gSaveContext.entranceIndex == ENTR_HYRULE_FIELD_11) ||
               (gSaveContext.entranceIndex == ENTR_HYRULE_FIELD_12) ||
               (gSaveContext.entranceIndex == ENTR_HYRULE_FIELD_13) ||
               (gSaveContext.entranceIndex == ENTR_HYRULE_FIELD_15)) {
        play->nextEntranceIndex = ENTR_HYRULE_FIELD_CENTER_EXIT;
    } else {
        play->nextEntranceIndex = gSaveContext.entranceIndex;
    }

    play->transitionType = TRANS_TYPE_FADE_BLACK;
}

void Play_TriggerRespawn(PlayState* play) {
    Play_SetupRespawnPoint(play, RESPAWN_MODE_DOWN, 0xDFF);
    Play_LoadToLastEntrance(play);
}

s32 Play_CamIsNotFixed(PlayState* play) {
#ifdef __PS3__
    if (play->sceneSegment == NULL || play->roomCtx.curRoom.meshHeader == NULL) {
        PLAY_PS3_TRACE("play-camera: scene commands absent; deferring horse camera setting");
        return 0;
    }
#endif
    return (play->roomCtx.curRoom.meshHeader->base.type != 1) && (YREG(15) != 0x20) && (YREG(15) != 0x30) &&
           (YREG(15) != 0x40) && (play->sceneNum != SCENE_CASTLE_COURTYARD_GUARDS_DAY);
}

s32 FrameAdvance_IsEnabled(PlayState* play) {
    return !!play->frameAdvCtx.enabled;
}

s32 func_800C0D34(PlayState* play, Actor* actor, s16* yaw) {
    TransitionActorEntry* transitionActor;
    s32 frontRoom;

    if (actor->category != ACTORCAT_DOOR) {
        return 0;
    }

    transitionActor = &play->transiActorCtx.list[(u16)actor->params >> 10];
    frontRoom = transitionActor->sides[0].room;

    if (frontRoom == transitionActor->sides[1].room) {
        return 0;
    }

    if (frontRoom == actor->room) {
        *yaw = actor->shape.rot.y;
    } else {
        *yaw = actor->shape.rot.y + 0x8000;
    }

    return 1;
}

s32 func_800C0DB4(PlayState* play, Vec3f* pos) {
    WaterBox* waterBox;
    CollisionPoly* poly;
    Vec3f waterSurfacePos;
    s32 bgId;

    waterSurfacePos = *pos;

    if (WaterBox_GetSurface1(play, &play->colCtx, waterSurfacePos.x, waterSurfacePos.z, &waterSurfacePos.y,
                             &waterBox) == true &&
        pos->y < waterSurfacePos.y &&
        BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &waterSurfacePos) != BGCHECK_Y_MIN) {
        return true;
    } else {
        return false;
    }
}

void Play_PerformSave(PlayState* play) {
    if (play != NULL && gSaveContext.fileNum != 0xFF) {
        Play_SaveSceneFlags(play);
        gSaveContext.savedSceneNum = play->sceneNum;

        // Track values from temp B
        uint8_t prevB = gSaveContext.equips.buttonItems[0];
        uint8_t prevStatus = gSaveContext.buttonStatus[0];

        // Replicate the B button restore from minigames/epona that kaleido does
        if (gSaveContext.equips.buttonItems[0] == ITEM_SLINGSHOT || gSaveContext.equips.buttonItems[0] == ITEM_BOW ||
            gSaveContext.equips.buttonItems[0] == ITEM_BOMBCHU ||
            gSaveContext.equips.buttonItems[0] == ITEM_FISHING_POLE ||
            (gSaveContext.equips.buttonItems[0] == ITEM_NONE && !Flags_GetInfTable(INFTABLE_SWORDLESS))) {

            gSaveContext.equips.buttonItems[0] = gSaveContext.buttonStatus[0];
            GameInteractor_Should(VB_TEMP_B_RESTORE_SWORDLESS, true);
        }

        Save_SaveFile();

        // Restore temp B values back
        gSaveContext.equips.buttonItems[0] = prevB;
        gSaveContext.buttonStatus[0] = prevStatus;
    }
}
