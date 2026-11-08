// SPDX-License-Identifier: GPL-3.0-or-later
// JPEG decoding for pre-rendered room backgrounds (ResourceMgr_LoadJPEG).
// upstream_compat/include/stb_image.h is a stub, so the real stb_image is
// compiled here on its own, JPEG only.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC // another file already builds stb_image (zlib only)
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS
#include "../../third_party/stb/stb_image.h"

unsigned char* oot_ps3_jpeg_decode_rgba(const unsigned char* data, int size, int* width, int* height) {
    int components = 0;
    return stbi_load_from_memory(data, size, width, height, &components, STBI_rgb_alpha);
}

void oot_ps3_jpeg_free(unsigned char* pixels) {
    stbi_image_free(pixels);
}
