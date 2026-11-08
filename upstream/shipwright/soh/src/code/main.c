#ifdef _WIN32
#include <Windows.h>
#include <locale.h>
#endif

#include "global.h"
#include "vt.h"
#include "stdio.h"
#include <libultraship/bridge/crashhandlerbridge.h>
#include <soh/Enhancements/bootcommands.h>
#include "soh/OTRGlobals.h"
#include "soh/CrashHandlerExt.h"
#ifdef __PS3__
#include <unistd.h>
#include "ps3_platform/shipwright_video_ps3.h"

void oot_ps3_early_trace(const char* message);
#endif

s32 gScreenWidth = SCREEN_WIDTH;
s32 gScreenHeight = SCREEN_HEIGHT;
size_t gSystemHeapSize = 0;

PreNmiBuff* gAppNmiBufferPtr;
SchedContext gSchedContext;
PadMgr gPadMgr;
IrqMgr gIrqMgr;
uintptr_t gSegments[NUM_SEGMENTS];
OSThread sGraphThread;
u8 sGraphStack[0x1800];
u8 sSchedStack[0x600];
u8 sAudioStack[0x800];
u8 sPadMgrStack[0x500];
u8 sIrqMgrStack[0x500];
StackEntry sGraphStackInfo;
StackEntry sSchedStackInfo;
StackEntry sAudioStackInfo;
StackEntry sPadMgrStackInfo;
StackEntry sIrqMgrStackInfo;
AudioMgr gAudioMgr;
OSMesgQueue sSiIntMsgQ;
OSMesg sSiIntMsgBuf[1];

#ifdef __PS3__
static void PS3BootTrace(const char* message) {
    oot_ps3_early_trace(message);
}

static void PS3BootStage(u32 stage) {
    (void)stage;
    /* RSX startup is intentionally deferred until the engine reaches its
     * normal Graph_StartFrame path.  The old diagnostic animation blocked
     * the first real Shipwright call before it could produce useful traces. */
    PS3BootTrace("boot-stage: diagnostic video bypassed");
}
#endif

void Main_LogSystemHeap(void) {
    osSyncPrintf(VT_FGCOL(GREEN));
    // "System heap size% 08x (% dKB) Start address% 08x"
    osSyncPrintf("システムヒープサイズ %08x(%dKB) 開始アドレス %08x\n", gSystemHeapSize, gSystemHeapSize / 1024,
                 gSystemHeap);
    osSyncPrintf(VT_RST);
}

