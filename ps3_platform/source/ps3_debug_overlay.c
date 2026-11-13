// SPDX-License-Identifier: GPL-3.0-or-later
/* Debug menu: an overlay drawn on top of every frame, opened and closed with R3.
 *
 * - Performance panel (fps, game logic and draw times, skipped frames, memory,
 *   scene/room/actors), also available as a small HUD while playing.
 * - Warp to any area (Ship of Harkinian's Map Select list).
 * - Toggles saved in USRDIR: trace files, 60/20 fps, the experimental GPU vertex path.
 * - "Mark this moment" (numbered marker + game snapshot in the trace) and screenshots
 *   (USRDIR/screenshot_NNN.bmp, taken before the menu is drawn).
 *
 * The text is drawn with the public-domain 8x8 font into a 512x256 RGBA panel that is
 * uploaded only when it changes, then drawn as one textured rectangle through the
 * normal RSX triangle path. While the menu is open the game gets no controller input.
 * PS3 system headers only (game-side helpers are in ps3_debug_game.c). */

/* 1 = the R3 debug menu is built in (test builds); release builds pass 0. */
#ifndef OOT_PS3_DEBUG_MENU
#define OOT_PS3_DEBUG_MENU 1
#endif
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../../third_party/font8x8/font8x8_basic.h"

/* Button mask passed by the pad code (shipwright_os_ps3.c). */
#define DBG_BTN_R3 0x0001U
#define DBG_BTN_UP 0x0002U
#define DBG_BTN_DOWN 0x0004U
#define DBG_BTN_LEFT 0x0008U
#define DBG_BTN_RIGHT 0x0010U
#define DBG_BTN_CROSS 0x0020U
#define DBG_BTN_CIRCLE 0x0040U
#define DBG_BTN_L1 0x0080U
#define DBG_BTN_R1 0x0100U

/* Elsewhere in the port. */
void oot_ps3_early_trace(const char* message);
void oot_ps3_early_trace_flush_usb(void);
unsigned long long oot_ps3_microseconds_now(void);
unsigned int oot_ps3_heap_used_kb(void);
int oot_ps3_trace_enabled(void);
void oot_ps3_trace_set_enabled(int on);
int oot_ps3_frame_mode(void);
void oot_ps3_set_frame_mode(int mode);
int oot_ps3_frame_auto_half(void); /* shipwright_bootstrap_ps3.cpp */
int oot_ps3_tnl_requested(void);
void oot_ps3_tnl_set_requested(int on);
int oot_ps3_render_thread_enabled(void);          /* shipwright_bootstrap_ps3.cpp */
void oot_ps3_render_thread_set_enabled(int on);
int oot_ps3_tri_fast_enabled(void);             /* interpreter.cpp */
void oot_ps3_tri_fast_set_enabled(int on);
int oot_ps3_rsx_overlay_upload(const uint8_t* rgba, uint32_t width, uint32_t height);
const uint32_t* oot_ps3_rsx_capture_frame(uint32_t* width, uint32_t* height);
void oot_ps3_rsx_set_depth(int test, int write);
void oot_ps3_rsx_set_decal(int decal);
void oot_ps3_rsx_get_depth_state(int* test, int* write, int* decal);
void oot_ps3_rsx_viewport(int x, int y, int width, int height);
void oot_ps3_rsx_scissor(int x, int y, int width, int height);
void oot_ps3_rsx_set_draw_state(uint32_t uv0Off, uint32_t uv1Off, uint32_t fogOff, uint32_t inputOff,
                                uint32_t inputPer, float flags, const float packs[4]);
int oot_ps3_rsx_triangles(const float* vbo, uint32_t stride, uint32_t triangles, uint32_t texture0_id,
                          uint32_t texture1_id, int alpha, uint32_t inputs, int material);
const char* oot_ps3_rsx_selftest_summary(void);
/* ps3_debug_game.c */
int oot_ps3_debug_game_info(char* line1, char* line2, unsigned int size);
int oot_ps3_debug_warp_count(void);
const char* oot_ps3_debug_warp_name(int index);
int oot_ps3_debug_warp(int index);
void oot_ps3_debug_mark(int number);

#define PANEL_COLS 64
#define PANEL_ROWS 32
#define PANEL_W (PANEL_COLS * 8)
#define PANEL_H (PANEL_ROWS * 8)
#define HUD_ROWS 3
#define OVERLAY_TEXTURE_ID 0x4000U

