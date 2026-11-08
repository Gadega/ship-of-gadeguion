#pragma once
typedef int drmp3_bool32;
typedef unsigned long long drmp3_uint64;
typedef struct drmp3 { int unused; } drmp3;
typedef struct drmp3_config { unsigned int channels; unsigned int sampleRate; } drmp3_config;
static inline drmp3_bool32 drmp3_init_memory(drmp3*, const void*, unsigned long, void*) { return 0; }
static inline drmp3_uint64 drmp3_read_pcm_frames_s16(drmp3*, drmp3_uint64, short*) { return 0; }
static inline void drmp3_uninit(drmp3*) {}
