// SPDX-License-Identifier: GPL-3.0-or-later
/* Game menu: opened and closed with SELECT, drawn as a panel on the left of the game.
 * Volumes (master / music / sound effects), the R3 debug menu switch, reset to the title
 * screen and quit. Settings live in USRDIR/settings.txt (debug menu: USRDIR/debug_menu_on).
 * While it is open the game gets no controller input. PS3 system headers only. */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ps3_ui.h"

#ifndef OOT_PS3_PORT_VERSION
#define OOT_PS3_PORT_VERSION "dev"
#endif
#define USRDIR "/dev_hdd0/game/OOTP30048/USRDIR"
#define SETTINGS_PATH USRDIR "/settings.txt"
#define DEBUG_MENU_FLAG USRDIR "/debug_menu_on"

/* Buttons passed by shipwright_os_ps3.c (same bits as the debug menu, plus SELECT). */
#define BTN_UP 0x0002U
#define BTN_DOWN 0x0004U
#define BTN_LEFT 0x0008U
#define BTN_RIGHT 0x0010U
#define BTN_CROSS 0x0020U
#define BTN_CIRCLE 0x0040U
#define BTN_SELECT 0x0200U

void oot_ps3_early_trace(const char* message);
void oot_ps3_audio_set_master(float volume);       /* ps3_audio.c */
int oot_ps3_audio_is_ready(void);                  /* ps3_audio.c */
void oot_ps3_set_game_volumes(float music, float sfx); /* ps3_debug_game.c */
int oot_ps3_debug_reset_to_title(void);            /* ps3_debug_game.c */
void oot_ps3_request_exit(void);                   /* ps3_early_entry.c */
int oot_ps3_debug_menu_allowed(void);              /* ps3_debug_overlay.c */
void oot_ps3_debug_menu_set_allowed(int on);
void oot_ps3_savestate_request(int load, int slot);     /* ps3_savestate.c */
int oot_ps3_savestate_slot_used(int slot);
const char* oot_ps3_savestate_status(unsigned int* serial);

enum { ITEM_RESUME, ITEM_SAVESTATE, ITEM_LOADSTATE, ITEM_MASTER, ITEM_MUSIC, ITEM_SFX, ITEM_RUMBLE, ITEM_DEBUG, ITEM_RESET, ITEM_QUIT, ITEM_COUNT };

static int sOpen, sSel, sConfirm, sSwallow;
static unsigned int sPrev, sHeld;
static int sVolume[3] = { 100, 100, 100 }; /* master, music, sfx (percent) */
static int sLoaded, sApplied;
static int sRumble = 1;
static int sStateSlot; /* 0..2 */

int oot_ps3_game_menu_rumble_enabled(void) {
    return sRumble;
}

static void LoadSettings(void) {
    char text[256];
    int fd, n;
    sLoaded = 1;
    fd = open(SETTINGS_PATH, O_RDONLY);
    if (fd < 0) return;
    n = (int)read(fd, text, sizeof(text) - 1);
    close(fd);
    if (n <= 0) return;
    text[n] = '\0';
    {
        static const char* const kKeys[3] = { "master=", "music=", "sfx=" };
        const char* rumble = strstr(text, "rumble=");
        if (rumble != NULL) sRumble = atoi(rumble + 7) != 0;
        int i;
        for (i = 0; i < 3; ++i) {
            const char* at = strstr(text, kKeys[i]);
            if (at != NULL) {
                const int v = atoi(at + strlen(kKeys[i]));
                sVolume[i] = v < 0 ? 0 : v > 100 ? 100 : v;
            }
        }
    }
}

