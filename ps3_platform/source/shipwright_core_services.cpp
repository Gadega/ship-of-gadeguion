// SPDX-License-Identifier: GPL-3.0-or-later
#include "libultraship/color.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

namespace {

enum ValueType : uint8_t {
    VALUE_EMPTY,
    VALUE_INTEGER,
    VALUE_FLOAT,
    VALUE_STRING,
    VALUE_RGBA,
    VALUE_RGB,
};

struct Entry {
    char name[96];
    ValueType type;
    union {
        int32_t integer;
        float floating;
        Color_RGBA8 rgba;
        Color_RGB8 rgb;
    } value;
    char string[128];
};

constexpr size_t kEntryCount = 256;
Entry gEntries[kEntryCount] = {};
bool gWindowRunning = true;

Entry* Find(const char* name) {
    if (name == nullptr) {
        return nullptr;
    }
    for (Entry& entry : gEntries) {
        if (entry.type != VALUE_EMPTY && strcmp(entry.name, name) == 0) {
            return &entry;
        }
    }
    return nullptr;
}

Entry* FindOrCreate(const char* name) {
    Entry* existing = Find(name);
    if (existing != nullptr || name == nullptr || name[0] == '\0') {
        return existing;
    }
    for (Entry& entry : gEntries) {
        if (entry.type == VALUE_EMPTY) {
            strncpy(entry.name, name, sizeof(entry.name) - 1);
            entry.name[sizeof(entry.name) - 1] = '\0';
            return &entry;
        }
    }
    return nullptr;
}

uint64_t MicrosecondsNow() {
    timeval now = {};
    gettimeofday(&now, nullptr);
    return static_cast<uint64_t>(now.tv_sec) * 1000000ULL + static_cast<uint64_t>(now.tv_usec);
}

} // namespace

extern "C" int32_t CVarGetInteger(const char* name, int32_t defaultValue) {
    Entry* entry = Find(name);
    return entry != nullptr && entry->type == VALUE_INTEGER ? entry->value.integer : defaultValue;
}

extern "C" float CVarGetFloat(const char* name, float defaultValue) {
    Entry* entry = Find(name);
    return entry != nullptr && entry->type == VALUE_FLOAT ? entry->value.floating : defaultValue;
}

extern "C" const char* CVarGetString(const char* name, const char* defaultValue) {
    Entry* entry = Find(name);
    return entry != nullptr && entry->type == VALUE_STRING ? entry->string : defaultValue;
}

extern "C" Color_RGBA8 CVarGetColor(const char* name, Color_RGBA8 defaultValue) {
    Entry* entry = Find(name);
    return entry != nullptr && entry->type == VALUE_RGBA ? entry->value.rgba : defaultValue;
}

extern "C" Color_RGB8 CVarGetColor24(const char* name, Color_RGB8 defaultValue) {
    Entry* entry = Find(name);
    return entry != nullptr && entry->type == VALUE_RGB ? entry->value.rgb : defaultValue;
}

extern "C" void CVarSetInteger(const char* name, int32_t value) {
    Entry* entry = FindOrCreate(name);
    if (entry != nullptr) {
        entry->type = VALUE_INTEGER;
        entry->value.integer = value;
    }
}

extern "C" void CVarSetFloat(const char* name, float value) {
    Entry* entry = FindOrCreate(name);
    if (entry != nullptr) {
        entry->type = VALUE_FLOAT;
        entry->value.floating = value;
    }
}

extern "C" void CVarSetString(const char* name, const char* value) {
    Entry* entry = FindOrCreate(name);
    if (entry != nullptr) {
        entry->type = VALUE_STRING;
        strncpy(entry->string, value != nullptr ? value : "", sizeof(entry->string) - 1);
        entry->string[sizeof(entry->string) - 1] = '\0';
    }
}

extern "C" void CVarSetColor(const char* name, Color_RGBA8 value) {
    Entry* entry = FindOrCreate(name);
    if (entry != nullptr) {
        entry->type = VALUE_RGBA;
        entry->value.rgba = value;
    }
}

extern "C" void CVarSetColor24(const char* name, Color_RGB8 value) {
    Entry* entry = FindOrCreate(name);
    if (entry != nullptr) {
        entry->type = VALUE_RGB;
        entry->value.rgb = value;
    }
}

extern "C" void CVarRegisterInteger(const char* name, int32_t value) {
    if (Find(name) == nullptr) CVarSetInteger(name, value);
}
extern "C" void CVarRegisterFloat(const char* name, float value) {
    if (Find(name) == nullptr) CVarSetFloat(name, value);
}
extern "C" void CVarRegisterString(const char* name, const char* value) {
    if (Find(name) == nullptr) CVarSetString(name, value);
}
extern "C" void CVarRegisterColor(const char* name, Color_RGBA8 value) {
    if (Find(name) == nullptr) CVarSetColor(name, value);
}
extern "C" void CVarRegisterColor24(const char* name, Color_RGB8 value) {
    if (Find(name) == nullptr) CVarSetColor24(name, value);
}

extern "C" void CVarClear(const char* name) {
    Entry* entry = Find(name);
    if (entry != nullptr) {
        *entry = {};
    }
}

extern "C" bool CVarExists(const char* name) {
    return Find(name) != nullptr;
}

extern "C" void CVarClearBlock(const char* prefix) {
    if (prefix == nullptr) return;
    const size_t length = strlen(prefix);
    for (Entry& entry : gEntries) {
        if (entry.type != VALUE_EMPTY && strncmp(entry.name, prefix, length) == 0) {
            entry = {};
        }
    }
}

extern "C" void CVarCopy(const char* from, const char* to) {
    Entry* source = Find(from);
    Entry* target = FindOrCreate(to);
    if (source == nullptr || target == nullptr) return;
    char targetName[sizeof(target->name)];
    strncpy(targetName, target->name, sizeof(targetName));
    *target = *source;
    strncpy(target->name, targetName, sizeof(target->name));
}

extern "C" void CVarLoad() {}
extern "C" void CVarSave() {}

extern "C" bool WindowIsRunning() { return gWindowRunning; }
extern "C" uint32_t WindowGetWidth() { return 1280U; }
extern "C" uint32_t WindowGetHeight() { return 720U; }
extern "C" float WindowGetAspectRatio() { return 16.0f / 9.0f; }
extern "C" int32_t WindowGetPosX() { return 0; }
extern "C" int32_t WindowGetPosY() { return 0; }
extern "C" bool WindowIsFullscreen() { return true; }

extern "C" uint64_t GetFrequency() { return 1000000ULL; }
extern "C" uint64_t GetPerfCounter() { return MicrosecondsNow(); }
extern "C" uint64_t osGetTime() { return MicrosecondsNow(); }
extern "C" uint32_t osGetCount() { return static_cast<uint32_t>(MicrosecondsNow()); }
extern "C" uint64_t GetUnixTimestamp() { return static_cast<uint64_t>(time(nullptr)); }
extern "C" int Controller_ShouldRumble(size_t) { return 1; }
