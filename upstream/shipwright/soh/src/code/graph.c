#include "global.h"
#include "vt.h"
#include "regs.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <libultraship/bridge/gfxdebuggerbridge.h>
#include <libultraship/bridge/windowbridge.h>
#include "soh/Enhancements/gameconsole.h"
#include "soh/OTRGlobals.h"

#ifdef __PS3__
#include <unistd.h>
void oot_ps3_early_trace(const char* message);
void oot_ps3_early_trace_flush_usb(void);
void oot_ps3_trace_compact_play(void);
void oot_ps3_trace_full_detail(void);
s32 Camera_Ps3OOBTimer(void);
s32 oot_ps3_audio_is_ready(void);
void oot_ps3_audio_kick(void);
void oot_ps3_render_sync(void); // shipwright_bootstrap_ps3.cpp (render thread)
void oot_ps3_audio_lock(void);
void oot_ps3_audio_unlock(void);
static unsigned long long sPs3UpdAcc, sPs3GfxAcc, sPs3ResAcc;
static unsigned long long sPs3LastLogicUs, sPs3LastGfxUs;
/* 0 GameState_Update inside Graph_Update, 1 whole Graph_Update (logicprof line). */
unsigned long long gPs3GraphStepUs[2];
extern unsigned long long oot_ps3_screen_take_flip_wait_us(void);
extern void oot_ps3_fast3d_take_run_stats(unsigned long long* us, unsigned int* calls);
void oot_ps3_video_begin_play_probe(void);
void oot_ps3_graph_set_opaque_list(const Gfx* commands, size_t count);
static void GraphPs3Trace(const char* message) {
    oot_ps3_early_trace(message);
}

/* Capture the real title-frame command stream before adding a PS3 Fast3D
 * interpreter.  Each stream has an explicit array bound; never follow the
 * display-list pointers in w1 during this probe. */
static void GraphPs3ProbeDisplayList(const char* name, const Gfx* list, size_t capacity) {
    static const unsigned char interesting[] = {
        0x00U, 0x01U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U, 0xBFU,
        0xD9U, 0xDAU, 0xDBU, 0xDEU, 0xDFU, 0xE4U, 0xE5U, 0xE7U,
        0xEDU, 0xF3U, 0xF5U, 0xF6U, 0xF7U, 0xFAU, 0xFBU, 0xFCU,
        0xFDU, 0xFEU, 0xFFU
    };
    const size_t limit = capacity < 2048U ? capacity : 2048U;
    const char* stop = "limit";
    size_t inspected = 0;
    char line[112];
    unsigned int seen[256] = { 0 };
    size_t first[256] = { 0 };
    u32 firstW0[256] = { 0 };
    u32 firstW1[256] = { 0 };
    for (size_t i = 0; i < limit; ++i) {
        const u32 w0 = list[i].words.w0;
        const u32 w1 = list[i].words.w1;
        const u32 opcode = w0 >> 24;
        if (seen[opcode]++ == 0U) {
            first[opcode] = i;
            firstW0[opcode] = w0;
            firstW1[opcode] = w1;
        }
        if (i < 4U) {
            snprintf(line, sizeof(line), "gfx-dl: %s[%u] %08X %08X", name,
                     (unsigned)i, (unsigned)w0, (unsigned)w1);
            GraphPs3Trace(line);
        }
        inspected = i + 1U;
        if (opcode == 0xDFU || opcode == 0xB8U) {
            stop = "end";
            break;
        }
        if (opcode == 0xDEU && (w0 & 0x00010000U) != 0U) {
            stop = "branch";
            break;
        }
    }
    snprintf(line, sizeof(line), "gfx-dl: %s inspected=%u stop=%s", name,
             (unsigned)inspected, stop);
    GraphPs3Trace(line);
    for (size_t i = 0; i < sizeof(interesting); ++i) {
        const unsigned int opcode = interesting[i];
        if (seen[opcode] == 0U) continue;
        snprintf(line, sizeof(line), "gfx-dl: %s op=%02X count=%u first=%u %08X %08X",
                 name, opcode, seen[opcode], (unsigned)first[opcode],
                 (unsigned)firstW0[opcode], (unsigned)firstW1[opcode]);
        GraphPs3Trace(line);
    }
}
#else
static void GraphPs3Trace(const char* message) {
    (void)message;
}
#endif

#define GFXPOOL_HEAD_MAGIC 0x1234
#define GFXPOOL_TAIL_MAGIC 0x5678

// SOH [Port] Game State management for our render loop
static struct RunFrameContext {
    GraphicsContext gfxCtx;
    GameStateOverlay* nextOvl;
    GameStateOverlay* ovl;
    int state;
} runFrameContext;

#ifdef __PS3__
/* Release only state-owned allocations in the diagnostic path.  The normal
 * GameState_Destroy currently waits for a graphics-queue reply that the
 * bypassed PS3 task submission never sends. */
void PadMgr_PS3Poll(PadMgr* padMgr);

static void GraphPs3ReleaseStateArena(GameState* gameState) {
    GraphPs3Trace("graph: before diagnostic THA_Dt");
    THA_Dt(&gameState->tha);
    GraphPs3Trace("graph: before diagnostic GameAlloc_Cleanup");
    GameAlloc_Cleanup(&gameState->alloc);
    GraphPs3Trace("graph: before diagnostic game state free");
    SYSTEM_ARENA_FREE_DEBUG(gameState);
    GraphPs3Trace("graph: diagnostic state arena released");
}
#endif

OSTime sGraphUpdateTime;
OSTime sGraphSetTaskTime;
FaultClient sGraphFaultClient;
CfbInfo sGraphCfbInfos[3];
FaultClient sGraphUcodeFaultClient;

void PadMgr_ThreadEntry(PadMgr* padMgr);

// clang-format off
UCodeInfo D_8012D230[3] = {
    //{ UCODE_F3DZEX, D_80155F50 },
    { UCODE_UNK, NULL },
    //{ UCODE_S2DEX, D_80113070 },
};

UCodeInfo D_8012D248[3] = {
    //{ UCODE_F3DZEX, D_80155F50 },
    { UCODE_UNK, NULL },
    //{ UCODE_S2DEX, D_80113070 },
};
// clang-format on

void Graph_FaultClient() {
    void* nextFb = osViGetNextFramebuffer();
    void* newFb = ((uintptr_t)SysCfb_GetFbPtr(0) != (uintptr_t)nextFb) ? SysCfb_GetFbPtr(0) : SysCfb_GetFbPtr(1);

    osViSwapBuffer(newFb);
    Fault_WaitForInput();
    osViSwapBuffer(nextFb);
}

