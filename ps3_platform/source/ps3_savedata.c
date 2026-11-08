/* PS3 system save data (the XMB "Saved Data Utility") for the three OoT files.
 *
 * Each file slot is its own save directory, OOTP30048-FILE1..3, written and
 * read with the silent "auto" save/load calls (no system dialogs). A directory
 * holds PARAM.SFO (title/subtitle/detail, written by the system from the
 * strings below), ICON0.PNG (the game's icon) and SAVE.DAT (the bytes the game
 * passes in). PS3 system headers only: they clash with the game headers. */

#include <sys/memory.h>
#include <sysutil/save.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void oot_ps3_early_trace(const char* message);

#define PS3_SAVE_DIR_PREFIX "OOTP30048-FILE"
#define PS3_SAVE_DATA_FILE "SAVE.DAT"
#define PS3_SAVE_ICON_PATH "/dev_hdd0/game/OOTP30048/ICON0.PNG"
#define PS3_SAVE_MAX_FILES 4

typedef struct {
    int saving;
    int stage;          /* 0 icon, 1 data, 2 done */
    int found;
    const void* writeData;
    unsigned writeSize;
    void* readBuffer;
    unsigned readCapacity;
    unsigned readSize;  /* SAVE.DAT size reported by the status callback */
    unsigned readDone;  /* bytes actually read */
    const char* subtitle;
    const char* detail;
    char listParameter[SYS_SAVE_MAX_LIST_PARAMETER];
} Ps3SaveJob;

static Ps3SaveJob sJob;
static void* sIcon;
static unsigned sIconSize;
static int sIconLoaded;
static char sFileName[SYS_SAVE_MAX_FILE_NAME] = PS3_SAVE_DATA_FILE;
static unsigned char sListBuffer[PS3_SAVE_MAX_FILES * sizeof(sysSaveFileStatus) + 1024];

static void LoadIcon(void) {
    FILE* file;
    long size;
    if (sIconLoaded) return;
    sIconLoaded = 1;
    file = fopen(PS3_SAVE_ICON_PATH, "rb");
    if (file == NULL) return;
    if (fseek(file, 0, SEEK_END) == 0 && (size = ftell(file)) > 0 && size < 1024 * 1024 &&
        fseek(file, 0, SEEK_SET) == 0) {
        sIcon = malloc((size_t)size);
        if (sIcon != NULL && fread(sIcon, 1, (size_t)size, file) == (size_t)size) {
            sIconSize = (unsigned)size;
        } else {
            free(sIcon);
            sIcon = NULL;
        }
    }
    fclose(file);
}

static void StatusCallback(sysSaveCallbackResult* result, sysSaveStatusIn* in, sysSaveStatusOut* out) {
    unsigned i;
    result->result = SYS_SAVE_CALLBACK_RESULT_CONTINUE;
    out->setParam = &in->getParam;
    out->recreateMode = SYS_SAVE_RECREATE_MODE_OVERWRITE_NOT_CORRUPTED;
    if (!sJob.saving) {
        sJob.found = 0;
        if (in->isNew) {
            result->result = SYS_SAVE_CALLBACK_RESULT_NOT_FOUND;
            return;
        }
        for (i = 0; i < in->numFiles; ++i) {
            if (in->fileList[i].fileType == SYS_SAVE_FILETYPE_STANDARD_FILE &&
                strcmp(in->fileList[i].filename, PS3_SAVE_DATA_FILE) == 0) {
                sJob.readSize = (unsigned)in->fileList[i].fileSize;
                sJob.found = 1;
            }
        }
        if (!sJob.found || sJob.readSize > sJob.readCapacity) {
            result->result = sJob.found ? SYS_SAVE_CALLBACK_RESULT_CORRUPTED : SYS_SAVE_CALLBACK_RESULT_NOT_FOUND;
            return;
        }
        sJob.stage = 1;
        return;
    }
    {
        /* Room for SAVE.DAT, the icon and the system file, in KB. Overwriting
         * frees what the directory already uses. */
        const int neededKB = (int)((sJob.writeSize + sIconSize) / 1024U) + 2 + in->systemSizeKB;
        if (in->freeSpaceKB + in->sizeKB < neededKB) {
            result->result = SYS_SAVE_CALLBACK_RESULT_NO_SPACE_LEFT;
            result->missingSpaceKB = neededKB - (in->freeSpaceKB + in->sizeKB);
            return;
        }
    }
    strncpy(in->getParam.title, "Ship of Gadeguion", SYS_SAVE_MAX_TITLE - 1);
    in->getParam.title[SYS_SAVE_MAX_TITLE - 1] = '\0';
    strncpy(in->getParam.subtitle, sJob.subtitle != NULL ? sJob.subtitle : "", SYS_SAVE_MAX_SUBTITLE - 1);
    in->getParam.subtitle[SYS_SAVE_MAX_SUBTITLE - 1] = '\0';
    strncpy(in->getParam.detail, sJob.detail != NULL ? sJob.detail : "", SYS_SAVE_MAX_DETAIL - 1);
    in->getParam.detail[SYS_SAVE_MAX_DETAIL - 1] = '\0';
    memcpy(in->getParam.listParameter, sJob.listParameter, SYS_SAVE_MAX_LIST_PARAMETER);
    sJob.stage = sIconSize > 0 ? 0 : 1;
}

