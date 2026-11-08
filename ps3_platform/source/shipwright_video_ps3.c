// SPDX-License-Identifier: GPL-3.0-or-later
// Keep PSL1GHT/RSX headers in C so their SDK typedefs and attributes never
// collide with Shipwright's C++ compatibility headers.

#include "ps3_platform/shipwright_video_ps3.h"
#include "ps3_platform/rsx_screen.h"
#include "ps3_platform/software_renderer.h"

#include <sys/thread.h>
#include <unistd.h>
#include <math.h>
#include <stdio.h>

static OotPs3Screen sScreen;
static int sReady;
extern void oot_ps3_early_trace(const char* message);
extern void oot_ps3_early_trace_flush_usb(void);
static sys_ppu_thread_t sWatchdogThread;
static int sWatchdogStarted;
static volatile unsigned int sPlayProbeStarted;

void oot_ps3_video_begin_play_probe(void) {
    if (!sPlayProbeStarted) {
        sPlayProbeStarted = 1U;
        oot_ps3_early_trace("rsx-watchdog: separate 50-second Play frame budget activated");
    }
}

void oot_ps3_video_begin_mounted_play_probe(void) {
    sPlayProbeStarted = 2U;
    oot_ps3_early_trace("rsx-watchdog: mounted first Play frame has its own 50-second budget");
}

// Every presented frame kicks the watchdog. If no frame arrives for a while the
// game is stuck: write where (the last trace call), flush the trace files and
// leave to the XMB so the console doesn't need a hard power-off.
static volatile unsigned int sFramesPresented;

extern unsigned long long oot_ps3_thread_key(void);
extern void oot_ps3_lock_debug_state(char* out, unsigned size);
extern void oot_ps3_audio_debug_state(char* out, unsigned size);
extern unsigned long long oot_ps3_audio_thread_key(void);
extern void oot_ps3_heap_check(int maxReports, int fromWatchdog);
static volatile unsigned long long sGameThreadKey;

void oot_ps3_watchdog_kick(void) {
    ++sFramesPresented;
    sGameThreadKey = oot_ps3_thread_key(); /* frames are presented from the game thread */
}

extern const char* oot_ps3_last_trace(void);
extern unsigned int oot_ps3_trace_count(void);

static void VideoProbeWatchdog(void* argument) {
    (void)argument;
    oot_ps3_early_trace("rsx-watchdog: active (stall detector: no frame and no trace for 60 s before the "
                        "first frame, then 20 s; no frame at all for 180 s)");
    unsigned int lastSeen = sFramesPresented;
    unsigned int lastTraceCount = oot_ps3_trace_count();
    unsigned int stalledSeconds = 0;   // no frame and no trace call
    unsigned int noFrameSeconds = 0;   // no frame (scene loads trace but present nothing)
    for (;;) {
        usleep(1000000U);
        const unsigned int now = sFramesPresented;
        const unsigned int traceCount = oot_ps3_trace_count();
        if (now != lastSeen) {
            lastSeen = now;
            lastTraceCount = traceCount;
            stalledSeconds = 0;
            noFrameSeconds = 0;
            continue;
        }
        ++noFrameSeconds;
        if (traceCount != lastTraceCount) {
            lastTraceCount = traceCount;
            stalledSeconds = 0;
        } else {
            ++stalledSeconds;
        }
        if (stalledSeconds >= (now == 0U ? 60U : 20U) || noFrameSeconds >= 180U) break;
    }
    char line[256];
    snprintf(line, sizeof(line), "rsx-watchdog: no frame for %u s (no trace for %u s); stopped at: %s",
             noFrameSeconds, stalledSeconds, oot_ps3_last_trace());
    oot_ps3_early_trace(line);
    {
        char state[192];
        snprintf(line, sizeof(line), "rsx-watchdog: threads game=%llx audio=%llx", sGameThreadKey,
                 oot_ps3_audio_thread_key());
        oot_ps3_early_trace(line);
        oot_ps3_lock_debug_state(state, sizeof(state));
        snprintf(line, sizeof(line), "rsx-watchdog: %s", state);
        oot_ps3_early_trace(line);
        oot_ps3_audio_debug_state(state, sizeof(state));
        snprintf(line, sizeof(line), "rsx-watchdog: %s", state);
        oot_ps3_early_trace(line);
    }
    oot_ps3_heap_check(8, 1);
    oot_ps3_early_trace("rsx-watchdog: exiting to XMB");
    oot_ps3_early_trace_flush_usb();
    _exit(0);
}

