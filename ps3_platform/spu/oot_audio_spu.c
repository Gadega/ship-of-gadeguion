/* OoT audio mixer on an SPU.
 *
 * The audio thread records the N64 audio commands (soh/mixer.c a*Impl calls) into a
 * list in main memory and sends its address here through signal register 1; this
 * thread runs the list in local store and answers with a user event. The operations
 * are soh/mixer.c's reference code (same integer arithmetic, same DMEM layout), with
 * main-memory accesses (sample data, ADPCM codebooks, note states, output) done by
 * DMA. An address of 0 ends the thread.
 *
 * List: a 32-byte header (u32 command count first) followed by 32-byte commands
 * (OotMixCmd, soh/mixer.c). */

#include <spu_intrinsics.h>
#include <spu_mfcio.h>
#include <stdint.h>
#include <string.h>
#include <sys/spu_event.h>
#include <sys/spu_thread.h>

#define SPU_PORT 12
#define DMA_TAG 1

typedef struct {
    uint8_t op;
    uint8_t flags;
    uint16_t a;
    uint16_t b;
    uint16_t c;
    uint32_t d;
    uint32_t e;
    uint64_t ea;
    uint64_t ea2;
} __attribute__((aligned(16))) OotMixCmd;

enum {
    MIX_CLEARBUFFER = 1,
    MIX_LOADBUFFER,
    MIX_SAVEBUFFER,
    MIX_LOADADPCM,
    MIX_SETBUFFER,
    MIX_INTERLEAVE,
    MIX_DMEMMOVE,
    MIX_SETLOOP,
    MIX_ADPCMDEC,
    MIX_RESAMPLE,
    MIX_ENVSETUP1,
    MIX_ENVSETUP2,
    MIX_ENVMIXER,
    MIX_MIX,
    MIX_S8DEC,
    MIX_ADDMIXER,
    MIX_DUPLICATE,
    MIX_RESAMPLEZOH,
    MIX_INTERL,
    MIX_FILTER,
    MIX_HILOGAIN,
    MIX_UNKCMD19,
};

#define A_INIT 0x01
#define A_LOOP 0x02

#define ROUND_UP_64(v) (((v) + 63) & ~63)
#define ROUND_UP_32(v) (((v) + 31) & ~31)
#define ROUND_UP_16(v) (((v) + 15) & ~15)
#define ROUND_UP_8(v) (((v) + 7) & ~7)
#define ROUND_DOWN_16(v) ((v) & ~0xf)

#define DMEM_BUF_SIZE (0x1000 - 0x3C0 - 0x40)
/* Slack around the buffer: an out-of-range DMEM address must not hit our code. */
#define DMEM_SLACK 0x1000

static struct {
    uint16_t in;
    uint16_t out;
    uint16_t nbytes;
    uint16_t vol[2];
    uint16_t rate[2];
    uint16_t vol_wet;
    uint16_t rate_wet;
    uint64_t adpcm_loop_state;
    int16_t adpcm_table[8][2][8];
    uint16_t filter_count;
    int16_t filter[8];
} rspa;

static uint8_t sDmem[DMEM_SLACK + DMEM_BUF_SIZE + DMEM_SLACK] __attribute__((aligned(128)));
#define BUF_U8(a) (sDmem + DMEM_SLACK + ((int)(a)-0x3C0))
#define BUF_S16(a) ((int16_t*)BUF_U8(a))

