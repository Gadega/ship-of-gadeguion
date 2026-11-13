// SPDX-License-Identifier: GPL-3.0-or-later
/* Menu drawing (setup screen, game menu): anti-aliased text from a DejaVu Sans atlas
 * (ps3_ui_font.h, made by tools/ui-font/make_ui_assets.c) and solid rectangles, drawn
 * as textured quads at the display's own resolution.
 *
 * Coordinates are virtual pixels: 1080 high, width = 1080 x display aspect (1920 on
 * 16:9, 1440 on 4:3). Colours are 0xRRGGBBAA. Call ps3ui_begin() inside an RSX frame
 * (between oot_ps3_rsx_begin and oot_ps3_rsx_end), draw, then ps3ui_end(). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "ps3_ui.h"
#include "ps3_ui_font.h"

void oot_ps3_early_trace(const char* message);
float oot_ps3_display_aspect(void); /* rsx_screen.c */
int oot_ps3_rsx_ui_upload(const uint8_t* rgba, uint32_t width, uint32_t height);
void oot_ps3_rsx_set_depth(int test, int write);
void oot_ps3_rsx_get_depth_state(int* test, int* write, int* decal);
void oot_ps3_rsx_set_decal(int decal);
void oot_ps3_rsx_viewport(int x, int y, int width, int height);
void oot_ps3_rsx_scissor(int x, int y, int width, int height);
void oot_ps3_rsx_set_draw_state(uint32_t uv0Off, uint32_t uv1Off, uint32_t fogOff, uint32_t inputOff,
                                uint32_t inputPer, float flags, const float packs[4]);
int oot_ps3_rsx_triangles(const float* vbo, uint32_t stride, uint32_t triangles, uint32_t texture0_id,
                          uint32_t texture1_id, int alpha, uint32_t inputs, int material);

#define UI_TEXTURE_ID 0x4001U
enum { UI_STRIDE = 10, UI_MAX_QUADS = 1024 };

static int sAtlasState; /* 0 not yet, 1 uploaded, -1 failed */
static float sVerts[UI_MAX_QUADS * 6 * UI_STRIDE];
static int sQuads;
static unsigned int sScreenW, sScreenH;
static float sVirtW = 1920.0f;
static int sSavedDepthTest, sSavedDepthWrite, sSavedDecal;

static int UploadAtlas(void) {
    uLongf size = UI_ATLAS_W * UI_ATLAS_H;
    uint8_t* alpha = (uint8_t*)malloc(size);
    uint8_t* rgba = (uint8_t*)malloc((size_t)UI_ATLAS_W * UI_ATLAS_H * 4);
    int ok = 0;
    if (alpha != NULL && rgba != NULL && uncompress(alpha, &size, kUiAtlasZ, kUiAtlasZSize) == Z_OK &&
        size == (uLongf)(UI_ATLAS_W * UI_ATLAS_H)) {
        uLongf i;
        for (i = 0; i < size; ++i) {
            rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = alpha[i];
        }
        ok = oot_ps3_rsx_ui_upload(rgba, UI_ATLAS_W, UI_ATLAS_H);
    }
    free(alpha);
    free(rgba);
    oot_ps3_early_trace(ok ? "trace: menu font atlas ready" : "trace: menu font atlas FAILED");
    return ok;
}

float ps3ui_width(void) {
    return sVirtW;
}

int ps3ui_begin(unsigned int screenWidth, unsigned int screenHeight) {
    const float aspect = oot_ps3_display_aspect();
    if (sAtlasState == 0) sAtlasState = UploadAtlas() ? 1 : -1;
    if (sAtlasState < 0 || screenWidth == 0 || screenHeight == 0) return 0;
    sScreenW = screenWidth;
    sScreenH = screenHeight;
    sVirtW = 1080.0f * (aspect > 1.0f ? aspect : 16.0f / 9.0f);
    sQuads = 0;
    oot_ps3_rsx_get_depth_state(&sSavedDepthTest, &sSavedDepthWrite, &sSavedDecal);
    return 1;
}

static void Quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t rgba) {
    float* v;
    const float r = (float)((rgba >> 24) & 0xFF) / 255.0f, g = (float)((rgba >> 16) & 0xFF) / 255.0f;
    const float b = (float)((rgba >> 8) & 0xFF) / 255.0f, a = (float)(rgba & 0xFF) / 255.0f;
    const float cx0 = x0 / sVirtW * 2.0f - 1.0f, cx1 = x1 / sVirtW * 2.0f - 1.0f;
    const float cy0 = 1.0f - y0 / 540.0f, cy1 = 1.0f - y1 / 540.0f;
    const float corner[6][4] = { { cx0, cy0, u0, v0 }, { cx1, cy0, u1, v0 }, { cx1, cy1, u1, v1 },
                                 { cx0, cy0, u0, v0 }, { cx1, cy1, u1, v1 }, { cx0, cy1, u0, v1 } };
    int i;
    if (sQuads >= UI_MAX_QUADS) ps3ui_end();
    v = &sVerts[(size_t)sQuads * 6 * UI_STRIDE];
    for (i = 0; i < 6; ++i, v += UI_STRIDE) {
        v[0] = corner[i][0];
        v[1] = corner[i][1];
        v[2] = 0.0f;
        v[3] = 1.0f;
        v[4] = corner[i][2];
        v[5] = corner[i][3];
        v[6] = r;
        v[7] = g;
        v[8] = b;
        v[9] = a;
    }
    ++sQuads;
}