enum { COL_TEXT, COL_DIM, COL_TITLE, COL_SELECT, COL_VALUE, COL_WARN, COL_COUNT };
static const uint8_t kColors[COL_COUNT][3] = {
    { 235, 235, 235 }, { 140, 140, 150 }, { 120, 220, 255 }, { 255, 225, 90 }, { 140, 255, 140 }, { 255, 120, 110 },
};

static char sText[PANEL_ROWS][PANEL_COLS];
static uint8_t sColor[PANEL_ROWS][PANEL_COLS];
static char sDrawnText[PANEL_ROWS][PANEL_COLS];
static uint8_t sDrawnColor[PANEL_ROWS][PANEL_COLS];
static uint8_t sPixels[PANEL_W * PANEL_H * 4];
static int sUploaded;

static int sOpen, sHud, sPage, sSel, sWarpSel, sWarpTop;
static unsigned int sPrevButtons, sHeldTicks;
static int sMarkCount, sShotCount, sShotPending;
static char sStatus[PANEL_COLS + 1];

/* Performance numbers: per-update values from graph.c, frames counted here. */
static unsigned long long sLogicUs, sDrawUs[3];
static unsigned int sDrawn, sSkipped, sSkippedSecond;
static unsigned long long sFpsT0;
static unsigned int sFpsFrames;
static float sFps;
static unsigned long long sLastRefreshUs;

void oot_ps3_debug_note_update(unsigned long long logicUs, const unsigned long long drawUs[3], unsigned int drawn,
                               unsigned int skipped) {
    sLogicUs = logicUs;
    sDrawUs[0] = drawUs[0];
    sDrawUs[1] = drawUs[1];
    sDrawUs[2] = drawUs[2];
    sDrawn = drawn;
    sSkipped = skipped;
    sSkippedSecond += skipped;
}

int oot_ps3_debug_menu_open(void) {
    return sOpen;
}

/* ---- text grid ---- */
static void Clear(void) {
    memset(sText, ' ', sizeof(sText));
    memset(sColor, COL_TEXT, sizeof(sColor));
}

static void Put(int row, int col, int color, const char* text) {
    if (row < 0 || row >= PANEL_ROWS) return;
    for (; *text != '\0' && col < PANEL_COLS; ++text, ++col) {
        if (col < 0) continue;
        sText[row][col] = (*text >= 32 && *text < 127) ? *text : '?';
        sColor[row][col] = (uint8_t)color;
    }
}

