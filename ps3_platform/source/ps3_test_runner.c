// SPDX-License-Identifier: GPL-3.0-or-later
/* Debug-menu test cases and the automatic test run.
 *
 * Every reported problem gets a test case: it sets up the situation (area, age,
 * cutscene, items, flags), runs a short timed script (screenshots, actions such as
 * putting Link next to the blue warp), switches on the probes that explain the problem
 * (texture watch in the renderer, fade colours, Link's face, masks, memory) and writes a
 * report section with the captured lines, the measured numbers and a verdict.
 *
 *   "Test cases..." -> X on one test  : that test, report + screenshots
 *   "Run ALL tests"                    : every test in order, one report for the run
 *
 * Output: USRDIR/tests/run_<UTC time>/report.txt + NN_<test>_<k>.png, copied to
 * <USB>/ShipOfGadeguion-tests/run_<UTC time>/ when a USB drive is plugged in.
 * The main log gets "trace: test ..." lines too.
 *
 * Rule (2026-11-21): every fix gets a test case here, including the problems whose
 * cause isn't known yet (with the probes that will explain them). */

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "global.h"

void oot_ps3_early_trace(const char* message);
void oot_ps3_lock_backoff(unsigned int* spins);
int oot_ps3_usb_root(char* out, unsigned int size);
unsigned long long oot_ps3_microseconds_now(void);
int oot_ps3_debug_request_shot(const char* hddPath, const char* usbPath); /* ps3_debug_overlay.c */
int oot_ps3_debug_shot_busy(void);
void oot_ps3_fast3d_watch_texture(const char* text);                  /* interpreter.cpp */
void oot_ps3_ui_hint(const char* title, const char* text, unsigned int frames); /* ps3_game_menu.c */
int oot_ps3_debug_queue_test(int index, int entrance, int age, const char* name); /* ps3_debug_game.c */
int oot_ps3_debug_queue_warp_to(int entrance, const char* name);
int oot_ps3_debug_request_pending(void);
unsigned int oot_ps3_heap_used_kb(void);
unsigned int oot_ps3_heap_size_kb(void);
unsigned int oot_ps3_system_free_kb(void);
int oot_ps3_tnl_requested(void);
int oot_ps3_render_thread_enabled(void);
int oot_ps3_frame_mode(void);
int oot_ps3_tri_fast_enabled(void);
int oot_ps3_trace_enabled(void);
void oot_ps3_trace_set_enabled(int on);
extern void (*volatile gOotPs3TraceTap)(const char* message); /* ps3_early_entry.c */
extern unsigned int gOotPs3Fast3dTextureIds;                   /* fast3d_video_ps3.cpp */
extern int gOotPs3Fast3dTextureIdsExhausted;

int gOotPs3TestProbeFade; /* z_fbdemo_fade.c / z_kankyo.c colour probes */
int gOotPs3TestActive;    /* other probes may log more while a test runs */

#define USRDIR "/dev_hdd0/game/OOTP30048/USRDIR"

/* ---- test table ---- */
enum { TS_PLAIN, TS_GOHMA_FIGHT, TS_GOHMA_WARP, TS_DODONGO_FIGHT, TS_DUNGEON_KID, TS_TOUR };
enum { PR_MASKS = 1, PR_FADE = 2, PR_WARP = 4, PR_FACE = 8, PR_MINIMAP = 16, PR_MEMORY = 32 };
enum { AT_MS, AT_CS };                       /* when: ms after the area loaded / cutscene frame */
enum { ACT_SHOT, ACT_TELEPORT_WARP, ACT_STATE, ACT_END, ACT_PLACE_LINK };
typedef struct {
    u8 when;
    u16 at;
    u8 action;
} Ps3TestStep;
#define STEPS_MAX 10
typedef struct {
    const char* name;  /* "#issue short description" */
    const char* slug;  /* file names */
    const char* check; /* what to look at (banner + report) */
    s16 entrance;
    u16 cutsceneIndex; /* 0 = none (the entrance's first-visit cutscenes count as seen) */
    s8 age;
    u8 setup;
    s8 transitionIn;   /* fade into the area, -1 = the entrance's own */
    u8 probes;
    const char* watchTexture; /* renderer texture watch, NULL = none */
    Ps3TestStep steps[STEPS_MAX];
    f32 place[3];             /* ACT_PLACE_LINK target (where the scene's trigger is) */
} Ps3TestCase;

