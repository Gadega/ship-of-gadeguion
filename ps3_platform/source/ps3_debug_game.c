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

/* ---- Warps, cutscenes, age switch, reset (debug menu) ----
 * Requests are queued and applied by oot_ps3_debug_game_tick() between game updates
 * (graph.c calls it in every game state). They work from the N64 logo, the title
 * screen and file select too: without a loaded file a fresh one is made (never saved).
 * In gameplay a request waits for a running transition / the pause menu to finish,
 * then everything left over is cleared so the new area loads plain and playable: no
 * cutscene (the new-game intro keeps cutsceneIndex 0xFFFx, which forced the Deku
 * Tree/Navi intro), no pending respawn, all buttons enabled, HUD visible, no forced
 * music; the target's first-visit cutscenes count as seen. */
#include "ps3_cutscene_list.inc"
void Sram_InitNewSave(void);                     /* z_sram.c */
void Audio_SetGameVolume(int player_id, f32 volume); /* code_800E4FE0.c */

enum { PS3_REQ_NONE, PS3_REQ_WARP, PS3_REQ_AGE, PS3_REQ_CUTSCENE, PS3_REQ_RESET };
static int sPs3Request, sPs3RequestEntrance, sPs3RequestFrames, sPs3RequestCutscene, sPs3RequestAge;
static char sPs3RequestName[64];

static void Ps3ClearSaveForWarp(void) {
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
}

static void Ps3ClearForWarp(PlayState* play) {
    Ps3ClearSaveForWarp();
    play->csCtx.state = CS_STATE_IDLE; /* a running cutscene must not send its own transition */
    play->csCtx.frames = 0;
}

/* No file loaded (N64 logo, title demo, file select): start from a fresh file that is
 * never written (fileNum 0xFF). */
static void Ps3EnsureGameSave(PlayState* play) {
    if (gSaveContext.gameMode == GAMEMODE_NORMAL && gSaveContext.fileNum != 0xFF) return;
    Sram_InitNewSave();
    gSaveContext.fileNum = 0xFF;
    gSaveContext.gameMode = GAMEMODE_NORMAL;
    if (play != NULL) play->linkAgeOnLoad = gSaveContext.linkAge; /* no equipment swap for this */
    oot_ps3_early_trace("trace: debug menu: no file loaded; using a fresh file (not saved)");
}

static void Ps3StartTransition(PlayState* play, s16 entrance, int markSeen) {
    extern void Cutscene_Ps3MarkEntranceCutscenesSeen(s16 entrance, s32 age); /* z_demo.c */
    if (markSeen) Cutscene_Ps3MarkEntranceCutscenesSeen(entrance, play->linkAgeOnLoad);
    play->nextEntranceIndex = entrance;
    play->transitionTrigger = TRANS_TRIGGER_START;
    play->transitionType = TRANS_TYPE_INSTANT;
    gSaveContext.nextTransitionType = TRANS_TYPE_FADE_BLACK_FAST;
}

static int Ps3QueueRequest(int type, int entrance, int cutscene, int age, const char* name) {
    char line[128];
    if (gGameState == NULL) return 0;
    sPs3Request = type;
    sPs3RequestEntrance = entrance;
    sPs3RequestCutscene = cutscene;
    sPs3RequestAge = age;
    sPs3RequestFrames = 0;
    snprintf(sPs3RequestName, sizeof(sPs3RequestName), "%s", name);
    snprintf(line, sizeof(line), "trace: debug menu request %d (%s)", type, name);
    oot_ps3_early_trace(line);
    return 1;
}

int oot_ps3_debug_warp(int index) {
    if (index < 0 || index >= (int)ARRAY_COUNT(kPs3WarpList)) return 0;
    return Ps3QueueRequest(PS3_REQ_WARP, kPs3WarpList[index].entrance, -1, -1, kPs3WarpList[index].name);
}

int oot_ps3_debug_switch_age(void) {
    if (Ps3DebugPlay() == NULL || gSaveContext.gameMode != GAMEMODE_NORMAL) return 0;
    return Ps3QueueRequest(PS3_REQ_AGE, 0, -1, -1, LINK_IS_ADULT ? "to child" : "to adult");
}

