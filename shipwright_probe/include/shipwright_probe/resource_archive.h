// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SHIPWRIGHT_PROBE_RESOURCE_ARCHIVE_H
#define SHIPWRIGHT_PROBE_RESOURCE_ARCHIVE_H

#include <stdint.h>

#include <string>
#include <vector>

namespace ShipwrightProbe {

struct ResourceEntry {
    std::string name;
    uint32_t offset;
    uint32_t size;
    uint32_t crc;
};

class ResourceArchive {
  public:
    bool Open(const char* path);
    bool Has(const char* name) const;
    bool Read(const char* name, std::vector<unsigned char>* output) const;
    bool ReadText(const char* name, std::string* output) const;
    uint32_t EntryCount() const;

  private:
    const ResourceEntry* Find(const char* name) const;

    std::vector<ResourceEntry> entries_;
    std::vector<unsigned char> payload_;
};

uint32_t Crc32(const unsigned char* data, uint32_t size);

} // namespace ShipwrightProbe

#endif