static void FileCallback(sysSaveCallbackResult* result, sysSaveFileIn* in, sysSaveFileOut* out) {
    memset(out, 0, sizeof(*out));
    result->result = SYS_SAVE_CALLBACK_RESULT_CONTINUE;
    switch (sJob.stage) {
        case 0:
            out->fileOperation = SYS_SAVE_FILE_OPERATION_WRITE;
            out->fileType = SYS_SAVE_FILETYPE_CONTENT_ICON0;
            out->size = sIconSize;
            out->bufferSize = sIconSize;
            out->buffer = sIcon;
            result->incrementProgress = 30;
            sJob.stage = 1;
            break;
        case 1:
            out->filename = sFileName;
            out->fileType = SYS_SAVE_FILETYPE_STANDARD_FILE;
            if (sJob.saving) {
                out->fileOperation = SYS_SAVE_FILE_OPERATION_WRITE;
                out->size = sJob.writeSize;
                out->bufferSize = sJob.writeSize;
                out->buffer = (void*)sJob.writeData;
            } else {
                out->fileOperation = SYS_SAVE_FILE_OPERATION_READ;
                out->size = sJob.readSize;
                out->bufferSize = sJob.readCapacity;
                out->buffer = sJob.readBuffer;
            }
            result->incrementProgress = 100;
            sJob.stage = 2;
            break;
        default:
            if (!sJob.saving) sJob.readDone = in->previousOperationResultSize;
            result->result = SYS_SAVE_CALLBACK_RESULT_DONE;
            break;
    }
}

/* The save utility needs a memory container for its work buffers. */
static int CreateContainer(sys_mem_container_t* container) {
    if (sysMemContainerCreate(container, 5U * 1024U * 1024U) == 0) return 1;
    if (sysMemContainerCreate(container, 3U * 1024U * 1024U) == 0) return 1;
    return 0;
}

static int Run(int slot) {
    char directory[SYS_SAVE_MAX_DIRECTORY_NAME];
    char line[160];
    sysSaveBufferSettings buffers;
    sys_mem_container_t container;
    s32 ret;

    snprintf(directory, sizeof(directory), PS3_SAVE_DIR_PREFIX "%d", slot + 1);
    snprintf(sJob.listParameter, sizeof(sJob.listParameter), "FILE%d", slot + 1);
    memset(&buffers, 0, sizeof(buffers));
    buffers.maxDirectories = 1;
    buffers.maxFiles = PS3_SAVE_MAX_FILES;
    buffers.bufferSize = sizeof(sListBuffer);
    buffers.buffer = sListBuffer;
    if (!CreateContainer(&container)) {
        oot_ps3_early_trace("trace: savedata memory container FAILED");
        return -1;
    }
    snprintf(line, sizeof(line), "savedata: %s %s begin", sJob.saving ? "save" : "load", directory);
    oot_ps3_early_trace(line);
    if (sJob.saving) {
        ret = sysSaveAutoSave2(SYS_SAVE_CURRENT_VERSION, directory, SYS_SAVE_ERROR_DIALOG_NONE, &buffers,
                               StatusCallback, FileCallback, container, NULL);
    } else {
        ret = sysSaveAutoLoad2(SYS_SAVE_CURRENT_VERSION, directory, SYS_SAVE_ERROR_DIALOG_NONE, &buffers,
                               StatusCallback, FileCallback, container, NULL);
    }
    sysMemContainerDestroy(container);
    snprintf(line, sizeof(line), "trace: savedata %s %s ret=0x%08x found=%d size=%u", sJob.saving ? "save" : "load",
             directory, (unsigned)ret, sJob.found, sJob.saving ? sJob.writeSize : sJob.readDone);
    oot_ps3_early_trace(line);
    return ret;
}

/* Writes `size` bytes as file slot `slot` (0..2). Returns 1 on success. */
int oot_ps3_savedata_write(int slot, const void* data, unsigned size, const char* subtitle, const char* detail) {
    if (slot < 0 || slot > 2 || data == NULL || size == 0) return 0;
    LoadIcon();
    memset(&sJob, 0, sizeof(sJob));
    sJob.saving = 1;
    sJob.writeData = data;
    sJob.writeSize = size;
    sJob.subtitle = subtitle;
    sJob.detail = detail;
    return Run(slot) == SYS_SAVE_RETURN_DONE;
}

/* Reads file slot `slot` into `buffer`. Returns 1 and the size read, 0 when the
 * slot has no save directory, -1 on an error (corrupted, too large, ...). */
int oot_ps3_savedata_read(int slot, void* buffer, unsigned capacity, unsigned* size) {
    s32 ret;
    if (size != NULL) *size = 0;
    if (slot < 0 || slot > 2 || buffer == NULL || capacity == 0) return -1;
    memset(&sJob, 0, sizeof(sJob));
    sJob.readBuffer = buffer;
    sJob.readCapacity = capacity;
    ret = Run(slot);
    if (ret == SYS_SAVE_RETURN_DONE && sJob.found && sJob.readDone == sJob.readSize) {
        if (size != NULL) *size = sJob.readDone;
        return 1;
    }
    return sJob.found ? -1 : 0;
}