void Graph_DisassembleUCode(Gfx* workBuf) {
#if 0
    UCodeDisas disassembler;

    if (HREG(80) == 7 && HREG(81) != 0) {
        UCodeDisas_Init(&disassembler);
        disassembler.enableLog = HREG(83);
        UCodeDisas_RegisterUCode(&disassembler, ARRAY_COUNT(D_8012D230), D_8012D230);
        //UCodeDisas_SetCurUCode(&disassembler, D_80155F50);
        UCodeDisas_Disassemble(&disassembler, workBuf);
        HREG(93) = disassembler.dlCnt;
        HREG(84) = disassembler.tri2Cnt * 2 + disassembler.tri1Cnt + (disassembler.quadCnt * 2) + disassembler.lineCnt;
        HREG(85) = disassembler.vtxCnt;
        HREG(86) = disassembler.spvtxCnt;
        HREG(87) = disassembler.tri1Cnt;
        HREG(88) = disassembler.tri2Cnt;
        HREG(89) = disassembler.quadCnt;
        HREG(90) = disassembler.lineCnt;
        HREG(91) = disassembler.syncErr;
        HREG(92) = disassembler.loaducodeCnt;
        if (HREG(82) == 1 || HREG(82) == 2) {
            osSyncPrintf("vtx_cnt=%d\n", disassembler.vtxCnt);
            osSyncPrintf("spvtx_cnt=%d\n", disassembler.spvtxCnt);
            osSyncPrintf("tri1_cnt=%d\n", disassembler.tri1Cnt);
            osSyncPrintf("tri2_cnt=%d\n", disassembler.tri2Cnt);
            osSyncPrintf("quad_cnt=%d\n", disassembler.quadCnt);
            osSyncPrintf("line_cnt=%d\n", disassembler.lineCnt);
            osSyncPrintf("sync_err=%d\n", disassembler.syncErr);
            osSyncPrintf("loaducode_cnt=%d\n", disassembler.loaducodeCnt);
            osSyncPrintf("dl_depth=%d\n", disassembler.dlDepth);
            osSyncPrintf("dl_cnt=%d\n", disassembler.dlCnt);
        }
        UCodeDisas_Destroy(&disassembler);
    }
#endif
}

void Graph_UCodeFaultClient(Gfx* workBuf) {
#if 0
    UCodeDisas disassembler;

    UCodeDisas_Init(&disassembler);
    disassembler.enableLog = true;
    UCodeDisas_RegisterUCode(&disassembler, ARRAY_COUNT(D_8012D248), D_8012D248);
    //UCodeDisas_SetCurUCode(&disassembler, D_80155F50);
    UCodeDisas_Disassemble(&disassembler, workBuf);
    UCodeDisas_Destroy(&disassembler);
#endif
}

void Graph_InitTHGA(GraphicsContext* gfxCtx) {
    GfxPool* pool = &gGfxPools[gfxCtx->gfxPoolIdx & 1];

    pool->headMagic = GFXPOOL_HEAD_MAGIC;
    pool->tailMagic = GFXPOOL_TAIL_MAGIC;
    THGA_Ct(&gfxCtx->polyOpa, pool->polyOpaBuffer, sizeof(pool->polyOpaBuffer));
    THGA_Ct(&gfxCtx->polyXlu, pool->polyXluBuffer, sizeof(pool->polyXluBuffer));
    THGA_Ct(&gfxCtx->overlay, pool->overlayBuffer, sizeof(pool->overlayBuffer));
    THGA_Ct(&gfxCtx->work, pool->workBuffer, sizeof(pool->workBuffer));

    gfxCtx->polyOpaBuffer = pool->polyOpaBuffer;
    gfxCtx->polyXluBuffer = pool->polyXluBuffer;
    gfxCtx->overlayBuffer = pool->overlayBuffer;
    gfxCtx->workBuffer = pool->workBuffer;

    gfxCtx->curFrameBuffer = (u16*)SysCfb_GetFbPtr(gfxCtx->fbIdx % 2);
    gfxCtx->unk_014 = 0;
}

GameStateOverlay* Graph_GetNextGameState(GameState* gameState) {
    void* gameStateInitFunc = GameState_GetInit(gameState);

    if (gameStateInitFunc == TitleSetup_Init) {
        return &gGameStateOverlayTable[0];
    }
    if (gameStateInitFunc == Select_Init) {
        return &gGameStateOverlayTable[1];
    }
    if (gameStateInitFunc == Title_Init) {
        return &gGameStateOverlayTable[2];
    }
    if (gameStateInitFunc == Play_Init) {
        return &gGameStateOverlayTable[3];
    }
    if (gameStateInitFunc == Opening_Init) {
        return &gGameStateOverlayTable[4];
    }
    if (gameStateInitFunc == FileChoose_Init) {
        return &gGameStateOverlayTable[5];
    }

    LOG_ADDRESS("game_init_func", gameStateInitFunc);
    return NULL;
}

void Graph_Init(GraphicsContext* gfxCtx) {
    GraphPs3Trace("graph: Graph_Init entered");
    memset(gfxCtx, 0, sizeof(GraphicsContext));
    GraphPs3Trace("graph: context cleared");
    gfxCtx->gfxPoolIdx = 0;
    gfxCtx->fbIdx = 0;
    gfxCtx->viMode = NULL;
    gfxCtx->viFeatures = gViConfigFeatures;
    gfxCtx->xScale = gViConfigXScale;
    gfxCtx->yScale = gViConfigYScale;
    GraphPs3Trace("graph: before graph message queue");
    osCreateMesgQueue(&gfxCtx->queue, gfxCtx->msgBuff, ARRAY_COUNT(gfxCtx->msgBuff));
    GraphPs3Trace("graph: after graph message queue");
    GraphPs3Trace("graph: before gfx system init");
    func_800D31F0();
    GraphPs3Trace("graph: after gfx system init");
    GraphPs3Trace("graph: before graph fault client");
    Fault_AddClient(&sGraphFaultClient, Graph_FaultClient, 0, 0);
    GraphPs3Trace("graph: Graph_Init completed");
}

void Graph_Destroy(GraphicsContext* gfxCtx) {
    func_800D3210();
    Fault_RemoveClient(&sGraphFaultClient);
}

