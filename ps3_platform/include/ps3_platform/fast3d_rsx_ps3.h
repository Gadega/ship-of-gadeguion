#ifndef OOT_PS3_FAST3D_RSX_H
#define OOT_PS3_FAST3D_RSX_H

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Shipwright Fast3D vertex inputs and material selector feed the RSX combiner. */
int oot_ps3_rsx_begin(uint32_t width, uint32_t height);
int oot_ps3_rsx_texture(uint32_t id, const uint8_t* rgba, uint32_t width, uint32_t height);
int oot_ps3_rsx_triangles(const float* vbo, uint32_t stride, uint32_t triangles,
                          uint32_t texture0_id, uint32_t texture1_id, int alpha,
                          uint32_t inputs, int material);
void oot_ps3_rsx_clear(int color, int depth);
void oot_ps3_rsx_end(void);
void oot_ps3_rsx_viewport(int x, int y, int width, int height);
void oot_ps3_rsx_scissor(int x, int y, int width, int height);

/* GPU vertex pipeline (fast3d_ps3_tnl.vcg). One combiner input slot of a draw:
 * its rgb (and alpha) come from the vertex shade colour, or are the constant value. */
typedef struct {
    int rgbShade;
    int alphaShade;
    float value[4];
} OotPs3TnlInput;
int oot_ps3_rsx_tnl_combiner(const OotPs3TnlInput* inputs, uint32_t numInputs, int alphaInputs, float out[25][4]);
int oot_ps3_rsx_tnl_draw(const void* records, uint32_t count, float* consts, int drawDirty, uint32_t firstConst,
                         uint32_t numConst, uint32_t texture0_id, uint32_t texture1_id, int alpha, int cullMode,
                         int cullFlip, int floatAttribs);
void oot_ps3_rsx_take_tnl_stats(unsigned int* tnlDraws, unsigned int* tnlVerts, unsigned int* cpuDraws,
                                unsigned int* vpSwitches, unsigned int* constVec4, unsigned int* combHits,
                                unsigned int* combMisses);

#ifdef __cplusplus
}
#endif
#endif
