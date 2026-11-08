// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OOT_PS3_RUNTIME_LOG_H
#define OOT_PS3_RUNTIME_LOG_H

#include <stdbool.h>
#include <stdio.h>

typedef struct {
    FILE* file;
    const char* path;
} OotPs3RuntimeLog;

bool oot_ps3_log_open(OotPs3RuntimeLog* log);
void oot_ps3_log_printf(OotPs3RuntimeLog* log, const char* format, ...);
void oot_ps3_log_close(OotPs3RuntimeLog* log);

#endif
