// SPDX-License-Identifier: GPL-3.0-or-later
// PS3 audio output: a libaudio port fed by an audio thread, like SoH's OTRAudio_Thread.
// No game headers here (PS3 system headers clash with them); the game side is
// oot_ps3_audio_produce() in audioMgr.c, which runs AudioMgr_CreateNextAudioBuffer.

#include <audio/audio.h>
#include <sys/thread.h>
#include <lv2/thread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern void oot_ps3_early_trace(const char* message);
extern unsigned long long oot_ps3_microseconds_now(void);
/* SPU mixer (ps3_spu_audio.c) and the command recorder in soh/mixer.c. */
int oot_ps3_spu_audio_init(void);
int oot_ps3_spu_audio_active(void);
void oot_ps3_spu_audio_shutdown(void);
void oot_ps3_spu_audio_take_stats(unsigned int* runs, unsigned long long* totalUs, unsigned long long* maxUs);
void oot_ps3_mixer_set_spu(int on);
void oot_ps3_mixer_flush(void);
/* Game side: fills `frames` interleaved stereo s16 frames at 32 kHz. */
extern void oot_ps3_audio_produce(int16_t* out, uint32_t frames);

enum {
    GAME_RATE = 32000,
    OUT_RATE = 48000,
    /* Blocks of AUDIO_BLOCK_SAMPLES (256) frames kept queued ahead of the hardware:
     * 8 * 256 / 48000 = 43 ms. The port ring holds 16 blocks. */
    TARGET_AHEAD = 8,
    /* 32 kHz frames waiting to be resampled. */
    FIFO_FRAMES = 4096,
};

static u32 sPort;
static audioPortConfig sConfig;
static sys_ppu_thread_t sThread;
static volatile int sRunning;
static volatile int sPrimed;
static volatile int sThreadDone;
static int sStarted;

static int16_t sFifo[FIFO_FRAMES * 2];
static uint32_t sFifoFrames;
static double sFifoPos; /* fractional read position in sFifo, in 32 kHz frames */
static int32_t sSampleDebtThirds;

static unsigned long long sProduceUs;
static unsigned int sProduceCalls;
static unsigned int sUnderruns;
static unsigned long long sProduceMaxUs;
static unsigned long long sWakeGapMaxUs;
static unsigned int sMinAhead = 99;
static unsigned int sZeroFills;

/* The game thread's Audio_Update (sound/sequence commands and state) and the synthesizer
 * share engine state; on PC SoH runs synthesis right after the game update, so they rarely
 * overlap. Here they would, every frame at 40 fps: serialize them (Audio_Update < 1 ms). */
static volatile int sEngineLock;

/* Stall diagnostics for the watchdog: where the audio thread is and when it last woke. */
extern unsigned long long oot_ps3_thread_key(void);
static volatile unsigned long long sAudioThreadKey;
unsigned long long oot_ps3_audio_thread_key(void) { return sAudioThreadKey; }
static volatile int sAudioStage; /* 0 loop, 1 waiting for the engine lock, 2 synthesizing, 3 SPU mixing */
static volatile unsigned long long sAudioLastWakeUs;
void oot_ps3_audio_debug_state(char* out, unsigned size) {
    const unsigned long long now = oot_ps3_microseconds_now();
    snprintf(out, size, "audio stage=%d (1 lock wait, 2 synth, 3 SPU) last wake %llu ms ago engineLock=%d", sAudioStage,
             sAudioLastWakeUs != 0 ? (now - sAudioLastWakeUs) / 1000ULL : 0ULL, sEngineLock);
}

void oot_ps3_audio_lock(void) {
    while (__sync_lock_test_and_set(&sEngineLock, 1)) sysThreadYield();
}

void oot_ps3_audio_unlock(void) {
    __sync_lock_release(&sEngineLock);
}

/* Same cadence as SoH: 528 or 544 frames per engine update, averaging
 * 32000 / 60 = 533.33 so the sequencer tempo (60 updates per second) holds. */
static void ProduceMore(void) {
    const uint32_t frames = sSampleDebtThirds > 0 ? 544 : 528;
    unsigned long long t0, spent;
    if (sFifoFrames + frames > FIFO_FRAMES) return; /* no room: don't advance the tempo bookkeeping */
    sSampleDebtThirds += 1600 - 3 * (int32_t)frames;
    t0 = oot_ps3_microseconds_now();
    sAudioStage = 1;
    oot_ps3_audio_lock();
    sAudioStage = 2;
    oot_ps3_audio_produce(sFifo + sFifoFrames * 2, frames);
    oot_ps3_audio_unlock();
    /* SPU mode: the commands recorded above run on the SPU now, outside the engine
     * lock (the game's Audio_Update doesn't touch what the mixer reads or writes,
     * as on the N64 where the RSP ran the list in parallel with the CPU). */
    sAudioStage = 3;
    oot_ps3_mixer_flush();
    sAudioStage = 0;
    spent = oot_ps3_microseconds_now() - t0;
    sProduceUs += spent;
    if (spent > sProduceMaxUs) sProduceMaxUs = spent;
    sProduceCalls++;
    sFifoFrames += frames;
}