static void SaveSettings(void) {
    char text[96];
    const int fd = open(SETTINGS_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    int n;
    if (fd < 0) return;
    n = snprintf(text, sizeof(text), "master=%d\nmusic=%d\nsfx=%d\nrumble=%d\n", sVolume[0], sVolume[1], sVolume[2],
                 sRumble);
    (void)write(fd, text, (size_t)n);
    close(fd);
}

static void ApplyVolumes(void) {
    oot_ps3_audio_set_master(sVolume[0] / 100.0f);
    oot_ps3_set_game_volumes(sVolume[1] / 100.0f, sVolume[2] / 100.0f);
}

int oot_ps3_game_menu_open(void) {
    return sOpen;
}

/* Called with the pad state on every game input read (game thread). Returns 1 when the
 * game must not see the buttons (menu open, or buttons still held after closing it). */
int oot_ps3_game_menu_input(unsigned int buttons) {
    const unsigned int pressed = buttons & ~sPrev;
    unsigned int repeat = 0;
    if (!sLoaded) LoadSettings();
    if (!sApplied && oot_ps3_audio_is_ready()) {
        sApplied = 1;
        ApplyVolumes();
    }
    if (buttons != 0 && buttons == sPrev) {
        if (++sHeld > 6 && (sHeld & 1U)) repeat = buttons & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT);
    } else {
        sHeld = 0;
    }
    sPrev = buttons;
    if (!sOpen) {
        if (pressed & BTN_SELECT) {
            sOpen = 1;
            sSel = 0;
            sConfirm = 0;
            oot_ps3_early_trace("trace: game menu opened");
            return 1;
        }
        if (sSwallow) {
            if (buttons == 0) sSwallow = 0;
            return 1;
        }
        return 0;
    }
    {
        const unsigned int act = pressed | repeat;
        int close = 0;
        if (act & BTN_UP) { sSel = (sSel + ITEM_COUNT - 1) % ITEM_COUNT; sConfirm = 0; }
        if (act & BTN_DOWN) { sSel = (sSel + 1) % ITEM_COUNT; sConfirm = 0; }
        if ((sSel == ITEM_SAVESTATE || sSel == ITEM_LOADSTATE) && (act & (BTN_LEFT | BTN_RIGHT)))
            sStateSlot = (sStateSlot + ((act & BTN_RIGHT) ? 1 : 2)) % 3;
        if (sSel >= ITEM_MASTER && sSel <= ITEM_SFX && (act & (BTN_LEFT | BTN_RIGHT))) {
            int* v = &sVolume[sSel - ITEM_MASTER];
            *v += (act & BTN_RIGHT) ? 5 : -5;
            if (*v < 0) *v = 0;
            if (*v > 100) *v = 100;
            ApplyVolumes();
            SaveSettings();
        }
        if (pressed & BTN_CROSS) {
            switch (sSel) {
                case ITEM_RESUME: close = 1; break;
                case ITEM_SAVESTATE:
                case ITEM_LOADSTATE:
                    oot_ps3_savestate_request(sSel == ITEM_LOADSTATE, sStateSlot);
                    close = 1;
                    break;
                case ITEM_DEBUG: oot_ps3_debug_menu_set_allowed(!oot_ps3_debug_menu_allowed()); break;
                case ITEM_RUMBLE:
                    sRumble = !sRumble;
                    SaveSettings();
                    break;
                case ITEM_RESET:
                    if (!sConfirm) {
                        sConfirm = 1;
                    } else if (oot_ps3_debug_reset_to_title()) {
                        close = 1;
                    }
                    break;
                case ITEM_QUIT:
                    if (!sConfirm) sConfirm = 1;
                    else oot_ps3_request_exit();
                    break;
                default: break;
            }
        }
        if (pressed & (BTN_SELECT | BTN_CIRCLE)) close = 1;
        if (close) {
            sOpen = 0;
            sSwallow = 1; /* the closing press must not reach the game */
            oot_ps3_early_trace("trace: game menu closed");
        }
    }
    return 1;
}

