#pragma once

#include <stdint.h>

#ifdef __cplusplus
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <stdexcept>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <stdarg.h>
namespace std {
typedef atomic<bool> atomic_bool;
using ::strtoull;
#if defined(__powerpc64__) || defined(__PPU__)
inline string to_string(int value) { char buf[32]; snprintf(buf, sizeof(buf), "%d", value); return string(buf); }
inline string to_string(unsigned value) { char buf[32]; snprintf(buf, sizeof(buf), "%u", value); return string(buf); }
inline string to_string(long value) { char buf[32]; snprintf(buf, sizeof(buf), "%ld", value); return string(buf); }
inline string to_string(unsigned long value) { char buf[32]; snprintf(buf, sizeof(buf), "%lu", value); return string(buf); }
inline string to_string(long long value) { char buf[32]; snprintf(buf, sizeof(buf), "%lld", value); return string(buf); }
inline string to_string(unsigned long long value) { char buf[32]; snprintf(buf, sizeof(buf), "%llu", value); return string(buf); }
inline int stoi(const string& value, size_t* pos = nullptr, int base = 10) {
    char* end = nullptr;
    long out = strtol(value.c_str(), &end, base);
    if (pos != nullptr) {
        *pos = end != nullptr ? static_cast<size_t>(end - value.c_str()) : 0;
    }
    return static_cast<int>(out);
}
#endif
inline uint32_t rotr(uint32_t value, unsigned int count) {
    count &= 31U;
    return (value >> count) | (value << ((32U - count) & 31U));
}
}

// Avoid C++20 __VA_OPT__ in Shipwright's randomizer enum defaults. The boot
// path only needs these headers to parse during this collision scan.
#ifndef RANDO_ENUM_BEGIN
#define RANDO_ENUM_BEGIN(EnumName) typedef enum EnumName {
#define RANDO_ENUM_ITEM(name, ...) name,
#define RANDO_ENUM_END(EnumName) } EnumName;
#endif

// Cheap C++20 unordered_map/set compatibility for diagnostics.
#ifndef contains
#define contains count
#endif
#endif

#ifndef PS3_COMPAT_GET_ITEM_ENTRY_DEFINED
#define PS3_COMPAT_GET_ITEM_ENTRY_DEFINED

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

static const unsigned char gEmptyTexture[8] = {0};

#ifndef LOG_LEVEL_GAME_PRINTS
#define LOG_LEVEL_GAME_PRINTS 6
#endif

#ifndef SPDLOG_ACTIVE_LEVEL
#define SPDLOG_ACTIVE_LEVEL 6
#endif

#ifndef CVAR_PREFIX_RANDOMIZER_ENHANCEMENT
#define CVAR_PREFIX_RANDOMIZER_ENHANCEMENT "gRandomizerEnhancement"
#endif
#ifndef CVAR_PREFIX_RANDOMIZER_SETTING
#define CVAR_PREFIX_RANDOMIZER_SETTING "gRandomizerSetting"
#endif
#ifndef CVAR_PREFIX_COSMETIC
#define CVAR_PREFIX_COSMETIC "gCosmetic"
#endif
#ifndef CVAR_PREFIX_AUDIO
#define CVAR_PREFIX_AUDIO "gAudio"
#endif
#ifndef CVAR_PREFIX_CHEAT
#define CVAR_PREFIX_CHEAT "gCheat"
#endif
#ifndef CVAR_PREFIX_ENHANCEMENT
#define CVAR_PREFIX_ENHANCEMENT "gEnhancement"
#endif
#ifndef CVAR_PREFIX_SETTING
#define CVAR_PREFIX_SETTING "gSetting"
#endif
#ifndef CVAR_PREFIX_WINDOW
#define CVAR_PREFIX_WINDOW "gWindow"
#endif
#ifndef CVAR_PREFIX_TRACKER
#define CVAR_PREFIX_TRACKER "gTracker"
#endif
#ifndef CVAR_PREFIX_DEVELOPER_TOOLS
#define CVAR_PREFIX_DEVELOPER_TOOLS "gDeveloperTools"
#endif
#ifndef CVAR_PREFIX_GENERAL
#define CVAR_PREFIX_GENERAL "gGeneral"
#endif
#ifndef CVAR_PREFIX_REMOTE
#define CVAR_PREFIX_REMOTE "gRemote"
#endif
#ifndef CVAR_PREFIX_GAMEPLAY_STATS
#define CVAR_PREFIX_GAMEPLAY_STATS "gGameplayStats"
#endif
#ifndef CVAR_PREFIX_TIME_DISPLAY
#define CVAR_PREFIX_TIME_DISPLAY "gTimeDisplay"
#endif