static const Ps3TestCase kPs3TestList[] = {
    { "#14 Queen Gohma: boss textures (fight)", "gohma",
      "Gohma's body, shell and eye must be textured (no black).",
      ENTR_DEKU_TREE_BOSS_ENTRANCE, 0, LINK_AGE_CHILD, TS_GOHMA_FIGHT, -1, PR_MASKS, "object_goma",
      { { AT_MS, 1500, ACT_PLACE_LINK }, { AT_MS, 6000, ACT_SHOT }, { AT_MS, 12000, ACT_SHOT },
        { AT_MS, 18000, ACT_SHOT }, { AT_MS, 24000, ACT_SHOT }, { AT_MS, 30000, ACT_SHOT }, { AT_MS, 32000, ACT_END } },
      { 150.0f, -640.0f, 350.0f } /* z_boss_goma.c encounter: |x-150| < 60, |z-350| < 60 */ },
    { "#14 King Dodongo: boss textures (fight)", "dodongo",
      "King Dodongo and the lava floor must be textured (no black).",
      ENTR_DODONGOS_CAVERN_BOSS_ENTRANCE, 0, LINK_AGE_CHILD, TS_DODONGO_FIGHT, -1, PR_MASKS, "object_kingdodongo",
      { { AT_MS, 1500, ACT_PLACE_LINK }, { AT_MS, 6000, ACT_SHOT }, { AT_MS, 12000, ACT_SHOT },
        { AT_MS, 18000, ACT_SHOT }, { AT_MS, 24000, ACT_SHOT }, { AT_MS, 26000, ACT_END } },
      { -890.0f, -1100.0f, -2804.0f } /* drop into the arena: the intro starts below y -1223.76 */ },
    { "#7 Blue warp after Queen Gohma", "bluewarp",
      "Gohma beaten: the blue warp must glow in the middle of the room.",
      ENTR_DEKU_TREE_BOSS_ENTRANCE, 0, LINK_AGE_CHILD, TS_GOHMA_WARP, -1, PR_WARP, "gWarpPortalTex",
      { { AT_MS, 3000, ACT_SHOT }, { AT_MS, 5000, ACT_TELEPORT_WARP }, { AT_MS, 7000, ACT_SHOT },
        { AT_MS, 9000, ACT_STATE }, { AT_MS, 10000, ACT_SHOT }, { AT_MS, 11000, ACT_END } } },
    { "#9 Deku Tree's first words (gray screen)", "graytext",
      "The Deku Tree's first line must be shown over GRAY, not reddish.",
      ENTR_KOKIRI_FOREST_0, 0xFFF3, LINK_AGE_CHILD, TS_PLAIN, TRANS_TYPE_FADE_WHITE_CS_DELAYED, PR_FADE, NULL,
      { { AT_MS, 1500, ACT_SHOT }, { AT_MS, 4000, ACT_SHOT }, { AT_MS, 7000, ACT_SHOT }, { AT_MS, 10000, ACT_SHOT },
        { AT_MS, 12000, ACT_END } } },
    { "#8 Link asleep in bed (eyes)", "eyes",
      "Watch Link's eyes while he sleeps (compare with SoH on PC).",
      ENTR_LINKS_HOUSE_CHILD_SPAWN, 0xFFF0, LINK_AGE_CHILD, TS_PLAIN, -1, PR_FACE, "Eyes",
      { { AT_CS, 60, ACT_SHOT }, { AT_CS, 150, ACT_SHOT }, { AT_CS, 260, ACT_SHOT }, { AT_CS, 400, ACT_SHOT },
        { AT_CS, 560, ACT_SHOT }, { AT_CS, 600, ACT_END }, { AT_MS, 45000, ACT_END } } },
    { "#12 Minimap in Dodongo's Cavern", "minimap",
      "The blue minimap (bottom right) must be visible.",
      ENTR_DODONGOS_CAVERN_ENTRANCE, 0, LINK_AGE_CHILD, TS_DUNGEON_KID, -1, PR_MINIMAP, "MinimapTex",
      { { AT_MS, 3000, ACT_SHOT }, { AT_MS, 6000, ACT_SHOT }, { AT_MS, 7000, ACT_END } } },
    { "#12/#13 Memory tour: 25 areas, then the minimap", "tour",
      "Visits many areas (~5 s each) like a long session. Watch for missing textures.",
      ENTR_KOKIRI_FOREST_0, 0, LINK_AGE_CHILD, TS_TOUR, -1, PR_MEMORY | PR_MINIMAP, "MinimapTex",
      { { AT_MS, 60000, ACT_END } } /* steps unused: the tour drives itself */ },
};
#define PS3_TEST_COUNT ((int)ARRAY_COUNT(kPs3TestList))

static const s16 kPs3TourList[] = {
    ENTR_KOKIRI_FOREST_0, ENTR_LOST_WOODS_SOUTH_EXIT, ENTR_SACRED_FOREST_MEADOW_SOUTH_EXIT,
    ENTR_HYRULE_FIELD_PAST_BRIDGE_SPAWN, ENTR_LON_LON_RANCH_ENTRANCE, ENTR_MARKET_ENTRANCE_NORTH_EXIT,
    ENTR_MARKET_SOUTH_EXIT, ENTR_CASTLE_GROUNDS_SOUTH_EXIT, ENTR_KAKARIKO_VILLAGE_FRONT_GATE,
    ENTR_GRAVEYARD_ENTRANCE, ENTR_DEATH_MOUNTAIN_TRAIL_BOTTOM_EXIT, ENTR_GORON_CITY_UPPER_EXIT,
    ENTR_DEATH_MOUNTAIN_CRATER_UPPER_EXIT, ENTR_ZORAS_RIVER_WEST_EXIT, ENTR_ZORAS_DOMAIN_ENTRANCE,
    ENTR_LAKE_HYLIA_NORTH_EXIT, ENTR_GERUDO_VALLEY_EAST_EXIT, ENTR_GERUDOS_FORTRESS_EAST_EXIT,
    ENTR_DESERT_COLOSSUS_EAST_EXIT, ENTR_DEKU_TREE_ENTRANCE, ENTR_JABU_JABU_ENTRANCE,
    ENTR_FOREST_TEMPLE_ENTRANCE, ENTR_FIRE_TEMPLE_ENTRANCE, ENTR_WATER_TEMPLE_ENTRANCE,
    ENTR_DODONGOS_CAVERN_ENTRANCE, /* last: the minimap check after everything above */
};
#define PS3_TOUR_STEPS ((int)ARRAY_COUNT(kPs3TourList))

