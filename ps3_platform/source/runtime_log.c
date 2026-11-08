// SPDX-License-Identifier: GPL-3.0-or-later
#include "ps3_platform/runtime_log.h"

#include <ppu-types.h>
#include <stdarg.h>
#include <string.h>

static const char* const LOG_PATHS[] = {
    "/dev_hdd0/tmp/oot_ps3_runtime.log",
    "/dev_hdd0/game/OOTP30046/USRDIR/oot_ps3_runtime.log",
    "oot_ps3_runtime.log",
};

bool oot_ps3_log_open(OotPs3RuntimeLog* log) {
    memset(log, 0, sizeof(*log));

    for (u32 i = 0; i < sizeof(LOG_PATHS) / sizeof(LOG_PATHS[0]); ++i) {
        log->file = fopen(LOG_PATHS[i], "w");
        if (log->file != NULL) {
            log->path = LOG_PATHS[i];
            oot_ps3_log_printf(log, "oot ps3 runtime log opened at %s\n", log->path);
            return true;
        }
    }
    return false;
}

void oot_ps3_log_printf(OotPs3RuntimeLog* log, const char* format, ...) {
    if (log->file == NULL) {
        return;
    }

    va_list args;
    va_start(args, format);
    vfprintf(log->file, format, args);
    va_end(args);
    fflush(log->file);
}

void oot_ps3_log_close(OotPs3RuntimeLog* log) {
    if (log->file != NULL) {
        oot_ps3_log_printf(log, "oot ps3 runtime log closed\n");
        fclose(log->file);
        log->file = NULL;
    }
    log->path = NULL;
}
