/*
 * File: z_title.c
 * Overlay: ovl_title
 * Description: Displays the Nintendo Logo
 */

#include "global.h"
#include "alloca.h"
#include "textures/nintendo_rogo_static/nintendo_rogo_static.h"
#include "assets/objects/gameplay_keep/gameplay_keep.h"
#include <soh/Enhancements/bootcommands.h>
#include <soh/GameVersions.h>
#include <soh/SaveManager.h>
#include "soh/ResourceManagerHelpers.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include <string.h>
#include <stdlib.h>

#include "time.h"

#ifdef __PS3__
void oot_ps3_early_trace(const char* message);
static void TitlePs3Trace(const char* message) {
    oot_ps3_early_trace(message);
}
#else
static void TitlePs3Trace(const char* message) {
    (void)message;
}
#endif

// Note: In other rom versions this function also updates unk_1D4, coverAlpha, addAlpha, visibleDuration to calculate
// the fade-in/fade-out + the duration of the n64 logo animation
void Title_Calc(TitleContext* this) {
    if ((this->coverAlpha == 0) && (this->visibleDuration != 0)) {
        this->visibleDuration--;
        this->unk_1D4--;
        if (this->unk_1D4 == 0) {
            this->unk_1D4 = 400;
        }
    } else {
        this->coverAlpha += this->addAlpha;
        if (this->coverAlpha <= 0) {
            this->coverAlpha = 0;
#ifdef __PS3__
            this->addAlpha = 32;
            TitlePs3Trace("title-calc: fade-in completed; fast fade-out armed");
#else
            this->addAlpha = 3;
#endif
        } else if (this->coverAlpha >= 0xFF) {
            this->coverAlpha = 0xFF;
            this->exit = 1;
#ifdef __PS3__
            TitlePs3Trace("title-calc: fade-out completed; natural Opening handoff ready");
#endif
        }
    }
    this->uls = this->ult & 0x7F;
    this->ult++;
}

void Title_SetupView(TitleContext* this, f32 x, f32 y, f32 z) {
    View* view = &this->view;
    Vec3f eye;
    Vec3f lookAt;
    Vec3f up;

    eye.x = x;
    eye.y = y;
    eye.z = z;
    up.x = up.z = 0.0f;
    lookAt.x = lookAt.y = lookAt.z = 0.0f;
    up.y = 1.0f;

    func_800AA460(view, 30.0f, 10.0f, 12800.0f);
    func_800AA358(view, &eye, &lookAt, &up);
    func_800AAA50(view, 0xF);
}

void Title_Draw(TitleContext* this) {
    static s16 sTitleRotY = 0;
    static Lights1 sTitleLights = gdSPDefLights1(0x64, 0x64, 0x64, 0xFF, 0xFF, 0xFF, 0x45, 0x45, 0x45);

    u16 y;
    u16 idx;
    s32 pad1;
    Vec3f v3;
    Vec3f v1;
    Vec3f v2;
    s32 pad2[2];

    TitlePs3Trace("title-draw: entered");
    TitlePs3Trace("title-draw: before OPEN_DISPS");
    OPEN_DISPS(this->state.gfxCtx);
    TitlePs3Trace("title-draw: after OPEN_DISPS");

    TitlePs3Trace("title-draw: before light vectors");
    v3.x = 69;
    v3.y = 69;
    v3.z = 69;
    v2.x = -4949.148;
    v2.y = 4002.5417;
    v1.x = 0;
    v1.y = 0;
    v1.z = 0;
    v2.z = 1119.0837;

    TitlePs3Trace("title-draw: before func_8002EABC");
    func_8002EABC(&v1, &v2, &v3, this->state.gfxCtx);
    TitlePs3Trace("title-draw: after func_8002EABC");
    TitlePs3Trace("title-draw: before lights");
    gSPSetLights1(POLY_OPA_DISP++, sTitleLights);
    TitlePs3Trace("title-draw: after lights");
    TitlePs3Trace("title-draw: before setup view");
    Title_SetupView(this, 0, 150.0, 300.0);
    TitlePs3Trace("title-draw: after setup view");
    TitlePs3Trace("title-draw: before Gfx_SetupDL_25Opa");
    Gfx_SetupDL_25Opa(this->state.gfxCtx);
    TitlePs3Trace("title-draw: after Gfx_SetupDL_25Opa");
    TitlePs3Trace("title-draw: before matrix transforms");
    Matrix_Translate(-53.0, -5.0, 0, MTXMODE_NEW);
    Matrix_Scale(1.0, 1.0, 1.0, MTXMODE_APPLY);
    Matrix_RotateZYX(0, sTitleRotY, 0, MTXMODE_APPLY);
    TitlePs3Trace("title-draw: after matrix transforms");

    TitlePs3Trace("title-draw: before logo matrix command");
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(this->state.gfxCtx), G_MTX_LOAD);
    TitlePs3Trace("title-draw: after logo matrix command");
    TitlePs3Trace("title-draw: before logo display list");