int oot_ps3_shipwright_video_start(void) {
    if (!sReady) {
        if (!sWatchdogStarted) {
            oot_ps3_early_trace("rsx-watchdog: before thread creation");
            if (sysThreadCreate(&sWatchdogThread, VideoProbeWatchdog, NULL,
                                1500, 16U * 1024U, 0, "oot-rsx-watchdog") != 0) {
                oot_ps3_early_trace("rsx-watchdog: creation failed; skipping RSX test");
                return 0;
            }
            sWatchdogStarted = 1;
            oot_ps3_early_trace("rsx-watchdog: thread created");
        }
        oot_ps3_early_trace("video-start: before screen init");
        sReady = oot_ps3_screen_init(&sScreen) != 0;
        oot_ps3_early_trace(sReady ? "video-start: screen init succeeded" :
                                     "video-start: screen init failed");
        if (sReady) usleep(1200000U);
    }
    return sReady;
}

void* oot_ps3_shipwright_video_rsx_context(void) {
    return sReady ? sScreen.context : NULL;
}

uint32_t oot_ps3_shipwright_video_rsx_offset(void) {
    return sReady ? sScreen.buffers[sScreen.current_buffer].offset : 0;
}

void oot_ps3_shipwright_video_present_title_layout(const OotPs3TitleRect* rects, uint32_t count) {
    OotPs3DisplayBuffer* target;
    uint32_t i;
    if (!sReady || rects == 0) return;
    target = &sScreen.buffers[sScreen.current_buffer];
    oot_ps3_renderer_clear(target, 0x00081018U, 0);

    /* The rectangle positions come from Shipwright's E4 commands.  These
     * colors are diagnostic; no game texture or Nintendo art is embedded. */
    for (i = 0; i < count; ++i) {
        const uint32_t x0 = ((uint32_t)rects[i].left * target->width) / 320U;
        const uint32_t y0 = ((uint32_t)rects[i].top * target->height) / 240U;
        const uint32_t x1 = ((uint32_t)rects[i].right * target->width) / 320U;
        const uint32_t y1 = ((uint32_t)rects[i].bottom * target->height) / 240U;
        if (x1 <= x0 || y1 <= y0 || x0 >= target->width || y0 >= target->height) continue;
        oot_ps3_renderer_rect(target, (s32)x0, (s32)y0,
                              (u16)(x1 - x0), (u16)(y1 - y0),
                              (i & 1U) ? 0x0000BFC0U : 0x00FFFFFFU);
    }
    oot_ps3_screen_present(&sScreen);
}

int oot_ps3_shipwright_video_present_title_texture(const OotPs3TitleRect* rects, uint32_t count,
                                                   const uint8_t* texture, uint32_t size,
                                                   uint32_t width, uint32_t height) {
    OotPs3DisplayBuffer* target;
    uint32_t i;
    if (!sReady || rects == 0 || texture == 0 || count != 16U ||
        width != 192U || height != 32U || size < width * height) return 0;
    target = &sScreen.buffers[sScreen.current_buffer];
    if (target->pixels == 0) return 0;
    oot_ps3_renderer_clear(target, 0x00081018U, 0);

    /* The title draws 16 adjacent 192x2 strips from this I8 resource.
     * Keep the N64 rectangle positions; sample the actual archive pixels.
     * Its second 32x32 texture and combiner effects are for a later pass. */
    for (i = 0; i < count; ++i) {
        uint32_t x, y;
        const uint32_t left = (uint32_t)rects[i].left * target->width / 320U;
        const uint32_t right = (uint32_t)rects[i].right * target->width / 320U;
        const uint32_t top = (uint32_t)rects[i].top * target->height / 240U;
        const uint32_t bottom = (uint32_t)rects[i].bottom * target->height / 240U;
        if (left >= right || top >= bottom ||
            right > target->width || bottom > target->height) continue;
        for (y = top; y < bottom; ++y) {
            const uint32_t texY = (i * 2U) + (y - top) * 2U / (bottom - top);
            u32* row = target->pixels + y * (uint32_t)target->width;
            for (x = left; x < right; ++x) {
                const uint32_t texX = (x - left) * width / (right - left);
                const uint32_t intensity = texture[texY * width + texX];
                /* I8 is a grayscale intensity.  Preserve its pixel shape. */
                row[x] = (intensity << 16) | (intensity << 8) | intensity;
            }
        }
    }
    oot_ps3_screen_present(&sScreen);
    return 1;
}

