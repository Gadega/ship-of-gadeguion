// SPDX-License-Identifier: GPL-3.0-or-later
// Vanilla-mode implementations for desktop enhancement entry points referenced
// by the decompiled core.  These preserve base-game behavior or disable an
// optional feature; they are not claims that those desktop features are ported.

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <unistd.h>

#include "global.h"
#include "ResourceManagerHelpers.h"
#include "libultraship/color.h"
#include "soh/OTRGlobals.h"
#include "soh/ActorDB.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

extern "C" void oot_ps3_early_trace(const char* message);
extern "C" int oot_ps3_probe_scene_resource(const char* name, int spawn,
                                            ActorEntry* selectedEntry, int* selectedRoom);
extern "C" int oot_ps3_install_selected_room_mesh(PlayState* play);
extern "C" s32 Object_Spawn(ObjectContext* objectCtx, s16 objectId);

#include "shipwright_item_tables_ps3.inc"

namespace {
GameInteractor gVanillaInteractor;
char gTimeText[] = "00:00:00";

// ItemTableManager::RetrieveItemEntry(MOD_NONE, id) on PC: the vanilla table entry for
// GetItemID `id` with drawItemId/drawModIndex set from it, GET_ITEM_NONE when out of range.
// (A placeholder that copied `id` into every field made every chest give a Pocket Egg.)
GetItemEntry VanillaItemEntry(int16_t id) {
    if (id < 1 || static_cast<size_t>(id) > sizeof(kPs3GetItemTable) / sizeof(kPs3GetItemTable[0])) {
        const GetItemEntry none = GET_ITEM_NONE;
        return none;
    }
    GetItemEntry entry = kPs3GetItemTable[id - 1];
    entry.drawItemId = entry.itemId;
    entry.drawModIndex = entry.modIndex;
    return entry;
}
} // namespace

GameInteractor* GameInteractor::Instance = &gVanillaInteractor;
OTRGlobals* OTRGlobals::Instance = nullptr;

// 60 = SoH frame interpolation on (Play is drawn up to 3x per 20 Hz game update);
// a file named "fps20" in USRDIR turns it off. See oot_ps3_target_fps (bootstrap).
extern "C" int oot_ps3_target_fps(void);
uint32_t OTRGlobals::GetInterpolationFPS() { return static_cast<uint32_t>(oot_ps3_target_fps()); }

