// SPDX-License-Identifier: GPL-3.0-or-later
#include "shipwright_probe/o2r_probe.h"

#include "shipwright_probe/resource_archive.h"

#include <string>
#include <vector>

namespace {

const char* kSyntheticO2rPath = "/dev_hdd0/game/OOTP30046/USRDIR/assets/synthetic.o2r";
const char* kExpectedSceneName = "scenes/test_scene/room_0/display_list.bin";
const char* kExpectedConfigName = "meta/platform_ps3.txt";
const char* kExpectedScenePayload = "synthetic-fast3d-display-list-placeholder\n";
const char* kExpectedConfigPayload = "o2r=synthetic\nplatform=ps3\nrenderer=rsx-next\n";

uint32_t checksum_bytes(const std::vector<unsigned char>& bytes) {
    uint32_t checksum = 0x811C9DC5U;
    for (std::vector<unsigned char>::const_iterator it = bytes.begin(); it != bytes.end(); ++it) {
        checksum ^= *it;
        checksum *= 16777619U;
    }
    return checksum;
}

} // namespace

OotPs3O2rProbeResult oot_ps3_o2r_probe_load(void) {
    OotPs3O2rProbeResult result = {0, 0, 0};
    ShipwrightProbe::ResourceArchive archive;

    if (!archive.Open(kSyntheticO2rPath)) {
        return result;
    }
    result.flags |= OOT_PS3_O2R_OPENED | OOT_PS3_O2R_HEADER;
    result.resource_count = archive.EntryCount();

    if (archive.EntryCount() != 4U) {
        return result;
    }
    result.flags |= OOT_PS3_O2R_TABLE;

    if (!archive.Has(kExpectedSceneName) ||
        !archive.Has(kExpectedConfigName) ||
        !archive.Has("objects/link_stub/model.bin") ||
        !archive.Has("textures/ui/status.rgba")) {
        return result;
    }
    result.flags |= OOT_PS3_O2R_LOOKUP;

    std::string scene_text;
    std::string config_text;
    std::vector<unsigned char> model_bytes;
    std::vector<unsigned char> status_texture;
    if (!archive.ReadText(kExpectedSceneName, &scene_text) ||
        !archive.ReadText(kExpectedConfigName, &config_text) ||
        !archive.Read("objects/link_stub/model.bin", &model_bytes) ||
        !archive.Read("textures/ui/status.rgba", &status_texture)) {
        return result;
    }
    result.flags |= OOT_PS3_O2R_CRC;

    result.payload_checksum = checksum_bytes(model_bytes) ^ checksum_bytes(status_texture);

    if (scene_text == kExpectedScenePayload &&
        config_text == kExpectedConfigPayload &&
        model_bytes.size() == 16U &&
        status_texture.size() == 16U &&
        result.payload_checksum != 0U) {
        result.flags |= OOT_PS3_O2R_CONTENT;
    }

    return result;
}
