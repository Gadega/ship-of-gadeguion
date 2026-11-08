#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include "ship/resource/Resource.h"

namespace Ship { namespace Math { float clamp(float value, float low, float high); } }

// Small, synchronous resource interface for the native Fast3D interpreter.
// The PS3 build already owns its O2R archive cache in shipwright_resource_ps3.
class Ps3Fast3dResourceManager {
  public:
    void* GetResourceRawPointer(const char* path) const;
    void* GetResourceRawPointer(uint64_t hash) const;
    const char* HashToCString(uint64_t hash) const;
    Ps3Fast3dResourceManager* GetArchiveManager() { return this; }
    int OtrSignatureCheck(const char* path) const;
    std::shared_ptr<Ship::IResource> LoadResourceProcess(
        const std::string& path, bool exact = false,
        std::shared_ptr<Ship::ResourceInitData> initData = nullptr) const;
};

Ps3Fast3dResourceManager* Ps3ResourceManager();