int oot_ps3_debug_link_is_adult(void) {
    return LINK_IS_ADULT ? 1 : 0;
}

int oot_ps3_debug_cutscene_count(void) {
    return (int)ARRAY_COUNT(kPs3CutsceneList);
}

const char* oot_ps3_debug_cutscene_name(int index) {
    if (index < 0 || index >= (int)ARRAY_COUNT(kPs3CutsceneList)) return "";
    return kPs3CutsceneList[index].name;
}

/* 0 adult, 1 child; cutscenes for either age use Link's current age. */
int oot_ps3_debug_cutscene_age(int index) {
    if (index < 0 || index >= (int)ARRAY_COUNT(kPs3CutsceneList)) return 1;
    if (kPs3CutsceneList[index].age == 2) return gSaveContext.gameMode == GAMEMODE_NORMAL ? gSaveContext.linkAge : 1;
    return kPs3CutsceneList[index].age;
}

int oot_ps3_debug_play_cutscene(int index, int age) {
    if (index < 0 || index >= (int)ARRAY_COUNT(kPs3CutsceneList)) return 0;
    return Ps3QueueRequest(PS3_REQ_CUTSCENE, kPs3CutsceneList[index].entrance, index, age ? 1 : 0,
                           kPs3CutsceneList[index].name);
}

int oot_ps3_debug_reset_to_title(void) {
    if (gGameState == NULL || gGameState->init == Opening_Init) return 0;
    return Ps3QueueRequest(PS3_REQ_RESET, 0, -1, -1, "reset to the title screen");
}

/* Age for the next area: in gameplay graph.c swaps the equipment when the area is left. */
static void Ps3SetAge(PlayState* play, int age) {
    if (age < 0) return;
    if (play != NULL) {
        play->linkAgeOnLoad = age;
    } else if (gSaveContext.linkAge != age) {
        Inventory_SwapAgeEquipment();
        gSaveContext.linkAge = age;
    }
}

/* Cutscene request: entrance cutscenes play again (flag cleared); scene-layer cutscenes
 * through nextCutsceneIndex (Play_Init picks the layer). */
static void Ps3PrepareCutscene(int index) {
    if (kPs3CutsceneList[index].cutsceneIndex == 0) {
        Flags_UnsetEventChkInf(kPs3CutsceneList[index].flag);
    } else {
        gSaveContext.nextCutsceneIndex = kPs3CutsceneList[index].cutsceneIndex;
    }
}

/* From a state that isn't gameplay (N64 logo, file select, or the title demo, which is
 * a Play state without a file): straight into a new Play state, like file select and
 * Map Select do. The title demo must not keep running with a file loaded: drawing its
 * HUD froze the game (2026-11-13 crash log). */