static int16_t resample_table[64][4] = {
    { 0x0c39, 0x66ad, 0x0d46, 0xffdf }, { 0x0b39, 0x6696, 0x0e5f, 0xffd8 }, { 0x0a44, 0x6669, 0x0f83, 0xffd0 },
    { 0x095a, 0x6626, 0x10b4, 0xffc8 }, { 0x087d, 0x65cd, 0x11f0, 0xffbf }, { 0x07ab, 0x655e, 0x1338, 0xffb6 },
    { 0x06e4, 0x64d9, 0x148c, 0xffac }, { 0x0628, 0x643f, 0x15eb, 0xffa1 }, { 0x0577, 0x638f, 0x1756, 0xff96 },
    { 0x04d1, 0x62cb, 0x18cb, 0xff8a }, { 0x0435, 0x61f3, 0x1a4c, 0xff7e }, { 0x03a4, 0x6106, 0x1bd7, 0xff71 },
    { 0x031c, 0x6007, 0x1d6c, 0xff64 }, { 0x029f, 0x5ef5, 0x1f0b, 0xff56 }, { 0x022a, 0x5dd0, 0x20b3, 0xff48 },
    { 0x01be, 0x5c9a, 0x2264, 0xff3a }, { 0x015b, 0x5b53, 0x241e, 0xff2c }, { 0x0101, 0x59fc, 0x25e0, 0xff1e },
    { 0x00ae, 0x5896, 0x27a9, 0xff10 }, { 0x0063, 0x5720, 0x297a, 0xff02 }, { 0x001f, 0x559d, 0x2b50, 0xfef4 },
    { 0xffe2, 0x540d, 0x2d2c, 0xfee8 }, { 0xffac, 0x5270, 0x2f0d, 0xfedb }, { 0xff7c, 0x50c7, 0x30f3, 0xfed0 },
    { 0xff53, 0x4f14, 0x32dc, 0xfec6 }, { 0xff2e, 0x4d57, 0x34c8, 0xfebd }, { 0xff0f, 0x4b91, 0x36b6, 0xfeb6 },
    { 0xfef5, 0x49c2, 0x38a5, 0xfeb0 }, { 0xfedf, 0x47ed, 0x3a95, 0xfeac }, { 0xfece, 0x4611, 0x3c85, 0xfeab },
    { 0xfec0, 0x4430, 0x3e74, 0xfeac }, { 0xfeb6, 0x424a, 0x4060, 0xfeaf }, { 0xfeaf, 0x4060, 0x424a, 0xfeb6 },
    { 0xfeac, 0x3e74, 0x4430, 0xfec0 }, { 0xfeab, 0x3c85, 0x4611, 0xfece }, { 0xfeac, 0x3a95, 0x47ed, 0xfedf },
    { 0xfeb0, 0x38a5, 0x49c2, 0xfef5 }, { 0xfeb6, 0x36b6, 0x4b91, 0xff0f }, { 0xfebd, 0x34c8, 0x4d57, 0xff2e },
    { 0xfec6, 0x32dc, 0x4f14, 0xff53 }, { 0xfed0, 0x30f3, 0x50c7, 0xff7c }, { 0xfedb, 0x2f0d, 0x5270, 0xffac },
    { 0xfee8, 0x2d2c, 0x540d, 0xffe2 }, { 0xfef4, 0x2b50, 0x559d, 0x001f }, { 0xff02, 0x297a, 0x5720, 0x0063 },
    { 0xff10, 0x27a9, 0x5896, 0x00ae }, { 0xff1e, 0x25e0, 0x59fc, 0x0101 }, { 0xff2c, 0x241e, 0x5b53, 0x015b },
    { 0xff3a, 0x2264, 0x5c9a, 0x01be }, { 0xff48, 0x20b3, 0x5dd0, 0x022a }, { 0xff56, 0x1f0b, 0x5ef5, 0x029f },
    { 0xff64, 0x1d6c, 0x6007, 0x031c }, { 0xff71, 0x1bd7, 0x6106, 0x03a4 }, { 0xff7e, 0x1a4c, 0x61f3, 0x0435 },
    { 0xff8a, 0x18cb, 0x62cb, 0x04d1 }, { 0xff96, 0x1756, 0x638f, 0x0577 }, { 0xffa1, 0x15eb, 0x643f, 0x0628 },
    { 0xffac, 0x148c, 0x64d9, 0x06e4 }, { 0xffb6, 0x1338, 0x655e, 0x07ab }, { 0xffbf, 0x11f0, 0x65cd, 0x087d },
    { 0xffc8, 0x10b4, 0x6626, 0x095a }, { 0xffd0, 0x0f83, 0x6669, 0x0a44 }, { 0xffd8, 0x0e5f, 0x6696, 0x0b39 },
    { 0xffdf, 0x0d46, 0x66ad, 0x0c39 }
};

