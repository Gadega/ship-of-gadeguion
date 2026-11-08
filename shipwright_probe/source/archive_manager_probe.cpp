// SPDX-License-Identifier: GPL-3.0-or-later
#include "shipwright_probe/archive_manager_probe.h"

#include "shipwright_probe/ship_hash.h"
#include "shipwright_probe/store_archive_manager.h"

#include <string>

namespace {

const char* kBasePath = "/dev_hdd0/game/OOTP30046/USRDIR/assets/synthetic_zip.o2r";
const char* kOverridePath = "/dev_hdd0/game/OOTP30046/USRDIR/assets/synthetic_override.o2r";
const char* kSceneName = "scenes/test_scene/room_0/display_list.bin";
const char* kVersionName = "version";
const char* kOverrideScene = "override-scene-from-second-archive\n";

} // namespace

OotPs3ArchiveManagerProbeResult oot_ps3_archive_manager_probe_load(void) {
    OotPs3ArchiveManagerProbeResult result = {0, 0, 0};
    ShipwrightProbe::StoreArchiveManager manager;

    if (!manager.AddArchive(kBasePath)) {
        return result;
    }
    result.flags |= OOT_PS3_MANAGER_OPENED_BASE;

    if (!manager.AddArchive(kOverridePath)) {
        return result;
    }
    result.flags |= OOT_PS3_MANAGER_OPENED_OVERRIDE;
    result.archive_count = manager.ArchiveCount();
    result.indexed_count = manager.IndexedCount();

    const uint64_t scene_hash = ShipwrightProbe::ShipCrc64Name(kSceneName);
    const uint64_t version_hash = ShipwrightProbe::ShipCrc64Name(kVersionName);
    if (!manager.Has(scene_hash) || !manager.Has(version_hash) ||
        manager.ArchiveCount() != 2U || manager.IndexedCount() != 6U) {
        return result;
    }
    result.flags |= OOT_PS3_MANAGER_INDEXED;

    std::string version;
    if (!manager.ReadText(version_hash, &version) || version.size() != 5U) {
        return result;
    }
    result.flags |= OOT_PS3_MANAGER_BASE_READ;

    std::string scene;
    if (!manager.ReadText(scene_hash, &scene)) {
        return result;
    }
    result.flags |= OOT_PS3_MANAGER_OVERRIDE_READ;

    if (scene == kOverrideScene) {
        result.flags |= OOT_PS3_MANAGER_PRIORITY;
    }

    return result;
}
