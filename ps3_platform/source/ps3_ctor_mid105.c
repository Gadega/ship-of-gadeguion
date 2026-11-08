// SPDX-License-Identifier: GPL-3.0-or-later
// Priority-105 marker after the constructor-free lock/glob replacements.

#include <sys/file.h>

static void WriteMarker(const char* path) {
    static const char marker[] =
        "P105F: constructor-free lock and cwd providers active\n";
    s32 descriptor = -1;
    u64 written = 0;

    if (sysLv2FsOpen(path, SYS_O_WRONLY | SYS_O_CREAT | SYS_O_APPEND,
                     &descriptor, 0666, 0, 0) != 0) {
        return;
    }
    (void)sysLv2FsWrite(descriptor, marker, sizeof(marker) - 1U, &written);
    (void)sysLv2FsFsync(descriptor);
    (void)sysLv2FsClose(descriptor);
}

static void __attribute__((constructor(105))) OotPs3TraceBetweenPriority105(void) {
    WriteMarker("/dev_hdd0/tmp/oot_ps3_v48_audio_probe_premain.txt");
    WriteMarker("/dev_hdd0/oot_ps3_v48_audio_probe_premain.txt");
}