/* Real O2R vertex positions, projected on the CPU. This proves the geometry
 * path independently of N64 triangle commands, materials, and matrices. */
int oot_ps3_shipwright_video_present_logo_vertices(const int16_t* xyz, uint32_t count, uint32_t frame) {
    OotPs3DisplayBuffer* target;
    float minX = 32768.0f, minY = 32768.0f, minZ = 32768.0f;
    float maxX = -32768.0f, maxY = -32768.0f, maxZ = -32768.0f;
    float centerX, centerY, centerZ, radius, angle, sine, cosine, size;
    uint32_t i, step;
    if (!sReady || xyz == NULL || count == 0 || count > 512U) return 0;
    target = &sScreen.buffers[sScreen.current_buffer];
    if (target->pixels == NULL) return 0;
    for (i = 0; i < count; ++i) {
        const float x = (float)xyz[i * 3U];
        const float y = (float)xyz[i * 3U + 1U];
        const float z = (float)xyz[i * 3U + 2U];
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
        if (z < minZ) minZ = z;
        if (z > maxZ) maxZ = z;
    }
    centerX = (minX + maxX) * 0.5f;
    centerY = (minY + maxY) * 0.5f;
    centerZ = (minZ + maxZ) * 0.5f;
    radius = fmaxf(fmaxf(maxX - minX, maxY - minY), maxZ - minZ) * 0.5f;
    if (radius < 1.0f) return 0;
    size = (float)(target->height < target->width ? target->height : target->width) * 0.31f;
    for (step = 0; step < 7U; ++step) {
        target = &sScreen.buffers[sScreen.current_buffer];
        angle = (float)(frame % 12U) * 0.45f + (float)step * 0.34f;
        sine = sinf(angle);
        cosine = cosf(angle);
        oot_ps3_renderer_clear(target, 0x00081018U, 0);
        for (i = 0; i < count; ++i) {
            const float x = (float)xyz[i * 3U] - centerX;
            const float y = (float)xyz[i * 3U + 1U] - centerY;
            const float z = (float)xyz[i * 3U + 2U] - centerZ;
            const float rotatedX = x * cosine + z * sine;
            const float rotatedZ = z * cosine - x * sine;
            const float depth = 2.5f * radius + rotatedZ;
            const float projection = size * 2.5f / depth;
            const int px = (int)((float)target->width * 0.5f + rotatedX * projection);
            const int py = (int)((float)target->height * 0.5f - y * projection);
            const uint32_t brightness = (uint32_t)(160.0f + (rotatedZ / radius + 1.0f) * 35.0f);
            const uint32_t color = (brightness << 16) | (brightness << 8) | 0x48U;
            oot_ps3_renderer_rect(target, px - 2, py - 2, 5, 5, color);
        }
        oot_ps3_screen_present(&sScreen);
        usleep(150000U);
    }
    return 1;
}

/* Temporary CPU triangle path over the known-good Mario-inspired RSX flip
 * buffers. Geometry and triangle indices come exclusively from the O2R.
 * A proper RSX vertex/shader backend can replace this draw stage later. */
