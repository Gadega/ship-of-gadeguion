// SPDX-License-Identifier: GPL-3.0-or-later
#include "shipwright_probe/user_o2r_probe.h"

#include "shipwright_probe/ship_hash.h"

#include <cstdio>
#include <string>
#include <vector>

#define STBI_ONLY_ZLIB
#define STBI_SUPPORT_ZLIB
#include "stb_image.h"

namespace {

struct CentralEntry {
    std::string name;
    uint16_t method;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint32_t local_header_offset;
};

const char* kUserArchivePaths[] = {
    "/dev_usb000/oot_user.o2r",
    "/dev_usb001/oot_user.o2r",
    "/dev_usb006/oot_user.o2r",
    "/dev_hdd0/tmp/oot_user.o2r",
    "/dev_hdd0/game/OOTP30046/USRDIR/assets/oot_user.o2r",
};

const uint32_t kMaxSampleUncompressed = 64U * 1024U;
const uint32_t kMaxSampleCompressed = 256U * 1024U;

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

bool read_at(FILE* file, long offset, std::vector<unsigned char>* output) {
    if (output == NULL || std::fseek(file, offset, SEEK_SET) != 0) {
        return false;
    }
    return std::fread(output->data(), 1U, output->size(), file) == output->size();
}

uint32_t tiny_text_hash(const std::vector<unsigned char>& bytes) {
    uint32_t hash = 2166136261U;
    for (std::vector<unsigned char>::const_iterator it = bytes.begin(); it != bytes.end(); ++it) {
        hash ^= (uint32_t)(*it);
        hash *= 16777619U;
    }
    return hash;
}

bool read_entry_payload(FILE* file, const CentralEntry& entry, std::vector<unsigned char>* output) {
    if (output == NULL || entry.uncompressed_size == 0U ||
        entry.uncompressed_size > kMaxSampleUncompressed ||
        entry.compressed_size > kMaxSampleCompressed) {
        return false;
    }

    std::vector<unsigned char> local(30U);
    if (!read_at(file, (long)entry.local_header_offset, &local) ||
        read_le32(local, 0U) != 0x04034B50U) {
        return false;
    }

    const uint16_t name_len = read_le16(local, 26U);
    const uint16_t extra_len = read_le16(local, 28U);
    const long data_offset = (long)entry.local_header_offset + 30L + name_len + extra_len;
    std::vector<unsigned char> compressed(entry.compressed_size);
    if (!read_at(file, data_offset, &compressed)) {
        return false;
    }

    if (entry.method == 0U) {
        if (entry.compressed_size != entry.uncompressed_size) {
            return false;
        }
        *output = compressed;
        return true;
    }
    if (entry.method == 8U) {
        output->assign(entry.uncompressed_size, 0U);
        const int written = stbi_zlib_decode_noheader_buffer((char*)output->data(),
                                                             (int)output->size(),
                                                             (const char*)compressed.data(),
                                                             (int)compressed.size());
        if (written != (int)entry.uncompressed_size) {
            output->clear();
            return false;
        }
        return true;
    }
    return false;
}

const CentralEntry* find_entry(const std::vector<CentralEntry>& entries, const char* name) {
    for (std::vector<CentralEntry>::const_iterator it = entries.begin(); it != entries.end(); ++it) {
        if (it->name == name) {
            return &(*it);
        }
    }
    return NULL;
}

const CentralEntry* first_small_sample_entry(const std::vector<CentralEntry>& entries) {
    for (std::vector<CentralEntry>::const_iterator it = entries.begin(); it != entries.end(); ++it) {
        if (it->name != "version" && it->name != "manifest.json" &&
            it->uncompressed_size > 0U &&
            it->uncompressed_size <= kMaxSampleUncompressed &&
            it->compressed_size <= kMaxSampleCompressed &&
            (it->method == 0U || it->method == 8U)) {
            return &(*it);
        }
    }
    return NULL;
}

uint32_t read_version_value(const std::vector<unsigned char>& bytes) {
    if (bytes.size() < 5U) {
        return 0xFFFFFFFFU;
    }
    const uint8_t endian = bytes[0];
    if (endian == 0U) {
        return (uint32_t)bytes[1] |
               ((uint32_t)bytes[2] << 8U) |
               ((uint32_t)bytes[3] << 16U) |
               ((uint32_t)bytes[4] << 24U);
    }
    if (endian == 1U) {
        return ((uint32_t)bytes[1] << 24U) |
               ((uint32_t)bytes[2] << 16U) |
               ((uint32_t)bytes[3] << 8U) |
               (uint32_t)bytes[4];
    }
    return 0xFFFFFFFFU;
}

bool scan_path(const char* path, uint32_t path_index, OotPs3UserO2rProbeResult* result) {
    FILE* file = std::fopen(path, "rb");
    if (file == NULL) {
        return false;
    }

    result->flags |= OOT_PS3_USER_O2R_FOUND;
    result->path_index = path_index;

    std::fseek(file, 0, SEEK_END);
    const long file_size = std::ftell(file);
    if (file_size < 22L) {
        std::fclose(file);
        return true;
    }

    const long tail_size = file_size > 66000L ? 66000L : file_size;
    std::vector<unsigned char> tail((std::vector<unsigned char>::size_type)tail_size);
    if (!read_at(file, file_size - tail_size, &tail)) {
        std::fclose(file);
        return true;
    }

    uint32_t eocd = 0xFFFFFFFFU;
    for (uint32_t pos = (uint32_t)tail.size() - 22U;; --pos) {
        if (read_le32(tail, pos) == 0x06054B50U) {
            eocd = pos;
            break;
        }
        if (pos == 0U) {
            break;
        }
    }
    if (eocd == 0xFFFFFFFFU) {
        std::fclose(file);
        return true;
    }
    result->flags |= OOT_PS3_USER_O2R_EOCD;

    const uint16_t disk_number = read_le16(tail, eocd + 4U);
    const uint16_t central_disk = read_le16(tail, eocd + 6U);
    const uint16_t entry_count = read_le16(tail, eocd + 10U);
    const uint32_t central_size = read_le32(tail, eocd + 12U);
    const uint32_t central_offset = read_le32(tail, eocd + 16U);
    if (disk_number != 0U || central_disk != 0U || entry_count == 0U ||
        central_size == 0U || central_size > 16U * 1024U * 1024U ||
        central_offset > (uint32_t)file_size ||
        central_size > (uint32_t)file_size - central_offset) {
        std::fclose(file);
        return true;
    }

    std::vector<unsigned char> central(central_size);
    if (!read_at(file, (long)central_offset, &central)) {
        std::fclose(file);
        return true;
    }

    uint32_t cursor = 0;
    std::vector<CentralEntry> entries;
    for (uint16_t i = 0; i < entry_count; ++i) {
        if (!range_fits(cursor, 46U, central_size) || read_le32(central, cursor) != 0x02014B50U) {
            std::fclose(file);
            return true;
        }

        const uint16_t method = read_le16(central, cursor + 10U);
        const uint32_t compressed_size = read_le32(central, cursor + 20U);
        const uint32_t uncompressed_size = read_le32(central, cursor + 24U);
        const uint16_t name_len = read_le16(central, cursor + 28U);
        const uint16_t extra_len = read_le16(central, cursor + 30U);
        const uint16_t comment_len = read_le16(central, cursor + 32U);
        const uint32_t local_header_offset = read_le32(central, cursor + 42U);
        const uint32_t name_offset = cursor + 46U;
        const uint32_t record_size = 46U + name_len + extra_len + comment_len;
        if (!range_fits(cursor, record_size, central_size)) {
            std::fclose(file);
            return true;
        }

        std::string name((const char*)central.data() + name_offset, name_len);
        if (!name.empty() && name[name.size() - 1U] == '/') {
            ++result->directory_count;
        } else {
            CentralEntry entry;
            entry.name = name;
            entry.method = method;
            entry.compressed_size = compressed_size;
            entry.uncompressed_size = uncompressed_size;
            entry.local_header_offset = local_header_offset;
            entries.push_back(entry);

            ++result->entry_count;
            if (method == 0U) {
                ++result->stored_count;
            } else if (method == 8U) {
                ++result->deflated_count;
            } else {
                ++result->unsupported_count;
            }
            if (uncompressed_size > result->largest_uncompressed &&
                uncompressed_size != 0xFFFFFFFFU) {
                result->largest_uncompressed = uncompressed_size;
            }
            if (result->entry_count == 1U) {
                const uint64_t hash = ShipwrightProbe::ShipCrc64Name(name.c_str());
                result->first_hash_high = (uint32_t)(hash >> 32U);
                result->first_hash_low = (uint32_t)hash;
            }
        }
        cursor += record_size;
    }

    result->flags |= OOT_PS3_USER_O2R_INDEXED;
    if (result->deflated_count > 0U) {
        result->flags |= OOT_PS3_USER_O2R_HAS_DEFLATE;
    }
    if (result->unsupported_count == 0U && (result->stored_count + result->deflated_count) > 0U) {
        result->flags |= OOT_PS3_USER_O2R_METHODS_OK;
    }

    const CentralEntry* version = find_entry(entries, "version");
    if (version != NULL) {
        result->flags |= OOT_PS3_USER_O2R_HAS_VERSION;
        std::vector<unsigned char> payload;
        if (read_entry_payload(file, *version, &payload)) {
            result->version_value = read_version_value(payload);
            result->flags |= OOT_PS3_USER_O2R_READ_VERSION;
        }
    }

    const CentralEntry* manifest = find_entry(entries, "manifest.json");
    if (manifest != NULL) {
        result->flags |= OOT_PS3_USER_O2R_HAS_MANIFEST;
        std::vector<unsigned char> payload;
        if (read_entry_payload(file, *manifest, &payload)) {
            result->manifest_size = (uint32_t)payload.size();
            result->manifest_name_hash = tiny_text_hash(payload);
            result->flags |= OOT_PS3_USER_O2R_READ_MANIFEST;
        }
    }

    const CentralEntry* sample = first_small_sample_entry(entries);
    if (sample != NULL) {
        std::vector<unsigned char> payload;
        if (read_entry_payload(file, *sample, &payload)) {
            const uint64_t hash = ShipwrightProbe::ShipCrc64Name(sample->name.c_str());
            result->sampled_size = (uint32_t)payload.size();
            result->sampled_hash_high = (uint32_t)(hash >> 32U);
            result->sampled_hash_low = (uint32_t)hash;
            result->flags |= OOT_PS3_USER_O2R_SAMPLED_SMALL;
        }
    }

    std::fclose(file);
    return true;
}

} // namespace

OotPs3UserO2rProbeResult oot_ps3_user_o2r_probe_scan(void) {
    OotPs3UserO2rProbeResult result = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFFFFFFFFU, 0, 0, 0, 0, 0};
    for (uint32_t i = 0; i < (uint32_t)(sizeof(kUserArchivePaths) / sizeof(kUserArchivePaths[0])); ++i) {
        if (scan_path(kUserArchivePaths[i], i, &result)) {
            break;
        }
    }
    return result;
}
