// SPDX-License-Identifier: GPL-3.0-or-later
// Screen initialization adapted from PSL1GHT's GPL-3.0 videoTest sample.
#include "ps3_platform/rsx_screen.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <rsx/gcm_sys.h>
#include <sysutil/video.h>

#define HOST_ALIGNMENT (1024U * 1024U)
#define HOST_SIZE (16U * 1024U * 1024U)
#define COMMAND_BUFFER_SIZE (512U * 1024U)

extern void oot_ps3_early_trace(const char* message);
extern unsigned long long oot_ps3_microseconds_now(void);
/* Display refresh counter (59.94 Hz on 720p/1080i/480p). The frame pacing in
 * Graph_ProcessGfxCommands schedules frames on vblanks instead of a 60 Hz CPU
 * timer: the two clocks drift apart by one frame every ~17 s, which showed up as a
 * GPU wait that kept growing and then a dropped frame. */
static volatile unsigned int sPs3VBlanks;
static void Ps3VBlankHandler(const u32 head) {
    (void)head;
    ++sPs3VBlanks;
}
unsigned int oot_ps3_vblank_count(void) { return sPs3VBlanks; }

/* Shape of the TV picture (XMB TV Type 4:3 / 16:9). 480/576 modes are 720 pixels wide
 * for both shapes, so the pixel count alone doesn't tell; this does. */
static float sPs3DisplayAspect = 16.0f / 9.0f;
float oot_ps3_display_aspect(void) { return sPs3DisplayAspect; }

static unsigned long long sPs3FlipWaitUs;
unsigned long long oot_ps3_screen_take_flip_wait_us(void) {
    unsigned long long v = sPs3FlipWaitUs;
    sPs3FlipWaitUs = 0;
    return v;
}

/* The installed librsx.a calls gcmInitBodyEx first from rsxInit.
 * --wrap at final link redirects that call here for a single boundary trace.
 * Distinct C names avoid conflicting with SDK header prototypes. */
/* Import slots contain the local trampoline address in the ELF.  The PS3
 * loader must replace each one with an exported function descriptor.  Reading
 * the slots at runtime is safe; calling an unresolved trampoline is not. */
extern volatile u32 gcmInitBodyEx_stub;

static void oot_trace_u32(const char* prefix, u32 value) {
    static const char digits[] = "0123456789ABCDEF";
    char line[64];
    unsigned int i = 0;
    unsigned int shift;
    while (prefix[i] != '\0' && i < sizeof(line) - 12U) {
        line[i] = prefix[i];
        ++i;
    }
    line[i++] = '0';
    line[i++] = 'x';
    for (shift = 0; shift < 8; ++shift) {
        line[i++] = digits[(value >> (28U - 4U * shift)) & 15U];
    }
    line[i] = '\0';
    oot_ps3_early_trace(line);
}

static void oot_trace_import_slot(const char* prefix, volatile const u32* slot) {
    oot_trace_u32(prefix, *slot);
}

s32 oot_trace_gcm_init(void* context, u32 cmdSize, u32 ioSize,
                       const void* ioAddress) __asm__("__wrap_gcmInitBodyEx");
extern const unsigned char __gcmInitBodyEx[];
extern s32 oot_real_gcm_init(void*, u32, u32, const void*)
    __asm__("__real_gcmInitBodyEx");
s32 oot_trace_gcm_init(void* context, u32 cmdSize, u32 ioSize,
                       const void* ioAddress) {
    s32 result;
    oot_trace_import_slot("gcm-import: gcmInitBodyEx slot=", &gcmInitBodyEx_stub);
    if (gcmInitBodyEx_stub == (u32)(uintptr_t)__gcmInitBodyEx) {
        oot_ps3_early_trace("gcm-init: import unresolved; avoiding recursive call");
        return -1;
    }
    oot_trace_u32("gcm-init: cmdSize=", cmdSize);
    oot_trace_u32("gcm-init: ioSize=", ioSize);
    oot_trace_u32("gcm-init: ioAddress=", (u32)(uintptr_t)ioAddress);
    oot_trace_u32("gcm-init: contextArg=", (u32)(uintptr_t)context);
    oot_ps3_early_trace("gcm-init: before resolved gcmInitBodyEx");
    result = oot_real_gcm_init(context, cmdSize, ioSize, ioAddress);
    oot_trace_u32("gcm-init: result=", (u32)result);
    oot_ps3_early_trace(result == 0 ? "gcm-init: gcmInitBodyEx returned success" :
                                      "gcm-init: gcmInitBodyEx returned failure");
    return result;
}

static void wait_for_flip(void) {
    while (gcmGetFlipStatus() != 0) {
        usleep(200);
    }
    gcmResetFlipStatus();
}