void Graph_TaskSet00(GraphicsContext* gfxCtx) {
    static Gfx* D_8012D260 = NULL;
    static s32 sGraphCfbInfoIdx = 0;

    OSTime time;
    OSTimer timer;
    OSMesg msg;
    OSTask_t* task = &gfxCtx->task.list.t;
    OSScTask* scTask = &gfxCtx->task;
    CfbInfo* cfb;
    s32 pad1;

    D_8016A528 = osGetTime() - sGraphSetTaskTime - D_8016A558;

    osSetTimer(&timer, OS_USEC_TO_CYCLES(3000000), 0, &gfxCtx->queue, OS_MESG_32(666));

    osRecvMesg(&gfxCtx->queue, &msg, OS_MESG_BLOCK);
    osStopTimer(&timer);
// OTRTODO - Proper GFX crash handler
#if 0
    if (msg == (OSMesg)666) {
        osSyncPrintf(VT_FGCOL(RED));
        osSyncPrintf("RCPが帰ってきませんでした。"); // "RCP did not return."
        osSyncPrintf(VT_RST);
        LogUtils_LogHexDump((void*)&HW_REG(SP_MEM_ADDR_REG, u32), 0x20);
        LogUtils_LogHexDump((void*)&DPC_START_REG, 0x20);
        LogUtils_LogHexDump(gGfxSPTaskYieldBuffer, sizeof(gGfxSPTaskYieldBuffer));

        SREG(6) = -1;
        if (D_8012D260 != NULL) {
            HREG(80) = 7;
            HREG(81) = 1;
            HREG(83) = 2;
            D_8012D260 = D_8012D260;
            Graph_DisassembleUCode(D_8012D260);
        }
        Fault_AddHungupAndCrashImpl("RCP is HUNG UP!!", "Oh! MY GOD!!");
    }
#endif
    osRecvMesg(&gfxCtx->queue, &msg, OS_MESG_NOBLOCK);

    D_8012D260 = gfxCtx->workBuffer;
    if (gfxCtx->callback != NULL) {
        gfxCtx->callback(gfxCtx, gfxCtx->callbackParam);
    }

    time = osGetTime();
    if (D_8016A550 != 0) {
        D_8016A558 = (D_8016A558 + time) - D_8016A550;
        D_8016A550 = time;
    }
    D_8016A520 = D_8016A558;
    D_8016A558 = 0;
    sGraphSetTaskTime = osGetTime();

    task->type = M_GFXTASK;
    task->flags = OS_SC_DRAM_DLIST;
    task->ucode_boot = SysUcode_GetUCodeBoot();
    task->ucode_boot_size = SysUcode_GetUCodeBootSize();
    task->ucode = SysUcode_GetUCode();
    task->ucode_data = SysUcode_GetUCodeData();
    task->ucode_size = 0x1000;
    task->ucode_data_size = 0x800;
    task->dram_stack = (u64*)gGfxSPTaskStack;
    task->dram_stack_size = sizeof(gGfxSPTaskStack);
    task->output_buff = gGfxSPTaskOutputBuffer;
    task->output_buff_size = (u64*)((u8*)gGfxSPTaskOutputBuffer + sizeof(gGfxSPTaskOutputBuffer));
    task->data_ptr = (u64*)gfxCtx->workBuffer;

    OPEN_DISPS(gfxCtx);
    task->data_size = (uintptr_t)WORK_DISP - (uintptr_t)gfxCtx->workBuffer;
    CLOSE_DISPS(gfxCtx);

    task->yield_data_ptr = (u64*)gGfxSPTaskYieldBuffer;
    task->yield_data_size = sizeof(gGfxSPTaskYieldBuffer);

    scTask->next = NULL;
    scTask->flags = OS_SC_RCP_MASK | OS_SC_SWAPBUFFER | OS_SC_LAST_TASK;
    if (SREG(33) & 1) {
        SREG(33) &= ~1;
        scTask->flags &= ~OS_SC_SWAPBUFFER;
        gfxCtx->fbIdx--;
    }

    scTask->msgQ = &gfxCtx->queue;
    scTask->msg.ptr = NULL;

    cfb = &sGraphCfbInfos[sGraphCfbInfoIdx++];
    cfb->fb1 = gfxCtx->curFrameBuffer;
    cfb->swapBuffer = gfxCtx->curFrameBuffer;
    cfb->viMode = gfxCtx->viMode;
    cfb->features = gfxCtx->viFeatures;
    cfb->xScale = gfxCtx->xScale;
    cfb->yScale = gfxCtx->yScale;
    cfb->unk_10 = 0;
    cfb->updateRate = R_UPDATE_RATE;

    scTask->framebuffer = cfb;
    sGraphCfbInfoIdx = sGraphCfbInfoIdx % ARRAY_COUNT(sGraphCfbInfos);

    gfxCtx->schedMsgQ = &gSchedContext.cmdQ;

    osSendMesgPtr(&gSchedContext.cmdQ, scTask, OS_MESG_BLOCK);
    Sched_SendEntryMsg(&gSchedContext);
}