/* ---- DMA: any alignment, any size (staged through an aligned buffer) ---- */
#define STAGE_SIZE 4096
static uint8_t sStage[STAGE_SIZE + 32] __attribute__((aligned(128)));

static void dma_wait(void) {
    mfc_write_tag_mask(1 << DMA_TAG);
    mfc_read_tag_status_all();
}

static void get_any(void* ls, uint64_t ea, uint32_t size) {
    uint8_t* dst = (uint8_t*)ls;
    while (size > 0) {
        const uint32_t chunk = size > STAGE_SIZE ? STAGE_SIZE : size;
        const uint64_t base = ea & ~15ULL;
        const uint32_t off = (uint32_t)(ea & 15);
        const uint32_t total = (off + chunk + 15) & ~15U;
        mfc_get(sStage, base, total, DMA_TAG, 0, 0);
        dma_wait();
        memcpy(dst, sStage + off, chunk);
        dst += chunk;
        ea += chunk;
        size -= chunk;
    }
}

static void put_any(const void* ls, uint64_t ea, uint32_t size) {
    const uint8_t* src = (const uint8_t*)ls;
    while (size > 0) {
        const uint32_t chunk = size > STAGE_SIZE ? STAGE_SIZE : size;
        const uint64_t base = ea & ~15ULL;
        const uint32_t off = (uint32_t)(ea & 15);
        const uint32_t total = (off + chunk + 15) & ~15U;
        if (off != 0 || (chunk & 15) != 0) {
            mfc_get(sStage, base, total, DMA_TAG, 0, 0); /* keep the bytes around a partial block */
            dma_wait();
        }
        memcpy(sStage + off, src, chunk);
        mfc_put(sStage, base, total, DMA_TAG, 0, 0);
        dma_wait();
        src += chunk;
        ea += chunk;
        size -= chunk;
    }
}

static inline int16_t clamp16(int32_t v) {
    if (v < -0x8000) return -0x8000;
    if (v > 0x7fff) return 0x7fff;
    return (int16_t)v;
}

/* ---- soh/mixer.c operations ---- */

static void aADPCMdec(uint8_t flags, uint64_t stateEa) {
    int16_t state[16] __attribute__((aligned(16)));
    uint8_t* in = BUF_U8(rspa.in);
    int16_t* out = BUF_S16(rspa.out);
    int nbytes = ROUND_UP_32(rspa.nbytes);
    if (flags & A_INIT) {
        memset(out, 0, 16 * sizeof(int16_t));
    } else if (flags & A_LOOP) {
        get_any(out, rspa.adpcm_loop_state, 16 * sizeof(int16_t));
    } else {
        get_any(out, stateEa, 16 * sizeof(int16_t));
    }
    out += 16;
    while (nbytes > 0) {
        int shift = *in >> 4;
        int table_index = *in++ & 0xf;
        int16_t(*tbl)[8] = rspa.adpcm_table[table_index];
        int i;
        for (i = 0; i < 2; i++) {
            int16_t ins[8];
            int16_t prev1 = out[-1];
            int16_t prev2 = out[-2];
            int j, k;
            if (flags & 4) {
                for (j = 0; j < 2; j++) {
                    ins[j * 4] = (((*in >> 6) << 30) >> 30) << shift;
                    ins[j * 4 + 1] = ((((*in >> 4) & 0x3) << 30) >> 30) << shift;
                    ins[j * 4 + 2] = ((((*in >> 2) & 0x3) << 30) >> 30) << shift;
                    ins[j * 4 + 3] = (((*in++ & 0x3) << 30) >> 30) << shift;
                }
            } else {
                for (j = 0; j < 4; j++) {
                    ins[j * 2] = (((*in >> 4) << 28) >> 28) << shift;
                    ins[j * 2 + 1] = (((*in++ & 0xf) << 28) >> 28) << shift;
                }
            }
            for (j = 0; j < 8; j++) {
                int32_t acc = tbl[0][j] * prev2 + tbl[1][j] * prev1 + (ins[j] << 11);
                for (k = 0; k < j; k++) acc += tbl[1][((j - k) - 1)] * ins[k];
                acc >>= 11;
                *out++ = clamp16(acc);
            }
        }
        nbytes -= 16 * sizeof(int16_t);
    }
    memcpy(state, out - 16, 16 * sizeof(int16_t));
    put_any(state, stateEa, 16 * sizeof(int16_t));
}

