// SPDX-License-Identifier: GPL-3.0-or-later
// Constructor-free replacement for PSL1GHT librt's globfile.o.

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/dirent.h>
#include <sys/file.h>
#include <sys/lv2errno.h>
#include <sys/reent.h>
#include <sys/syslimits.h>

/* PSL1GHT initializes this with strdup("/") in a priority-105 constructor.
 * Static storage provides the same initial state without allocating before
 * main.  Keep the exported pointer because other librt archive members use it.
 */
static char sInitialCwd[] = "/";
char* __cwd = sInitialCwd;

static int CopyJoinedPath(const char* path, char* joined) {
    const size_t pathLength = strlen(path);

    if (path[0] == '/') {
        if (pathLength > PATH_MAX) {
            return -1;
        }
        memcpy(joined, path, pathLength + 1U);
        return 0;
    }

    const size_t cwdLength = strlen(__cwd);
    const int needsSlash = cwdLength > 0U && __cwd[cwdLength - 1U] != '/';
    if (cwdLength + (size_t)needsSlash + pathLength > PATH_MAX) {
        return -1;
    }

    memcpy(joined, __cwd, cwdLength);
    size_t position = cwdLength;
    if (needsSlash) {
        joined[position++] = '/';
    }
    memcpy(joined + position, path, pathLength + 1U);
    return 0;
}

/* Lexically normalize an absolute path.  The PS3 has one process-wide cwd,
 * matching PSL1GHT's implementation; filesystem existence is checked by the
 * caller when this is used for chdir().
 */
char* __full_path(const char* path, char* resolvedPath) {
    char joined[PATH_MAX + 1];
    const char* cursor;
    size_t outputLength = 1U;

    if (path == NULL || resolvedPath == NULL || path[0] == '\0' ||
        CopyJoinedPath(path, joined) != 0) {
        return NULL;
    }

    resolvedPath[0] = '/';
    resolvedPath[1] = '\0';
    cursor = joined;

    while (*cursor != '\0') {
        const char* component;
        size_t componentLength;

        while (*cursor == '/') {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }

        component = cursor;
        while (*cursor != '\0' && *cursor != '/') {
            ++cursor;
        }
        componentLength = (size_t)(cursor - component);

        if (componentLength == 1U && component[0] == '.') {
            continue;
        }
        if (componentLength == 2U && component[0] == '.' && component[1] == '.') {
            if (outputLength > 1U) {
                --outputLength;
                while (outputLength > 1U && resolvedPath[outputLength - 1U] != '/') {
                    --outputLength;
                }
                if (outputLength > 1U) {
                    --outputLength;
                }
                resolvedPath[outputLength] = '\0';
            }
            continue;
        }

        if (outputLength > 1U) {
            if (outputLength == PATH_MAX) {
                return NULL;
            }
            resolvedPath[outputLength++] = '/';
        }
        if (componentLength > PATH_MAX - outputLength) {
            return NULL;
        }
        memcpy(resolvedPath + outputLength, component, componentLength);
        outputLength += componentLength;
        resolvedPath[outputLength] = '\0';
    }

    return resolvedPath;
}

int __librt_chdir_r(struct _reent* reent, const char* dirname) {
    char resolved[PATH_MAX + 1];
    char* replacement;
    s32 descriptor;
    s32 result;

    if (__full_path(dirname, resolved) == NULL) {
        reent->_errno = ENAMETOOLONG;
        return -1;
    }

    result = sysLv2FsOpenDir(resolved, &descriptor);
    if (result != 0) {
        return lv2errno_r(reent, result);
    }
    (void)sysLv2FsCloseDir(descriptor);

    replacement = strdup(resolved);
    if (replacement == NULL) {
        reent->_errno = ENOMEM;
        return -1;
    }
    if (__cwd != sInitialCwd) {
        free(__cwd);
    }
    __cwd = replacement;
    return 0;
}

char* __librt_getcwd_r(struct _reent* reent, char* buffer, size_t size) {
    const size_t length = strlen(__cwd);

    if (buffer == NULL || size <= length) {
        reent->_errno = EINVAL;
        return NULL;
    }
    memcpy(buffer, __cwd, length + 1U);
    return buffer;
}