static void Line(int row, int color, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
static void Line(int row, int color, const char* fmt, ...) {
    char buffer[PANEL_COLS + 1];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    Put(row, 1, color, buffer);
}

static void Rasterize(void) {
    int row, col, y, x;
    for (row = 0; row < PANEL_ROWS; ++row) {
        for (col = 0; col < PANEL_COLS; ++col) {
            const unsigned char ch = (unsigned char)sText[row][col];
            const uint8_t* glyph = font8x8_basic[ch & 0x7F];
            const uint8_t* rgb = kColors[sColor[row][col] < COL_COUNT ? sColor[row][col] : 0];
            for (y = 0; y < 8; ++y) {
                uint8_t* px = &sPixels[(((size_t)row * 8 + y) * PANEL_W + (size_t)col * 8) * 4];
                for (x = 0; x < 8; ++x, px += 4) {
                    if (glyph[y] & (1U << x)) { /* bit 0 = leftmost pixel */
                        px[0] = rgb[0]; px[1] = rgb[1]; px[2] = rgb[2]; px[3] = 255;
                    } else {
                        px[0] = px[1] = px[2] = 0; px[3] = 175; /* translucent black */
                    }
                }
            }
        }
    }
}

/* ---- pages ---- */
enum { ITEM_HUD, ITEM_WARP, ITEM_TRACE, ITEM_FPS, ITEM_RES, ITEM_TNL, ITEM_THREAD, ITEM_TRIFAST, ITEM_MARK, ITEM_SHOT, ITEM_CLOSE, ITEM_COUNT };

/* Resolution: native by default, USRDIR/res720 forces 720p (rsx_screen.c, read at boot). */
static int sRes720 = -1;
static int Res720(void) {
    if (sRes720 < 0) {
        const int fd = open("/dev_hdd0/game/OOTP30048/USRDIR/res720", O_RDONLY);
        sRes720 = fd >= 0;
        if (fd >= 0) close(fd);
    }
    return sRes720;
}
static void SetRes720(int on) {
    sRes720 = on;
    if (on) {
        const int fd = open("/dev_hdd0/game/OOTP30048/USRDIR/res720", O_WRONLY | O_CREAT, 0666);
        if (fd >= 0) close(fd);
    } else {
        unlink("/dev_hdd0/game/OOTP30048/USRDIR/res720");
    }
}

static void PerfLines(int row) {
    char info1[PANEL_COLS + 1], info2[PANEL_COLS + 1];
    Line(row, COL_VALUE, "FPS %4.1f  logic %4.1f ms  draws %4.1f/%4.1f/%4.1f ms  skip %u/s", (double)sFps,
         sLogicUs / 1000.0, sDrawUs[0] / 1000.0, sDrawUs[1] / 1000.0, sDrawUs[2] / 1000.0, sSkippedSecond);
    info1[0] = info2[0] = '\0';
    oot_ps3_debug_game_info(info1, info2, sizeof(info1));
    Line(row + 1, COL_VALUE, "heap %u/65536 KB  %s", oot_ps3_heap_used_kb(), info1);
    Line(row + 2, COL_VALUE, "%s", info2);
}

static void BuildMain(void) {
    static const char* const kLabels[ITEM_COUNT] = {
        "Performance HUD while playing", "Warp to area...", "Log files (bug reports)", "Frame rate",
        "Resolution (after restart)", "GPU vertex path (experimental)", "Render thread (game + draw in parallel)",
        "Triangle fast path (GPU path)",
        "Mark this moment in the trace",
        "Take screenshot", "Close menu",
    };
    int i;
    Clear();
    Line(0, COL_TITLE, "SHIP OF GADEGUION - DEBUG MENU                    R3: close");
    PerfLines(2);
    for (i = 0; i < ITEM_COUNT; ++i) {
        char value[16] = "";
        switch (i) {
            case ITEM_HUD: snprintf(value, sizeof(value), "[%s]", sHud ? "ON" : "OFF"); break;
            case ITEM_TRACE: snprintf(value, sizeof(value), "[%s]", oot_ps3_trace_enabled() ? "ON" : "OFF"); break;
            case ITEM_FPS: {
                static const char* const kModes[5] = { "[60]", "[UNLOCKED]", "[20]", "[30 STEADY]", "[AUTO 60]" };
                const int mode = oot_ps3_frame_mode();
                snprintf(value, sizeof(value), "%s",
                         mode == 4 && oot_ps3_frame_auto_half() ? "[AUTO 30]" : kModes[mode >= 0 && mode < 5 ? mode : 0]);
            } break;
            case ITEM_RES: snprintf(value, sizeof(value), "[%s]", Res720() ? "720p" : "NATIVE"); break;
            case ITEM_TNL: {
                static const char* const kTnl[7] = { "[OFF]", "[ON]", "[PROBE]", "[NO CULL]", "[ATTR PROBE]",
                                                     "[ATTR PROBE F]", "[ON FLOAT]" };
                const int mode = oot_ps3_tnl_requested();
                snprintf(value, sizeof(value), "%s", kTnl[mode >= 0 && mode < 7 ? mode : 0]);
            } break;
            case ITEM_THREAD: snprintf(value, sizeof(value), "[%s]", oot_ps3_render_thread_enabled() ? "ON" : "OFF"); break;
            case ITEM_TRIFAST: snprintf(value, sizeof(value), "[%s]", oot_ps3_tri_fast_enabled() ? "ON" : "OFF"); break;
            default: break;
        }
        Line(6 + i, i == sSel ? COL_SELECT : COL_TEXT, "%s %-40s %s", i == sSel ? ">" : " ", kLabels[i], value);
    }
    if (sSel == ITEM_TNL) Line(6 + ITEM_COUNT + 1, COL_DIM, "  (probes draw white, no depth/cull; attr probe ignores matrices)");
    Line(6 + ITEM_COUNT + 3, COL_DIM, "%s", oot_ps3_rsx_selftest_summary());
    if (sSel == ITEM_FPS) Line(6 + ITEM_COUNT + 1, COL_DIM, "  (auto: 60, steady 30 when heavy; unlocked may tear)");
    if (sSel == ITEM_RES) Line(6 + ITEM_COUNT + 1, COL_DIM, "  (native = 1080p on a 1080p TV; restart the game to apply)");
    Line(PANEL_ROWS - 3, COL_WARN, "%s", sStatus);
    Line(PANEL_ROWS - 1, COL_DIM, "D-pad: move   X: select   O: back   R3: close");
}

static void BuildWarp(void) {
    const int count = oot_ps3_debug_warp_count();
    const int visible = PANEL_ROWS - 6;
    int i;
    Clear();
    Line(0, COL_TITLE, "WARP TO AREA  (%d/%d)                         R3: close", sWarpSel + 1, count);
    if (sWarpSel < sWarpTop) sWarpTop = sWarpSel;
    if (sWarpSel >= sWarpTop + visible) sWarpTop = sWarpSel - visible + 1;
    for (i = 0; i < visible && sWarpTop + i < count; ++i) {
        const int index = sWarpTop + i;
        Line(2 + i, index == sWarpSel ? COL_SELECT : COL_TEXT, "%s %3d %s", index == sWarpSel ? ">" : " ", index + 1,
             oot_ps3_debug_warp_name(index));
    }
    Line(PANEL_ROWS - 3, COL_WARN, "%s", sStatus);
    Line(PANEL_ROWS - 1, COL_DIM, "D-pad: move  L1/R1: page  X: warp  O: back");
}

static void BuildHud(void) {
    Clear();
    PerfLines(0);
}

/* ---- actions ---- */
static void SetStatus(const char* text) {
    snprintf(sStatus, sizeof(sStatus), "%s", text);
}

static void WriteScreenshot(void) {
    uint32_t width = 0, height = 0, y, x;
    const uint32_t* frame = oot_ps3_rsx_capture_frame(&width, &height);
    char path[96], msg[96];
    static uint8_t row[1920 * 3];
    FILE* f = NULL;
    if (frame == NULL || width == 0 || height == 0 || width > 1920) {
        SetStatus("Screenshot failed: no frame");
        return;
    }
    /* Next free screenshot_NNN.bmp. */
    for (; sShotCount < 1000; ++sShotCount) {
        int fd;
        snprintf(path, sizeof(path), "/dev_hdd0/game/OOTP30048/USRDIR/screenshot_%03d.bmp", sShotCount + 1);
        fd = open(path, O_RDONLY);
        if (fd < 0) break;
        close(fd);
    }
    ++sShotCount;
    f = fopen(path, "wb");
    if (f == NULL) {
        SetStatus("Screenshot failed: cannot write USRDIR");
        return;
    }
    {
        /* 24-bit BMP, bottom-up rows (width * 3 is a multiple of 4 for 1280/1920/720). */
        const uint32_t rowBytes = (width * 3 + 3) & ~3U;
        const uint32_t imageBytes = rowBytes * height;
        uint8_t header[54];
        memset(header, 0, sizeof(header));
        header[0] = 'B'; header[1] = 'M';
#define PUT32(off, v) do { header[off] = (uint8_t)(v); header[off + 1] = (uint8_t)((v) >> 8); \
        header[off + 2] = (uint8_t)((v) >> 16); header[off + 3] = (uint8_t)((v) >> 24); } while (0)
        PUT32(2, 54 + imageBytes);
        PUT32(10, 54);
        PUT32(14, 40);
        PUT32(18, width);
        PUT32(22, height);
        header[26] = 1;
        header[28] = 24;
        PUT32(34, imageBytes);
#undef PUT32
        fwrite(header, 1, sizeof(header), f);
        for (y = height; y-- > 0;) {
            const uint32_t* src = frame + (size_t)y * width;
            memset(row, 0, rowBytes);
            for (x = 0; x < width; ++x) {
                const uint32_t p = src[x]; /* X8R8G8B8 */
                row[x * 3 + 0] = (uint8_t)p;
                row[x * 3 + 1] = (uint8_t)(p >> 8);
                row[x * 3 + 2] = (uint8_t)(p >> 16);
            }
            fwrite(row, 1, rowBytes, f);
        }
    }
    fclose(f);
    snprintf(msg, sizeof(msg), "Screenshot saved: USRDIR/screenshot_%03d.bmp", sShotCount);
    SetStatus(msg);
    oot_ps3_early_trace(msg);
}