static void Ps3ApplyOutsidePlay(PlayState* titlePlay) {
    char line[160];
    GameState* state = gGameState;
    if (sPs3Request == PS3_REQ_AGE) {
        sPs3Request = PS3_REQ_NONE;
        return;
    }
    state->running = false;
    if (sPs3Request == PS3_REQ_RESET) {
        SET_NEXT_GAMESTATE(state, Opening_Init, OpeningContext);
        oot_ps3_early_trace("trace: debug menu reset -> title screen");
        sPs3Request = PS3_REQ_NONE;
        return;
    }
    Ps3EnsureGameSave(NULL);
    Ps3ClearSaveForWarp();
    Ps3SetAge(NULL, sPs3Request == PS3_REQ_CUTSCENE ? sPs3RequestAge : -1);
    /* the age is already applied: graph.c must not swap the equipment again */
    if (titlePlay != NULL) titlePlay->linkAgeOnLoad = gSaveContext.linkAge;
    Audio_QueueSeqCmd(SEQ_PLAYER_BGM_MAIN << 24 | NA_BGM_STOP);
    gSaveContext.entranceIndex = sPs3RequestEntrance;
    if (sPs3Request == PS3_REQ_CUTSCENE) {
        Ps3PrepareCutscene(sPs3RequestCutscene);
    } else {
        extern void Cutscene_Ps3MarkEntranceCutscenesSeen(s16 entrance, s32 age);
        Cutscene_Ps3MarkEntranceCutscenesSeen(sPs3RequestEntrance, gSaveContext.linkAge);
    }
    SET_NEXT_GAMESTATE(state, Play_Init, PlayState);
    snprintf(line, sizeof(line), "trace: debug menu %s (entrance %04X, age %d) from outside gameplay", sPs3RequestName,
             (unsigned)(u16)sPs3RequestEntrance, (int)gSaveContext.linkAge);
    oot_ps3_early_trace(line);
    sPs3Request = PS3_REQ_NONE;
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
    if (sPs3Request == PS3_REQ_RESET) {
        play->state.running = false;
        SET_NEXT_GAMESTATE(&play->state, Opening_Init, OpeningContext);
        oot_ps3_early_trace("trace: debug menu reset -> title screen");
        sPs3Request = PS3_REQ_NONE;
        return;
    }
    if (gSaveContext.gameMode != GAMEMODE_NORMAL) { /* title demo */
        Ps3ApplyOutsidePlay(play);
        return;
    }
    Ps3EnsureGameSave(play);
    Ps3ClearForWarp(play);
    if (sPs3Request == PS3_REQ_AGE) {
        s16 entrance = gSaveContext.entranceIndex;
        /* SoH SwitchAge: these two scenes are different areas for each age. */
        if (play->sceneNum == SCENE_HYRULE_CASTLE || play->sceneNum == SCENE_OUTSIDE_GANONS_CASTLE)
            entrance = ENTR_CASTLE_GROUNDS_SOUTH_EXIT;
        play->linkAgeOnLoad ^= 1; /* graph.c swaps the equipment when the area is released */
        Ps3StartTransition(play, entrance, 1);
        snprintf(line, sizeof(line), "trace: debug menu age switch -> %s at entrance %04X",
                 play->linkAgeOnLoad == LINK_AGE_ADULT ? "adult" : "child", (unsigned)(u16)entrance);
    } else if (sPs3Request == PS3_REQ_CUTSCENE) {
        Ps3SetAge(play, sPs3RequestAge);
        Ps3PrepareCutscene(sPs3RequestCutscene);
        Ps3StartTransition(play, (s16)sPs3RequestEntrance, 0);
        snprintf(line, sizeof(line), "trace: debug menu cutscene %s (entrance %04X, cs %04X, %s)", sPs3RequestName,
                 (unsigned)(u16)sPs3RequestEntrance, (unsigned)kPs3CutsceneList[sPs3RequestCutscene].cutsceneIndex,
                 sPs3RequestAge == LINK_AGE_ADULT ? "adult" : "child");
    } else {
        Ps3StartTransition(play, (s16)sPs3RequestEntrance, 1);
        snprintf(line, sizeof(line), "trace: debug menu warp to %s (entrance %04X) from scene %d",
                 sPs3RequestName, (unsigned)(u16)sPs3RequestEntrance, (int)play->sceneNum);
    }
    oot_ps3_early_trace(line);
    sPs3Request = PS3_REQ_NONE;
}

