// SPDX-License-Identifier: GPL-3.0-or-later
#include "shipwright_probe/store_zip_archive.h"

#include "shipwright_probe/resource_archive.h"
#include "shipwright_probe/ship_hash.h"

#include <cstdio>

#define STBI_ONLY_ZLIB
#define STBI_SUPPORT_ZLIB
#include "stb_image.h"

namespace ShipwrightProbe {
namespace {

uint16_t read_le16(const std::vector<unsigned char>& bytes, uint32_t offset) {
    return (uint16_t)bytes[offset] | ((uint16_t)bytes[offset + 1U] << 8U);
}

uint32_t read_le32(const std::vector<unsigned char>& bytes, uint32_t offset) {
    return (uint32_t)bytes[offset] |
           ((uint32_t)bytes[offset + 1U] << 8U) |
           ((uint32_t)bytes[offset + 2U] << 16U) |
           ((uint32_t)bytes[offset + 3U] << 24U);
}

bool range_fits(uint32_t offset, uint32_t size, uint32_t total) {
    return offset <= total && size <= total - offset;
}

} // namespace

bool StoreZipArchive::Open(const char* path) {
    bytes_.clear();
    entries_.clear();

    FILE* file = std::fopen(path, "rb");
    if (file == NULL) {
        return false;
    }

    std::fseek(file, 0, SEEK_END);
    const long file_size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (file_size < 22 || file_size > 0x7FFFFFFF) {
        std::fclose(file);
        return false;
    }

    bytes_.resize((std::vector<unsigned char>::size_type)file_size);
    const size_t read_count = std::fread(bytes_.data(), 1U, bytes_.size(), file);
    std::fclose(file);
    if (read_count != bytes_.size()) {
        bytes_.clear();
        return false;
    }

    const uint32_t total = (uint32_t)bytes_.size();
    uint32_t eocd = 0xFFFFFFFFU;
    const uint32_t min_eocd = total >= 66000U ? total - 66000U : 0U;
    for (uint32_t pos = total - 22U;; --pos) {
        if (read_le32(bytes_, pos) == 0x06054B50U) {
            eocd = pos;
            break;
        }
        if (pos == min_eocd) {
            break;
        }
    }
    if (eocd == 0xFFFFFFFFU) {
        bytes_.clear();
        return false;
    }

    const uint16_t disk_number = read_le16(bytes_, eocd + 4U);
    const uint16_t central_disk = read_le16(bytes_, eocd + 6U);
    const uint16_t entry_count = read_le16(bytes_, eocd + 10U);
    const uint32_t central_size = read_le32(bytes_, eocd + 12U);
    const uint32_t central_offset = read_le32(bytes_, eocd + 16U);
    if (disk_number != 0U || central_disk != 0U ||
        !range_fits(central_offset, central_size, total)) {
        bytes_.clear();
        return false;
    }

    uint32_t cursor = central_offset;
    for (uint16_t i = 0; i < entry_count; ++i) {
        if (!range_fits(cursor, 46U, total) || read_le32(bytes_, cursor) != 0x02014B50U) {
            bytes_.clear();
            entries_.clear();
            return false;
        }

        const uint16_t method = read_le16(bytes_, cursor + 10U);
        const uint32_t crc = read_le32(bytes_, cursor + 16U);
        const uint32_t compressed_size = read_le32(bytes_, cursor + 20U);
        const uint32_t uncompressed_size = read_le32(bytes_, cursor + 24U);
        const uint16_t name_len = read_le16(bytes_, cursor + 28U);
        const uint16_t extra_len = read_le16(bytes_, cursor + 30U);
        const uint16_t comment_len = read_le16(bytes_, cursor + 32U);
        const uint32_t local_offset = read_le32(bytes_, cursor + 42U);
        const uint32_t name_offset = cursor + 46U;
        const uint32_t record_size = 46U + name_len + extra_len + comment_len;
        if (!range_fits(cursor, record_size, total)) {
            bytes_.clear();
            entries_.clear();
            return false;
        }

        StoreZipEntry entry;
        entry.name.assign((const char*)bytes_.data() + name_offset, name_len);
        entry.method = method;
        entry.crc = crc;
        entry.compressed_size = compressed_size;
        entry.uncompressed_size = uncompressed_size;
        entry.local_header_offset = local_offset;
        if (!entry.name.empty() && entry.name[entry.name.size() - 1U] != '/') {
            entries_.push_back(entry);
        }
        cursor += record_size;
    }

    return !entries_.empty();
}

bool StoreZipArchive::Has(const char* name) const {
    return Find(name) != NULL;
}

bool StoreZipArchive::HasHash(uint64_t hash) const {
    return FindHash(hash) != NULL;
}

bool StoreZipArchive::Read(const char* name, std::vector<unsigned char>* output) const {
    const StoreZipEntry* entry = Find(name);
    return entry != NULL && ReadByHash(ShipCrc64Name(entry->name.c_str()), output);
}

bool StoreZipArchive::ReadByHash(uint64_t hash, std::vector<unsigned char>* output) const {
    const StoreZipEntry* entry = FindHash(hash);
    if (entry == NULL || output == NULL) {
        return false;
    }

    const uint32_t total = (uint32_t)bytes_.size();
    const uint32_t local = entry->local_header_offset;
    if (!range_fits(local, 30U, total) || read_le32(bytes_, local) != 0x04034B50U) {
        return false;
    }

    const uint16_t name_len = read_le16(bytes_, local + 26U);
    const uint16_t extra_len = read_le16(bytes_, local + 28U);
    const uint32_t data_offset = local + 30U + name_len + extra_len;
    if (!range_fits(data_offset, entry->compressed_size, total)) {
        return false;
    }

    const unsigned char* first = bytes_.data() + data_offset;
    if (entry->method == 0U) {
        if (entry->compressed_size != entry->uncompressed_size) {
            return false;
        }
        output->assign(first, first + entry->uncompressed_size);
    } else if (entry->method == 8U) {
        output->assign(entry->uncompressed_size, 0U);
        const int written = stbi_zlib_decode_noheader_buffer((char*)output->data(),
                                                             (int)output->size(),
                                                             (const char*)first,
                                                             (int)entry->compressed_size);
        if (written != (int)entry->uncompressed_size) {
            output->clear();
            return false;
        }
    } else {
        return false;
    }
    return Crc32(output->data(), (uint32_t)output->size()) == entry->crc;
}

bool StoreZipArchive::ReadText(const char* name, std::string* output) const {
    std::vector<unsigned char> bytes;
    if (output == NULL || !Read(name, &bytes)) {
        return false;
    }
    output->assign((const char*)bytes.data(), bytes.size());
    return true;
}

bool StoreZipArchive::ReadTextByHash(uint64_t hash, std::string* output) const {
    std::vector<unsigned char> bytes;
    if (output == NULL || !ReadByHash(hash, &bytes)) {
        return false;
    }
    output->assign((const char*)bytes.data(), bytes.size());
    return true;
}

uint32_t StoreZipArchive::EntryCount() const {
    return (uint32_t)entries_.size();
}

uint32_t StoreZipArchive::StoredEntryCount() const {
    uint32_t count = 0;
    for (std::vector<StoreZipEntry>::const_iterator it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->method == 0U) {
            ++count;
        }
    }
    return count;
}

