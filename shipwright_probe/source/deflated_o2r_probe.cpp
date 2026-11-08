// SPDX-License-Identifier: GPL-3.0-or-later
#include "shipwright_probe/deflated_o2r_probe.h"

#include "shipwright_probe/ship_hash.h"
#include "shipwright_probe/store_zip_archive.h"

#include <string>
#include <vector>

namespace {

const char* kDeflatedPath = "/dev_hdd0/game/OOTP30046/USRDIR/assets/synthetic_deflated.o2r";
const char* kSceneName = "scenes/test_scene/room_0/display_list.bin";

} // namespace

OotPs3DeflatedO2rProbeResult oot_ps3_deflated_o2r_probe_load(void) {
    OotPs3DeflatedO2rProbeResult result = {0, 0, 0};
    ShipwrightProbe::StoreZipArchive archive;

    if (!archive.Open(kDeflatedPath)) {
        return result;
    }
    result.flags |= OOT_PS3_DEFLATED_O2R_OPENED;
    result.entry_count = archive.EntryCount();
    result.stored_count = archive.StoredEntryCount();

    const uint64_t scene_hash = ShipwrightProbe::ShipCrc64Name(kSceneName);
    const uint64_t manifest_hash = ShipwrightProbe::ShipCrc64Name("manifest.json");
    if (archive.EntryCount() != 4U || archive.StoredEntryCount() != 0U ||
        !archive.HasHash(scene_hash) || !archive.HasHash(manifest_hash)) {
        return result;
    }
    result.flags |= OOT_PS3_DEFLATED_O2R_INDEXED;

    std::string scene;
    std::string manifest;
    std::vector<unsigned char> texture;
    if (!archive.ReadTextByHash(scene_hash, &scene) ||
        !archive.ReadTextByHash(manifest_hash, &manifest) ||
        !archive.Read("textures/ui/status.rgba", &texture)) {
        return result;
    }
    result.flags |= OOT_PS3_DEFLATED_O2R_INFLATED | OOT_PS3_DEFLATED_O2R_CRC;

    if (scene.find("deflated-o2r-resource-placeholder") == 0U &&
        manifest.find("Synthetic Deflated PS3 O2R") != std::string::npos &&
        texture.size() == 16U) {
        result.flags |= OOT_PS3_DEFLATED_O2R_CONTENT;
    }

    return result;
}
