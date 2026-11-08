#pragma once
static inline unsigned char* stbi_load_from_memory(const unsigned char*, int, int* x, int* y, int* comp, int req_comp) {
    if (x != 0) {
        *x = 0;
    }
    if (y != 0) {
        *y = 0;
    }
    if (comp != 0) {
        *comp = req_comp;
    }
    return 0;
}
static inline void stbi_image_free(void*) {}