/* 4-point Hermite (Catmull-Rom) interpolation of one channel at fraction t in [x1, x2]. */
static float Hermite(float x0, float x1, float x2, float x3, float t) {
    const float c1 = 0.5f * (x2 - x0);
    const float c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
    const float c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
    return ((c3 * t + c2) * t + c1) * t + x1;
}

static float ClampUnit(float v) {
    return v > 1.0f ? 1.0f : (v < -1.0f ? -1.0f : v);
}

/* 32 kHz -> 48 kHz into one 256-frame float block. Cubic instead of linear: linear
 * interpolation dulls the highs and adds faint grit/aliasing (fairy chimes, high notes).
 * The FIFO keeps one frame before the read position for the 4-point kernel. */
static void FillBlock(float* out) {
    const double step = (double)GAME_RATE / (double)OUT_RATE;
    const double needed = sFifoPos + step * AUDIO_BLOCK_SAMPLES + 3.0;
    uint32_t consumed;
    int i;
    if (sFifoFrames == 0) { /* start: one silent frame of history */
        sFifo[0] = sFifo[1] = 0;
        sFifoFrames = 1;
        sFifoPos = 1.0;
    }
    while ((double)sFifoFrames < needed && sFifoFrames + 544 <= FIFO_FRAMES) {
        ProduceMore();
    }
    for (i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
        uint32_t index = (uint32_t)sFifoPos;
        const float t = (float)(sFifoPos - (double)index);
        const int16_t* f;
        if (index < 1 || index + 2 >= sFifoFrames) {
            out[i * 2] = 0.0f;
            out[i * 2 + 1] = 0.0f;
            sZeroFills++;
            continue;
        }
        f = sFifo + (index - 1) * 2;
        out[i * 2] = ClampUnit(Hermite(f[0], f[2], f[4], f[6], t) * (1.0f / 32768.0f));
        out[i * 2 + 1] = ClampUnit(Hermite(f[1], f[3], f[5], f[7], t) * (1.0f / 32768.0f));
        sFifoPos += step;
    }
    /* Drop consumed frames but keep one before the read position as history. */
    consumed = (uint32_t)sFifoPos;
    consumed = consumed > 1 ? consumed - 1 : 0;
    if (consumed > sFifoFrames) consumed = sFifoFrames;
    memmove(sFifo, sFifo + consumed * 2, (sFifoFrames - consumed) * 2 * sizeof(int16_t));
    sFifoFrames -= consumed;
    sFifoPos -= (double)consumed;
}

static void AudioThread(void* arg) {
    float* const ring = (float*)(uintptr_t)sConfig.audioDataStart;
    const uint32_t blocks = (uint32_t)sConfig.numBlocks;
    const uint32_t blockFloats = (uint32_t)sConfig.channelCount * AUDIO_BLOCK_SAMPLES;
    uint32_t writeBlock = 0;
    int writeValid = 0;
    unsigned long long lastReportUs = 0;
    unsigned long long lastWakeUs = 0;
    (void)arg;

    oot_ps3_early_trace("audio-out: thread running");
    sAudioThreadKey = oot_ps3_thread_key();
    while (sRunning) {
        uint32_t readBlock;
        uint32_t ahead;
        if (!sPrimed) {
            usleep(5000);
            continue;
        }
        readBlock = (uint32_t)(*(volatile uint64_t*)(uintptr_t)sConfig.readIndex % blocks);
        if (!writeValid) {
            writeBlock = (readBlock + 1) % blocks;
            writeValid = 1;
        }
        ahead = (writeBlock + blocks - readBlock) % blocks;
        {
            const unsigned long long wake = oot_ps3_microseconds_now();
            if (lastWakeUs != 0 && wake - lastWakeUs > sWakeGapMaxUs) sWakeGapMaxUs = wake - lastWakeUs;
            lastWakeUs = wake;
            sAudioLastWakeUs = wake;
        }
        if (ahead < sMinAhead) sMinAhead = ahead;
        if (ahead == 0) {
            /* The hardware reached our write position: we fell behind. */
            sUnderruns++;
            writeBlock = (readBlock + 1) % blocks;
            ahead = 1;
        }
        while (ahead < TARGET_AHEAD && sRunning) {
            FillBlock(ring + writeBlock * blockFloats);
            writeBlock = (writeBlock + 1) % blocks;
            ahead++;
        }
        {
            const unsigned long long now = oot_ps3_microseconds_now();
            if (now - lastReportUs >= 10000000ULL) { /* every 10 s */
                char line[224];
                snprintf(line, sizeof(line),
                         "graph: Play audio-out updates=%u synth=%llu us maxSynth=%llu us underruns=%u "
                         "minAhead=%u/%u blocks maxWakeGap=%llu us zeroFill=%u (last 10 s)",
                         sProduceCalls, sProduceUs, sProduceMaxUs, sUnderruns, sMinAhead, (unsigned)TARGET_AHEAD,
                         sWakeGapMaxUs, sZeroFills);
                if (lastReportUs != 0) oot_ps3_early_trace(line);
                if (oot_ps3_spu_audio_active()) {
                    unsigned int spuRuns;
                    unsigned long long spuUs, spuMaxUs;
                    oot_ps3_spu_audio_take_stats(&spuRuns, &spuUs, &spuMaxUs);
                    snprintf(line, sizeof(line), "graph: Play audio-spu runs=%u total=%llu us max=%llu us (last 10 s)",
                             spuRuns, spuUs, spuMaxUs);
                    if (lastReportUs != 0) oot_ps3_early_trace(line);
                }
                lastReportUs = now;
                sProduceCalls = 0;
                sProduceUs = 0;
                sUnderruns = 0;
                sProduceMaxUs = 0;
                sWakeGapMaxUs = 0;
                sMinAhead = 99;
                sZeroFills = 0;
            }
        }
        usleep(2000);
    }
    sThreadDone = 1;
    sysThreadExit(0);
}

