// SPDX-License-Identifier: GPL-3.0-or-later
#include "shipwright_probe/soh_resource_api_probe.h"

#include "shipwright_probe/lus_shell.h"
#include "shipwright_probe/ship_hash.h"
#include "shipwright_probe/soh_upstream_hooks_probe.h"

#include <string>

namespace {

const char* kBasePath = "/dev_hdd0/game/OOTP30046/USRDIR/assets/synthetic_deflated.o2r";
const char* kOverridePath = "/dev_hdd0/game/OOTP30046/USRDIR/assets/synthetic_override.o2r";
const char* kSceneName = "scenes/test_scene/room_0/display_list.bin";
const char* kExpectedScene = "override-scene-from-second-archive\n";

class ResourceMgrShim {
  public:
    bool EnsureReady() {
        if (ready_) {
            return true;
        }

        if (!archives_.AddArchive(kBasePath) || !archives_.AddArchive(kOverridePath)) {
            return false;
        }

        ShipwrightProbe::LusShell::File version;
        if (!archives_.LoadFile("version", &version)) {
            return false;
        }
        game_version_ = ShipwrightProbe::LusShell::ReadGameVersion(version);
        ready_ = true;
        return true;
    }

    bool FileExists(const char* file_path) {
        return file_path != nullptr && EnsureReady() &&
               archives_.HasFile(ShipwrightProbe::ShipCrc64Name(file_path));
    }

    uint32_t GameVersion() {
        return EnsureReady() ? game_version_ : 0xFFFFFFFFU;
    }

    const char* HashToCString(uint64_t hash) {
        if (!EnsureReady()) {
            return nullptr;
        }
        const std::string* path = archives_.HashToString(hash);
        return path == nullptr ? nullptr : path->c_str();
    }

    uint32_t Priority(uint64_t hash) {
        if (!EnsureReady()) {
            return 0xFFFFFFFFU;
        }
        const int32_t priority = archives_.GetFilePriority(hash);
        return priority < 0 ? 0xFFFFFFFFU : (uint32_t)priority;
    }

    uint32_t LoadByName(const char* file_path, uint8_t* output, uint32_t capacity) {
        if (file_path == nullptr) {
            return 0U;
        }
        return LoadByHash(ShipwrightProbe::ShipCrc64Name(file_path), output, capacity);
    }

    uint32_t LoadByHash(uint64_t hash, uint8_t* output, uint32_t capacity) {
        if (output == nullptr || capacity == 0U || !EnsureReady()) {
            return 0U;
        }

        ShipwrightProbe::LusShell::File file;
        if (!archives_.LoadFile(hash, &file) || !file.loaded || file.buffer.empty()) {
            return 0U;
        }

        const uint32_t size = (uint32_t)file.buffer.size();
        const uint32_t copy_size = size < capacity ? size : capacity;
        for (uint32_t i = 0; i < copy_size; ++i) {
            output[i] = file.buffer[i];
        }
        return copy_size;
    }

  private:
    ShipwrightProbe::LusShell::ArchiveManager archives_;
    uint32_t game_version_ = 0xFFFFFFFFU;
    bool ready_ = false;
};

ResourceMgrShim& resource_mgr_shim() {
    static ResourceMgrShim shim;
    return shim;
}

} // namespace

extern "C" uint8_t ResourceMgr_FileExists(const char* file_path) {
    return resource_mgr_shim().FileExists(file_path) ? 1U : 0U;
}

extern "C" uint32_t ResourceMgr_GetGameVersion(uint32_t index) {
    (void)index;
    return resource_mgr_shim().GameVersion();
}

extern "C" uint64_t ResourceMgr_GetNameHash(const char* file_path) {
    return ShipwrightProbe::ShipCrc64Name(file_path == nullptr ? "" : file_path);
}

extern "C" const char* ResourceMgr_HashToString(uint64_t hash) {
    return resource_mgr_shim().HashToCString(hash);
}

extern "C" uint32_t ResourceMgr_GetFilePriority(uint64_t hash) {
    return resource_mgr_shim().Priority(hash);
}

extern "C" uint32_t ResourceMgr_LoadFileByName(const char* file_path, uint8_t* output, uint32_t capacity) {
    return resource_mgr_shim().LoadByName(file_path, output, capacity);
}

extern "C" uint32_t ResourceMgr_LoadFileByHash(uint64_t hash, uint8_t* output, uint32_t capacity) {
    return resource_mgr_shim().LoadByHash(hash, output, capacity);
}

OotPs3SohResourceApiProbeResult oot_ps3_soh_resource_api_probe_load(void) {
    OotPs3SohResourceApiProbeResult result = {0, 0xFFFFFFFFU, 0, 0, 0xFFFFFFFFU, 0, 0, 0};
    uint8_t by_name[96] = {0};
    uint8_t by_hash[96] = {0};

    const uint64_t scene_hash = ResourceMgr_GetNameHash(kSceneName);
    result.scene_hash_high = (uint32_t)(scene_hash >> 32U);
    result.scene_hash_low = (uint32_t)scene_hash;

    if (ResourceMgr_FileExists(kSceneName) != 0U) {
        result.flags |= OOT_PS3_SOH_RESOURCE_API_FILE_EXISTS;
    }

    const char* resolved = ResourceMgr_HashToString(scene_hash);
    if (resolved != nullptr && std::string(resolved) == kSceneName) {
        result.flags |= OOT_PS3_SOH_RESOURCE_API_HASH_RESOLVE;
    }

    result.name_size = ResourceMgr_LoadFileByName(kSceneName, by_name, (uint32_t)sizeof(by_name));
    if (result.name_size == (uint32_t)std::string(kExpectedScene).size() &&
        std::string((const char*)by_name, result.name_size) == kExpectedScene) {
        result.flags |= OOT_PS3_SOH_RESOURCE_API_LOAD_BY_NAME;
    }

    result.hash_size = ResourceMgr_LoadFileByHash(scene_hash, by_hash, (uint32_t)sizeof(by_hash));
    if (result.hash_size == result.name_size &&
        std::string((const char*)by_hash, result.hash_size) == kExpectedScene) {
        result.flags |= OOT_PS3_SOH_RESOURCE_API_LOAD_BY_HASH;
    }

    result.game_version = ResourceMgr_GetGameVersion(0U);
    if (result.game_version == 0x78563412U) {
        result.flags |= OOT_PS3_SOH_RESOURCE_API_GAME_VERSION;
    }

    result.priority = ResourceMgr_GetFilePriority(scene_hash);
    if (result.priority == 1U) {
        result.flags |= OOT_PS3_SOH_RESOURCE_API_PRIORITY;
    }

    const OotPs3SohUpstreamHooksProbeResult hooks = oot_ps3_soh_upstream_hooks_probe_load();
    result.hook_flags = hooks.flags;
    if ((hooks.flags & OOT_PS3_SOH_HOOK_ALL) == OOT_PS3_SOH_HOOK_ALL) {
        result.flags |= OOT_PS3_SOH_RESOURCE_API_HOOK_COMPAT;
    }

    return result;
}
