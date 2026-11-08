// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SHIPWRIGHT_PROBE_SHIP_HASH_H
#define SHIPWRIGHT_PROBE_SHIP_HASH_H

#include <stdint.h>

namespace ShipwrightProbe {

uint64_t ShipCrc64Name(const char* text);

} // namespace ShipwrightProbe

#endif