static void aResample(uint8_t flags, uint16_t pitch, uint64_t stateEa) {
    int16_t state[16] __attribute__((aligned(16)));
    int16_t tmp[16];
    int16_t* in_initial = BUF_S16(rspa.in);
    int16_t* in = in_initial;
    int16_t* out = BUF_S16(rspa.out);
    int nbytes = ROUND_UP_16(rspa.nbytes);
    uint32_t pitch_accumulator;
    int i;
    int16_t* tbl;
    int32_t sample;

    get_any(state, stateEa, sizeof(state)); /* also when A_INIT: words 6 and 7 are kept */
    if (flags & A_INIT) {
        memset(tmp, 0, 5 * sizeof(int16_t));
    } else {
        memcpy(tmp, state, 16 * sizeof(int16_t));
    }
    if (flags & 2) {
        memcpy(in - 8, tmp + 8, 8 * sizeof(int16_t));
        in -= tmp[5] / (int)sizeof(int16_t);
    }
    in -= 4;
    pitch_accumulator = (uint16_t)tmp[4];
    memcpy(in, tmp, 4 * sizeof(int16_t));
    do {
        for (i = 0; i < 8; i++) {
            tbl = resample_table[pitch_accumulator * 64 >> 16];
            sample = ((in[0] * tbl[0] + 0x4000) >> 15) + ((in[1] * tbl[1] + 0x4000) >> 15) +
                     ((in[2] * tbl[2] + 0x4000) >> 15) + ((in[3] * tbl[3] + 0x4000) >> 15);
            *out++ = clamp16(sample);
            pitch_accumulator += (pitch << 1);
            in += pitch_accumulator >> 16;
            pitch_accumulator %= 0x10000;
        }
        nbytes -= 8 * sizeof(int16_t);
    } while (nbytes > 0);
    state[4] = (int16_t)pitch_accumulator;
    memcpy(state, in, 4 * sizeof(int16_t));
    i = (in - in_initial + 4) & 7;
    in -= i;
    if (i != 0) i = -8 - i;
    state[5] = i;
    memcpy(state + 8, in, 8 * sizeof(int16_t));
    put_any(state, stateEa, sizeof(state));
}

static void aEnvMixer(uint16_t in_addr, uint16_t n_samples, int swap_reverb, int neg_3, int neg_2, int neg_left,
                      int neg_right, int32_t wet_dry_addr) {
    int16_t* in = BUF_S16(in_addr);
    int16_t* dry[2] = { BUF_S16(((wet_dry_addr >> 24) & 0xFF) << 4), BUF_S16(((wet_dry_addr >> 16) & 0xFF) << 4) };
    int16_t* wet[2] = { BUF_S16(((wet_dry_addr >> 8) & 0xFF) << 4), BUF_S16(((wet_dry_addr)&0xFF) << 4) };
    int16_t negs[4] = { neg_left ? -1 : 0, neg_right ? -1 : 0, neg_3 ? -4 : 0, neg_2 ? -2 : 0 };
    int swapped[2] = { swap_reverb ? 1 : 0, swap_reverb ? 0 : 1 };
    int n = ROUND_UP_16(n_samples);
    uint16_t vols[2] = { rspa.vol[0], rspa.vol[1] };
    uint16_t rates[2] = { rspa.rate[0], rspa.rate[1] };
    uint16_t vol_wet = rspa.vol_wet;
    uint16_t rate_wet = rspa.rate_wet;
    do {
        int i, j;
        for (i = 0; i < 8; i++) {
            int16_t samples[2] = { *in, *in };
            in++;
            for (j = 0; j < 2; j++) samples[j] = (samples[j] * vols[j] >> 16) ^ negs[j];
            for (j = 0; j < 2; j++) {
                *dry[j] = clamp16(*dry[j] + samples[j]);
                dry[j]++;
                *wet[j] = clamp16(*wet[j] + ((samples[swapped[j]] * vol_wet >> 16) ^ negs[2 + j]));
                wet[j]++;
            }
        }
        vols[0] += rates[0];
        vols[1] += rates[1];
        vol_wet += rate_wet;
        n -= 8;
    } while (n > 0);
}

