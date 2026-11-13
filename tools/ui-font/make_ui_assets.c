// SPDX-License-Identifier: GPL-3.0-or-later
/* Host tool: builds the PS3 UI font atlas and the built-in XMB icon from DejaVu Sans.
 *
 *   make_ui_assets <DejaVuSans.ttf> <DejaVuSans-Bold.ttf> <out ps3_ui_font.h> <out ICON0.PNG>
 *
 * Atlas: 512x1024 alpha, regular at 40 px and bold at 72 px (ASCII 32..126), packed with
 * stb_truetype, plus an 8x8 white block in the bottom-left corner for solid rectangles.
 * Stored zlib-compressed in the header (the game inflates it once). The icon is
 * "Ship of Gadeguion" on a dark rounded card, anti-aliased. Build: scripts/build-ui-assets.sh */
#define STB_TRUETYPE_IMPLEMENTATION
#include "../../third_party/stb/stb_truetype.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

enum { AW = 512, AH = 1024, FIRST = 32, COUNT = 95 };
static const float kSizes[2] = { 40.0f, 72.0f };

static unsigned char* ReadFile(const char* path) {
    FILE* f = fopen(path, "rb");
    long n;
    unsigned char* data;
    if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc((size_t)n);
    if (fread(data, 1, (size_t)n, f) != (size_t)n) exit(1);
    fclose(f);
    return data;
}

static void PngChunk(FILE* f, const char* tag, const unsigned char* data, unsigned size) {
    unsigned char head[8], tail[4];
    uLong crc = crc32(0, (const Bytef*)tag, 4);
    if (size) crc = crc32(crc, data, size);
    head[0] = size >> 24; head[1] = size >> 16; head[2] = size >> 8; head[3] = size;
    memcpy(head + 4, tag, 4);
    tail[0] = crc >> 24; tail[1] = crc >> 16; tail[2] = crc >> 8; tail[3] = crc;
    fwrite(head, 1, 8, f);
    if (size) fwrite(data, 1, size, f);
    fwrite(tail, 1, 4, f);
}

static void WritePng(const char* path, const unsigned char* rgba, int w, int h) {
    uLong rawSize = (uLong)(w * 4 + 1) * h, packedSize = compressBound(rawSize);
    unsigned char* raw = malloc(rawSize);
    unsigned char* packed = malloc(packedSize);
    unsigned char ihdr[13] = { 0 };
    FILE* f = fopen(path, "wb");
    int y;
    for (y = 0; y < h; ++y) {
        raw[y * (w * 4 + 1)] = 0;
        memcpy(raw + y * (w * 4 + 1) + 1, rgba + y * w * 4, (size_t)w * 4);
    }
    compress2(packed, &packedSize, raw, rawSize, 9);
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    ihdr[0] = w >> 24; ihdr[1] = w >> 16; ihdr[2] = w >> 8; ihdr[3] = w;
    ihdr[4] = h >> 24; ihdr[5] = h >> 16; ihdr[6] = h >> 8; ihdr[7] = h;
    ihdr[8] = 8; ihdr[9] = 6;
    PngChunk(f, "IHDR", ihdr, 13);
    PngChunk(f, "IDAT", packed, (unsigned)packedSize);
    PngChunk(f, "IEND", NULL, 0);
    fclose(f);
}

/* Blend coverage `a` (0..1) of colour c over the RGBA float canvas. */
static void Over(float* px, const float c[3], float a) {
    int k;
    const float outA = a + px[3] * (1 - a);
    if (outA <= 0) return;
    for (k = 0; k < 3; ++k) px[k] = (c[k] * a + px[k] * px[3] * (1 - a)) / outA;
    px[3] = outA;
}

static float TextWidth(stbtt_fontinfo* font, float scale, const char* s) {
    float w = 0;
    for (; *s; ++s) {
        int adv, lsb;
        stbtt_GetCodepointHMetrics(font, *s, &adv, &lsb);
        w += adv * scale;
        if (s[1]) w += scale * stbtt_GetCodepointKernAdvance(font, s[0], s[1]);
    }
    return w;
}

