// SPDX-License-Identifier: GPL-3.0-or-later
#include "shipwright_probe/zip_o2r_probe.h"

#include "shipwright_probe/store_zip_archive.h"

#include <string>
#include <vector>

namespace {

const char* kZipO2rPath = "/dev_hdd0/game/OOTP30046/USRDIR/assets/synthetic_zip.o2r";

} // namespace

OotPs3ZipO2rProbeResult oot_ps3_zip_o2r_probe_load(void) {
    OotPs3ZipO2rProbeResult result = {0, 0, 0};
    ShipwrightProbe::StoreZipArchive archive;

    if (!archive.Open(kZipO2rPath)) {
        return result;
    }
    result.flags |= OOT_PS3_ZIP_O2R_OPENED | OOT_PS3_ZIP_O2R_TABLE;
    result.entry_count = archive.EntryCount();
    result.stored_count = archive.StoredEntryCount();

    if (archive.EntryCount() != 4U ||
        !archive.Has("version") ||
        !archive.Has("manifest.json") ||
        !archive.Has("scenes/test_scene/room_0/display_list.bin") ||
        !archive.Has("textures/ui/status.rgba")) {
        return result;
    }
    result.flags |= OOT_PS3_ZIP_O2R_LOOKUP;

    if (archive.StoredEntryCount() != 4U) {
        return result;
    }
    result.flags |= OOT_PS3_ZIP_O2R_STORED;

    std::vector<unsigned char> version;
    std::string manifest;
    std::string scene;
    std::vector<unsigned char> texture;
    if (!archive.Read("version", &version) ||
        !archive.ReadText("manifest.json", &manifest) ||
        !archive.ReadText("scenes/test_scene/room_0/display_list.bin", &scene) ||
        !archive.Read("textures/ui/status.rgba", &texture)) {
        return result;
    }
    result.flags |= OOT_PS3_ZIP_O2R_CRC;

    if (version.size() == 5U &&
        manifest.find("\"name\":\"Synthetic PS3 O2R\"") != std::string::npos &&
        scene == "zip-backed-o2r-resource-placeholder\n" &&
        texture.size() == 16U) {
        result.flags |= OOT_PS3_ZIP_O2R_CONTENT;
    }

    return result;
}
