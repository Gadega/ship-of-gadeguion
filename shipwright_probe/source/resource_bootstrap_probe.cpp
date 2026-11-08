// SPDX-License-Identifier: GPL-3.0-or-later
#include "shipwright_probe/resource_bootstrap_probe.h"

#include "shipwright_probe/lus_shell.h"
#include "shipwright_probe/ship_hash.h"

#include <string>

namespace {

const char* kBasePath = "/dev_hdd0/game/OOTP30046/USRDIR/assets/synthetic_deflated.o2r";
const char* kOverridePath = "/dev_hdd0/game/OOTP30046/USRDIR/assets/synthetic_override.o2r";
const char* kSceneName = "scenes/test_scene/room_0/display_list.bin";
const char* kOverrideScene = "override-scene-from-second-archive\n";

} // namespace

OotPs3ResourceBootstrapProbeResult oot_ps3_resource_bootstrap_probe_load(void) {
    OotPs3ResourceBootstrapProbeResult result = {0, 0, 0, 0xFFFFFFFFU, 0, 0, 0xFFFFFFFFU, 0, 0};
    ShipwrightProbe::LusShell::ArchiveManager resources;

    if (!resources.AddArchive(kBasePath) || !resources.AddArchive(kOverridePath)) {
        return result;
    }
    result.archive_count = resources.ArchiveCount();
    result.file_count = resources.FileCount();
    if (result.archive_count == 2U && result.file_count == 9U) {
        result.flags |= OOT_PS3_RESOURCE_BOOT_MOUNTED;
    }

    ShipwrightProbe::LusShell::File version;
    if (resources.LoadFile("version", &version)) {
        result.version_value = ShipwrightProbe::LusShell::ReadGameVersion(version);
        result.flags |= OOT_PS3_RESOURCE_BOOT_VERSION;
    }

    ShipwrightProbe::LusShell::File manifest;
    if (resources.LoadFile("manifest.json", &manifest) && manifest.buffer.size() > 0U) {
        result.manifest_size = (uint32_t)manifest.buffer.size();
        result.flags |= OOT_PS3_RESOURCE_BOOT_MANIFEST;
    }

    const uint64_t scene_hash = ShipwrightProbe::ShipCrc64Name(kSceneName);
    result.scene_hash_high = (uint32_t)(scene_hash >> 32U);
    result.scene_hash_low = (uint32_t)scene_hash;

    const std::string* resolved = resources.HashToString(scene_hash);
    if (resolved != NULL && *resolved == kSceneName) {
        result.flags |= OOT_PS3_RESOURCE_BOOT_HASH_STRING;
    }

    ShipwrightProbe::LusShell::File scene;
    if (resources.LoadFile(scene_hash, &scene) && scene.buffer.size() > 0U) {
        result.scene_size = (uint32_t)scene.buffer.size();
        result.flags |= OOT_PS3_RESOURCE_BOOT_SCENE_HASH;
    }

    result.scene_priority = (uint32_t)resources.GetFilePriority(scene_hash);
    if (scene.loaded && result.scene_priority == 1U) {
        const std::string scene_text((const char*)scene.buffer.data(), scene.buffer.size());
        if (scene_text == kOverrideScene) {
            result.flags |= OOT_PS3_RESOURCE_BOOT_PRIORITY;
        }
    }

    return result;
}
