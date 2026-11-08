// SPDX-License-Identifier: GPL-3.0-or-later
#include "shipwright_probe/hash_lookup_probe.h"

#include "shipwright_probe/ship_hash.h"
#include "shipwright_probe/store_zip_archive.h"

#include <string>

namespace {

const char* kZipO2rPath = "/dev_hdd0/game/OOTP30046/USRDIR/assets/synthetic_zip.o2r";
const char* kResourceName = "scenes/test_scene/room_0/display_list.bin";

} // namespace

OotPs3HashLookupProbeResult oot_ps3_hash_lookup_probe_load(void) {
    OotPs3HashLookupProbeResult result = {0, 0, 0};
    ShipwrightProbe::StoreZipArchive archive;

    if (!archive.Open(kZipO2rPath)) {
        return result;
    }
    result.flags |= OOT_PS3_HASH_LOOKUP_OPENED;

    const uint64_t hash = ShipwrightProbe::ShipCrc64Name(kResourceName);
    result.hash_low = (uint32_t)(hash & 0xFFFFFFFFULL);
    result.hash_high = (uint32_t)(hash >> 32U);
    if (hash == 0U) {
        return result;
    }
    result.flags |= OOT_PS3_HASH_LOOKUP_HASHED;

    if (!archive.HasHash(hash)) {
        return result;
    }
    result.flags |= OOT_PS3_HASH_LOOKUP_FOUND;

    std::string scene;
    if (!archive.ReadTextByHash(hash, &scene)) {
        return result;
    }
    result.flags |= OOT_PS3_HASH_LOOKUP_READ;

    if (scene == "zip-backed-o2r-resource-placeholder\n") {
        result.flags |= OOT_PS3_HASH_LOOKUP_CONTENT;
    }

    return result;
}
