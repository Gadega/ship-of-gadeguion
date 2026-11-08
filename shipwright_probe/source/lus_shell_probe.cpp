// SPDX-License-Identifier: GPL-3.0-or-later
#include "shipwright_probe/lus_shell_probe.h"

#include "shipwright_probe/lus_shell.h"
#include "shipwright_probe/ship_hash.h"

#include <string>

namespace {

const char* kBasePath = "/dev_hdd0/game/OOTP30046/USRDIR/assets/synthetic_deflated.o2r";
const char* kOverridePath = "/dev_hdd0/game/OOTP30046/USRDIR/assets/synthetic_override.o2r";
const char* kSceneName = "scenes/test_scene/room_0/display_list.bin";
const char* kOverrideScene = "override-scene-from-second-archive\n";

} // namespace

OotPs3LusShellProbeResult oot_ps3_lus_shell_probe_load(void) {
    OotPs3LusShellProbeResult result = {0, 0, 0, 0xFFFFFFFFU, 0, 0, 0, 0};
    ShipwrightProbe::LusShell::ArchiveManager manager;

    if (!manager.AddArchive(kBasePath) || !manager.AddArchive(kOverridePath)) {
        return result;
    }

    result.archive_count = manager.ArchiveCount();
    result.file_count = manager.FileCount();
    if (result.archive_count != 2U || result.file_count != 9U) {
        return result;
    }
    result.flags |= OOT_PS3_LUS_SHELL_OPENED;

    ShipwrightProbe::LusShell::File version;
    if (manager.LoadFile("version", &version)) {
        result.version_value = ShipwrightProbe::LusShell::ReadGameVersion(version);
        result.flags |= OOT_PS3_LUS_SHELL_VERSION;
    }

    ShipwrightProbe::LusShell::File manifest;
    if (manager.LoadFile("manifest.json", &manifest) && manifest.buffer.size() > 0U) {
        result.manifest_size = (uint32_t)manifest.buffer.size();
        result.flags |= OOT_PS3_LUS_SHELL_MANIFEST;
    }

    const uint64_t scene_hash = ShipwrightProbe::ShipCrc64Name(kSceneName);
    ShipwrightProbe::LusShell::File scene;
    if (manager.LoadFile(scene_hash, &scene) && scene.buffer.size() > 0U) {
        result.scene_size = (uint32_t)scene.buffer.size();
        result.scene_hash_high = (uint32_t)(scene_hash >> 32U);
        result.scene_hash_low = (uint32_t)scene_hash;
        result.flags |= OOT_PS3_LUS_SHELL_HASH_LOAD;
    }

    if (scene.loaded) {
        const std::string scene_text((const char*)scene.buffer.data(), scene.buffer.size());
        if (scene_text == kOverrideScene) {
            result.flags |= OOT_PS3_LUS_SHELL_PRIORITY;
        }
    }

    return result;
}
