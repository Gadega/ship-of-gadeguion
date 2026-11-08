// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SHIPWRIGHT_PROBE_STORE_ARCHIVE_MANAGER_H
#define SHIPWRIGHT_PROBE_STORE_ARCHIVE_MANAGER_H

#include <stdint.h>

#include <string>
#include <vector>

#include "shipwright_probe/store_zip_archive.h"

namespace ShipwrightProbe {

class StoreArchiveManager {
  public:
    bool AddArchive(const char* path);
    bool Has(uint64_t hash) const;
    bool Read(uint64_t hash, std::vector<unsigned char>* output) const;
    bool ReadText(uint64_t hash, std::string* output) const;
    uint32_t ArchiveCount() const;
    uint32_t IndexedCount() const;

  private:
    std::vector<StoreZipArchive> archives_;
};

} // namespace ShipwrightProbe

#endif