static void Activate(void) {
    char msg[80];
    switch (sSel) {
        case ITEM_HUD: sHud = !sHud; break;
        case ITEM_WARP: sPage = 1; SetStatus(""); break;
        case ITEM_TRACE:
            oot_ps3_trace_set_enabled(!oot_ps3_trace_enabled());
            SetStatus(oot_ps3_trace_enabled() ? "Log files ON (USRDIR/logs + USB)" : "Log files OFF");
            break;
        case ITEM_FPS: {
            /* 60 -> auto -> steady 30 -> unlocked -> 20 -> 60 */
            static const int kNext[5] = { 4, 2, 0, 1, 3 };
            const int mode = oot_ps3_frame_mode();
            oot_ps3_set_frame_mode(kNext[mode >= 0 && mode < 5 ? mode : 0]);
        } break;
        case ITEM_RES:
            SetRes720(!Res720());
            SetStatus("Resolution saved: restart the game to apply");
            break;
        case ITEM_TNL:
            /* off -> on -> white probe -> no culling -> attr probe -> attr probe (float) -> on (float) */
            oot_ps3_tnl_set_requested((oot_ps3_tnl_requested() + 1) % 7);
            break;
        case ITEM_THREAD: oot_ps3_render_thread_set_enabled(!oot_ps3_render_thread_enabled()); break;
        case ITEM_TRIFAST:
            oot_ps3_tri_fast_set_enabled(!oot_ps3_tri_fast_enabled());
            oot_ps3_early_trace(oot_ps3_tri_fast_enabled() ? "graph: Play triangle fast path ON (debug menu)"
                                                            : "graph: Play triangle fast path OFF (debug menu)");
            break;
        case ITEM_MARK:
            if (!oot_ps3_trace_enabled()) oot_ps3_trace_set_enabled(1);
            ++sMarkCount;
            oot_ps3_debug_mark(sMarkCount);
            oot_ps3_early_trace_flush_usb();
            snprintf(msg, sizeof(msg), "Mark #%d written to the trace", sMarkCount);
            SetStatus(msg);
            break;
        case ITEM_SHOT:
            sShotPending = 1; /* taken at the end of this frame, before the menu is drawn */
            SetStatus("Taking screenshot...");
            break;
        case ITEM_CLOSE: sOpen = 0; break;
        default: break;
    }
}

