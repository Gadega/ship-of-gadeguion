// SPDX-License-Identifier: GPL-3.0-or-later
// Early native PS3 implementations needed to turn the compiled SoH core into
// an executable.  This is deliberately a vanilla-game bootstrap layer: PC UI,
// randomizer and debugging features remain disabled until the base game boots.

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include "global.h"
#include "libultraship/bridge/audiobridge.h"
#include "ps3_platform/shipwright_video_ps3.h"
#include "soh/OTRGlobals.h"
#include "soh/ResourceManagerHelpers.h"

// SaveManager.h exposes its desktop JSON/thread-pool implementation in C++.
// The bootstrap only needs this stable C-facing metadata layout.
typedef struct {
    u8 valid;
    u16 deaths;
    u8 playerName[8];
    u16 healthCapacity;
    u32 questItems;
    s8 defense;
    u16 health;
    u32 requiresMasterQuest;
    u32 requiresOriginal;
    u8 seedHash[5];
    u8 randoSave;
    char buildVersion[50];
    s16 buildVersionMajor;
    s16 buildVersionMinor;
    s16 buildVersionPatch;
    u8 inventoryItems[24];
    u16 equipment;
    u32 upgrades;
    u8 isMagicAcquired;
    u8 isDoubleMagicAcquired;
    s16 rupees;
    s16 gsTokens;
    u8 isDoubleDefenseAcquired;
    s32 filenameLanguage;
    bool gregFound;
    bool hasWallet;
    u8 triforcePieces;
    u8 maxTriforcePieces;
    bool hasFishingRod;
    bool fishingPoleShuffled;
} SaveFileMetaInfo;

// FilenameLanguage, also from SaveManager.h.
enum { NAME_LANGUAGE_PAL, NAME_LANGUAGE_NTSC_JPN, NAME_LANGUAGE_NTSC_ENG };

extern "C" void oot_ps3_early_trace(const char* message);
extern "C" float oot_ps3_display_aspect(void);
extern "C" int oot_ps3_savedata_write(int slot, const void* data, unsigned size, const char* subtitle,
                                      const char* detail);
extern "C" int oot_ps3_savedata_read(int slot, void* buffer, unsigned capacity, unsigned* size);

