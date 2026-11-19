// SPDX-License-Identifier: GPL-3.0-or-later
/* Save-state relocation registry (ps3_savestate.c + the resource bridge).
 *
 * A save state copies the game's memory (system heap, audio heap, gSaveContext, ...).
 * That memory points into blocks the resource bridge allocated with malloc (decoded
 * skeletons, scene command lists, collision, cutscenes, audio samples/fonts/sequences,
 * message tables). After a restart those blocks live at other addresses. The bridge
 * records every such block with a stable key (kind, name/hash, index); a save state
 * stores the keys of the blocks it points into, and on load the blocks are recreated
 * by key and every pointer is moved to the new address. */
#ifndef PS3_RELOC_H
#define PS3_RELOC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PS3_RELOC_RES = 1,       /* bridge Resource: index 0 = data, 1+k = display-list reference k */
    PS3_RELOC_SCENE = 2,     /* decoded scene/room (Ps3Scene); extra = depth */
    PS3_RELOC_CUTSCENE = 3,  /* converted cutscene words */
    PS3_RELOC_COLLISION = 4, /* converted collision header + arrays */
    PS3_RELOC_AUDIO_SAMPLE = 5,
    PS3_RELOC_AUDIO_FONT = 6,
    PS3_RELOC_AUDIO_SEQ = 7,
    PS3_RELOC_HEAP = 8,      /* the game's system/audio heaps */
    PS3_RELOC_MESSAGES = 9   /* message tables (built at boot) */
};

typedef struct {
    int kind;
    unsigned int index;
    unsigned int extra;
    unsigned long long hash;
    const char* name; /* valid until oot_ps3_reloc_end_query() */
    uintptr_t addr;
    size_t size;
} Ps3RelocInfo;

/* Register a block (idempotent for the same key). */
void oot_ps3_reloc_register(int kind, const char* name, unsigned int index, const void* ptr, size_t size);
/* Address lookups: wrap them in begin/end (takes the bridge lock, sorts once). */
void oot_ps3_reloc_begin_query(void);
int oot_ps3_reloc_find(uintptr_t value, Ps3RelocInfo* out);
void oot_ps3_reloc_end_query(void);
/* Make sure the block with this key exists in this run (loads/decodes it if needed) and
 * return its current address and size. 1 = ok. */
int oot_ps3_reloc_resolve(int kind, const char* name, unsigned long long hash, unsigned int index,
                          unsigned int extra, uintptr_t* addr, size_t* size);
/* Malloc heap (ps3_sbrk_fixed.c): base and size of the LV2 block behind malloc. */
void oot_ps3_heap_range(uintptr_t* base, size_t* size);

#ifdef __cplusplus
}
#endif
#endif
