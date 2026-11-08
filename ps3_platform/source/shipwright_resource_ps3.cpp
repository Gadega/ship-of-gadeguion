// SPDX-License-Identifier: GPL-3.0-or-later
// Synchronous, streaming O2R resource bridge for memory-constrained PS3 builds.
#include "global.h"
#include "message_data_static.h"

#include "GameVersions.h"
#include "ResourceManagerHelpers.h"
#include "fast/resource/ResourceType.h"
#include "fast/lus_gbi.h"
#include "ship/resource/ResourceType.h"
#include "soh/resource/type/SohResourceType.h"

#include "shipwright_probe/ship_hash.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <new>
#include <string>
#include <sys/types.h>
#include <unistd.h>
#include <deque>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#define STBI_ONLY_ZLIB
#define STBI_SUPPORT_ZLIB
#include "stb_image.h"

extern "C" void oot_ps3_early_trace(const char* message);
extern "C" void* ResourceGetDataByName(const char* name);
extern "C" unsigned long long oot_ps3_microseconds_now(void);
extern "C" { int gOotPs3ResourceTimingEnabled = 0; }

// ---- PS3 title cutscene converter: mirrors SOH CutsceneFactory.cpp ----
#include <cstring>
#include <cstdlib>
static uint32_t* sPs3CutsceneWords = nullptr;
static size_t sPs3CutsceneWordCount = 0;

namespace {
struct Ps3CsCursor {
    const unsigned char* data;
    size_t size;
    size_t off;
    bool big;
    bool ok;
    bool Have4() { if (!ok || off > size || size - off < 4) { ok = false; } return ok; }
    uint32_t CsRaw() {
        if (!Have4()) return 0;
        uint32_t v;
        memcpy(&v, data + off, 4);
        off += 4;
        return v;
    }
    uint32_t CsU32() {
        if (!Have4()) return 0;
        const unsigned char* p = data + off;
        off += 4;
        return big ? ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]
                   : ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
    }
    static void CsSwap(uint8_t* x, uint8_t* y) { uint8_t t = *x; *x = *y; *y = t; }
    uint32_t CsBBH() { uint32_t v = CsRaw(); if (!big) { uint8_t* b = (uint8_t*)&v; CsSwap(&b[2], &b[3]); } return v; }
    uint32_t CsHBB() { uint32_t v = CsRaw(); if (!big) { uint8_t* b = (uint8_t*)&v; CsSwap(&b[0], &b[1]); } return v; }
    uint32_t CsHH()  { uint32_t v = CsRaw(); if (!big) { uint8_t* b = (uint8_t*)&v; CsSwap(&b[0], &b[1]); CsSwap(&b[2], &b[3]); } return v; }
};
} // namespace

static bool Ps3ConvertTitleCutscene(const unsigned char* data, size_t size, bool big) {
    Ps3CsCursor c = { data, size, 0, big, true };
    std::vector<uint32_t> w;
    const uint32_t numEntries = c.CsU32();
    w.reserve(numEntries < 65536U ? numEntries : 0U);
    w.push_back(c.CsU32()); // numCommands
    w.push_back(c.CsU32()); // endFrame
    bool ended = false;
    while (c.ok && !ended) {
        const uint32_t id = c.CsU32();
        if (!c.ok) break;
        w.push_back(id);
        switch (id) {
            case 0x01: case 0x02: case 0x05: case 0x06: { // camera pos/focus (+Link variants)
                w.push_back(c.CsHH());
                w.push_back(c.CsHH());
                const size_t ps3CamFirst = w.size();
                while (c.ok) {
                    uint32_t val = c.CsBBH();
                    const int8_t continueFlag = ((int8_t*)&val)[0];
                    w.push_back(val);
                    w.push_back(c.CsU32());
                    w.push_back(c.CsHH());
                    w.push_back(c.CsHH());
                    if (continueFlag == -1) break;
                }
                if (w.size() >= ps3CamFirst + 4) {
                    uint16_t h[8];
                    memcpy(&h[0], &w[ps3CamFirst - 2], 4);
                    memcpy(&h[2], &w[ps3CamFirst - 1], 4);
                    memcpy(&h[4], &w[ps3CamFirst + 0], 4);
                    int16_t p[4];
                    memcpy(&p[0], &w[ps3CamFirst + 2], 4);
                    memcpy(&p[2], &w[ps3CamFirst + 3], 4);
                    char camLine[200];
                    snprintf(camLine, sizeof(camLine),
                             "trace: cs camera cmd id=%lu start=%u end=%u points=%lu firstHold=%u firstPos=(%d,%d,%d)",
                             (unsigned long)id, (unsigned)h[1], (unsigned)h[2],
                             (unsigned long)((w.size() - ps3CamFirst) / 4), (unsigned)h[5],
                             (int)p[0], (int)p[1], (int)p[2]);
                    oot_ps3_early_trace(camLine);
                }
                break;
            }
            case 0x03: case 0x04: case 0x15: case 0x1A: case 0x56: case 0x57: case 0x7C: {
                // special action, lighting, 0x15, unknown, play/stop/fade BGM
                const uint32_t n = c.CsU32();
                w.push_back(n);
                for (uint32_t i = 0; i < n && c.ok; ++i) {
                    w.push_back(c.CsHH());
                    w.push_back(c.CsHH());
                    for (int k = 0; k < 10; ++k) w.push_back(c.CsU32());
                }
                break;
            }
            case 0x09: {
                const uint32_t n = c.CsU32();
                w.push_back(n);
                for (uint32_t i = 0; i < n && c.ok; ++i) {
                    w.push_back(c.CsHH());
                    w.push_back(c.CsHBB());
                    w.push_back(c.CsBBH());
                }
                break;
            }
            case 0x13: { // textbox
                const uint32_t n = c.CsU32();
                w.push_back(n);
                for (uint32_t i = 0; i < n && c.ok; ++i) {
                    w.push_back(c.CsHH());
                    w.push_back(c.CsHH());
                    w.push_back(c.CsHH());
                }
                break;
            }
            case 0x0A: case 0x0F: case 17: case 18: case 23: case 34: case 39: case 46: case 76: case 85:
            case 93: case 105: case 107: case 110: case 119: case 123: case 138: case 139: case 144:
            case 0x0E: case 16: case 24: case 35: case 40: case 48: case 64: case 68: case 70: case 78:
            case 80: case 94: case 116: case 118: case 120: case 125: case 131: case 141:
            case 0x19: case 36: case 41: case 50: case 67: case 69: case 72: case 74: case 81: case 106:
            case 117: case 121: case 126: case 132:
            case 0x1D: case 37: case 42: case 51: case 53: case 63: case 65: case 66: case 75: case 82:
            case 108: case 127: case 133:
            case 0x1E: case 38: case 43: case 47: case 54: case 79: case 83: case 128: case 135:
            case 0x2C: case 55: case 77: case 84: case 90: case 129: case 136:
            case 0x1F: case 52: case 57: case 58: case 88: case 115: case 130: case 137:
            case 0x31: case 60: case 89: case 111: case 114: case 134: case 142:
            case 0x3E: case 0x8F: { // actor actions
                const uint32_t n = c.CsU32();
                w.push_back(n);
                for (uint32_t i = 0; i < n && c.ok; ++i) {
                    w.push_back(c.CsHH());
                    w.push_back(c.CsHH());
                    w.push_back(c.CsHH());
                    for (int k = 0; k < 9; ++k) w.push_back(c.CsU32());
                }
                break;
            }
            case 0x2D:   // scene transition FX
            case 0x3E8:  // terminator
                w.push_back(c.CsU32());
                w.push_back(c.CsHH());
                w.push_back(c.CsHH());
                break;
            case 0x8C: { // set time
                const uint32_t n = c.CsU32();
                w.push_back(n);
                for (uint32_t i = 0; i < n && c.ok; ++i) {
                    w.push_back(c.CsHH());
                    w.push_back(c.CsHBB());
                    w.push_back(c.CsU32());
                }
                break;
            }
            case 0xFFFFFFFFU: // CS_END
                w.push_back(c.CsU32());
                ended = true;
                break;
            default: // upstream also just continues on unknown IDs
                break;
        }
    }
    char msg[160];
    if (!c.ok || !ended) {
        snprintf(msg, sizeof(msg), "trace: title cutscene conversion FAILED ok=%d ended=%d offset=%lu size=%lu",
                 (int)c.ok, (int)ended, (unsigned long)c.off, (unsigned long)size);
        oot_ps3_early_trace(msg);
        return false;
    }
    uint32_t* words = (uint32_t*)malloc(w.size() * sizeof(uint32_t));
    if (words == nullptr) {
        oot_ps3_early_trace("trace: title cutscene conversion FAILED: out of memory");
        return false;
    }
    memcpy(words, w.data(), w.size() * sizeof(uint32_t));
    if (sPs3CutsceneWords != nullptr) free(sPs3CutsceneWords);
    sPs3CutsceneWords = words;
    sPs3CutsceneWordCount = w.size();
    snprintf(msg, sizeof(msg), "trace: title cutscene converted words=%lu commands=%lu endFrame=%lu bytes=%lu/%lu",
             (unsigned long)w.size(), (unsigned long)w[0], (unsigned long)w[1],
             (unsigned long)c.off, (unsigned long)size);
    oot_ps3_early_trace(msg);
    return true;
}

extern "C" void* oot_ps3_title_cutscene_segment(void) { return sPs3CutsceneWords; }
// ---- end PS3 title cutscene converter ----

// ---- PS3 scene collision converter: mirrors SOH CollisionHeaderFactory.cpp ----
static CollisionHeader* sPs3CollisionHeader = nullptr;

namespace {
struct Ps3ColCursor {
    const unsigned char* data;
    size_t size;
    size_t off;
    bool big;
    bool ok;
    bool ColHave(size_t n) { if (!ok || off > size || size - off < n) { ok = false; } return ok; }
    uint16_t ColU16() {
        if (!ColHave(2)) return 0;
        const unsigned char* p = data + off;
        off += 2;
        return big ? (uint16_t)((p[0] << 8) | p[1]) : (uint16_t)((p[1] << 8) | p[0]);
    }
    uint32_t ColU32() {
        if (!ColHave(4)) return 0;
        const unsigned char* p = data + off;
        off += 4;
        return big ? ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]
                   : ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
    }
};
} // namespace

static bool Ps3ColFail(const char* why, const Ps3ColCursor& c) {
    char msg[200];
    snprintf(msg, sizeof(msg), "trace: scene collision conversion FAILED (%s) offset=%lu size=%lu",
             why, (unsigned long)c.off, (unsigned long)c.size);
    oot_ps3_early_trace(msg);
    return false;
}

static bool Ps3ConvertCollision(const unsigned char* data, size_t size, bool big) {
    Ps3ColCursor c = { data, size, 0, big, true };
    CollisionHeader* h = (CollisionHeader*)calloc(1, sizeof(CollisionHeader));
    if (h == nullptr) return Ps3ColFail("out of memory", c);

    h->minBounds.x = (s16)c.ColU16(); h->minBounds.y = (s16)c.ColU16(); h->minBounds.z = (s16)c.ColU16();
    h->maxBounds.x = (s16)c.ColU16(); h->maxBounds.y = (s16)c.ColU16(); h->maxBounds.z = (s16)c.ColU16();

    const uint32_t numVtx = c.ColU32();
    if (!c.ok || numVtx > 0xFFFFU) return Ps3ColFail("vertex count", c);
    Vec3s* vtx = (Vec3s*)calloc(numVtx ? numVtx : 1, sizeof(Vec3s));
    if (vtx == nullptr) return Ps3ColFail("out of memory (vertices)", c);
    for (uint32_t i = 0; i < numVtx && c.ok; ++i) {
        vtx[i].x = (s16)c.ColU16(); vtx[i].y = (s16)c.ColU16(); vtx[i].z = (s16)c.ColU16();
    }
    h->numVertices = (u16)numVtx;
    h->vtxList = vtx;

    const uint32_t numPoly = c.ColU32();
    if (!c.ok || numPoly > 0xFFFFU) return Ps3ColFail("polygon count", c);
    CollisionPoly* poly = (CollisionPoly*)calloc(numPoly ? numPoly : 1, sizeof(CollisionPoly));
    if (poly == nullptr) return Ps3ColFail("out of memory (polygons)", c);
    for (uint32_t i = 0; i < numPoly && c.ok; ++i) {
        poly[i].type = c.ColU16();
        poly[i].flags_vIA = c.ColU16();
        poly[i].flags_vIB = c.ColU16();
        poly[i].vIC = c.ColU16();
        poly[i].normal.x = (s16)c.ColU16();
        poly[i].normal.y = (s16)c.ColU16();
        poly[i].normal.z = (s16)c.ColU16();
        poly[i].dist = (s16)c.ColU16();
    }
    h->numPolygons = (u16)numPoly;
    h->polyList = poly;

    const uint32_t numSurf = c.ColU32();
    if (!c.ok || numSurf > 0x10000U) return Ps3ColFail("surface count", c);
    SurfaceType* surf = (SurfaceType*)calloc(numSurf ? numSurf : 1, sizeof(SurfaceType));
    if (surf == nullptr) return Ps3ColFail("out of memory (surfaces)", c);
    for (uint32_t i = 0; i < numSurf && c.ok; ++i) {
        surf[i].data[1] = c.ColU32(); // upstream reads data[1] first
        surf[i].data[0] = c.ColU32();
    }
    h->surfaceTypeList = surf;

    const uint32_t numCam = c.ColU32();
    if (!c.ok || numCam > 0x1000U) return Ps3ColFail("camera data count", c);
    CamData* cams = (CamData*)calloc(numCam ? numCam : 1, sizeof(CamData));
    int32_t* camIdx = (int32_t*)calloc(numCam ? numCam : 1, sizeof(int32_t));
    if (cams == nullptr || camIdx == nullptr) return Ps3ColFail("out of memory (camera data)", c);
    for (uint32_t i = 0; i < numCam && c.ok; ++i) {
        cams[i].cameraSType = c.ColU16();
        cams[i].numCameras = (s16)c.ColU16();
        camIdx[i] = (int32_t)c.ColU32();
    }
    const int32_t numCamPos = (int32_t)c.ColU32();
    if (!c.ok || numCamPos < 0 || numCamPos > 0x10000) return Ps3ColFail("camera position count", c);
    Vec3s* camPos = (Vec3s*)calloc(numCamPos > 0 ? numCamPos : 1, sizeof(Vec3s)); // entry 0 doubles as the zero vector
    if (camPos == nullptr) return Ps3ColFail("out of memory (camera positions)", c);
    for (int32_t i = 0; i < numCamPos && c.ok; ++i) {
        camPos[i].x = (s16)c.ColU16(); camPos[i].y = (s16)c.ColU16(); camPos[i].z = (s16)c.ColU16();
    }
    for (uint32_t i = 0; i < numCam; ++i) {
        const int32_t idx = camIdx[i];
        cams[i].camPosData = (numCamPos > 0 && idx >= 0 && idx < numCamPos) ? &camPos[idx] : &camPos[0];
    }
    free(camIdx);
    h->cameraDataList = cams;
    h->cameraDataListLen = numCam;

    const uint32_t numWater = c.ColU32();
    if (!c.ok || numWater > 0x1000U) return Ps3ColFail("water box count", c);
    WaterBox* water = (WaterBox*)calloc(numWater ? numWater : 1, sizeof(WaterBox));
    if (water == nullptr) return Ps3ColFail("out of memory (water boxes)", c);
    for (uint32_t i = 0; i < numWater && c.ok; ++i) {
        water[i].xMin = (s16)c.ColU16();
        water[i].ySurface = (s16)c.ColU16();
        water[i].zMin = (s16)c.ColU16();
        water[i].xLength = (s16)c.ColU16();
        water[i].zLength = (s16)c.ColU16();
        water[i].properties = c.ColU32();
    }
    h->numWaterBoxes = (u16)numWater;
    h->waterBoxes = water;

    if (!c.ok) return Ps3ColFail("truncated data", c);
    sPs3CollisionHeader = h;
    char msg[240];
    snprintf(msg, sizeof(msg),
             "trace: scene collision converted vtx=%lu polys=%lu surfaces=%lu cams=%lu camPos=%ld water=%lu bytes=%lu/%lu "
             "bounds=(%d,%d,%d)-(%d,%d,%d)",
             (unsigned long)numVtx, (unsigned long)numPoly, (unsigned long)numSurf, (unsigned long)numCam,
             (long)numCamPos, (unsigned long)numWater, (unsigned long)c.off, (unsigned long)size,
             h->minBounds.x, h->minBounds.y, h->minBounds.z, h->maxBounds.x, h->maxBounds.y, h->maxBounds.z);
    oot_ps3_early_trace(msg);
    return true;
}

static char sPs3CollisionPath[256];
static EnvLightSettings sPs3LightSettings[32];
static uint32_t sPs3LightSettingsCount;
// ---- end PS3 scene collision converter ----
static unsigned long long sResourceLoadUs = 0;
static unsigned int sResourceCacheHits = 0;
static unsigned int sResourceCacheMisses = 0;
extern "C" void oot_ps3_resource_frame_stats(unsigned long long* outUs, unsigned int* outHits, unsigned int* outMisses) {
    if (outUs) *outUs = sResourceLoadUs;
    if (outHits) *outHits = sResourceCacheHits;
    if (outMisses) *outMisses = sResourceCacheMisses;
    sResourceLoadUs = 0;
    sResourceCacheHits = 0;
    sResourceCacheMisses = 0;
}

namespace Fast {
static bool IsExpandedLogoOpcode(int8_t opcode) {
    return opcode == G_SETTIMG_OTR_HASH || opcode == G_DL_OTR_HASH ||
           opcode == G_VTX_OTR_HASH || opcode == G_BRANCH_Z_OTR ||
           opcode == G_MARKER || opcode == G_MTX_OTR || opcode == G_MOVEMEM_OTR;
}
}

