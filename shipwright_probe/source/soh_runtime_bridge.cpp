// SPDX-License-Identifier: GPL-3.0-or-later
#include "shipwright_probe/soh_runtime_bridge.h"

#include "shipwright_probe/lus_shell.h"
#include "shipwright_probe/resource_bootstrap_probe.h"
#include "shipwright_probe/ship_hash.h"

#include <string>

namespace {

const char* kBasePath = "/dev_hdd0/game/OOTP30046/USRDIR/assets/synthetic_deflated.o2r";
const char* kOverridePath = "/dev_hdd0/game/OOTP30046/USRDIR/assets/synthetic_override.o2r";
const char* kStartupScene = "scenes/test_scene/room_0/display_list.bin";
const char* kExpectedScene = "override-scene-from-second-archive\n";

enum BridgePhase {
    BRIDGE_PHASE_EMPTY = 0U,
    BRIDGE_PHASE_INIT_OTR = 1U,
    BRIDGE_PHASE_HEAP_READY = 2U,
    BRIDGE_PHASE_MAIN_READY = 3U,
    BRIDGE_PHASE_FIRST_SCENE = 4U,
    BRIDGE_PHASE_FRAME_TICKS = 5U,
    BRIDGE_PHASE_SHUTDOWN_READY = 6U,
};

class SohRuntimeBridge {
  public:
    bool InitOtrLike(OotPs3SohRuntimeBridgeResult* result) {
        if (result == nullptr) {
            return false;
        }

        const OotPs3ResourceBootstrapProbeResult resources = oot_ps3_resource_bootstrap_probe_load();
        result->resource_flags = resources.flags;
        result->game_version = resources.version_value;
        result->scene_hash_high = resources.scene_hash_high;
        result->scene_hash_low = resources.scene_hash_low;

        if ((resources.flags & OOT_PS3_RESOURCE_BOOT_ALL) != OOT_PS3_RESOURCE_BOOT_ALL) {
            return false;
        }
        if (!archive_manager_.AddArchive(kBasePath) || !archive_manager_.AddArchive(kOverridePath)) {
            return false;
        }

        result->phase = BRIDGE_PHASE_INIT_OTR;
        result->flags |= OOT_PS3_SOH_BRIDGE_INIT_OTR;
        return true;
    }

    bool PrepareHeap(OotPs3SohRuntimeBridgeResult* result) {
        if (result == nullptr || (result->flags & OOT_PS3_SOH_BRIDGE_INIT_OTR) == 0U) {
            return false;
        }

        scratch_heap_marker_ = result->game_version ^ result->resource_flags ^ 0x534F4830U;
        if (scratch_heap_marker_ == 0U) {
            return false;
        }

        result->phase = BRIDGE_PHASE_HEAP_READY;
        result->flags |= OOT_PS3_SOH_BRIDGE_HEAP_READY;
        return true;
    }

    bool MainReady(OotPs3SohRuntimeBridgeResult* result) {
        if (result == nullptr || (result->flags & OOT_PS3_SOH_BRIDGE_HEAP_READY) == 0U) {
            return false;
        }

        const uint64_t scene_hash = ShipwrightProbe::ShipCrc64Name(kStartupScene);
        const std::string* resolved = archive_manager_.HashToString(scene_hash);
        if (resolved == nullptr || *resolved != kStartupScene) {
            return false;
        }

        result->phase = BRIDGE_PHASE_MAIN_READY;
        result->flags |= OOT_PS3_SOH_BRIDGE_MAIN_READY;
        return true;
    }

    bool LoadFirstScene(OotPs3SohRuntimeBridgeResult* result) {
        if (result == nullptr || (result->flags & OOT_PS3_SOH_BRIDGE_MAIN_READY) == 0U) {
            return false;
        }

        ShipwrightProbe::LusShell::File scene;
        const uint64_t scene_hash = ShipwrightProbe::ShipCrc64Name(kStartupScene);
        if (!archive_manager_.LoadFile(scene_hash, &scene) || !scene.loaded || scene.buffer.empty()) {
            return false;
        }
        const std::string scene_text((const char*)scene.buffer.data(), scene.buffer.size());
        if (scene_text != kExpectedScene || archive_manager_.GetFilePriority(scene_hash) != 1) {
            return false;
        }

        result->scene_size = (uint32_t)scene.buffer.size();
        result->phase = BRIDGE_PHASE_FIRST_SCENE;
        result->flags |= OOT_PS3_SOH_BRIDGE_FIRST_SCENE;
        return true;
    }

    bool TickFrames(OotPs3SohRuntimeBridgeResult* result, uint32_t tick_count) {
        if (result == nullptr || (result->flags & OOT_PS3_SOH_BRIDGE_FIRST_SCENE) == 0U || tick_count == 0U) {
            return false;
        }

        for (uint32_t i = 0; i < tick_count; ++i) {
            ++result->tick_count;
            result->frame_accumulator =
                (result->frame_accumulator << 5U) ^
                (result->frame_accumulator >> 2U) ^
                (scratch_heap_marker_ + result->tick_count + result->scene_size);
        }

        if (result->tick_count == tick_count && result->frame_accumulator != 0U) {
            result->phase = BRIDGE_PHASE_FRAME_TICKS;
            result->flags |= OOT_PS3_SOH_BRIDGE_FRAME_TICKS;
            return true;
        }
        return false;
    }

    bool ShutdownReady(OotPs3SohRuntimeBridgeResult* result) {
        if (result == nullptr || (result->flags & OOT_PS3_SOH_BRIDGE_FRAME_TICKS) == 0U) {
            return false;
        }

        result->phase = BRIDGE_PHASE_SHUTDOWN_READY;
        result->flags |= OOT_PS3_SOH_BRIDGE_SHUTDOWN_READY;
        return true;
    }

  private:
    ShipwrightProbe::LusShell::ArchiveManager archive_manager_;
    uint32_t scratch_heap_marker_ = 0U;
};

} // namespace

OotPs3SohRuntimeBridgeResult oot_ps3_soh_runtime_bridge_probe_load(void) {
    OotPs3SohRuntimeBridgeResult result = {0, BRIDGE_PHASE_EMPTY, 0, 0, 0, 0xFFFFFFFFU, 0, 0, 0};
    SohRuntimeBridge bridge;

    if (!bridge.InitOtrLike(&result)) {
        return result;
    }
    if (!bridge.PrepareHeap(&result)) {
        return result;
    }
    if (!bridge.MainReady(&result)) {
        return result;
    }
    if (!bridge.LoadFirstScene(&result)) {
        return result;
    }
    if (!bridge.TickFrames(&result, 5U)) {
        return result;
    }
    bridge.ShutdownReady(&result);

    return result;
}
