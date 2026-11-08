#pragma once
typedef struct vorbis_info { int channels; long rate; } vorbis_info;
typedef struct vorbis_comment { int unused; } vorbis_comment;
typedef struct vorbis_dsp_state { int unused; } vorbis_dsp_state;
typedef struct vorbis_block { int unused; } vorbis_block;
static inline void vorbis_info_init(vorbis_info*) {}
static inline int vorbis_synthesis_headerin(vorbis_info*, vorbis_comment*, void*) { return 0; }
static inline int vorbis_synthesis_init(vorbis_dsp_state*, vorbis_info*) { return 0; }
static inline int vorbis_block_init(vorbis_dsp_state*, vorbis_block*) { return 0; }
static inline int vorbis_synthesis(vorbis_block*, void*) { return 0; }
static inline int vorbis_synthesis_blockin(vorbis_dsp_state*, vorbis_block*) { return 0; }
static inline int vorbis_synthesis_pcmout(vorbis_dsp_state*, float***) { return 0; }
static inline int vorbis_synthesis_read(vorbis_dsp_state*, int) { return 0; }
static inline void vorbis_block_clear(vorbis_block*) {}
static inline void vorbis_dsp_clear(vorbis_dsp_state*) {}
static inline void vorbis_comment_clear(vorbis_comment*) {}
static inline void vorbis_info_clear(vorbis_info*) {}