static int make_buffer(OotPs3DisplayBuffer* buffer, u16 width, u16 height, s32 id) {
    const u32 pitch = (u32)width * sizeof(u32);
    const u32 size = pitch * (u32)height;

    memset(buffer, 0, sizeof(*buffer));
    buffer->pixels = (u32*)rsxMemalign(64, size);
    if (buffer->pixels == NULL) {
        return 0;
    }
    if (rsxAddressToOffset(buffer->pixels, &buffer->offset) != 0 ||
        gcmSetDisplayBuffer(id, buffer->offset, pitch, width, height) != 0) {
        rsxFree(buffer->pixels);
        buffer->pixels = NULL;
        return 0;
    }
    buffer->width = width;
    buffer->height = height;
    buffer->id = id;
    return 1;
}

int oot_ps3_screen_init(OotPs3Screen* screen) {
    videoState state;
    videoResolution resolution;
    videoConfiguration configuration;

    oot_ps3_early_trace("rsx-init: entered");
    memset(screen, 0, sizeof(*screen));
    oot_ps3_early_trace("rsx-init: before aligned host allocation");
    screen->host_memory = memalign(HOST_ALIGNMENT, HOST_SIZE);
    if (screen->host_memory == NULL) {
        oot_ps3_early_trace("rsx-init: aligned host allocation failed");
        goto error;
    }
    oot_trace_u32("rsx-init: aligned host address=", (u32)(uintptr_t)screen->host_memory);
    oot_ps3_early_trace("rsx-init: aligned host allocation ready");

    oot_ps3_early_trace("rsx-init: before rsxInit");
    if (rsxInit(&screen->context, COMMAND_BUFFER_SIZE, HOST_SIZE, screen->host_memory) != 0 ||
        screen->context == NULL) {
        oot_ps3_early_trace("rsx-init: rsxInit failed");
        goto error;
    }
    oot_ps3_early_trace("rsx-init: after rsxInit");
    oot_ps3_early_trace("rsx-init: before video state query");
    if (videoGetState(0, 0, &state) != 0 || state.state != 0 ||
        videoGetResolution(state.displayMode.resolution, &resolution) != 0) {
        oot_ps3_early_trace("rsx-init: video state query failed");
        goto error;
    }
    oot_ps3_early_trace("rsx-init: video state and resolution ready");

    /* Render at 1280x720 on a 1080p display (the TV scales): 2.25x fewer pixels to shade,
     * and the RSX was the limit in wide views at 1080p. USRDIR/res1080 keeps native 1080p. */
    {
        FILE* keep1080 = fopen("/dev_hdd0/game/OOTP30048/USRDIR/res1080", "rb");
        if (keep1080 != NULL) {
            fclose(keep1080);
            oot_ps3_early_trace("rsx-init: USRDIR/res1080 present; keeping the display resolution");
        } else if (state.displayMode.resolution == VIDEO_RESOLUTION_1080) {
            videoResolution res720;
            videoConfiguration try720;
            memset(&try720, 0, sizeof(try720));
            if (videoGetResolution(VIDEO_RESOLUTION_720, &res720) == 0) {
                try720.resolution = VIDEO_RESOLUTION_720;
                try720.format = VIDEO_BUFFER_FORMAT_XRGB;
                try720.pitch = res720.width * sizeof(u32);
                try720.aspect = state.displayMode.aspect;
                if (videoConfigure(0, &try720, NULL, 0) == 0) {
                    state.displayMode.resolution = VIDEO_RESOLUTION_720;
                    resolution = res720;
                    oot_ps3_early_trace("rsx-init: rendering at 1280x720 (display was 1080p)");
                } else {
                    oot_ps3_early_trace("rsx-init: 720p refused; keeping 1080p");
                }
            }
        }
    }

    memset(&configuration, 0, sizeof(configuration));
    configuration.resolution = state.displayMode.resolution;
    configuration.format = VIDEO_BUFFER_FORMAT_XRGB;
    configuration.pitch = resolution.width * sizeof(u32);
    configuration.aspect = state.displayMode.aspect;
    if (state.displayMode.aspect == VIDEO_ASPECT_4_3) {
        sPs3DisplayAspect = 4.0f / 3.0f;
    } else if (state.displayMode.aspect == VIDEO_ASPECT_16_9) {
        sPs3DisplayAspect = 16.0f / 9.0f;
    } else {
        sPs3DisplayAspect = resolution.height <= 576 ? 4.0f / 3.0f : 16.0f / 9.0f;
    }
    {
        char line[96];
        snprintf(line, sizeof(line), "rsx-init: display %ux%u aspect %s (mode %u)", (unsigned)resolution.width,
                 (unsigned)resolution.height, sPs3DisplayAspect < 1.5f ? "4:3" : "16:9",
                 (unsigned)state.displayMode.aspect);
        oot_ps3_early_trace(line);
    }

    oot_ps3_early_trace("rsx-init: before videoConfigure");
    if (videoConfigure(0, &configuration, NULL, 0) != 0) {
        oot_ps3_early_trace("rsx-init: videoConfigure failed");
        goto error;
    }
    oot_ps3_early_trace("rsx-init: after videoConfigure");
    gcmSetFlipMode(GCM_FLIP_VSYNC);
    oot_ps3_early_trace("rsx-init: flip mode configured");
    gcmSetVBlankHandler(Ps3VBlankHandler);
    oot_ps3_early_trace("rsx-init: vblank counter registered");

    for (s32 i = 0; i < OOT_PS3_BUFFER_COUNT; ++i) {
        oot_ps3_early_trace(i == 0 ? "rsx-init: before display buffer 0" :
                                      "rsx-init: before display buffer 1");
        if (!make_buffer(&screen->buffers[i], resolution.width, resolution.height, i)) {
            oot_ps3_early_trace("rsx-init: display buffer creation failed");
            goto error;
        }
        oot_ps3_early_trace(i == 0 ? "rsx-init: after display buffer 0" :
                                      "rsx-init: after display buffer 1");
    }

    gcmResetFlipStatus();
    oot_ps3_early_trace("rsx-init: flip status reset");

    /* Put visible pixels in both buffers before the first flip. */
    oot_ps3_early_trace("rsx-init: before initial buffer fills");
    oot_ps3_screen_fill(&screen->buffers[0], 0x00FFFFFFU, 0);
    oot_ps3_screen_fill(&screen->buffers[1], 0x0000AAFFU, 0);
    oot_ps3_early_trace("rsx-init: after initial buffer fills");

    oot_ps3_early_trace("rsx-init: before first gcmSetFlip");
    if (gcmSetFlip(screen->context, 0) != 0) {
        oot_ps3_early_trace("rsx-init: first gcmSetFlip failed");
        goto error;
    }
    oot_ps3_early_trace("rsx-init: after first gcmSetFlip");
    rsxFlushBuffer(screen->context);
    oot_ps3_early_trace("rsx-init: command buffer flushed");
    gcmSetWaitFlip(screen->context);
    oot_ps3_early_trace("rsx-init: wait-flip command queued");
    /* Buffer 0 was just queued for display.  Draw the next frame into the
     * other buffer so the title preview is visible after its own flip. */
    screen->current_buffer = 1;
    oot_ps3_early_trace("rsx-init: completed");
    return 1;

error:
    oot_ps3_early_trace("rsx-init: entering cleanup");
    oot_ps3_screen_shutdown(screen);
    oot_ps3_early_trace("rsx-init: cleanup completed");
    return 0;
}