namespace {

constexpr const char* kSavePath = "/dev_hdd0/game/OOTP30048/USRDIR/oot_save.sav";
constexpr size_t kSaveCapacity = 0x8000;
uint32_t gFrame = 0;
const Gfx* sTitleOpaqueCommands = nullptr;
size_t sTitleOpaqueCapacity = 0;
SaveFileMetaInfo gSaveMeta[3] = {};

void ReadAt(uintptr_t offset, void* destination, size_t size) {
    if (destination == nullptr || size == 0) return;
    memset(destination, 0, size);
    FILE* file = fopen(kSavePath, "rb");
    if (file == nullptr) return;
    if (fseek(file, static_cast<long>(offset), SEEK_SET) == 0) {
        (void)fread(destination, 1, size, file);
    }
    fclose(file);
}

void WriteAt(uintptr_t offset, const void* source, size_t size) {
    if (source == nullptr || size == 0 || offset > kSaveCapacity || size > kSaveCapacity - offset) return;
    FILE* file = fopen(kSavePath, "r+b");
    if (file == nullptr) file = fopen(kSavePath, "w+b");
    if (file == nullptr) return;
    if (fseek(file, static_cast<long>(offset), SEEK_SET) == 0) {
        (void)fwrite(source, 1, size, file);
        (void)fflush(file);
    }
    fclose(file);
}

// Save slots: one file each. The persistent data is what the N64 SRAM held
// (SaveContext up to `fileNum`) plus SoH's `ship` block; everything between
// them (game mode, respawn, timers, HUD) is runtime state and is not stored.
constexpr size_t kSaveBaseSize = offsetof(SaveContext, fileNum);
constexpr uint32_t kSaveVersion = 1;
const char kSaveMagic[8] = { 'O', 'O', 'T', 'P', 'S', '3', 'S', 'V' };

struct SaveFileHeader {
    char magic[8];
    uint32_t version;
    uint32_t baseSize;
    uint32_t shipSize;
};

SaveContext sSlotScratch; // file contents for meta/copy; too large for the stack

void SlotPath(int slot, char* path, size_t capacity) {
    snprintf(path, capacity, "/dev_hdd0/game/OOTP30048/USRDIR/oot_file%d.sav", slot + 1);
}

// Slot bytes: header + SaveContext up to fileNum + the ship block. Stored in the
// PS3 system save data (ps3_savedata.c, one directory per slot); the old
// USRDIR/oot_fileN.sav files are still written as a backup and are migrated
// into the system save data the first time a slot without one is read.
constexpr size_t kSlotBytes = sizeof(SaveFileHeader) + kSaveBaseSize + sizeof(SaveContext::ship);
// An erased slot keeps its save directory and holds just this marker.
const char kErasedMagic[8] = { 'O', 'O', 'T', 'P', 'S', '3', 'E', 'M' };
unsigned char sSlotBytes[kSlotBytes];

size_t SerializeSlot(const SaveContext& save, unsigned char* out) {
    SaveFileHeader header = {};
    memcpy(header.magic, kSaveMagic, sizeof(header.magic));
    header.version = kSaveVersion;
    header.baseSize = kSaveBaseSize;
    header.shipSize = sizeof(save.ship);
    memcpy(out, &header, sizeof(header));
    memcpy(out + sizeof(header), &save, kSaveBaseSize);
    memcpy(out + sizeof(header) + kSaveBaseSize, &save.ship, sizeof(save.ship));
    return kSlotBytes;
}

// Copies only the persistent parts into `save`; its runtime fields are untouched.
bool DeserializeSlot(const unsigned char* in, size_t size, SaveContext* save) {
    SaveFileHeader header = {};
    if (size != kSlotBytes) return false;
    memcpy(&header, in, sizeof(header));
    if (memcmp(header.magic, kSaveMagic, sizeof(header.magic)) != 0 || header.version != kSaveVersion ||
        header.baseSize != kSaveBaseSize || header.shipSize != sizeof(save->ship))
        return false;
    memcpy(save, in + sizeof(header), kSaveBaseSize);
    memcpy(&save->ship, in + sizeof(header) + kSaveBaseSize, sizeof(save->ship));
    return true;
}

bool WriteSlotFile(int slot, const unsigned char* bytes, size_t size) {
    char path[96];
    SlotPath(slot, path, sizeof(path));
    FILE* file = fopen(path, "wb");
    if (file == nullptr) return false;
    const bool ok = fwrite(bytes, size, 1, file) == 1;
    fclose(file);
    return ok;
}

size_t ReadSlotFile(int slot, unsigned char* bytes, size_t capacity) {
    char path[96];
    SlotPath(slot, path, sizeof(path));
    FILE* file = fopen(path, "rb");
    if (file == nullptr) return 0;
    const size_t size = fread(bytes, 1, capacity, file);
    fclose(file);
    return size;
}

// Player name as text for the XMB save list (SoH's z2ASCII /
// decodeNTSCPlayerNameChar; kana become '?').
void PlayerNameText(const SaveContext& save, char* out, size_t capacity) {
    size_t n = 0;
    for (size_t i = 0; i < sizeof(save.playerName) && n + 1 < capacity; ++i) {
        const int c = save.playerName[i];
        char ch;
        if (save.ship.filenameLanguage == NAME_LANGUAGE_PAL) {
            ch = c < 10 ? '0' + c : c < 36 ? 'A' + (c - 10) : c < 62 ? 'a' + (c - 36) : c == 62 ? ' '
                 : c == 63 ? '-' : c == 64 ? '.' : '?';
        } else {
            ch = c < 10 ? '0' + c : (c >= 171 && c < 197) ? 'A' + (c - 171) : (c >= 197 && c < 223) ? 'a' + (c - 197)
                 : c == 223 ? ' ' : c == 228 ? '-' : c == 234 ? '.' : '?';
        }
        out[n++] = ch;
    }
    while (n > 0 && out[n - 1] == ' ') --n;
    out[n] = '\0';
}

// Writes the slot to the PS3 system save data; leaves the bytes in sSlotBytes.
bool WriteSystemSave(int slot, const SaveContext& save, size_t* size) {
    *size = SerializeSlot(save, sSlotBytes);
    char name[16];
    char subtitle[64];
    char detail[192];
    PlayerNameText(save, name, sizeof(name));
    snprintf(subtitle, sizeof(subtitle), "File %d: %s", slot + 1, name[0] != '\0' ? name : "Link");
    snprintf(detail, sizeof(detail), "Hearts: %d/%d\nRupees: %d\nGold Skulltulas: %d\nDeaths: %d",
             save.health / 16, save.healthCapacity / 16, save.rupees, save.inventory.gsTokens, save.deaths);
    return oot_ps3_savedata_write(slot, sSlotBytes, static_cast<unsigned>(*size), subtitle, detail) == 1;
}

bool WriteSlot(int slot, const SaveContext& save) {
    if (slot < 0 || slot >= 3) return false;
    size_t size = 0;
    const bool system = WriteSystemSave(slot, save, &size);
    const bool file = WriteSlotFile(slot, sSlotBytes, size);
    return system || file;
}

// Reads only the persistent parts into `save`; its runtime fields are untouched.
bool ReadSlot(int slot, SaveContext* save) {
    if (slot < 0 || slot >= 3 || save == nullptr) return false;
    unsigned size = 0;
    const int found = oot_ps3_savedata_read(slot, sSlotBytes, sizeof(sSlotBytes), &size);
    if (found == 1) return DeserializeSlot(sSlotBytes, size, save); // includes erased slots
    // No system save yet (or it failed): the USRDIR file from older builds.
    const size_t fileSize = ReadSlotFile(slot, sSlotBytes, sizeof(sSlotBytes));
    if (!DeserializeSlot(sSlotBytes, fileSize, save)) return false;
    if (found == 0) {
        size_t written = 0;
        const bool migrated = WriteSystemSave(slot, *save, &written);
        char line[96];
        snprintf(line, sizeof(line), "trace: save slot=%d migrated to PS3 save data %s", slot + 1,
                 migrated ? "ok" : "FAILED");
        oot_ps3_early_trace(line);
    }
    return true;
}

bool EraseSlot(int slot) {
    char path[96];
    SlotPath(slot, path, sizeof(path));
    remove(path);
    unsigned char marker[sizeof(SaveFileHeader)] = {};
    memcpy(marker, kErasedMagic, sizeof(kErasedMagic));
    return oot_ps3_savedata_write(slot, marker, sizeof(marker), "Empty", "") == 1;
}

// Mirrors SaveManager::InitMeta for a vanilla (non-randomizer) file.
void BuildMeta(int slot, const SaveContext& save) {
    SaveFileMetaInfo& meta = gSaveMeta[slot];
    memset(&meta, 0, sizeof(meta));
    meta.valid = true;
    meta.deaths = save.deaths;
    memcpy(meta.playerName, save.playerName, sizeof(meta.playerName));
    meta.healthCapacity = save.healthCapacity;
    meta.questItems = save.inventory.questItems;
    memcpy(meta.inventoryItems, save.inventory.items, sizeof(meta.inventoryItems));
    meta.equipment = save.inventory.equipment;
    meta.upgrades = save.inventory.upgrades;
    meta.isMagicAcquired = save.isMagicAcquired;
    meta.isDoubleMagicAcquired = save.isDoubleMagicAcquired;
    meta.rupees = save.rupees;
    meta.gsTokens = save.inventory.gsTokens;
    meta.isDoubleDefenseAcquired = save.isDoubleDefenseAcquired;
    meta.filenameLanguage = save.ship.filenameLanguage;
    meta.hasWallet = true;
    meta.hasFishingRod = true;
    meta.defense = save.inventory.defenseHearts;
    meta.health = save.health;
    meta.requiresMasterQuest = save.ship.quest.id == QUEST_MASTER;
    meta.requiresOriginal = save.ship.quest.id != QUEST_MASTER;
    meta.buildVersionMajor = save.ship.stats.buildVersionMajor;
    meta.buildVersionMinor = save.ship.stats.buildVersionMinor;
    meta.buildVersionPatch = save.ship.stats.buildVersionPatch;
}

void LoadMeta(int slot) {
    if (slot < 0 || slot >= 3) return;
    if (ReadSlot(slot, &sSlotScratch)) {
        BuildMeta(slot, sSlotScratch);
    } else {
        memset(&gSaveMeta[slot], 0, sizeof(gSaveMeta[slot]));
    }
}

// SaveManager::InitFileNormal: the state of a brand-new file.
void InitFileNormal() {
    gSaveContext.totalDays = 0;
    gSaveContext.bgsDayCount = 0;
    gSaveContext.deaths = 0;
    if (ResourceMgr_GetGameRegion(0) == GAME_REGION_PAL && gSaveContext.language != LANGUAGE_JPN) {
        memset(gSaveContext.playerName, 0x3E, sizeof(gSaveContext.playerName));
    } else {
        memset(gSaveContext.playerName, 0xDF, sizeof(gSaveContext.playerName));
    }
    gSaveContext.healthCapacity = STARTING_HEALTH;
    gSaveContext.health = STARTING_HEALTH;
    gSaveContext.magicLevel = 0;
    gSaveContext.magic = MAGIC_NORMAL_METER;
    gSaveContext.rupees = 0;
    gSaveContext.swordHealth = 0;
    gSaveContext.naviTimer = 0;
    gSaveContext.isMagicAcquired = 0;
    gSaveContext.isDoubleMagicAcquired = 0;
    gSaveContext.isDoubleDefenseAcquired = 0;
    gSaveContext.bgsFlag = 0;
    gSaveContext.ocarinaGameRoundNum = 0;
    for (ItemEquips* equips : { &gSaveContext.childEquips, &gSaveContext.adultEquips, &gSaveContext.equips }) {
        memset(equips->buttonItems, ITEM_NONE, sizeof(equips->buttonItems));
        memset(equips->cButtonSlots, SLOT_NONE, sizeof(equips->cButtonSlots));
        equips->equipment = 0;
    }
    gSaveContext.equips.equipment = 0x1100;
    gSaveContext.unk_54 = 0;
    gSaveContext.savedSceneNum = SCENE_LINKS_HOUSE;

    memset(gSaveContext.inventory.items, ITEM_NONE, sizeof(gSaveContext.inventory.items));
    memset(gSaveContext.inventory.ammo, 0, sizeof(gSaveContext.inventory.ammo));
    gSaveContext.inventory.equipment = 0x1100;
    gSaveContext.inventory.upgrades = 0;
    gSaveContext.inventory.questItems = 0;
    memset(gSaveContext.inventory.dungeonItems, 0, sizeof(gSaveContext.inventory.dungeonItems));
    memset(gSaveContext.inventory.dungeonKeys, 0xFF, sizeof(gSaveContext.inventory.dungeonKeys));
    gSaveContext.inventory.defenseHearts = 0;
    gSaveContext.inventory.gsTokens = 0;
    memset(gSaveContext.sceneFlags, 0, sizeof(gSaveContext.sceneFlags));
    memset(&gSaveContext.fw, 0, sizeof(gSaveContext.fw));
    memset(gSaveContext.gsFlags, 0, sizeof(gSaveContext.gsFlags));
    memset(gSaveContext.highScores, 0, sizeof(gSaveContext.highScores));
    memset(gSaveContext.eventChkInf, 0, sizeof(gSaveContext.eventChkInf));
    memset(gSaveContext.itemGetInf, 0, sizeof(gSaveContext.itemGetInf));
    memset(gSaveContext.infTable, 0, sizeof(gSaveContext.infTable));
    gSaveContext.worldMapAreaData = 0;
    gSaveContext.scarecrowLongSongSet = 0;
    memset(gSaveContext.scarecrowLongSong, 0, sizeof(gSaveContext.scarecrowLongSong));
    gSaveContext.scarecrowSpawnSongSet = 0;
    memset(gSaveContext.scarecrowSpawnSong, 0, sizeof(gSaveContext.scarecrowSpawnSong));

    gSaveContext.horseData.scene = SCENE_HYRULE_FIELD;
    gSaveContext.horseData.pos.x = -1840;
    gSaveContext.horseData.pos.y = 72;
    gSaveContext.horseData.pos.z = 5497;
    gSaveContext.horseData.angle = -0x6AD9;
    gSaveContext.infTable[29] = 1;
    gSaveContext.sceneFlags[5].swch = 0x40000000;

    // SoH specific
    memset(&gSaveContext.ship, 0, sizeof(gSaveContext.ship));
    gSaveContext.ship.backupFW = gSaveContext.fw;
    gSaveContext.ship.pendingSale = ITEM_NONE;
    gSaveContext.ship.pendingSaleMod = MOD_NONE;
    gSaveContext.ship.pendingIceTrapCount = 0;
    gSaveContext.ship.maskMemory = PLAYER_MASK_NONE;
    gSaveContext.ship.quest.id = ResourceMgr_GameHasOriginal() ? QUEST_NORMAL : QUEST_MASTER;
    gSaveContext.ship.filenameLanguage = (ResourceMgr_GetGameRegion(0) == GAME_REGION_PAL &&
                                          gSaveContext.language != LANGUAGE_JPN)
                                             ? NAME_LANGUAGE_PAL
                                             : ((gSaveContext.language == LANGUAGE_JPN) ? NAME_LANGUAGE_NTSC_JPN
                                                                                       : NAME_LANGUAGE_NTSC_ENG);
}

void TraceSave(const char* what, int slot, bool ok) {
    char line[96];
    snprintf(line, sizeof(line), "trace: save %s slot=%d %s", what, slot + 1, ok ? "ok" : "FAILED");
    oot_ps3_early_trace(line);
}

} // namespace

