// SPDX-License-Identifier: GPL-3.0-or-later
/* Game-side helpers for the debug menu (ps3_debug_overlay.c): current scene/room/
 * position text, warping to an area, and the "mark this moment" snapshot written to
 * the trace. Compiled with the game headers. */

#include "global.h"

void oot_ps3_early_trace(const char* message);

#include "ps3_warp_list.inc"

/* gPlayState is not cleared on PS3 (Play_Destroy is skipped), so check the state. */
static PlayState* Ps3DebugPlay(void) {
    if (gGameState == NULL || gGameState->main != Play_Main || gPlayState == NULL) return NULL;
    if ((void*)gPlayState != (void*)gGameState) return NULL;
    return gPlayState;
}

int oot_ps3_debug_game_info(char* line1, char* line2, unsigned int size) {
    PlayState* play = Ps3DebugPlay();
    const unsigned int minutes = (unsigned int)gSaveContext.dayTime * 1440U / 0x10000U;
    if (play == NULL) {
        snprintf(line1, size, "not in gameplay (title / file select)");
        snprintf(line2, size, "%s", "");
        return 0;
    }
    {
        Player* link = GET_PLAYER(play);
        snprintf(line1, size, "scene %d room %d entr %04X actors %d", (int)play->sceneNum,
                 (int)play->roomCtx.curRoom.num, (unsigned)gSaveContext.entranceIndex, (int)play->actorCtx.total);
        snprintf(line2, size, "Link (%.0f,%.0f,%.0f)  time %02u:%02u  cs %d", link ? (double)link->actor.world.pos.x : 0.0,
                 link ? (double)link->actor.world.pos.y : 0.0, link ? (double)link->actor.world.pos.z : 0.0,
                 minutes / 60U, minutes % 60U, (int)play->csCtx.frames);
    }
    return 1;
}

int oot_ps3_debug_warp_count(void) {
    return (int)ARRAY_COUNT(kPs3WarpList);
}

const char* oot_ps3_debug_warp_name(int index) {
    if (index < 0 || index >= (int)ARRAY_COUNT(kPs3WarpList)) return "";
    return kPs3WarpList[index].name;
}

/* ---- Warps and age switch (debug menu) ----
 * A request is kept until the game can take it (no transition running, not paused),
 * then everything left over from the current situation is cleared so the new area
 * loads plain and playable: no cutscene (the new-game intro keeps cutsceneIndex
 * 0xFFFx, which forced the Deku Tree/Navi intro), no pending cutscene/respawn, all
 * buttons enabled, HUD visible, no forced music. Entrance cutscenes of the target
 * entrance are marked as seen. */
enum { PS3_REQ_NONE, PS3_REQ_WARP, PS3_REQ_AGE };
static int sPs3Request, sPs3RequestEntrance, sPs3RequestFrames;
static char sPs3RequestName[64];

static void Ps3ClearForWarp(PlayState* play) {
    s32 i;
    gSaveContext.cutsceneIndex = 0;
    gSaveContext.nextCutsceneIndex = 0xFFEF;
    gSaveContext.cutsceneTrigger = 0;
    gSaveContext.respawnFlag = 0;
    gSaveContext.forcedSeqId = NA_BGM_GENERAL_SFX;
    gSaveContext.nextDayTime = 0xFFFF;
    for (i = 0; i < ARRAY_COUNT(gSaveContext.buttonStatus); i++) gSaveContext.buttonStatus[i] = BTN_ENABLED;
    gSaveContext.forceRisingButtonAlphas = 0;
    gSaveContext.hudVisibilityMode = gSaveContext.nextHudVisibilityMode = HUD_VISIBILITY_ALL;
    gSaveContext.hudVisibilityModeTimer = 0;
    play->csCtx.state = CS_STATE_IDLE; /* a running cutscene must not send its own transition */
    play->csCtx.frames = 0;
}

static void Ps3StartTransition(PlayState* play, s16 entrance) {
    extern void Cutscene_Ps3MarkEntranceCutscenesSeen(s16 entrance, s32 age); /* z_demo.c */
    Cutscene_Ps3MarkEntranceCutscenesSeen(entrance, play->linkAgeOnLoad);
    play->nextEntranceIndex = entrance;
    play->transitionTrigger = TRANS_TRIGGER_START;
    play->transitionType = TRANS_TYPE_INSTANT;
    gSaveContext.nextTransitionType = TRANS_TYPE_FADE_BLACK_FAST;
}

static int Ps3QueueRequest(int type, int entrance, const char* name) {
    char line[128];
    if (Ps3DebugPlay() == NULL) return 0;
    sPs3Request = type;
    sPs3RequestEntrance = entrance;
    sPs3RequestFrames = 0;
    snprintf(sPs3RequestName, sizeof(sPs3RequestName), "%s", name);
    snprintf(line, sizeof(line), "trace: debug menu %s requested (%s)", type == PS3_REQ_AGE ? "age switch" : "warp", name);
    oot_ps3_early_trace(line);
    return 1;
}

int oot_ps3_debug_warp(int index) {
    if (index < 0 || index >= (int)ARRAY_COUNT(kPs3WarpList)) return 0;
    return Ps3QueueRequest(PS3_REQ_WARP, kPs3WarpList[index].entrance, kPs3WarpList[index].name);
}

int oot_ps3_debug_switch_age(void) {
    return Ps3QueueRequest(PS3_REQ_AGE, 0, LINK_IS_ADULT ? "to child" : "to adult");
}

int oot_ps3_debug_link_is_adult(void) {
    return LINK_IS_ADULT ? 1 : 0;
}

