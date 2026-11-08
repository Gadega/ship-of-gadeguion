// SPDX-License-Identifier: GPL-3.0-or-later
#include "shipwright_probe/lus_shell.h"

#include "shipwright_probe/ship_hash.h"

namespace ShipwrightProbe {
namespace LusShell {

O2rArchive::O2rArchive(const char* path) : path_(path == NULL ? "" : path) {
}

bool O2rArchive::Open() {
    files_.clear();
    if (!archive_.Open(path_.c_str())) {
        return false;
    }
    for (uint32_t i = 0; i < archive_.EntryCount(); ++i) {
        const StoreZipEntry* entry = archive_.EntryAt(i);
        if (entry == NULL) {
            continue;
        }
        FileIndexEntry indexed;
        indexed.hash = ShipCrc64Name(entry->name.c_str());
        indexed.path = entry->name;
        indexed.uncompressed_size = entry->uncompressed_size;
        indexed.method = entry->method;
        files_.push_back(indexed);
    }
    return !files_.empty();
}

bool O2rArchive::LoadFile(const std::string& file_path, File* file) const {
    if (file == NULL) {
        return false;
    }
    file->loaded = archive_.Read(file_path.c_str(), &file->buffer);
    return file->loaded;
}

bool O2rArchive::LoadFile(uint64_t hash, File* file) const {
    if (file == NULL) {
        return false;
    }
    file->loaded = archive_.ReadByHash(hash, &file->buffer);
    return file->loaded;
}

bool O2rArchive::HasFile(uint64_t hash) const {
    return HashToString(hash) != NULL;
}

const std::string* O2rArchive::HashToString(uint64_t hash) const {
    for (std::vector<FileIndexEntry>::const_iterator it = files_.begin(); it != files_.end(); ++it) {
        if (it->hash == hash) {
            return &it->path;
        }
    }
    return NULL;
}

bool O2rArchive::FirstSmallDataHash(uint64_t* hash) const {
    if (hash == NULL) {
        return false;
    }
    for (std::vector<FileIndexEntry>::const_iterator it = files_.begin(); it != files_.end(); ++it) {
        if (it->path != "version" && it->path != "manifest.json" &&
            it->uncompressed_size > 0U &&
            it->uncompressed_size <= 64U * 1024U &&
            (it->method == 0U || it->method == 8U)) {
            *hash = it->hash;
            return true;
        }
    }
    return false;
}

uint32_t O2rArchive::FileCount() const {
    return (uint32_t)files_.size();
}

bool ArchiveManager::AddArchive(const char* path) {
    O2rArchive archive(path);
    if (!archive.Open()) {
        return false;
    }
    archives_.push_back(archive);
    return true;
}

bool ArchiveManager::LoadFile(const std::string& file_path, File* file) const {
    return LoadFile(ShipCrc64Name(file_path.c_str()), file);
}

bool ArchiveManager::LoadFile(uint64_t hash, File* file) const {
    for (std::vector<O2rArchive>::const_reverse_iterator it = archives_.rbegin();
         it != archives_.rend(); ++it) {
        if (it->LoadFile(hash, file)) {
            return true;
        }
    }
    if (file != NULL) {
        file->loaded = false;
        file->buffer.clear();
    }
    return false;
}

bool ArchiveManager::HasFile(uint64_t hash) const {
    for (std::vector<O2rArchive>::const_reverse_iterator it = archives_.rbegin();
         it != archives_.rend(); ++it) {
        if (it->HasFile(hash)) {
            return true;
        }
    }
    return false;
}

const std::string* ArchiveManager::HashToString(uint64_t hash) const {
    for (std::vector<O2rArchive>::const_reverse_iterator it = archives_.rbegin();
         it != archives_.rend(); ++it) {
        const std::string* path = it->HashToString(hash);
        if (path != NULL) {
            return path;
        }
    }
    return NULL;
}

int32_t ArchiveManager::GetFilePriority(uint64_t hash) const {
    int32_t priority = (int32_t)archives_.size() - 1;
    for (std::vector<O2rArchive>::const_reverse_iterator it = archives_.rbegin();
         it != archives_.rend(); ++it, --priority) {
        if (it->HasFile(hash)) {
            return priority;
        }
    }
    return -1;
}

bool ArchiveManager::FirstSmallDataHash(uint64_t* hash) const {
    for (std::vector<O2rArchive>::const_reverse_iterator it = archives_.rbegin();
         it != archives_.rend(); ++it) {
        if (it->FirstSmallDataHash(hash)) {
            return true;
        }
    }
    return false;
}

uint32_t ArchiveManager::ArchiveCount() const {
    return (uint32_t)archives_.size();
}

uint32_t ArchiveManager::FileCount() const {
    uint32_t count = 0;
    for (std::vector<O2rArchive>::const_iterator it = archives_.begin(); it != archives_.end(); ++it) {
        count += it->FileCount();
    }
    return count;
}

uint32_t ReadGameVersion(const File& file) {
    if (!file.loaded || file.buffer.size() < 5U) {
        return 0xFFFFFFFFU;
    }
    const uint8_t endian = file.buffer[0];
    if (endian == 0U) {
        return (uint32_t)file.buffer[1] |
               ((uint32_t)file.buffer[2] << 8U) |
               ((uint32_t)file.buffer[3] << 16U) |
               ((uint32_t)file.buffer[4] << 24U);
    }
    if (endian == 1U) {
        return ((uint32_t)file.buffer[1] << 24U) |
               ((uint32_t)file.buffer[2] << 16U) |
               ((uint32_t)file.buffer[3] << 8U) |
               (uint32_t)file.buffer[4];
    }
    return 0xFFFFFFFFU;
}

} // namespace LusShell
} // namespace ShipwrightProbe
