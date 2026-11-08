// SPDX-License-Identifier: GPL-3.0-or-later
#include "shipwright_probe/ship_hash.h"

namespace ShipwrightProbe {

uint64_t ShipCrc64Name(const char* text) {
    const uint64_t poly = 0x42F0E1EBA9EA3693ULL;
    uint64_t crc = 0xFFFFFFFFFFFFFFFFULL;

    for (const unsigned char* s = (const unsigned char*)text; *s != 0U; ++s) {
        crc ^= (uint64_t)(*s) << 56U;
        for (uint32_t bit = 0; bit < 8U; ++bit) {
            if ((crc & 0x8000000000000000ULL) != 0U) {
                crc = (crc << 1U) ^ poly;
            } else {
                crc <<= 1U;
            }
        }
    }

    return crc;
}

} // namespace ShipwrightProbe