int oot_ps3_debug_test_count(void) { return PS3_TEST_COUNT; }
const char* oot_ps3_debug_test_name(int index) {
    return index >= 0 && index < PS3_TEST_COUNT ? kPs3TestList[index].name : "";
}
const char* oot_ps3_debug_test_check(int index) {
    return index >= 0 && index < PS3_TEST_COUNT ? kPs3TestList[index].check : "";
}
int Ps3Test_CutsceneIndex(int index) {
    return index >= 0 && index < PS3_TEST_COUNT ? kPs3TestList[index].cutsceneIndex : 0;
}

/* ---- report buffer + trace tap (any thread) ---- */
static volatile unsigned int sTapLock;
static char sReport[128 * 1024];
static unsigned int sReportLen;
static unsigned long long sTestT0; /* test start (us) for the [+s] stamps */
static unsigned int sCapturedLines, sDroppedLines;

static void Lock(void) {
    unsigned int spins = 0;
    while (__sync_lock_test_and_set(&sTapLock, 1U)) oot_ps3_lock_backoff(&spins);
}
static void Unlock(void) { __sync_lock_release(&sTapLock); }

static void ReportRaw(const char* text) {
    unsigned int n = 0;
    while (text[n] != '\0') ++n;
    if (sReportLen + n + 1 >= sizeof(sReport)) return;
    memcpy(sReport + sReportLen, text, n);
    sReportLen += n;
    sReport[sReportLen] = '\0';
}
static void Report(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void Report(const char* fmt, ...) {
    char line[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    Lock();
    ReportRaw(line);
    ReportRaw("\n");
    Unlock();
}

/* Numbers the probes measure during one test (filled by the tap). */
static struct {
    unsigned int masks, maskFails;
    unsigned int fadeGray, fadeOther;
    char fadeOtherExample[48];
    unsigned int watchLines, watchTris, watchGpu, watchCpu, watchRejected, watchCulled, watchWneg;
    unsigned int watchAlphaZero;
    unsigned int warpDrawLines;
    unsigned int faceLines, faceClosed, faceAnimNonZero;
    unsigned int errors, idsExhausted, memWarnings;
    unsigned int memLines, heapMinKb, heapMaxKb;
    unsigned int shotsOk, shotsFailed;
} sM;

static unsigned int ParseU(const char* text, const char* key) {
    const char* p = strstr(text, key);
    return p ? (unsigned int)strtoul(p + strlen(key), NULL, 10) : 0U;
}

static int WantLine(const char* m) {
    static const char* const kAlways[] = {
        "trace: test", "trace: debug menu", "graph: Play mask texture composite", "fast3d-rsx: mask composite upload failed",
        "graph: Play warp draw", "graph: Play link face", "graph: Play memory", "fast3d texture ids exhausted",
        "graph: Play state", "rsx-watchdog", "trace: resource missing", "graph: Play heap-guard",
    };
    unsigned int i;
    for (i = 0; i < ARRAY_COUNT(kAlways); ++i)
        if (strncmp(m, kAlways[i], strlen(kAlways[i])) == 0) return 1;
    return strstr(m, "FAILED") != NULL || strstr(m, "BAD ") != NULL;
}

static void Tap(const char* m) {
    char stamp[24];
    const unsigned long long now = oot_ps3_microseconds_now();
    if (!WantLine(m)) return;
    Lock();
    /* measurements */
    if (strncmp(m, "graph: Play mask texture composite", 34) == 0) ++sM.masks;
    else if (strstr(m, "mask composite upload failed")) ++sM.maskFails;
    else if (strncmp(m, "trace: test-probe fade", 22) == 0 || strncmp(m, "trace: test-probe fill", 22) == 0) {
        const char* p = strstr(m, "rgba=");
        unsigned int r = 0, g = 0, b = 0, a = 0;
        if (p && sscanf(p + 5, "%u,%u,%u,%u", &r, &g, &b, &a) == 4 && a > 0) {
            if (r == g && g == b) ++sM.fadeGray;
            else {
                if (sM.fadeOther++ == 0) snprintf(sM.fadeOtherExample, sizeof(sM.fadeOtherExample), "%u,%u,%u,%u", r, g, b, a);
            }
        }
    } else if (strncmp(m, "trace: test-probe tex", 21) == 0) {
        const char* p = strstr(m, "prim=");
        unsigned int pr = 0, pg = 0, pb = 0, pa = 255;
        ++sM.watchLines;
        sM.watchTris += ParseU(m, "tris=");
        sM.watchGpu += ParseU(m, "gpu=");
        sM.watchCpu += ParseU(m, "cpu=");
        sM.watchRejected += ParseU(m, "rejected=");
        sM.watchCulled += ParseU(m, "culled=");
        sM.watchWneg += ParseU(m, "wneg=");
        if (p && sscanf(p + 5, "%u,%u,%u,%u", &pr, &pg, &pb, &pa) == 4 && pa == 0) ++sM.watchAlphaZero;
    } else if (strncmp(m, "graph: Play warp draw", 21) == 0) ++sM.warpDrawLines;
    else if (strncmp(m, "graph: Play link face", 21) == 0) {
        ++sM.faceLines;
        if (ParseU(m, "eye=") == 2) ++sM.faceClosed;
        if (strstr(m, "anim=0 ") == NULL) ++sM.faceAnimNonZero;
    } else if (strstr(m, "texture ids exhausted")) ++sM.idsExhausted;
    else if (strncmp(m, "graph: Play memory", 18) == 0) {
        const unsigned int heap = ParseU(m, "heap=");
        if (strstr(m, "memory warning")) ++sM.memWarnings;
        ++sM.memLines;
        if (heap) {
            if (sM.heapMinKb == 0 || heap < sM.heapMinKb) sM.heapMinKb = heap;
            if (heap > sM.heapMaxKb) sM.heapMaxKb = heap;
        }
    } else if (strncmp(m, "trace: test shot", 16) == 0) {
        if (strstr(m, "FAILED")) ++sM.shotsFailed; else ++sM.shotsOk;
    }
    if (strstr(m, "FAILED") || strstr(m, "BAD ") || strncmp(m, "rsx-watchdog", 12) == 0) ++sM.errors;
    /* the line itself */
    if (sCapturedLines < 600) {
        snprintf(stamp, sizeof(stamp), "  [+%6.2f s] ", (double)(now - sTestT0) / 1000000.0);
        ReportRaw(stamp);
        ReportRaw(m);
        ReportRaw("\n");
        ++sCapturedLines;
    } else {
        ++sDroppedLines;
    }
    Unlock();
}

/* ---- files ---- */
static char sRunDir[96], sUsbRunDir[160];
static unsigned int sRunNumber;

static void AppendFile(const char* path, const char* data, unsigned int size) {
    const int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0) return;
    while (size > 0) {
        const ssize_t w = write(fd, data, size);
        if (w <= 0) break;
        data += w;
        size -= (unsigned int)w;
    }
    close(fd);
}

/* Writes what the report buffer holds to report.txt (HDD and USB) and empties it. */
static void FlushReport(void) {
    char path[200];
    static char copy[sizeof(sReport)];
    unsigned int len;
    Lock();
    len = sReportLen;
    memcpy(copy, sReport, len);
    sReportLen = 0;
    sReport[0] = '\0';
    Unlock();
    if (len == 0) return;
    snprintf(path, sizeof(path), "%s/report.txt", sRunDir);
    AppendFile(path, copy, len);
    if (sUsbRunDir[0]) {
        snprintf(path, sizeof(path), "%s/report.txt", sUsbRunDir);
        AppendFile(path, copy, len);
    }
}

static void MakeRunDirs(void) {
    char usb[32];
    time_t now = time(0);
    struct tm parts;
    char stamp[32];
    if (now > 0 && gmtime_r(&now, &parts) != 0)
        snprintf(stamp, sizeof(stamp), "%04d%02d%02d-%02d%02d%02d", parts.tm_year + 1900, parts.tm_mon + 1,
                 parts.tm_mday, parts.tm_hour, parts.tm_min, parts.tm_sec);
    else
        snprintf(stamp, sizeof(stamp), "n%u", sRunNumber);
    ++sRunNumber;
    (void)mkdir(USRDIR "/tests", 0777);
    snprintf(sRunDir, sizeof(sRunDir), USRDIR "/tests/run_%s", stamp);
    (void)mkdir(sRunDir, 0777);
    sUsbRunDir[0] = '\0';
    if (oot_ps3_usb_root(usb, sizeof(usb))) {
        char base[64];
        snprintf(base, sizeof(base), "%s/ShipOfGadeguion-tests", usb);
        (void)mkdir(base, 0777);
        snprintf(sUsbRunDir, sizeof(sUsbRunDir), "%s/run_%s", base, stamp);
        if (mkdir(sUsbRunDir, 0777) != 0) {
            struct stat st;
            if (stat(sUsbRunDir, &st) != 0) sUsbRunDir[0] = '\0';
        }
    }
}

/* ---- runner state ---- */
enum { RS_IDLE, RS_QUEUED, RS_RUNNING };
static int sRunState = RS_IDLE;
static int sRunList[32], sRunCount, sRunPos, sRunAll;
static int sCur = -1;           /* test running */
static s16 sCurScene = -1;      /* scene it waits for */
static int sArrived, sStep, sShotNo;
static unsigned long long sQueuedT0, sArriveT0;
static int sTourStep;
static unsigned long long sTourT0;
static int sTourShot;
static char sVerdicts[32][200];
static unsigned int sHeapAtRunStart;

int oot_ps3_test_running(void) { return sRunState != RS_IDLE; }

static const char* ModeName(int mode) {
    static const char* const kNames[] = { "60", "unlocked", "20", "30 steady", "auto" };
    return mode >= 0 && mode < 5 ? kNames[mode] : "?";
}

static void ReportSettings(void) {
    Report("settings: GPU vertex path %s, render thread %s, frame mode %s, triangle fast path %s, logs %s",
           oot_ps3_tnl_requested() ? "ON" : "OFF", oot_ps3_render_thread_enabled() ? "ON" : "OFF",
           ModeName(oot_ps3_frame_mode()), oot_ps3_tri_fast_enabled() ? "ON" : "OFF", oot_ps3_trace_enabled() ? "ON" : "OFF");
    Report("memory: heap %u of %u KB, system free %u KB, texture ids %u%s", oot_ps3_heap_used_kb(), oot_ps3_heap_size_kb(),
           oot_ps3_system_free_kb(), gOotPs3Fast3dTextureIds, gOotPs3Fast3dTextureIdsExhausted ? " (EXHAUSTED)" : "");
}

static void Shot(const char* tag) {
    char hdd[200], usb[240], line[160];
    const Ps3TestCase* t = &kPs3TestList[sCur];
    ++sShotNo;
    snprintf(hdd, sizeof(hdd), "%s/%02d_%s_%s%d.png", sRunDir, sRunPos + 1, t->slug, tag, sShotNo);
    usb[0] = '\0';
    if (sUsbRunDir[0]) snprintf(usb, sizeof(usb), "%s/%02d_%s_%s%d.png", sUsbRunDir, sRunPos + 1, t->slug, tag, sShotNo);
    if (!oot_ps3_debug_request_shot(hdd, usb)) {
        snprintf(line, sizeof(line), "trace: test shot %d skipped (previous one still being written)", sShotNo);
        oot_ps3_early_trace(line);
    }
}

static void StartTest(int listPos) {
    const Ps3TestCase* t;
    char line[200];
    sRunPos = listPos;
    sCur = sRunList[listPos];
    t = &kPs3TestList[sCur];
    memset(&sM, 0, sizeof(sM));
    sCapturedLines = sDroppedLines = 0;
    sArrived = 0;
    sStep = 0;
    sShotNo = 0;
    sTestT0 = sQueuedT0 = oot_ps3_microseconds_now();
    Report("");
    Report("=== TEST %d/%d: %s ===", listPos + 1, sRunCount, t->name);
    Report("check: %s", t->check);
    ReportSettings();
    gOotPs3TestProbeFade = (t->probes & PR_FADE) != 0;
    gOotPs3TestActive = 1;
    oot_ps3_fast3d_watch_texture(t->watchTexture ? t->watchTexture : "");
    if (t->watchTexture) Report("probe: renderer texture watch on \"%s\"", t->watchTexture);
    snprintf(line, sizeof(line), "trace: test %d/%d started: %s", listPos + 1, sRunCount, t->name);
    oot_ps3_early_trace(line);
    sTourStep = -1;
    if (!oot_ps3_debug_queue_test(sCur, t->entrance, t->age, t->name)) {
        Report("result: NOT STARTED (no game state)");
    }
    sRunState = RS_QUEUED;
}

static void Verdict(char* out, unsigned int size) {
    const Ps3TestCase* t = &kPs3TestList[sCur];
    char part[200];
    out[0] = '\0';
    if (!sArrived) {
        snprintf(out, size, "TIMEOUT: the area never loaded (see the 'debug menu request' lines above)");
        return;
    }
    if (t->probes & PR_MASKS) {
        if (sM.maskFails) snprintf(part, sizeof(part), "FAIL: %u mask composite uploads failed", sM.maskFails);
        else if (sM.masks) snprintf(part, sizeof(part), "PASS: masked textures composed (%u); check the shots", sM.masks);
        else snprintf(part, sizeof(part), "CHECK: no masked texture drawn (boss never on screen?)");
        snprintf(out + strlen(out), size - strlen(out), "%s%s", out[0] ? " | " : "", part);
    }
    if (t->probes & PR_FADE) {
        if (sM.fadeOther) snprintf(part, sizeof(part), "FAIL: non-gray fade colour seen %u times (e.g. %s)", sM.fadeOther, sM.fadeOtherExample);
        else if (sM.fadeGray) snprintf(part, sizeof(part), "PASS: every fade/fill colour gray (%u)", sM.fadeGray);
        else snprintf(part, sizeof(part), "NO DATA: no fade or fill was drawn");
        snprintf(out + strlen(out), size - strlen(out), "%s%s", out[0] ? " | " : "", part);
    }
    if (t->probes & PR_WARP) {
        if (sM.warpDrawLines == 0) snprintf(part, sizeof(part), "EXPLAINED: the warp actor never drew (not spawned?)");
        else if (sM.watchTris == 0) snprintf(part, sizeof(part), "EXPLAINED: warp drew but no warp-textured triangle reached the renderer");
        else if (sM.watchRejected >= sM.watchTris) snprintf(part, sizeof(part), "EXPLAINED: all %u warp triangles clip-rejected (off screen / bad matrix)", sM.watchTris);
        else if (sM.watchCulled * 10U >= sM.watchTris * 9U) snprintf(part, sizeof(part), "EXPLAINED: %u of %u warp triangles back-face culled", sM.watchCulled, sM.watchTris);
        else if (sM.watchAlphaZero && sM.watchAlphaZero == sM.watchLines) snprintf(part, sizeof(part), "EXPLAINED: warp drawn with primitive alpha 0");
        else snprintf(part, sizeof(part), "DRAWN: %u warp triangles reach the renderer (gpu %u, cpu %u, culled %u, w<0 %u); compare shots",
                      sM.watchTris, sM.watchGpu, sM.watchCpu, sM.watchCulled, sM.watchWneg);
        snprintf(out + strlen(out), size - strlen(out), "%s%s", out[0] ? " | " : "", part);
    }
    if (t->probes & PR_FACE) {
        if (sM.faceLines == 0) snprintf(part, sizeof(part), "NO DATA: no face changes logged in the cutscene");
        else snprintf(part, sizeof(part), "INFO: eyes closed in %u of %u face changes; animation face value %s", sM.faceClosed,
                      sM.faceLines, sM.faceAnimNonZero ? "SET (animation picks the eyes)" : "always 0 (blinking picks the eyes)");
        snprintf(out + strlen(out), size - strlen(out), "%s%s", out[0] ? " | " : "", part);
    }
    if (t->probes & PR_MINIMAP) {
        if (sM.idsExhausted) snprintf(part, sizeof(part), "FAIL: texture ids ran out");
        else if (sM.watchTris > sM.watchRejected) snprintf(part, sizeof(part), "PASS: minimap drawn (%u triangles)", sM.watchTris);
        else snprintf(part, sizeof(part), "FAIL: minimap texture never drawn");
        snprintf(out + strlen(out), size - strlen(out), "%s%s", out[0] ? " | " : "", part);
    }
    if (t->probes & PR_MEMORY) {
        const unsigned int size_kb = oot_ps3_heap_size_kb(), used = oot_ps3_heap_used_kb();
        snprintf(part, sizeof(part), "%s: heap %u -> %u KB of %u (%u%%), %u area reports, warnings %u, texture ids %u",
                 used >= size_kb / 100U * 94U ? "FAIL" : "PASS", sM.heapMinKb ? sM.heapMinKb : used, used, size_kb,
                 size_kb ? used * 100U / size_kb : 0U, sM.memLines, sM.memWarnings, gOotPs3Fast3dTextureIds);
        snprintf(out + strlen(out), size - strlen(out), "%s%s", out[0] ? " | " : "", part);
    }
    if (sM.errors) snprintf(out + strlen(out), size - strlen(out), " | %u error lines (see above)", sM.errors);
    if (out[0] == '\0') snprintf(out, size, "DONE: check the shots");
}

static void EndTest(const char* why) {
    char line[260];
    const unsigned long long now = oot_ps3_microseconds_now();
    gOotPs3TestProbeFade = 0;
    oot_ps3_fast3d_watch_texture("");
    Verdict(sVerdicts[sRunPos], sizeof(sVerdicts[sRunPos]));
    if (sDroppedLines) Report("  ... %u more lines not copied (see the main log)", sDroppedLines);
    Report("end: %s after %.1f s, %d screenshots (%u ok, %u failed)", why, (double)(now - sTestT0) / 1000000.0, sShotNo,
           sM.shotsOk, sM.shotsFailed);
    ReportSettings();
    Report("RESULT: %s", sVerdicts[sRunPos]);
    FlushReport();
    snprintf(line, sizeof(line), "trace: test %d/%d result: %s", sRunPos + 1, sRunCount, sVerdicts[sRunPos]);
    oot_ps3_early_trace(line);
    if (sRunPos + 1 < sRunCount) {
        StartTest(sRunPos + 1);
        return;
    }
    /* run finished */
    {
        int i;
        Report("");
        Report("=== SUMMARY (%d tests) ===", sRunCount);
        for (i = 0; i < sRunCount; ++i) Report("%2d. %-48s %s", i + 1, kPs3TestList[sRunList[i]].name, sVerdicts[i]);
        Report("heap at run start %u KB, now %u KB of %u KB", sHeapAtRunStart, oot_ps3_heap_used_kb(), oot_ps3_heap_size_kb());
        FlushReport();
    }
    gOotPs3TraceTap = 0;
    gOotPs3TestActive = 0;
    sRunState = RS_IDLE;
    sCur = -1;
    snprintf(line, sizeof(line), "Report: USRDIR/tests/%s/report.txt%s", strrchr(sRunDir, '/') + 1,
             sUsbRunDir[0] ? " (and on the USB drive)" : "");
    oot_ps3_ui_hint(sRunAll ? "All tests finished" : "Test finished", line, 600);
    oot_ps3_early_trace("trace: test run finished");
}

static void StartRun(const int* list, int count, int all) {
    char line[160];
    int i;
    if (count <= 0) return;
    if (!oot_ps3_trace_enabled()) oot_ps3_trace_set_enabled(1); /* the main log too */
    for (i = 0; i < count && i < (int)ARRAY_COUNT(sRunList); ++i) sRunList[i] = list[i];
    sRunCount = i;
    sRunAll = all;
    MakeRunDirs();
    sReportLen = 0;
    sHeapAtRunStart = oot_ps3_heap_used_kb();
    Report("Ship of Gadeguion test run (%s), EBOOT built %s %s", all ? "all tests" : "one test", __DATE__, __TIME__);
    Report("folder: %s%s%s", sRunDir, sUsbRunDir[0] ? "  usb: " : "", sUsbRunDir);
    Report("tests: %d. Each one sets up its scene, takes screenshots and records its probes.", sRunCount);
    gOotPs3TraceTap = Tap;
    snprintf(line, sizeof(line), "trace: test run started (%d tests) -> %s", sRunCount, sRunDir);
    oot_ps3_early_trace(line);
    StartTest(0);
}

int oot_ps3_debug_run_test(int index) {
    if (index < 0 || index >= PS3_TEST_COUNT || sRunState != RS_IDLE) return 0;
    StartRun(&index, 1, 0);
    return 1;
}

int oot_ps3_debug_run_all_tests(void) {
    int list[32], i;
    if (sRunState != RS_IDLE) return 0;
    for (i = 0; i < PS3_TEST_COUNT && i < 32; ++i) list[i] = i;
    StartRun(list, i, 1);
    return 1;
}

void oot_ps3_debug_stop_tests(void) {
    if (sRunState == RS_IDLE) return;
    Report("run stopped from the debug menu");
    sRunCount = sRunPos + 1; /* this test is the last one */
    EndTest("stopped");
}

/* ---- hooks from ps3_debug_game.c ---- */
void Ps3Test_BeforeLoad(int index) {
    const int setup = kPs3TestList[index].setup;
    if (kPs3TestList[index].cutsceneIndex != 0) gSaveContext.nextCutsceneIndex = kPs3TestList[index].cutsceneIndex;
    /* Gohma's arena is room 1 of her lair: its clear flag makes Boss_Goma spawn the warp. */
    if (setup == TS_GOHMA_WARP) gSaveContext.sceneFlags[SCENE_DEKU_TREE_BOSS].clear |= 1U << 1;
    if (setup == TS_GOHMA_FIGHT) gSaveContext.sceneFlags[SCENE_DEKU_TREE_BOSS].clear &= ~(1U << 1);
    if (setup == TS_DODONGO_FIGHT) gSaveContext.sceneFlags[SCENE_DODONGOS_CAVERN_BOSS].clear &= ~(1U << 1);
    sCurScene = gEntranceTable[kPs3TestList[index].entrance].scene;
}
void Ps3Test_AfterTransition(int index) {
    if (kPs3TestList[index].transitionIn >= 0) gSaveContext.nextTransitionType = (u8)kPs3TestList[index].transitionIn;
}

static void GiveKidKit(PlayState* play, int bombs) {
    Item_Give(play, ITEM_SWORD_KOKIRI);
    Item_Give(play, ITEM_SHIELD_DEKU);
    Item_Give(play, ITEM_SLINGSHOT);
    Item_Give(play, ITEM_NUT);
    Item_Give(play, ITEM_STICK);
    if (bombs) {
        Item_Give(play, ITEM_BOMB_BAG_20);
        Item_Give(play, ITEM_BOMBS_20);
    }
    Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, EQUIP_VALUE_SWORD_KOKIRI);
    Inventory_ChangeEquipment(EQUIP_TYPE_SHIELD, EQUIP_VALUE_SHIELD_DEKU);
    gSaveContext.equips.buttonItems[0] = ITEM_SWORD_KOKIRI;
    gSaveContext.equips.buttonItems[1] = ITEM_SLINGSHOT;
    gSaveContext.equips.cButtonSlots[0] = SLOT_SLINGSHOT;
    if (bombs) {
        gSaveContext.equips.buttonItems[2] = ITEM_BOMB;
        gSaveContext.equips.cButtonSlots[1] = SLOT_BOMB;
    }
    Interface_LoadItemIcon1(play, 1);
    if (bombs) Interface_LoadItemIcon1(play, 2);
    gSaveContext.healthCapacity = gSaveContext.health = 10 * 0x10;
    Player_SetEquipmentData(play, GET_PLAYER(play));
}