void ps3ui_rect(float x, float y, float w, float h, uint32_t rgba) {
    /* the white block in the atlas corner */
    const float u = 4.0f / UI_ATLAS_W, v = (UI_ATLAS_H - 4.0f) / UI_ATLAS_H;
    if (w <= 0.0f || h <= 0.0f || (rgba & 0xFF) == 0) return;
    Quad(x, y, x + w, y + h, u, v, u, v, rgba);
}

/* Vertical gradient: top colour to bottom colour in `steps` bands. */
void ps3ui_gradient(float x, float y, float w, float h, uint32_t top, uint32_t bottom, int steps) {
    int i;
    for (i = 0; i < steps; ++i) {
        const float t = steps > 1 ? (float)i / (float)(steps - 1) : 0.0f;
        uint32_t c = 0;
        int k;
        for (k = 0; k < 4; ++k) {
            const float a = (float)((top >> (k * 8)) & 0xFF), b = (float)((bottom >> (k * 8)) & 0xFF);
            c |= (uint32_t)(a + (b - a) * t + 0.5f) << (k * 8);
        }
        ps3ui_rect(x, y + h * i / steps, w, h / steps + 0.5f, c);
    }
}

static const UiGlyph* Glyph(int bold, char ch) {
    int index = (unsigned char)ch - UI_FIRST_CHAR;
    if (index < 0 || index >= UI_CHAR_COUNT) index = '?' - UI_FIRST_CHAR;
    return &kUiGlyphs[bold ? 1 : 0][index];
}

float ps3ui_text_width(const char* text, float px, int bold) {
    const float scale = px / kUiFontPx[bold ? 1 : 0];
    float w = 0.0f;
    for (; *text != '\0'; ++text) w += Glyph(bold, *text)->xadvance * scale;
    return w;
}

/* `y` is the top of the line (the font's ascent below it is the baseline). */
float ps3ui_text(float x, float y, float px, uint32_t rgba, int bold, const char* text) {
    const int f = bold ? 1 : 0;
    const float scale = px / kUiFontPx[f];
    const float baseline = y + (f ? kUiAscent1 : kUiAscent0) * scale;
    const float startX = x;
    for (; *text != '\0'; ++text) {
        const UiGlyph* g = Glyph(bold, *text);
        if (g->x1 > g->x0)
            Quad(x + g->xoff * scale, baseline + g->yoff * scale, x + g->xoff2 * scale, baseline + g->yoff2 * scale,
                 (float)g->x0 / UI_ATLAS_W, (float)g->y0 / UI_ATLAS_H, (float)g->x1 / UI_ATLAS_W,
                 (float)g->y1 / UI_ATLAS_H, rgba);
        x += g->xadvance * scale;
    }
    return x - startX;
}

void ps3ui_text_centered(float centreX, float y, float px, uint32_t rgba, int bold, const char* text) {
    ps3ui_text(centreX - ps3ui_text_width(text, px, bold) * 0.5f, y, px, rgba, bold, text);
}

void ps3ui_text_right(float rightX, float y, float px, uint32_t rgba, int bold, const char* text) {
    ps3ui_text(rightX - ps3ui_text_width(text, px, bold), y, px, rgba, bold, text);
}

void ps3ui_end(void) {
    if (sQuads > 0 && sScreenW != 0) {
        /* (TEXEL0 - 0) * INPUT_1 + 0 for colour and alpha: code TEXEL0 = 8, INPUT_1 = 1 */
        const float pack = (float)(8U | (0U << 4) | (1U << 8) | (0U << 12));
        const float packs[4] = { pack, pack, pack, pack };
        oot_ps3_rsx_set_depth(0, 0);
        oot_ps3_rsx_set_decal(0);
        oot_ps3_rsx_viewport(0, 0, (int)sScreenW, (int)sScreenH);
        oot_ps3_rsx_scissor(0, 0, (int)sScreenW, (int)sScreenH);
        oot_ps3_rsx_set_draw_state(4U, 0xFFFFU, 0xFFFFU, 6U, 4U, 2.0f /* blended */, packs);
        oot_ps3_rsx_triangles(sVerts, UI_STRIDE, (uint32_t)sQuads * 2U, UI_TEXTURE_ID, 0U, 1, 1U, 0);
        oot_ps3_rsx_set_depth(sSavedDepthTest, sSavedDepthWrite);
        oot_ps3_rsx_set_decal(sSavedDecal);
    }
    sQuads = 0;
}