static void Ps3ApplyRequest(PlayState* play) {
    char line[160];
    if (sPs3Request == PS3_REQ_NONE) return;
    /* Wait (up to ~10 s) for a running transition or the pause menu to finish. */
    if (play->transitionTrigger != TRANS_TRIGGER_OFF || play->transitionMode != TRANS_MODE_OFF ||
        play->pauseCtx.state != 0 || play->pauseCtx.debugState != 0) {
        if (++sPs3RequestFrames > 200) {
            snprintf(line, sizeof(line), "trace: debug menu request dropped (transition=%d/%d pause=%d)",
                     (int)play->transitionTrigger, (int)play->transitionMode, (int)play->pauseCtx.state);
            oot_ps3_early_trace(line);
            sPs3Request = PS3_REQ_NONE;
        }
        return;
    }
    if (gSaveContext.gameMode != GAMEMODE_NORMAL) {
        oot_ps3_early_trace("trace: debug menu request ignored: no save file loaded (title demo)");
        sPs3Request = PS3_REQ_NONE;
        return;
    }
    Ps3ClearForWarp(play);
    if (sPs3Request == PS3_REQ_AGE) {
        s16 entrance = gSaveContext.entranceIndex;
        /* SoH SwitchAge: these two scenes are different areas for each age. */
        if (play->sceneNum == SCENE_HYRULE_CASTLE || play->sceneNum == SCENE_OUTSIDE_GANONS_CASTLE)
            entrance = ENTR_CASTLE_GROUNDS_SOUTH_EXIT;
        play->linkAgeOnLoad ^= 1; /* graph.c swaps the equipment when the area is released */
        Ps3StartTransition(play, entrance);
        snprintf(line, sizeof(line), "trace: debug menu age switch -> %s at entrance %04X",
                 play->linkAgeOnLoad == LINK_AGE_ADULT ? "adult" : "child", (unsigned)(u16)entrance);
    } else {
        Ps3StartTransition(play, (s16)sPs3RequestEntrance);
        snprintf(line, sizeof(line), "trace: debug menu warp to %s (entrance %04X) from scene %d",
                 sPs3RequestName, (unsigned)(u16)sPs3RequestEntrance, (int)play->sceneNum);
    }
    oot_ps3_early_trace(line);
    sPs3Request = PS3_REQ_NONE;
}

/* Game state in the log every ~10 s and on every area change: enough to know where a
 * player was and what was going on when a log ends in a crash. */
static void Ps3LogState(PlayState* play, const char* why) {
    char line[256];
    Player* link = GET_PLAYER(play);
    const unsigned int minutes = (unsigned int)gSaveContext.dayTime * 1440U / 0x10000U;
    extern unsigned int oot_ps3_heap_used_kb(void);
    snprintf(line, sizeof(line),
             "graph: Play state (%s) scene=%d room=%d entrance=%04X layer=%d age=%s pos=(%.0f,%.0f,%.0f) "
             "hp=%d/%d time=%02u:%02u cs=%d/%04X msg=%d pause=%d trans=%d/%d actors=%d heap=%uKB "
             "flags1=%08X",
             why, (int)play->sceneNum, (int)play->roomCtx.curRoom.num, (unsigned)(u16)gSaveContext.entranceIndex,
             (int)gSaveContext.sceneLayer, LINK_IS_ADULT ? "adult" : "child",
             link ? (double)link->actor.world.pos.x : 0.0, link ? (double)link->actor.world.pos.y : 0.0,
             link ? (double)link->actor.world.pos.z : 0.0, (int)gSaveContext.health, (int)gSaveContext.healthCapacity,
             minutes / 60U, minutes % 60U, (int)play->csCtx.state, (unsigned)(u16)gSaveContext.cutsceneIndex,
             (int)play->msgCtx.msgMode, (int)play->pauseCtx.state, (int)play->transitionTrigger,
             (int)play->transitionMode, (int)play->actorCtx.total, oot_ps3_heap_used_kb(),
             link ? (unsigned)link->stateFlags1 : 0U);
    oot_ps3_early_trace(line);
}

void oot_ps3_debug_game_tick(void) {
    static void* sLastPlay;
    static s16 sLastScene = -1;
    static unsigned int sUpdates;
    PlayState* play = Ps3DebugPlay();
    if (play == NULL) return;
    Ps3ApplyRequest(play);
    ++sUpdates;
    if ((void*)play != sLastPlay || play->sceneNum != sLastScene) {
        sLastPlay = play;
        sLastScene = play->sceneNum;
        sUpdates = 0;
        Ps3LogState(play, "area start");
    } else if (sUpdates % 200U == 0U) { /* 200 updates = 10 s */
        Ps3LogState(play, "every 10 s");
    }
}

/* Numbered marker plus a snapshot of the game state, for "the glitch is at mark 3". */
void oot_ps3_debug_mark(int number) {
    PlayState* play = Ps3DebugPlay();
    char line[192], info1[96], info2[96];
    s32 category;
    oot_ps3_debug_game_info(info1, info2, sizeof(info1));
    snprintf(line, sizeof(line), "graph: Play ===== MARK #%d ===== %s | %s", number, info1, info2);
    oot_ps3_early_trace(line);
    if (play == NULL) return;
    for (category = 0; category < ACTORCAT_MAX; category++) {
        Actor* actor = play->actorCtx.actorLists[category].head;
        s32 count = 0;
        while (actor != NULL && count < 64) {
            snprintf(line, sizeof(line), "graph: Play mark #%d actor cat=%d id=%04X params=%04X pos=(%.0f,%.0f,%.0f) room=%d",
                     number, (int)category, (unsigned)(u16)actor->id, (unsigned)(u16)actor->params,
                     (double)actor->world.pos.x, (double)actor->world.pos.y, (double)actor->world.pos.z,
                     (int)actor->room);
            oot_ps3_early_trace(line);
            actor = actor->next;
            count++;
        }
    }
}