/* Anti-aliased text with 4x4 supersampled positions (subpixel placement via stbtt). */
static void DrawText(float* canvas, int cw, int ch, stbtt_fontinfo* font, float px, const char* s, float x,
                     float baseline, const float color[3], float alpha) {
    const float scale = stbtt_ScaleForPixelHeight(font, px);
    for (; *s; ++s) {
        int adv, lsb, x0, y0, x1, y1, w, h, gx, gy;
        const float fx = x - floorf(x);
        unsigned char* bmp;
        stbtt_GetCodepointHMetrics(font, *s, &adv, &lsb);
        stbtt_GetCodepointBitmapBoxSubpixel(font, *s, scale, scale, fx, 0, &x0, &y0, &x1, &y1);
        w = x1 - x0;
        h = y1 - y0;
        if (w > 0 && h > 0) {
            bmp = calloc((size_t)w * h, 1);
            stbtt_MakeCodepointBitmapSubpixel(font, bmp, w, h, w, scale, scale, fx, 0, *s);
            for (gy = 0; gy < h; ++gy)
                for (gx = 0; gx < w; ++gx) {
                    const int X = (int)floorf(x) + x0 + gx, Y = (int)floorf(baseline) + y0 + gy;
                    if (X < 0 || Y < 0 || X >= cw || Y >= ch || !bmp[gy * w + gx]) continue;
                    Over(canvas + ((size_t)Y * cw + X) * 4, color, alpha * bmp[gy * w + gx] / 255.0f);
                }
            free(bmp);
        }
        x += adv * scale;
        if (s[1]) x += scale * stbtt_GetCodepointKernAdvance(font, s[0], s[1]);
    }
}

static void MakeIcon(const char* path, stbtt_fontinfo* bold) {
    enum { W = 320, H = 176 };
    float* canvas = calloc((size_t)W * H * 4, sizeof(float));
    unsigned char* out = malloc((size_t)W * H * 4);
    const float shadow[3] = { 4, 8, 22 }, text[3] = { 236, 244, 255 }, accent[3] = { 150, 210, 255 };
    int x, y, i;
    /* rounded card, vertical gradient, 4x4 supersampled corners */
    for (y = 0; y < H; ++y)
        for (x = 0; x < W; ++x) {
            int inside = 0, sx, sy;
            for (sy = 0; sy < 4; ++sy)
                for (sx = 0; sx < 4; ++sx) {
                    const float px = x + (sx + 0.5f) / 4, py = y + (sy + 0.5f) / 4;
                    const float r = 22, l = 6, t = 6, rr = W - 6, b = H - 6;
                    float dx = 0, dy = 0;
                    if (px < l || px > rr || py < t || py > b) continue;
                    if (px < l + r) dx = l + r - px;
                    if (px > rr - r) dx = px - (rr - r);
                    if (py < t + r) dy = t + r - py;
                    if (py > b - r) dy = py - (b - r);
                    if (dx * dx + dy * dy <= r * r) ++inside;
                }
            if (inside) {
                const float t = (float)y / H;
                const float c[3] = { 14 + 18 * t, 30 + 34 * t, 70 + 60 * t };
                Over(canvas + ((size_t)y * W + x) * 4, c, inside / 16.0f);
            }
        }
    {
        const char* line1 = "Ship of";
        const char* line2 = "Gadeguion";
        /* largest size that leaves a 28 px margin on each side */
        const float wide = TextWidth(bold, stbtt_ScaleForPixelHeight(bold, 64.0f), line2);
        const float size = fminf(64.0f, 64.0f * (W - 56) / wide), scale = stbtt_ScaleForPixelHeight(bold, size);
        const float w1 = TextWidth(bold, scale, line1), w2 = TextWidth(bold, scale, line2);
        const float base1 = H / 2.0f - size * 0.08f, base2 = base1 + size * 0.95f;
        DrawText(canvas, W, H, bold, size, line1, (W - w1) / 2 + 2, base1 + 3, shadow, 0.8f);
        DrawText(canvas, W, H, bold, size, line2, (W - w2) / 2 + 2, base2 + 3, shadow, 0.8f);
        DrawText(canvas, W, H, bold, size, line1, (W - w1) / 2, base1, accent, 1.0f);
        DrawText(canvas, W, H, bold, size, line2, (W - w2) / 2, base2, text, 1.0f);
    }
    for (i = 0; i < W * H; ++i) {
        int k;
        for (k = 0; k < 3; ++k) out[i * 4 + k] = (unsigned char)fminf(255, fmaxf(0, canvas[i * 4 + k] + 0.5f));
        out[i * 4 + 3] = (unsigned char)fminf(255, canvas[i * 4 + 3] * 255 + 0.5f);
    }
    WritePng(path, out, W, H);
}