namespace {

// Current Torch-generated O2R files carry a 64-byte resource header.
// The old 20-byte offset points into that header, before the texture body.
constexpr uint32_t kOtrHeaderSize = 64;

struct ZipEntry {
    std::string name;
    uint64_t hash;
    uint16_t method;
    uint32_t crc;
    uint32_t compressedSize;
    uint32_t uncompressedSize;
    uint32_t localOffset;
};

struct Archive {
    std::string path;
    std::vector<ZipEntry> entries;
    // hash -> position in entries, built on first lookup (FindEntry used to scan
    // all ~30k entries on every miss).
    mutable std::unordered_map<uint64_t, size_t> index;
};

struct Resource {
    std::string name;
    uint64_t hash = 0;
    uint32_t type = 0;
    uint32_t version = 0;
    bool custom = false;
    bool bigEndian = false;
    uint16_t width = 0;
    uint16_t height = 0;
    uint32_t textureType = 0;
    uint32_t textureFlags = 0;
    std::vector<unsigned char> data;
    std::vector<std::vector<unsigned char>> references;
};

/* The native boot probe removes project-wide constructor tables.  These
 * containers therefore cannot live as ordinary namespace globals.  Build
 * them explicitly on first use, after the PS3 C/C++ runtime is operational.
 */
template <typename T>
T& LazyObject() {
    alignas(T) static unsigned char storage[sizeof(T)] = {};
    static bool initialized = false;
    if (!initialized) {
        new (storage) T();
        initialized = true;
    }
    return *reinterpret_cast<T*>(storage);
}

using ArchiveList = std::vector<Archive>;
using ResourceList = std::vector<Resource>;
using VersionList = std::vector<uint32_t>;

ArchiveList& Archives() { return LazyObject<ArchiveList>(); }
ResourceList& Resources() { return LazyObject<ResourceList>(); }
// hash -> position in Resources(). LoadHash used to scan every loaded resource on every
// call (~350 calls/frame, ~15 ms/frame in Kokiri Forest). Positions shift when
// ResourceUnloadByName erases, so that marks the index stale and it is rebuilt.
std::unordered_map<uint64_t, size_t>& ResourceIndex() { return LazyObject<std::unordered_map<uint64_t, size_t>>(); }
bool sResourceIndexStale = false;
VersionList& Versions() { return LazyObject<VersionList>(); }
bool sInitialized = false;

uint16_t Le16(const unsigned char* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

uint32_t Le32(const unsigned char* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint32_t Crc32(const unsigned char* data, size_t size) {
    uint32_t crc = ~0U;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            const uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

bool ReadAt(int descriptor, uint32_t offset, void* output, size_t size) {
    unsigned char* cursor = static_cast<unsigned char*>(output);
    size_t remaining = size;

    if (lseek(descriptor, static_cast<off_t>(offset), SEEK_SET) < 0) return false;
    while (remaining != 0U) {
        const ssize_t received = read(descriptor, cursor, remaining);
        if (received <= 0) return false;
        cursor += static_cast<size_t>(received);
        remaining -= static_cast<size_t>(received);
    }
    return true;
}

bool IndexArchive(const char* path, Archive* archive) {
    const bool titleArchive = std::strcmp(path,
        "/dev_hdd0/game/OOTP30048/USRDIR/oot.o2r") == 0;
    const int descriptor = open(path, O_RDONLY);
    if (descriptor < 0) {
        if (titleArchive) oot_ps3_early_trace("resource-index: oot.o2r open failed");
        return false;
    }
    if (titleArchive) oot_ps3_early_trace("resource-index: oot.o2r open succeeded");
    const off_t lengthOffset = lseek(descriptor, 0, SEEK_END);
    if (lengthOffset < 22 || lengthOffset > 0x7FFFFFFF) {
        if (titleArchive) oot_ps3_early_trace("resource-index: oot.o2r invalid file length");
        close(descriptor);
        return false;
    }
    const uint32_t length = static_cast<uint32_t>(lengthOffset);
    if (titleArchive) {
        char line[80];
        snprintf(line, sizeof(line), "resource-index: oot.o2r file bytes=%u",
                      static_cast<unsigned>(length));
        oot_ps3_early_trace(line);
    }
    const uint32_t tailSize = std::min<uint32_t>(length, 66000U);
    std::vector<unsigned char> tail(tailSize);
    if (!ReadAt(descriptor, length - tailSize, tail.data(), tail.size())) {
        if (titleArchive) oot_ps3_early_trace("resource-index: oot.o2r tail read failed");
        close(descriptor);
        return false;
    }
    uint32_t eocdInTail = 0xFFFFFFFFU;
    for (uint32_t pos = tailSize - 22U;; --pos) {
        if (Le32(tail.data() + pos) == 0x06054B50U) {
            eocdInTail = pos;
            break;
        }
        if (pos == 0) break;
    }
    if (eocdInTail == 0xFFFFFFFFU) {
        if (titleArchive) oot_ps3_early_trace("resource-index: oot.o2r ZIP end record missing");
        close(descriptor);
        return false;
    }
    if (titleArchive) oot_ps3_early_trace("resource-index: oot.o2r ZIP end record found");
    const unsigned char* eocd = tail.data() + eocdInTail;
    const uint16_t count = Le16(eocd + 10);
    const uint32_t centralSize = Le32(eocd + 12);
    const uint32_t centralOffset = Le32(eocd + 16);
    if (centralOffset > length || centralSize > length - centralOffset) {
        if (titleArchive) oot_ps3_early_trace("resource-index: oot.o2r central directory outside file");
        close(descriptor);
        return false;
    }
    if (titleArchive) {
        char line[96];
        snprintf(line, sizeof(line),
                      "resource-index: oot.o2r entries=%u central=%u offset=%u",
                      static_cast<unsigned>(count), static_cast<unsigned>(centralSize),
                      static_cast<unsigned>(centralOffset));
        oot_ps3_early_trace(line);
    }
    std::vector<unsigned char> central(centralSize);
    if (!ReadAt(descriptor, centralOffset, central.data(), central.size())) {
        if (titleArchive) oot_ps3_early_trace("resource-index: oot.o2r central directory read failed");
        close(descriptor);
        return false;
    }
    close(descriptor);

    archive->path = path;
    archive->entries.clear();
    uint32_t cursor = 0;
    for (uint16_t i = 0; i < count; ++i) {
        if (cursor > centralSize || centralSize - cursor < 46 || Le32(central.data() + cursor) != 0x02014B50U) {
            if (titleArchive) oot_ps3_early_trace("resource-index: oot.o2r central record invalid");
            return false;
        }
        const unsigned char* record = central.data() + cursor;
        const uint16_t nameLength = Le16(record + 28);
        const uint16_t extraLength = Le16(record + 30);
        const uint16_t commentLength = Le16(record + 32);
        const uint32_t recordSize = 46U + nameLength + extraLength + commentLength;
        if (recordSize > centralSize - cursor) {
            if (titleArchive) oot_ps3_early_trace("resource-index: oot.o2r central record truncated");
            return false;
        }
        ZipEntry entry;
        entry.name.assign(reinterpret_cast<const char*>(record + 46), nameLength);
        entry.hash = ShipwrightProbe::ShipCrc64Name(entry.name.c_str());
        entry.method = Le16(record + 10);
        entry.crc = Le32(record + 16);
        entry.compressedSize = Le32(record + 20);
        entry.uncompressedSize = Le32(record + 24);
        entry.localOffset = Le32(record + 42);
        if (!entry.name.empty() && entry.name.back() != '/') archive->entries.push_back(entry);
        cursor += recordSize;
    }
    if (titleArchive) oot_ps3_early_trace(archive->entries.empty() ?
        "resource-index: oot.o2r contains no usable entries" :
        "resource-index: oot.o2r indexed successfully");
    return !archive->entries.empty();
}

const ZipEntry* FindEntry(const Archive& archive, uint64_t hash) {
    if (archive.index.size() != archive.entries.size()) {
        archive.index.clear();
        archive.index.reserve(archive.entries.size());
        for (size_t i = 0; i < archive.entries.size(); ++i) archive.index.emplace(archive.entries[i].hash, i);
    }
    const auto found = archive.index.find(hash);
    return found != archive.index.end() ? &archive.entries[found->second] : nullptr;
}

bool ReadEntry(const Archive& archive, const ZipEntry& entry, std::vector<unsigned char>* output) {
    const int descriptor = open(archive.path.c_str(), O_RDONLY);
    if (descriptor < 0) return false;
    unsigned char local[30];
    if (!ReadAt(descriptor, entry.localOffset, local, sizeof(local)) || Le32(local) != 0x04034B50U) {
        close(descriptor);
        return false;
    }
    const uint32_t dataOffset = entry.localOffset + 30U + Le16(local + 26) + Le16(local + 28);
    std::vector<unsigned char> compressed(entry.compressedSize);
    const bool received = ReadAt(descriptor, dataOffset, compressed.data(), compressed.size());
    close(descriptor);
    if (!received) return false;
    if (entry.method == 0) {
        if (entry.compressedSize != entry.uncompressedSize) return false;
        *output = std::move(compressed);
    } else if (entry.method == 8) {
        output->assign(entry.uncompressedSize, 0);
        const int written = stbi_zlib_decode_noheader_buffer(reinterpret_cast<char*>(output->data()),
                                                              static_cast<int>(output->size()),
                                                              reinterpret_cast<const char*>(compressed.data()),
                                                              static_cast<int>(compressed.size()));
        if (written != static_cast<int>(entry.uncompressedSize)) return false;
    } else {
        return false;
    }
    return Crc32(output->data(), output->size()) == entry.crc;
}

std::string Normalize(const char* input) {
    std::string path = input == nullptr ? "" : input;
    if (path.compare(0, 7, "__OTR__") == 0) path.erase(0, 7);
    return path;
}

class Reader {
  public:
    Reader(const std::vector<unsigned char>& bytes, size_t offset, bool big)
        : bytes_(bytes), offset_(offset), big_(big) {}
    bool Can(size_t count) const { return offset_ <= bytes_.size() && count <= bytes_.size() - offset_; }
    uint8_t U8() { return Can(1) ? bytes_[offset_++] : 0; }
    uint16_t U16() {
        if (!Can(2)) return 0;
        const unsigned char* p = bytes_.data() + offset_; offset_ += 2;
        return big_ ? static_cast<uint16_t>((p[0] << 8) | p[1]) : Le16(p);
    }
    uint32_t ReadU32() {
        if (!Can(4)) return 0;
        const unsigned char* p = bytes_.data() + offset_; offset_ += 4;
        return big_ ? (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                          (static_cast<uint32_t>(p[2]) << 8) | p[3] : Le32(p);
    }
    void Align(size_t alignment) { offset_ = (offset_ + alignment - 1) & ~(alignment - 1); }
    bool Skip(size_t count) { if (!Can(count)) return false; offset_ += count; return true; }
    // Shipwright's scene strings use a little-endian length even when the
    // surrounding command IDs and numeric fields use big-endian byte order.
    bool SceneString(std::string* value) {
        if (!Can(4)) return false;
        const uint32_t length = Le32(bytes_.data() + offset_);
        if (length > 512 || !Can(static_cast<size_t>(length) + 4)) return false;
        offset_ += 4;
        for (uint32_t i = 0; i < length; ++i) {
            if (bytes_[offset_ + i] < 32 || bytes_[offset_ + i] > 126) return false;
        }
        if (value != nullptr) value->assign(reinterpret_cast<const char*>(bytes_.data() + offset_), length);
        offset_ += length;
        return true;
    }
    size_t Offset() const { return offset_; }
  private:
    const std::vector<unsigned char>& bytes_;
    size_t offset_;
    bool big_;
};

bool Decode(Resource* resource, const std::vector<unsigned char>& file) {
    if (file.size() < kOtrHeaderSize || (file[0] != 0 && file[0] != 1)) return false;
    const bool big = file[0] == 1;
    resource->bigEndian = big;
    Reader header(file, 4, big);
    resource->custom = file[1] != 0;
    resource->type = header.ReadU32();
    resource->version = header.ReadU32();
    Reader body(file, kOtrHeaderSize, big);

    // Binary layouts below follow Shipwright's V0 SkeletonFactory,
    // SkeletonLimbFactory, AnimationFactory and PlayerAnimationFactory.
    // Never expose serialized O2R bytes as native pointers.
    if (resource->type == static_cast<uint32_t>(SOH::ResourceType::SOH_SkeletonLimb)) {
        if (resource->version != 0 || !body.Can(2)) return false;
        const uint8_t limbType = body.U8();
        const uint8_t segmentType = body.U8();
        if (limbType != 1 && limbType != 2 && limbType != 3) return false;
        std::string skinDisplayList;
        if (!body.SceneString(&skinDisplayList) || !body.Can(6)) return false;
        const uint16_t totalVertices = body.U16();
        const uint32_t modCount = body.ReadU32();
        // Epona's body has 134 real skin modification entries (374 vertices).
        // Keep an upper bound while accepting the asset's native structure.
        if (modCount > 512 || totalVertices > 8192 || (limbType != 3 && modCount != 0)) return false;
        struct SkinPart {
            SkinLimbModif mod = {};
            std::vector<SkinVertex> vertices;
            std::vector<SkinTransformation> transformations;
        };
        std::vector<SkinPart> parts(modCount);
        uint32_t totalDecodedVertices = 0;
        uint32_t totalDecodedTransforms = 0;
        for (SkinPart& part : parts) {
            if (!body.Can(6)) return false;
            part.mod.unk_4 = body.U16();
            const uint32_t count = body.ReadU32();
            if (count > 8192 - totalDecodedVertices || !body.Can(static_cast<size_t>(count) * 10)) return false;
            totalDecodedVertices += count;
            part.vertices.reserve(count);
            for (uint32_t i = 0; i < count; ++i) {
                SkinVertex vertex = {};
                vertex.index = body.U16();
                vertex.s = static_cast<s16>(body.U16());
                vertex.t = static_cast<s16>(body.U16());
                vertex.normX = static_cast<s8>(body.U8());
                vertex.normY = static_cast<s8>(body.U8());
                vertex.normZ = static_cast<s8>(body.U8());
                vertex.alpha = body.U8();
                if (vertex.index >= totalVertices) return false;
                part.vertices.push_back(vertex);
            }
            if (!body.Can(4)) return false;
            const uint32_t transforms = body.ReadU32();
            if (transforms > 512 - totalDecodedTransforms || !body.Can(static_cast<size_t>(transforms) * 8)) return false;
            totalDecodedTransforms += transforms;
            part.transformations.reserve(transforms);
            for (uint32_t i = 0; i < transforms; ++i) {
                SkinTransformation transform = {};
                transform.limbIndex = body.U8();
                transform.x = static_cast<s16>(body.U16());
                transform.y = static_cast<s16>(body.U16());
                transform.z = static_cast<s16>(body.U16());
                transform.scale = body.U8();
                part.transformations.push_back(transform);
            }
        }
        std::string skinAnimatedList;
        if (!body.SceneString(&skinAnimatedList) || !body.Skip(12 + 6)) return false;
        std::string child, sibling, displayList, farDisplayList;
        if (!body.SceneString(&child) || !body.SceneString(&sibling) ||
            !body.SceneString(&displayList) || !body.SceneString(&farDisplayList) || !body.Can(8)) return false;
        const s16 x = static_cast<s16>(body.U16());
        const s16 y = static_cast<s16>(body.U16());
        const s16 z = static_cast<s16>(body.U16());
        const uint8_t childIndex = body.U8(), siblingIndex = body.U8();
        if (body.Offset() != file.size()) return false;
        // Shipwright passes display-list archive names as tagged pointers.
        // The byte vectors own their backing allocation after Resource moves.
        auto display = [&](const std::string& name) -> Gfx* {
            if (name.empty()) return nullptr;
            std::vector<unsigned char> owned(name.size() + 8, 0);
            std::memcpy(owned.data(), "__OTR__", 7);
            std::memcpy(owned.data() + 7, name.c_str(), name.size() + 1);
            resource->references.push_back(std::move(owned));
            return reinterpret_cast<Gfx*>(resource->references.back().data());
        };
        if (limbType == 3) {
            if (segmentType == 4 && (totalVertices == 0 || modCount == 0)) return false;
            if (segmentType != 0 && segmentType != 4 && segmentType != 5 && segmentType != 11) return false;
            auto aligned = [](size_t at, size_t align) { return (at + align - 1) & ~(align - 1); };
            size_t next = sizeof(SkinLimb);
            size_t animOffset = 0, modsOffset = 0;
            std::vector<size_t> verticesOffset(modCount), transformsOffset(modCount);
            if (segmentType == 4) {
                next = aligned(next, alignof(SkinAnimatedLimbData));
                animOffset = next;
                next += sizeof(SkinAnimatedLimbData);
                next = aligned(next, alignof(SkinLimbModif));
                modsOffset = next;
                next += modCount * sizeof(SkinLimbModif);
                for (uint32_t i = 0; i < modCount; ++i) {
                    next = aligned(next, alignof(SkinVertex));
                    verticesOffset[i] = next;
                    next += parts[i].vertices.size() * sizeof(SkinVertex);
                    next = aligned(next, alignof(SkinTransformation));
                    transformsOffset[i] = next;
                    next += parts[i].transformations.size() * sizeof(SkinTransformation);
                }
            }
            resource->data.resize(next);
            auto* bytes = resource->data.data();
            SkinLimb limb = {};
            limb.jointPos.x = x; limb.jointPos.y = y; limb.jointPos.z = z;
            limb.child = childIndex; limb.sibling = siblingIndex;
            limb.segmentType = segmentType;
            if (segmentType == 11) limb.segment = display(skinDisplayList);
            if (segmentType == 4) {
                SkinAnimatedLimbData animation = {};
                animation.totalVtxCount = totalVertices;
                animation.limbModifCount = static_cast<u16>(modCount);
                animation.limbModifications = reinterpret_cast<SkinLimbModif*>(bytes + modsOffset);
                animation.dlist = display(skinAnimatedList);
                std::memcpy(bytes + animOffset, &animation, sizeof(animation));
                limb.segment = bytes + animOffset;
                for (uint32_t i = 0; i < modCount; ++i) {
                    SkinLimbModif mod = parts[i].mod;
                    mod.vtxCount = static_cast<u16>(parts[i].vertices.size());
                    mod.transformCount = static_cast<u16>(parts[i].transformations.size());
                    mod.skinVertices = reinterpret_cast<SkinVertex*>(bytes + verticesOffset[i]);
                    mod.limbTransformations = reinterpret_cast<SkinTransformation*>(bytes + transformsOffset[i]);
                    std::memcpy(bytes + modsOffset + i * sizeof(mod), &mod, sizeof(mod));
                    if (!parts[i].vertices.empty()) std::memcpy(bytes + verticesOffset[i], parts[i].vertices.data(), parts[i].vertices.size() * sizeof(SkinVertex));
                    if (!parts[i].transformations.empty()) std::memcpy(bytes + transformsOffset[i], parts[i].transformations.data(), parts[i].transformations.size() * sizeof(SkinTransformation));
                }
            }
            std::memcpy(bytes, &limb, sizeof(limb));
            // Avoid dozens of synchronous trace writes while Epona's limbs load.
            if (resource->name == "objects/object_horse/gEponaBodyLimb") {
                oot_ps3_early_trace("resource: native horse skin limb decoded");
            }
        } else if (limbType == 1) {
            StandardLimb limb = {};
            limb.jointPos.x = x; limb.jointPos.y = y; limb.jointPos.z = z;
            limb.child = childIndex; limb.sibling = siblingIndex;
            limb.dList = display(displayList);
            resource->data.resize(sizeof(limb));
            std::memcpy(resource->data.data(), &limb, sizeof(limb));
        } else {
            LodLimb limb = {};
            limb.jointPos.x = x; limb.jointPos.y = y; limb.jointPos.z = z;
            limb.child = childIndex; limb.sibling = siblingIndex;
            limb.dLists[0] = display(displayList);
            limb.dLists[1] = display(farDisplayList);
            resource->data.resize(sizeof(limb));
            std::memcpy(resource->data.data(), &limb, sizeof(limb));
        }
        return true;
    }
    if (resource->type == static_cast<uint32_t>(SOH::ResourceType::SOH_Skeleton)) {
        if (resource->version != 0 || !body.Can(15)) return false;
        const uint8_t skeletonType = body.U8(), limbType = body.U8();
        const uint32_t limbCount = body.ReadU32(), dListCount = body.ReadU32();
        (void)body.U8(); // limb table type (may differ from limb type)
        const uint32_t tableCount = body.ReadU32();
        if (skeletonType > 1 || (limbType != 1 && limbType != 2 && limbType != 3) ||
            limbCount == 0 || limbCount > 64 || tableCount == 0 ||
            tableCount > 64 || dListCount > 64) return false;
        std::vector<std::string> paths;
        paths.reserve(tableCount);
        for (uint32_t i = 0; i < tableCount; ++i) {
            std::string path;
            if (!body.SceneString(&path) || path.empty()) return false;
            paths.push_back(std::move(path));
        }
        if (body.Offset() != file.size()) return false;
        std::vector<void*> limbs;
        limbs.reserve(tableCount);
        for (const std::string& path : paths) {
            void* limb = ResourceGetDataByName(path.c_str());
            if (limb == nullptr) return false;
            limbs.push_back(limb);
        }
        const size_t tableOffset = (sizeof(FlexSkeletonHeader) + alignof(void*) - 1) & ~(alignof(void*) - 1);
        resource->data.resize(tableOffset + limbs.size() * sizeof(void*));
        std::memcpy(resource->data.data() + tableOffset, limbs.data(), limbs.size() * sizeof(void*));
        FlexSkeletonHeader header = {};
        header.sh.segment = reinterpret_cast<void**>(resource->data.data() + tableOffset);
        header.sh.limbCount = static_cast<uint8_t>(limbCount);
        header.sh.skeletonType = skeletonType;
        header.dListCount = static_cast<uint8_t>(dListCount);
        std::memcpy(resource->data.data(), &header, sizeof(header));
        oot_ps3_early_trace("resource: native skeleton and limb table decoded");
        return true;
    }
    if (resource->type == static_cast<uint32_t>(SOH::ResourceType::SOH_PlayerAnimation)) {
        if (resource->version != 0 || !body.Can(4)) return false;
        const uint32_t count = body.ReadU32();
        if (count == 0 || count > 65536 || !body.Can(static_cast<size_t>(count) * 2) ||
            body.Offset() + static_cast<size_t>(count) * 2 != file.size()) return false;
        resource->data.resize(static_cast<size_t>(count) * 2 + 2);
        s16* frames = reinterpret_cast<s16*>(resource->data.data());
        for (uint32_t i = 0; i < count; ++i) frames[i] = static_cast<s16>(body.U16());
        return true;
    }
    if (resource->type == static_cast<uint32_t>(SOH::ResourceType::SOH_Animation)) {
        if (resource->version != 0 || !body.Can(6)) return false;
        const uint32_t animationType = body.ReadU32();
        if (animationType == 0) {
            if (!body.Can(6)) return false;
            const s16 frames = static_cast<s16>(body.U16());
            const uint32_t valueCount = body.ReadU32();
            if (frames <= 0 || valueCount > 131072 || !body.Can(static_cast<size_t>(valueCount) * 2 + 4)) return false;
            std::vector<s16> values(valueCount);
            for (s16& value : values) value = static_cast<s16>(body.U16());
            const uint32_t jointCount = body.ReadU32();
            if (jointCount == 0 || jointCount > 128 || !body.Can(static_cast<size_t>(jointCount) * 6 + 2)) return false;
            std::vector<JointIndex> joints(jointCount);
            for (JointIndex& joint : joints) {
                joint.x = body.U16(); joint.y = body.U16(); joint.z = body.U16();
            }
            const u16 staticIndexMax = body.U16();
            if (staticIndexMax > valueCount || body.Offset() != file.size()) return false;
            auto aligned = [](size_t at, size_t align) { return (at + align - 1) & ~(align - 1); };
            const size_t valuesOffset = aligned(sizeof(AnimationHeader), alignof(s16));
            const size_t jointsOffset = aligned(valuesOffset + values.size() * sizeof(s16), alignof(JointIndex));
            resource->data.resize(jointsOffset + joints.size() * sizeof(JointIndex));
            auto* bytes = resource->data.data();
            if (!values.empty()) std::memcpy(bytes + valuesOffset, values.data(), values.size() * sizeof(s16));
            std::memcpy(bytes + jointsOffset, joints.data(), joints.size() * sizeof(JointIndex));
            AnimationHeader animation = {};
            animation.common.frameCount = frames;
            animation.frameData = reinterpret_cast<s16*>(bytes + valuesOffset);
            animation.jointIndices = reinterpret_cast<JointIndex*>(bytes + jointsOffset);
            animation.staticIndexMax = staticIndexMax;
            std::memcpy(bytes, &animation, sizeof(animation));
            oot_ps3_early_trace("resource: native horse normal animation decoded");
            return true;
        }
        if (animationType != 1) return false;
        const s16 frameCount = static_cast<s16>(body.U16());
        std::string path;
        if (frameCount <= 0 || !body.SceneString(&path) || path.empty() ||
            body.Offset() != file.size()) return false;
        void* frames = ResourceGetDataByName(path.c_str());
        if (frames == nullptr) return false;
        LinkAnimationHeader header = {};
        header.common.frameCount = frameCount;
        header.segment = frames;
        resource->data.resize(sizeof(header));
        std::memcpy(resource->data.data(), &header, sizeof(header));
        oot_ps3_early_trace("resource: native Link animation decoded");
        return true;
    }
    // Background (pre-rendered room JPEG) uses the blob layout: u32 size + bytes.
    if (resource->type == static_cast<uint32_t>(Ship::ResourceType::Blob) ||
        resource->type == static_cast<uint32_t>(SOH::ResourceType::SOH_Background)) {
        const uint32_t size = body.ReadU32();
        if (!body.Can(size)) return false;
        resource->data.assign(file.begin() + body.Offset(), file.begin() + body.Offset() + size);
        resource->data.resize(resource->data.size() + 16, 0);
        return true;
    }
    if (resource->type == static_cast<uint32_t>(Fast::ResourceType::Texture)) {
        resource->textureType = body.ReadU32();
        resource->width = static_cast<uint16_t>(body.ReadU32());
        resource->height = static_cast<uint16_t>(body.ReadU32());
        if (resource->version >= 1) {
            resource->textureFlags = body.ReadU32();
            (void)body.ReadU32();
            (void)body.ReadU32();
        }
        const uint32_t size = body.ReadU32();
        if (!body.Can(size)) return false;
        resource->data.assign(file.begin() + body.Offset(), file.begin() + body.Offset() + size);
        return true;
    }
    if (resource->type == static_cast<uint32_t>(Fast::ResourceType::Vertex)) {
        const uint32_t count = body.ReadU32();
        resource->data.resize(static_cast<size_t>(count) * sizeof(Vtx));
        Vtx* vertices = reinterpret_cast<Vtx*>(resource->data.data());
        for (uint32_t i = 0; i < count; ++i) {
            if (!body.Can(16)) return false;
            vertices[i].v.ob[0] = static_cast<s16>(body.U16());
            vertices[i].v.ob[1] = static_cast<s16>(body.U16());
            vertices[i].v.ob[2] = static_cast<s16>(body.U16());
            vertices[i].v.flag = body.U16();
            vertices[i].v.tc[0] = static_cast<s16>(body.U16());
            vertices[i].v.tc[1] = static_cast<s16>(body.U16());
            for (int channel = 0; channel < 4; ++channel) vertices[i].v.cn[channel] = body.U8();
        }
        return true;
    }
    // Torch exports the logo's Vtx_001C00 as a SOH Array, not a Fast Vertex.
    // ArrayFactory reads: array type, element count, then 16 bytes per vertex.
    // Limit this compatibility path to the one resource used by the title.
    if (resource->name == "textures/nintendo_rogo_static/nintendo_rogo_staticVtx_001C00") {
        if (!body.Can(8)) return false;
        const uint32_t arrayType = body.ReadU32();
        const uint32_t count = body.ReadU32();
        if (count == 0 || count > 512 || !body.Can(static_cast<size_t>(count) * 16U) ||
            body.Offset() + static_cast<size_t>(count) * 16U != file.size()) return false;
        char line[96];
        snprintf(line, sizeof(line), "logo-3d: vertex array type=%lu count=%lu",
                 static_cast<unsigned long>(arrayType), static_cast<unsigned long>(count));
        oot_ps3_early_trace(line);
        resource->data.resize(static_cast<size_t>(count) * sizeof(Vtx));
        Vtx* vertices = reinterpret_cast<Vtx*>(resource->data.data());
        for (uint32_t i = 0; i < count; ++i) {
            vertices[i].v.ob[0] = static_cast<s16>(body.U16());
            vertices[i].v.ob[1] = static_cast<s16>(body.U16());
            vertices[i].v.ob[2] = static_cast<s16>(body.U16());
            vertices[i].v.flag = body.U16();
            vertices[i].v.tc[0] = static_cast<s16>(body.U16());
            vertices[i].v.tc[1] = static_cast<s16>(body.U16());
            for (int channel = 0; channel < 4; ++channel) vertices[i].v.cn[channel] = body.U8();
        }
        return true;
    }
    // SOH Array resources ('OARR'): ArrayFactory reads the array type, the
    // element count, then the elements. Model vertices (Link, Epona's skin,
    // most objects) are exported this way. A raw copy kept the 8-byte header
    // and the archive's byte order, scrambling every skeletal model.
    if (resource->type == 0x4F415252U && body.Can(8)) {
        const uint32_t arrayType = body.ReadU32();
        const uint32_t count = body.ReadU32();
        if (count > 0 && count <= 65536U &&
            body.Offset() + static_cast<size_t>(count) * 16U == file.size()) {
            resource->data.resize(static_cast<size_t>(count) * sizeof(Vtx));
            Vtx* vertices = reinterpret_cast<Vtx*>(resource->data.data());
            for (uint32_t i = 0; i < count; ++i) {
                vertices[i].v.ob[0] = static_cast<s16>(body.U16());
                vertices[i].v.ob[1] = static_cast<s16>(body.U16());
                vertices[i].v.ob[2] = static_cast<s16>(body.U16());
                vertices[i].v.flag = body.U16();
                vertices[i].v.tc[0] = static_cast<s16>(body.U16());
                vertices[i].v.tc[1] = static_cast<s16>(body.U16());
                for (int channel = 0; channel < 4; ++channel) vertices[i].v.cn[channel] = body.U8();
            }
            static int ps3VtxArrayLogged = 0;
            if (ps3VtxArrayLogged < 8) {
                char line[256];
                snprintf(line, sizeof(line), "trace: vertex array decoded type=%lu count=%lu first=(%d,%d,%d) name=%s",
                         static_cast<unsigned long>(arrayType), static_cast<unsigned long>(count),
                         vertices[0].v.ob[0], vertices[0].v.ob[1], vertices[0].v.ob[2], resource->name.c_str());
                oot_ps3_early_trace(line);
                ++ps3VtxArrayLogged;
            }
            return true;
        }
    }
    if (resource->type == static_cast<uint32_t>(Fast::ResourceType::DisplayList)) {
        if (!body.Can(1)) return false;
        (void)body.U8();
        body.Align(8);
        std::vector<Gfx> commands;
        bool terminated = false;
        while (body.Can(8)) {
            // Room lists can be much larger than the original logo probe.
            // Bound allocation by the serialized body (8 bytes per word pair).
            if (commands.size() >= (file.size() - kOtrHeaderSize) / 8U) return false;
            Gfx command = {};
            command.words.w0 = body.ReadU32();
            command.words.w1 = body.ReadU32();
            commands.push_back(command);
            const uint8_t opcode = static_cast<uint8_t>(command.words.w0 >> 24);
            // DisplayListFactory treats these as 128-bit instructions. The
            // second pair contains an archive hash or marker payload; it is
            // not an N64 opcode, even when its top byte resembles G_ENDDL.
            if (Fast::IsExpandedLogoOpcode(static_cast<int8_t>(opcode))) {
                if (!body.Can(8)) return false;
                Gfx payload = {};
                payload.words.w0 = body.ReadU32();
                payload.words.w1 = body.ReadU32();
                commands.push_back(payload);
            }
            if (opcode == 0xDF || opcode == 0xB8) {
                terminated = true;
                break;
            }
        }
        if (!terminated) return false;
        resource->data.resize(commands.size() * sizeof(Gfx));
        if (!commands.empty()) std::memcpy(resource->data.data(), commands.data(), resource->data.size());
        if (commands.size() > 256U) {
            char line[384];
            snprintf(line, sizeof(line), "resource-dlist: large list decoded entries=%lu name=%s",
                     static_cast<unsigned long>(commands.size()), resource->name.c_str());
            oot_ps3_early_trace(line);
        }
        if (resource->name == "textures/nintendo_rogo_static/gNintendo64LogoDL")
            oot_ps3_early_trace("logo-3d: bounded O2R display list decoded");
        return !commands.empty();
    }
    if (resource->type == static_cast<uint32_t>(Fast::ResourceType::Matrix)) {
        Mtx matrix = {};
        for (int row = 0; row < 4; ++row) for (int column = 0; column < 4; ++column)
            matrix.m[row][column] = static_cast<s32>(body.ReadU32());
        resource->data.resize(sizeof(matrix));
        std::memcpy(resource->data.data(), &matrix, sizeof(matrix));
        return true;
    }
    {
        static int ps3RawLogged = 0;
        if (ps3RawLogged < 24) {
            char line[256];
            snprintf(line, sizeof(line), "trace: resource raw fallback type=%08lx size=%lu name=%s",
                     static_cast<unsigned long>(resource->type), static_cast<unsigned long>(file.size()),
                     resource->name.c_str());
            oot_ps3_early_trace(line);
            ++ps3RawLogged;
        }
    }
    resource->data.assign(file.begin() + kOtrHeaderSize, file.end());
    return true;
}

void Initialize() {
    if (sInitialized) return;
    oot_ps3_early_trace("resource: Initialize entered");
    ArchiveList& archives = Archives();
    oot_ps3_early_trace("resource: lazy archive container ready");
    VersionList& versions = Versions();
    oot_ps3_early_trace("resource: lazy version container ready");
    sInitialized = true;
    static const char* candidates[] = {
        "/dev_hdd0/game/OOTP30048/USRDIR/oot.o2r",
        "/dev_hdd0/game/OOTP30048/USRDIR/oot.otr",
        "/dev_hdd0/game/OOTP30048/USRDIR/assets/oot.o2r",
        "/dev_hdd0/game/OOTP30048/USRDIR/assets/oot.otr",
        "/dev_hdd0/game/OOTP30048/USRDIR/soh.o2r",
    };
    oot_ps3_early_trace("resource: scanning OOTP30048 archive paths");
    for (const char* path : candidates) {
        Archive archive;
        if (IndexArchive(path, &archive)) archives.push_back(std::move(archive));
    }
    oot_ps3_early_trace("resource: archive path scan completed");
    oot_ps3_early_trace(archives.empty() ? "resource: no user asset archive found" :
                                          "resource: user asset archive indexed");
    for (const Archive& archive : archives) {
        const uint64_t versionHash = ShipwrightProbe::ShipCrc64Name("version");
        const ZipEntry* entry = FindEntry(archive, versionHash);
        std::vector<unsigned char> version;
        if (entry != nullptr && ReadEntry(archive, *entry, &version) && version.size() >= 5) {
            uint32_t value = version[0] == 1 ? (static_cast<uint32_t>(version[1]) << 24) |
                (static_cast<uint32_t>(version[2]) << 16) | (static_cast<uint32_t>(version[3]) << 8) | version[4]
                : static_cast<uint32_t>(version[1]) | (static_cast<uint32_t>(version[2]) << 8) |
                (static_cast<uint32_t>(version[3]) << 16) | (static_cast<uint32_t>(version[4]) << 24);
            if (std::find(versions.begin(), versions.end(), value) == versions.end()) versions.push_back(value);
        }
    }
    oot_ps3_early_trace("resource: version scan completed");
}

Resource* LoadHash(uint64_t hash, const char* requestedName = nullptr) {
    ResourceList& resources = Resources();
    ArchiveList& archives = Archives();
    const bool titleProbe = requestedName != nullptr &&
        std::strcmp(requestedName,
                    "textures/nintendo_rogo_static/nintendo_rogo_static_Tex_000000") == 0;
    Initialize();
    std::unordered_map<uint64_t, size_t>& resourceIndex = ResourceIndex();
    if (sResourceIndexStale || resourceIndex.size() != resources.size()) {
        resourceIndex.clear();
        for (size_t i = 0; i < resources.size(); ++i) resourceIndex.emplace(resources[i].hash, i);
        sResourceIndexStale = false;
    }
    {
        const auto found = resourceIndex.find(hash);
        if (found != resourceIndex.end() && found->second < resources.size() && resources[found->second].hash == hash) {
            return &resources[found->second];
        }
    }
    for (auto archive = archives.rbegin(); archive != archives.rend(); ++archive) {
        const ZipEntry* entry = FindEntry(*archive, hash);
        if (entry == nullptr) continue;
        if (titleProbe) oot_ps3_early_trace("resource: title texture entry found");
        std::vector<unsigned char> file;
        if (!ReadEntry(*archive, *entry, &file)) {
            if (titleProbe) oot_ps3_early_trace("resource: title texture entry read failed");
            return nullptr;
        }
        if (titleProbe) oot_ps3_early_trace("resource: title texture entry read succeeded");
        Resource resource;
        resource.name = requestedName != nullptr ? requestedName : entry->name;
        resource.hash = hash;
        if (!Decode(&resource, file)) {
            if (titleProbe) oot_ps3_early_trace("resource: title texture decode failed");
            if (resource.type == static_cast<uint32_t>(Fast::ResourceType::DisplayList)) {
                char line[384];
                snprintf(line, sizeof(line), "resource-dlist: invalid or unterminated resource=%s", resource.name.c_str());
                oot_ps3_early_trace(line);
            }
            if (resource.type == static_cast<uint32_t>(SOH::ResourceType::SOH_Skeleton) ||
                resource.type == static_cast<uint32_t>(SOH::ResourceType::SOH_SkeletonLimb) ||
                resource.type == static_cast<uint32_t>(SOH::ResourceType::SOH_Animation) ||
                resource.type == static_cast<uint32_t>(SOH::ResourceType::SOH_PlayerAnimation)) {
                char line[192];
                snprintf(line, sizeof(line), "resource: native skeleton/animation decode rejected: %s",
                              resource.name.c_str());
                oot_ps3_early_trace(line);
            }
            return nullptr;
        }
        if (titleProbe) oot_ps3_early_trace("resource: title texture decoded");
        resources.push_back(std::move(resource));
        resourceIndex[hash] = resources.size() - 1;
        return &resources.back();
    }
    if (titleProbe) oot_ps3_early_trace("resource: title texture entry lookup failed");
    // Not in any archive. SoH keeps some assets of its own in soh.o2r; the empty
    // texture (the A button with no action, empty font glyphs) is built here when
    // soh.o2r isn't installed: 128x128 RGBA32, all transparent, as in soh.o2r.
    // Without it Fast3D kept the previous texture and drew garbage on the A button.
    static const uint64_t emptyTextureHash = ShipwrightProbe::ShipCrc64Name("textures/virtual/gEmptyTexture");
    if (hash == emptyTextureHash) {
        static const unsigned char header[0x50] = {
            0x00, 0x00, 0x00, 0x00, 0x58, 0x45, 0x54, 0x4f, 0x00, 0x00, 0x00, 0x00, 0xef, 0xbe, 0xad, 0xde,
            0xef, 0xbe, 0xad, 0xde, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x01, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
        };
        std::vector<unsigned char> file(sizeof(header) + 0x10000U, 0);
        memcpy(file.data(), header, sizeof(header));
        Resource resource;
        resource.name = "textures/virtual/gEmptyTexture";
        resource.hash = hash;
        if (!Decode(&resource, file)) {
            oot_ps3_early_trace("trace: built-in gEmptyTexture decode FAILED");
            return nullptr;
        }
        oot_ps3_early_trace("trace: built-in gEmptyTexture used (soh.o2r not installed)");
        resources.push_back(std::move(resource));
        resourceIndex[hash] = resources.size() - 1;
        return &resources.back();
    }
    // Report each missing resource once (first 64), so absent soh.o2r assets show up.
    static std::unordered_set<uint64_t> reportedMissing;
    if (reportedMissing.size() < 64 && reportedMissing.insert(hash).second) {
        char line[320];
        snprintf(line, sizeof(line), "trace: resource missing %s (%016llx)",
                 requestedName != nullptr ? requestedName : "(by hash)", static_cast<unsigned long long>(hash));
        oot_ps3_early_trace(line);
    }
    return nullptr;
}

Resource* LoadName(const char* name) {
    const std::string path = Normalize(name);
    if (path.find("EffectMask") != std::string::npos) {
        const uint64_t ps3MaskHash = ShipwrightProbe::ShipCrc64Name(path.c_str());
        Resource* ps3MaskResult = LoadHash(ps3MaskHash, path.c_str());
        static int ps3MaskLookups = 0;
        if (ps3MaskLookups < 40) {
            char line[320];
            snprintf(line, sizeof(line), "trace: mask lookup requested=%s normalized=%s hash=%016llx -> res=%s data=%p",
                     name ? name : "(null)", path.c_str(), static_cast<unsigned long long>(ps3MaskHash),
                     ps3MaskResult ? ps3MaskResult->name.c_str() : "(none)",
                     ps3MaskResult && !ps3MaskResult->data.empty()
                         ? static_cast<const void*>(ps3MaskResult->data.data()) : nullptr);
            oot_ps3_early_trace(line);
            ++ps3MaskLookups;
        }
        return ps3MaskResult;
    }
    if (!gOotPs3ResourceTimingEnabled) {
        return LoadHash(ShipwrightProbe::ShipCrc64Name(path.c_str()), path.c_str());
    }
    const unsigned long long ps3ResourceStartUs = oot_ps3_microseconds_now();
    Resource* ps3ResourceResult = LoadHash(ShipwrightProbe::ShipCrc64Name(path.c_str()), path.c_str());
    const unsigned long long ps3ResourceElapsedUs = oot_ps3_microseconds_now() - ps3ResourceStartUs;
    sResourceLoadUs += ps3ResourceElapsedUs;
    if (ps3ResourceElapsedUs > 5000ULL) ++sResourceCacheMisses; else ++sResourceCacheHits;
    return ps3ResourceResult;
}

bool IsPal(uint32_t version) {
    return version == OOT_PAL_10 || version == OOT_PAL_11 || version == OOT_PAL_GC ||
           version == OOT_PAL_MQ || version == OOT_PAL_GC_DBG1 || version == OOT_PAL_GC_DBG2 ||
           version == OOT_PAL_GC_MQ_DBG;
}

} // namespace

// Mirrors SetMeshFactory's serialized field order without constructing any
// display lists. Graphics remain disabled for this diagnostic scene probe.
static bool SkipSceneMesh(Reader& reader) {
    if (!reader.Can(2)) return false;
    (void)reader.U8(); // SetMesh::data
    const uint8_t type = reader.U8();
    if (type > 2) return false;
    uint32_t polygons = 1;
    if (type != 1) {
        if (!reader.Can(1)) return false;
        polygons = reader.U8();
    }
    if (polygons > 64) return false;
    for (uint32_t i = 0; i < polygons; ++i) {
        if (type == 0) {
            if (!reader.Skip(1) || !reader.SceneString(nullptr) || !reader.SceneString(nullptr)) return false;
        } else if (type == 2) {
            if (!reader.Skip(9) || !reader.SceneString(nullptr) || !reader.SceneString(nullptr)) return false;
        } else {
            if (!reader.Skip(1) || !reader.SceneString(nullptr) || !reader.SceneString(nullptr) || !reader.Can(4))
                return false;
            const uint32_t images = reader.ReadU32();
            if (images > 64) return false;
            for (uint32_t image = 0; image < images; ++image) {
                if (!reader.Skip(3) || !reader.SceneString(nullptr) || !reader.Skip(18)) return false;
            }
            if (!reader.Skip(1) || !reader.SceneString(nullptr) || !reader.SceneString(nullptr)) return false;
        }
    }
    return true;
}

// Keep the decoded room mesh and its OTR path strings alive for Room_Draw.
// These are pointers to archive resources, never embedded game assets.
struct Ps3RoomMesh {
    MeshHeader header;
    PolygonDlist normal[64];
    PolygonDlist2 sorted[64];
    char paths[128][520];
    void* segment;
    bool ready;
};

static Ps3RoomMesh& ActiveRoomMesh() {
    static Ps3RoomMesh room = {};
    return room;
}

static bool ReadRoomMesh(Reader& reader, Ps3RoomMesh* room) {
    if (!reader.Can(2)) return false;
    reader.U8(); // command data
    const uint8_t type = reader.U8();
    if (type != 0 && type != 2) return false;
    if (!reader.Can(1)) return false;
    const unsigned count = reader.U8();
    if (count == 0 || count > 64) return false;
    room->header = {};
    room->header.base.type = type;
    if (type == 0) {
        room->header.polygon0.num = count;
        room->header.polygon0.start = room->normal;
        room->header.polygon0.end = room->normal + count;
    } else {
        room->header.polygon2.num = count;
        room->header.polygon2.start = room->sorted;
        room->header.polygon2.end = room->sorted + count;
    }
    for (unsigned i = 0; i < count; ++i) {
        if (!reader.Can(type == 2 ? 9 : 1)) return false;
        reader.U8(); // polygon type, unused by the upstream importer
        if (type == 2) {
            room->sorted[i].pos.x = static_cast<int16_t>(reader.U16());
            room->sorted[i].pos.y = static_cast<int16_t>(reader.U16());
            room->sorted[i].pos.z = static_cast<int16_t>(reader.U16());
            room->sorted[i].unk_06 = static_cast<int16_t>(reader.U16());
        }
        for (unsigned pass = 0; pass < 2; ++pass) {
            std::string path;
            if (!reader.SceneString(&path)) return false;
            Gfx* displayList = nullptr;
            if (!path.empty()) {
                if (path.size() > sizeof(room->paths[0]) - 8) return false;
                char* ownedPath = room->paths[2 * i + pass];
                std::memcpy(ownedPath, "__OTR__", 7);
                std::memcpy(ownedPath + 7, path.c_str(), path.size() + 1);
                displayList = reinterpret_cast<Gfx*>(ownedPath);
            }
            if (type == 0) {
                if (pass == 0) room->normal[i].opa = displayList;
                else room->normal[i].xlu = displayList;
            } else {
                if (pass == 0) room->sorted[i].opa = displayList;
                else room->sorted[i].xlu = displayList;
            }
        }
    }
    return true;
}

// Room command 0x10 (SCENE_CMD_TIME_SETTINGS) of the selected room layer, applied
// by z_play.c the way Scene_CommandTimeSettings does on PC.
static bool sPs3RoomTimeValid = false;
static uint8_t sPs3RoomTime[3] = { 0xFF, 0xFF, 0xFF }; // hour, minute, timeIncrement

extern "C" int oot_ps3_room_time_settings(unsigned char* hour, unsigned char* minute, unsigned char* increment) {
    if (!sPs3RoomTimeValid) return 0;
    *hour = sPs3RoomTime[0];
    *minute = sPs3RoomTime[1];
    *increment = sPs3RoomTime[2];
    return 1;
}

// One JPN message as native-endian Shift-JIS code units. The message tables are
// not built on PS3; the title screen's ordered font only needs message 0xFFFC.
// Layout mirrors ResourceFactoryBinaryTextV0: u32 count, then per entry
// u16 id, u8 textboxType, u8 textboxYPos, u32 length + bytes (code units stored LE).
extern "C" int oot_ps3_load_jpn_message(unsigned short id, unsigned short* out, int maxUnits) {
    Resource* resource = LoadName("text/jpn_message_data_static/jpn_message_data_static");
    if (resource == nullptr || resource->data.size() < 4 || out == nullptr || maxUnits <= 0) return -1;
    const std::vector<unsigned char>& bytes = resource->data;
    Reader reader(bytes, 0, resource->bigEndian);
    const uint32_t count = reader.ReadU32();
    if (count > 8192) return -1;
    for (uint32_t i = 0; i < count; ++i) {
        if (!reader.Can(8)) return -1;
        const uint16_t entryId = reader.U16();
        (void)reader.U8();
        (void)reader.U8();
        const uint32_t length = reader.ReadU32();
        if (!reader.Can(length)) return -1;
        if (entryId == id) {
            const size_t offset = reader.Offset();
            int units = static_cast<int>(length / 2);
            if (units > maxUnits) units = maxUnits;
            for (int unit = 0; unit < units; ++unit) {
                const unsigned char lo = bytes[offset + unit * 2];
                const unsigned char hi = bytes[offset + unit * 2 + 1];
                out[unit] = resource->bigEndian ? static_cast<unsigned short>((lo << 8) | hi)
                                                : static_cast<unsigned short>((hi << 8) | lo);
            }
            return units;
        }
        reader.Skip(length);
    }
    return 0;
}

// ---- message tables: mirrors soh/z_message_OTR.cpp (OTRMessage_Init) ----
// Each table is decoded once into one persistent block: entries plus a 0xFFFF
// terminator (Message_FindMessage scans until it), and the text bytes. JPN
// text is 16-bit Shift-JIS stored little-endian; it is swapped to native order
// because the game reads it through msgBufWide (u16).
extern "C" MessageTableEntry* sNesMessageEntryTablePtr;
extern "C" MessageTableEntry* sGerMessageEntryTablePtr;
extern "C" MessageTableEntry* sFraMessageEntryTablePtr;
extern "C" MessageTableEntry* sJpnMessageEntryTablePtr;
extern "C" MessageTableEntry* sStaffMessageEntryTablePtr;
extern "C" char* _message_0xFFFC_nes;

static MessageTableEntry* Ps3LoadMessageTable(const char* path, bool wideText, bool isNes) {
    if (!ResourceMgr_FileExists(path)) return nullptr;
    Resource* resource = LoadName(path);
    if (resource == nullptr || resource->data.size() < 4) return nullptr;
    const std::vector<unsigned char> bytes = resource->data; // copy: later loads may move the resource list
    const bool big = resource->bigEndian;
    Reader reader(bytes, 0, big);
    const uint32_t count = reader.ReadU32();
    if (count == 0 || count > 8192) return nullptr;
    size_t textBytes = 0;
    {
        Reader scan = reader;
        for (uint32_t i = 0; i < count; ++i) {
            if (!scan.Can(8)) return nullptr;
            scan.Skip(4);
            const uint32_t length = scan.ReadU32();
            if (!scan.Can(length)) return nullptr;
            scan.Skip(length);
            textBytes += length + 2;
        }
    }
    MessageTableEntry* table = static_cast<MessageTableEntry*>(malloc(sizeof(MessageTableEntry) * (count + 1)));
    char* text = static_cast<char*>(malloc(textBytes + 2));
    if (table == nullptr || text == nullptr) return nullptr;
    char* out = text;
    for (uint32_t i = 0; i < count; ++i) {
        MessageTableEntry& entry = table[i];
        entry.textId = reader.U16();
        const uint8_t textboxType = reader.U8();
        const uint8_t textboxYPos = reader.U8();
        entry.typePos = static_cast<u8>((textboxType << 4) | textboxYPos);
        const uint32_t length = reader.ReadU32();
        const unsigned char* source = bytes.data() + reader.Offset();
        if (wideText && !big) {
            for (uint32_t b = 0; b + 1 < length; b += 2) {
                out[b] = static_cast<char>(source[b + 1]);
                out[b + 1] = static_cast<char>(source[b]);
            }
            if (length & 1) out[length - 1] = static_cast<char>(source[length - 1]);
        } else {
            std::memcpy(out, source, length);
        }
        reader.Skip(length);
        out[length] = '\0';
        out[length + 1] = '\0';
        entry.segment = out;
        entry.msgSize = length;
        if (isNes && entry.textId == 0xFFFC) _message_0xFFFC_nes = out;
        out += length + 2;
    }
    table[count].textId = 0xFFFF;
    table[count].typePos = 0;
    table[count].segment = out;
    table[count].msgSize = 0;
    out[0] = out[1] = '\0';
    char line[160];
    snprintf(line, sizeof(line), "trace: message table %s: %lu messages", path, static_cast<unsigned long>(count));
    oot_ps3_early_trace(line);
    return table;
}

extern "C" void OTRMessage_Init(void) {
    if (sNesMessageEntryTablePtr == nullptr)
        sNesMessageEntryTablePtr =
            Ps3LoadMessageTable("text/nes_message_data_static/nes_message_data_static", false, true);
    if (sGerMessageEntryTablePtr == nullptr)
        sGerMessageEntryTablePtr =
            Ps3LoadMessageTable("text/ger_message_data_static/ger_message_data_static", false, false);
    if (sFraMessageEntryTablePtr == nullptr)
        sFraMessageEntryTablePtr =
            Ps3LoadMessageTable("text/fra_message_data_static/fra_message_data_static", false, false);
    if (sJpnMessageEntryTablePtr == nullptr)
        sJpnMessageEntryTablePtr =
            Ps3LoadMessageTable("text/jpn_message_data_static/jpn_message_data_static", true, false);
    // After the PAL table, so message 0xFFFC comes from it when present (as in SoH).
    if (sNesMessageEntryTablePtr == nullptr)
        sNesMessageEntryTablePtr =
            Ps3LoadMessageTable("text/nes_message_data_static/ntsc_nes_message_data_static", false, true);
    if (sStaffMessageEntryTablePtr == nullptr)
        sStaffMessageEntryTablePtr =
            Ps3LoadMessageTable("text/staff_message_data_static/staff_message_data_static", false, false);
}

static bool DecodeRoomCommands(Resource* resource, unsigned depth) {
    if (resource == nullptr || resource->data.size() < 4 || depth > 1) return false;
    Reader reader(resource->data, 0, resource->bigEndian);
    const uint32_t count = reader.ReadU32();
    if (count == 0 || count > 128) return false;
    for (uint32_t i = 0; i < count; ++i) {
        if (!reader.Can(4)) return false;
        const uint32_t id = reader.ReadU32();
        size_t fixed = 0, width = 0;
        switch (id) {
            case 0x00: case 0x01: width = 16; break;
            case 0x06: case 0x0B: case 0x13: width = 2; break;
            case 0x0C: width = 13; break;
            case 0x0E: width = 16; break;
            case 0x0F: width = 22; break;
            case 0x02: case 0x08: case 0x19: fixed = 5; break;
            case 0x05: case 0x11: fixed = 4; break;
            case 0x07: case 0x15: fixed = 3; break;
            case 0x10: {
                if (!reader.Can(3)) return false;
                sPs3RoomTime[0] = reader.U8();
                sPs3RoomTime[1] = reader.U8();
                sPs3RoomTime[2] = reader.U8();
                sPs3RoomTimeValid = true;
                char line[96];
                snprintf(line, sizeof(line), "room-mesh: time settings hour=%u minute=%u increment=%u",
                         sPs3RoomTime[0], sPs3RoomTime[1], sPs3RoomTime[2]);
                oot_ps3_early_trace(line);
                continue;
            }
            case 0x12: fixed = 2; break;
            case 0x16: fixed = 1; break;
            case 0x14: break;
            case 0x0A: {
                Ps3RoomMesh& mesh = ActiveRoomMesh();
                if (!ReadRoomMesh(reader, &mesh)) return false;
                mesh.segment = resource->data.data();
                mesh.ready = true;
                oot_ps3_early_trace("room-mesh: decoded upstream room display-list paths");
                return true;
            }
            case 0x03: case 0x17:
                if (!reader.SceneString(nullptr)) return false;
                continue;
            case 0x0D: case 0x18: {
                if (!reader.Can(4)) return false;
                const uint32_t entries = reader.ReadU32();
                if (entries > 128) return false;
                std::string alternate;
                for (uint32_t entry = 0; entry < entries; ++entry) {
                    std::string path;
                    if (!reader.SceneString(&path)) return false;
                    if (id == 0x18 && gSaveContext.sceneLayer != 0 &&
                        entry == static_cast<uint32_t>(gSaveContext.sceneLayer - 1)) alternate = path;
                }
                if (!alternate.empty() && depth == 0) {
                    Resource* selected = LoadName(alternate.c_str());
                    if (selected != nullptr) return DecodeRoomCommands(selected, depth + 1);
                }
                continue;
            }
            case 0x04: {
                if (!reader.Can(4)) return false;
                const uint32_t entries = reader.ReadU32();
                if (entries > 128) return false;
                for (uint32_t entry = 0; entry < entries; ++entry)
                    if (!reader.SceneString(nullptr) || !reader.Skip(8)) return false;
                continue;
            }
            default:
                oot_ps3_early_trace("room-mesh: unsupported room command; room draw deferred");
                return false;
        }
        if (width != 0) {
            if (!reader.Can(4)) return false;
            const uint32_t entries = reader.ReadU32();
            if (entries > 128 || entries > (resource->data.size() - reader.Offset()) / width) return false;
            if (!reader.Skip(static_cast<size_t>(entries) * width)) return false;
        } else if (!reader.Skip(fixed)) return false;
    }
    oot_ps3_early_trace("room-mesh: selected room has no supported mesh command");
    return false;
}

// The scene body uses Shipwright's SceneFactory V0 format. Probe only the
// bounded serialized commands here; a Scene is not a raw N64 command stream and
// must not be passed to Scene_ExecuteCommands or Actor_InitContext.
static int ProbeSceneResource(const char* name, int spawn, ActorEntry* selectedEntry,
                              int* selectedRoom, unsigned depth) {
    if (selectedEntry == nullptr || selectedRoom == nullptr) return 0;
    *selectedEntry = {};
    *selectedRoom = -1;
    Resource* resource = LoadName(name);
    if (resource == nullptr) {
        oot_ps3_early_trace("scene-probe: scene resource missing or invalid");
        return 0;
    }
    const std::vector<unsigned char>& bytes = resource->data;
    Reader reader(bytes, 0, resource->bigEndian);
    if (!reader.Can(4)) {
        oot_ps3_early_trace("scene-probe: truncated scene header");
        return 0;
    }
    const uint32_t commands = reader.ReadU32();
    char line[160];
    snprintf(line, sizeof(line), "scene-probe: type=0x%lx commands=%lu spawn=%d bytes=%lu",
                  static_cast<unsigned long>(resource->type),
                  static_cast<unsigned long>(commands), spawn,
                  static_cast<unsigned long>(bytes.size()));
    oot_ps3_early_trace(line);
    if (commands == 0 || commands > 128) {
        oot_ps3_early_trace("scene-probe: invalid scene command count");
        return 0;
    }
    size_t positionsOffset = 0;
    uint32_t positionsCount = 0;
    unsigned selectedSpawn = 0;
    int roomIndex = -1;
    bool entranceFound = false;
    std::vector<std::string> roomPaths;
    for (uint32_t i = 0; i < commands; ++i) {
        if (!reader.Can(4)) {
            oot_ps3_early_trace("scene-probe: truncated command ID");
            return 0;
        }
        const uint32_t id = reader.ReadU32();
        snprintf(line, sizeof(line), "scene-probe: command=%lu id=0x%lx offset=%lu",
                 static_cast<unsigned long>(i), static_cast<unsigned long>(id),
                 static_cast<unsigned long>(reader.Offset() - 4));
        oot_ps3_early_trace(line);
        size_t fixedBytes = 0;
        size_t entryBytes = 0;
        switch (id) {
            case 0x00: case 0x01: entryBytes = 16; break; // spawn / actor lists
            case 0x06: entryBytes = 2; break;  // entrances
            case 0x0B: case 0x13: entryBytes = 2; break; // objects / exits
            case 0x0C: entryBytes = 13; break; // lights
            case 0x0E: entryBytes = 16; break; // transition actors
            case 0x0F: entryBytes = 22; break; // lighting settings
            case 0x02: case 0x08: case 0x19: fixedBytes = 5; break;
            case 0x05: case 0x11: fixedBytes = 4; break;
            case 0x07: case 0x10: case 0x15: fixedBytes = 3; break;
            case 0x12: fixedBytes = 2; break;
            case 0x16: fixedBytes = 1; break;
            case 0x14: fixedBytes = 0; break; // end marker
            case 0x04: { // SetRoomListFactory: count, then path and VROM bounds.
                if (!reader.Can(4)) {
                    oot_ps3_early_trace("scene-probe: room count truncated");
                    return 0;
                }
                const uint32_t rooms = reader.ReadU32();
                roomPaths.reserve(rooms < 128 ? rooms : 0);
                snprintf(line, sizeof(line), "scene-probe: room-list count=%lu", static_cast<unsigned long>(rooms));
                oot_ps3_early_trace(line);
                if (rooms > 128) {
                    oot_ps3_early_trace("scene-probe: invalid room count");
                    return 0;
                }
                for (uint32_t room = 0; room < rooms; ++room) {
                    std::string path;
                    if (!reader.SceneString(&path) || !reader.Skip(8)) {
                        oot_ps3_early_trace("scene-probe: room path or VROM data truncated");
                        return 0;
                    }
                    roomPaths.push_back(path);
                    if (room == 0) {
                        snprintf(line, sizeof(line), "scene-probe: first room=%s", path.c_str());
                        oot_ps3_early_trace(line);
                    }
                }
                continue;
            }
            case 0x03: case 0x17: { // collision or cutscene resource path
                std::string path;
                if (!reader.SceneString(&path)) {
                    oot_ps3_early_trace("scene-probe: resource path truncated");
                    return 0;
                }
                snprintf(line, sizeof(line), "scene-probe: referenced resource=%s", path.c_str());
                oot_ps3_early_trace(line);
                if (id == 0x03) {
                    // Only remember the path: loading here can reallocate the resource
                    // list and invalidate the scene resource this walker is reading.
                    snprintf(sPs3CollisionPath, sizeof(sPs3CollisionPath), "%s", path.c_str());
                }
                if (id == 0x17) {
                    Resource* cutscene = LoadName(path.c_str());
                    if (cutscene == nullptr) {
                        oot_ps3_early_trace("intro-route: title cutscene resource missing");
                    } else {
                        snprintf(line, sizeof(line),
                                 "intro-route: title cutscene O2R type=0x%lx version=%lu bytes=%lu",
                                 static_cast<unsigned long>(cutscene->type),
                                 static_cast<unsigned long>(cutscene->version),
                                 static_cast<unsigned long>(cutscene->data.size()));
                        oot_ps3_early_trace(line);
                        if (cutscene->data.size() >= 12) {
                            Reader script(cutscene->data, 0, cutscene->bigEndian);
                            const unsigned long entries = script.ReadU32();
                            const unsigned long commands = script.ReadU32();
                            const unsigned long endFrame = script.ReadU32();
                            snprintf(line, sizeof(line),
                                     "intro-route: title cutscene serialized entries=%lu commands=%lu end-frame=%lu",
                                     entries, commands, endFrame);
                            oot_ps3_early_trace(line);
                            Ps3ConvertTitleCutscene(cutscene->data.data(), cutscene->data.size(),
                                                    cutscene->bigEndian);
                        }
                    }
                }
                continue;
            }
            case 0x18: { // Shipwright title screen selects sceneLayer 7.
                if (!reader.Can(4)) return 0;
                const uint32_t count = reader.ReadU32();
                if (count > 128) return 0;
                const unsigned layer = gSaveContext.sceneLayer;
                std::string selectedHeader;
                for (uint32_t index = 0; index < count; ++index) {
                    std::string candidate;
                    if (!reader.SceneString(&candidate)) return 0;
                    if (layer != 0 && index == layer - 1) selectedHeader = candidate;
                }
                if (!selectedHeader.empty() && depth == 0) {
                    snprintf(line, sizeof(line), "intro-route: scene layer %u archive header=%s",
                             layer, selectedHeader.c_str());
                    oot_ps3_early_trace(line);
                    return ProbeSceneResource(selectedHeader.c_str(), spawn,
                                              selectedEntry, selectedRoom, depth + 1);
                }
                oot_ps3_early_trace(layer == 0 ?
                    "intro-route: using scene base header" :
                    "intro-route: requested alternate scene header absent; using base");
                continue;
            }
            case 0x0D: { // pathways
                if (!reader.Can(4)) {
                    oot_ps3_early_trace("scene-probe: string list count truncated");
                    return 0;
                }
                const uint32_t count = reader.ReadU32();
                if (count > 128) return 0;
                for (uint32_t index = 0; index < count; ++index) {
                    if (!reader.SceneString(nullptr)) {
                        oot_ps3_early_trace("scene-probe: string list truncated");
                        return 0;
                    }
                }
                continue;
            }
            case 0x0A:
                if (!SkipSceneMesh(reader)) {
                    oot_ps3_early_trace("scene-probe: mesh layout rejected");
                    return 0;
                }
                oot_ps3_early_trace("scene-probe: mesh command parsed without drawing");
                continue;
            default:
                oot_ps3_early_trace("scene-probe: unsupported command ID");
                return 0;
        }
        if (entryBytes != 0) {
            if (!reader.Can(4)) {
                oot_ps3_early_trace("scene-probe: truncated list count");
                return 0;
            }
            const uint32_t count = reader.ReadU32();
            if (count > 128 || count > (bytes.size() - reader.Offset()) / entryBytes) {
                oot_ps3_early_trace("scene-probe: invalid list length");
                return 0;
            }
            if (id == 0x00) { positionsOffset = reader.Offset(); positionsCount = count; }
            if (id == 0x06 && spawn >= 0 && static_cast<uint32_t>(spawn) < count) {
                selectedSpawn = bytes[reader.Offset() + static_cast<size_t>(spawn) * 2];
                roomIndex = bytes[reader.Offset() + static_cast<size_t>(spawn) * 2 + 1];
                entranceFound = true;
            }
            snprintf(line, sizeof(line), "scene-probe: list id=0x%lx entries=%lu",
                     static_cast<unsigned long>(id), static_cast<unsigned long>(count));
            oot_ps3_early_trace(line);
            if (id == 0x0F) {
                Reader lr = reader;
                const uint32_t n = count < 32U ? count : 32U;
                for (uint32_t e = 0; e < n; ++e) {
                    EnvLightSettings& L = sPs3LightSettings[e];
                    for (int c = 0; c < 3; ++c) L.ambientColor[c] = lr.U8();
                    for (int c = 0; c < 3; ++c) L.light1Dir[c] = static_cast<s8>(lr.U8());
                    for (int c = 0; c < 3; ++c) L.light1Color[c] = lr.U8();
                    for (int c = 0; c < 3; ++c) L.light2Dir[c] = static_cast<s8>(lr.U8());
                    for (int c = 0; c < 3; ++c) L.light2Color[c] = lr.U8();
                    for (int c = 0; c < 3; ++c) L.fogColor[c] = lr.U8();
                    L.fogNear = static_cast<s16>(lr.U16());
                    L.fogFar = static_cast<s16>(lr.U16());
                }
                sPs3LightSettingsCount = n;
                if (n > 0) {
                    const EnvLightSettings& L = sPs3LightSettings[n > 3 ? 3 : 0];
                    snprintf(line, sizeof(line),
                             "trace: scene lighting decoded count=%lu sample ambient=(%u,%u,%u) light1=(%u,%u,%u) "
                             "fog=(%u,%u,%u) near=%d far=%d",
                             static_cast<unsigned long>(n), L.ambientColor[0], L.ambientColor[1], L.ambientColor[2],
                             L.light1Color[0], L.light1Color[1], L.light1Color[2],
                             L.fogColor[0], L.fogColor[1], L.fogColor[2], L.fogNear & 0x3FF, L.fogFar);
                    oot_ps3_early_trace(line);
                }
            }
            if (!reader.Skip(static_cast<size_t>(count) * entryBytes)) return 0;
        } else if (!reader.Skip(fixedBytes)) {
            oot_ps3_early_trace("scene-probe: truncated fixed command");
            return 0;
        }
    }
    if (positionsCount && entranceFound && selectedSpawn < positionsCount) {
        Reader position(bytes, positionsOffset + static_cast<size_t>(selectedSpawn) * 16, resource->bigEndian);
        selectedEntry->id = position.U16();
        selectedEntry->pos.x = static_cast<int16_t>(position.U16());
        selectedEntry->pos.y = static_cast<int16_t>(position.U16());
        selectedEntry->pos.z = static_cast<int16_t>(position.U16());
        selectedEntry->rot.x = static_cast<int16_t>(position.U16());
        selectedEntry->rot.y = static_cast<int16_t>(position.U16());
        selectedEntry->rot.z = static_cast<int16_t>(position.U16());
        selectedEntry->params = position.U16();
        *selectedRoom = roomIndex;
        snprintf(line, sizeof(line), "scene-probe: player spawn actor=%u position=(%d,%d,%d)",
                 static_cast<unsigned>(selectedEntry->id), selectedEntry->pos.x,
                 selectedEntry->pos.y, selectedEntry->pos.z);
        oot_ps3_early_trace(line);
    } else {
        oot_ps3_early_trace("scene-probe: spawn or entrance list unavailable");
    }
    if (*selectedRoom < 0) return 0;
    if (static_cast<size_t>(*selectedRoom) < roomPaths.size()) {
        const std::string& roomPath = roomPaths[static_cast<size_t>(*selectedRoom)];
        snprintf(line, sizeof(line), "intro-route: selected O2R room=%s", roomPath.c_str());
        oot_ps3_early_trace(line);
        Resource* room = LoadName(roomPath.c_str());
        if (room != nullptr && room->data.size() >= 4) {
            Reader roomReader(room->data, 0, room->bigEndian);
            const uint32_t roomCommands = roomReader.ReadU32();
            snprintf(line, sizeof(line), "intro-route: room resource type=0x%lx commands=%lu bytes=%lu",
                     static_cast<unsigned long>(room->type),
                     static_cast<unsigned long>(roomCommands),
                     static_cast<unsigned long>(room->data.size()));
            oot_ps3_early_trace(line);
            ActiveRoomMesh().ready = false;
            if (!DecodeRoomCommands(room, 0))
                oot_ps3_early_trace("room-mesh: unable to decode selected room; draw remains deferred");
        } else {
            oot_ps3_early_trace("intro-route: selected O2R room unavailable");
        }
    }
    oot_ps3_early_trace("scene-probe: selected O2R player spawn and room decoded");
    return 1;
}

extern "C" int oot_ps3_probe_scene_resource(const char* name, int spawn,
                                            ActorEntry* selectedEntry, int* selectedRoom) {
    return ProbeSceneResource(name, spawn, selectedEntry, selectedRoom, 0);
}

extern "C" int oot_ps3_install_selected_room_mesh(PlayState* play) {
    if (play == nullptr || !ActiveRoomMesh().ready) return 0;
    Ps3RoomMesh& mesh = ActiveRoomMesh();
    play->roomCtx.curRoom.meshHeader = &mesh.header;
    play->roomCtx.curRoom.segment = mesh.segment;
    oot_ps3_early_trace("room-mesh: selected O2R mesh installed for upstream Room_Draw");
    return 1;
}

extern "C" void InitOTR(int argc, char* argv[]) { (void)argc; (void)argv; Initialize(); }
extern "C" void DeinitOTR(void) { Resources().clear(); Archives().clear(); Versions().clear(); sInitialized = false; }
extern "C" uint32_t IsResourceManagerLoaded(void) { Initialize(); return !Archives().empty(); }
extern "C" uint64_t ResourceGetCrcByName(const char* name) { const std::string p = Normalize(name); return ShipwrightProbe::ShipCrc64Name(p.c_str()); }
extern "C" const char* ResourceGetNameByCrc(uint64_t crc) { Initialize(); for (const Archive& a : Archives()) { const ZipEntry* e = FindEntry(a, crc); if (e) return e->name.c_str(); } return nullptr; }
extern "C" void* ResourceGetDataByName(const char* name) { Resource* r = LoadName(name); return r && !r->data.empty() ? r->data.data() : nullptr; }
extern "C" void oot_ps3_trace_resource_address(const void* pointer) {
    const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
    for (const Resource& resource : Resources()) {
        if (resource.data.empty()) continue;
        const uintptr_t begin = reinterpret_cast<uintptr_t>(resource.data.data());
        if (address >= begin && address - begin <= resource.data.size()) {
            char line[512];
            snprintf(line, sizeof(line), "fast3d-resource: command owner=%s offset=%lu bytes=%lu type=%08lx",
                     resource.name.c_str(), static_cast<unsigned long>(address - begin),
                     static_cast<unsigned long>(resource.data.size()), static_cast<unsigned long>(resource.type));
            oot_ps3_early_trace(line);
            return;
        }
    }
    oot_ps3_early_trace("fast3d-resource: command address is outside cached archive resources");
}
extern "C" void* ResourceGetDataByCrc(uint64_t crc) { Resource* r = LoadHash(crc); return r && !r->data.empty() ? r->data.data() : nullptr; }
extern "C" size_t ResourceGetSizeByName(const char* name) { Resource* r = LoadName(name); return r ? r->data.size() : 0; }
extern "C" size_t ResourceGetSizeByCrc(uint64_t crc) { Resource* r = LoadHash(crc); return r ? r->data.size() : 0; }
extern "C" uint8_t ResourceGetIsCustomByName(const char* name) { Resource* r = LoadName(name); return r && r->custom; }
extern "C" uint8_t ResourceGetIsCustomByCrc(uint64_t crc) { Resource* r = LoadHash(crc); return r && r->custom; }
extern "C" uint16_t ResourceGetTexWidthByName(const char* name) { Resource* r = LoadName(name); return r ? r->width : 0; }
extern "C" uint16_t ResourceGetTexWidthByCrc(uint64_t crc) { Resource* r = LoadHash(crc); return r ? r->width : 0; }
extern "C" uint16_t ResourceGetTexHeightByName(const char* name) { Resource* r = LoadName(name); return r ? r->height : 0; }
extern "C" uint16_t ResourceGetTexHeightByCrc(uint64_t crc) { Resource* r = LoadHash(crc); return r ? r->height : 0; }
extern "C" uint32_t ResourceGetTexTypeByName(const char* name) { Resource* r = LoadName(name); return r ? r->textureType : 0; }
extern "C" uint32_t ResourceGetTexFlagsByName(const char* name) { Resource* r = LoadName(name); return r ? r->textureFlags : 0; }
extern "C" size_t ResourceGetTexSizeByName(const char* name) { return ResourceGetSizeByName(name); }
extern "C" size_t ResourceGetTexSizeByCrc(uint64_t crc) { return ResourceGetSizeByCrc(crc); }
extern "C" void ResourceLoadDirectory(const char* name) { (void)name; Initialize(); }
extern "C" void ResourceLoadDirectoryAsync(const char* name) { ResourceLoadDirectory(name); }
extern "C" void ResourceDirtyDirectory(const char* name) { (void)name; }
extern "C" void ResourceDirtyByName(const char* name) { (void)name; }
extern "C" void ResourceDirtyByCrc(uint64_t crc) { (void)crc; }
extern "C" void ResourceUnloadByName(const char* name) { const uint64_t hash = ResourceGetCrcByName(name); ResourceList& r = Resources(); r.erase(std::remove_if(r.begin(), r.end(), [hash](const Resource& v) { return v.hash == hash; }), r.end()); sResourceIndexStale = true; }
extern "C" void ResourceUnloadByCrc(uint64_t crc) { ResourceList& r = Resources(); r.erase(std::remove_if(r.begin(), r.end(), [crc](const Resource& v) { return v.hash == crc; }), r.end()); }
extern "C" void ResourceUnloadDirectory(const char* name) { (void)name; }
extern "C" void ResourceClearCache(void) { Resources().clear(); }
extern "C" void ResourceGetGameVersions(uint32_t* versions, size_t capacity, size_t* count) { Initialize(); VersionList& v = Versions(); if (count) *count = v.size(); if (versions) for (size_t i = 0; i < std::min(capacity, v.size()); ++i) versions[i] = v[i]; }
extern "C" uint32_t ResourceHasGameVersion(uint32_t hash) { Initialize(); VersionList& v = Versions(); return std::find(v.begin(), v.end(), hash) != v.end(); }

extern "C" uint32_t ResourceMgr_GetNumGameVersions(void) { Initialize(); return Versions().size(); }
extern "C" uint32_t ResourceMgr_GetGameVersion(int index) { Initialize(); VersionList& v = Versions(); return index >= 0 && static_cast<size_t>(index) < v.size() ? v[index] : 0xFFFFFFFFU; }
extern "C" uint32_t ResourceMgr_GetGamePlatform(int index) { (void)index; return GAME_PLATFORM_N64; }
extern "C" uint32_t ResourceMgr_GetGameRegion(int index) { const uint32_t v = ResourceMgr_GetGameVersion(index); return v == 0xFFFFFFFFU ? GAME_REGION_UNKNOWN : (IsPal(v) ? GAME_REGION_PAL : GAME_REGION_NTSC); }
extern "C" bool ResourceMgr_IsPalLoaded(void) { return ResourceMgr_GetNumGameVersions() && ResourceMgr_GetGameRegion(0) == GAME_REGION_PAL; }
extern "C" uint32_t ResourceMgr_GameHasMasterQuest(void) { return 0; }
extern "C" uint32_t ResourceMgr_GameHasOriginal(void) { return ResourceMgr_GetNumGameVersions() != 0; }
extern "C" uint32_t ResourceMgr_IsSceneMasterQuest(s16 sceneNum) { (void)sceneNum; return 0; }
extern "C" uint32_t ResourceMgr_IsGameMasterQuest(void) { return 0; }
extern "C" void ResourceMgr_LoadDirectory(const char* name) { ResourceLoadDirectory(name); }
extern "C" void ResourceMgr_DirtyDirectory(const char* name) { ResourceDirtyDirectory(name); }
extern "C" void ResourceMgr_UnloadResource(const char* name) { ResourceUnloadByName(name); }
// Archive entries whose name starts with the part of `mask` before the first '*'
// (AudioLoad_Init asks for "audio/sequences*" and "audio/fonts*"). The game frees
// the array and every string, so both come from malloc.
extern "C" char** ResourceMgr_ListFiles(const char* mask, int* count) {
    if (count) *count = 0;
    if (mask == nullptr) return nullptr;
    Initialize();
    std::string prefix(mask);
    const size_t star = prefix.find('*');
    if (star != std::string::npos) prefix.resize(star);
    std::vector<std::string> names;
    for (const Archive& archive : Archives()) {
        for (const ZipEntry& entry : archive.entries) {
            if (entry.name.compare(0, prefix.size(), prefix) == 0) names.push_back(entry.name);
        }
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    if (names.empty()) return nullptr;
    char** list = static_cast<char**>(malloc(names.size() * sizeof(char*)));
    if (list == nullptr) return nullptr;
    for (size_t i = 0; i < names.size(); ++i) list[i] = strdup(names[i].c_str());
    if (count) *count = static_cast<int>(names.size());
    char line[160];
    snprintf(line, sizeof(line), "trace: ResourceMgr_ListFiles %s -> %lu files", mask, (unsigned long)names.size());
    oot_ps3_early_trace(line);
    return list;
}
extern "C" uint8_t ResourceMgr_FileExists(const char* name) { Initialize(); const uint64_t h = ResourceGetCrcByName(name); for (const Archive& a : Archives()) if (FindEntry(a, h)) return 1; return 0; }
extern "C" uint8_t ResourceMgr_FileAltExists(const char* name) { const std::string p = "alt/" + Normalize(name); return ResourceMgr_FileExists(p.c_str()); }
extern "C" bool ResourceMgr_IsAltAssetsEnabled(void) { return false; }
extern "C" void ResourceMgr_UnloadOriginalWhenAltExists(const char* name) { (void)name; }
extern "C" char* ResourceMgr_GetResourceDataByNameHandlingMQ(const char* path) { return static_cast<char*>(ResourceGetDataByName(path)); }
extern "C" uint8_t ResourceMgr_TexIsRaw(const char* path) { Resource* r = LoadName(path); return r ? (r->textureFlags & 1U) != 0 : 0; }
extern "C" uint8_t ResourceMgr_ResourceIsBackground(char* path) { Resource* r = LoadName(path); return r && r->type == static_cast<uint32_t>(SOH::ResourceType::SOH_Background); }
extern "C" unsigned char* oot_ps3_jpeg_decode_rgba(const unsigned char* data, int size, int* width, int* height);
extern "C" void oot_ps3_jpeg_free(unsigned char* pixels);
// SoH ResourceManagerHelpers.cpp: decode to RGBA5551, high byte first, into a
// shared buffer that swapAndConvertJPEG copies back over the background data.
extern "C" char* ResourceMgr_LoadJPEG(char* data, size_t size) {
    static std::vector<char> decoded;
    decoded.assign(size, 0);
    int width = 0;
    int height = 0;
    unsigned char* pixels = oot_ps3_jpeg_decode_rgba(reinterpret_cast<const unsigned char*>(data),
                                                     static_cast<int>(size), &width, &height);
    if (pixels == nullptr) {
        oot_ps3_early_trace("trace: background JPEG decode FAILED");
        return decoded.data();
    }
    size_t out = 0;
    for (int i = 0; i < width * height && out + 1 < size; ++i) {
        const unsigned char* p = pixels + i * 4;
        const uint16_t value = static_cast<uint16_t>(((p[0] / 8) << 11) | ((p[1] / 8) << 6) | ((p[2] / 8) << 1) |
                                                     (p[3] != 0 ? 1 : 0));
        decoded[out++] = static_cast<char>(value >> 8);
        decoded[out++] = static_cast<char>(value & 0xFF);
    }
    oot_ps3_jpeg_free(pixels);
    char line[96];
    snprintf(line, sizeof(line), "trace: background JPEG decoded %dx%d", width, height);
    oot_ps3_early_trace(line);
    return decoded.data();
}
extern "C" uint16_t ResourceMgr_LoadTexWidthByName(char* path) { return ResourceGetTexWidthByName(path); }
extern "C" uint16_t ResourceMgr_LoadTexHeightByName(char* path) { return ResourceGetTexHeightByName(path); }
extern "C" char* ResourceMgr_LoadTexOrDListByName(const char* path) { return static_cast<char*>(ResourceGetDataByName(path)); }
extern "C" char* ResourceMgr_LoadIfDListByName(const char* path) { Resource* r = LoadName(path); return r && r->type == static_cast<uint32_t>(Fast::ResourceType::DisplayList) ? reinterpret_cast<char*>(r->data.data()) : nullptr; }
extern "C" char* ResourceMgr_LoadPlayerAnimByName(const char* path) { return static_cast<char*>(ResourceGetDataByName(path)); }
extern "C" Gfx* ResourceMgr_LoadGfxByName(const char* path) { return static_cast<Gfx*>(ResourceGetDataByName(path)); }
extern "C" Gfx* ResourceMgr_LoadGfxByCRC(uint64_t crc) { return static_cast<Gfx*>(ResourceGetDataByCrc(crc)); }
extern "C" Vtx* ResourceMgr_LoadVtxByName(char* path) { return static_cast<Vtx*>(ResourceGetDataByName(path)); }
extern "C" Vtx* ResourceMgr_LoadVtxByCRC(uint64_t crc) { return static_cast<Vtx*>(ResourceGetDataByCrc(crc)); }
// Actors load their own collision meshes and cutscenes by name: hand them the
// converted native structures (as the scene loader does), never the raw O2R bytes.
static CollisionHeader* Ps3CollisionForName(const char* path);
static void* Ps3CutsceneForName(const char* path);
extern "C" CollisionHeader* ResourceMgr_LoadColByName(const char* path) { return Ps3CollisionForName(path); }
extern "C" AnimationHeaderCommon* ResourceMgr_LoadAnimByName(const char* path) { return static_cast<AnimationHeaderCommon*>(ResourceGetDataByName(path)); }
extern "C" SkeletonHeader* ResourceMgr_LoadSkeletonByName(const char* path, SkelAnime* skel) { (void)skel; return static_cast<SkeletonHeader*>(ResourceGetDataByName(path)); }
extern "C" void ResourceMgr_UnregisterSkeleton(SkelAnime* skel) { (void)skel; }
extern "C" void ResourceMgr_ClearSkeletons(void) {}
extern "C" s32* ResourceMgr_LoadCSByName(const char* path) { return static_cast<s32*>(Ps3CutsceneForName(path)); }
extern "C" char* ResourceMgr_LoadArrayByName(const char* path) { return static_cast<char*>(ResourceGetDataByName(path)); }
// SoH (ResourceManagerHelpers.cpp) returns a malloc'd Vec3s copy that the caller frees:
// Player_DrawPause free()s it every pause frame. Handing out the resource's own buffer
// let that free() wreck the heap (pause menu froze inside malloc). Scalar arrays are
// kept as raw O2R body bytes, so decode them here (ArrayFactory V0 layout:
// arrayType, count, then per element scalarType [, n for Vector], n scalars).
extern "C" char* ResourceMgr_LoadArrayByNameAsVec3s(const char* path) {
    constexpr uint32_t kArrayScalar = 16, kArrayVector = 24, kScalarS16 = 4, kScalarU16 = 5;
    std::vector<s16> scalars;
    bool ok = false;
    uint32_t arrayType = 0, count = 0;
    Resource* r = LoadName(path);
    if (r != nullptr && r->type == 0x4F415252U) {
        Reader body(r->data, 0, r->bigEndian);
        arrayType = body.ReadU32();
        count = body.ReadU32();
        ok = (arrayType == kArrayScalar || arrayType == kArrayVector) && count <= 4096U;
        for (uint32_t i = 0; ok && i < count; ++i) {
            if (!body.Can(4)) { ok = false; break; }
            const uint32_t scalarType = body.ReadU32();
            uint32_t n = 1;
            if (arrayType == kArrayVector) {
                if (!body.Can(4)) { ok = false; break; }
                n = body.ReadU32();
                if (n > 16U) { ok = false; break; }
            }
            for (uint32_t k = 0; k < n; ++k) {
                if (scalarType == kScalarS16 || scalarType == kScalarU16) {
                    if (!body.Can(2)) { ok = false; break; }
                    scalars.push_back(static_cast<s16>(body.U16()));
                } else {
                    scalars.push_back(0); // SoH reads nothing for other scalar types either
                }
            }
        }
    }
    static int ps3Vec3sLogged = 0;
    if (ps3Vec3sLogged < 8 || !ok) {
        char line[256];
        snprintf(line, sizeof(line), "trace: Vec3s array %s type=%lu count=%lu values=%lu first=(%d,%d,%d) name=%s",
                 ok ? "decoded" : "FAILED", static_cast<unsigned long>(arrayType), static_cast<unsigned long>(count),
                 static_cast<unsigned long>(scalars.size()), scalars.size() > 0 ? scalars[0] : 0,
                 scalars.size() > 1 ? scalars[1] : 0, scalars.size() > 2 ? scalars[2] : 0, path ? path : "(null)");
        oot_ps3_early_trace(line);
        ++ps3Vec3sLogged;
    }
    // The caller copies one Vec3s per limb without a bound: never return null, and keep
    // room for a full skeleton (zero pose) if the data is missing or malformed.
    const size_t values = ok ? scalars.size() : 0;
    const size_t bytes = (values > 3U * 64U ? values : 3U * 64U) * sizeof(s16);
    s16* copy = static_cast<s16*>(std::calloc(1, bytes));
    if (copy != nullptr && values > 0) std::memcpy(copy, scalars.data(), values * sizeof(s16));
    return reinterpret_cast<char*>(copy);
}
// Audio resources go through converters (raw O2R bytes are never native structs, rule 16).
static SoundFontSample* Ps3AudioSampleForName(const char* path);
static SoundFont* Ps3AudioFontForName(const char* path);
static SequenceData* Ps3AudioSeqForName(const char* path);
extern "C" SoundFontSample* ResourceMgr_LoadAudioSample(const char* path) { return Ps3AudioSampleForName(path); }
extern "C" SoundFont* ResourceMgr_LoadAudioSoundFontByName(const char* path) { return Ps3AudioFontForName(path); }
extern "C" SequenceData* ResourceMgr_LoadSeqPtrByName(const char* path) { return Ps3AudioSeqForName(path); }
extern "C" SequenceData ResourceMgr_LoadSeqByName(const char* path) { SequenceData empty = {}; SequenceData* value = ResourceMgr_LoadSeqPtrByName(path); return value ? *value : empty; }
extern "C" uint8_t ResourceMgr_FileIsCustomByName(const char* path) { return ResourceGetIsCustomByName(path); }
extern "C" char* ResourceMgr_GetNameByCRC(uint64_t crc, char* alloc) { const char* name = ResourceGetNameByCrc(crc); if (!name) return nullptr; if (alloc) { std::strcpy(alloc, name); return alloc; } return const_cast<char*>(name); }
extern "C" int ResourceMgr_OTRSigCheck(char* data) { return data && (reinterpret_cast<uintptr_t>(data) & 1U) == 0 && std::memcmp(data, "__OTR__", 7) == 0; }
extern "C" void ResourceMgr_PatchGfxByName(const char* path, const char* patch, int index, Gfx instruction) { (void)patch; Resource* r = LoadName(path); if (r && index >= 0 && (static_cast<size_t>(index) + 1) * sizeof(Gfx) <= r->data.size()) reinterpret_cast<Gfx*>(r->data.data())[index] = instruction; }
extern "C" void ResourceMgr_PatchCustomGfxByName(const char* path, const char* patch, int index, Gfx instruction) { ResourceMgr_PatchGfxByName(path, patch, index, instruction); }
extern "C" void ResourceMgr_UnpatchGfxByName(const char* path, const char* patch) { (void)patch; ResourceUnloadByName(path); }

// ---- PS3 general scene loader: mirrors soh/z_play_otr.cpp and soh/z_scene_otr.cpp ----
// Scene and room resources are decoded once into native structures (the layouts
// of Shipwright's scenecommand factories) and kept for the rest of the run, so
// pointers handed to the game (actor lists, room files, meshes) stay valid.
extern "C" RomFile sNaviMsgFiles[];
extern "C" s32 Object_Spawn(ObjectContext* objectCtx, s16 objectId);
extern "C" uint8_t D_8015FCC8;

namespace {

struct Ps3Mesh {
    MeshHeader header = {};
    std::vector<PolygonDlist> normal;
    std::vector<PolygonDlist2> sorted;
    std::vector<BgImage> images;
    PolygonDlist bgDlist = {};
    std::deque<std::string> paths; // "__OTR__..." strings; deque keeps c_str() stable
};

struct Ps3SceneCmd {
    uint32_t id = 0;
    uint8_t raw[8] = {};
    std::vector<ActorEntry> actors;
    std::vector<EntranceEntry> entrances;
    std::vector<s16> shorts;
    std::vector<LightInfo> lights;
    std::vector<TransitionActorEntry> transitions;
    std::vector<EnvLightSettings> lightSettings;
    std::vector<RomFile> rooms;
    std::vector<std::string> names; // room files, alternate headers, path files
    std::vector<Path> paths;
    std::vector<std::vector<Vec3s>> pathPoints;
    std::string file; // collision header or cutscene
    Ps3Mesh* mesh = nullptr;
};

struct Ps3Scene {
    std::string name;
    std::vector<Ps3SceneCmd> cmds;
    std::vector<Ps3Scene*> alternates; // index = scene layer - 1; nullptr when absent
};

std::vector<Ps3Scene*>& LoadedScenes() {
    static std::vector<Ps3Scene*> scenes;
    return scenes;
}

void SceneTrace(const char* what, const std::string& detail) {
    char line[320];
    snprintf(line, sizeof(line), "scene-load: %s %s", what, detail.c_str());
    oot_ps3_early_trace(line);
}

void ReadActorEntries(Reader& r, std::vector<ActorEntry>* out) {
    const uint32_t n = r.ReadU32();
    for (uint32_t i = 0; i < n && r.Can(16); ++i) {
        ActorEntry e;
        e.id = static_cast<s16>(r.U16());
        e.pos.x = static_cast<s16>(r.U16());
        e.pos.y = static_cast<s16>(r.U16());
        e.pos.z = static_cast<s16>(r.U16());
        e.rot.x = static_cast<s16>(r.U16());
        e.rot.y = static_cast<s16>(r.U16());
        e.rot.z = static_cast<s16>(r.U16());
        e.params = static_cast<s16>(r.U16());
        out->push_back(e);
    }
}

const char* OwnPath(Ps3Mesh* mesh, const std::string& path) {
    mesh->paths.push_back("__OTR__" + path);
    return mesh->paths.back().c_str();
}

// SetMeshFactory: types 0 (normal), 1 (pre-rendered background), 2 (sorted).
bool ReadMesh(Reader& r, Ps3Mesh* mesh) {
    r.U8(); // command data
    const uint8_t type = r.U8();
    mesh->header.base.type = type;
    if (type == 1) {
        PolygonType1& p = mesh->header.polygon1;
        p.format = r.U8();
        if (!r.SceneString(nullptr) || !r.SceneString(nullptr)) return false;
        const uint32_t count = r.ReadU32();
        if (count > 64) return false;
        for (uint32_t i = 0; i < count; ++i) {
            BgImage image = {};
            image.unk_00 = r.U16();
            image.id = r.U8();
            std::string path;
            if (!r.SceneString(&path)) return false;
            image.source = const_cast<char*>(OwnPath(mesh, path));
            image.unk_0C = r.ReadU32();
            image.tlut = r.ReadU32();
            image.width = r.U16();
            image.height = r.U16();
            image.fmt = r.U8();
            image.siz = r.U8();
            image.mode0 = r.U16();
            image.tlutCount = r.U16();
            if (p.format == 1) {
                p.single.source = image.source;
                p.single.unk_0C = image.unk_0C;
                p.single.tlut = reinterpret_cast<void*>(static_cast<uintptr_t>(image.tlut));
                p.single.width = image.width;
                p.single.height = image.height;
                p.single.fmt = image.fmt;
                p.single.siz = image.siz;
                p.single.mode0 = image.mode0;
                p.single.tlutCount = image.tlutCount;
            } else {
                mesh->images.push_back(image);
            }
        }
        if (p.format != 1) {
            p.multi.count = static_cast<u8>(count);
            p.multi.list = mesh->images.data();
        }
        r.U8(); // polygon type, unused
        std::string opa, xlu;
        if (!r.SceneString(&opa) || !r.SceneString(&xlu)) return false;
        mesh->bgDlist.opa = opa.empty() ? nullptr : reinterpret_cast<Gfx*>(const_cast<char*>(OwnPath(mesh, opa)));
        mesh->bgDlist.xlu = xlu.empty() ? nullptr : reinterpret_cast<Gfx*>(const_cast<char*>(OwnPath(mesh, xlu)));
        p.dlist = reinterpret_cast<Gfx*>(&mesh->bgDlist);
        return true;
    }
    if (type != 0 && type != 2) return false;
    const unsigned count = r.U8();
    if (count > 64) return false;
    mesh->normal.resize(type == 0 ? count : 0);
    mesh->sorted.resize(type == 2 ? count : 0);
    for (unsigned i = 0; i < count; ++i) {
        r.U8(); // polygon type, unused
        if (type == 2) {
            mesh->sorted[i].pos.x = static_cast<s16>(r.U16());
            mesh->sorted[i].pos.y = static_cast<s16>(r.U16());
            mesh->sorted[i].pos.z = static_cast<s16>(r.U16());
            mesh->sorted[i].unk_06 = static_cast<s16>(r.U16());
        }
        std::string opa, xlu;
        if (!r.SceneString(&opa) || !r.SceneString(&xlu)) return false;
        Gfx* opaList = opa.empty() ? nullptr : reinterpret_cast<Gfx*>(const_cast<char*>(OwnPath(mesh, opa)));
        Gfx* xluList = xlu.empty() ? nullptr : reinterpret_cast<Gfx*>(const_cast<char*>(OwnPath(mesh, xlu)));
        if (type == 0) {
            mesh->normal[i].opa = opaList;
            mesh->normal[i].xlu = xluList;
        } else {
            mesh->sorted[i].opa = opaList;
            mesh->sorted[i].xlu = xluList;
        }
    }
    if (type == 0) {
        mesh->header.polygon0.num = static_cast<u8>(count);
        mesh->header.polygon0.start = mesh->normal.data();
        mesh->header.polygon0.end = mesh->normal.data() + count;
    } else {
        mesh->header.polygon2.num = static_cast<u8>(count);
        mesh->header.polygon2.start = mesh->sorted.data();
        mesh->header.polygon2.end = mesh->sorted.data() + count;
    }
    return true;
}

// PathFactory: u32 numPaths, then per path u32 count + s16 xyz points.
bool ReadPathFile(const std::string& name, Ps3SceneCmd* cmd) {
    Resource* resource = LoadName(name.c_str());
    if (resource == nullptr) return false;
    Reader r(resource->data, 0, resource->bigEndian);
    const uint32_t numPaths = r.ReadU32();
    if (numPaths > 256) return false;
    const size_t first = cmd->pathPoints.size();
    for (uint32_t k = 0; k < numPaths; ++k) {
        const uint32_t count = r.ReadU32();
        if (count > 1024 || !r.Can(static_cast<size_t>(count) * 6)) return false;
        std::vector<Vec3s> points(count);
        for (uint32_t i = 0; i < count; ++i) {
            points[i].x = static_cast<s16>(r.U16());
            points[i].y = static_cast<s16>(r.U16());
            points[i].z = static_cast<s16>(r.U16());
        }
        cmd->pathPoints.push_back(std::move(points));
    }
    for (size_t k = first; k < cmd->pathPoints.size(); ++k) {
        Path path = {};
        path.count = static_cast<u8>(cmd->pathPoints[k].size());
        path.points = cmd->pathPoints[k].data();
        cmd->paths.push_back(path);
    }
    return true;
}

Ps3Scene* LoadSceneResource(const std::string& name, unsigned depth);

bool ReadSceneCommand(Reader& r, Ps3SceneCmd* cmd, Ps3Scene* scene, unsigned depth) {
    switch (cmd->id) {
        case 0x00: case 0x01:
            ReadActorEntries(r, &cmd->actors);
            return true;
        case 0x03: case 0x17:
            return r.SceneString(&cmd->file);
        case 0x04: {
            const uint32_t n = r.ReadU32();
            if (n > 128) return false;
            cmd->names.resize(n);
            for (uint32_t i = 0; i < n; ++i) {
                if (!r.SceneString(&cmd->names[i])) return false;
                RomFile room = {};
                room.vromStart = r.ReadU32();
                room.vromEnd = r.ReadU32();
                cmd->rooms.push_back(room);
            }
            for (uint32_t i = 0; i < n; ++i) cmd->rooms[i].fileName = const_cast<char*>(cmd->names[i].c_str());
            return true;
        }
        case 0x06: {
            const uint32_t n = r.ReadU32();
            for (uint32_t i = 0; i < n && r.Can(2); ++i) {
                EntranceEntry e;
                e.spawn = r.U8();
                e.room = r.U8();
                cmd->entrances.push_back(e);
            }
            return true;
        }
        case 0x0A:
            cmd->mesh = new Ps3Mesh();
            return ReadMesh(r, cmd->mesh);
        case 0x0B: case 0x13: {
            const uint32_t n = r.ReadU32();
            for (uint32_t i = 0; i < n && r.Can(2); ++i) cmd->shorts.push_back(static_cast<s16>(r.U16()));
            return true;
        }
        case 0x0C: {
            const uint32_t n = r.ReadU32();
            for (uint32_t i = 0; i < n && r.Can(13); ++i) {
                LightInfo light = {};
                light.type = r.U8();
                light.params.point.x = static_cast<s16>(r.U16());
                light.params.point.y = static_cast<s16>(r.U16());
                light.params.point.z = static_cast<s16>(r.U16());
                light.params.point.color[0] = r.U8();
                light.params.point.color[1] = r.U8();
                light.params.point.color[2] = r.U8();
                light.params.point.drawGlow = r.U8();
                light.params.point.radius = static_cast<s16>(r.U16());
                cmd->lights.push_back(light);
            }
            return true;
        }
        case 0x0D: {
            const uint32_t n = r.ReadU32();
            if (n > 64) return false;
            for (uint32_t i = 0; i < n; ++i) {
                std::string name;
                if (!r.SceneString(&name)) return false;
                if (!ReadPathFile(name, cmd)) SceneTrace("path file missing", name);
            }
            return true;
        }
        case 0x0E: {
            const uint32_t n = r.ReadU32();
            for (uint32_t i = 0; i < n && r.Can(16); ++i) {
                TransitionActorEntry e = {};
                e.sides[0].room = static_cast<s8>(r.U8());
                e.sides[0].effects = static_cast<s8>(r.U8());
                e.sides[1].room = static_cast<s8>(r.U8());
                e.sides[1].effects = static_cast<s8>(r.U8());
                e.id = static_cast<s16>(r.U16());
                e.pos.x = static_cast<s16>(r.U16());
                e.pos.y = static_cast<s16>(r.U16());
                e.pos.z = static_cast<s16>(r.U16());
                e.rotY = static_cast<s16>(r.U16());
                e.params = static_cast<s16>(r.U16());
                cmd->transitions.push_back(e);
            }
            return true;
        }
        case 0x0F: {
            const uint32_t n = r.ReadU32();
            for (uint32_t i = 0; i < n && r.Can(22); ++i) {
                EnvLightSettings L = {};
                for (int c = 0; c < 3; ++c) L.ambientColor[c] = r.U8();
                for (int c = 0; c < 3; ++c) L.light1Dir[c] = static_cast<s8>(r.U8());
                for (int c = 0; c < 3; ++c) L.light1Color[c] = r.U8();
                for (int c = 0; c < 3; ++c) L.light2Dir[c] = static_cast<s8>(r.U8());
                for (int c = 0; c < 3; ++c) L.light2Color[c] = r.U8();
                for (int c = 0; c < 3; ++c) L.fogColor[c] = r.U8();
                L.fogNear = static_cast<s16>(r.U16());
                L.fogFar = static_cast<s16>(r.U16());
                cmd->lightSettings.push_back(L);
            }
            return true;
        }
        case 0x18: {
            const uint32_t n = r.ReadU32();
            if (n > 32) return false;
            for (uint32_t i = 0; i < n; ++i) {
                std::string header;
                if (!r.SceneString(&header)) return false;
                scene->alternates.push_back(header.empty() || depth > 0 ? nullptr
                                                                        : LoadSceneResource(header, depth + 1));
            }
            return true;
        }
        case 0x14:
            return true;
        default: {
            size_t size = 0;
            switch (cmd->id) {
                case 0x02: case 0x08: case 0x19: size = 5; break;
                case 0x05: case 0x11: size = 4; break;
                case 0x07: case 0x10: case 0x15: size = 3; break;
                case 0x12: size = 2; break;
                case 0x16: size = 1; break;
                default: return false;
            }
            if (!r.Can(size)) return false;
            for (size_t i = 0; i < size; ++i) cmd->raw[i] = r.U8();
            return true;
        }
    }
}

Ps3Scene* LoadSceneResource(const std::string& name, unsigned depth) {
    for (Ps3Scene* scene : LoadedScenes()) {
        if (scene->name == name) return scene;
    }
    Resource* resource = LoadName(name.c_str());
    if (resource == nullptr || resource->data.size() < 4) {
        SceneTrace("resource missing", name);
        return nullptr;
    }
    // Copy: the resource list may reallocate while nested resources load.
    const std::vector<unsigned char> bytes = resource->data;
    const bool big = resource->bigEndian;
    Ps3Scene* scene = new Ps3Scene();
    scene->name = name;
    Reader r(bytes, 0, big);
    const uint32_t count = r.ReadU32();
    for (uint32_t i = 0; i < count && count <= 128; ++i) {
        Ps3SceneCmd cmd;
        cmd.id = r.ReadU32();
        if (!ReadSceneCommand(r, &cmd, scene, depth)) {
            char line[160];
            snprintf(line, sizeof(line), "command 0x%lx unreadable in", static_cast<unsigned long>(cmd.id));
            SceneTrace(line, name);
            break;
        }
        scene->cmds.push_back(std::move(cmd));
        if (scene->cmds.back().id == 0x14) break;
    }
    LoadedScenes().push_back(scene);
    char line[64];
    snprintf(line, sizeof(line), "decoded %lu commands:", static_cast<unsigned long>(scene->cmds.size()));
    SceneTrace(line, name);
    return scene;
}

// Converted cutscene scripts and collision headers, kept per resource name.
void* CutsceneWords(const std::string& name) {
    static std::vector<std::pair<std::string, void*>> converted;
    for (auto& entry : converted) {
        if (entry.first == name) return entry.second;
    }
    Resource* resource = LoadName(name.c_str());
    if (resource == nullptr) return nullptr;
    const std::vector<unsigned char> bytes = resource->data;
    void* words = nullptr;
    uint32_t* saved = sPs3CutsceneWords; // keep the title script untouched
    sPs3CutsceneWords = nullptr;
    if (Ps3ConvertTitleCutscene(bytes.data(), bytes.size(), resource->bigEndian)) words = sPs3CutsceneWords;
    sPs3CutsceneWords = saved;
    converted.push_back({ name, words });
    return words;
}

CollisionHeader* CollisionFor(const std::string& name) {
    static std::vector<std::pair<std::string, CollisionHeader*>> converted;
    for (auto& entry : converted) {
        if (entry.first == name) return entry.second;
    }
    Resource* resource = LoadName(name.c_str());
    if (resource == nullptr) return nullptr;
    const std::vector<unsigned char> bytes = resource->data;
    CollisionHeader* saved = sPs3CollisionHeader;
    sPs3CollisionHeader = nullptr;
    CollisionHeader* header = nullptr;
    if (Ps3ConvertCollision(bytes.data(), bytes.size(), resource->bigEndian)) header = sPs3CollisionHeader;
    sPs3CollisionHeader = saved;
    converted.push_back({ name, header });
    return header;
}

void ExecuteSceneCommands(PlayState* play, Ps3Scene* scene);

// One handler per scene command, as in z_scene_otr.cpp's sceneCommands table.
// Returns true when the command replaced the rest of this header (alternate layer).
bool ExecuteCommand(PlayState* play, Ps3Scene* scene, Ps3SceneCmd& cmd) {
    const uint8_t* b = cmd.raw;
    switch (cmd.id) {
        case 0x00: // spawn list
            if (play->setupEntranceList != nullptr && !cmd.actors.empty()) {
                const u8 spawnIndex = play->setupEntranceList[play->curSpawn].spawn;
                play->linkActorEntry = &cmd.actors[spawnIndex < cmd.actors.size() ? spawnIndex : 0];
            }
            play->linkAgeOnLoad = gSaveContext.linkAge;
            Object_Spawn(&play->objectCtx, gLinkObjectIds[gSaveContext.linkAge]);
            break;
        case 0x01: // actor list
            play->numSetupActors = static_cast<s16>(cmd.actors.size());
            play->setupActorList = cmd.actors.empty() ? nullptr : cmd.actors.data();
            break;
        case 0x03: { // collision header
            CollisionHeader* header = CollisionFor(cmd.file);
            if (header != nullptr) {
                BgCheck_Allocate(&play->colCtx, play, header);
            } else {
                SceneTrace("collision unavailable", cmd.file);
            }
            break;
        }
        case 0x04: // room list
            play->numRooms = static_cast<u8>(cmd.rooms.size());
            play->roomList = cmd.rooms.empty() ? nullptr : cmd.rooms.data();
            break;
        case 0x05: // wind
            play->envCtx.windDirection.x = static_cast<s8>(b[0]);
            play->envCtx.windDirection.y = static_cast<s8>(b[1]);
            play->envCtx.windDirection.z = static_cast<s8>(b[2]);
            play->envCtx.windSpeed = b[3];
            break;
        case 0x06: // entrance list
            play->setupEntranceList = cmd.entrances.empty() ? nullptr : cmd.entrances.data();
            break;
        case 0x07: { // special files: elf message (u8), global object (s16)
            const s16 globalObject = static_cast<s16>(b[1] | (b[2] << 8));
            if (globalObject != 0) play->objectCtx.subKeepIndex = Object_Spawn(&play->objectCtx, globalObject);
            if (b[0] != 0) {
                Resource* blob = LoadName(sNaviMsgFiles[b[0] - 1].fileName);
                play->cUpElfMsgs = blob != nullptr ? reinterpret_cast<ElfMessage*>(blob->data.data()) : nullptr;
            }
            break;
        }
        case 0x08: { // room behavior: s8, s32
            const s32 flags2 = static_cast<s32>(b[1] | (b[2] << 8) | (b[3] << 16) | (static_cast<uint32_t>(b[4]) << 24));
            play->roomCtx.curRoom.behaviorType1 = static_cast<s8>(b[0]);
            play->roomCtx.curRoom.behaviorType2 = flags2 & 0xFF;
            play->roomCtx.curRoom.lensMode = (flags2 >> 8) & 1;
            play->msgCtx.disableWarpSongs = (flags2 >> 0xA) & 1;
            break;
        }
        case 0x0A: // mesh
            play->roomCtx.curRoom.meshHeader = cmd.mesh != nullptr ? &cmd.mesh->header : nullptr;
            break;
        case 0x0B: { // object list (Scene_CommandObjectList)
            s32 i = play->objectCtx.unk_09;
            s32 k = 0;
            for (; i < play->objectCtx.num; i++, k++) {
                if (static_cast<size_t>(k) >= cmd.shorts.size() || play->objectCtx.status[i].id != cmd.shorts[k]) {
                    for (s32 j = i; j < play->objectCtx.num; j++) play->objectCtx.status[j].id = OBJECT_INVALID;
                    Actor_KillAllWithMissingObject(play, &play->actorCtx);
                    break;
                }
            }
            for (; static_cast<size_t>(k) < cmd.shorts.size(); k++, i++) {
                if (i < OBJECT_EXCHANGE_BANK_MAX - 1) play->objectCtx.status[i].id = -cmd.shorts[k];
            }
            play->objectCtx.num = static_cast<u8>(i);
            break;
        }
        case 0x0C: // light list
            for (LightInfo& light : cmd.lights) LightContext_InsertLight(play, &play->lightCtx, &light);
            break;
        case 0x0D: // path list
            play->setupPathList = cmd.paths.empty() ? nullptr : cmd.paths.data();
            break;
        case 0x0E: // transition actors
            play->transiActorCtx.numActors = static_cast<u8>(cmd.transitions.size());
            play->transiActorCtx.list = cmd.transitions.empty() ? nullptr : cmd.transitions.data();
            for (TransitionActorEntry& e : cmd.transitions) e.id = ABS(e.id);
            break;
        case 0x0F: // light settings
            play->envCtx.lightSettingsList = cmd.lightSettings.empty() ? nullptr : cmd.lightSettings.data();
            play->envCtx.numLightSettings = static_cast<u8>(cmd.lightSettings.size());
            break;
        case 0x10: // time settings: applied by z_play.c's PS3 environment helper
            sPs3RoomTime[0] = b[0];
            sPs3RoomTime[1] = b[1];
            sPs3RoomTime[2] = b[2];
            sPs3RoomTimeValid = true;
            break;
        case 0x11: // skybox settings: unk, skyboxId, weather, indoors
            play->skyboxId = b[1];
            play->envCtx.unk_17 = play->envCtx.unk_18 = b[2];
            play->envCtx.indoors = b[3];
            break;
        case 0x12: // skybox disables
            play->envCtx.skyboxDisabled = b[0];
            play->envCtx.sunMoonDisabled = b[1];
            break;
        case 0x13: // exit list
            play->setupExitList = cmd.shorts.empty() ? nullptr : cmd.shorts.data();
            break;
        case 0x15: // sound settings: reverb, natureAmbienceId, seqId
            play->sequenceCtx.seqId = b[2];
            play->sequenceCtx.natureAmbienceId = b[1];
            if (gSaveContext.seqId == 0xFF) Audio_QueueSeqCmd(static_cast<s8>(b[0]) | 0xF0000000);
            break;
        case 0x16: // echo
            play->roomCtx.curRoom.echo = static_cast<s8>(b[0]);
            break;
        case 0x17: { // cutscene
            void* words = CutsceneWords(cmd.file);
            play->csCtx.segment = static_cast<decltype(play->csCtx.segment)>(words);
            if (words == nullptr) SceneTrace("cutscene unavailable", cmd.file);
            break;
        }
        case 0x18: { // alternate headers (Scene_CommandAlternateHeaderList)
            const s32 layer = gSaveContext.sceneLayer;
            if (layer != 0) {
                Ps3Scene* header =
                    static_cast<size_t>(layer - 1) < scene->alternates.size() ? scene->alternates[layer - 1] : nullptr;
                if (header == nullptr && layer == 3 && scene->alternates.size() >= 2) header = scene->alternates[1];
                if (header != nullptr) {
                    ExecuteSceneCommands(play, header);
                    return true;
                }
            }
            break;
        }
        case 0x19: { // misc: camera movement (s8), world map area (s32)
            YREG(15) = static_cast<s8>(b[0]);
            gSaveContext.worldMapArea =
                static_cast<s32>(b[1] | (b[2] << 8) | (b[3] << 16) | (static_cast<uint32_t>(b[4]) << 24));
            if ((play->sceneNum == SCENE_BAZAAR || play->sceneNum == SCENE_SHOOTING_GALLERY) &&
                LINK_AGE_IN_YEARS == YEARS_ADULT) {
                gSaveContext.worldMapArea = 1;
            }
            if (((play->sceneNum >= SCENE_HYRULE_FIELD) && (play->sceneNum <= SCENE_OUTSIDE_GANONS_CASTLE)) ||
                ((play->sceneNum >= SCENE_MARKET_ENTRANCE_DAY) &&
                 (play->sceneNum <= SCENE_TEMPLE_OF_TIME_EXTERIOR_RUINS))) {
                if (gSaveContext.cutsceneIndex < 0xFFF0) {
                    gSaveContext.worldMapAreaData |= gBitFlags[gSaveContext.worldMapArea];
                }
            }
            break;
        }
        default:
            break;
    }
    return false;
}

void ExecuteSceneCommands(PlayState* play, Ps3Scene* scene) {
    if (scene == nullptr) return;
    for (Ps3SceneCmd& cmd : scene->cmds) {
        if (cmd.id == 0x14) break;
        if (ExecuteCommand(play, scene, cmd)) break;
    }
}

uint8_t sPs3DummySceneSegment[64] __attribute__((aligned(16)));

} // namespace

static CollisionHeader* Ps3CollisionForName(const char* path) {
    if (path == nullptr) return nullptr;
    return CollisionFor(Normalize(path));
}

static void* Ps3CutsceneForName(const char* path) {
    if (path == nullptr) return nullptr;
    return CutsceneWords(Normalize(path));
}

// ---- PS3 audio converters: mirror SoH AudioSampleFactory / AudioSoundFontFactory /
// AudioSequenceFactory (binary V2). Container fields use the resource byte order; the
// N64 payloads (sequence bytecode, ADPCM data) are copied as-is, and envelope/book
// values are stored as native s16 (BE16SWAP is a no-op on big-endian PS3). Every
// converted object is kept for the whole run; the game holds raw pointers to them.
namespace {
struct Ps3AudioCursor {
    const std::vector<unsigned char>& d;
    size_t off;
    bool big;
    bool ok;
    bool Have(size_t n) {
        if (!ok || off > d.size() || d.size() - off < n) ok = false;
        return ok;
    }
    uint8_t Byte() { return Have(1) ? d[off++] : 0; }
    uint16_t Half() {
        if (!Have(2)) return 0;
        const unsigned char* p = d.data() + off;
        off += 2;
        return big ? static_cast<uint16_t>((p[0] << 8) | p[1]) : static_cast<uint16_t>((p[1] << 8) | p[0]);
    }
    uint32_t Word() {
        if (!Have(4)) return 0;
        const unsigned char* p = d.data() + off;
        off += 4;
        return big ? (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
                         (static_cast<uint32_t>(p[2]) << 8) | p[3]
                   : (static_cast<uint32_t>(p[3]) << 24) | (static_cast<uint32_t>(p[2]) << 16) |
                         (static_cast<uint32_t>(p[1]) << 8) | p[0];
    }
    float Float() {
        const uint32_t v = Word();
        float f;
        std::memcpy(&f, &v, sizeof(f));
        return f;
    }
    std::string Str() {
        const uint32_t n = Word();
        if (n > 1024 || !Have(n)) {
            ok = false;
            return std::string();
        }
        std::string value(reinterpret_cast<const char*>(d.data() + off), n);
        off += n;
        return value;
    }
};

void Ps3AudioFail(const char* what, const std::string& name) {
    static int logged = 0;
    if (logged++ >= 16) return;
    char line[256];
    snprintf(line, sizeof(line), "trace: audio %s conversion FAILED name=%s", what, name.c_str());
    oot_ps3_early_trace(line);
}

// Copies the resource body: LoadName may reallocate the resource list (rule 4).
bool Ps3AudioBytes(const std::string& name, uint32_t type, std::vector<unsigned char>* bytes, bool* big) {
    Resource* resource = LoadName(name.c_str());
    if (resource == nullptr || resource->type != type) return false;
    *bytes = resource->data;
    *big = resource->bigEndian;
    return true;
}

SoundFontSample* Ps3AudioSample(const std::string& name) {
    static std::map<std::string, SoundFontSample*> cache;
    auto found = cache.find(name);
    if (found != cache.end()) return found->second;
    SoundFontSample* result = nullptr;
    std::vector<unsigned char> bytes;
    bool big = false;
    if (Ps3AudioBytes(name, static_cast<uint32_t>(SOH::ResourceType::SOH_AudioSample), &bytes, &big)) {
        Ps3AudioCursor c{ bytes, 0, big, true };
        SoundFontSample* sample = new SoundFontSample();
        std::memset(sample, 0, sizeof(*sample));
        sample->codec = c.Byte();
        sample->medium = c.Byte();
        sample->unk_bit26 = c.Byte();
        sample->isRelocated = c.Byte();
        sample->size = c.Word();
        if (c.Have(sample->size)) {
            sample->sampleAddr = new u8[sample->size ? sample->size : 1];
            std::memcpy(sample->sampleAddr, bytes.data() + c.off, sample->size);
            c.off += sample->size;
        }
        AdpcmLoop* loop = new AdpcmLoop();
        std::memset(loop, 0, sizeof(*loop));
        loop->start = c.Word();
        loop->loopEnd = c.Word();
        loop->count = c.Word();
        const uint32_t states = c.Word();
        for (uint32_t i = 0; i < states && c.ok; ++i) {
            const s16 value = static_cast<s16>(c.Half());
            if (i < 16) loop->predictorState[i] = value;
        }
        AdpcmBook* book = new AdpcmBook();
        book->order = static_cast<s32>(c.Word());
        book->npredictors = static_cast<s32>(c.Word());
        const uint32_t entries = c.Word();
        book->book = new s16[entries ? (entries < 0x10000 ? entries : 1) : 1];
        for (uint32_t i = 0; i < entries && i < 0x10000 && c.ok; ++i) book->book[i] = static_cast<s16>(c.Half());
        sample->loop = loop;
        sample->book = book;
        if (c.ok && sample->sampleAddr != nullptr) {
            result = sample;
        } else {
            Ps3AudioFail("sample", name);
        }
    } else {
        Ps3AudioFail("sample (missing)", name);
    }
    cache[name] = result;
    return result;
}

AdsrEnvelope* Ps3AudioEnvelope(Ps3AudioCursor& c) {
    const uint32_t count = c.Word();
    if (count > 256) {
        c.ok = false;
        return nullptr;
    }
    AdsrEnvelope* envelope = new AdsrEnvelope[count ? count : 1];
    for (uint32_t i = 0; i < count; ++i) {
        envelope[i].delay = static_cast<s16>(c.Half());
        envelope[i].arg = static_cast<s16>(c.Half());
    }
    return envelope;
}

void Ps3AudioSound(Ps3AudioCursor& c, SoundFontSound* sound) {
    (void)c.Byte(); // hasSampleRef
    const std::string sampleName = c.Str();
    sound->tuning = c.Float();
    sound->sample = sampleName.empty() ? nullptr : Ps3AudioSample(sampleName);
}

SoundFont* Ps3AudioFont(const std::string& name) {
    static std::map<std::string, SoundFont*> cache;
    auto found = cache.find(name);
    if (found != cache.end()) return found->second;
    SoundFont* result = nullptr;
    std::vector<unsigned char> bytes;
    bool big = false;
    if (Ps3AudioBytes(name, static_cast<uint32_t>(SOH::ResourceType::SOH_AudioSoundFont), &bytes, &big)) {
        Ps3AudioCursor c{ bytes, 0, big, true };
        SoundFont* font = new SoundFont();
        std::memset(font, 0, sizeof(*font));
        font->fntIndex = static_cast<s32>(c.Word());
        (void)c.Byte(); // medium
        (void)c.Byte(); // cachePolicy
        const uint16_t data1 = c.Half();
        font->sampleBankId1 = static_cast<u8>(data1 >> 8);
        font->sampleBankId2 = static_cast<u8>(data1 & 0xFF);
        (void)c.Half(); // data2
        (void)c.Half(); // data3
        const uint32_t drums = c.Word();
        const uint32_t instruments = c.Word();
        const uint32_t sfx = c.Word();
        if (drums > 255 || instruments > 255 || sfx > 0xFFFF) c.ok = false;
        font->numDrums = static_cast<u8>(drums);
        font->numInstruments = static_cast<u8>(instruments);
        font->numSfx = static_cast<u16>(sfx);
        font->drums = new Drum*[drums ? drums : 1]();
        for (uint32_t i = 0; i < drums && c.ok; ++i) {
            Drum* drum = new Drum();
            std::memset(drum, 0, sizeof(*drum));
            drum->releaseRate = c.Byte();
            drum->pan = c.Byte();
            (void)c.Byte();
            drum->loaded = 0;
            drum->envelope = Ps3AudioEnvelope(c);
            Ps3AudioSound(c, &drum->sound);
            font->drums[i] = drum->sound.sample != nullptr ? drum : nullptr;
        }
        font->instruments = new Instrument*[instruments ? instruments : 1]();
        for (uint32_t i = 0; i < instruments && c.ok; ++i) {
            Instrument* instrument = new Instrument();
            std::memset(instrument, 0, sizeof(*instrument));
            const uint8_t valid = c.Byte();
            (void)c.Byte();
            instrument->loaded = 0;
            instrument->normalRangeLo = c.Byte();
            instrument->normalRangeHi = c.Byte();
            instrument->releaseRate = c.Byte();
            instrument->envelope = Ps3AudioEnvelope(c);
            SoundFontSound* sounds[3] = { &instrument->lowNotesSound, &instrument->normalNotesSound,
                                          &instrument->highNotesSound };
            for (SoundFontSound* sound : sounds) {
                if (c.Byte()) Ps3AudioSound(c, sound);
            }
            font->instruments[i] = valid ? instrument : nullptr;
        }
        font->soundEffects = new SoundFontSound[sfx ? sfx : 1]();
        for (uint32_t i = 0; i < sfx && c.ok; ++i) {
            if (c.Byte()) Ps3AudioSound(c, &font->soundEffects[i]);
        }
        if (c.ok) {
            result = font;
        } else {
            Ps3AudioFail("soundfont", name);
        }
    } else {
        Ps3AudioFail("soundfont (missing)", name);
    }
    cache[name] = result;
    return result;
}

SequenceData* Ps3AudioSeq(const std::string& name) {
    static std::map<std::string, SequenceData*> cache;
    auto found = cache.find(name);
    if (found != cache.end()) return found->second;
    SequenceData* result = nullptr;
    std::vector<unsigned char> bytes;
    bool big = false;
    if (Ps3AudioBytes(name, static_cast<uint32_t>(SOH::ResourceType::SOH_AudioSequence), &bytes, &big)) {
        Ps3AudioCursor c{ bytes, 0, big, true };
        SequenceData* seq = new SequenceData();
        std::memset(seq, 0, sizeof(*seq));
        const uint32_t size = c.Word();
        if (c.Have(size)) {
            seq->seqData = new char[size ? size : 1];
            std::memcpy(seq->seqData, bytes.data() + c.off, size);
            c.off += size;
            seq->seqDataSize = static_cast<int32_t>(size);
        }
        seq->seqNumber = c.Byte();
        seq->medium = c.Byte();
        seq->cachePolicy = c.Byte();
        seq->numFonts = static_cast<int32_t>(c.Word());
        for (int32_t i = 0; i < seq->numFonts && i < 16 && c.ok; ++i) seq->fonts[i] = c.Byte();
        if (c.ok && seq->seqData != nullptr) {
            result = seq;
        } else {
            Ps3AudioFail("sequence", name);
        }
    } else {
        Ps3AudioFail("sequence (missing)", name);
    }
    cache[name] = result;
    return result;
}
} // namespace

static SoundFontSample* Ps3AudioSampleForName(const char* path) {
    return path != nullptr ? Ps3AudioSample(Normalize(path)) : nullptr;
}
static SoundFont* Ps3AudioFontForName(const char* path) {
    return path != nullptr ? Ps3AudioFont(Normalize(path)) : nullptr;
}
static SequenceData* Ps3AudioSeqForName(const char* path) {
    return path != nullptr ? Ps3AudioSeq(Normalize(path)) : nullptr;
}
// ---- end PS3 audio converters ----

// OTRPlay_SpawnScene + OTRPlay_InitScene for every scene except the title intro.
extern "C" int oot_ps3_spawn_scene(PlayState* play, s32 sceneId, s32 spawn) {
    SceneTableEntry* entry = &gSceneTable[sceneId];
    entry->unk_13 = 0;
    play->loadedScene = entry;
    play->sceneNum = sceneId;
    play->sceneConfig = entry->config;
    const bool dungeon = (sceneId >= SCENE_DEKU_TREE && sceneId <= SCENE_ICE_CAVERN) ||
                         sceneId == SCENE_GERUDO_TRAINING_GROUND || sceneId == SCENE_INSIDE_GANONS_CASTLE;
    if (entry->sceneFile.fileName == nullptr) return 0;
    char path[256];
    snprintf(path, sizeof(path), "scenes/%s/%s/%s", dungeon ? "nonmq" : "shared", entry->sceneFile.fileName,
             entry->sceneFile.fileName);
    Ps3Scene* scene = LoadSceneResource(path, 0);
    if (scene == nullptr) return 0;
    // Segment 2 is never dereferenced by O2R display lists; Play_Draw only needs it non-null.
    play->sceneSegment = sPs3DummySceneSegment;

    play->curSpawn = spawn;
    play->linkActorEntry = nullptr;
    play->unk_11DFC = nullptr;
    play->setupEntranceList = nullptr;
    play->setupExitList = nullptr;
    play->cUpElfMsgs = nullptr;
    play->setupPathList = nullptr;
    play->numSetupActors = 0;
    Object_InitBank(play, &play->objectCtx);
    LightContext_Init(play, &play->lightCtx);
    TransitionActor_InitContext(&play->state, &play->transiActorCtx);
    func_80096FD4(play, &play->roomCtx.curRoom);
    YREG(15) = 0;
    gSaveContext.worldMapArea = 0;
    sPs3RoomTimeValid = false;
    play->envCtx.unk_BF = 0xFF; // Environment_Init's "no light override"; cutscenes may set it
    ExecuteSceneCommands(play, scene);
    // Play_InitEnvironment: Skybox_Init here; the PS3 environment helper in
    // z_play.c stands in for Environment_Init on the first Play_Update.
    Skybox_Init(&play->state, &play->skyboxCtx, play->skyboxId);
    D_8015FCC8 = 1; // Environment_Init: cutscenes may control the camera
    func_80096FE8(play, &play->roomCtx); // allocates room buffers and requests the first room
    char line[160];
    snprintf(line, sizeof(line), "scene-load: scene %d spawn %d layer %d ready (rooms=%u skybox=%d)",
             static_cast<int>(sceneId), static_cast<int>(spawn), static_cast<int>(gSaveContext.sceneLayer),
             static_cast<unsigned>(play->numRooms), static_cast<int>(play->skyboxId));
    oot_ps3_early_trace(line);
    return 1;
}

// OTRRoom_RequestNewRoom for the general loader.
extern "C" s32 oot_ps3_request_room(PlayState* play, RoomContext* roomCtx, s32 roomNum) {
    if (roomCtx->status != 0) return 0;
    roomCtx->prevRoom = roomCtx->curRoom;
    roomCtx->curRoom.num = roomNum;
    roomCtx->curRoom.segment = nullptr;
    roomCtx->status = 1;
    if (roomNum < 0 || roomNum >= play->numRooms) {
        oot_ps3_early_trace("scene-load: room request out of range");
        return 0;
    }
    const u32 size = static_cast<u32>(play->roomList[roomNum].vromEnd - play->roomList[roomNum].vromStart);
    roomCtx->unk_34 = reinterpret_cast<void*>(
        ALIGN16(reinterpret_cast<uintptr_t>(roomCtx->bufPtrs[roomCtx->activeBufPage]) -
                ((size + 8) * roomCtx->activeBufPage + 7)));
    osCreateMesgQueue(&roomCtx->loadQueue, &roomCtx->loadMsg, 1);
    roomCtx->roomToLoad = LoadSceneResource(play->roomList[roomNum].fileName, 0);
    roomCtx->activeBufPage ^= 1;
    return 1;
}

// OTRfunc_800973FC for the general loader: runs the requested room's commands.
extern "C" s32 oot_ps3_finish_room(PlayState* play, RoomContext* roomCtx) {
    if (roomCtx->status != 1) return 1;
    roomCtx->status = 0;
    roomCtx->curRoom.segment = roomCtx->unk_34;
    gSegments[3] = VIRTUAL_TO_PHYSICAL(roomCtx->unk_34);
    ExecuteSceneCommands(play, static_cast<Ps3Scene*>(roomCtx->roomToLoad));
    Player_SetBootData(play, GET_PLAYER(play));
    Actor_SpawnTransitionActors(play, &play->actorCtx);
    char line[96];
    snprintf(line, sizeof(line), "scene-load: room %d loaded (mesh type %d)", static_cast<int>(roomCtx->curRoom.num),
             roomCtx->curRoom.meshHeader != nullptr ? roomCtx->curRoom.meshHeader->base.type : -1);
    oot_ps3_early_trace(line);
    return 1;
}
// ---- end PS3 general scene loader ----

// Loads and converts the scene collision after the scene walk has finished.
extern "C" void* oot_ps3_scene_collision_header(void) {
    if (sPs3CollisionHeader == nullptr && sPs3CollisionPath[0] != '\0') {
        Resource* collision = LoadName(sPs3CollisionPath);
        if (collision == nullptr || collision->data.empty()) {
            oot_ps3_early_trace("trace: scene collision resource missing");
        } else {
            Ps3ConvertCollision(collision->data.data(), collision->data.size(), collision->bigEndian);
        }
        sPs3CollisionPath[0] = '\0'; // attempt only once
    }
    return sPs3CollisionHeader;
}

extern "C" int oot_ps3_scene_light_settings(void** out) {
    if (out) *out = sPs3LightSettingsCount ? static_cast<void*>(sPs3LightSettings) : nullptr;
    return static_cast<int>(sPs3LightSettingsCount);
}