/* Opens the port and starts the thread; synthesis waits for oot_ps3_audio_kick(). */
int oot_ps3_audio_start(void) {
    audioPortParam params;
    char line[192];
    s32 result;
    if (sStarted) return 1;
    result = audioInit();
    if (result != 0) {
        snprintf(line, sizeof(line), "audio-out: audioInit failed %08x", (unsigned)result);
        oot_ps3_early_trace(line);
        return 0;
    }
    memset(&params, 0, sizeof(params));
    params.numChannels = AUDIO_PORT_2CH;
    params.numBlocks = AUDIO_BLOCK_16;
    params.attrib = AUDIO_PORT_INITLEVEL;
    params.level = 1.0f;
    result = audioPortOpen(&params, &sPort);
    if (result != 0) {
        snprintf(line, sizeof(line), "audio-out: audioPortOpen failed %08x", (unsigned)result);
        oot_ps3_early_trace(line);
        audioQuit();
        return 0;
    }
    result = audioGetPortConfig(sPort, &sConfig);
    if (result != 0 || sConfig.audioDataStart == 0 || sConfig.readIndex == 0 || sConfig.numBlocks == 0) {
        oot_ps3_early_trace("audio-out: audioGetPortConfig failed");
        audioPortClose(sPort);
        audioQuit();
        return 0;
    }
    memset((void*)(uintptr_t)sConfig.audioDataStart, 0, sConfig.portSize);
    snprintf(line, sizeof(line), "audio-out: port %u open channels=%llu blocks=%llu size=%u",
             (unsigned)sPort, (unsigned long long)sConfig.channelCount, (unsigned long long)sConfig.numBlocks,
             (unsigned)sConfig.portSize);
    oot_ps3_early_trace(line);
    result = audioPortStart(sPort);
    if (result != 0) {
        snprintf(line, sizeof(line), "audio-out: audioPortStart failed %08x", (unsigned)result);
        oot_ps3_early_trace(line);
        audioPortClose(sPort);
        audioQuit();
        return 0;
    }
    sRunning = 1;
    if (oot_ps3_spu_audio_init()) oot_ps3_mixer_set_spu(1);
    if (sysThreadCreate(&sThread, AudioThread, NULL, 900, 256 * 1024, THREAD_JOINABLE, "oot-audio") != 0) {
        oot_ps3_early_trace("audio-out: thread creation failed");
        sRunning = 0;
        audioPortStop(sPort);
        audioPortClose(sPort);
        audioQuit();
        return 0;
    }
    sStarted = 1;
    oot_ps3_early_trace("audio-out: started (32 kHz synth -> 48 kHz port)");
    return 1;
}

/* Called after the game's per-frame Audio_Update: the engine is ready to synthesize. */
void oot_ps3_audio_kick(void) {
    sPrimed = 1;
}

/* Before exit: stop the thread and release the port so the XMB gets the audio device back. */
void oot_ps3_audio_shutdown(void) {
    u64 code;
    int waitedMs = 0;
    if (!sStarted) return;
    sRunning = 0;
    oot_ps3_spu_audio_shutdown();
    /* Join only if the thread actually left its loop (at most ~300 ms): a thread stuck
     * inside the engine must not keep the console from returning to the XMB. */
    while (!sThreadDone && waitedMs < 300) {
        usleep(10000);
        waitedMs += 10;
    }
    if (sThreadDone) {
        sysThreadJoin(sThread, &code);
    } else {
        oot_ps3_early_trace("audio-out: thread did not stop; closing the port anyway");
    }
    audioPortStop(sPort);
    audioPortClose(sPort);
    audioQuit();
    sStarted = 0;
}
