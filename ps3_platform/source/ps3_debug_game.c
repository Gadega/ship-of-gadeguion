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

/* Ship of Harkinian's debug-console warp: the next entrance with an instant transition. */
int oot_ps3_debug_warp(int index) {
    PlayState* play = Ps3DebugPlay();
    char line[128];
    if (play == NULL || index < 0 || index >= (int)ARRAY_COUNT(kPs3WarpList)) return 0;
    if (play->transitionTrigger != TRANS_TRIGGER_OFF) return 0;
    play->nextEntranceIndex = kPs3WarpList[index].entrance;
    play->transitionTrigger = TRANS_TRIGGER_START;
    play->transitionType = TRANS_TYPE_INSTANT;
    gSaveContext.nextTransitionType = TRANS_TYPE_INSTANT;
    snprintf(line, sizeof(line), "trace: debug menu warp to %s (entrance %04X)", kPs3WarpList[index].name,
             (unsigned)(u16)kPs3WarpList[index].entrance);
    oot_ps3_early_trace(line);
    return 1;
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
