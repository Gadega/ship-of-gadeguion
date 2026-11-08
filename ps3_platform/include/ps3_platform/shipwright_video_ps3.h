// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OOT_PS3_SHIPWRIGHT_VIDEO_H
#define OOT_PS3_SHIPWRIGHT_VIDEO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int oot_ps3_shipwright_video_start(void);
void* oot_ps3_shipwright_video_rsx_context(void);
uint32_t oot_ps3_shipwright_video_rsx_offset(void);
int oot_ps3_shipwright_video_acquire_fast3d(uint32_t** pixels, uint32_t* width, uint32_t* height);
void oot_ps3_shipwright_video_present_fast3d(void);
typedef struct {
    uint16_t left;
    uint16_t top;
    uint16_t right;
    uint16_t bottom;
} OotPs3TitleRect;
void oot_ps3_shipwright_video_present_title_layout(const OotPs3TitleRect* rects, uint32_t count);
int oot_ps3_shipwright_video_present_title_texture(const OotPs3TitleRect* rects, uint32_t count,
                                                   const uint8_t* texture, uint32_t size,
                                                   uint32_t width, uint32_t height);
int oot_ps3_shipwright_video_present_logo_vertices(const int16_t* xyz, uint32_t count, uint32_t frame);
typedef struct {
    uint16_t index[3];
    uint32_t color;
} OotPs3LogoTriangle;
int oot_ps3_shipwright_video_present_logo_mesh(const int16_t* xyz, uint32_t vertex_count,
                                               const OotPs3LogoTriangle* triangles,
                                               uint32_t triangle_count, uint32_t frame);
void oot_ps3_shipwright_video_present_boot_stage(uint32_t stage, uint32_t frame);
void oot_ps3_shipwright_video_present_heartbeat(uint32_t frame);
uint32_t oot_ps3_shipwright_video_width(void);
uint32_t oot_ps3_shipwright_video_height(void);
void oot_ps3_shipwright_video_stop(void);

#ifdef __cplusplus
}
#endif

#endif