int oot_ps3_shipwright_video_present_logo_mesh(const int16_t* xyz, uint32_t vertex_count,
                                               const OotPs3LogoTriangle* triangles,
                                               uint32_t triangle_count, uint32_t frame) {
    struct Projected { int x, y; float depth; } projected[512];
    float triangle_depth[256];
    uint16_t order[256];
    OotPs3DisplayBuffer* target;
    float min_x = 32768.0f, min_y = 32768.0f, min_z = 32768.0f;
    float max_x = -32768.0f, max_y = -32768.0f, max_z = -32768.0f;
    float center_x, center_y, center_z, radius, size;
    uint32_t i, step;
    if (!sReady || xyz == NULL || triangles == NULL || vertex_count == 0U ||
        vertex_count > 512U || triangle_count == 0U || triangle_count > 256U) return 0;
    target = &sScreen.buffers[sScreen.current_buffer];
    if (target->pixels == NULL) return 0;
    for (i = 0; i < vertex_count; ++i) {
        const float x = (float)xyz[i * 3U];
        const float y = (float)xyz[i * 3U + 1U];
        const float z = (float)xyz[i * 3U + 2U];
        if (x < min_x) min_x = x;
        if (x > max_x) max_x = x;
        if (y < min_y) min_y = y;
        if (y > max_y) max_y = y;
        if (z < min_z) min_z = z;
        if (z > max_z) max_z = z;
    }
    center_x = (min_x + max_x) * 0.5f;
    center_y = (min_y + max_y) * 0.5f;
    center_z = (min_z + max_z) * 0.5f;
    radius = fmaxf(fmaxf(max_x - min_x, max_y - min_y), max_z - min_z) * 0.5f;
    if (radius < 1.0f) return 0;
    size = (float)(target->height < target->width ? target->height : target->width) * 0.31f;
    for (step = 0; step < 5U; ++step) {
        const float angle = (float)(frame % 12U) * 0.45f + (float)step * 0.4f;
        const float sine = sinf(angle), cosine = cosf(angle);
        uint32_t t;
        target = &sScreen.buffers[sScreen.current_buffer];
        oot_ps3_renderer_clear(target, 0x00081018U, 0);
        for (i = 0; i < vertex_count; ++i) {
            const float x = (float)xyz[i * 3U] - center_x;
            const float y = (float)xyz[i * 3U + 1U] - center_y;
            const float z = (float)xyz[i * 3U + 2U] - center_z;
            const float rotated_x = x * cosine + z * sine;
            const float rotated_z = z * cosine - x * sine;
            const float depth = 2.5f * radius + rotated_z;
            const float projection = size * 2.5f / depth;
            projected[i].x = (int)((float)target->width * 0.5f + rotated_x * projection);
            projected[i].y = (int)((float)target->height * 0.5f - y * projection);
            projected[i].depth = depth;
        }
        for (t = 0; t < triangle_count; ++t) {
            const OotPs3LogoTriangle* tri = &triangles[t];
            if (tri->index[0] >= vertex_count || tri->index[1] >= vertex_count ||
                tri->index[2] >= vertex_count) return 0;
            triangle_depth[t] = projected[tri->index[0]].depth +
                                projected[tri->index[1]].depth + projected[tri->index[2]].depth;
            order[t] = (uint16_t)t;
        }
        /* Draw far faces first: enough for this bounded diagnostic mesh. */
        for (t = 1; t < triangle_count; ++t) {
            uint16_t key = order[t];
            uint32_t j = t;
            while (j > 0 && triangle_depth[order[j - 1U]] < triangle_depth[key]) {
                order[j] = order[j - 1U];
                --j;
            }
            order[j] = key;
        }
        for (t = 0; t < triangle_count; ++t) {
            const OotPs3LogoTriangle* tri = &triangles[order[t]];
            const struct Projected* a = &projected[tri->index[0]];
            const struct Projected* b = &projected[tri->index[1]];
            const struct Projected* c = &projected[tri->index[2]];
            const int area = (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
            int left, right, top, bottom, x, y;
            uint32_t color = tri->color;
            if (area == 0) continue;
            /* Give reverse-facing surfaces a darker shade. No face culling. */
            if (area < 0) color = ((color & 0x00fefefeU) >> 1) & 0x007f7f7fU;
            left = a->x < b->x ? a->x : b->x;
            left = left < c->x ? left : c->x;
            right = a->x > b->x ? a->x : b->x;
            right = right > c->x ? right : c->x;
            top = a->y < b->y ? a->y : b->y;
            top = top < c->y ? top : c->y;
            bottom = a->y > b->y ? a->y : b->y;
            bottom = bottom > c->y ? bottom : c->y;
            if (left < 0) left = 0;
            if (top < 0) top = 0;
            if (right >= target->width) right = target->width - 1;
            if (bottom >= target->height) bottom = target->height - 1;
            for (y = top; y <= bottom; ++y) {
                u32* row = target->pixels + (u32)y * target->width;
                for (x = left; x <= right; ++x) {
                    const int e0 = (b->x - a->x) * (y - a->y) - (b->y - a->y) * (x - a->x);
                    const int e1 = (c->x - b->x) * (y - b->y) - (c->y - b->y) * (x - b->x);
                    const int e2 = (a->x - c->x) * (y - c->y) - (a->y - c->y) * (x - c->x);
                    if ((area > 0 && e0 >= 0 && e1 >= 0 && e2 >= 0) ||
                        (area < 0 && e0 <= 0 && e1 <= 0 && e2 <= 0)) row[x] = color;
                }
            }
        }
        oot_ps3_screen_present(&sScreen);
        usleep(120000U);
    }
    return 1;
}

void oot_ps3_shipwright_video_present_boot_stage(uint32_t stage, uint32_t frame) {
    static const uint32_t stageColors[] = {
        0x00CC2020U, 0x00D0B000U, 0x0000B0D0U, 0x00B000D0U,
        0x00D0D0D0U, 0x0000CC66U, 0x006666CCU, 0x00CC6600U,
    };
    OotPs3DisplayBuffer* target;
    uint32_t color;
    uint16_t markerRange;
    uint16_t markerX;
    uint32_t i;

    if (!oot_ps3_shipwright_video_start()) return;

    target = &sScreen.buffers[sScreen.current_buffer];
    color = stageColors[stage % (sizeof(stageColors) / sizeof(stageColors[0]))];
    oot_ps3_renderer_clear(target, 0x00000810U + ((stage & 7U) * 0x00040404U), frame);

    for (i = 0; i <= stage && i < 12U; ++i) {
        oot_ps3_renderer_rect(target, 16, (uint16_t)(24 + i * 18U), 220, 10, color);
    }

    markerRange = target->width > 96 ? (uint16_t)(target->width - 96) : 1;
    markerX = (uint16_t)((frame * 7U) % markerRange);
    oot_ps3_renderer_rect(target, markerX, 8, 96, 12, 0x00FFFFFFU);
    oot_ps3_screen_present(&sScreen);
}

void oot_ps3_shipwright_video_present_heartbeat(uint32_t frame) {
    OotPs3DisplayBuffer* target;
    uint16_t markerRange;
    uint16_t markerX;
    if (!oot_ps3_shipwright_video_start()) return;
    target = &sScreen.buffers[sScreen.current_buffer];
    oot_ps3_renderer_clear(target, 0x00000810U, 0);
    markerRange = target->width > 96 ? (uint16_t)(target->width - 96) : 1;
    markerX = (uint16_t)((frame * 5U) % markerRange);
    oot_ps3_renderer_rect(target, markerX, 20, 96, 12, 0x0000CC66U);
    oot_ps3_screen_present(&sScreen);
}

uint32_t oot_ps3_shipwright_video_width(void) {
    return sReady ? sScreen.buffers[0].width : 1280U;
}

int oot_ps3_shipwright_video_acquire_fast3d(uint32_t** pixels, uint32_t* width, uint32_t* height) {
    OotPs3DisplayBuffer* target;
    if (!pixels || !width || !height || !oot_ps3_shipwright_video_start()) return 0;
    target = &sScreen.buffers[sScreen.current_buffer];
    if (!target->pixels) return 0;
    *pixels = target->pixels;
    *width = target->width;
    *height = target->height;
    return 1;
}

void oot_ps3_shipwright_video_present_fast3d(void) {
    if (sReady) oot_ps3_screen_present(&sScreen);
}

uint32_t oot_ps3_shipwright_video_height(void) {
    return sReady ? sScreen.buffers[0].height : 720U;
}

void oot_ps3_shipwright_video_stop(void) {
    if (sReady) {
        oot_ps3_screen_shutdown(&sScreen);
        sReady = 0;
    }
}