void oot_ps3_screen_fill(OotPs3DisplayBuffer* buffer, u32 color, u32 frame) {
    (void)frame;
    for (u32 y = 0; y < buffer->height; ++y) {
        u32* row = buffer->pixels + (y * buffer->width);
        for (u32 x = 0; x < buffer->width; ++x) {
            row[x] = color;
        }
    }
}

void oot_ps3_screen_rect(OotPs3DisplayBuffer* buffer, u16 x, u16 y, u16 width, u16 height, u32 color) {
    const u32 end_x = ((u32)x + width > buffer->width) ? buffer->width : (u32)x + width;
    const u32 end_y = ((u32)y + height > buffer->height) ? buffer->height : (u32)y + height;

    for (u32 row_y = y; row_y < end_y; ++row_y) {
        u32* row = buffer->pixels + (row_y * buffer->width);
        for (u32 col_x = x; col_x < end_x; ++col_x) {
            row[col_x] = color;
        }
    }
}

void oot_ps3_screen_present(OotPs3Screen* screen) {
    /* No CPU wait for the previous flip any more: gcmSetWaitFlip (queued after every
     * flip below) already makes the RSX wait for it before drawing the next frame, which
     * is what protects the buffer on screen. The CPU poll blocked up to a full vsync
     * interval per frame (~9 ms average at 40 fps). Set to 1 to restore it. */
    if (0) {
        unsigned long long waitStartUs = oot_ps3_microseconds_now();
        wait_for_flip();
        {
            unsigned long long waitElapsedUs = oot_ps3_microseconds_now() - waitStartUs;
            sPs3FlipWaitUs += waitElapsedUs;
            char waitTrace[96];
            snprintf(waitTrace, sizeof(waitTrace), "rsx-present: wait_for_flip took %llu us", waitElapsedUs);
            oot_ps3_early_trace(waitTrace);
        }
    }
    const s32 id = screen->buffers[screen->current_buffer].id;
    if (gcmSetFlip(screen->context, id) == 0) {
        rsxFlushBuffer(screen->context);
        gcmSetWaitFlip(screen->context);
    }
    screen->current_buffer = (screen->current_buffer + 1) % OOT_PS3_BUFFER_COUNT;
}

void oot_ps3_screen_shutdown(OotPs3Screen* screen) {
    if (screen->context != NULL) {
        gcmSetWaitFlip(screen->context);
        rsxFinish(screen->context, 1);
    }
    for (s32 i = 0; i < OOT_PS3_BUFFER_COUNT; ++i) {
        if (screen->buffers[i].pixels != NULL) {
            rsxFree(screen->buffers[i].pixels);
            screen->buffers[i].pixels = NULL;
        }
    }
    free(screen->host_memory);
    screen->host_memory = NULL;
    screen->context = NULL;
}
