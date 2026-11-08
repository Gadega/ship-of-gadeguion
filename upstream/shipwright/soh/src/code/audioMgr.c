#include "global.h"
#include <string.h>
#include <stdio.h>

#ifdef __PS3__
void oot_ps3_early_trace(const char* message);
static void AudioMgr_PS3Trace(const char* message) {
    oot_ps3_early_trace(message);
}
void AudioMgr_CreateNextAudioBuffer(s16* samples, u32 num_samples);
int oot_ps3_audio_start(void); // ps3_audio.c: libaudio port + audio thread
static volatile s32 sPs3AudioReady = 0;

// Set once Audio_Init/Audio_InitSound finished and the output port runs; graph.c
// only calls Audio_Update after this (an uninitialized engine must not be driven).
s32 oot_ps3_audio_is_ready(void) {
    return sPs3AudioReady;
}

// Audio thread (ps3_audio.c): synthesize `frames` stereo frames at 32 kHz.
void oot_ps3_audio_produce(s16* out, u32 frames) {
    static u32 sCalls = 0;
    static s32 sPeak = 0;
    u32 i;
    AudioMgr_CreateNextAudioBuffer(out, frames);
    for (i = 0; i < frames * 2; i++) {
        s32 v = out[i] < 0 ? -out[i] : out[i];
        if (v > sPeak) sPeak = v;
    }
    // Engine state every 600 updates (10 s): why is it silent / what is playing.
    if (++sCalls % 600 == 0) {
        char line[256];
        s32 notes = 0;
        s32 n;
        for (n = 0; n < gAudioContext.numNotes; n++) {
            if (gAudioContext.notes[n].noteSubEu.bitField0.enabled) notes++;
        }
        extern u8 D_80133418;   // code_800F9280.c: Audio_Update waits for an audio reset while != 0
        extern u8 sSeqCmdWrPos; // code_800F9280.c: game-side sequence command queue
        extern u8 sSeqCmdRdPos;
        extern u8 gAudioSpecId;
        snprintf(line, sizeof(line),
                 "graph: Play audio-wait=%d seqcmd wr=%d rd=%d spec=%d load=%d procQ=%d resetQ=%d",
                 (int)D_80133418, (int)sSeqCmdWrPos, (int)sSeqCmdRdPos, (int)gAudioSpecId,
                 (int)gAudioContext.audioResetSpecIdToLoad, (int)gAudioContext.cmdProcQueue.validCount,
                 (int)gAudioContext.audioResetQueue.validCount);
        oot_ps3_early_trace(line);
        snprintf(line, sizeof(line),
                 "graph: Play audio-engine reset=%d notes=%d/%d seq0=%d:%04x seq1=%d:%04x seq2=%d:%04x seq3=%d:%04x "
                 "cmd wr=%d rd=%d fin=%d tasks=%d peak=%d",
                 (int)gAudioContext.resetStatus, (int)notes, (int)gAudioContext.numNotes,
                 (int)gAudioContext.seqPlayers[0].enabled, (unsigned)gAudioContext.seqPlayers[0].seqId,
                 (int)gAudioContext.seqPlayers[1].enabled, (unsigned)gAudioContext.seqPlayers[1].seqId,
                 (int)gAudioContext.seqPlayers[2].enabled, (unsigned)gAudioContext.seqPlayers[2].seqId,
                 (int)gAudioContext.seqPlayers[3].enabled, (unsigned)gAudioContext.seqPlayers[3].seqId,
                 (int)gAudioContext.cmdWrPos, (int)gAudioContext.cmdRdPos, (int)gAudioContext.cmdQueueFinished,
                 (int)gAudioContext.totalTaskCnt, (int)sPeak);
        oot_ps3_early_trace(line);
        sPeak = 0;
    }
}
#else
static void AudioMgr_PS3Trace(const char* message) {
    (void)message;
}
#endif

void func_800C3C80(AudioMgr* audioMgr) {
    AudioTask* task;

    task = audioMgr->rspTask;
    if (audioMgr->rspTask->taskQueue != NULL) {
        osSendMesgPtr(task->taskQueue, NULL, OS_MESG_BLOCK);
    }
}

void AudioMgr_HandleRetrace(AudioMgr* audioMgr) {
    AudioTask* rspTask;

    if (SREG(20) > 0) {
        audioMgr->rspTask = NULL;
    }
    if (audioMgr->rspTask != NULL) {
        audioMgr->audioTask.next = NULL;
        audioMgr->audioTask.flags = 2;
        audioMgr->audioTask.framebuffer = NULL;

        audioMgr->audioTask.list = audioMgr->rspTask->task;
        audioMgr->audioTask.msgQ = &audioMgr->unk_AC;

        audioMgr->audioTask.msg.ptr = NULL;
        osSendMesgPtr(&audioMgr->sched->cmdQ, &audioMgr->audioTask, OS_MESG_BLOCK);
        Sched_SendEntryMsg(audioMgr->sched);
    }

    D_8016A550 = osGetTime();
    if (SREG(20) >= 2) {
        rspTask = NULL;
    } else {
        rspTask = func_800E4FE0();
    }
    D_8016A558 += osGetTime() - D_8016A550;
    D_8016A550 = 0;
    if (audioMgr->rspTask != NULL) {
        osRecvMesg(&audioMgr->unk_AC, NULL, OS_MESG_BLOCK);
        func_800C3C80(audioMgr);
    }
    audioMgr->rspTask = rspTask;
}

