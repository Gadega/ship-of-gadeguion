// SPDX-License-Identifier: GPL-3.0-or-later
#include "shipwright_probe/title_boot_probe.h"

#include "ps3_platform/software_renderer.h"
#include "shipwright_probe/lus_shell.h"
#include "shipwright_probe/ship_hash.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

const char* kBasePath = "/dev_hdd0/game/OOTP30046/USRDIR/assets/synthetic_deflated.o2r";
const char* kOverridePath = "/dev_hdd0/game/OOTP30046/USRDIR/assets/synthetic_override.o2r";
const char* kTitlePath = "scenes/title/title_display_list.ps3gfx";
const char* kTitleScenePath = "scenes/title/title_scene.ps3scene";
const char* kTitleActorsPath = "scenes/title/title_actors.ps3actor";

enum CommandType {
    COMMAND_CLEAR,
    COMMAND_RECT,
    COMMAND_PULSE,
    COMMAND_SCROLL,
};

struct TitleCommand {
    CommandType type;
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    uint32_t color_a;
    uint32_t color_b;
};

struct QueuedTitleCommand {
    CommandType type;
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    uint32_t color;
};

struct TitleDrawQueue {
    QueuedTitleCommand commands[32];
    uint32_t count;
    uint32_t checksum;
};

struct TitleSceneDescriptor {
    bool magic_ok;
    char display_path[96];
    char actors_path[96];
    uint32_t phases;
    uint32_t heap_size;
    uint32_t fps;
    uint32_t checksum;
};

struct TitleActor {
    uint32_t id;
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    uint32_t color;
};

struct TitleActorTable {
    bool magic_ok;
    TitleActor actors[8];
    uint32_t count;
    uint32_t checksum;
};

uint32_t parse_hex_color(const char* text) {
    return (uint32_t)std::strtoul(text, NULL, 16) & 0x00FFFFFFU;
}

uint32_t mix_text_checksum(uint32_t checksum, const char* text) {
    if (text == NULL) {
        return checksum;
    }
    while (*text != '\0') {
        checksum ^= (uint8_t)*text++;
        checksum *= 16777619U;
    }
    return checksum;
}

uint32_t mix_checksum(uint32_t checksum, const TitleCommand& command) {
    checksum ^= (uint32_t)command.type + 0x9E3779B9U + (checksum << 6U) + (checksum >> 2U);
    checksum ^= command.x + (command.y << 3U) + (command.w << 7U) + (command.h << 11U);
    checksum ^= command.color_a + (command.color_b << 1U);
    return checksum;
}

void queue_push(TitleDrawQueue* queue, CommandType type, uint32_t x, uint32_t y,
                uint32_t w, uint32_t h, uint32_t color) {
    if (queue == NULL || queue->count >= 32U) {
        return;
    }
    QueuedTitleCommand* command = &queue->commands[queue->count++];
    command->type = type;
    command->x = x;
    command->y = y;
    command->w = w;
    command->h = h;
    command->color = color & 0x00FFFFFFU;
}

uint32_t process_queue(TitleDrawQueue* queue) {
    uint32_t checksum = 0x51554555U;
    if (queue == NULL) {
        return 0;
    }
    for (uint32_t i = 0; i < queue->count; ++i) {
        const QueuedTitleCommand* command = &queue->commands[i];
        checksum ^= ((uint32_t)command->type + 1U) * 0x45D9F3BU;
        checksum += command->x + (command->y << 3U) + (command->w << 7U) + (command->h << 11U);
        checksum ^= command->color + (checksum << 6U) + (checksum >> 2U);
    }
    queue->checksum = checksum;
    return checksum;
}