static void aMix(uint16_t count, int16_t gain, uint16_t in_addr, uint16_t out_addr) {
    int nbytes = ROUND_UP_32(ROUND_DOWN_16(count << 4));
    int16_t* in = BUF_S16(in_addr);
    int16_t* out = BUF_S16(out_addr);
    int i;
    int32_t sample;
    if (gain == -0x8000) {
        while (nbytes > 0) {
            for (i = 0; i < 16; i++) {
                sample = *out - *in++;
                *out++ = clamp16(sample);
            }
            nbytes -= 16 * sizeof(int16_t);
        }
    }
    while (nbytes > 0) {
        for (i = 0; i < 16; i++) {
            sample = ((*out * 0x7fff + *in++ * gain) + 0x4000) >> 15;
            *out++ = clamp16(sample);
        }
        nbytes -= 16 * sizeof(int16_t);
    }
}

static void aS8Dec(uint8_t flags, uint64_t stateEa) {
    int16_t state[16] __attribute__((aligned(16)));
    uint8_t* in = BUF_U8(rspa.in);
    int16_t* out = BUF_S16(rspa.out);
    int nbytes = ROUND_UP_32(rspa.nbytes);
    int i;
    if (flags & A_INIT) {
        memset(out, 0, 16 * sizeof(int16_t));
    } else if (flags & A_LOOP) {
        get_any(out, rspa.adpcm_loop_state, 16 * sizeof(int16_t));
    } else {
        get_any(out, stateEa, 16 * sizeof(int16_t));
    }
    out += 16;
    while (nbytes > 0) {
        for (i = 0; i < 16; i++) *out++ = (int16_t)(*in++ << 8);
        nbytes -= 16 * sizeof(int16_t);
    }
    memcpy(state, out - 16, 16 * sizeof(int16_t));
    put_any(state, stateEa, sizeof(state));
}

static void aAddMixer(uint16_t count, uint16_t in_addr, uint16_t out_addr) {
    int16_t* in = BUF_S16(in_addr);
    int16_t* out = BUF_S16(out_addr);
    int nbytes = ROUND_UP_64(ROUND_DOWN_16(count));
    int i;
    do {
        for (i = 0; i < 16; i++) {
            *out = clamp16(*out + *in++);
            out++;
        }
        nbytes -= 16 * sizeof(int16_t);
    } while (nbytes > 0);
}

static void aDuplicate(uint16_t count, uint16_t in_addr, uint16_t out_addr) {
    uint8_t* in = BUF_U8(in_addr);
    uint8_t* out = BUF_U8(out_addr);
    uint8_t tmp[128];
    memcpy(tmp, in, 128);
    do {
        memcpy(out, tmp, 128);
        out += 128;
    } while (count-- > 0);
}

static void aResampleZoh(uint16_t pitch, uint16_t start_fract) {
    int16_t* in = BUF_S16(rspa.in);
    int16_t* out = BUF_S16(rspa.out);
    int nbytes = ROUND_UP_8(rspa.nbytes);
    uint32_t pos = start_fract;
    uint32_t pitch_add = pitch << 2;
    int i;
    do {
        for (i = 0; i < 4; i++) {
            *out++ = in[pos >> 17];
            pos += pitch_add;
        }
        nbytes -= 4 * sizeof(int16_t);
    } while (nbytes > 0);
}