void AudioMgr_HandlePRENMI(AudioMgr* audioMgr) {
    // "Audio manager received OS_SC_PRE_NMI_MSG"
    osSyncPrintf("オーディオマネージャが OS_SC_PRE_NMI_MSG を受け取りました\n");
    Audio_PreNMI();
}

void AudioMgr_ThreadEntry(void* arg0) {
    AudioMgr* audioMgr = (AudioMgr*)arg0;
    s16* msg = NULL;

    // while (true) {
    osRecvMesg(&audioMgr->unk_74, (OSMesg*)&msg, OS_MESG_BLOCK);
    AudioMgr_HandleRetrace(audioMgr);
    /*switch (*msg) {
        case OS_SC_RETRACE_MSG:
            AudioMgr_HandleRetrace(audioMgr);
            while (audioMgr->unk_74.validCount != 0) {
                osRecvMesg(&audioMgr->unk_74, (OSMesg*)&msg, OS_MESG_BLOCK);
                switch (*msg) {
                    case OS_SC_RETRACE_MSG:
                        break;
                    case OS_SC_PRE_NMI_MSG:
                        AudioMgr_HandlePRENMI(audioMgr);
                        break;
                }
            }
            break;
        case OS_SC_PRE_NMI_MSG:
            AudioMgr_HandlePRENMI(audioMgr);
            break;
    }*/
    //}
}

void AudioMgr_Unlock(AudioMgr* audioMgr) {
    osRecvMesg(&audioMgr->unk_C8, NULL, OS_MESG_BLOCK);
}

void AudioMgr_Init(AudioMgr* audioMgr, void* stack, OSPri pri, OSId id, SchedContext* sched, IrqMgr* irqMgr) {
    // AudioPlayer_Init();

    AudioMgr_PS3Trace("audio: AudioMgr_Init entered");
    memset(audioMgr, 0, sizeof(AudioMgr));

    audioMgr->sched = sched;
    audioMgr->irqMgr = irqMgr;
    audioMgr->rspTask = NULL;

    AudioMgr_PS3Trace("audio: creating manager queues");
    osCreateMesgQueue(&audioMgr->unk_AC, &audioMgr->unk_C4, 1);
    osCreateMesgQueue(&audioMgr->unk_74, &audioMgr->unk_8C, 8);
    osCreateMesgQueue(&audioMgr->unk_C8, &audioMgr->unk_E0, 1);

    AudioMgr_PS3Trace("audio: priming AC queue");
    osSendMesgPtr(&audioMgr->unk_AC, NULL, OS_MESG_BLOCK);
    AudioMgr_PS3Trace("audio: AC queue primed");

    static bool hasInitialized = false;

    if (!hasInitialized) {
        IrqMgrClient irqClient;

        osSyncPrintf("オーディオマネージャスレッド実行開始\n"); // "Start running audio manager thread"
        AudioMgr_PS3Trace("audio: before Audio_Init");
        Audio_Init();
        AudioMgr_PS3Trace("audio: after Audio_Init");
        AudioMgr_PS3Trace("audio: before AudioLoad_SetDmaHandler");
        AudioLoad_SetDmaHandler(DmaMgr_DmaHandler);
        AudioMgr_PS3Trace("audio: after AudioLoad_SetDmaHandler");
        AudioMgr_PS3Trace("audio: before Audio_InitSound");
        Audio_InitSound();
        AudioMgr_PS3Trace("audio: after Audio_InitSound");
        osSendMesgPtr(&audioMgr->unk_C8, NULL, OS_MESG_BLOCK);
        AudioMgr_PS3Trace("audio: unlock queue primed");

        AudioMgr_PS3Trace("audio: before volume setup");
        Audio_SetGameVolume(SEQ_PLAYER_BGM_MAIN,
                            ((float)CVarGetInteger(CVAR_SETTING("Volume.MainMusic"), 100) / 100.0f));
        Audio_SetGameVolume(SEQ_PLAYER_BGM_SUB, ((float)CVarGetInteger(CVAR_SETTING("Volume.SubMusic"), 100) / 100.0f));
        Audio_SetGameVolume(SEQ_PLAYER_FANFARE, ((float)CVarGetInteger(CVAR_SETTING("Volume.Fanfare"), 100) / 100.0f));
        Audio_SetGameVolume(SEQ_PLAYER_SFX, ((float)CVarGetInteger(CVAR_SETTING("Volume.SFX"), 100) / 100.0f));
        AudioMgr_PS3Trace("audio: after volume setup");

        // Removed due to crash
        // IrqMgr_AddClient(audioMgr->irqMgr, &irqClient, &audioMgr->unk_74);
        hasInitialized = true;
    }

    // osCreateThread(&audioMgr->unk_E8, id, AudioMgr_ThreadEntry, audioMgr, stack, pri);
    // osStartThread(&audioMgr->unk_E8);
#ifdef __PS3__
    if (oot_ps3_audio_start()) {
        sPs3AudioReady = 1;
        AudioMgr_PS3Trace("audio: PS3 audio output running");
    } else {
        AudioMgr_PS3Trace("audio: PS3 audio output unavailable; game continues silent");
    }
#endif
    AudioMgr_PS3Trace("audio: AudioMgr_Init completed");
}