void Graph_Update(GraphicsContext* gfxCtx, GameState* gameState) {
    u32 problem;

    GraphPs3Trace("graph-update: entered");
    // Skip game frame updates while gfx debugger is active, and execute with the last frame's DL buffer
    if (GfxDebuggerIsDebugging()) {
        GraphPs3Trace("graph-update: debugger path before process commands");
        Graph_ProcessGfxCommands(runFrameContext.gfxCtx.workBuffer);
        GraphPs3Trace("graph-update: debugger path completed");
        return;
    }

    gameState->unk_A0 = 0;
    GraphPs3Trace("graph-update: before Graph_InitTHGA");
    Graph_InitTHGA(gfxCtx);
    GraphPs3Trace("graph-update: after Graph_InitTHGA");

    GraphPs3Trace("graph-update: before opening initial display lists");
    OPEN_DISPS(gfxCtx);

    gDPNoOpString(WORK_DISP++, "WORK_DISP 開始", 0);
    gDPNoOpString(POLY_OPA_DISP++, "POLY_OPA_DISP 開始", 0);
    gDPNoOpString(POLY_XLU_DISP++, "POLY_XLU_DISP 開始", 0);
    gDPNoOpString(OVERLAY_DISP++, "OVERLAY_DISP 開始", 0);

    CLOSE_DISPS(gfxCtx);
    GraphPs3Trace("graph-update: after opening initial display lists");

    GraphPs3Trace("graph-update: before GameState_ReqPadData");
    GameState_ReqPadData(gameState);
    GraphPs3Trace("graph-update: after GameState_ReqPadData");
    GraphPs3Trace("graph-update: before GameState_Update");
#ifdef __PS3__
    {
        extern unsigned long long oot_ps3_microseconds_now(void);
        extern unsigned long long gPs3GraphStepUs[2];
        const unsigned long long ps3T0 = oot_ps3_microseconds_now();
        GameState_Update(gameState);
        gPs3GraphStepUs[0] += oot_ps3_microseconds_now() - ps3T0;
    }
#else
    GameState_Update(gameState);
#endif
    GraphPs3Trace("graph-update: after GameState_Update");

    GraphPs3Trace("graph-update: before final close OPEN_DISPS");
    OPEN_DISPS(gfxCtx);
    GraphPs3Trace("graph-update: after final close OPEN_DISPS");

    GraphPs3Trace("graph-update: before final WORK no-op");
    gDPNoOpString(WORK_DISP++, "WORK_DISP 終了", 0);
    GraphPs3Trace("graph-update: after final WORK no-op");
    GraphPs3Trace("graph-update: before final POLY_OPA no-op");
    gDPNoOpString(POLY_OPA_DISP++, "POLY_OPA_DISP 終了", 0);
    GraphPs3Trace("graph-update: after final POLY_OPA no-op");
    GraphPs3Trace("graph-update: before final POLY_XLU no-op");
    gDPNoOpString(POLY_XLU_DISP++, "POLY_XLU_DISP 終了", 0);
    GraphPs3Trace("graph-update: after final POLY_XLU no-op");
    GraphPs3Trace("graph-update: before final OVERLAY no-op");
    gDPNoOpString(OVERLAY_DISP++, "OVERLAY_DISP 終了", 0);
    GraphPs3Trace("graph-update: after final OVERLAY no-op");

    GraphPs3Trace("graph-update: before final close CLOSE_DISPS");
    CLOSE_DISPS(gfxCtx);
    GraphPs3Trace("graph-update: after final close CLOSE_DISPS");

    GraphPs3Trace("graph-update: before branch OPEN_DISPS");
    OPEN_DISPS(gfxCtx);
    GraphPs3Trace("graph-update: after branch OPEN_DISPS");

    GraphPs3Trace("graph-update: before branch work to opa");
    gSPBranchList(WORK_DISP++, gfxCtx->polyOpaBuffer);
    GraphPs3Trace("graph-update: after branch work to opa");
    GraphPs3Trace("graph-update: before branch opa to xlu");
    gSPBranchList(POLY_OPA_DISP++, gfxCtx->polyXluBuffer);
    GraphPs3Trace("graph-update: after branch opa to xlu");
    GraphPs3Trace("graph-update: before branch xlu to overlay");
    gSPBranchList(POLY_XLU_DISP++, gfxCtx->overlayBuffer);
    GraphPs3Trace("graph-update: after branch xlu to overlay");
    GraphPs3Trace("graph-update: before overlay pipe sync");
    gDPPipeSync(OVERLAY_DISP++);
    GraphPs3Trace("graph-update: after overlay pipe sync");
    GraphPs3Trace("graph-update: before overlay full sync");
    gDPFullSync(OVERLAY_DISP++);
    GraphPs3Trace("graph-update: after overlay full sync");
    GraphPs3Trace("graph-update: before overlay end display list");
    gSPEndDisplayList(OVERLAY_DISP++);
    GraphPs3Trace("graph-update: after overlay end display list");

    GraphPs3Trace("graph-update: before branch CLOSE_DISPS");
    CLOSE_DISPS(gfxCtx);
    GraphPs3Trace("graph-update: after branch CLOSE_DISPS");

#ifdef __PS3__
    {
        static u32 ps3DisplayListScanCount = 0;
        GfxPool* pool = &gGfxPools[gfxCtx->gfxPoolIdx & 1];
        if (ps3DisplayListScanCount++ == 0U) {
            GraphPs3Trace("gfx-dl-probe: extended title opcode scan");
            GraphPs3ProbeDisplayList("work", pool->workBuffer,
                                    sizeof(pool->workBuffer) / sizeof(pool->workBuffer[0]));
            GraphPs3ProbeDisplayList("opaque", pool->polyOpaBuffer,
                                    sizeof(pool->polyOpaBuffer) / sizeof(pool->polyOpaBuffer[0]));
            GraphPs3ProbeDisplayList("translucent", pool->polyXluBuffer,
                                    sizeof(pool->polyXluBuffer) / sizeof(pool->polyXluBuffer[0]));
            GraphPs3ProbeDisplayList("overlay", pool->overlayBuffer,
                                    sizeof(pool->overlayBuffer) / sizeof(pool->overlayBuffer[0]));
        }
        oot_ps3_graph_set_opaque_list(pool->polyOpaBuffer,
                                     sizeof(pool->polyOpaBuffer) / sizeof(pool->polyOpaBuffer[0]));
    }
#endif

    GraphPs3Trace("graph-update: before HREG ucode debug checks");
    if (HREG(80) == 10 && HREG(93) == 2) {
        HREG(80) = 7;
        HREG(81) = -1;
        HREG(83) = HREG(92);
    }
    GraphPs3Trace("graph-update: after HREG ucode debug setup");

    if (HREG(80) == 7 && HREG(81) != 0) {
        GraphPs3Trace("graph-update: HREG ucode debug path entered");
        if (HREG(82) == 3) {
            Fault_AddClient(&sGraphUcodeFaultClient, Graph_UCodeFaultClient, gfxCtx->workBuffer, "do_count_fault");
        }

        Graph_DisassembleUCode(gfxCtx->workBuffer);

        if (HREG(82) == 3) {
            Fault_RemoveClient(&sGraphUcodeFaultClient);
        }

        if (HREG(81) < 0) {
            LogUtils_LogHexDump((void*)&HW_REG(SP_MEM_ADDR_REG, u32), 0x20);
            LogUtils_LogHexDump((void*)&DPC_START_REG, 0x20);
        }

        if (HREG(81) < 0) {
            HREG(81) = 0;
        }
        GraphPs3Trace("graph-update: HREG ucode debug path completed");
    }
    GraphPs3Trace("graph-update: after HREG ucode debug checks");

    problem = false;

    {
        GfxPool* pool = &gGfxPools[gfxCtx->gfxPoolIdx & 1];

        GraphPs3Trace("graph-update: before gfx pool head magic check");
        if (pool->headMagic != GFXPOOL_HEAD_MAGIC) {
            //! @bug (?) : "problem = true;" may be missing
            osSyncPrintf("%c", BEL);
            // "Dynamic area head is destroyed"
            osSyncPrintf(VT_COL(RED, WHITE) "ダイナミック領域先頭が破壊されています\n" VT_RST);
            Fault_AddHungupAndCrash(__FILE__, __LINE__);
        }
        GraphPs3Trace("graph-update: after gfx pool head magic check");
        GraphPs3Trace("graph-update: before gfx pool tail magic check");
        if (pool->tailMagic != GFXPOOL_TAIL_MAGIC) {
            problem = true;
            osSyncPrintf("%c", BEL);
            // "Dynamic region tail is destroyed"
            osSyncPrintf(VT_COL(RED, WHITE) "ダイナミック領域末尾が破壊されています\n" VT_RST);
            Fault_AddHungupAndCrash(__FILE__, __LINE__);
        }
        GraphPs3Trace("graph-update: after gfx pool tail magic check");
    }

    GraphPs3Trace("graph-update: before THGA polyOpa check");
    if (THGA_IsCrash(&gfxCtx->polyOpa)) {
        problem = true;
        osSyncPrintf("%c", BEL);
        // "Zelda 0 is dead"
        osSyncPrintf(VT_COL(RED, WHITE) "ゼルダ0は死んでしまった(graph_alloc is empty)\n" VT_RST);
    }
    GraphPs3Trace("graph-update: after THGA polyOpa check");
    GraphPs3Trace("graph-update: before THGA polyXlu check");
    if (THGA_IsCrash(&gfxCtx->polyXlu)) {
        problem = true;
        osSyncPrintf("%c", BEL);
        // "Zelda 1 is dead"
        osSyncPrintf(VT_COL(RED, WHITE) "ゼルダ1は死んでしまった(graph_alloc is empty)\n" VT_RST);
    }
    GraphPs3Trace("graph-update: after THGA polyXlu check");
    GraphPs3Trace("graph-update: before THGA overlay check");
    if (THGA_IsCrash(&gfxCtx->overlay)) {
        problem = true;
        osSyncPrintf("%c", BEL);
        // "Zelda 4 is dead"
        osSyncPrintf(VT_COL(RED, WHITE) "ゼルダ4は死んでしまった(graph_alloc is empty)\n" VT_RST);
    }
    GraphPs3Trace("graph-update: after THGA overlay check");

    if (!problem) {
#ifdef __PS3__
        GraphPs3Trace("graph-update: diagnostic skip Graph_TaskSet00");
        gfxCtx->gfxPoolIdx++;
        gfxCtx->fbIdx++;
#else
        GraphPs3Trace("graph-update: before Graph_TaskSet00");
        Graph_TaskSet00(gfxCtx);
        GraphPs3Trace("graph-update: after Graph_TaskSet00");
        gfxCtx->gfxPoolIdx++;
        gfxCtx->fbIdx++;
#endif
    }

#ifdef __PS3__
    // The game side of audio (sound effects, music commands) once per frame, as on PC;
    // the audio thread (ps3_audio.c) synthesizes from the queued commands.
    if (oot_ps3_audio_is_ready()) {
        oot_ps3_audio_lock(); // never at the same time as the synthesizer (ps3_audio.c)
        Audio_Update();
        oot_ps3_audio_unlock();
        oot_ps3_audio_kick();
    }
#else
    GraphPs3Trace("graph-update: before Audio_Update");
    Audio_Update();
    GraphPs3Trace("graph-update: after Audio_Update");
#endif

    {
        GraphPs3Trace("graph-update: before timing counters");
        OSTime time = osGetTime();
        s32 pad[4];

        D_8016A538 = gRSPGFXTotalTime;
        D_8016A530 = gRSPAudioTotalTime;
        D_8016A540 = gRDPTotalTime;
        gRSPGFXTotalTime = 0;
        gRSPAudioTotalTime = 0;
        gRDPTotalTime = 0;

        if (sGraphUpdateTime != 0) {
            D_8016A548 = time - sGraphUpdateTime;
        }
        sGraphUpdateTime = time;
        GraphPs3Trace("graph-update: after timing counters");
    }

    GraphPs3Trace("graph-update: before map-select CVar");
    s32 mask = CVarGetInteger("gDeveloperTools.MapSelectBtn", BTN_Z | BTN_L | BTN_R);
    GraphPs3Trace("graph-update: after map-select CVar");

    GraphPs3Trace("graph-update: before developer debug CVar");
    if (CVarGetInteger(CVAR_DEVELOPER_TOOLS("DebugEnabled"), 0)) {
        if (CHECK_BTN_ANY(gameState->input[0].press.button, mask) &&
            CHECK_BTN_ALL(gameState->input[0].cur.button, mask)) {
            gSaveContext.gameMode = GAMEMODE_NORMAL;
            SET_NEXT_GAMESTATE(gameState, Select_Init, SelectContext);
            gameState->running = false;
        }
    }
    GraphPs3Trace("graph-update: after developer debug CVar");

    GraphPs3Trace("graph-update: before PreNMI check");
    if (gIsCtrlr2Valid && PreNmiBuff_IsResetting(gAppNmiBufferPtr) && !gameState->unk_A0) {
        // "To reset mode"
        osSyncPrintf(VT_COL(YELLOW, BLACK) "PRE-NMIによりリセットモードに移行します\n" VT_RST);
        SET_NEXT_GAMESTATE(gameState, PreNMI_Init, PreNMIContext);
        gameState->running = false;
    }
    GraphPs3Trace("graph-update: completed");
}

