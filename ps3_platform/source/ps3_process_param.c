// SPDX-License-Identifier: GPL-3.0-or-later
// Keep PSL1GHT process metadata isolated from Shipwright headers.  The SDK
// ppu-types.h aliases collide with the decompiled game's u64/u32 typedefs.
#include <sys/process.h>

SYS_PROCESS_PARAM(1001, 0x100000);