uint32_t StoreZipArchive::DeflatedEntryCount() const {
    uint32_t count = 0;
    for (std::vector<StoreZipEntry>::const_iterator it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->method == 8U) {
            ++count;
        }
    }
    return count;
}

uint32_t StoreZipArchive::UnsupportedEntryCount() const {
    uint32_t count = 0;
    for (std::vector<StoreZipEntry>::const_iterator it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->method != 0U && it->method != 8U) {
            ++count;
        }
    }
    return count;
}

const StoreZipEntry* StoreZipArchive::EntryAt(uint32_t index) const {
    if (index >= entries_.size()) {
        return NULL;
    }
    return &entries_[(std::vector<StoreZipEntry>::size_type)index];
}

const StoreZipEntry* StoreZipArchive::Find(const char* name) const {
    for (std::vector<StoreZipEntry>::const_iterator it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->name == name) {
            return &(*it);
        }
    }
    return NULL;
}

const StoreZipEntry* StoreZipArchive::FindHash(uint64_t hash) const {
    for (std::vector<StoreZipEntry>::const_iterator it = entries_.begin(); it != entries_.end(); ++it) {
        if (ShipCrc64Name(it->name.c_str()) == hash) {
            return &(*it);
        }
    }
    return NULL;
}

} // namespace ShipwrightProbe