uint64_t GetFrequency();
uint64_t GetPerfCounter();

extern AudioMgr gAudioMgr;

extern void ProcessSaveStateRequests(void);

static void RunFrame() {
    u32 size;
    char faultMsg[0x50];
    s32 ps3DiagnosticOverlayCount = 0;
#ifdef __PS3__
    static u32 ps3CompletedTitleFrames = 0;
    static u32 ps3OpeningTransitionPending = 0;
#endif

    GraphPs3Trace("graph: RunFrame entered");
    switch (runFrameContext.state) {
        case 0:
            GraphPs3Trace("graph: RunFrame state 0");
            break;
        case 1:
            GraphPs3Trace("graph: RunFrame resume next frame");
            goto nextFrame;
    }

    runFrameContext.nextOvl = &gGameStateOverlayTable[0];
    GraphPs3Trace("graph: selected initial overlay");

    osSyncPrintf("グラフィックスレッド実行開始\n"); // "Start graphic thread execution"
    Graph_Init(&runFrameContext.gfxCtx);
    GraphPs3Trace("graph: after Graph_Init");
#ifdef __PS3__
    // The audio manager thread (Audio_InitSound) doesn't run on PS3 yet. Its
    // sound-bank lists must still be built: zeroed lists loop forever in
    // Audio_StopSfxByBank/ByPos (game state changes, actors being removed).
    Audio_ResetSounds();
    GraphPs3Trace("graph: sound bank lists initialized (audio thread deferred)");
#endif

    while (runFrameContext.nextOvl) {
        ps3DiagnosticOverlayCount++;
        GraphPs3Trace("graph: overlay loop entered");
        if (ps3DiagnosticOverlayCount == 1) {
            GraphPs3Trace("graph: overlay diagnostic iteration 1");
        } else if (ps3DiagnosticOverlayCount == 2) {
            GraphPs3Trace("graph: overlay diagnostic iteration 2");
        } else {
            GraphPs3Trace("graph: overlay diagnostic iteration 3+");
        }
        runFrameContext.ovl = runFrameContext.nextOvl;
        GraphPs3Trace("graph: before Overlay_LoadGameState");
        Overlay_LoadGameState(runFrameContext.ovl);
        GraphPs3Trace("graph: after Overlay_LoadGameState");

        size = runFrameContext.ovl->instanceSize;
        GraphPs3Trace("graph: got overlay instance size");
        osSyncPrintf("クラスサイズ＝%dバイト\n", size); // "Class size = %d bytes"

        GraphPs3Trace("graph: before game state malloc");
        gGameState = SYSTEM_ARENA_MALLOC_DEBUG(size);
        GraphPs3Trace("graph: after game state malloc");

        if (!gGameState) {
            osSyncPrintf("確保失敗\n"); // "Failure to secure"

            snprintf(faultMsg, sizeof(faultMsg), "CLASS SIZE= %d bytes", size);
            Fault_AddHungupAndCrashImpl("GAME CLASS MALLOC FAILED", faultMsg);
        }
        GraphPs3Trace("graph: before GameState_Init");
        GameState_Init(gGameState, runFrameContext.ovl->init, &runFrameContext.gfxCtx);
        GraphPs3Trace("graph: after GameState_Init");
#ifdef __PS3__
        if (runFrameContext.ovl->init == Opening_Init) {
            GraphPs3Trace("graph: diagnostic Opening_Init completed; probing next state");
        }
        if (runFrameContext.ovl->init == Play_Init) {
            PlayState* play = (PlayState*)gGameState;
            unsigned int completedFrames = 0;
            unsigned int framesAfterFade = 0;
            char playFrameTrace[256];
            extern unsigned long long oot_ps3_microseconds_now(void);
            unsigned long long frameStartUs = 0;
            GraphPs3Trace("graph: diagnostic Play_Init completed; before first real Graph_Update");
            ps3OpeningTransitionPending = 0;
            oot_ps3_video_begin_play_probe();
            {
                extern int gOotPs3ResourceTimingEnabled;
                gOotPs3ResourceTimingEnabled = 1;
            }
            GraphPs3Trace("graph: Play sequence v1; native fade; maximum 180 frames");
            while (GameState_IsRunning(gGameState)) {
                frameStartUs = oot_ps3_microseconds_now();
                snprintf(playFrameTrace, sizeof(playFrameTrace),
                         "graph: Play frame %u begin; gameplay=%u transition=%d trigger=%d rate=%d",
                         completedFrames + 1U, (unsigned int)play->gameplayFrames,
                         (int)play->transitionMode, (int)play->transitionTrigger, (int)R_UPDATE_RATE);
                GraphPs3Trace(playFrameTrace);
                Graph_StartFrame();
                PadMgr_PS3Poll(&gPadMgr);
                GraphPs3Trace("graph: diagnostic before real Play frame Graph_Update");
                {
                    unsigned long long updateStartUs = oot_ps3_microseconds_now();
                    Graph_Update(&runFrameContext.gfxCtx, gGameState);
                    {
                        unsigned long long updateElapsedUs = oot_ps3_microseconds_now() - updateStartUs;
                        sPs3UpdAcc += updateElapsedUs;
                        sPs3LastLogicUs = updateElapsedUs;
                        gPs3GraphStepUs[1] += updateElapsedUs;
                        {
                            static unsigned int sLogicProfUpdates = 0;
                            extern unsigned long long gPs3GameStepUs[5];
                            if (++sLogicProfUpdates % 64U == 0U) {
                                snprintf(playFrameTrace, sizeof(playFrameTrace),
                                         "graph: Play logicprof/64 graphUpdate=%llu gameStateUpdate=%llu "
                                         "setFb=%llu main=%llu c4344=%llu gameStateDraw=%llu frameHook=%llu us",
                                         gPs3GraphStepUs[1], gPs3GraphStepUs[0], gPs3GameStepUs[0],
                                         gPs3GameStepUs[1], gPs3GameStepUs[2], gPs3GameStepUs[3], gPs3GameStepUs[4]);
                                oot_ps3_early_trace(playFrameTrace);
                                memset(gPs3GraphStepUs, 0, sizeof(gPs3GraphStepUs));
                                memset(gPs3GameStepUs, 0, sizeof(gPs3GameStepUs));
                            }
                        }
                        snprintf(playFrameTrace, sizeof(playFrameTrace),
                                 "graph: Graph_Update took %llu us", updateElapsedUs);
                        GraphPs3Trace(playFrameTrace);
                    }
                }
                GraphPs3Trace("graph: diagnostic real Play frame Graph_Update completed; before RSX draw");
                {
                    unsigned long long gfxStartUs = oot_ps3_microseconds_now();
                    Graph_ProcessGfxCommands(runFrameContext.gfxCtx.workBuffer);
                    {
                        unsigned long long gfxElapsedUs = oot_ps3_microseconds_now() - gfxStartUs;
                        sPs3GfxAcc += gfxElapsedUs;
                        sPs3LastGfxUs = gfxElapsedUs;
                        snprintf(playFrameTrace, sizeof(playFrameTrace),
                                 "graph: Graph_ProcessGfxCommands took %llu us", gfxElapsedUs);
                        GraphPs3Trace(playFrameTrace);
                    }
                }
                {
                    extern void oot_ps3_resource_frame_stats(unsigned long long* outUs, unsigned int* outHits, unsigned int* outMisses);
                    unsigned long long resourceUs = 0;
                    unsigned int resourceHits = 0;
                    unsigned int resourceMisses = 0;
                    oot_ps3_resource_frame_stats(&resourceUs, &resourceHits, &resourceMisses);
                    sPs3ResAcc += resourceUs;
                    snprintf(playFrameTrace, sizeof(playFrameTrace),
                             "graph: resource loads took %llu us (hits=%u misses=%u)",
                             resourceUs, resourceHits, resourceMisses);
                    GraphPs3Trace(playFrameTrace);
                    {
                        // Per-update hitch: two frames skipped, a draw over 25 ms or logic over
                        // 20 ms (the steady overrun is in the fps/drawprof lines). One line each.
                        extern void oot_ps3_last_update_draws(unsigned long long drawUs[3], unsigned int* drawn,
                                                              unsigned int* skipped);
                        static unsigned int sSpikeLines = 0;
                        unsigned long long drawUs[3];
                        unsigned int drawn = 0, skipped = 0;
                        oot_ps3_last_update_draws(drawUs, &drawn, &skipped);
                        {
                            extern void oot_ps3_debug_note_update(unsigned long long logicUs,
                                                                  const unsigned long long drawUs[3],
                                                                  unsigned int drawn, unsigned int skipped);
                            oot_ps3_debug_note_update(sPs3LastLogicUs, drawUs, drawn, skipped);
                        }
                        const unsigned long long maxDrawUs =
                            drawUs[0] > drawUs[1] ? (drawUs[0] > drawUs[2] ? drawUs[0] : drawUs[2])
                                                  : (drawUs[1] > drawUs[2] ? drawUs[1] : drawUs[2]);
                        if ((skipped >= 2U || maxDrawUs > 25000ULL || sPs3LastLogicUs > 20000ULL) &&
                            sSpikeLines < 1000U && completedFrames > 1U) {
                            ++sSpikeLines;
                            snprintf(playFrameTrace, sizeof(playFrameTrace),
                                     "graph: Play spike scene=%d cs=%d logic=%llu draws=%u skipped=%u "
                                     "draw=%llu/%llu/%llu res=%llu new=%u actors=%d us",
                                     (int)play->sceneNum, (int)play->csCtx.frames, sPs3LastLogicUs, drawn, skipped,
                                     drawUs[0], drawUs[1], drawUs[2], resourceUs, resourceMisses,
                                     (int)play->actorCtx.total);
                            oot_ps3_early_trace(playFrameTrace);
                        }
                    }
                }
                ++completedFrames;
                {
                    unsigned long long frameElapsedUs = oot_ps3_microseconds_now() - frameStartUs;
                    snprintf(playFrameTrace, sizeof(playFrameTrace),
                             "graph: Play frame %u submitted; gameplay=%u transition=%d trigger=%d bgAlpha=%d frameUs=%llu",
                             completedFrames, (unsigned int)play->gameplayFrames,
                             (int)play->transitionMode, (int)play->transitionTrigger, (int)play->bgCoverAlpha,
                             frameElapsedUs);
                    GraphPs3Trace(playFrameTrace);
                }
                if (completedFrames == 1U || completedFrames % 16U == 0U) {
                    {
                        Camera* ps3Cam = play->cameraPtrs[play->activeCamera];
                        snprintf(playFrameTrace, sizeof(playFrameTrace),
                                 "graph: Play view eye=(%.1f,%.1f,%.1f) activeCam=%d camStatus=%d camSetting=%d "
                                 "camMode=%d camEye=(%.1f,%.1f,%.1f) csState=%d csFrames=%d",
                                 (double)play->view.eye.x, (double)play->view.eye.y, (double)play->view.eye.z,
                                 (int)play->activeCamera,
                                 ps3Cam ? (int)ps3Cam->status : -1, ps3Cam ? (int)ps3Cam->setting : -1,
                                 ps3Cam ? (int)ps3Cam->mode : -1,
                                 ps3Cam ? (double)ps3Cam->eye.x : 0.0, ps3Cam ? (double)ps3Cam->eye.y : 0.0,
                                 ps3Cam ? (double)ps3Cam->eye.z : 0.0,
                                 (int)play->csCtx.state, (int)play->csCtx.frames);
                        GraphPs3Trace(playFrameTrace);
                        {
                            Player* ps3Link = GET_PLAYER(play);
                            snprintf(playFrameTrace, sizeof(playFrameTrace),
                                     "graph: Play link pos=(%.1f,%.1f,%.1f)",
                                     ps3Link ? (double)ps3Link->actor.world.pos.x : 0.0,
                                     ps3Link ? (double)ps3Link->actor.world.pos.y : 0.0,
                                     ps3Link ? (double)ps3Link->actor.world.pos.z : 0.0);
                        }
                        GraphPs3Trace(playFrameTrace);
                        snprintf(playFrameTrace, sizeof(playFrameTrace),
                                 "graph: Play cam at=(%.1f,%.1f,%.1f) fov=%.1f",
                                 ps3Cam ? (double)ps3Cam->at.x : 0.0, ps3Cam ? (double)ps3Cam->at.y : 0.0,
                                 ps3Cam ? (double)ps3Cam->at.z : 0.0, ps3Cam ? (double)ps3Cam->fov : 0.0);
                        GraphPs3Trace(playFrameTrace);
                        {
                            Demo1* ps3Demo1 = ps3Cam ? (Demo1*)ps3Cam->paramData : NULL;
                            snprintf(playFrameTrace, sizeof(playFrameTrace),
                                     "graph: Play cam animState=%d keyframe=%d curFrame=%.3f oob=%d",
                                     ps3Cam ? (int)ps3Cam->animState : -1,
                                     ps3Demo1 ? (int)ps3Demo1->anim.keyframe : -1,
                                     ps3Demo1 ? (double)ps3Demo1->anim.curFrame : -1.0,
                                     (int)Camera_Ps3OOBTimer());
                        }
                        GraphPs3Trace(playFrameTrace);
                        {
                            extern unsigned long long oot_ps3_rsx_take_gpu_wait_us(void);
                            extern void oot_ps3_rsx_take_timing(unsigned long long* fill,
                                                                unsigned long long* submit);
                            unsigned long long ps3Fill = 0, ps3Submit = 0;
                            oot_ps3_rsx_take_timing(&ps3Fill, &ps3Submit);
                            static unsigned long long ps3LastWall = 0;
                            const unsigned long long ps3NowWall = oot_ps3_microseconds_now();
                            const unsigned long long ps3Wall = ps3LastWall ? ps3NowWall - ps3LastWall : 0;
                            ps3LastWall = ps3NowWall;
                            unsigned long long ps3RunUs = 0;
                            unsigned int ps3Runs = 0;
                            oot_ps3_fast3d_take_run_stats(&ps3RunUs, &ps3Runs);
                            snprintf(playFrameTrace, sizeof(playFrameTrace),
                                     "graph: Play last 16 frames: wall=%llu update=%llu gfx=%llu run=%llu runs=%u res=%llu gpuwait=%llu fill=%llu submit=%llu flip=%llu us",
                                     ps3Wall, sPs3UpdAcc, sPs3GfxAcc, ps3RunUs, ps3Runs, sPs3ResAcc,
                                     oot_ps3_rsx_take_gpu_wait_us(), ps3Fill, ps3Submit,
                                     oot_ps3_screen_take_flip_wait_us());
                            sPs3UpdAcc = sPs3GfxAcc = sPs3ResAcc = 0;
                        }
                    }
                    GraphPs3Trace(playFrameTrace); /* the log writer thread flushes/fsyncs */
                }
                if (completedFrames == 1U) oot_ps3_trace_compact_play();
                if (play->transitionMode == TRANS_MODE_OFF && play->transitionTrigger == TRANS_TRIGGER_OFF) {
                    ++framesAfterFade;
                } else {
                    framesAfterFade = 0;
                }
                if (0 && completedFrames >= 64U && framesAfterFade >= 1000U) { /* PS3: intro loops; keep running */
                    GraphPs3Trace("graph: Play sequence native transition finished; 24 subsequent frames submitted");
                    break;
                }
            }
            snprintf(playFrameTrace, sizeof(playFrameTrace),
                     "graph: Play sequence stopped; frames=%u running=%d transition=%d",
                     completedFrames, (int)GameState_IsRunning(gGameState), (int)play->transitionMode);
            GraphPs3Trace(playFrameTrace);
            if (!GameState_IsRunning(gGameState)) {
                // Play chose the next game state (e.g. PRESS START -> file select).
                GraphPs3Trace("graph: Play finished; handing over to next game state");
                oot_ps3_trace_full_detail(); /* see the next state's init in the trace */
                oot_ps3_early_trace_flush_usb();
                goto ps3NextGameState;
            }
            GraphPs3Trace("graph: holding submitted Play framebuffer for three seconds");
            oot_ps3_early_trace_flush_usb();
            usleep(3000000);
            GraphPs3Trace("graph: diagnostic bounded Play sequence completed; exiting to XMB");
            oot_ps3_early_trace_flush_usb();
            exit(0);
        }
        if (ps3OpeningTransitionPending && runFrameContext.ovl->init != Opening_Init) {
            GraphPs3Trace("graph: diagnostic next state after Opening initialized; exiting to XMB");
            exit(0);
        }
#endif

        uint64_t freq = GetFrequency();
        (void)freq;
        GraphPs3Trace("graph: before game state loop");

        GraphPs3Trace("graph: before GameState_IsRunning probe");
        s32 ps3DiagnosticRunningProbe = GameState_IsRunning(gGameState);
        if (ps3DiagnosticRunningProbe) {
            GraphPs3Trace("graph: GameState_IsRunning returned true");
        } else {
            GraphPs3Trace("graph: GameState_IsRunning returned false");
        }

        while (ps3DiagnosticRunningProbe) {
            // uint64_t ticksA, ticksB;
            // ticksA = GetPerfCounter();

            GraphPs3Trace("graph: before Graph_StartFrame");
            Graph_StartFrame();
            GraphPs3Trace("graph: after Graph_StartFrame");

#ifdef __PS3__
            PadMgr_PS3Poll(&gPadMgr);
#else
            PadMgr_ThreadEntry(&gPadMgr);
#endif

            GraphPs3Trace("graph: before Graph_Update");
            Graph_Update(&runFrameContext.gfxCtx, gGameState);
            GraphPs3Trace("graph: after Graph_Update");
            // ticksB = GetPerfCounter();

            if (GfxDebuggerIsDebuggingRequested()) {
                GfxDebuggerDebugDisplayList(runFrameContext.gfxCtx.workBuffer);
            }

            GraphPs3Trace("graph: before Graph_ProcessGfxCommands");
            Graph_ProcessGfxCommands(runFrameContext.gfxCtx.workBuffer);
            GraphPs3Trace("graph: after Graph_ProcessGfxCommands");
#ifdef __PS3__
            if (runFrameContext.ovl->init == Opening_Init) {
                GraphPs3Trace("graph: diagnostic Opening frame completed; resuming handoff");
            } else if (runFrameContext.ovl->init == FileChoose_Init) {
                static unsigned int ps3FileChooseFrames = 0;
                if (ps3FileChooseFrames == 2U) oot_ps3_trace_compact_play();
                if (++ps3FileChooseFrames == 1U || ps3FileChooseFrames % 64U == 0U) {
                    char ps3Line[96];
                    snprintf(ps3Line, sizeof(ps3Line), "graph: FileChoose frame %u", ps3FileChooseFrames);
                    GraphPs3Trace(ps3Line);
                }
            } else {
                ps3CompletedTitleFrames++;
                if (ps3CompletedTitleFrames == 1) {
                    GraphPs3Trace("graph: title frame 1 complete; testing frame resume");
                } else if (ps3CompletedTitleFrames == 2) {
                    GraphPs3Trace("graph: title frame 2 complete; testing frame resume");
                } else if (ps3CompletedTitleFrames == 6) {
                    GraphPs3Trace("graph: title frame 6 complete; native fade continuing");
                }
            }
#endif

            // uint64_t diff = (ticksB - ticksA) / (freq / 1000);
            // printf("Frame simulated in %ims\n", diff);
            runFrameContext.state = 1;
            GraphPs3Trace("graph: frame reached return point");
            ProcessSaveStateRequests();
            return;
        nextFrame:;
            ps3DiagnosticRunningProbe = GameState_IsRunning(gGameState);
#ifdef __PS3__
            if (runFrameContext.ovl->init == Opening_Init && ps3DiagnosticRunningProbe) {
                GraphPs3Trace("graph: Opening still running after one frame; exiting to XMB");
                exit(0);
            }
            if (runFrameContext.ovl->init == FileChoose_Init && !ps3DiagnosticRunningProbe) {
                GraphPs3Trace("graph: FileChoose finished; full trace for the next game state");
                oot_ps3_trace_full_detail();
                oot_ps3_early_trace_flush_usb();
            }
#endif
        }

#ifdef __PS3__
    ps3NextGameState:
#endif
        GraphPs3Trace("graph: before Graph_GetNextGameState");
#ifdef __PS3__
        // The render thread may still be drawing this state's last display list, which
        // points into the state's memory: let it finish before anything is released.
        oot_ps3_render_sync();
#endif
        runFrameContext.nextOvl = Graph_GetNextGameState(gGameState);
        if (runFrameContext.nextOvl) {
            GraphPs3Trace("graph: next game state selected");
        } else {
            GraphPs3Trace("graph: no next game state selected");
        }
#ifdef __PS3__
        if (runFrameContext.nextOvl && runFrameContext.ovl->init == TitleSetup_Init) {
            GraphPs3Trace("graph: diagnostic release TitleSetup state arena");
            GraphPs3ReleaseStateArena(gGameState);
            gGameState = NULL;
            continue;
        }
        if (runFrameContext.nextOvl && runFrameContext.ovl->init == Title_Init) {
            GraphPs3Trace("graph: diagnostic release Title arena; enter Opening_Init");
            Sram_InitSram(gGameState); // all Title_Destroy does: load the save slots
            GraphPs3ReleaseStateArena(gGameState);
            gGameState = NULL;
            continue;
        }
        if (runFrameContext.nextOvl && runFrameContext.ovl->init == Play_Init) {
            GraphPs3Trace("graph: release Play arena (Play_Destroy skipped on PS3); enter next state");
            GraphPs3ReleaseStateArena(gGameState);
            gGameState = NULL;
            continue;
        }
        if (runFrameContext.ovl->init == Opening_Init) {
            if (!runFrameContext.nextOvl) {
                GraphPs3Trace("graph: Opening selected no next game state; exiting to XMB");
                exit(0);
            }
            GraphPs3Trace("graph: diagnostic release Opening arena; probe next game state");
            ps3OpeningTransitionPending = 1;
            GraphPs3ReleaseStateArena(gGameState);
            gGameState = NULL;
            continue;
        }
#endif
        GraphPs3Trace("graph: before GameState_Destroy");
        GameState_Destroy(gGameState);
        GraphPs3Trace("graph: after GameState_Destroy");
        GraphPs3Trace("graph: before game state free");
        SYSTEM_ARENA_FREE_DEBUG(gGameState);
        GraphPs3Trace("graph: after game state free");
        GraphPs3Trace("graph: before Overlay_FreeGameState");
        Overlay_FreeGameState(runFrameContext.ovl);
        GraphPs3Trace("graph: after Overlay_FreeGameState");

#ifdef __PS3__
        if (0 && ps3DiagnosticOverlayCount >= 3) { /* PS3: file select -> Play is a normal handover now */
            GraphPs3Trace("graph: diagnostic auto-exit after 3 non-running overlays");
            exit(0);
        }
#endif
    }
    Graph_Destroy(&runFrameContext.gfxCtx);
    osSyncPrintf("グラフィックスレッド実行終了\n"); // "End of graphic thread execution"

    // Graph_Update(gfxCtxTest, gameStateTest);
    exit(0);
}