TitleDrawQueue build_title_queue(const TitleCommand* commands, uint32_t command_count, uint32_t frame) {
    TitleDrawQueue queue = {{{COMMAND_RECT, 0, 0, 0, 0, 0}}, 0, 0};

    for (uint32_t i = 0; i < command_count && i < 24U; ++i) {
        const TitleCommand* command = &commands[i];
        if (command->type == COMMAND_CLEAR) {
            queue_push(&queue, COMMAND_CLEAR, 0, 0, 256, 144, command->color_a);
        } else if (command->type == COMMAND_RECT) {
            queue_push(&queue, COMMAND_RECT, command->x, command->y, command->w, command->h, command->color_a);
        } else if (command->type == COMMAND_PULSE) {
            const uint32_t color = (frame & 32U) ? command->color_a : command->color_b;
            queue_push(&queue, COMMAND_RECT, command->x, command->y, command->w, command->h, color);
        } else if (command->type == COMMAND_SCROLL) {
            const uint32_t travel = command->w > 32U ? command->w - 32U : 1U;
            const uint32_t offset = (frame * 2U) % travel;
            queue_push(&queue, COMMAND_RECT, command->x + offset, command->y, 32, command->h, command->color_a);
        }
    }

    process_queue(&queue);
    return queue;
}

uint32_t parse_commands(const std::string& text, TitleCommand* commands, uint32_t max_commands,
                        uint32_t* rect_count, uint32_t* checksum, bool* magic_ok) {
    uint32_t count = 0;
    const char* cursor = text.c_str();
    char line[160];

    if (rect_count != NULL) {
        *rect_count = 0;
    }
    if (checksum != NULL) {
        *checksum = 0x5449544CU;
    }
    if (magic_ok != NULL) {
        *magic_ok = false;
    }

    while (*cursor != '\0') {
        uint32_t len = 0;
        while (cursor[len] != '\0' && cursor[len] != '\n' && len + 1U < sizeof(line)) {
            line[len] = cursor[len];
            len++;
        }
        line[len] = '\0';
        cursor += len;
        if (*cursor == '\n') {
            cursor++;
        }

        if (len == 0U || line[0] == '#') {
            continue;
        }
        if (std::strcmp(line, "PS3GFX1") == 0) {
            if (magic_ok != NULL) {
                *magic_ok = true;
            }
            continue;
        }

        TitleCommand command = {COMMAND_RECT, 0, 0, 0, 0, 0, 0};
        char color_a[16] = {0};
        char color_b[16] = {0};
        unsigned int x = 0;
        unsigned int y = 0;
        unsigned int w = 0;
        unsigned int h = 0;

        if (std::sscanf(line, "CLEAR %15s", color_a) == 1) {
            command.type = COMMAND_CLEAR;
            command.color_a = parse_hex_color(color_a);
        } else if (std::sscanf(line, "RECT %u %u %u %u %15s", &x, &y, &w, &h, color_a) == 5) {
            command.type = COMMAND_RECT;
            command.x = x;
            command.y = y;
            command.w = w;
            command.h = h;
            command.color_a = parse_hex_color(color_a);
            if (rect_count != NULL) {
                (*rect_count)++;
            }
        } else if (std::sscanf(line, "PULSE %u %u %u %u %15s %15s", &x, &y, &w, &h, color_a, color_b) == 6) {
            command.type = COMMAND_PULSE;
            command.x = x;
            command.y = y;
            command.w = w;
            command.h = h;
            command.color_a = parse_hex_color(color_a);
            command.color_b = parse_hex_color(color_b);
            if (rect_count != NULL) {
                (*rect_count)++;
            }
        } else if (std::sscanf(line, "SCROLL %u %u %u %u %15s", &x, &y, &w, &h, color_a) == 5) {
            command.type = COMMAND_SCROLL;
            command.x = x;
            command.y = y;
            command.w = w;
            command.h = h;
            command.color_a = parse_hex_color(color_a);
            if (rect_count != NULL) {
                (*rect_count)++;
            }
        } else {
            continue;
        }

        if (count < max_commands) {
            commands[count] = command;
        }
        if (checksum != NULL) {
            *checksum = mix_checksum(*checksum, command);
        }
        count++;
    }

    return count;
}