/* ---- Give items (debug menu) ---- */
#define PS3_GIVE_EVERYTHING 0x1FF
#define PS3_GIVE_REFILL 0x1FE
static const struct {
    const char* name;
    u16 item;
} kPs3ItemList[] = {
    { "Everything (all items, upgrades, songs, quest items, 20 hearts)", PS3_GIVE_EVERYTHING },
    { "Refill health, magic and rupees", PS3_GIVE_REFILL },
    { "Deku Sticks", ITEM_STICK }, { "Deku Nuts", ITEM_NUT }, { "Bombs", ITEM_BOMB }, { "Fairy Bow", ITEM_BOW },
    { "Fire Arrows", ITEM_ARROW_FIRE }, { "Din's Fire", ITEM_DINS_FIRE }, { "Fairy Slingshot", ITEM_SLINGSHOT },
    { "Fairy Ocarina", ITEM_OCARINA_FAIRY }, { "Ocarina (blue)", ITEM_OCARINA_TIME }, { "Bombchus", ITEM_BOMBCHU },
    { "Hookshot", ITEM_HOOKSHOT }, { "Longshot", ITEM_LONGSHOT }, { "Ice Arrows", ITEM_ARROW_ICE },
    { "Farore's Wind", ITEM_FARORES_WIND }, { "Boomerang", ITEM_BOOMERANG }, { "Lens of Truth", ITEM_LENS },
    { "Magic Beans", ITEM_BEAN }, { "Megaton Hammer", ITEM_HAMMER }, { "Light Arrows", ITEM_ARROW_LIGHT },
    { "Nayru's Love", ITEM_NAYRUS_LOVE }, { "Empty Bottle", ITEM_BOTTLE }, { "Bottle: Red Potion", ITEM_POTION_RED },
    { "Bottle: Fairy", ITEM_FAIRY }, { "Bottle: Blue Fire", ITEM_BLUE_FIRE }, { "Ruto's Letter", ITEM_LETTER_RUTO },
    { "Weird Egg", ITEM_WEIRD_EGG }, { "Zelda's Letter", ITEM_LETTER_ZELDA }, { "Keaton Mask", ITEM_MASK_KEATON },
    { "Skull Mask", ITEM_MASK_SKULL }, { "Spooky Mask", ITEM_MASK_SPOOKY }, { "Bunny Hood", ITEM_MASK_BUNNY },
    { "Mask of Truth", ITEM_MASK_TRUTH }, { "Pocket Egg", ITEM_POCKET_EGG }, { "Claim Check", ITEM_CLAIM_CHECK },
    { "Kokiri Sword", ITEM_SWORD_KOKIRI }, { "Master Sword", ITEM_SWORD_MASTER }, { "Biggoron's Sword", ITEM_SWORD_BGS },
    { "Deku Shield", ITEM_SHIELD_DEKU }, { "Hylian Shield", ITEM_SHIELD_HYLIAN }, { "Mirror Shield", ITEM_SHIELD_MIRROR },
    { "Goron Tunic", ITEM_TUNIC_GORON }, { "Zora Tunic", ITEM_TUNIC_ZORA }, { "Iron Boots", ITEM_BOOTS_IRON },
    { "Hover Boots", ITEM_BOOTS_HOVER }, { "Bullet Bag (50)", ITEM_BULLET_BAG_50 }, { "Quiver (50)", ITEM_QUIVER_50 },
    { "Bomb Bag (40)", ITEM_BOMB_BAG_40 }, { "Golden Gauntlets", ITEM_GAUNTLETS_GOLD }, { "Golden Scale", ITEM_SCALE_GOLDEN },
    { "Giant's Wallet", ITEM_WALLET_GIANT }, { "Deku Stick capacity 30", ITEM_STICK_UPGRADE_30 },
    { "Deku Nut capacity 40", ITEM_NUT_UPGRADE_40 }, { "Minuet of Forest", ITEM_SONG_MINUET },
    { "Bolero of Fire", ITEM_SONG_BOLERO }, { "Serenade of Water", ITEM_SONG_SERENADE },
    { "Requiem of Spirit", ITEM_SONG_REQUIEM }, { "Nocturne of Shadow", ITEM_SONG_NOCTURNE },
    { "Prelude of Light", ITEM_SONG_PRELUDE }, { "Zelda's Lullaby", ITEM_SONG_LULLABY }, { "Epona's Song", ITEM_SONG_EPONA },
    { "Saria's Song", ITEM_SONG_SARIA }, { "Sun's Song", ITEM_SONG_SUN }, { "Song of Time", ITEM_SONG_TIME },
    { "Song of Storms", ITEM_SONG_STORMS }, { "Forest Medallion", ITEM_MEDALLION_FOREST },
    { "Fire Medallion", ITEM_MEDALLION_FIRE }, { "Water Medallion", ITEM_MEDALLION_WATER },
    { "Spirit Medallion", ITEM_MEDALLION_SPIRIT }, { "Shadow Medallion", ITEM_MEDALLION_SHADOW },
    { "Light Medallion", ITEM_MEDALLION_LIGHT }, { "Kokiri's Emerald", ITEM_KOKIRI_EMERALD },
    { "Goron's Ruby", ITEM_GORON_RUBY }, { "Zora's Sapphire", ITEM_ZORA_SAPPHIRE },
    { "Stone of Agony", ITEM_STONE_OF_AGONY }, { "Gerudo's Card", ITEM_GERUDO_CARD },
    { "Heart Container", ITEM_HEART_CONTAINER }, { "Gold Skulltula Token", ITEM_SKULL_TOKEN },
    { "Small Key (this dungeon)", ITEM_KEY_SMALL }, { "Boss Key (this dungeon)", ITEM_KEY_BOSS },
    { "Map (this dungeon)", ITEM_DUNGEON_MAP }, { "Compass (this dungeon)", ITEM_COMPASS },
};

