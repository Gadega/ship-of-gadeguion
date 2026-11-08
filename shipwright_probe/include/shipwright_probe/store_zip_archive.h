// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SHIPWRIGHT_PROBE_STORE_ZIP_ARCHIVE_H
#define SHIPWRIGHT_PROBE_STORE_ZIP_ARCHIVE_H

#include <stdint.h>

#include <string>
#include <vector>

namespace ShipwrightProbe {

struct StoreZipEntry {
    std::string name;
    uint16_t method;
    uint32_t crc;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint32_t local_header_offset;
};

class StoreZipArchive {
  public:
    bool Open(const char* path);
    bool Has(const char* name) const;
    bool HasHash(uint64_t hash) const;
    bool Read(const char* name, std::vector<unsigned char>* output) const;
    bool ReadByHash(uint64_t hash, std::vector<unsigned char>* output) const;
    bool ReadText(const char* name, std::string* output) const;
    bool ReadTextByHash(uint64_t hash, std::string* output) const;
    uint32_t EntryCount() const;
    uint32_t StoredEntryCount() const;
    uint32_t DeflatedEntryCount() const;
    uint32_t UnsupportedEntryCount() const;
    const StoreZipEntry* EntryAt(uint32_t index) const;

  private:
    const StoreZipEntry* Find(const char* name) const;
    const StoreZipEntry* FindHash(uint64_t hash) const;

    std::vector<unsigned char> bytes_;
    std::vector<StoreZipEntry> entries_;
};

} // namespace ShipwrightProbe

#endif