static Actor* FindActor(PlayState* play, s16 id) {
    s32 category;
    for (category = 0; category < ACTORCAT_MAX; category++) {
        Actor* actor = play->actorCtx.actorLists[category].head;
        s32 guard = 0;
        while (actor != NULL && guard++ < 512) {
            if (actor->id == id) return actor;
            actor = actor->next;
        }
    }
    return NULL;
}

static void LogState(PlayState* play, const char* why) {
    Player* link = GET_PLAYER(play);
    Report("  state (%s): scene %d room %d entrance %04X age %s Link (%.0f,%.0f,%.0f) cs %d/%d actors %d", why,
           (int)play->sceneNum, (int)play->roomCtx.curRoom.num, (unsigned)(u16)gSaveContext.entranceIndex,
           LINK_IS_ADULT ? "adult" : "child", link ? (double)link->actor.world.pos.x : 0.0,
           link ? (double)link->actor.world.pos.y : 0.0, link ? (double)link->actor.world.pos.z : 0.0,
           (int)play->csCtx.state, (int)play->csCtx.frames, (int)play->actorCtx.total);
}

/* Puts Link where the scene's trigger is (boss intro). */
static void PlaceLink(PlayState* play, const f32* pos) {
    Player* link = GET_PLAYER(play);
    if (link == NULL) return;
    link->actor.world.pos.x = pos[0];
    link->actor.world.pos.y = pos[1];
    link->actor.world.pos.z = pos[2];
    link->actor.prevPos = link->actor.world.pos;
    Report("  action: Link placed at (%.0f,%.0f,%.0f)", (double)pos[0], (double)pos[1], (double)pos[2]);
}