extern "C" {
void oot_ps3_early_trace(const char* message);
int oot_ps3_fast3d_render_commands(void* commands);
int oot_ps3_fast3d_render_commands_interp(void* commands, int index, float t, int interpolate);
int FrameInterpolation_Ps3TakeRecorded(void);
unsigned long long oot_ps3_microseconds_now(void);

// 60 unless USRDIR/fps20 exists (then 20, no interpolation). Read once.
void sysThreadYieldPs3(void); // ps3_early_entry.c (PS3 system headers stay out of here)
int oot_ps3_target_fps(void) {
    static int sFps = 0;
    if (sFps == 0) {
        FILE* off = fopen("/dev_hdd0/game/OOTP30048/USRDIR/fps20", "rb");
        sFps = off != nullptr ? 20 : 60;
        if (off != nullptr) fclose(off);
        oot_ps3_early_trace(sFps == 60 ? "graph: frame interpolation on (target 60 fps; USRDIR/fps20 turns it off)"
                                       : "graph: frame interpolation off (USRDIR/fps20 present)");
    }
    return sFps;
}
void oot_ps3_poll_exit(void);
void oot_ps3_watchdog_kick(void);
unsigned int oot_ps3_vblank_count(void); // rsx_screen.c
void oot_ps3_heap_check(int maxReports, int fromWatchdog); // ps3_heap_guard.c
void oot_ps3_rsx_take_tnl_stats(unsigned int* tnlDraws, unsigned int* tnlVerts, unsigned int* cpuDraws,
                                unsigned int* vpSwitches, unsigned int* constVec4, unsigned int* combHits,
                                unsigned int* combMisses); // fast3d_rsx_ps3.c

// Vblank count for pacing. Falls back to a 59.94 Hz clock if the vblank handler
// never runs (so pacing can't stall on a dead counter).
unsigned int oot_ps3_pacing_vblank(void) {
    static int sState = 0; // 0 probing, 1 live vblanks, 2 timer fallback
    static unsigned int sFirst = 0;
    static unsigned long long sProbeStart = 0;
    const unsigned int v = oot_ps3_vblank_count();
    if (sState == 1) return v;
    const unsigned long long now = oot_ps3_microseconds_now();
    if (sState == 0) {
        if (sProbeStart == 0) {
            sProbeStart = now;
            sFirst = v;
        } else if (v != sFirst) {
            sState = 1;
            oot_ps3_early_trace("graph: Play pacing on display vblanks");
            return v;
        } else if (now - sProbeStart > 500000ULL) {
            sState = 2;
            oot_ps3_early_trace("graph: Play pacing: vblank counter dead, using a 59.94 Hz timer");
        }
    }
    return static_cast<unsigned int>(now / 16683ULL);
}
void* ResourceGetDataByName(const char* name);
size_t ResourceGetSizeByName(const char* name);
uint16_t ResourceGetTexWidthByName(const char* name);
uint16_t ResourceGetTexHeightByName(const char* name);

void oot_ps3_graph_set_opaque_list(const Gfx* commands, size_t count) {
    sTitleOpaqueCommands = commands;
    sTitleOpaqueCapacity = count;
}

void Graph_StartFrame(void) {
    // Diagnostic boot: do not start the PS3 video bridge here yet.  The real
    // frame-start path currently blocks before returning, so keep the graph
    // loop moving while we probe game-state and pad/update code.
}

// The last update's draws, for graph.c's per-update spike line.
static unsigned long long sLastDrawUs[3];
static unsigned int sLastDrawn, sLastSkipped;
void oot_ps3_last_update_draws(unsigned long long drawUs[3], unsigned int* drawn, unsigned int* skipped) {
    for (int i = 0; i < 3; ++i) drawUs[i] = sLastDrawUs[i];
    *drawn = sLastDrawn;
    *skipped = sLastSkipped;
}

void Graph_ProcessGfxCommands(Gfx* commands) {
    oot_ps3_poll_exit();
    (void)commands;
    oot_ps3_early_trace("gfx-process: entered");
    oot_ps3_early_trace("gfx-process: before isolated video startup");
    const int videoReady = oot_ps3_shipwright_video_start();
    oot_ps3_early_trace(videoReady ? "gfx-process: isolated video startup succeeded" :
                                     "gfx-process: isolated video startup failed");
    if (videoReady) {
        oot_ps3_early_trace("gfx-process: before native Fast3D display-list draw");
        {
            // Frame pacing on the display's vblanks (rsx_screen.c counts them; 59.94 Hz).
            // One game update (OoT logic, 20 Hz) owns three vblank slots. With frame
            // interpolation each slot shows one draw: t = 1/3, 2/3 (interpolated
            // matrices), then the real frame t = 1. An in-between draw whose vblank has
            // already come is skipped, so a slow moment drops one frame instead of making
            // the following frames uneven. The real frame is always drawn. The old pacing
            // slept to 16.67 ms CPU-timer slots, which drift against the display and
            // produced a growing GPU wait and periodic hitches.
            static unsigned int sSlotBase = 0;      // vblank where this update's first slot starts
            static bool sSlotStarted = false;
            static unsigned int sReportTicks = 0, sReportDrawn = 0, sReportSkipped = 0, sReportResync = 0;
            static unsigned long long sReportRenderUs = 0, sReportWaitUs = 0, sReportMaxDrawUs = 0;
            const bool interpolating = oot_ps3_target_fps() >= 60 && FrameInterpolation_Ps3TakeRecorded() != 0;
            unsigned int vNow = oot_ps3_pacing_vblank();
            if (!sSlotStarted || static_cast<int>(vNow - sSlotBase) > 3 || static_cast<int>(sSlotBase - vNow) > 6) {
                if (sSlotStarted) ++sReportResync; // more than a whole update late (or clock jump)
                sSlotBase = vNow + 1U;
                sSlotStarted = true;
            }
            // Start an update at most one vblank before its first slot (the game logic
            // for the next update already ran; this keeps 20 updates per second).
            {
                const unsigned long long w0 = oot_ps3_microseconds_now();
                while (static_cast<int>(vNow - (sSlotBase - 1U)) < 0 &&
                       oot_ps3_microseconds_now() - w0 < 100000ULL) {
                    usleep(1000);
                    vNow = oot_ps3_pacing_vblank();
                }
                sReportWaitUs += oot_ps3_microseconds_now() - w0;
            }
            bool drawn = false;
            sLastDrawUs[0] = sLastDrawUs[1] = sLastDrawUs[2] = 0;
            sLastDrawn = sLastSkipped = 0;
            for (int slot = interpolating ? 0 : 2; slot < 3; ++slot) {
                const bool last = slot == 2;
                if (!last && static_cast<int>(oot_ps3_pacing_vblank() - (sSlotBase + static_cast<unsigned int>(slot) + 1U)) >= 0) {
                    ++sReportSkipped; // its vblank has passed
                    ++sLastSkipped;
                    continue;
                }
                const unsigned long long r0 = oot_ps3_microseconds_now();
                const float t = static_cast<float>(slot + 1) / 3.0f;
                if (!oot_ps3_fast3d_render_commands_interp(commands, slot, t, last ? 0 : 1)) break;
                drawn = true;
                oot_ps3_watchdog_kick();
                const unsigned long long spent = oot_ps3_microseconds_now() - r0;
                sLastDrawUs[slot] = spent;
                ++sLastDrawn;
                sReportRenderUs += spent;
                if (spent > sReportMaxDrawUs) sReportMaxDrawUs = spent;
                ++sReportDrawn;
            }
            sSlotBase += 3U;
            if (drawn) {
                if (++sReportTicks % 64 == 0) {
                    oot_ps3_heap_check(4, 0);
                    char line[200];
                    snprintf(line, sizeof(line),
                             "graph: Play fps vsync/64 updates drawn=%u (%.1f fps) skipped=%u resync=%u render=%llu us "
                             "maxDraw=%llu us waited=%llu us interp=%d",
                             sReportDrawn, static_cast<double>(sReportDrawn) * 59.94 / 192.0, sReportSkipped,
                             sReportResync, sReportRenderUs, sReportMaxDrawUs, sReportWaitUs, interpolating ? 1 : 0);
                    oot_ps3_early_trace(line);
                    {
                        unsigned int tnlDraws = 0, tnlVerts = 0, cpuDraws = 0, vpSwitches = 0, constVec4 = 0;
                        unsigned int combHits = 0, combMisses = 0;
                        oot_ps3_rsx_take_tnl_stats(&tnlDraws, &tnlVerts, &cpuDraws, &vpSwitches, &constVec4,
                                                   &combHits, &combMisses);
                        snprintf(line, sizeof(line),
                                 "graph: Play tnl/64 updates gpuDraws=%u gpuVerts=%u cpuDraws=%u vpSwitches=%u "
                                 "constVec4=%u comb hit/miss=%u/%u",
                                 tnlDraws, tnlVerts, cpuDraws, vpSwitches, constVec4, combHits, combMisses);
                        oot_ps3_early_trace(line);
                    }
                    sReportDrawn = sReportSkipped = sReportResync = 0;
                    sReportRenderUs = sReportWaitUs = sReportMaxDrawUs = 0;
                }
                oot_ps3_early_trace("gfx-process: native Fast3D display-list frame presented");
                ++gFrame;
                oot_ps3_early_trace("gfx-process: completed");
                return;
            }
        }
        oot_ps3_early_trace("gfx-process: native Fast3D unavailable; title fallback");
        OotPs3TitleRect rects[64] = {};
        size_t rectCount = 0;
        const size_t limit = sTitleOpaqueCapacity < 2048U ? sTitleOpaqueCapacity : 2048U;
        for (size_t i = 0; sTitleOpaqueCommands != nullptr && i < limit; ++i) {
            const uint32_t w0 = sTitleOpaqueCommands[i].words.w0;
            const uint32_t w1 = sTitleOpaqueCommands[i].words.w1;
            const uint32_t opcode = w0 >> 24;
            if (opcode == 0xE4U && rectCount < sizeof(rects) / sizeof(rects[0])) {
                OotPs3TitleRect& rect = rects[rectCount];
                rect.left = static_cast<uint16_t>(((w1 >> 12) & 0xFFFU) >> 2);
                rect.top = static_cast<uint16_t>((w1 & 0xFFFU) >> 2);
                rect.right = static_cast<uint16_t>(((w0 >> 12) & 0xFFFU) >> 2);
                rect.bottom = static_cast<uint16_t>((w0 & 0xFFFU) >> 2);
                if (rect.right > rect.left && rect.bottom > rect.top) ++rectCount;
            }
            if (opcode == 0xDFU || opcode == 0xB8U ||
                (opcode == 0xDEU && (w0 & 0x00010000U) != 0U)) break;
        }
        char line[80];
        snprintf(line, sizeof(line), "gfx-process: decoded %u real title texture rectangles",
                 static_cast<unsigned>(rectCount));
        oot_ps3_early_trace(line);
        // Only the user's locally extracted archive can supply these pixels.
        // The title itself requests this 192x32 I8 image for its 16 strips.
        const char* const titleTexture =
            "__OTR__textures/nintendo_rogo_static/nintendo_rogo_static_Tex_000000";
        const uint8_t* pixels = static_cast<const uint8_t*>(ResourceGetDataByName(titleTexture));
        const size_t size = ResourceGetSizeByName(titleTexture);
        const uint32_t width = ResourceGetTexWidthByName(titleTexture);
        const uint32_t height = ResourceGetTexHeightByName(titleTexture);
        snprintf(line, sizeof(line), "gfx-process: archive title I8 %ux%u bytes=%u",
                 static_cast<unsigned>(width), static_cast<unsigned>(height),
                 static_cast<unsigned>(size));
        oot_ps3_early_trace(line);
        if (rectCount == 16 && pixels != nullptr && size <= UINT32_MAX &&
            oot_ps3_shipwright_video_present_title_texture(
                rects, static_cast<uint32_t>(rectCount), pixels,
                static_cast<uint32_t>(size), width, height)) {
            oot_ps3_early_trace("gfx-process: archive title texture presented");
        } else {
            oot_ps3_early_trace("gfx-process: archive texture unavailable; geometry preview retained");
            oot_ps3_early_trace("gfx-process: presenting title geometry preview");
            oot_ps3_shipwright_video_present_title_layout(rects, static_cast<uint32_t>(rectCount));
            oot_ps3_early_trace("gfx-process: title geometry preview submitted");
        }
        // The screenshot hold is brief because Graph now presents three
        // consecutive title frames before its own deliberate exit.
        usleep(800000U);
    }
    ++gFrame;
    oot_ps3_early_trace("gfx-process: completed");
}

float OTRGetAspectRatio(void) { return oot_ps3_display_aspect(); }
float OTRGetDimensionFromLeftEdge(float value) { return 160.0f - 120.0f * OTRGetAspectRatio() + value; }
float OTRGetDimensionFromRightEdge(float value) { return 160.0f + 120.0f * OTRGetAspectRatio() - (320.0f - value); }
int16_t OTRGetRectDimensionFromLeftEdge(float value) {
    return static_cast<int16_t>(floorf(OTRGetDimensionFromLeftEdge(value)));
}
int16_t OTRGetRectDimensionFromRightEdge(float value) {
    return static_cast<int16_t>(ceilf(OTRGetDimensionFromRightEdge(value)));
}
uint32_t OTRGetGameRenderWidth(void) { return oot_ps3_shipwright_video_width(); }
uint32_t OTRGetGameRenderHeight(void) { return oot_ps3_shipwright_video_height(); }
void OTRGetPixelDepthPrepare(float x, float y) { (void)x; (void)y; }
uint16_t OTRGetPixelDepth(float x, float y) { (void)x; (void)y; return 0xFFFFU; }

void Ctx_ReadSaveFile(uintptr_t address, void* destination, size_t size) { ReadAt(address, destination, size); }
void Ctx_WriteSaveFile(uintptr_t address, void* source, size_t size) { WriteAt(address, source, size); }

void Save_Init(void) {
    for (int slot = 0; slot < 3; ++slot) LoadMeta(slot);
    char line[96];
    snprintf(line, sizeof(line), "trace: save slots found=%d%d%d", gSaveMeta[0].valid, gSaveMeta[1].valid,
             gSaveMeta[2].valid);
    oot_ps3_early_trace(line);
}
void Save_InitFile(int isDebug) {
    // Opening_SetupTitleScreen calls Sram_InitDebugSave with fileNum 0xFF.
    // The desktop SaveManager's title save selects the Hyrule Field entrance;
    // leaving it at zero sent the PS3 probe to Dodongo's Cavern instead.
    if (isDebug && gSaveContext.fileNum == 0xFF &&
        gSaveContext.gameMode == GAMEMODE_TITLE_SCREEN) {
        gSaveContext.entranceIndex = ENTR_HYRULE_FIELD_PAST_BRIDGE_SPAWN;
        gSaveContext.horseData.scene = SCENE_HYRULE_FIELD;
        gSaveContext.horseData.pos.x = -1840;
        gSaveContext.horseData.pos.y = 72;
        gSaveContext.horseData.pos.z = 5497;
        gSaveContext.horseData.angle = -0x6AD9;
        // The title-screen debug save equips adult Link: Kokiri tunic and boots,
        // Hylian Shield, Master Sword (packed nibbles boots|tunic|shield|sword).
        // Left at zero, the tunic index became -1 and read a garbage color.
        gSaveContext.equips.equipment = 0x1122;
        gSaveContext.equips.buttonItems[0] = ITEM_SWORD_MASTER;
        // The title save never set the time of day, so it stayed at midnight (0):
        // the moon then sits straight overhead and never enters the intro's shots.
        // ~4:00 AM puts it ~30 degrees above the western horizon, where the
        // opening camera looks, while the scene lighting stays "night".
        gSaveContext.dayTime = 0x2AAB;
        oot_ps3_early_trace("intro-route: title save equipment set (Kokiri tunic, Hylian Shield, Master Sword)");
        oot_ps3_early_trace("intro-route: real Hyrule Field title entrance selected");
        return;
    }
    InitFileNormal(); // SoH's debug file is a developer option; PS3 creates normal files
    oot_ps3_early_trace("trace: save new file initialized");
}
void Save_LoadFile(void) {
    const int slot = gSaveContext.fileNum;
    TraceSave("load", slot, ReadSlot(slot, &gSaveContext));
}
void Save_SaveFile(void) {
    const int slot = gSaveContext.fileNum;
    const bool ok = WriteSlot(slot, gSaveContext);
    if (ok) BuildMeta(slot, gSaveContext);
    TraceSave("write", slot, ok);
}
void Save_SaveGlobal(void) {}
void Save_SaveSection(int sectionId) { (void)sectionId; Save_SaveFile(); }
void Save_CopyFile(int from, int to) {
    if (from < 0 || from >= 3 || to < 0 || to >= 3) return;
    const bool ok = ReadSlot(from, &sSlotScratch) && WriteSlot(to, sSlotScratch);
    if (ok) BuildMeta(to, sSlotScratch);
    TraceSave("copy to", to, ok);
}
void Save_DeleteFile(int slot) {
    if (slot < 0 || slot >= 3) return;
    const bool ok = EraseSlot(slot);
    memset(&gSaveMeta[slot], 0, sizeof(gSaveMeta[slot]));
    TraceSave("delete", slot, ok);
}
u32 Save_Exist(int slot) { return slot >= 0 && slot < 3 && gSaveMeta[slot].valid; }
SaveFileMetaInfo* Save_GetSaveMetaInfo(int slot) {
    if (slot < 0 || slot >= 3) slot = 0;
    return &gSaveMeta[slot];
}
void SaveManager_ThreadPoolWait(void) {}

void GfxDebuggerDebugDisplayList(void*) {}
bool GfxDebuggerIsDebugging(void) { return false; }
bool GfxDebuggerIsDebuggingRequested(void) { return false; }
void Gfx_RegisterBlendedTexture(const char*, u8*, u8*) {}
void Gfx_UnregisterBlendedTexture(const char*) {}
void Gfx_TextureCacheDelete(const uint8_t*) {}

void OTRControllerCallback(uint8_t rumble) {
    (void)rumble; // Native pad polling/rumble lives in shipwright_os_ps3.c.
}
void ProcessSaveStateRequests(void) {}
void SetAudioChannels(AudioChannelsSetting channels) { (void)channels; }
void Messagebox_ShowErrorBox(char* title, char* body) {
    fprintf(stderr, "PS3 error: %s: %s\n", title != nullptr ? title : "", body != nullptr ? body : "");
}

OSPiHandle* osCartRomInit(void) {
    static OSPiHandle handle = {};
    return &handle;
}

u16 gBuildVersionMajor = 0;
u16 gBuildVersionMinor = 1;
u8 gBuildTeam[] = { 'O', 'o', 'T', ' ', 'P', 'S', '3', ' ', 'p', 'o', 'r', 't', 0 };
u8 gBuildDate[] = { '2', '0', '2', '6', '-', '0', '9', '-', '2', '1', 0 };
u8 gBuildMakeOption[] = { 'n', 'a', 't', 'i', 'v', 'e', '-', 'b', 'o', 'o', 't', 's', 't', 'r', 'a', 'p', 0 };

} // extern "C"