void Graph_ThreadEntry(void* arg0) {
    while (WindowIsRunning()) {
        RunFrame();
    }
}

void* Graph_Alloc(GraphicsContext* gfxCtx, size_t size) {
    TwoHeadGfxArena* thga = &gfxCtx->polyOpa;

    if (HREG(59) == 1) {
        osSyncPrintf("graph_alloc siz=%d thga size=%08x bufp=%08x head=%08x tail=%08x\n", size, thga->size, thga->bufp,
                     thga->p, thga->d);
    }
    return THGA_AllocEnd(&gfxCtx->polyOpa, ALIGN16(size));
}

void* Graph_Alloc2(GraphicsContext* gfxCtx, size_t size) {
    TwoHeadGfxArena* thga = &gfxCtx->polyOpa;

    if (HREG(59) == 1) {
        osSyncPrintf("graph_alloc siz=%d thga size=%08x bufp=%08x head=%08x tail=%08x\n", size, thga->size, thga->bufp,
                     thga->p, thga->d);
    }
    return THGA_AllocEnd(&gfxCtx->polyOpa, ALIGN16(size));
}

void Graph_OpenDisps(Gfx** dispRefs, GraphicsContext* gfxCtx, const char* file, s32 line) {
    // SOH [Debugging] Force open/close disp string handling on so that the graphics debugger can leverage it
    if (true || HREG(80) == 7 && HREG(82) != 4) {
        dispRefs[0] = gfxCtx->polyOpa.p;
        dispRefs[1] = gfxCtx->polyXlu.p;
        dispRefs[2] = gfxCtx->overlay.p;

        gDPNoOpOpenDisp(gfxCtx->polyOpa.p++, file, line);
        gDPNoOpOpenDisp(gfxCtx->polyXlu.p++, file, line);
        gDPNoOpOpenDisp(gfxCtx->overlay.p++, file, line);
    }
}