/* Puts Link in front of the blue warp, facing it, outside its pull radius. */
static void TeleportToWarp(PlayState* play) {
    Actor* warp = FindActor(play, ACTOR_DOOR_WARP1);
    Player* link = GET_PLAYER(play);
    if (warp == NULL || link == NULL) {
        Report("  action: teleport to the warp: NO WARP ACTOR in the room");
        return;
    }
    link->actor.world.pos.x = warp->world.pos.x;
    link->actor.world.pos.y = warp->world.pos.y;
    link->actor.world.pos.z = warp->world.pos.z + 260.0f;
    link->actor.prevPos = link->actor.home.pos = link->actor.world.pos;
    link->actor.shape.rot.y = link->actor.world.rot.y = link->yaw = Math_Vec3f_Yaw(&link->actor.world.pos, &warp->world.pos);
    Report("  action: Link moved next to the warp (warp at %.0f,%.0f,%.0f params %d room %d)",
           (double)warp->world.pos.x, (double)warp->world.pos.y, (double)warp->world.pos.z, (int)warp->params,
           (int)warp->room);
}

void Ps3Test_AreaStart(PlayState* play) {
    const Ps3TestCase* t;
    if (sRunState == RS_IDLE || sCur < 0) return;
    if (sRunState == RS_QUEUED && play->sceneNum == sCurScene) {
        t = &kPs3TestList[sCur];
        sRunState = RS_RUNNING;
        sArrived = 1;
        sArriveT0 = oot_ps3_microseconds_now();
        switch (t->setup) {
            case TS_GOHMA_FIGHT: case TS_GOHMA_WARP: case TS_DUNGEON_KID: GiveKidKit(play, 0); break;
            case TS_DODONGO_FIGHT: GiveKidKit(play, 1); break;
            default: break;
        }
        Report("arrived after %.1f s", (double)(sArriveT0 - sQueuedT0) / 1000000.0);
        LogState(play, "arrival");
        if (t->setup == TS_TOUR) {
            sTourStep = 0;
            sTourT0 = sArriveT0;
            sTourShot = 0;
        } else {
            oot_ps3_ui_hint(t->name, t->check, 40); /* ~1.5 s, gone before the first shot */
        }
    }
}

