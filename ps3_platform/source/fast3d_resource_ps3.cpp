// SPDX-License-Identifier: GPL-3.0-or-later
// Adapt the user's local O2R cache to Shipwright's real Fast3D interpreter.
#include "ps3_platform/fast3d_resource_ps3.h"
#include <unordered_map>
#include "fast/resource/type/Texture.h"
#include "ship/resource/File.h"

#include <cstring>
#include <limits>
#include <vector>

extern "C" {
void* ResourceGetDataByName(const char* path);
void* ResourceGetDataByCrc(uint64_t hash);
const char* ResourceGetNameByCrc(uint64_t hash);
size_t ResourceGetSizeByName(const char* path);
uint16_t ResourceGetTexWidthByName(const char* path);
uint16_t ResourceGetTexHeightByName(const char* path);
uint32_t ResourceGetTexTypeByName(const char* path);
uint32_t ResourceGetTexFlagsByName(const char* path);
}

namespace Ship {
IResource::IResource(std::shared_ptr<ResourceInitData> initData) : mInitData(std::move(initData)) {}
IResource::~IResource() = default;
std::shared_ptr<ResourceInitData> IResource::GetInitData() { return mInitData; }

namespace Math {
float clamp(float value, float low, float high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}
} // namespace Math
} // namespace Ship

namespace Fast {
Texture::Texture() : Resource(std::shared_ptr<Ship::ResourceInitData>()) {}
Texture::~Texture() = default;
uint8_t* Texture::GetPointer() { return ImageData; }
size_t Texture::GetPointerSize() { return ImageDataSize; }
} // namespace Fast

void* Ps3Fast3dResourceManager::GetResourceRawPointer(const char* path) const {
    return path ? ResourceGetDataByName(path) : nullptr;
}

void* Ps3Fast3dResourceManager::GetResourceRawPointer(uint64_t hash) const {
    return ResourceGetDataByCrc(hash);
}

const char* Ps3Fast3dResourceManager::HashToCString(uint64_t hash) const {
    return ResourceGetNameByCrc(hash);
}

int Ps3Fast3dResourceManager::OtrSignatureCheck(const char* path) const {
    const uintptr_t address = reinterpret_cast<uintptr_t>(path);
    if (address <= 0x0FFFFFFFU) return 0;
    return std::memcmp(path, "__OTR__", 7) == 0;
}

std::shared_ptr<Ship::IResource> Ps3Fast3dResourceManager::LoadResourceProcess(
    const std::string& path, bool exact, std::shared_ptr<Ship::ResourceInitData> initData) const {
    (void)exact;
    const char* name = path.c_str();
    // One persistent texture per path. Fast3D keeps raw pointers to ImageData
    // and keys its texture cache by that address; a fresh temporary copy per
    // call was freed immediately, so different textures reused the same
    // address (use-after-free and cache collisions, e.g. the title flame masks).
    static std::unordered_map<std::string, std::shared_ptr<Fast::Texture>> sPs3TextureCache;
    const auto cached = sPs3TextureCache.find(path);
    if (cached != sPs3TextureCache.end()) return cached->second;
    const auto* bytes = static_cast<const uint8_t*>(ResourceGetDataByName(name));
    const size_t size = ResourceGetSizeByName(name);
    const uint16_t width = ResourceGetTexWidthByName(name);
    const uint16_t height = ResourceGetTexHeightByName(name);
    if (!bytes || !size || !width || !height || size > std::numeric_limits<uint32_t>::max()) {
        return nullptr;
    }

    if (!initData) initData = std::make_shared<Ship::ResourceInitData>();
    initData->Path = path;
    auto texture = std::make_shared<Fast::Texture>(initData);
    texture->mImageBuffer = std::make_shared<std::vector<char>>(bytes, bytes + size);
    texture->ImageData = reinterpret_cast<uint8_t*>(texture->mImageBuffer->data());
    texture->ImageDataSize = static_cast<uint32_t>(size);
    texture->Type = static_cast<Fast::TextureType>(ResourceGetTexTypeByName(name));
    texture->Flags = ResourceGetTexFlagsByName(name);
    texture->Width = width;
    texture->Height = height;
    sPs3TextureCache.emplace(path, texture);
    return texture;
}

Ps3Fast3dResourceManager* Ps3ResourceManager() {
    static Ps3Fast3dResourceManager manager;
    return &manager;
}