/* Called with the PS3 buttons each time the game reads the controller.
 * Returns 1 while the menu is open: the game then gets no input. */
int oot_ps3_debug_input(unsigned int buttons) {
    if (!OOT_PS3_DEBUG_MENU) return 0; /* release build: no menu */
    const unsigned int pressed = buttons & ~sPrevButtons;
    unsigned int repeat = 0;
    if (buttons != 0 && buttons == sPrevButtons) {
        /* held: repeat D-pad/L1/R1 after ~0.4 s, then every other read */
        if (++sHeldTicks > 8 && (sHeldTicks & 1U)) repeat = buttons & (DBG_BTN_UP | DBG_BTN_DOWN | DBG_BTN_L1 | DBG_BTN_R1);
    } else {
        sHeldTicks = 0;
    }
    sPrevButtons = buttons;
    if (pressed & DBG_BTN_R3) {
        sOpen = !sOpen;
        sPage = 0;
        SetStatus("");
        return 1; /* swallow the R3 press itself */
    }
    if (!sOpen) return 0;
    {
        const unsigned int act = pressed | repeat;
        if (sPage == 0) {
            if (act & DBG_BTN_UP) sSel = (sSel + ITEM_COUNT - 1) % ITEM_COUNT;
            if (act & DBG_BTN_DOWN) sSel = (sSel + 1) % ITEM_COUNT;
            if (pressed & DBG_BTN_CROSS) Activate();
            if (pressed & DBG_BTN_CIRCLE) sOpen = 0;
        } else {
            const int count = oot_ps3_debug_warp_count();
            if (count > 0) {
                if (act & DBG_BTN_UP) sWarpSel = (sWarpSel + count - 1) % count;
                if (act & DBG_BTN_DOWN) sWarpSel = (sWarpSel + 1) % count;
                if (act & DBG_BTN_L1) sWarpSel = sWarpSel >= 10 ? sWarpSel - 10 : 0;
                if (act & DBG_BTN_R1) sWarpSel = sWarpSel + 10 < count ? sWarpSel + 10 : count - 1;
                if (pressed & DBG_BTN_CROSS) {
                    if (oot_ps3_debug_warp(sWarpSel)) {
                        sOpen = 0;
                        sPage = 0;
                    } else {
                        SetStatus("Warp only works during gameplay");
                    }
                }
            }
            if (pressed & DBG_BTN_CIRCLE) sPage = 0;
        }
    }
    return 1;
}