static void aInterl(uint16_t in_addr, uint16_t out_addr, uint16_t n_samples) {
    int16_t* in = BUF_S16(in_addr);
    int16_t* out = BUF_S16(out_addr);
    int n = ROUND_UP_8(n_samples);
    int i;
    do {
        for (i = 0; i < 8; i++) {
            *out++ = *in++;
            in++;
        }
        n -= 8;
    } while (n > 0);
}

static void aFilter(uint8_t flags, uint16_t count_or_buf, uint64_t ea) {
    if (flags > A_INIT) {
        rspa.filter_count = ROUND_UP_16(count_or_buf);
        get_any(rspa.filter, ea, sizeof(rspa.filter));
    } else {
        int16_t st[16] __attribute__((aligned(16)));
        int16_t tmp[16], tmp2[8];
        int count = rspa.filter_count;
        int16_t* buf = BUF_S16(count_or_buf);
        int i, j;
        if (flags == A_INIT) {
            memset(tmp, 0, 8 * sizeof(int16_t));
            memset(tmp2, 0, 8 * sizeof(int16_t));
        } else {
            get_any(st, ea, sizeof(st));
            memcpy(tmp, st, 8 * sizeof(int16_t));
            memcpy(tmp2, st + 8, 8 * sizeof(int16_t));
        }
        for (i = 0; i < 8; i++) rspa.filter[i] = (tmp2[i] + rspa.filter[i]) / 2;
        do {
            memcpy(tmp + 8, buf, 8 * sizeof(int16_t));
            for (i = 0; i < 8; i++) {
                int64_t sample = 0x4000;
                for (j = 0; j < 8; j++) sample += tmp[i + j] * rspa.filter[7 - j];
                buf[i] = clamp16((int32_t)(sample >> 15));
            }
            memcpy(tmp, tmp + 8, 8 * sizeof(int16_t));
            buf += 8;
            count -= 8 * sizeof(int16_t);
        } while (count > 0);
        memcpy(st, tmp, 8 * sizeof(int16_t));
        memcpy(st + 8, rspa.filter, 8 * sizeof(int16_t));
        put_any(st, ea, sizeof(st));
    }
}

static void aHiLoGain(uint8_t g, uint16_t count, uint16_t addr) {
    int16_t* samples = BUF_S16(addr);
    int nbytes = ROUND_UP_32(count);
    int i;
    do {
        for (i = 0; i < 8; i++) {
            *samples = clamp16((*samples * g) >> 4);
            samples++;
        }
        nbytes -= 8;
    } while (nbytes > 0);
}

static void aUnkCmd19(uint8_t f, uint16_t count, uint16_t out_addr, uint16_t in_addr) {
    int nbytes = ROUND_UP_64(count);
    int16_t* in = BUF_S16(in_addr + f);
    int16_t* out = BUF_S16(out_addr);
    int16_t tbl[32];
    int i;
    memcpy(tbl, in, 32 * sizeof(int16_t));
    do {
        for (i = 0; i < 32; i++) out[i] = clamp16(out[i] * tbl[i]);
        out += 32;
        nbytes -= 32 * sizeof(int16_t);
    } while (nbytes > 0);
}

static void aInterleave(uint16_t dest, uint16_t left, uint16_t right, uint16_t c) {
    int count = ROUND_UP_8(c) / sizeof(int16_t) / 4;
    int16_t* l = BUF_S16(left);
    int16_t* r = BUF_S16(right);
    int16_t* d = BUF_S16(dest);
    while (count > 0) {
        int16_t l0 = *l++, l1 = *l++, l2 = *l++, l3 = *l++;
        int16_t r0 = *r++, r1 = *r++, r2 = *r++, r3 = *r++;
        *d++ = l0;
        *d++ = r0;
        *d++ = l1;
        *d++ = r1;
        *d++ = l2;
        *d++ = r2;
        *d++ = l3;
        *d++ = r3;
        --count;
    }
}