int oot_ps3_debug_item_count(void) {
    PlayState* play = Ps3DebugPlay();
    return play != NULL && gSaveContext.gameMode == GAMEMODE_NORMAL ? (int)ARRAY_COUNT(kPs3ItemList) : 0;
}

const char* oot_ps3_debug_item_name(int index) {
    if (index < 0 || index >= (int)ARRAY_COUNT(kPs3ItemList)) return "";
    return kPs3ItemList[index].name;
}

static void Ps3Refill(PlayState* play) {
    gSaveContext.health = gSaveContext.healthCapacity;
    if (gSaveContext.isMagicAcquired) Magic_Fill(play);
    Rupees_ChangeBy(999);
}

int oot_ps3_debug_give_item(int index) {
    PlayState* play = Ps3DebugPlay();
    char line[128];
    if (play == NULL || gSaveContext.gameMode != GAMEMODE_NORMAL || index < 0 ||
        index >= (int)ARRAY_COUNT(kPs3ItemList))
        return 0;
    if (kPs3ItemList[index].item == PS3_GIVE_REFILL) {
        Ps3Refill(play);
    } else if (kPs3ItemList[index].item == PS3_GIVE_EVERYTHING) {
        s32 i;
        for (i = 0; i < ARRAY_COUNT(kPs3ItemList); i++) {
            const u16 item = kPs3ItemList[i].item;
            if (item >= 0x100 || item == ITEM_HEART_CONTAINER || item == ITEM_SKULL_TOKEN || item == ITEM_KEY_SMALL ||
                item == ITEM_KEY_BOSS || item == ITEM_DUNGEON_MAP || item == ITEM_COMPASS || item == ITEM_POCKET_EGG ||
                item == ITEM_CLAIM_CHECK || item == ITEM_WEIRD_EGG || item == ITEM_LETTER_ZELDA)
                continue;
            Item_Give(play, (u8)item);
        }
        Item_Give(play, ITEM_BOTTLE);
        Item_Give(play, ITEM_BOTTLE);
        gSaveContext.healthCapacity = 20 * 0x10;
        gSaveContext.isMagicAcquired = true;
        gSaveContext.isDoubleMagicAcquired = true;
        gSaveContext.magicFillTarget = MAGIC_DOUBLE_METER;
        gSaveContext.magicLevel = 0;
        gSaveContext.isDoubleDefenseAcquired = true;
        gSaveContext.inventory.defenseHearts = 20;
        Ps3Refill(play);
    } else {
        Item_Give(play, (u8)kPs3ItemList[index].item);
    }
    snprintf(line, sizeof(line), "trace: debug menu gave %s", kPs3ItemList[index].name);
    oot_ps3_early_trace(line);
    return 1;
}

/* Game menu volumes (ps3_game_menu.c): music = both BGM players + fanfares. */
void oot_ps3_set_game_volumes(float music, float sfx) {
    extern void oot_ps3_audio_lock(void);
    extern void oot_ps3_audio_unlock(void);
    oot_ps3_audio_lock();
    Audio_SetGameVolume(SEQ_PLAYER_BGM_MAIN, music);
    Audio_SetGameVolume(SEQ_PLAYER_BGM_SUB, music);
    Audio_SetGameVolume(SEQ_PLAYER_FANFARE, music);
    Audio_SetGameVolume(SEQ_PLAYER_SFX, sfx);
    oot_ps3_audio_unlock();
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
    if (play == NULL) {
        if (sPs3Request != PS3_REQ_NONE && gGameState != NULL && gGameState->running) Ps3ApplyOutsidePlay(NULL);
        return;
    }
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