static void TourTick(PlayState* play) {
    char name[48];
    const unsigned long long now = oot_ps3_microseconds_now();
    if (oot_ps3_debug_request_pending() || play->transitionTrigger != TRANS_TRIGGER_OFF ||
        play->transitionMode != TRANS_MODE_OFF) {
        sTourT0 = now; /* count from the moment the area is playable */
        return;
    }
    if (!sTourShot && now - sTourT0 > 3000000ULL) {
        char tag[16];
        snprintf(tag, sizeof(tag), "area%02d_", sTourStep + 1);
        Report("  tour %d/%d: scene %d", sTourStep + 1, PS3_TOUR_STEPS, (int)play->sceneNum);
        Shot(tag);
        sTourShot = 1;
    }
    if (now - sTourT0 < 5000000ULL) return;
    if (sTourStep + 1 >= PS3_TOUR_STEPS) {
        EndTest("tour finished");
        return;
    }
    ++sTourStep;
    sTourShot = 0;
    sTourT0 = now;
    snprintf(name, sizeof(name), "tour step %d", sTourStep + 1);
    oot_ps3_debug_queue_warp_to(kPs3TourList[sTourStep], name);
}

/* Every game update (play == NULL outside gameplay). */
void Ps3Test_Tick(PlayState* play) {
    const Ps3TestCase* t;
    const unsigned long long now = oot_ps3_microseconds_now();
    if (sRunState == RS_IDLE || sCur < 0) return;
    t = &kPs3TestList[sCur];
    if (sRunState == RS_QUEUED) {
        if (now - sQueuedT0 > 60000000ULL) EndTest("timeout");
        return;
    }
    if (play == NULL) return;
    if (t->setup == TS_TOUR) {
        TourTick(play);
        if (now - sArriveT0 > 600000000ULL) EndTest("timeout (10 min)");
        return;
    }
    while (sStep < STEPS_MAX && sRunState == RS_RUNNING) {
        const Ps3TestStep* s = &t->steps[sStep];
        int due;
        if (s->action == ACT_SHOT && s->at == 0 && s->when == AT_MS) break; /* unused slot: end of the list */
        due = s->when == AT_MS ? now - sArriveT0 >= (unsigned long long)s->at * 1000ULL
                               : play->csCtx.state != CS_STATE_IDLE && play->csCtx.frames >= s->at;
        /* a cutscene step whose cutscene already ended (or never started after 3 s) counts as due */
        if (s->when == AT_CS && !due && play->csCtx.state == CS_STATE_IDLE && now - sArriveT0 > 3000000ULL) {
            Report("  step %d: cutscene frame %u never reached (cutscene not running)", sStep + 1, s->at);
            due = 1;
        }
        if (!due) break;
        if (s->action == ACT_SHOT && oot_ps3_debug_shot_busy()) break; /* previous shot still writing */
        ++sStep;
        switch (s->action) {
            case ACT_SHOT: {
                char tag[16];
                snprintf(tag, sizeof(tag), "%s", s->when == AT_CS ? "cs" : "t");
                Report("  step %d: screenshot at %s %u", sStep, s->when == AT_CS ? "cutscene frame" : "ms", s->at);
                Shot(tag);
                break;
            }
            case ACT_TELEPORT_WARP: TeleportToWarp(play); break;
            case ACT_PLACE_LINK: PlaceLink(play, t->place); break;
            case ACT_STATE: LogState(play, "step"); break;
            case ACT_END: LogState(play, "end"); EndTest("script done"); return;
        }
    }
    if (sRunState == RS_RUNNING && now - sArriveT0 > 120000000ULL) EndTest("timeout (2 min)");
}
