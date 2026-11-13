// SPDX-License-Identifier: GPL-3.0-or-later
/* First-boot setup screen (2026-11-12).
 *
 * The PKG on GitHub contains no game data. When USRDIR/oot.o2r is missing (or TRIANGLE
 * is held while the game starts) this screen asks for a USB drive with the oot.o2r that
 * Ship of Harkinian makes from the player's own ROM, checks it, copies it to USRDIR
 * (plus soh.o2r if it sits next to it), and draws the XMB icon (ICON0.PNG) from the
 * archive's title-screen textures, as the PC packager does (tools/packager/xmb_icon.py).
 * Options: debug menu in game (USRDIR/debug_menu_on) and log files (USRDIR/logs/log_off).
 *
 * Drawn with the 8x8 debug font into a 512x288 RGBA picture stretched over the screen.
 * PS3 system headers only. */
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#include "../../third_party/font8x8/font8x8_basic.h"

#ifndef OOT_PS3_PORT_VERSION
#define OOT_PS3_PORT_VERSION "dev"
#endif

#define USRDIR "/dev_hdd0/game/OOTP30048/USRDIR"
#define GAME_O2R USRDIR "/oot.o2r"
#define GAME_O2R_TMP USRDIR "/oot.o2r.part"
#define SOH_O2R USRDIR "/soh.o2r"
#define DEBUG_MENU_FLAG USRDIR "/debug_menu_on"
#define ICON0_PATH "/dev_hdd0/game/OOTP30048/ICON0.PNG"

/* Elsewhere in the port. */
void oot_ps3_early_trace(const char* message);
void oot_ps3_early_trace_flush_usb(void);
int oot_ps3_trace_enabled(void);
void oot_ps3_trace_set_enabled(int on);
void oot_ps3_poll_exit(void);
unsigned int oot_ps3_pad_menu_buttons(void); /* shipwright_os_ps3.c */
uint32_t oot_ps3_shipwright_video_width(void);
uint32_t oot_ps3_shipwright_video_height(void);
void oot_ps3_shipwright_video_present_fast3d(void);
int oot_ps3_rsx_begin(uint32_t width, uint32_t height);
void oot_ps3_rsx_end(void);
int oot_ps3_rsx_overlay_upload(const uint8_t* rgba, uint32_t width, uint32_t height);
void oot_ps3_rsx_set_depth(int test, int write);
void oot_ps3_rsx_set_decal(int decal);
void oot_ps3_rsx_viewport(int x, int y, int width, int height);
void oot_ps3_rsx_scissor(int x, int y, int width, int height);
void oot_ps3_rsx_set_draw_state(uint32_t uv0Off, uint32_t uv1Off, uint32_t fogOff, uint32_t inputOff,
                                uint32_t inputPer, float flags, const float packs[4]);
int oot_ps3_rsx_triangles(const float* vbo, uint32_t stride, uint32_t triangles, uint32_t texture0_id,
                          uint32_t texture1_id, int alpha, uint32_t inputs, int material);

#define PAD_UP 0x01U
#define PAD_DOWN 0x02U
#define PAD_CROSS 0x04U
#define PAD_CIRCLE 0x08U
#define PAD_TRIANGLE 0x10U
#define PAD_START 0x20U

