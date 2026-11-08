// SPDX-License-Identifier: GPL-3.0-or-later
#include "shipwright_probe/lus_user_archive_probe.h"

#include "shipwright_probe/lus_shell.h"
#include "shipwright_probe/ship_hash.h"

#include <cstdio>

namespace {

const char* kUserArchivePaths[] = {
    "/dev_usb000/oot_user.o2r",
    "/dev_usb001/oot_user.o2r",
    "/dev_usb006/oot_user.o2r",
    "/dev_hdd0/tmp/oot_user.o2r",
    "/dev_hdd0/game/OOTP30046/USRDIR/assets/oot_user.o2r",
};

bool file_exists(const char* path) {
    FILE* file = std::fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    std::fclose(file);
    return true;
}

} // namespace

OotPs3LusUserArchiveProbeResult oot_ps3_lus_user_archive_probe_load(void) {
    OotPs3LusUserArchiveProbeResult result = {0, 0, 0, 0xFFFFFFFFU, 0, 0, 0, 0, 0xFFFFFFFFU};

    const char* selected_path = NULL;
    for (uint32_t i = 0; i < (uint32_t)(sizeof(kUserArchivePaths) / sizeof(kUserArchivePaths[0])); ++i) {
        if (file_exists(kUserArchivePaths[i])) {
            selected_path = kUserArchivePaths[i];
            result.path_index = i;
            result.flags |= OOT_PS3_LUS_USER_FOUND;
            break;
        }
    }
    if (selected_path == NULL) {
        return result;
    }

    ShipwrightProbe::LusShell::ArchiveManager manager;
    if (!manager.AddArchive(selected_path)) {
        return result;
    }

    result.file_count = manager.FileCount();
    result.flags |= OOT_PS3_LUS_USER_MOUNTED;

    ShipwrightProbe::LusShell::File version;
    if (manager.LoadFile("version", &version)) {
        result.version_value = ShipwrightProbe::LusShell::ReadGameVersion(version);
        result.flags |= OOT_PS3_LUS_USER_VERSION;
    }

    ShipwrightProbe::LusShell::File manifest;
    if (manager.LoadFile("manifest.json", &manifest) && manifest.buffer.size() > 0U) {
        result.manifest_size = (uint32_t)manifest.buffer.size();
        result.flags |= OOT_PS3_LUS_USER_MANIFEST;
    }

    uint64_t sample_hash = 0U;
    if (manager.FirstSmallDataHash(&sample_hash)) {
        const std::string* sample_path = manager.HashToString(sample_hash);
        if (sample_path != NULL && ShipwrightProbe::ShipCrc64Name(sample_path->c_str()) == sample_hash) {
            result.flags |= OOT_PS3_LUS_USER_HASH_TO_STRING;
        }

        ShipwrightProbe::LusShell::File sample;
        if (manager.LoadFile(sample_hash, &sample) && sample.buffer.size() > 0U) {
            result.sample_size = (uint32_t)sample.buffer.size();
            result.sample_hash_high = (uint32_t)(sample_hash >> 32U);
            result.sample_hash_low = (uint32_t)sample_hash;
            result.sample_priority = (uint32_t)manager.GetFilePriority(sample_hash);
            result.flags |= OOT_PS3_LUS_USER_SAMPLE;
        }
    }

    return result;
}