int main(int argc, char** argv) {
    unsigned char* ttf[2];
    stbtt_fontinfo info[2];
    static unsigned char atlas[AW * AH];
    static stbtt_packedchar chars[2][COUNT];
    stbtt_pack_context pc;
    FILE* f;
    int i, k;
    uLong packedSize;
    unsigned char* packed;
    if (argc != 5) {
        fprintf(stderr, "usage: make_ui_assets <regular.ttf> <bold.ttf> <out.h> <ICON0.PNG>\n");
        return 1;
    }
    for (i = 0; i < 2; ++i) {
        ttf[i] = ReadFile(argv[1 + i]);
        stbtt_InitFont(&info[i], ttf[i], 0);
    }
    if (!stbtt_PackBegin(&pc, atlas, AW, AH - 12, AW, 2, NULL)) return 1;
    for (i = 0; i < 2; ++i)
        if (!stbtt_PackFontRange(&pc, ttf[i], 0, kSizes[i], FIRST, COUNT, chars[i])) {
            fprintf(stderr, "atlas too small for font %d\n", i);
            return 1;
        }
    stbtt_PackEnd(&pc);
    for (k = AH - 8; k < AH; ++k) memset(atlas + k * AW, 255, 8); /* white block */
    packedSize = compressBound(sizeof(atlas));
    packed = malloc(packedSize);
    compress2(packed, &packedSize, atlas, sizeof(atlas), 9);

    f = fopen(argv[3], "w");
    fprintf(f, "// SPDX-License-Identifier: GPL-3.0-or-later\n"
               "// Generated by tools/ui-font/make_ui_assets.c from DejaVu Sans (third_party/dejavu, see LICENSE).\n"
               "// Font atlas %dx%d alpha, zlib-compressed; glyphs ASCII %d..%d, regular %.0f px, bold %.0f px.\n",
            AW, AH, FIRST, FIRST + COUNT - 1, kSizes[0], kSizes[1]);
    fprintf(f, "#define UI_ATLAS_W %d\n#define UI_ATLAS_H %d\n#define UI_FIRST_CHAR %d\n#define UI_CHAR_COUNT %d\n",
            AW, AH, FIRST, COUNT);
    fprintf(f, "static const float kUiFontPx[2] = { %.1ff, %.1ff };\n", kSizes[0], kSizes[1]);
    for (i = 0; i < 2; ++i) {
        int asc, desc, gap;
        const float scale = stbtt_ScaleForPixelHeight(&info[i], kSizes[i]);
        stbtt_GetFontVMetrics(&info[i], &asc, &desc, &gap);
        fprintf(f, "static const float kUiAscent%d = %.3ff;\n", i, asc * scale);
    }
    fprintf(f, "typedef struct { unsigned short x0, y0, x1, y1; float xoff, yoff, xoff2, yoff2, xadvance; } UiGlyph;\n");
    fprintf(f, "static const UiGlyph kUiGlyphs[2][%d] = {\n", COUNT);
    for (i = 0; i < 2; ++i) {
        fprintf(f, "  {\n");
        for (k = 0; k < COUNT; ++k) {
            const stbtt_packedchar* c = &chars[i][k];
            fprintf(f, "    { %u, %u, %u, %u, %.3ff, %.3ff, %.3ff, %.3ff, %.3ff },\n", c->x0, c->y0, c->x1, c->y1,
                    c->xoff, c->yoff, c->xoff2, c->yoff2, c->xadvance);
        }
        fprintf(f, "  },\n");
    }
    fprintf(f, "};\nstatic const unsigned int kUiAtlasZSize = %lu;\nstatic const unsigned char kUiAtlasZ[] = {", packedSize);
    for (i = 0; i < (int)packedSize; ++i) fprintf(f, "%s%u,", i % 24 == 0 ? "\n  " : "", packed[i]);
    fprintf(f, "\n};\n");
    fclose(f);
    MakeIcon(argv[4], &info[1]);
    printf("atlas %lu bytes compressed\n", packedSize);
    return 0;
}