static void Trace(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void Trace(const char* fmt, ...) {
    char line[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    oot_ps3_early_trace(line);
}

static int Exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static long long FileSize(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 ? (long long)st.st_size : -1;
}

/* ===================================================================== zip reading */
typedef struct {
    int fd;
    uint8_t* central;
    uint32_t centralSize;
    uint32_t entries;
} Zip;

static uint32_t Le16(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t Le32(const uint8_t* p) { return Le16(p) | (Le16(p + 2) << 16); }

static int ReadAt(int fd, uint32_t offset, void* out, uint32_t size) {
    if (lseek(fd, (off_t)offset, SEEK_SET) != (off_t)offset) return 0;
    return read(fd, out, size) == (ssize_t)size;
}

static void ZipClose(Zip* zip) {
    if (zip->fd >= 0) close(zip->fd);
    free(zip->central);
    memset(zip, 0, sizeof(*zip));
    zip->fd = -1;
}

static int ZipOpen(Zip* zip, const char* path) {
    uint8_t tail[22 + 0xFFFF];
    long long size = FileSize(path);
    uint32_t tailSize, i, cdOffset;
    memset(zip, 0, sizeof(*zip));
    zip->fd = -1;
    if (size < 1024 || size > 0x7FFFFFFFLL) return 0;
    zip->fd = open(path, O_RDONLY);
    if (zip->fd < 0) return 0;
    tailSize = size < (long long)sizeof(tail) ? (uint32_t)size : (uint32_t)sizeof(tail);
    if (!ReadAt(zip->fd, (uint32_t)size - tailSize, tail, tailSize)) goto fail;
    for (i = tailSize - 22;; --i) {
        if (Le32(tail + i) == 0x06054b50U) break;
        if (i == 0) goto fail;
    }
    zip->entries = Le16(tail + i + 10);
    zip->centralSize = Le32(tail + i + 12);
    cdOffset = Le32(tail + i + 16);
    if (zip->centralSize == 0 || (long long)cdOffset + zip->centralSize > size) goto fail;
    zip->central = (uint8_t*)malloc(zip->centralSize);
    if (zip->central == NULL || !ReadAt(zip->fd, cdOffset, zip->central, zip->centralSize)) goto fail;
    return 1;
fail:
    ZipClose(zip);
    return 0;
}

/* Calls visit(name, nameLen, record) for each central directory record; stops when it returns 1. */
static const uint8_t* ZipFind(const Zip* zip, const char* name, int prefix, uint32_t* cursor) {
    const uint32_t want = (uint32_t)strlen(name);
    uint32_t pos = cursor != NULL ? *cursor : 0;
    while (pos + 46 <= zip->centralSize && Le32(zip->central + pos) == 0x02014b50U) {
        const uint8_t* record = zip->central + pos;
        const uint32_t nameLen = Le16(record + 28);
        const uint32_t next = pos + 46 + nameLen + Le16(record + 30) + Le16(record + 32);
        const char* entryName = (const char*)record + 46;
        pos = next;
        if (next > zip->centralSize) break;
        if (prefix ? (nameLen >= want && memcmp(entryName, name, want) == 0)
                   : (nameLen == want && memcmp(entryName, name, want) == 0)) {
            if (cursor != NULL) *cursor = pos;
            return record;
        }
    }
    if (cursor != NULL) *cursor = pos;
    return NULL;
}

/* Whole entry, malloc'd (caller frees). */
static uint8_t* ZipRead(const Zip* zip, const uint8_t* record, uint32_t* outSize) {
    const uint32_t method = Le16(record + 10);
    const uint32_t packed = Le32(record + 20);
    const uint32_t size = Le32(record + 24);
    const uint32_t local = Le32(record + 42);
    uint8_t header[30];
    uint8_t *in = NULL, *out = NULL;
    if (size > 16U * 1024U * 1024U || packed > 16U * 1024U * 1024U) return NULL;
    if (!ReadAt(zip->fd, local, header, sizeof(header)) || Le32(header) != 0x04034b50U) return NULL;
    in = (uint8_t*)malloc(packed + 1);
    out = (uint8_t*)malloc(size + 1);
    if (in == NULL || out == NULL) goto fail;
    if (!ReadAt(zip->fd, local + 30 + Le16(header + 26) + Le16(header + 28), in, packed)) goto fail;
    if (method == 0) {
        if (packed != size) goto fail;
        memcpy(out, in, size);
    } else if (method == 8) {
        z_stream stream;
        memset(&stream, 0, sizeof(stream));
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) goto fail;
        stream.next_in = in;
        stream.avail_in = packed;
        stream.next_out = out;
        stream.avail_out = size;
        {
            const int result = inflate(&stream, Z_FINISH);
            inflateEnd(&stream);
            if (result != Z_STREAM_END || stream.total_out != size) goto fail;
        }
    } else {
        goto fail;
    }
    free(in);
    *outSize = size;
    return out;
fail:
    free(in);
    free(out);
    return NULL;
}

/* ===================================================================== archive check */
typedef struct {
    int ok;
    char error[96];
    char version[64];
    uint32_t crc;
    uint32_t entries;
} ArchiveInfo;

static const struct {
    uint32_t crc;
    const char* name;
} kVersions[] = {
    { 0xEC7011B7U, "NTSC-U 1.0" }, { 0xD43DA81FU, "NTSC-U 1.1" }, { 0x693BA2AEU, "NTSC-U 1.2" },
    { 0xB044B569U, "PAL 1.0" }, { 0xB2055FBDU, "PAL 1.1" },
    { 0xF7F52DB8U, "NTSC-J GameCube (Collector's Edition)" }, { 0xF611F4BAU, "NTSC-J GameCube" },
    { 0xF3DD35BAU, "NTSC-U GameCube" }, { 0x09465AC3U, "PAL GameCube" }, { 0xF43B45BAU, "NTSC-J Master Quest" },
    { 0xF034001AU, "NTSC-U Master Quest" }, { 0x1D4136F3U, "PAL Master Quest" },
};

static const char* const kRequired[] = {
    "scenes/shared/spot00_scene/spot00_scene",     "scenes/shared/spot04_scene/spot04_scene",
    "scenes/shared/link_home_scene/link_home_scene", "audio/sequences/000_Sound_Effects",
    "audio/fonts/03_Orchestra",
};

static void CheckArchive(const char* path, ArchiveInfo* info) {
    Zip zip;
    const uint8_t* record;
    unsigned int i;
    memset(info, 0, sizeof(*info));
    if (!ZipOpen(&zip, path)) {
        snprintf(info->error, sizeof(info->error), "This file is not a valid .o2r archive.");
        return;
    }
    info->entries = zip.entries;
    record = ZipFind(&zip, "version", 0, NULL);
    if (record == NULL) {
        snprintf(info->error, sizeof(info->error), "No game version inside: use the oot.o2r made by SoH.");
        ZipClose(&zip);
        return;
    }
    {
        uint32_t size = 0;
        uint8_t* data = ZipRead(&zip, record, &size);
        if (data != NULL && size >= 5) {
            info->crc = data[0] == 1 ? ((uint32_t)data[1] << 24 | (uint32_t)data[2] << 16 |
                                        (uint32_t)data[3] << 8 | data[4])
                                     : Le32(data + 1);
        }
        free(data);
    }
    snprintf(info->version, sizeof(info->version), "unknown (0x%08X)", (unsigned)info->crc);
    for (i = 0; i < sizeof(kVersions) / sizeof(kVersions[0]); ++i)
        if (kVersions[i].crc == info->crc) snprintf(info->version, sizeof(info->version), "%s", kVersions[i].name);
    for (i = 0; i < sizeof(kRequired) / sizeof(kRequired[0]); ++i) {
        if (ZipFind(&zip, kRequired[i], 0, NULL) == NULL) {
            snprintf(info->error, sizeof(info->error), "The archive is incomplete (no %.40s).", kRequired[i]);
            ZipClose(&zip);
            return;
        }
    }
    ZipClose(&zip);
    info->ok = 1;
}

/* ===================================================================== XMB icon */
enum { ICON_W = 320, ICON_H = 176, OTR_HEADER = 64 };
#define TEX_RGBA32 1U
#define TEX_I8 6U

typedef struct {
    uint32_t width, height;
    uint8_t* body; /* RGBA32 or I8 */
    uint8_t* blob;
} Tex;

/* The object_mag texture with this (type, width, height). */
static int FindTexture(const Zip* zip, uint32_t type, uint32_t width, uint32_t height, Tex* out) {
    uint32_t cursor = 0;
    const uint8_t* record;
    while ((record = ZipFind(zip, "objects/object_mag/", 1, &cursor)) != NULL) {
        uint32_t size = 0;
        uint8_t* data;
        if (Le32(record + 24) < OTR_HEADER + 16) continue;
        data = ZipRead(zip, record, &size);
        if (data == NULL) continue;
        if (size >= OTR_HEADER + 16 && Le32(data + 4) == 0x4F544558U && Le32(data + OTR_HEADER) == type &&
            Le32(data + OTR_HEADER + 4) == width && Le32(data + OTR_HEADER + 8) == height &&
            Le32(data + OTR_HEADER + 12) == width * height * (type == TEX_RGBA32 ? 4U : 1U) &&
            size >= OTR_HEADER + 16 + Le32(data + OTR_HEADER + 12)) {
            out->width = width;
            out->height = height;
            out->blob = data;
            out->body = data + OTR_HEADER + 16;
            return 1;
        }
        free(data);
    }
    return 0;
}

/* Canvas: r, g, b (0..255), a (0..1) floats. */
static void Blend(float* canvas, int cw, int ch, int x, int y, const float rgb[3], float a) {
    float* d;
    float outA;
    int c;
    if (x < 0 || y < 0 || x >= cw || y >= ch) return;
    d = canvas + ((size_t)y * cw + x) * 4;
    outA = a + d[3] * (1.0f - a);
    if (outA <= 0.0f) return;
    for (c = 0; c < 3; ++c) d[c] = (rgb[c] * a + d[c] * d[3] * (1.0f - a)) / outA;
    d[3] = outA;
}

/* En_Mag's I8 text: colour = lerp(env, prim, texel), alpha = texel. */
static void DrawI8(float* canvas, int cw, int ch, const Tex* tex, int left, int top, const float prim[3],
                   const float env[3]) {
    uint32_t x, y;
    for (y = 0; y < tex->height; ++y)
        for (x = 0; x < tex->width; ++x) {
            const uint8_t t = tex->body[y * tex->width + x];
            float f, rgb[3];
            int c;
            if (t == 0) continue;
            f = t / 255.0f;
            for (c = 0; c < 3; ++c) rgb[c] = env[c] + (prim[c] - env[c]) * f;
            Blend(canvas, cw, ch, left + (int)x, top + (int)y, rgb, f);
        }
}

static uint8_t Clamp255(float v) {
    return v <= 0.0f ? 0 : v >= 255.0f ? 255 : (uint8_t)(v + 0.5f);
}

static void PngChunk(uint8_t** p, const char* tag, const uint8_t* data, uint32_t size) {
    uint8_t* o = *p;
    uLong crc;
    o[0] = (uint8_t)(size >> 24); o[1] = (uint8_t)(size >> 16); o[2] = (uint8_t)(size >> 8); o[3] = (uint8_t)size;
    memcpy(o + 4, tag, 4);
    if (size != 0) memcpy(o + 8, data, size);
    crc = crc32(0L, o + 4, size + 4);
    o[8 + size] = (uint8_t)(crc >> 24); o[9 + size] = (uint8_t)(crc >> 16);
    o[10 + size] = (uint8_t)(crc >> 8); o[11 + size] = (uint8_t)crc;
    *p = o + 12 + size;
}

static int WritePng(const char* path, const uint8_t* rgba, uint32_t width, uint32_t height) {
    const uLong rawSize = (uLong)(width * 4 + 1) * height;
    uLong packedSize = compressBound(rawSize);
    uint8_t* raw = (uint8_t*)malloc(rawSize);
    uint8_t* packed = (uint8_t*)malloc(packedSize);
    uint8_t* file = (uint8_t*)malloc(packedSize + 128);
    uint8_t ihdr[13];
    uint8_t* p;
    uint32_t y;
    int fd, ok = 0;
    if (raw == NULL || packed == NULL || file == NULL) goto done;
    for (y = 0; y < height; ++y) {
        raw[y * (width * 4 + 1)] = 0;
        memcpy(raw + y * (width * 4 + 1) + 1, rgba + y * width * 4, width * 4);
    }
    if (compress2(packed, &packedSize, raw, rawSize, 9) != Z_OK) goto done;
    p = file;
    memcpy(p, "\x89PNG\r\n\x1a\n", 8);
    p += 8;
    ihdr[0] = (uint8_t)(width >> 24); ihdr[1] = (uint8_t)(width >> 16); ihdr[2] = (uint8_t)(width >> 8);
    ihdr[3] = (uint8_t)width;
    ihdr[4] = (uint8_t)(height >> 24); ihdr[5] = (uint8_t)(height >> 16); ihdr[6] = (uint8_t)(height >> 8);
    ihdr[7] = (uint8_t)height;
    ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    PngChunk(&p, "IHDR", ihdr, 13);
    PngChunk(&p, "IDAT", packed, (uint32_t)packedSize);
    PngChunk(&p, "IEND", NULL, 0);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        ok = write(fd, file, (size_t)(p - file)) == (ssize_t)(p - file);
        fsync(fd);
        close(fd);
    }
done:
    free(raw);
    free(packed);
    free(file);
    return ok;
}

/* ICON0.PNG: the title logo (with its two text lines) scaled to 176x176, centred on 320x176. */
static int BuildIcon(const char* o2rPath) {
    Zip zip;
    Tex logo = { 0 }, line1 = { 0 }, line2 = { 0 };
    float* canvas = NULL;
    uint8_t* icon = NULL;
    int ok = 0;
    if (!ZipOpen(&zip, o2rPath)) return 0;
    if (FindTexture(&zip, TEX_RGBA32, 160, 160, &logo) && FindTexture(&zip, TEX_I8, 72, 8, &line1) &&
        FindTexture(&zip, TEX_I8, 96, 8, &line2)) {
        const int w = 160, h = 160, size = ICON_H, left = (ICON_W - ICON_H) / 2;
        canvas = (float*)malloc((size_t)w * h * 4 * sizeof(float));
        icon = (uint8_t*)calloc((size_t)ICON_W * ICON_H, 4);
        if (canvas != NULL && icon != NULL) {
            const float shadowPrim[3] = { 0, 0, 0 }, shadowEnv[3] = { 100, 0, 100 };
            const float textPrim[3] = { 200, 200, 150 }, textEnv[3] = { 100, 100, 50 };
            int i, x, y;
            for (i = 0; i < w * h; ++i) {
                canvas[i * 4 + 0] = logo.body[i * 4 + 0];
                canvas[i * 4 + 1] = logo.body[i * 4 + 1];
                canvas[i * 4 + 2] = logo.body[i * 4 + 2];
                canvas[i * 4 + 3] = logo.body[i * 4 + 3] / 255.0f;
            }
            /* Screen positions from z_en_mag.c minus the logo's top-left (80, 20). */
            DrawI8(canvas, w, h, &line1, 154 - 80, 73 - 20, shadowPrim, shadowEnv);
            DrawI8(canvas, w, h, &line2, 152 - 80, 127 - 20, shadowPrim, shadowEnv);
            DrawI8(canvas, w, h, &line1, 153 - 80, 72 - 20, textPrim, textEnv);
            DrawI8(canvas, w, h, &line2, 151 - 80, 126 - 20, textPrim, textEnv);
            for (i = 0; i < w * h; ++i) /* premultiply for the resample */
                canvas[i * 4 + 0] *= canvas[i * 4 + 3], canvas[i * 4 + 1] *= canvas[i * 4 + 3],
                    canvas[i * 4 + 2] *= canvas[i * 4 + 3];
            for (y = 0; y < size; ++y) {
                float fy = (y + 0.5f) * h / size - 0.5f;
                int y0, y1;
                float ty;
                if (fy < 0) fy = 0;
                if (fy > h - 1) fy = (float)(h - 1);
                y0 = (int)fy;
                y1 = y0 + 1 < h ? y0 + 1 : h - 1;
                ty = fy - y0;
                for (x = 0; x < size; ++x) {
                    float fx = (x + 0.5f) * w / size - 0.5f, tx, v[4];
                    int x0, x1, c;
                    uint8_t* o = icon + ((size_t)y * ICON_W + left + x) * 4;
                    if (fx < 0) fx = 0;
                    if (fx > w - 1) fx = (float)(w - 1);
                    x0 = (int)fx;
                    x1 = x0 + 1 < w ? x0 + 1 : w - 1;
                    tx = fx - x0;
                    for (c = 0; c < 4; ++c) {
                        const float a = canvas[((size_t)y0 * w + x0) * 4 + c] * (1 - tx) + canvas[((size_t)y0 * w + x1) * 4 + c] * tx;
                        const float b = canvas[((size_t)y1 * w + x0) * 4 + c] * (1 - tx) + canvas[((size_t)y1 * w + x1) * 4 + c] * tx;
                        v[c] = a * (1 - ty) + b * ty;
                    }
                    if (v[3] > 0) {
                        o[0] = Clamp255(v[0] / v[3]);
                        o[1] = Clamp255(v[1] / v[3]);
                        o[2] = Clamp255(v[2] / v[3]);
                        o[3] = Clamp255(v[3] * 255.0f);
                    }
                }
            }
            ok = WritePng(ICON0_PATH, icon, ICON_W, ICON_H);
        }
    }
    free(logo.blob);
    free(line1.blob);
    free(line2.blob);
    free(canvas);
    free(icon);
    ZipClose(&zip);
    return ok;
}

/* ===================================================================== screen */
enum { SCR_W = 512, SCR_H = 288, SCREEN_TEXTURE_ID = 0x4000 };
static uint8_t sPixels[SCR_W * SCR_H * 4];

static void FillRect(int x0, int y0, int w, int h, const uint8_t rgb[3]) {
    int x, y;
    for (y = y0 < 0 ? 0 : y0; y < y0 + h && y < SCR_H; ++y)
        for (x = x0 < 0 ? 0 : x0; x < x0 + w && x < SCR_W; ++x) {
            uint8_t* p = &sPixels[((size_t)y * SCR_W + x) * 4];
            p[0] = rgb[0]; p[1] = rgb[1]; p[2] = rgb[2]; p[3] = 255;
        }
}

static void Text(int x, int y, int scale, const uint8_t rgb[3], const char* text) {
    for (; *text != '\0'; ++text, x += 8 * scale) {
        const uint8_t* glyph = font8x8_basic[(unsigned char)*text & 0x7F];
        int gx, gy;
        for (gy = 0; gy < 8; ++gy)
            for (gx = 0; gx < 8; ++gx)
                if (glyph[gy] & (1U << gx)) FillRect(x + gx * scale, y + gy * scale, scale, scale, rgb);
    }
}

static int TextWidth(const char* text, int scale) {
    return (int)strlen(text) * 8 * scale;
}

static void TextCentered(int y, int scale, const uint8_t rgb[3], const char* text) {
    Text((SCR_W - TextWidth(text, scale)) / 2, y, scale, rgb, text);
}

static const uint8_t kWhite[3] = { 235, 235, 240 }, kDim[3] = { 140, 150, 170 }, kGold[3] = { 255, 214, 90 },
                     kGreen[3] = { 130, 240, 140 }, kRed[3] = { 255, 120, 110 }, kShadow[3] = { 10, 14, 30 },
                     kSelect[3] = { 40, 70, 120 }, kBar[3] = { 90, 200, 120 }, kBarBack[3] = { 30, 40, 60 },
                     kTitle[3] = { 150, 215, 255 };

static void Present(void) {
    const uint32_t w = oot_ps3_shipwright_video_width(), h = oot_ps3_shipwright_video_height();
    if (w == 0 || h == 0 || !oot_ps3_rsx_begin(w, h)) {
        usleep(16000);
        return;
    }
    if (oot_ps3_rsx_overlay_upload(sPixels, SCR_W, SCR_H)) {
        /* x, y, z, w, u, v: the whole screen, two triangles */
        const float v[6][6] = { { -1, 1, 0, 1, 0, 0 }, { 1, 1, 0, 1, 1, 0 }, { 1, -1, 0, 1, 1, 1 },
                                { -1, 1, 0, 1, 0, 0 }, { 1, -1, 0, 1, 1, 1 }, { -1, -1, 0, 1, 0, 1 } };
        const float texel0 = 8.0f * 4096.0f; /* combiner D = TEXEL0 */
        const float packs[4] = { texel0, texel0, texel0, texel0 };
        oot_ps3_rsx_set_depth(0, 0);
        oot_ps3_rsx_set_decal(0);
        oot_ps3_rsx_viewport(0, 0, (int)w, (int)h);
        oot_ps3_rsx_scissor(0, 0, (int)w, (int)h);
        oot_ps3_rsx_set_draw_state(4U, 0xFFFFU, 0xFFFFU, 6U, 4U, 2.0f, packs);
        oot_ps3_rsx_triangles(&v[0][0], 6U, 2U, SCREEN_TEXTURE_ID, 0U, 1, 0U, 0);
    }
    oot_ps3_rsx_end();
    oot_ps3_shipwright_video_present_fast3d();
}

/* ===================================================================== USB search */
typedef struct {
    char path[96];
    char sohPath[96];
    int usb;
    long long size;
} Source;

static int FindSource(Source* source) {
    static const char* const kNames[] = { "oot.o2r", "ShipOfGadeguion/oot.o2r", "oot-mq.o2r",
                                          "ShipOfGadeguion/oot-mq.o2r" };
    int usb;
    unsigned int n;
    for (usb = 0; usb < 8; ++usb) {
        char root[16];
        snprintf(root, sizeof(root), "/dev_usb%03d", usb);
        if (!Exists(root)) continue;
        for (n = 0; n < sizeof(kNames) / sizeof(kNames[0]); ++n) {
            snprintf(source->path, sizeof(source->path), "%s/%s", root, kNames[n]);
            source->size = FileSize(source->path);
            if (source->size > 0) {
                const char* slash = strrchr(source->path, '/');
                snprintf(source->sohPath, sizeof(source->sohPath), "%.*s/soh.o2r", (int)(slash - source->path),
                         source->path);
                if (!Exists(source->sohPath)) source->sohPath[0] = '\0';
                source->usb = usb;
                return 1;
            }
        }
    }
    return 0;
}

/* ===================================================================== the screen */
enum { ST_SEARCH, ST_FOUND, ST_BAD, ST_COPY, ST_DONE, ST_FAILED };

static int DebugMenuFlag(void) {
    return Exists(DEBUG_MENU_FLAG);
}

static void SetDebugMenuFlag(int on) {
    if (on) {
        const int fd = open(DEBUG_MENU_FLAG, O_WRONLY | O_CREAT, 0666);
        if (fd >= 0) close(fd);
    } else {
        unlink(DEBUG_MENU_FLAG);
    }
}

/* A PKG update puts the built-in text icon back: draw the logo again whenever ICON0.PNG
 * isn't the one this code wrote last (size remembered in USRDIR/icon_size). */
#define ICON_SIZE_FILE USRDIR "/icon_size"
static void RememberIconSize(void) {
    const int fd = open(ICON_SIZE_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    char text[24];
    if (fd < 0) return;
    snprintf(text, sizeof(text), "%lld", FileSize(ICON0_PATH));
    (void)write(fd, text, strlen(text));
    close(fd);
}

static void RefreshIconIfReplaced(void) {
    char text[24] = "";
    const int fd = open(ICON_SIZE_FILE, O_RDONLY);
    if (fd >= 0) {
        const ssize_t n = read(fd, text, sizeof(text) - 1);
        text[n > 0 ? n : 0] = '\0';
        close(fd);
    }
    if (fd >= 0 && atoll(text) == FileSize(ICON0_PATH)) return;
    if (BuildIcon(GAME_O2R)) {
        RememberIconSize();
        Trace("trace: setup XMB icon drawn from oot.o2r");
    }
}

static int TriangleHeldAtBoot(void) {
    int i;
    for (i = 0; i < 20; ++i) { /* ~0.3 s: pads report shortly after ioPadInit */
        oot_ps3_poll_exit();
        if (oot_ps3_pad_menu_buttons() & PAD_TRIANGLE) return 1;
        usleep(15000);
    }
    return 0;
}

void oot_ps3_setup_screen(void) {
    const int installed = FileSize(GAME_O2R) > 0;
    int state = ST_SEARCH, sel = 0, frame = 0, debugMenu, logs;
    unsigned int prev = 0;
    Source source;
    ArchiveInfo info;
    int in = -1, out = -1;
    long long copied = 0;
    char message[96] = "";
    static uint8_t chunk[1024 * 1024];
    if (installed) {
        RefreshIconIfReplaced();
        if (!TriangleHeldAtBoot()) return;
    }
    Trace("trace: setup screen opened (game file %s)", installed ? "installed" : "missing");
    debugMenu = DebugMenuFlag();
    logs = oot_ps3_trace_enabled();
    memset(&source, 0, sizeof(source));
    memset(&info, 0, sizeof(info));
    prev = oot_ps3_pad_menu_buttons();
    for (;; ++frame) {
        unsigned int buttons, pressed;
        int items = 3; /* debug menu, logs, action */
        const char* action;
        oot_ps3_poll_exit(); /* SELECT / XMB Quit Game */
        buttons = oot_ps3_pad_menu_buttons();
        pressed = buttons & ~prev;
        prev = buttons;

        /* ---- work ---- */
        if ((state == ST_SEARCH || state == ST_FOUND || state == ST_BAD) && frame % 30 == 0) {
            Source found;
            memset(&found, 0, sizeof(found));
            if (!FindSource(&found)) {
                state = ST_SEARCH;
            } else if (state == ST_SEARCH || strcmp(found.path, source.path) != 0 || found.size != source.size) {
                source = found;
                CheckArchive(source.path, &info);
                state = info.ok ? ST_FOUND : ST_BAD;
                Trace("trace: setup found %s (%lld bytes): %s %s", source.path, source.size,
                      info.ok ? "ok, version" : "rejected:", info.ok ? info.version : info.error);
            }
        }
        if (state == ST_COPY) {
            const ssize_t n = read(in, chunk, sizeof(chunk));
            if (n < 0 || (n > 0 && write(out, chunk, (size_t)n) != n)) {
                snprintf(message, sizeof(message), "Copy failed (USB drive removed or HDD full?).");
                state = ST_FAILED;
            } else if (n > 0) {
                copied += n;
            } else {
                close(in);
                in = -1;
                fsync(out);
                close(out);
                out = -1;
                if (copied != source.size) {
                    snprintf(message, sizeof(message), "Copy incomplete (%lld of %lld bytes).", copied, source.size);
                    state = ST_FAILED;
                } else {
                    unlink(GAME_O2R);
                    if (rename(GAME_O2R_TMP, GAME_O2R) != 0) {
                        snprintf(message, sizeof(message), "Could not rename the copied file.");
                        state = ST_FAILED;
                    } else {
                        if (source.sohPath[0] != '\0') {
                            const int sIn = open(source.sohPath, O_RDONLY);
                            const int sOut = sIn >= 0 ? open(SOH_O2R, O_WRONLY | O_CREAT | O_TRUNC, 0666) : -1;
                            ssize_t k;
                            while (sOut >= 0 && (k = read(sIn, chunk, sizeof(chunk))) > 0)
                                if (write(sOut, chunk, (size_t)k) != k) break;
                            if (sOut >= 0) close(sOut);
                            if (sIn >= 0) close(sIn);
                        }
                        {
                            const int icon = BuildIcon(GAME_O2R);
                            if (icon) RememberIconSize();
                            Trace("trace: setup installed oot.o2r (%lld bytes, %s); XMB icon %s", copied,
                                  info.version, icon ? "written" : "FAILED");
                        }
                        state = ST_DONE;
                        sel = 2;
                    }
                }
            }
            if (state == ST_FAILED) {
                if (in >= 0) close(in);
                if (out >= 0) close(out);
                in = out = -1;
                unlink(GAME_O2R_TMP);
                Trace("trace: setup copy FAILED: %s", message);
            }
        }

        /* ---- action item ---- */
        switch (state) {
            case ST_FOUND: action = "Install the game file from the USB drive"; break;
            case ST_COPY: action = "Copying..."; break;
            case ST_DONE: action = "Start the game"; break;
            case ST_FAILED: action = "Try again"; break;
            default: action = installed ? "Start the game" : "Waiting for a USB drive..."; break;
        }

        /* ---- input ---- */
        if (state != ST_COPY) {
            if (pressed & PAD_UP) sel = (sel + items - 1) % items;
            if (pressed & PAD_DOWN) sel = (sel + 1) % items;
            if (pressed & (PAD_CROSS | PAD_START)) {
                if (sel == 0) {
                    debugMenu = !debugMenu;
                    SetDebugMenuFlag(debugMenu);
                    Trace("trace: setup debug menu %s", debugMenu ? "on" : "off");
                } else if (sel == 1) {
                    logs = !logs;
                    oot_ps3_trace_set_enabled(logs);
                } else if (state == ST_FOUND) {
                    in = open(source.path, O_RDONLY);
                    out = in >= 0 ? open(GAME_O2R_TMP, O_WRONLY | O_CREAT | O_TRUNC, 0666) : -1;
                    copied = 0;
                    if (in < 0 || out < 0) {
                        if (in >= 0) close(in);
                        in = -1;
                        snprintf(message, sizeof(message), "Could not open the files for copying.");
                        state = ST_FAILED;
                    } else {
                        Trace("trace: setup copying %s", source.path);
                        state = ST_COPY;
                    }
                } else if (state == ST_DONE || (installed && state != ST_FAILED)) {
                    Trace("trace: setup done; starting the game");
                    return;
                } else if (state == ST_FAILED) {
                    state = ST_SEARCH;
                    message[0] = '\0';
                    frame = -1;
                }
            }
        }

        if (installed && state != ST_COPY && (pressed & PAD_CIRCLE)) {
            Trace("trace: setup closed; starting the installed game");
            return;
        }

        /* ---- draw ---- */
        {
            int y;
            char line[80];
            for (y = 0; y < SCR_H; ++y) { /* dark blue gradient */
                const uint8_t rgb[3] = { (uint8_t)(8 + y / 24), (uint8_t)(14 + y / 14), (uint8_t)(38 + y / 6) };
                FillRect(0, y, SCR_W, 1, rgb);
            }
            Text((SCR_W - TextWidth("Ship of Gadeguion", 3)) / 2 + 2, 22, 3, kShadow, "Ship of Gadeguion");
            TextCentered(20, 3, kTitle, "Ship of Gadeguion");
            TextCentered(52, 1, kWhite, "A native port of Ship of Harkinian for the PlayStation 3");
            FillRect(40, 68, SCR_W - 80, 1, kDim);

            y = 82;
            switch (state) {
                case ST_SEARCH:
                    if (installed) {
                        TextCentered(y, 1, kGreen, "The game file is installed.");
                        TextCentered(y + 12, 1, kDim, "To replace it, plug in a USB drive with a new oot.o2r.");
                    } else {
                        TextCentered(y, 1, kGold, "Plug in a USB drive with your oot.o2r file.");
                        TextCentered(y + 12, 1, kDim, "Ship of Harkinian (PC) makes it from your own ROM.");
                        TextCentered(y + 24, 1, kDim, "Put it in the root of the drive (FAT32).");
                    }
                    break;
                case ST_FOUND:
                    snprintf(line, sizeof(line), "Found %.40s on USB drive %d (%.1f MB)",
                             strchr(source.path + 1, '/') + 1, source.usb, source.size / 1048576.0);
                    TextCentered(y, 1, kGreen, line);
                    snprintf(line, sizeof(line), "Game version: %.48s", info.version);
                    TextCentered(y + 12, 1, kWhite, line);
                    if (info.crc != 0xEC7011B7U)
                        TextCentered(y + 24, 1, kGold, "Only NTSC-U 1.0 has been tested so far.");
                    break;
                case ST_BAD:
                    TextCentered(y, 1, kRed, "This file can't be used:");
                    TextCentered(y + 12, 1, kRed, info.error);
                    break;
                case ST_COPY: {
                    const int barW = SCR_W - 120;
                    const int done = source.size > 0 ? (int)(barW * (double)copied / (double)source.size) : 0;
                    snprintf(line, sizeof(line), "Copying the game file... %d%%",
                             source.size > 0 ? (int)(100.0 * copied / source.size) : 0);
                    TextCentered(y, 1, kWhite, line);
                    FillRect(60, y + 14, barW, 10, kBarBack);
                    FillRect(60, y + 14, done, 10, kBar);
                    break;
                }
                case ST_DONE:
                    TextCentered(y, 1, kGreen, "Installed! The XMB icon updates when you return to the XMB.");
                    snprintf(line, sizeof(line), "Game version: %.48s", info.version);
                    TextCentered(y + 12, 1, kWhite, line);
                    break;
                case ST_FAILED:
                    TextCentered(y, 1, kRed, message);
                    break;
            }

            {
                const char* labels[3];
                char opt0[64], opt1[64];
                int i;
                snprintf(opt0, sizeof(opt0), "[%c] Debug menu in game (R3 opens it)", debugMenu ? 'X' : ' ');
                snprintf(opt1, sizeof(opt1), "[%c] Save log files (HDD + USB drive)", logs ? 'X' : ' ');
                labels[0] = opt0;
                labels[1] = opt1;
                labels[2] = action;
                for (i = 0; i < 3; ++i) {
                    const int rowY = 150 + i * 18 + (i == 2 ? 8 : 0);
                    const int enabled = i != 2 || state == ST_FOUND || state == ST_DONE || state == ST_FAILED ||
                                        (installed && (state == ST_SEARCH || state == ST_BAD));
                    if (i == sel) FillRect(80, rowY - 4, SCR_W - 160, 16, kSelect);
                    Text(96, rowY, 1, i == sel ? kGold : enabled ? kWhite : kDim, labels[i]);
                }
                if (sel == 1)
                    TextCentered(216, 1, kDim, "Logs help fix crashes: share ShipOfGadeguion-*.txt");
                if (sel == 0) TextCentered(216, 1, kDim, "Performance info, warps and screenshots.");
            }
            TextCentered(234, 1, kDim, installed ? "X: select    O: start the game    SELECT: quit"
                                                 : "X: select    SELECT: quit");
            FillRect(0, SCR_H - 22, SCR_W, 22, kShadow);
            Text(8, SCR_H - 15, 1, kWhite, "Port made by Gadega");
            snprintf(line, sizeof(line), "v%s", OOT_PS3_PORT_VERSION);
            Text(SCR_W - 8 - TextWidth(line, 1), SCR_H - 15, 1, kWhite, line);
        }
        Present();
    }
}