TitleSceneDescriptor parse_scene_descriptor(const std::string& text) {
    TitleSceneDescriptor scene = {false, {0}, {0}, 0, 0, 0, 0x5343454EU};
    const char* cursor = text.c_str();
    char line[160];

    while (*cursor != '\0') {
        uint32_t len = 0;
        while (cursor[len] != '\0' && cursor[len] != '\n' && len + 1U < sizeof(line)) {
            line[len] = cursor[len];
            len++;
        }
        line[len] = '\0';
        cursor += len;
        if (*cursor == '\n') {
            cursor++;
        }

        if (len == 0U || line[0] == '#') {
            continue;
        }
        scene.checksum = mix_text_checksum(scene.checksum, line);

        if (std::strcmp(line, "PS3SCENE1") == 0) {
            scene.magic_ok = true;
            continue;
        }

        char display_path[96] = {0};
        char actors_path[96] = {0};
        unsigned int value = 0;
        if (std::sscanf(line, "DISPLAY %95s", display_path) == 1) {
            std::strncpy(scene.display_path, display_path, sizeof(scene.display_path) - 1U);
        } else if (std::sscanf(line, "ACTORS %95s", actors_path) == 1) {
            std::strncpy(scene.actors_path, actors_path, sizeof(scene.actors_path) - 1U);
        } else if (std::sscanf(line, "PHASES %u", &value) == 1) {
            scene.phases = value;
        } else if (std::sscanf(line, "HEAP %u", &value) == 1) {
            scene.heap_size = value;
        } else if (std::sscanf(line, "FPS %u", &value) == 1) {
            scene.fps = value;
        }
    }

    return scene;
}

TitleActorTable parse_actor_table(const std::string& text) {
    TitleActorTable table = {false, {{0, 0, 0, 0, 0, 0}}, 0, 0x41435452U};
    const char* cursor = text.c_str();
    char line[160];

    while (*cursor != '\0') {
        uint32_t len = 0;
        while (cursor[len] != '\0' && cursor[len] != '\n' && len + 1U < sizeof(line)) {
            line[len] = cursor[len];
            len++;
        }
        line[len] = '\0';
        cursor += len;
        if (*cursor == '\n') {
            cursor++;
        }

        if (len == 0U || line[0] == '#') {
            continue;
        }
        table.checksum = mix_text_checksum(table.checksum, line);

        if (std::strcmp(line, "PS3ACTOR1") == 0) {
            table.magic_ok = true;
            continue;
        }

        unsigned int id = 0;
        unsigned int x = 0;
        unsigned int y = 0;
        unsigned int w = 0;
        unsigned int h = 0;
        char color[16] = {0};
        if (std::sscanf(line, "ACTOR %u %u %u %u %u %15s", &id, &x, &y, &w, &h, color) == 6 &&
            table.count < 8U) {
            TitleActor* actor = &table.actors[table.count++];
            actor->id = id;
            actor->x = x;
            actor->y = y;
            actor->w = w;
            actor->h = h;
            actor->color = parse_hex_color(color);
        }
    }

    return table;
}

bool load_scene_descriptor(ShipwrightProbe::LusShell::ArchiveManager* resources,
                           TitleSceneDescriptor* descriptor, uint32_t* scene_size) {
    if (resources == NULL || descriptor == NULL) {
        return false;
    }

    ShipwrightProbe::LusShell::File file;
    if (!resources->LoadFile(kTitleScenePath, &file) || file.buffer.empty()) {
        return false;
    }
    const std::string text((const char*)file.buffer.data(), file.buffer.size());
    *descriptor = parse_scene_descriptor(text);
    if (scene_size != NULL) {
        *scene_size = (uint32_t)file.buffer.size();
    }
    return descriptor->magic_ok && std::strcmp(descriptor->display_path, kTitlePath) == 0 &&
           std::strcmp(descriptor->actors_path, kTitleActorsPath) == 0 &&
           descriptor->phases >= 2U && descriptor->fps >= 10U;
}

bool load_actor_table(ShipwrightProbe::LusShell::ArchiveManager* resources,
                      TitleActorTable* table, uint32_t* actor_size) {
    if (resources == NULL || table == NULL) {
        return false;
    }

    ShipwrightProbe::LusShell::File file;
    if (!resources->LoadFile(kTitleActorsPath, &file) || file.buffer.empty()) {
        return false;
    }
    const std::string text((const char*)file.buffer.data(), file.buffer.size());
    *table = parse_actor_table(text);
    if (actor_size != NULL) {
        *actor_size = (uint32_t)file.buffer.size();
    }
    return table->magic_ok && table->count >= 3U && table->checksum != 0U;
}

bool load_title_file(ShipwrightProbe::LusShell::File* file) {
    ShipwrightProbe::LusShell::ArchiveManager resources;
    if (!resources.AddArchive(kBasePath) || !resources.AddArchive(kOverridePath)) {
        return false;
    }
    return resources.LoadFile(kTitlePath, file);
}