extern "C" {

void luslog(const char* file, int32_t line, int32_t level, const char* message) {
    fprintf(stderr, "[%d] %s:%d %s\n", level, file != nullptr ? file : "?", static_cast<int>(line),
            message != nullptr ? message : "");
}

void lusprintf(const char* file, int32_t line, int32_t level, const char* format, ...) {
    (void)file;
    (void)line;
    (void)level;
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

void AudioCollection_AddToCollection(char*, uint16_t) {}
bool AudioCollection_HasSequenceNum(uint16_t) { return false; }
uint16_t AudioEditor_GetReplacementSeq(uint16_t sequenceId) { return sequenceId; }

void BossRush_InitSave(void) {}
void FileChoose_UpdateBossRushMenu(GameState*) {}
void FileChoose_DrawBossRushMenuWindowContents(FileChooseContext*) {}

Color_RGBA8 CosmeticsEditor_GetDefaultValue(const char*) {
    Color_RGBA8 white = { 255, 255, 255, 255 };
    return white;
}

void CrashHandlerRegisterCallback(void (*)(char*, size_t*)) {}
void CrashHandler_PrintSohData(char*, size_t*) {}

void EntranceTracker_SetCurrentGrottoID(s16) {}
void EntranceTracker_SetLastEntranceOverride(s16) {}
char* GameplayStats_GetCurrentTime(void) { return gTimeText; }

size_t GetEquipNowMessage(char* destination, char* source, const size_t capacity) {
    if (destination == nullptr || capacity == 0) return 0;
    const char* text = source != nullptr ? source : "";
    size_t length = strlen(text);
    if (length >= capacity) length = capacity - 1;
    memcpy(destination, text, length);
    destination[length] = '\0';
    return length;
}

const char* Interface_ReplaceSpecialCharacters(char text[]) { return text != nullptr ? text : ""; }
void Mouse_HandleThirdPerson(float*, float*) {}
void Mouse_UpdateAll(void) {}
void NameTag_RemoveAllForActor(Actor*) {}

void OTRGfxPrint(const char* text, void* printer, void (*printImpl)(void*, char)) {
    if (text == nullptr || printImpl == nullptr) return;
    while (*text != '\0') printImpl(printer, *text++);
}
// OTRMessage_Init lives in shipwright_resource_ps3.cpp (builds the message tables).

// Match upstream's scene lookup and versioned archive path. Populate the
// actual player spawn from the user's O2R before the upstream actor path.
extern "C" void* oot_ps3_title_cutscene_segment(void);
extern "C" uint8_t D_8015FCC8;
extern "C" void* oot_ps3_scene_collision_header(void);
extern "C" int oot_ps3_scene_light_settings(void** out);
extern "C" int oot_ps3_spawn_scene(PlayState* play, s32 sceneId, s32 spawn);
extern "C" s32 oot_ps3_request_room(PlayState* play, RoomContext* roomCtx, s32 roomNum);
extern "C" s32 oot_ps3_finish_room(PlayState* play, RoomContext* roomCtx);
// 0: the title intro goes through the general scene loader (keep in sync with
// PLAY_PS3_USE_TITLE_STUB in z_play.c). 1: old hand-made Hyrule Field title stub.
#define OOT_PS3_USE_TITLE_STUB 0
static bool Ps3UseTitleStub(s32 sceneId) {
    return OOT_PS3_USE_TITLE_STUB && gSaveContext.gameMode == GAMEMODE_TITLE_SCREEN &&
           sceneId == SCENE_HYRULE_FIELD;
}
void OTRPlay_SpawnScene(PlayState* play, s32 sceneId, s32 spawn) {
    oot_ps3_early_trace("scene-probe: OTRPlay_SpawnScene entered");
    if (!Ps3UseTitleStub(sceneId)) {
        // Everything but the Hyrule Field title intro goes through the general scene loader
        // (including the other title demo scenes).
        if (ActorDB::Instance == nullptr) ActorDB::Instance = new ActorDB();
        if (!oot_ps3_spawn_scene(play, sceneId, spawn)) {
            oot_ps3_early_trace("scene-load: scene could not be loaded; exiting safely");
            _exit(0);
        }
        return;
    }
    SceneTableEntry* scene = &gSceneTable[sceneId];
    play->loadedScene = scene;
    play->sceneNum = sceneId;
    play->sceneConfig = scene->config;
    play->curSpawn = spawn;
    const bool dungeon = (sceneId >= SCENE_DEKU_TREE && sceneId <= SCENE_ICE_CAVERN) ||
                         sceneId == SCENE_GERUDO_TRAINING_GROUND || sceneId == SCENE_INSIDE_GANONS_CASTLE;
    const char* version = dungeon ? (ResourceMgr_IsGameMasterQuest() ? "mq" : "nonmq") : "shared";
    if (scene->sceneFile.fileName == nullptr) {
        oot_ps3_early_trace("scene-probe: scene table has no file name");
        return;
    }
    char path[256];
    const int written = snprintf(path, sizeof(path), "scenes/%s/%s/%s", version,
                                      scene->sceneFile.fileName, scene->sceneFile.fileName);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(path)) {
        oot_ps3_early_trace("scene-probe: scene archive path too long");
        return;
    }
    oot_ps3_early_trace(path);
    static ActorEntry selectedEntry;
    int selectedRoom = -1;
    if (!oot_ps3_probe_scene_resource(path, spawn, &selectedEntry, &selectedRoom)) {
        oot_ps3_early_trace("scene-init: no validated player spawn; exiting safely");
        _exit(0);
    }
    // Upstream OTRPlay_SpawnScene clears these before Scene_ExecuteCommands.
    // The V48 probe has not decoded room actor lists, so do not read stale
    // setupActorList contents during the first Actor_UpdateAll.
    // No cutscene commands have been installed either. An inherited segment
    // pointer must never be interpreted as raw CutsceneData by z_demo.c.
    play->csCtx.segment = static_cast<decltype(play->csCtx.segment)>(oot_ps3_title_cutscene_segment());
    if (play->csCtx.segment != nullptr) {
        // Environment_Init (z_kankyo.c) normally sets this during scene setup,
        // which the PS3 scene stub skips. Without it, cutscene camera commands
        // record their path but never take control of the camera.
        D_8015FCC8 = 1;
        oot_ps3_early_trace("trace: cutscene camera control enabled (D_8015FCC8=1)");
    }
    {
        CollisionHeader* ps3ColHeader = static_cast<CollisionHeader*>(oot_ps3_scene_collision_header());
        if (ps3ColHeader != nullptr) {
            oot_ps3_early_trace("trace: before BgCheck_Allocate with O2R scene collision");
            BgCheck_Allocate(&play->colCtx, play, ps3ColHeader);
            oot_ps3_early_trace("trace: scene collision installed (BgCheck_Allocate completed)");
        } else {
            oot_ps3_early_trace("trace: no scene collision to install");
        }
    }
    {
        void* ps3Lights = nullptr;
        const int ps3LightCount = oot_ps3_scene_light_settings(&ps3Lights);
        if (ps3Lights != nullptr && ps3LightCount > 0) {
            play->envCtx.lightSettingsList = static_cast<EnvLightSettings*>(ps3Lights);
            play->envCtx.numLightSettings = static_cast<u8>(ps3LightCount);
            oot_ps3_early_trace("trace: scene lighting settings installed");
        } else {
            oot_ps3_early_trace("trace: no scene lighting settings to install");
        }
    }
    {
        // Hyrule Field uses the normal sky (scene command 0x11). Play_InitEnvironment,
        // which normally calls Skybox_Init, does not run on PS3.
        oot_ps3_early_trace("trace: before Skybox_Init (normal sky)");
        play->skyboxId = SKYBOX_NORMAL_SKY;
        play->envCtx.skyboxDisabled = 0;
        play->envCtx.unk_BF = 0xFF; // light setting override: none until the cutscene sets one
        Skybox_Init(&play->state, &play->skyboxCtx, play->skyboxId);
        oot_ps3_early_trace("trace: Skybox_Init completed");
    }
    oot_ps3_early_trace(play->csCtx.segment != nullptr
                            ? "trace: title cutscene script installed into csCtx.segment"
                            : "scene-init: cutscene segment cleared until O2R scene commands load");
    // A decoded spawn does not provide the original N64 scene data. Leave
    // segment 2 empty until the O2R scene commands are installed as a scene.
    // Play_Draw emits this pointer through gSPSegment on its first frame.
    play->sceneSegment = nullptr;
    oot_ps3_early_trace("scene-init: scene segment cleared until O2R scene commands load");
    play->unk_11DFC = nullptr;
    play->setupEntranceList = nullptr;
    play->setupExitList = nullptr;
    play->cUpElfMsgs = nullptr;
    play->setupPathList = nullptr;
    play->setupActorList = nullptr;
    play->numSetupActors = 0;
    oot_ps3_early_trace("scene-init: empty scene actor list initialized");
    play->linkActorEntry = &selectedEntry;
    play->roomCtx.curRoom.num = selectedRoom;
    play->linkAgeOnLoad = gSaveContext.linkAge;
    oot_ps3_early_trace("actor-db: before construction");
    if (ActorDB::Instance == nullptr) {
        ActorDB::Instance = new ActorDB();
    }
    if (!ActorDB::Instance->RetrieveEntry(ACTOR_PLAYER).entry.valid) {
        oot_ps3_early_trace("actor-db: player entry unavailable; exiting safely");
        _exit(0);
    }
    oot_ps3_early_trace("actor-db: player entry initialized");
    oot_ps3_early_trace("scene-init: before Object_InitBank");
    Object_InitBank(play, &play->objectCtx);
    oot_ps3_early_trace("scene-init: after Object_InitBank");
    oot_ps3_early_trace("scene-init: before LightContext_Init");
    LightContext_Init(play, &play->lightCtx);
    oot_ps3_early_trace("scene-init: after LightContext_Init");
    oot_ps3_early_trace("scene-init: before TransitionActor_InitContext");
    TransitionActor_InitContext(&play->state, &play->transiActorCtx);
    oot_ps3_early_trace("scene-init: after TransitionActor_InitContext");
    oot_ps3_early_trace("scene-init: before initial room setup");
    func_80096FD4(play, &play->roomCtx.curRoom);
    // Native scene-command loading still omits the room draw-state reset.
    // The field draw config maintains its night-overlay alpha here.
    play->roomCtx.unk_74[0] = 0;
    play->roomCtx.unk_74[1] = 0;
    oot_ps3_early_trace("scene-init: after initial room setup");
    // Keep the diagnostic room number from func_80096FD4 until the scene's
    // collision and transition commands are loaded. Room_Draw uses the mesh
    // and segment independently of this number.
    if (!oot_ps3_install_selected_room_mesh(play))
        oot_ps3_early_trace("room-mesh: no decoded mesh; continuing without room draw");
    if (gSaveContext.linkAge > 1) {
        oot_ps3_early_trace("scene-init: invalid player age; exiting safely");
        _exit(0);
    }
    oot_ps3_early_trace("scene-init: before player Object_Spawn");
    Object_Spawn(&play->objectCtx, play->linkAgeOnLoad == 0 ? OBJECT_LINK_BOY : OBJECT_LINK_CHILD);
    oot_ps3_early_trace("scene-init: player object spawned; entering upstream Play_Init");
}
// The title intro installs its room directly; other scenes load rooms like OTR.
s32 OTRRoom_RequestNewRoom(PlayState* play, RoomContext* roomCtx, s32 roomNum) {
    if (Ps3UseTitleStub(play->sceneNum)) return 0;
    return oot_ps3_request_room(play, roomCtx, roomNum);
}
s32 OTRfunc_800973FC(PlayState* play, RoomContext* roomCtx) {
    if (Ps3UseTitleStub(play->sceneNum)) return 1;
    return oot_ps3_finish_room(play, roomCtx);
}

void RandoKaleido_DrawMiscCollectibles(PlayState*) {}
void Randomizer_DrawTriforcePieceGI(PlayState*, GetItemEntry) {}
uint8_t Randomizer_GenerateRandomizer(void) { return 0; }
RandomizerCheck Randomizer_GetCheckFromActor(s16, s16, s16) { return static_cast<RandomizerCheck>(0); }
void* Randomizer_GetEntranceOverrides(void) { return nullptr; }
GetItemEntry Randomizer_GetItemFromKnownCheck(RandomizerCheck, GetItemID originalId) {
    return VanillaItemEntry(static_cast<int16_t>(originalId));
}
GetItemEntry Randomizer_GetItemFromKnownCheckWithoutObtainabilityCheck(RandomizerCheck, GetItemID originalId) {
    return VanillaItemEntry(static_cast<int16_t>(originalId));
}
ItemObtainability Randomizer_GetItemObtainabilityFromRandomizerCheck(RandomizerCheck) {
    return static_cast<ItemObtainability>(0);
}
u8 Randomizer_GetSettingValue(RandomizerSettingKey) { return 0; }
ShopItemIdentity Randomizer_IdentifyShopItem(s32, u8) { return {}; }
void Randomizer_InitSaveFile(void) {}
bool Randomizer_IsCheckShuffled(RandomizerCheck) { return false; }
bool Randomizer_IsGenerating(void) { return false; }
uint8_t Randomizer_IsSeedGenerated(void) { return 0; }
uint8_t Randomizer_IsSpoilerLoaded(void) { return 0; }
void Randomizer_ParseSpoiler(const char*) {}
void Randomizer_SetSpoilerLoaded(bool) {}
void Randomizer_ShowRandomizerMenu(void) {}
void Randomizer_WaitForGeneration(void) {}
u16 Randomizer_Item_Give(PlayState*, GetItemEntry) { return 0; }

GetItemEntry ItemTable_Retrieve(int16_t id) { return VanillaItemEntry(id); }
GetItemEntry ItemTable_RetrieveEntry(s16 tableId, s16 id) {
    (void)tableId;
    return VanillaItemEntry(id);
}
GetItemEntry GetItemMystery(void) { return VanillaItemEntry(0); }
GetItemID RetrieveGetItemIDFromItemID(ItemID itemId) {
    for (const auto& pair : kPs3ItemIdToGetItemId) {
        if (pair.itemId == itemId) return pair.getItemId;
    }
    return GI_MAX;
}
RandomizerGet RetrieveRandomizerGetFromItemID(ItemID) { return static_cast<RandomizerGet>(0); }

Sprite* GetSeedTexture(uint8_t) { return nullptr; }
uint8_t GetSeedIconIndex(uint8_t index) { return index; }
u32 SpoilerFileExists(const char*) { return 0; }

s32 Ship_GetActorSpawnObjectIndex(PlayState* play, s16 objectId, s16 actorId) {
    if (play == nullptr) return -1;
    const ObjectContext* bank = &play->objectCtx;
    const int count = static_cast<int>(bank->num);
    char line[112];
    snprintf(line, sizeof(line), "intro-horse: object lookup actor=%d object=%d bank-count=%d",
             static_cast<int>(actorId), static_cast<int>(objectId), count);
    oot_ps3_early_trace(line);
    // Shipwright's default Object_GetIndex assumes the scene object list has
    // already been decoded. Until the PS3 scene loader populates that list,
    // reject an invalid bank count instead of walking off the status array.
    if (count < 0 || count > OBJECT_EXCHANGE_BANK_MAX) {
        oot_ps3_early_trace("intro-horse: invalid object bank count; actor spawn deferred");
        return -1;
    }
    for (int index = 0; index < count; ++index) {
        const int id = bank->status[index].id;
        if (id == objectId || id == -objectId) {
            oot_ps3_early_trace("intro-horse: actor object found in scene bank");
            return index;
        }
    }
    oot_ps3_early_trace("intro-horse: actor object absent from scene bank; spawn deferred");
    return -1;
}

int oot_ps3_stage_title_horse_object(PlayState* play) {
    if (play == nullptr || !Ps3UseTitleStub(play->sceneNum)) return 0;
    if (ActorDB::Instance == nullptr) {
        oot_ps3_early_trace("intro-horse: actor database unavailable");
        return 0;
    }
    const auto horse = ActorDB::Instance->RetrieveEntry(20).entry; // Actor 20 from the title trace.
    if (!horse.valid || horse.objectId < 0) {
        oot_ps3_early_trace("intro-horse: horse actor profile unavailable");
        return 0;
    }
    const s16 objectId = horse.objectId;
    ObjectContext* bank = &play->objectCtx;
    const int count = static_cast<int>(bank->num);
    if (count < 0 || count >= OBJECT_EXCHANGE_BANK_MAX - 1) {
        oot_ps3_early_trace("intro-horse: no safe free object bank slot");
        return 0;
    }
    for (int i = 0; i < count; ++i) {
        if (bank->status[i].id == objectId || bank->status[i].id == -objectId) {
            oot_ps3_early_trace("intro-horse: horse object already loaded");
            return 1;
        }
    }
    const uintptr_t begin = reinterpret_cast<uintptr_t>(bank->status[count].segment);
    const uintptr_t end = reinterpret_cast<uintptr_t>(bank->spaceEnd);
    char line[112];
    snprintf(line, sizeof(line), "intro-horse: staging object=%d free-bytes=%lu",
             static_cast<int>(objectId),
             static_cast<unsigned long>(end >= begin ? end - begin : 0));
    oot_ps3_early_trace(line);
    // PS3's gObjectTable keeps zeroed ROM offsets: Object_Spawn cannot DMA
    // this object. The actor's actual models are resolved by OTR resource
    // names; reserve a valid segment and register its object-bank identity.
    const size_t slotBytes = 256;
    if (begin == 0 || begin >= end || slotBytes >= end - begin) {
        oot_ps3_early_trace("intro-horse: object bank capacity invalid");
        return 0;
    }
    std::memset(reinterpret_cast<void*>(begin), 0, slotBytes);
    bank->status[count].id = objectId;
    bank->num = count + 1;
    bank->unk_09 = bank->num;
    bank->status[count + 1].segment = reinterpret_cast<void*>((begin + slotBytes + 15U) & ~uintptr_t(15U));
    oot_ps3_early_trace("intro-horse: title object registered without ROM DMA");
    return 1;
}
bool Ship_IsCStringEmpty(const char* text) { return text == nullptr || text[0] == '\0'; }
void Ship_CreateQuadVertexGroup(Vtx* vertices, s32 x, s32 y, s32 width, s32 height, u8 flipped) {
    if (vertices == nullptr) return;
    const s32 left = flipped ? width : 0;
    const s32 right = flipped ? width * 2 : width;
    vertices[0].v.ob[0] = x;         vertices[0].v.ob[1] = y;
    vertices[1].v.ob[0] = x + width; vertices[1].v.ob[1] = y;
    vertices[2].v.ob[0] = x;         vertices[2].v.ob[1] = y + height;
    vertices[3].v.ob[0] = x + width; vertices[3].v.ob[1] = y + height;
    vertices[0].v.tc[0] = left << 5;  vertices[0].v.tc[1] = 0;
    vertices[1].v.tc[0] = right << 5; vertices[1].v.tc[1] = 0;
    vertices[2].v.tc[0] = left << 5;  vertices[2].v.tc[1] = height << 5;
    vertices[3].v.tc[0] = right << 5; vertices[3].v.tc[1] = height << 5;
}
f32 Ship_GetCharFontWidth(u8) { return 8.0f; }
void* Ship_GetCharFontTexture(u8) { return nullptr; }

u8 SohFileSelect_CountVisibleQuests(void) { return 1; }
const char* SohFileSelect_GetSettingText(uint8_t, uint8_t) { return "Original Quest"; }
bool SohFileSelect_IsQuestHidden(u8 quest) { return quest != 0; }
void SohFileSelect_ShowPresetModal(void) {}

void enableBetaQuest(void) {}
void disableBetaQuest(void) {}

} // extern "C"