static void Run(const OotMixCmd* c) {
    switch (c->op) {
        case MIX_CLEARBUFFER: memset(BUF_U8(c->a), 0, c->flags ? (int)c->d : ROUND_UP_16((int)c->d)); break;
        case MIX_LOADBUFFER: get_any(BUF_U8(c->a), c->ea, c->b); break;
        case MIX_SAVEBUFFER: put_any(BUF_S16(c->a), c->ea, ROUND_DOWN_16(c->b)); break;
        case MIX_LOADADPCM: get_any(rspa.adpcm_table, c->ea, c->d > sizeof(rspa.adpcm_table) ? sizeof(rspa.adpcm_table) : c->d); break;
        case MIX_SETBUFFER:
            rspa.in = c->a;
            rspa.out = c->b;
            rspa.nbytes = c->c;
            break;
        case MIX_INTERLEAVE: aInterleave(c->a, c->b, c->c, (uint16_t)c->d); break;
        case MIX_DMEMMOVE: memmove(BUF_U8(c->b), BUF_U8(c->a), ROUND_UP_16((int)c->d)); break;
        case MIX_SETLOOP: rspa.adpcm_loop_state = c->ea; break;
        case MIX_ADPCMDEC: aADPCMdec(c->flags, c->ea); break;
        case MIX_RESAMPLE: aResample(c->flags, c->a, c->ea); break;
        case MIX_ENVSETUP1:
            rspa.vol_wet = (uint16_t)(c->flags << 8);
            rspa.rate_wet = c->a;
            rspa.rate[0] = c->b;
            rspa.rate[1] = c->c;
            break;
        case MIX_ENVSETUP2:
            rspa.vol[0] = c->a;
            rspa.vol[1] = c->b;
            break;
        case MIX_ENVMIXER:
            aEnvMixer(c->a, c->b, (c->flags >> 0) & 1, (c->flags >> 1) & 1, (c->flags >> 2) & 1, (c->flags >> 3) & 1,
                      (c->flags >> 4) & 1, (int32_t)c->d);
            break;
        case MIX_MIX: aMix(c->a, (int16_t)c->b, c->c, (uint16_t)c->d); break;
        case MIX_S8DEC: aS8Dec(c->flags, c->ea); break;
        case MIX_ADDMIXER: aAddMixer(c->a, c->b, c->c); break;
        case MIX_DUPLICATE: aDuplicate(c->a, c->b, c->c); break;
        case MIX_RESAMPLEZOH: aResampleZoh(c->a, c->b); break;
        case MIX_INTERL: aInterl(c->a, c->b, c->c); break;
        case MIX_FILTER: aFilter(c->flags, c->a, c->ea); break;
        case MIX_HILOGAIN: aHiLoGain(c->flags, c->a, c->b); break;
        case MIX_UNKCMD19: aUnkCmd19(c->flags, c->a, c->b, c->c); break;
        default: break;
    }
}

#define CHUNK 128
static OotMixCmd sCmds[CHUNK];
static uint32_t sHeader[4] __attribute__((aligned(16)));

int main(uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4) {
    (void)arg1;
    (void)arg2;
    (void)arg3;
    (void)arg4;
    for (;;) {
        const uint32_t ea = spu_read_signal1();
        uint32_t count, done = 0;
        if (ea == 0) break;
        get_any(sHeader, ea, sizeof(sHeader));
        count = sHeader[0];
        while (done < count) {
            const uint32_t n = count - done > CHUNK ? CHUNK : count - done;
            uint32_t i;
            get_any(sCmds, (uint64_t)ea + sizeof(OotMixCmd) + (uint64_t)done * sizeof(OotMixCmd),
                    n * sizeof(OotMixCmd));
            for (i = 0; i < n; i++) Run(&sCmds[i]);
            done += n;
        }
        spu_thread_send_event(SPU_PORT, count & 0xFFFFFF, 0);
    }
    spu_thread_exit(0);
    return 0;
}