#ifdef __PS3__
    if (ResourceMgr_LoadGfxByName(gNintendo64LogoDL) != NULL) {
        gSPDisplayList(POLY_OPA_DISP++, gNintendo64LogoDL);
        TitlePs3Trace("title-draw: real O2R Nintendo 64 logo display list emitted");
    } else {
        TitlePs3Trace("title-draw: O2R Nintendo 64 logo display list unavailable");
    }
#else
    gSPDisplayList(POLY_OPA_DISP++, gNintendo64LogoDL);
    TitlePs3Trace("title-draw: after logo display list");
#endif
    TitlePs3Trace("title-draw: before logo material commands");
    Gfx_SetupDL_39Opa(this->state.gfxCtx);
    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetCycleType(POLY_OPA_DISP++, G_CYC_2CYCLE);
    gDPSetRenderMode(POLY_OPA_DISP++, G_RM_XLU_SURF2, G_RM_OPA_CI | CVG_DST_WRAP);
    gDPSetCombineLERP(POLY_OPA_DISP++, TEXEL1, PRIMITIVE, ENV_ALPHA, TEXEL0, 0, 0, 0, TEXEL0, PRIMITIVE, ENVIRONMENT,
                      COMBINED, ENVIRONMENT, COMBINED, 0, PRIMITIVE, 0);
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 255, 255, 255, 255);
    gDPSetEnvColor(POLY_OPA_DISP++, 0, 0, 255, 128);
    TitlePs3Trace("title-draw: after logo material commands");

    TitlePs3Trace("title-draw: before small texture load");
    gDPLoadMultiBlock(POLY_OPA_DISP++, nintendo_rogo_static_Tex_001800, 0x100, 1, G_IM_FMT_I, G_IM_SIZ_8b, 32, 32, 0,
                      G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, 5, 5, 2, 11);
    TitlePs3Trace("title-draw: after small texture load");

    TitlePs3Trace("title-draw: before texture rectangle loop");
    for (idx = 0, y = 94; idx < 16; idx++, y += 2) {
        gDPLoadMultiTile(POLY_OPA_DISP++, nintendo_rogo_static_Tex_000000, 0, G_TX_RENDERTILE, G_IM_FMT_I, G_IM_SIZ_8b,
                         192, 32, 0, idx * 2, 192 - 1, (idx + 1) * 2 - 1, 0, G_TX_NOMIRROR | G_TX_WRAP,
                         G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);

        gDPSetTileSize(POLY_OPA_DISP++, 0, 0, 0, (192 - 1) << G_TEXTURE_IMAGE_FRAC, (2 - 1) << G_TEXTURE_IMAGE_FRAC);

        gDPSetTileSize(POLY_OPA_DISP++, 1, this->uls, (this->ult & 0x7F) - idx * 4, 0, 0);
        gSPTextureRectangle(POLY_OPA_DISP++, 388, y << 2, 1156, (y + 2) << 2, G_TX_RENDERTILE, 0, 0, 1 << 10, 1 << 10);
    }
    TitlePs3Trace("title-draw: after texture rectangle loop");

    TitlePs3Trace("title-draw: before Environment_FillScreen");
    Environment_FillScreen(this->state.gfxCtx, 0, 0, 0, (s16)this->coverAlpha, FILL_SCREEN_XLU);
    TitlePs3Trace("title-draw: after Environment_FillScreen");

    sTitleRotY += 300;

    TitlePs3Trace("title-draw: before CLOSE_DISPS");
    CLOSE_DISPS(this->state.gfxCtx);
    TitlePs3Trace("title-draw: completed");
}

