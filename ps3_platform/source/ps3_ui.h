// SPDX-License-Identifier: GPL-3.0-or-later
/* Menu drawing helpers (ps3_ui.c). Virtual coordinates: 1080 high, ps3ui_width() wide. */
#ifndef PS3_UI_H
#define PS3_UI_H
#include <stdint.h>

int ps3ui_begin(unsigned int screenWidth, unsigned int screenHeight);
void ps3ui_end(void);
float ps3ui_width(void);
void ps3ui_rect(float x, float y, float w, float h, uint32_t rgba);
void ps3ui_gradient(float x, float y, float w, float h, uint32_t top, uint32_t bottom, int steps);
float ps3ui_text(float x, float y, float px, uint32_t rgba, int bold, const char* text);
float ps3ui_text_width(const char* text, float px, int bold);
void ps3ui_text_centered(float centreX, float y, float px, uint32_t rgba, int bold, const char* text);
void ps3ui_text_right(float rightX, float y, float px, uint32_t rgba, int bold, const char* text);

#endif