void Graph_CloseDisps(Gfx** dispRefs, GraphicsContext* gfxCtx, const char* file, s32 line) {
    // SOH [Debugging] Force open/close disp string handling on so that the graphics debugger can leverage it
    if (true || HREG(80) == 7 && HREG(82) != 4) {
        if (dispRefs[0] + 1 == gfxCtx->polyOpa.p) {
            gfxCtx->polyOpa.p = dispRefs[0];
        } else {
            gDPNoOpCloseDisp(gfxCtx->polyOpa.p++, file, line);
        }

        if (dispRefs[1] + 1 == gfxCtx->polyXlu.p) {
            gfxCtx->polyXlu.p = dispRefs[1];
        } else {
            gDPNoOpCloseDisp(gfxCtx->polyXlu.p++, file, line);
        }

        if (dispRefs[2] + 1 == gfxCtx->overlay.p) {
            gfxCtx->overlay.p = dispRefs[2];
        } else {
            gDPNoOpCloseDisp(gfxCtx->overlay.p++, file, line);
        }
    }
}

Gfx* Graph_GfxPlusOne(Gfx* gfx) {
    return gfx + 1;
}

Gfx* Graph_BranchDlist(Gfx* gfx, Gfx* dst) {
    gSPBranchList(gfx, dst);
    return dst;
}

void* Graph_DlistAlloc(Gfx** gfx, size_t size) {
    u8* ptr;
    Gfx* dst;

    size = ((size + 7) & ~7),

    ptr = (u8*)(*gfx + 1);

    dst = (Gfx*)(ptr + size);
    gSPBranchList(*gfx, dst);

    *gfx = dst;
    return ptr;
}
