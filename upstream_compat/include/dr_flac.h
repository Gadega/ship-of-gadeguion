#pragma once
typedef int drflac_bool32;
typedef unsigned long long drflac_uint64;
typedef struct drflac { int unused; } drflac;
static inline drflac* drflac_open_memory(const void*, unsigned long, void*) { return 0; }
static inline drflac_uint64 drflac_read_pcm_frames_s16(drflac*, drflac_uint64, short*) { return 0; }
static inline void drflac_close(drflac*) {}