#ifdef _WIN32
int SDL_main(int argc, char* argv[]) {
    AllocConsole();
    (void)freopen("CONIN$", "r", stdin);
    (void)freopen("CONOUT$", "w", stdout);
    (void)freopen("CONOUT$", "w", stderr);
#ifndef _DEBUG
    ShowWindow(GetConsoleWindow(), SW_HIDE);
#endif
    // Allow non-ascii characters for Windows
    setlocale(LC_ALL, ".UTF8");

#elif defined(__PS3__)
int oot_shipwright_main(int argc, char* argv[]) {
#else //_WIN32
int main(int argc, char* argv[]) {
#endif
#ifdef __PS3__
    PS3BootTrace("main: entered");
    PS3BootStage(0);
    PS3BootTrace("main: before GameConsole_Init");
#endif
    GameConsole_Init();
#ifdef __PS3__
    PS3BootTrace("main: after GameConsole_Init");
    PS3BootStage(1);
    PS3BootTrace("main: before InitOTR");
#endif
    InitOTR(argc, argv);
#ifdef __PS3__
    PS3BootTrace("main: after InitOTR");
    PS3BootStage(2);
#endif
    // TODO: Was moved to below InitOTR because it requires window to be setup. But will be late to catch crashes.
    CrashHandlerRegisterCallback(CrashHandler_PrintSohData);
    BootCommands_Init();
#ifdef __PS3__
    PS3BootTrace("main: after CrashHandler/BootCommands");
    PS3BootStage(3);
#endif

#ifdef __PS3__
    PS3BootTrace("main: before Heaps_Alloc");
#endif
    Heaps_Alloc();
#ifdef __PS3__
    PS3BootTrace("main: after Heaps_Alloc");
    PS3BootStage(4);
    PS3BootTrace("main: before Main");
#endif
    Main(0);
#ifdef __PS3__
    PS3BootTrace("main: returned from Main");
    PS3BootStage(7);
#endif
    DeinitOTR();
#ifdef __PS3__
    PS3BootTrace("main: after DeinitOTR");
#endif
    Heaps_Free();
#ifdef __PS3__
    PS3BootTrace("main: exiting normally");
#endif
    return 0;
}

void Main(void* arg) {
    IrqMgrClient irqClient;
    OSMesgQueue irqMgrMsgQ;
    OSMesg irqMgrMsgBuf[60];
    uintptr_t sysHeap;
    uintptr_t fb;
    void* debugHeap;
    size_t debugHeapSize;
    s16* msg;

#ifdef __PS3__
    PS3BootTrace("game-main: entered Main");
#endif
    osSyncPrintf("mainproc 実行開始\n"); // "Start running"
    gScreenWidth = SCREEN_WIDTH;
    gScreenHeight = SCREEN_HEIGHT;
    gAppNmiBufferPtr = (PreNmiBuff*)osAppNmiBuffer;
    PreNmiBuff_Init(gAppNmiBufferPtr);
#ifdef __PS3__
    PS3BootTrace("game-main: after PreNmiBuff_Init");
#endif
    Fault_Init();
#ifdef __PS3__
    PS3BootTrace("game-main: after Fault_Init");
#endif
    SysCfb_Init(0);
#ifdef __PS3__
    PS3BootTrace("game-main: after SysCfb_Init");
#endif
    sysHeap = (uintptr_t)gSystemHeap;
    fb = SysCfb_GetFbPtr(0);
    gSystemHeapSize = 1024 * 1024 * 4;
    // "System heap initalization"
    osSyncPrintf("システムヒープ初期化 %08x-%08x %08x\n", sysHeap, fb, gSystemHeapSize);
    SystemHeap_Init((void*)sysHeap, gSystemHeapSize); // initializes the system heap
#ifdef __PS3__
    PS3BootTrace("game-main: after SystemHeap_Init");
#endif
    if (osMemSize >= 0x800000) {
        debugHeap = (void*)SysCfb_GetFbEnd();
        debugHeapSize = (0x80600000 - (uintptr_t)debugHeap);
    } else {
        debugHeapSize = 0x400;
        debugHeap = SYSTEM_ARENA_MALLOC_DEBUG(debugHeapSize);
    }

    debugHeapSize = 1024 * 64;

    osSyncPrintf("debug_InitArena(%08x, %08x)\n", debugHeap, debugHeapSize);
    DebugArena_Init(debugHeap, debugHeapSize);
    func_800636C0();
#ifdef __PS3__
    PS3BootTrace("game-main: after DebugArena/func_800636C0");
#endif

    R_ENABLE_ARENA_DBG = 0;

    osCreateMesgQueue(&sSiIntMsgQ, sSiIntMsgBuf, 1);
    osSetEventMesg(5, &sSiIntMsgQ, OS_MESG_PTR(NULL));
#ifdef __PS3__
    PS3BootTrace("game-main: after SI message queue");
#endif

    Main_LogSystemHeap();

    osCreateMesgQueue(&irqMgrMsgQ, irqMgrMsgBuf, 0x3C);
    StackCheck_Init(&sIrqMgrStackInfo, sIrqMgrStack, sIrqMgrStack + sizeof(sIrqMgrStack), 0, 0x100, "irqmgr");
#ifdef __PS3__
    PS3BootTrace("game-main: before IrqMgr_Init");
#endif
    IrqMgr_Init(&gIrqMgr, &sGraphStackInfo, Z_PRIORITY_IRQMGR, 1);
#ifdef __PS3__
    PS3BootTrace("game-main: after IrqMgr_Init");
#endif

    osSyncPrintf("タスクスケジューラの初期化\n"); // "Initialize the task scheduler"
    StackCheck_Init(&sSchedStackInfo, sSchedStack, sSchedStack + sizeof(sSchedStack), 0, 0x100, "sched");
#ifdef __PS3__
    PS3BootTrace("game-main: before Sched_Init");
#endif
    Sched_Init(&gSchedContext, &sAudioStack, Z_PRIORITY_SCHED, D_80013960, 1, &gIrqMgr);
#ifdef __PS3__
    PS3BootTrace("game-main: after Sched_Init");
#endif

    IrqMgr_AddClient(&gIrqMgr, &irqClient, &irqMgrMsgQ);

    StackCheck_Init(&sAudioStackInfo, sAudioStack, sAudioStack + sizeof(sAudioStack), 0, 0x100, "audio");
#ifdef __PS3__
    PS3BootTrace("game-main: before AudioMgr_Init");
#endif
    AudioMgr_Init(&gAudioMgr, sAudioStack + sizeof(sAudioStack), Z_PRIORITY_AUDIOMGR, 0xA, &gSchedContext, &gIrqMgr);
#ifdef __PS3__
    PS3BootTrace("game-main: after AudioMgr_Init");
#endif

    StackCheck_Init(&sPadMgrStackInfo, sPadMgrStack, sPadMgrStack + sizeof(sPadMgrStack), 0, 0x100, "padmgr");
#ifdef __PS3__
    PS3BootTrace("game-main: before PadMgr_Init");
#endif
    PadMgr_Init(&gPadMgr, &sSiIntMsgQ, &gIrqMgr, 7, Z_PRIORITY_PADMGR, &sIrqMgrStack);
#ifdef __PS3__
    PS3BootTrace("game-main: after PadMgr_Init");
#endif

    AudioMgr_Unlock(&gAudioMgr);
#ifdef __PS3__
    PS3BootTrace("game-main: after AudioMgr_Unlock");
#endif

    StackCheck_Init(&sGraphStackInfo, sGraphStack, sGraphStack + sizeof(sGraphStack), 0, 0x100, "graph");
#ifdef __PS3__
    PS3BootTrace("game-main: before graph thread start");
#endif
    osCreateThread(&sGraphThread, 4, Graph_ThreadEntry, arg, sGraphStack + sizeof(sGraphStack), Z_PRIORITY_GRAPH);
    osStartThread(&sGraphThread);
    osSetThreadPri(0, Z_PRIORITY_SCHED);
#ifdef __PS3__
    PS3BootTrace("game-main: graph thread start requested");
    PS3BootTrace("game-main: before direct Graph_ThreadEntry");
#endif

    Graph_ThreadEntry(0);
#ifdef __PS3__
    PS3BootTrace("game-main: direct Graph_ThreadEntry returned");
#endif

    while (true) {
        msg = NULL;
        osRecvMesg(&irqMgrMsgQ, (OSMesg*)&msg, OS_MESG_BLOCK);
        if (msg == NULL) {
            break;
        }
        if (*msg == OS_SC_PRE_NMI_MSG) {
            osSyncPrintf("main.c: リセットされたみたいだよ\n"); // "Looks like it's been reset"
            PreNmiBuff_SetReset(gAppNmiBufferPtr);
        }
    }

    osSyncPrintf("mainproc 後始末\n"); // "Cleanup"
    osDestroyThread(&sGraphThread);
    func_800FBFD8();
    osSyncPrintf("mainproc 実行終了\n"); // "End of execution"
}
