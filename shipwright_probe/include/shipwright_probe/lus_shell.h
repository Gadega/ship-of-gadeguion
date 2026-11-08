// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SHIPWRIGHT_PROBE_LUS_SHELL_H
#define SHIPWRIGHT_PROBE_LUS_SHELL_H

#include <stdint.h>

#include <string>
#include <vector>

#include "shipwright_probe/store_zip_archive.h"

namespace ShipwrightProbe {
namespace LusShell {

struct File {
    bool loaded;
    std::vector<unsigned char> buffer;
};

struct FileIndexEntry {
    uint64_t hash;
    std::string path;
    uint32_t uncompressed_size;
    uint16_t method;
};

class O2rArchive {
  public:
    explicit O2rArchive(const char* path);
    bool Open();
    bool LoadFile(const std::string& file_path, File* file) const;
    bool LoadFile(uint64_t hash, File* file) const;
    bool HasFile(uint64_t hash) const;
    const std::string* HashToString(uint64_t hash) const;
    bool FirstSmallDataHash(uint64_t* hash) const;
    uint32_t FileCount() const;

  private:
    std::string path_;
    StoreZipArchive archive_;
    std::vector<FileIndexEntry> files_;
};

class ArchiveManager {
  public:
    bool AddArchive(const char* path);
    bool LoadFile(const std::string& file_path, File* file) const;
    bool LoadFile(uint64_t hash, File* file) const;
    bool HasFile(uint64_t hash) const;
    const std::string* HashToString(uint64_t hash) const;
    int32_t GetFilePriority(uint64_t hash) const;
    bool FirstSmallDataHash(uint64_t* hash) const;
    uint32_t ArchiveCount() const;
    uint32_t FileCount() const;

  private:
    std::vector<O2rArchive> archives_;
};

uint32_t ReadGameVersion(const File& file);

} // namespace LusShell
} // namespace ShipwrightProbe

#endif