/* Drawn on top of every frame (render thread), before the debug menu. */
void oot_ps3_game_menu_frame(unsigned int screenWidth, unsigned int screenHeight) {
    static const char* const kLabels[ITEM_COUNT] = {
        "Resume", "Save state", "Load state", "Master volume", "Music volume", "Sound effects volume", "Rumble", "Debug menu (R3)", "Reset game",
        "Quit to XMB",
    };
    const float panelW = 680.0f, left = 56.0f;
    float y;
    int i;
    {
        /* Save-state result: shown for ~2.5 s at the top left, menu open or not. */
        static unsigned int sSeenSerial, sToastFrames;
        unsigned int serial = 0;
        const char* status = oot_ps3_savestate_status(&serial);
        if (serial != sSeenSerial) {
            sSeenSerial = serial;
            sToastFrames = 150;
        }
        if (sToastFrames > 0 && !sOpen && ps3ui_begin(screenWidth, screenHeight)) {
            const float w = ps3ui_text_width(status, 30, 0) + 48;
            --sToastFrames;
            ps3ui_rect(40, 40, w, 56, 0x0B1630E0);
            ps3ui_rect(40, 40, 5, 56, 0x96D2FFFF);
            ps3ui_text(64, 52, 30, 0xFFFFFFFF, 0, status);
            ps3ui_end();
        }
    }
    if (!sOpen || !ps3ui_begin(screenWidth, screenHeight)) return;
    ps3ui_rect(0, 0, ps3ui_width(), 1080, 0x00000060); /* dim the game a little */
    ps3ui_gradient(0, 0, panelW, 1080, 0x0B1630F0, 0x102448F0, 24);
    ps3ui_rect(panelW, 0, 3, 1080, 0x5FA8E8FF);
    ps3ui_text(left, 70, 52, 0x96D2FFFF, 1, "Ship of Gadeguion");
    ps3ui_text(left, 136, 26, 0xB4C3D8FF, 0, "Menu");
    y = 200;
    for (i = 0; i < ITEM_COUNT; ++i) {
        const int selected = i == sSel;
        const uint32_t color = selected ? 0xFFFFFFFF : 0xC8D2E0FF;
        char value[32] = "";
        if (selected) {
            ps3ui_rect(left - 24, y - 9, panelW - left + 8, 54, 0x2E5C9AC0);
            ps3ui_rect(left - 24, y - 9, 6, 54, 0x96D2FFFF);
        }
        ps3ui_text(left, y, 32, color, selected, kLabels[i]);
        if (i == ITEM_SAVESTATE || i == ITEM_LOADSTATE) {
            snprintf(value, sizeof(value), "< Slot %d >", sStateSlot + 1);
            if (i == ITEM_LOADSTATE && !oot_ps3_savestate_slot_used(sStateSlot)) snprintf(value, sizeof(value), "< Slot %d: empty >", sStateSlot + 1);
            ps3ui_text_right(panelW - 34, y + 2, 26, selected ? 0xFFFFFFFFU : 0xB4C3D8FFU, 0, value);
        } else if (i >= ITEM_MASTER && i <= ITEM_SFX) {
            const int v = sVolume[i - ITEM_MASTER];
            const float barX = panelW - 230, barW = 150;
            ps3ui_rect(barX, y + 16, barW, 8, 0x2A3A58FF);
            ps3ui_rect(barX, y + 16, barW * v / 100.0f, 8, selected ? 0x96D2FFFF : 0x6E9CC8FF);
            snprintf(value, sizeof(value), "%d", v);
            ps3ui_text_right(panelW - 34, y + 2, 26, color, 0, value);
        } else if (i == ITEM_RUMBLE) {
            ps3ui_text_right(panelW - 34, y + 2, 26, sRumble ? 0x8CF0A0FFU : 0xB4C3D8FFU, 0, sRumble ? "On" : "Off");
        } else if (i == ITEM_DEBUG) {
            ps3ui_text_right(panelW - 34, y + 2, 26, oot_ps3_debug_menu_allowed() ? 0x8CF0A0FF : 0xB4C3D8FF, 0,
                             oot_ps3_debug_menu_allowed() ? "On" : "Off");
        } else if ((i == ITEM_RESET || i == ITEM_QUIT) && selected && sConfirm) {
            ps3ui_text_right(panelW - 34, y + 2, 26, 0xFFD25AFF, 0, "Press X again");
        }
        y += 64;
        if (i == ITEM_LOADSTATE || i == ITEM_SFX || i == ITEM_DEBUG) {
            ps3ui_rect(left, y - 12, panelW - left - 40, 1, 0x3A4C70FF);
            y += 14;
        }
    }
    if (sSel == ITEM_RESET) ps3ui_text(left, y + 10, 24, 0x9AAAC4FF, 0, "Back to the title screen (unsaved progress is lost).");
    if (sSel == ITEM_DEBUG) ps3ui_text(left, y + 10, 24, 0x9AAAC4FF, 0, "Warps, cutscenes, items, performance info.");
    if (sSel == ITEM_SAVESTATE || sSel == ITEM_LOADSTATE)
        ps3ui_text(left, y + 10, 24, 0x9AAAC4FF, 0, "3 slots; kept until you quit the game.");
    ps3ui_text(left, 1080 - 104, 24, 0x9AAAC4FF, 0, "X  select     Left/Right  adjust     O  close");
    ps3ui_text(left, 1080 - 64, 22, 0x7888A4FF, 0, "Port made by Gadega");
    ps3ui_text_right(panelW - 34, 1080 - 64, 22, 0x7888A4FF, 0, "v" OOT_PS3_PORT_VERSION);
    ps3ui_end();
}