bool load_cached_title_commands(TitleCommand* commands, uint32_t max_commands, uint32_t* command_count, bool* magic_ok) {
    static bool attempted = false;
    static bool loaded = false;
    static TitleCommand cached[24];
    static uint32_t cached_count = 0;
    static bool cached_magic = false;

    if (!attempted) {
        attempted = true;
        ShipwrightProbe::LusShell::File file;
        if (load_title_file(&file) && !file.buffer.empty()) {
            const std::string text((const char*)file.buffer.data(), file.buffer.size());
            uint32_t rect_count = 0;
            uint32_t checksum = 0;
            cached_count = parse_commands(text, cached, 24U, &rect_count, &checksum, &cached_magic);
            loaded = cached_magic && cached_count > 0U;
        }
    }

    if (!loaded) {
        if (command_count != NULL) {
            *command_count = 0;
        }
        if (magic_ok != NULL) {
            *magic_ok = false;
        }
        return false;
    }

    const uint32_t copy_count = cached_count < max_commands ? cached_count : max_commands;
    for (uint32_t i = 0; i < copy_count; ++i) {
        commands[i] = cached[i];
    }
    if (command_count != NULL) {
        *command_count = copy_count;
    }
    if (magic_ok != NULL) {
        *magic_ok = cached_magic;
    }
    return true;
}

bool load_cached_actor_table(TitleActor* actors, uint32_t max_actors, uint32_t* actor_count, uint32_t* checksum) {
    static bool attempted = false;
    static bool loaded = false;
    static TitleActor cached[8];
    static uint32_t cached_count = 0;
    static uint32_t cached_checksum = 0;

    if (!attempted) {
        attempted = true;
        ShipwrightProbe::LusShell::ArchiveManager resources;
        if (resources.AddArchive(kBasePath) && resources.AddArchive(kOverridePath)) {
            TitleActorTable table;
            uint32_t actor_size = 0;
            if (load_actor_table(&resources, &table, &actor_size)) {
                cached_count = table.count;
                cached_checksum = table.checksum ^ actor_size;
                const uint32_t copy_count = cached_count < 8U ? cached_count : 8U;
                for (uint32_t i = 0; i < copy_count; ++i) {
                    cached[i] = table.actors[i];
                }
                loaded = true;
            }
        }
    }

    if (!loaded) {
        if (actor_count != NULL) {
            *actor_count = 0;
        }
        if (checksum != NULL) {
            *checksum = 0;
        }
        return false;
    }

    const uint32_t copy_count = cached_count < max_actors ? cached_count : max_actors;
    for (uint32_t i = 0; i < copy_count; ++i) {
        actors[i] = cached[i];
    }
    if (actor_count != NULL) {
        *actor_count = copy_count;
    }
    if (checksum != NULL) {
        *checksum = cached_checksum;
    }
    return true;
}

} // namespace

OotPs3TitleBootProbeResult oot_ps3_title_boot_probe_load(void) {
    OotPs3TitleBootProbeResult result = {};
    ShipwrightProbe::LusShell::ArchiveManager resources;

    const uint64_t title_hash = ShipwrightProbe::ShipCrc64Name(kTitlePath);
    result.path_hash_high = (uint32_t)(title_hash >> 32U);
    result.path_hash_low = (uint32_t)title_hash;

    if (!resources.AddArchive(kBasePath) || !resources.AddArchive(kOverridePath)) {
        return result;
    }
    result.flags |= OOT_PS3_TITLE_BOOT_ARCHIVES;

    TitleSceneDescriptor scene;
    if (load_scene_descriptor(&resources, &scene, &result.scene_size)) {
        result.flags |= OOT_PS3_TITLE_BOOT_SCENE;
        result.scene_phases = scene.phases;
        result.scene_fps = scene.fps;
        result.scene_checksum = scene.checksum;
    }
    {
        TitleActorTable table;
        if (load_actor_table(&resources, &table, &result.actor_size)) {
            result.flags |= OOT_PS3_TITLE_BOOT_ACTORS;
            result.actor_count = table.count;
            result.actor_checksum = table.checksum;
        }
    }

    ShipwrightProbe::LusShell::File file;
    if (!resources.LoadFile(title_hash, &file) || file.buffer.empty()) {
        return result;
    }
    result.flags |= OOT_PS3_TITLE_BOOT_LOADED;
    result.file_size = (uint32_t)file.buffer.size();

    const std::string text((const char*)file.buffer.data(), file.buffer.size());
    bool magic_ok = false;
    TitleCommand scratch[24];
    result.command_count = parse_commands(text, scratch, 24U, &result.rect_count, &result.checksum, &magic_ok);
    {
        const TitleDrawQueue queue = build_title_queue(scratch, result.command_count, 0);
        result.queued_count = queue.count;
        result.queue_checksum = queue.checksum;
    }
    if (magic_ok) {
        result.flags |= OOT_PS3_TITLE_BOOT_MAGIC;
    }
    if (result.command_count >= 5U && result.rect_count >= 4U) {
        result.flags |= OOT_PS3_TITLE_BOOT_COMMANDS;
    }
    if (result.checksum != 0U && result.checksum != 0x5449544CU) {
        result.flags |= OOT_PS3_TITLE_BOOT_CHECKSUM;
    }
    if (result.queued_count >= 5U && result.queue_checksum != 0U) {
        result.flags |= OOT_PS3_TITLE_BOOT_QUEUE;
    }

    return result;
}

