// SPDX-License-Identifier: GPL-3.0-or-later
#include "shipwright_probe/resource_archive.h"

#include <cstdio>
#include <cstdlib>

namespace ShipwrightProbe {
namespace {

bool parse_u32(const std::string& text, uint32_t* value) {
    char* end = NULL;
    const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
    if (end == NULL || *end != '\0' || parsed > 0xFFFFFFFFUL) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

bool parse_entry(const std::string& line, ResourceEntry* entry) {
    const std::string prefix = "entry=";
    if (line.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }

    const std::string body = line.substr(prefix.size());
    const std::string::size_type p1 = body.find('|');
    const std::string::size_type p2 = p1 == std::string::npos ? p1 : body.find('|', p1 + 1U);
    const std::string::size_type p3 = p2 == std::string::npos ? p2 : body.find('|', p2 + 1U);
    if (p1 == std::string::npos || p2 == std::string::npos || p3 == std::string::npos) {
        return false;
    }

    entry->name = body.substr(0U, p1);
    return parse_u32(body.substr(p1 + 1U, p2 - p1 - 1U), &entry->offset) &&
           parse_u32(body.substr(p2 + 1U, p3 - p2 - 1U), &entry->size) &&
           parse_u32(body.substr(p3 + 1U), &entry->crc);
}

bool parse_entries(const std::string& header, std::vector<ResourceEntry>* entries) {
    std::string::size_type start = 0;
    while (start < header.size()) {
        const std::string::size_type end = header.find('\n', start);
        const std::string line = header.substr(start, end == std::string::npos ? end : end - start);
        ResourceEntry entry;
        if (parse_entry(line, &entry)) {
            entries->push_back(entry);
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1U;
    }
    return !entries->empty();
}

} // namespace

uint32_t Crc32(const unsigned char* data, uint32_t size) {
    uint32_t crc = ~0U;
    for (uint32_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0; bit < 8U; ++bit) {
            const uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

bool ResourceArchive::Open(const char* path) {
    entries_.clear();
    payload_.clear();

    FILE* file = std::fopen(path, "rb");
    if (file == NULL) {
        return false;
    }

    std::fseek(file, 0, SEEK_END);
    const long file_size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (file_size <= 0) {
        std::fclose(file);
        return false;
    }

    std::vector<unsigned char> bytes((std::vector<unsigned char>::size_type)file_size);
    const size_t read_count = std::fread(bytes.data(), 1U, bytes.size(), file);
    std::fclose(file);
    if (read_count != bytes.size()) {
        return false;
    }

    const unsigned char* begin = bytes.data();
    const unsigned char* end = bytes.data() + bytes.size();
    const unsigned char* header_end = NULL;
    for (const unsigned char* cursor = begin; cursor + 1 < end; ++cursor) {
        if (cursor[0] == '\n' && cursor[1] == '\n') {
            header_end = cursor;
            break;
        }
    }
    if (header_end == NULL) {
        return false;
    }

    const std::string header((const char*)begin, (std::string::size_type)(header_end - begin));
    if (header.compare(0, 6U, "OPS3M1") != 0 || !parse_entries(header, &entries_)) {
        entries_.clear();
        return false;
    }

    const unsigned char* payload_begin = header_end + 2;
    payload_.assign(payload_begin, end);
    return true;
}

bool ResourceArchive::Has(const char* name) const {
    return Find(name) != NULL;
}

bool ResourceArchive::Read(const char* name, std::vector<unsigned char>* output) const {
    const ResourceEntry* entry = Find(name);
    if (entry == NULL || output == NULL) {
        return false;
    }
    if (entry->offset > payload_.size() || entry->size > payload_.size() - entry->offset) {
        return false;
    }

    const unsigned char* first = payload_.data() + entry->offset;
    output->assign(first, first + entry->size);
    return Crc32(output->data(), (uint32_t)output->size()) == entry->crc;
}

bool ResourceArchive::ReadText(const char* name, std::string* output) const {
    std::vector<unsigned char> bytes;
    if (output == NULL || !Read(name, &bytes)) {
        return false;
    }
    output->assign((const char*)bytes.data(), bytes.size());
    return true;
}

uint32_t ResourceArchive::EntryCount() const {
    return (uint32_t)entries_.size();
}

const ResourceEntry* ResourceArchive::Find(const char* name) const {
    for (std::vector<ResourceEntry>::const_iterator it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->name == name) {
            return &(*it);
        }
    }
    return NULL;
}

} // namespace ShipwrightProbe