void Title_Main(GameState* thisx) {
    TitleContext* this = (TitleContext*)thisx;

    TitlePs3Trace("title-main: entered");
    TitlePs3Trace("title-main: before OPEN_DISPS");
    OPEN_DISPS(this->state.gfxCtx);
    TitlePs3Trace("title-main: after OPEN_DISPS");

    TitlePs3Trace("title-main: before segment setup");
    gSPSegment(POLY_OPA_DISP++, 0, NULL);
    gSPSegment(POLY_OPA_DISP++, 1, this->staticSegment);
    TitlePs3Trace("title-main: after segment setup");
    TitlePs3Trace("title-main: before Gfx_SetupFrame");
    Gfx_SetupFrame(this->state.gfxCtx, 0, 0, 0);
    TitlePs3Trace("title-main: after Gfx_SetupFrame");
    TitlePs3Trace("title-main: before Title_Calc");
    Title_Calc(this);
    TitlePs3Trace("title-main: after Title_Calc");
    TitlePs3Trace("title-main: before Title_Draw");
    Title_Draw(this);
    TitlePs3Trace("title-main: after Title_Draw");

    if (this->exit) {
#ifdef __PS3__
        TitlePs3Trace("title-main: native fade completed; Opening selected");
#endif
        gSaveContext.seqId = (u8)NA_BGM_DISABLED;
        gSaveContext.natureAmbienceId = 0xFF;
        gSaveContext.gameMode = GAMEMODE_TITLE_SCREEN;
        this->state.running = false;
        SET_NEXT_GAMESTATE(&this->state, Opening_Init, OpeningContext);
    }

    CLOSE_DISPS(this->state.gfxCtx);
}

void Title_Destroy(GameState* thisx) {
    TitleContext* this = (TitleContext*)thisx;

    TitlePs3Trace("title-destroy: entered");
    Sram_InitSram(&this->state);
    TitlePs3Trace("title-destroy: after Sram_InitSram");
}

void Title_Init(GameState* thisx) {
    TitleContext* this = (TitleContext*)thisx;

    TitlePs3Trace("title-init: entered");
    this->staticSegment = NULL;
    osSyncPrintf("z_title.c\n");

    R_UPDATE_RATE = 1;
    TitlePs3Trace("title-init: before Matrix_Init");
    Matrix_Init(&this->state);
    TitlePs3Trace("title-init: after Matrix_Init");
    TitlePs3Trace("title-init: before View_Init");
    View_Init(&this->view, this->state.gfxCtx);
    TitlePs3Trace("title-init: after View_Init");
    this->state.main = Title_Main;
    this->state.destroy = Title_Destroy;
    this->exit = false;
    gSaveContext.fileNum = 0xFF;
    this->ult = 0;
    this->unk_1D4 = 0x14;
    this->coverAlpha = 255;
    this->addAlpha = -3;
    this->visibleDuration = 0x3C;
    TitlePs3Trace("title-init: before interactor hook");
    GameInteractor_ExecuteOnZTitleInit(this);
#ifdef __PS3__
    // Preserve Title_Calc and Title_Main's real state transition, but finish
    // both fade directions within the bounded PS3 probe (~26 title frames).
    this->addAlpha = -32;
    this->visibleDuration = 10;
    TitlePs3Trace("title-init: accelerated native fade for Opening handoff");
#endif
    TitlePs3Trace("title-init: completed");
}
