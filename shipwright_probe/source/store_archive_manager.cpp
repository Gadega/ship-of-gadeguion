// SPDX-License-Identifier: GPL-3.0-or-later
#include "shipwright_probe/store_archive_manager.h"

namespace ShipwrightProbe {

bool StoreArchiveManager::AddArchive(const char* path) {
    StoreZipArchive archive;
    if (!archive.Open(path)) {
        return false;
    }
    archives_.push_back(archive);
    return true;
}

bool StoreArchiveManager::Has(uint64_t hash) const {
    for (std::vector<StoreZipArchive>::const_reverse_iterator it = archives_.rbegin();
         it != archives_.rend(); ++it) {
        if (it->HasHash(hash)) {
            return true;
        }
    }
    return false;
}

bool StoreArchiveManager::Read(uint64_t hash, std::vector<unsigned char>* output) const {
    for (std::vector<StoreZipArchive>::const_reverse_iterator it = archives_.rbegin();
         it != archives_.rend(); ++it) {
        if (it->ReadByHash(hash, output)) {
            return true;
        }
    }
    return false;
}

bool StoreArchiveManager::ReadText(uint64_t hash, std::string* output) const {
    std::vector<unsigned char> bytes;
    if (output == NULL || !Read(hash, &bytes)) {
        return false;
    }
    output->assign((const char*)bytes.data(), bytes.size());
    return true;
}

uint32_t StoreArchiveManager::ArchiveCount() const {
    return (uint32_t)archives_.size();
}

uint32_t StoreArchiveManager::IndexedCount() const {
    uint32_t count = 0;
    for (std::vector<StoreZipArchive>::const_iterator it = archives_.begin(); it != archives_.end(); ++it) {
        count += it->EntryCount();
    }
    return count;
}

} // namespace ShipwrightProbe
