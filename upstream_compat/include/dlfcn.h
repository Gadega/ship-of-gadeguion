#pragma once
#define RTLD_LAZY 0
#define RTLD_NOW 0
static inline void* dlopen(const char*, int) { return 0; }
static inline void* dlsym(void*, const char*) { return 0; }
static inline int dlclose(void*) { return 0; }
static inline const char* dlerror(void) { return "dlfcn unavailable on ps3 probe"; }