void oot_ps3_title_boot_draw(OotPs3DisplayBuffer* buffer, uint32_t frame,
                             int32_t x, int32_t y, uint32_t scale) {
    if (buffer == NULL || scale == 0U) {
        return;
    }

    TitleCommand commands[24];
    TitleActor actors[8];
    uint32_t command_count = 0;
    uint32_t actor_count = 0;
    uint32_t actor_checksum = 0;
    bool magic_ok = false;

    if (!load_cached_title_commands(commands, 24U, &command_count, &magic_ok)) {
        oot_ps3_renderer_rect(buffer, x - 2, y - 2, (u16)(260U * scale), (u16)(148U * scale), 0x00B00000U);
        return;
    }
    load_cached_actor_table(actors, 8U, &actor_count, &actor_checksum);

    const uint32_t scene_phase = (frame / 20U) & 3U;
    const TitleDrawQueue queue = build_title_queue(commands, command_count, frame);
    const uint32_t border = magic_ok && queue.count >= 5U && queue.checksum != 0U ? 0x0000FF80U : 0x00FFFFFFU;
    oot_ps3_renderer_rect(buffer, x - 2, y - 2, (u16)(260U * scale), (u16)(148U * scale), border);

    for (uint32_t i = 0; i < queue.count && i < 32U; ++i) {
        const QueuedTitleCommand* command = &queue.commands[i];
        const uint32_t draw_w = command->w * scale;
        const uint32_t draw_h = command->h * scale;
        if (command->type == COMMAND_CLEAR || command->type == COMMAND_RECT) {
            oot_ps3_renderer_rect(buffer, x + (int32_t)(command->x * scale), y + (int32_t)(command->y * scale),
                                  (u16)draw_w, (u16)draw_h, command->color);
        }
    }

    for (uint32_t i = 0; i < queue.count && i < 12U; ++i) {
        const uint32_t bit = (queue.checksum >> (i & 15U)) & 1U;
        const uint32_t active = (i & 3U) == scene_phase;
        const uint32_t color = active ? 0x00FFFFFFU : (bit ? 0x00FFD080U : 0x00406080U);
        oot_ps3_renderer_rect(buffer, x + (int32_t)((8U + (i * 10U)) * scale),
                              y + (int32_t)(134U * scale), (u16)(6U * scale), (u16)(4U * scale), color);
    }

    for (uint32_t i = 0; i < actor_count && i < 8U; ++i) {
        const TitleActor* actor = &actors[i];
        const uint32_t bob = ((frame + actor->id * 7U) >> 4U) & 1U;
        const uint32_t tint = ((actor_checksum >> (i * 3U)) & 1U) ? 0x00080808U : 0U;
        oot_ps3_renderer_rect(buffer, x + (int32_t)(actor->x * scale),
                              y + (int32_t)((actor->y + bob) * scale),
                              (u16)(actor->w * scale), (u16)(actor->h * scale),
                              (actor->color ^ tint) & 0x00FFFFFFU);
    }
}
