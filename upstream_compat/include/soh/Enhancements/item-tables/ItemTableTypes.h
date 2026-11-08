#pragma once

// PS3 upstream-collision stub. The real item-table header pulls randomizer
// enum macros that require C++20 __VA_OPT__, which the PS3 GCC cannot parse.
// First boot does not need randomizer item tables.

#include <stdint.h>

typedef enum {
    ITEM_TABLE_NONE = 0,
} ItemTableType;

#ifndef PS3_COMPAT_GET_ITEM_ENTRY_DEFINED
#define PS3_COMPAT_GET_ITEM_ENTRY_DEFINED

// z64actor.h and z64player.h embed this type by value even when the
// randomizer implementation is disabled.  Keep the upstream ABI layout while
// avoiding the C++20-only enum generator pulled by the desktop header graph.
typedef enum GetItemFrom {
    ITEM_FROM_NPC,
    ITEM_FROM_SKULLTULA,
    ITEM_FROM_FREESTANDING,
    ITEM_FROM_CHEST,
} GetItemFrom;

typedef enum GetItemCategory {
    ITEM_CATEGORY_JUNK,
    ITEM_CATEGORY_LESSER,
    ITEM_CATEGORY_HEALTH,
    ITEM_CATEGORY_BOSS_KEY,
    ITEM_CATEGORY_SMALL_KEY,
    ITEM_CATEGORY_SKULLTULA_TOKEN,
    ITEM_CATEGORY_MAJOR,
} GetItemCategory;

typedef struct PlayState PlayState;
typedef struct GetItemEntry GetItemEntry;
typedef void (*CustomDrawFunc)(PlayState*, GetItemEntry*);

struct GetItemEntry {
    uint16_t itemId;
    uint16_t field;
    int16_t gi;
    uint16_t textId;
    uint16_t objectId;
    uint16_t modIndex;
    uint16_t tableId;
    int16_t getItemId;
    uint16_t gid;
    uint16_t collectable;
    GetItemFrom getItemFrom;
    GetItemCategory getItemCategory;
    uint16_t drawItemId;
    uint16_t drawModIndex;
    CustomDrawFunc drawFunc;
};

#endif