/* Called at the end of every drawn frame, before it is presented. */
void oot_ps3_debug_overlay_frame(unsigned int screenWidth, unsigned int screenHeight) {
    if (!OOT_PS3_DEBUG_MENU) return; /* release build: no menu */
    const unsigned long long now = oot_ps3_microseconds_now();
    int rows;
    float quadW, quadH, x0, y0, x1, y1, v1;
    ++sFpsFrames;
    if (sFpsT0 == 0) sFpsT0 = now;
    if (now - sFpsT0 >= 1000000ULL) {
        sFps = (float)sFpsFrames * 1000000.0f / (float)(now - sFpsT0);
        sFpsFrames = 0;
        sFpsT0 = now;
        sSkippedSecond = 0;
    }
    if (sShotPending) {
        sShotPending = 0;
        WriteScreenshot();
    }
    if (!sOpen && !sHud) return;
    /* Rebuild the text at most 4 times per second (immediately after input). */
    if (now - sLastRefreshUs >= 250000ULL || sOpen) {
        sLastRefreshUs = now;
        if (sOpen) {
            if (sPage == 0) BuildMain();
            else BuildWarp();
        } else {
            BuildHud();
        }
        if (!sUploaded || memcmp(sText, sDrawnText, sizeof(sText)) != 0 ||
            memcmp(sColor, sDrawnColor, sizeof(sColor)) != 0) {
            Rasterize();
            if (oot_ps3_rsx_overlay_upload(sPixels, PANEL_W, PANEL_H)) {
                sUploaded = 1;
                memcpy(sDrawnText, sText, sizeof(sText));
                memcpy(sDrawnColor, sColor, sizeof(sColor));
            }
        }
    }
    if (!sUploaded || screenWidth == 0 || screenHeight == 0) return;
    rows = sOpen ? PANEL_ROWS : HUD_ROWS;
    /* Panel width: up to 80% of the screen, at most 2 screen pixels per font pixel. */
    quadW = (float)screenWidth * 0.8f;
    if (quadW > PANEL_W * 2.0f) quadW = PANEL_W * 2.0f;
    quadH = quadW * (float)(rows * 8) / (float)PANEL_W;
    {
        /* Clip space: x -1..1 left to right, y +1 at the top. */
        const float margin = 24.0f;
        float v[6][6];
        x0 = -1.0f + 2.0f * margin / (float)screenWidth;
        y0 = 1.0f - 2.0f * margin / (float)screenHeight;
        x1 = x0 + 2.0f * quadW / (float)screenWidth;
        y1 = y0 - 2.0f * quadH / (float)screenHeight;
        v1 = (float)(rows * 8) / (float)PANEL_H;
        /* x, y, z, w, u, v: two triangles */
        {
            const float corners[6][4] = { { x0, y0, 0, 0 }, { x1, y0, 1, 0 }, { x1, y1, 1, v1 },
                                          { x0, y0, 0, 0 }, { x1, y1, 1, v1 }, { x0, y1, 0, v1 } };
            int i;
            for (i = 0; i < 6; ++i) {
                v[i][0] = corners[i][0];
                v[i][1] = corners[i][1];
                v[i][2] = 0.0f;
                v[i][3] = 1.0f;
                v[i][4] = corners[i][2];
                v[i][5] = corners[i][3];
            }
        }
        {
            /* Combiner: (0 - 0) * 0 + TEXEL0 for colour and alpha (code 8 = SHADER_TEXEL0). */
            const float texel0 = 8.0f * 4096.0f;
            const float packs[4] = { texel0, texel0, texel0, texel0 };
            int depthTest, depthWrite, decal;
            oot_ps3_rsx_get_depth_state(&depthTest, &depthWrite, &decal);
            oot_ps3_rsx_set_depth(0, 0);
            oot_ps3_rsx_set_decal(0);
            oot_ps3_rsx_viewport(0, 0, (int)screenWidth, (int)screenHeight);
            oot_ps3_rsx_scissor(0, 0, (int)screenWidth, (int)screenHeight);
            oot_ps3_rsx_set_draw_state(4U, 0xFFFFU, 0xFFFFU, 6U, 4U, 2.0f /* blended */, packs);
            oot_ps3_rsx_triangles(&v[0][0], 6U, 2U, OVERLAY_TEXTURE_ID, 0U, 1, 0U, 0);
            oot_ps3_rsx_set_depth(depthTest, depthWrite);
            oot_ps3_rsx_set_decal(decal);
        }
    }
}
