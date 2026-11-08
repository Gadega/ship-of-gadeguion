// SPDX-License-Identifier: GPL-3.0-or-later
// Draw the genuine Shipwright Fast3D vertex stream on RSX. PSL1GHT headers
// stay in C to avoid conflicting with the Shipwright C++ headers.
#include "ps3_platform/fast3d_rsx_ps3.h"
#include "ps3_platform/shipwright_video_ps3.h"
#include <rsx/rsx.h>
#include <sys/thread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
static uint32_t sPs3RsxFrames;
static uint32_t sPs3RsxDraws;
static int sDepthTest, sDepthWrite, sZmodeDecal;
static uint32_t sPs3TwoTexLogged;
void oot_ps3_rsx_set_depth(int test, int write) {
    sDepthTest = test;
    sDepthWrite = write;
}
/* N64 decal surfaces (paths/trails on grass, shadows) as in LUS's GL/D3D/Metal
 * backends: depth LEQUAL plus a -2 depth bias toward the camera. */
/* The debug overlay restores these after drawing (Fast3D only re-sends them on change). */
void oot_ps3_rsx_get_depth_state(int* test, int* write, int* decal) {
    *test = sDepthTest;
    *write = sDepthWrite;
    *decal = sZmodeDecal;
}
void oot_ps3_rsx_set_decal(int decal) {
    sZmodeDecal = decal;
}
unsigned int oot_ps3_rsx_frame_index(void) { return sPs3RsxFrames; }
extern unsigned long long oot_ps3_microseconds_now(void);
static unsigned long long sPs3GpuWaitUs;
static unsigned long long sPs3FillUs, sPs3SubmitUs;
void oot_ps3_rsx_take_timing(unsigned long long* fill, unsigned long long* submit) {
    *fill = sPs3FillUs; *submit = sPs3SubmitUs;
    sPs3FillUs = 0; sPs3SubmitUs = 0;
}
unsigned long long oot_ps3_rsx_take_gpu_wait_us(void) {
    unsigned long long v = sPs3GpuWaitUs;
    sPs3GpuWaitUs = 0;
    return v;
}
/* N64 combiner evaluated as a per-vertex linear form over the texture samples.
 * RGB basis: tex0.rgb, tex0.aaa, tex1.rgb, tex1.aaa, 1.  Alpha basis: tex0.a, tex1.a, 1. */
typedef struct { float k[5][3]; } Ps3RgbForm;
typedef struct { float k[3]; } Ps3AlphaForm;
static int sCodes[2][2][4];
static int sTwoCycle;
static int sFlameMaskTest;
static uint32_t sNonlinearVerts;

static void Ps3RgbOperand(Ps3RgbForm* f, int code, float in[3][4], const Ps3RgbForm* comb) {
    int ch;
    memset(f, 0, sizeof(*f));
    switch (code) {
        case 1: case 2: case 3:
            for (ch = 0; ch < 3; ++ch) f->k[4][ch] = in[code - 1][ch];
            break;
        case 8:  for (ch = 0; ch < 3; ++ch) f->k[0][ch] = 1.0f; break;
        case 9:  for (ch = 0; ch < 3; ++ch) f->k[1][ch] = 1.0f; break;
        case 10: for (ch = 0; ch < 3; ++ch) f->k[2][ch] = 1.0f; break;
        case 11: for (ch = 0; ch < 3; ++ch) f->k[3][ch] = 1.0f; break;
        case 12: for (ch = 0; ch < 3; ++ch) f->k[4][ch] = 1.0f; break;
        case 13: if (comb) *f = *comb; break;
        case 14: for (ch = 0; ch < 3; ++ch) f->k[4][ch] = 0.5f; break;
        default: break;
    }
}

static int Ps3RgbIsConst(const Ps3RgbForm* f) {
    int b, ch;
    for (b = 0; b < 4; ++b) for (ch = 0; ch < 3; ++ch) if (f->k[b][ch] != 0.0f) return 0;
    return 1;
}

__attribute__((unused)) static void Ps3RgbCycle(Ps3RgbForm* out, int cycle, float in[3][4], const Ps3RgbForm* comb) {
    Ps3RgbForm a, b, c, d, diff;
    const Ps3RgbForm* var;
    const Ps3RgbForm* scale;
    int i, ch;
    Ps3RgbOperand(&a, sCodes[cycle][0][0], in, comb);
    Ps3RgbOperand(&b, sCodes[cycle][0][1], in, comb);
    Ps3RgbOperand(&c, sCodes[cycle][0][2], in, comb);
    Ps3RgbOperand(&d, sCodes[cycle][0][3], in, comb);
    for (i = 0; i < 5; ++i) for (ch = 0; ch < 3; ++ch) diff.k[i][ch] = a.k[i][ch] - b.k[i][ch];
    if (Ps3RgbIsConst(&c)) { var = &diff; scale = &c; }
    else if (Ps3RgbIsConst(&diff)) { var = &c; scale = &diff; }
    else {
        /* texture x texture: approximate C's texture terms as white */
        for (ch = 0; ch < 3; ++ch) {
            c.k[4][ch] += c.k[0][ch] + c.k[1][ch] + c.k[2][ch] + c.k[3][ch];
            c.k[0][ch] = c.k[1][ch] = c.k[2][ch] = c.k[3][ch] = 0.0f;
        }
        var = &diff; scale = &c;
        ++sNonlinearVerts;
    }
    for (i = 0; i < 5; ++i) for (ch = 0; ch < 3; ++ch)
        out->k[i][ch] = var->k[i][ch] * scale->k[4][ch] + d.k[i][ch];
}

static void Ps3AlphaOperand(Ps3AlphaForm* f, int code, float in[3][4], const Ps3AlphaForm* comb) {
    memset(f, 0, sizeof(*f));
    switch (code) {
        case 1: case 2: case 3: f->k[2] = in[code - 1][3]; break;
        case 8: case 9:   f->k[0] = 1.0f; break;
        case 10: case 11: f->k[1] = 1.0f; break;
        case 12: f->k[2] = 1.0f; break;
        case 13: if (comb) *f = *comb; break;
        case 14: f->k[2] = 0.5f; break;
        default: break;
    }
}

__attribute__((unused)) static void Ps3AlphaCycle(Ps3AlphaForm* out, int cycle, float in[3][4], const Ps3AlphaForm* comb) {
    Ps3AlphaForm a, b, c, d, diff;
    const Ps3AlphaForm* var;
    float scale;
    int i;
    Ps3AlphaOperand(&a, sCodes[cycle][1][0], in, comb);
    Ps3AlphaOperand(&b, sCodes[cycle][1][1], in, comb);
    Ps3AlphaOperand(&c, sCodes[cycle][1][2], in, comb);
    Ps3AlphaOperand(&d, sCodes[cycle][1][3], in, comb);
    for (i = 0; i < 3; ++i) diff.k[i] = a.k[i] - b.k[i];
    if (c.k[0] == 0.0f && c.k[1] == 0.0f) { var = &diff; scale = c.k[2]; }
    else if (diff.k[0] == 0.0f && diff.k[1] == 0.0f) { var = &c; scale = diff.k[2]; }
    else { var = &diff; scale = c.k[0] + c.k[1] + c.k[2]; ++sNonlinearVerts; }
    for (i = 0; i < 3; ++i) out->k[i] = var->k[i] * scale + d.k[i];
}

/* Combiner compiled once per draw. The structure of the N64 combiner (which operand is a
 * texture term, an input or the previous cycle, and which (A-B)*C+D branch applies)
 * depends only on the draw's codes, so oot_ps3_rsx_set_draw_state builds a plan and the
 * per-vertex work is a few multiply-adds per channel (the per-vertex Ps3RgbCycle /
 * Ps3AlphaCycle path above cost ~3.6 ms per drawn frame). Results match the old path
 * except in the texture x texture case, which both approximate.
 * Set PS3_COMBINER_PLAN to 0 to use the old per-vertex path. */
#define PS3_COMBINER_PLAN 1
typedef struct {
    float one[4]; /* basis weights: rgb tex0.rgb, tex0.aaa, tex1.rgb, tex1.aaa; alpha tex0.a, tex1.a */
    float w[4];   /* input weights: in1, in2, in3, constant 1 */
    float cw;     /* weight of the previous cycle's form (code 13) */
} Ps3Sel;
typedef struct {
    Ps3Sel e, c;          /* A-B and C, for the per-vertex branch when they use the previous cycle */
    int dynamic;          /* 1: choose the (A-B)*C branch per vertex from actual values, like the old path */
    Ps3Sel v, d;          /* out = v * scale + d */
    float sw[4];          /* scale = sw.in + sw[3] + scw*comb.k4 + sOne + sCombBasis*sum(comb basis) */
    float scw, sOne, sCombBasis;
    int hasBasis;         /* the output has texture terms (structurally) */
} Ps3CyclePlan;
static Ps3CyclePlan sPlanRgb[2], sPlanAlpha[2];
/* Cycle 1 when it reads the previous cycle: [0] = previous form has no texture terms at
 * this vertex, [1] = it has (picked per vertex from the actual values). */
static Ps3CyclePlan sPlanRgb1[2], sPlanAlpha1[2];

static void Ps3SelFromCode(Ps3Sel* sel, int code, int alpha, int allowComb) {
    memset(sel, 0, sizeof(*sel));
    switch (code) {
        case 1: case 2: case 3: sel->w[code - 1] = 1.0f; break;
        case 8: case 9: case 10: case 11:
            if (alpha) sel->one[(code - 8) / 2] = 1.0f;
            else sel->one[code - 8] = 1.0f;
            break;
        case 12: sel->w[3] = 1.0f; break;
        case 13: if (allowComb) sel->cw = 1.0f; break;
        case 14: sel->w[3] = 0.5f; break;
        default: break;
    }
}

static int Ps3SelHasBasis(const Ps3Sel* sel, int combHasBasis) {
    return sel->one[0] != 0.0f || sel->one[1] != 0.0f || sel->one[2] != 0.0f || sel->one[3] != 0.0f ||
           (sel->cw != 0.0f && combHasBasis);
}

static void Ps3BuildPlan(Ps3CyclePlan* plan, const int codes[4], int alpha, int allowComb, int combHasBasis) {
    Ps3Sel a, b, c, d, e;
    int k, cTex, eTex, useC, nonLinear;
    Ps3SelFromCode(&a, codes[0], alpha, allowComb);
    Ps3SelFromCode(&b, codes[1], alpha, allowComb);
    Ps3SelFromCode(&c, codes[2], alpha, allowComb);
    Ps3SelFromCode(&d, codes[3], alpha, allowComb);
    for (k = 0; k < 4; ++k) {
        e.one[k] = a.one[k] - b.one[k];
        e.w[k] = a.w[k] - b.w[k];
    }
    e.cw = a.cw - b.cw;
    cTex = Ps3SelHasBasis(&c, combHasBasis);
    eTex = Ps3SelHasBasis(&e, combHasBasis);
    useC = cTex && !eTex;
    nonLinear = cTex && eTex;
    memset(plan, 0, sizeof(*plan));
    plan->e = e;
    plan->c = c;
    /* Whether the previous cycle's form has texture terms depends on its values at each
     * vertex (e.g. a texture term times an input that is 0), so decide per vertex then. */
    /* Exact per-vertex branch only when A-B or C both reads the previous form and has its own
     * texture term (the two could cancel); otherwise the caller picks sPlan*1[has] per vertex. */
    plan->dynamic = allowComb && ((e.cw != 0.0f && (e.one[0] != 0.0f || e.one[1] != 0.0f || e.one[2] != 0.0f ||
                                                    e.one[3] != 0.0f)) ||
                                  (c.cw != 0.0f && (c.one[0] != 0.0f || c.one[1] != 0.0f || c.one[2] != 0.0f ||
                                                    c.one[3] != 0.0f)));
    plan->v = useC ? c : e;
    plan->d = d;
    if (useC) {
        memcpy(plan->sw, e.w, sizeof(plan->sw));
        plan->scw = e.cw;
    } else {
        memcpy(plan->sw, c.w, sizeof(plan->sw));
        plan->scw = c.cw;
        if (nonLinear) { /* texture x texture: C's texture terms approximated as white */
            plan->sOne = c.one[0] + c.one[1] + c.one[2] + c.one[3];
            plan->sCombBasis = c.cw;
        }
    }
    plan->hasBasis = Ps3SelHasBasis(&plan->v, combHasBasis) || Ps3SelHasBasis(&d, combHasBasis);
}

static void Ps3EvalRgbDynamic(const Ps3CyclePlan* p, float in[3][4], float comb[5][3], float out[5][3]) {
    float eb[4][3], cbv[4][3], ek4[3], ck4v[3];
    int ch, b, cTex = 0, eTex = 0, useC, nonLinear;
    for (ch = 0; ch < 3; ++ch) {
        const float x0 = in[0][ch], x1 = in[1][ch], x2 = in[2][ch];
        for (b = 0; b < 4; ++b) {
            eb[b][ch] = p->e.one[b] + p->e.cw * comb[b][ch];
            cbv[b][ch] = p->c.one[b] + p->c.cw * comb[b][ch];
            if (eb[b][ch] != 0.0f) eTex = 1;
            if (cbv[b][ch] != 0.0f) cTex = 1;
        }
        ek4[ch] = p->e.w[0] * x0 + p->e.w[1] * x1 + p->e.w[2] * x2 + p->e.w[3] + p->e.cw * comb[4][ch];
        ck4v[ch] = p->c.w[0] * x0 + p->c.w[1] * x1 + p->c.w[2] * x2 + p->c.w[3] + p->c.cw * comb[4][ch];
    }
    useC = cTex && !eTex;
    nonLinear = cTex && eTex;
    for (ch = 0; ch < 3; ++ch) {
        const float x0 = in[0][ch], x1 = in[1][ch], x2 = in[2][ch];
        const float scale = useC ? ek4[ch]
                                 : ck4v[ch] + (nonLinear ? cbv[0][ch] + cbv[1][ch] + cbv[2][ch] + cbv[3][ch] : 0.0f);
        for (b = 0; b < 4; ++b) {
            out[b][ch] = (useC ? cbv[b][ch] : eb[b][ch]) * scale + p->d.one[b] + p->d.cw * comb[b][ch];
        }
        out[4][ch] = (useC ? ck4v[ch] : ek4[ch]) * scale + p->d.w[0] * x0 + p->d.w[1] * x1 + p->d.w[2] * x2 +
                     p->d.w[3] + p->d.cw * comb[4][ch];
    }
}

static void Ps3EvalRgb(const Ps3CyclePlan* p, float in[3][4], float comb[5][3], int hasComb, float out[5][3]) {
    int ch, b;
    if (hasComb && p->dynamic) {
        Ps3EvalRgbDynamic(p, in, comb, out);
        return;
    }
    for (ch = 0; ch < 3; ++ch) {
        const float x0 = in[0][ch], x1 = in[1][ch], x2 = in[2][ch];
        float cb[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        float ck4 = 0.0f, scale, vk4, dk4;
        if (hasComb) {
            cb[0] = comb[0][ch]; cb[1] = comb[1][ch]; cb[2] = comb[2][ch]; cb[3] = comb[3][ch];
            ck4 = comb[4][ch];
        }
        scale = p->sw[0] * x0 + p->sw[1] * x1 + p->sw[2] * x2 + p->sw[3] + p->scw * ck4 + p->sOne +
                p->sCombBasis * (cb[0] + cb[1] + cb[2] + cb[3]);
        for (b = 0; b < 4; ++b) {
            out[b][ch] = (p->v.one[b] + p->v.cw * cb[b]) * scale + p->d.one[b] + p->d.cw * cb[b];
        }
        vk4 = p->v.w[0] * x0 + p->v.w[1] * x1 + p->v.w[2] * x2 + p->v.w[3] + p->v.cw * ck4;
        dk4 = p->d.w[0] * x0 + p->d.w[1] * x1 + p->d.w[2] * x2 + p->d.w[3] + p->d.cw * ck4;
        out[4][ch] = vk4 * scale + dk4;
    }
}

static void Ps3EvalAlpha(const Ps3CyclePlan* p, float in[3][4], const float comb[3], int hasComb, float out[3]) {
    const float x0 = in[0][3], x1 = in[1][3], x2 = in[2][3];
    if (hasComb && p->dynamic) {
        const float e0 = p->e.one[0] + p->e.cw * comb[0], e1 = p->e.one[1] + p->e.cw * comb[1];
        const float c0 = p->c.one[0] + p->c.cw * comb[0], c1 = p->c.one[1] + p->c.cw * comb[1];
        const float ek = p->e.w[0] * x0 + p->e.w[1] * x1 + p->e.w[2] * x2 + p->e.w[3] + p->e.cw * comb[2];
        const float ck = p->c.w[0] * x0 + p->c.w[1] * x1 + p->c.w[2] * x2 + p->c.w[3] + p->c.cw * comb[2];
        const int cTex = c0 != 0.0f || c1 != 0.0f, eTex = e0 != 0.0f || e1 != 0.0f;
        const int useC = cTex && !eTex, nonLinear = cTex && eTex;
        const float scale = useC ? ek : (nonLinear ? c0 + c1 + ck : ck);
        out[0] = (useC ? c0 : e0) * scale + p->d.one[0] + p->d.cw * comb[0];
        out[1] = (useC ? c1 : e1) * scale + p->d.one[1] + p->d.cw * comb[1];
        out[2] = (useC ? ck : ek) * scale + p->d.w[0] * x0 + p->d.w[1] * x1 + p->d.w[2] * x2 + p->d.w[3] +
                 p->d.cw * comb[2];
        return;
    }
    const float c0 = hasComb ? comb[0] : 0.0f, c1 = hasComb ? comb[1] : 0.0f, ck = hasComb ? comb[2] : 0.0f;
    const float scale = p->sw[0] * x0 + p->sw[1] * x1 + p->sw[2] * x2 + p->sw[3] + p->scw * ck + p->sOne +
                        p->sCombBasis * (c0 + c1);
    out[0] = (p->v.one[0] + p->v.cw * c0) * scale + p->d.one[0] + p->d.cw * c0;
    out[1] = (p->v.one[1] + p->v.cw * c1) * scale + p->d.one[1] + p->d.cw * c1;
    out[2] = (p->v.w[0] * x0 + p->v.w[1] * x1 + p->v.w[2] * x2 + p->v.w[3] + p->v.cw * ck) * scale +
             p->d.w[0] * x0 + p->d.w[1] * x1 + p->d.w[2] * x2 + p->d.w[3] + p->d.cw * ck;
}
/* Combiner forms for one vertex from its inputs (shared by the CPU fill and the GPU
 * T&L combiner probing): rgb forms k0..k4 and alpha forms. */
static void Ps3EvalForms(float in[3][4], float rgbOutF[5][3], float alphaOutF[3]) {
    float form0[5][3], form1[5][3], alpha0[3], alpha1[3];
    float (*rgbOut)[3] = form0;
    const float* alphaOut = alpha0;
    int b;
    Ps3EvalRgb(&sPlanRgb[0], in, form0, 0, form0);
    Ps3EvalAlpha(&sPlanAlpha[0], in, alpha0, 0, alpha0);
    if (sTwoCycle) {
        const int rgbHas = form0[0][0] != 0.0f || form0[0][1] != 0.0f || form0[0][2] != 0.0f ||
                           form0[1][0] != 0.0f || form0[1][1] != 0.0f || form0[1][2] != 0.0f ||
                           form0[2][0] != 0.0f || form0[2][1] != 0.0f || form0[2][2] != 0.0f ||
                           form0[3][0] != 0.0f || form0[3][1] != 0.0f || form0[3][2] != 0.0f;
        const int alphaHas = alpha0[0] != 0.0f || alpha0[1] != 0.0f;
        Ps3EvalRgb(&sPlanRgb1[rgbHas], in, form0, 1, form1);
        Ps3EvalAlpha(&sPlanAlpha1[alphaHas], in, alpha0, 1, alpha1);
        rgbOut = form1;
        alphaOut = alpha1;
    }
    for (b = 0; b < 5; ++b) {
        rgbOutF[b][0] = rgbOut[b][0];
        rgbOutF[b][1] = rgbOut[b][1];
        rgbOutF[b][2] = rgbOut[b][2];
    }
    alphaOutF[0] = alphaOut[0];
    alphaOutF[1] = alphaOut[1];
    alphaOutF[2] = alphaOut[2];
}
static uint32_t sDrawUv0 = 4U, sDrawUv1 = 0xFFFFU, sDrawFog = 0xFFFFU;
static uint32_t sDrawInputOff = 4U, sDrawInputPer = 3U;
static float sDrawFlags = 0.0f;
static float sLeanAlpha, sLeanEdge, sLeanThreshold;
static float sDrawPack[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
static int sPlansValid;
void oot_ps3_rsx_set_draw_state(uint32_t uv0Off, uint32_t uv1Off, uint32_t fogOff, uint32_t inputOff,
                                uint32_t inputPer, float flags, const float packs[4]) {
    sDrawUv0 = uv0Off; sDrawUv1 = uv1Off; sDrawFog = fogOff;
    sDrawInputOff = inputOff; sDrawInputPer = inputPer;
    /* The plans depend only on the codes and flags: skip rebuilding them when those
     * are the same as for the previous draw (most draws). */
    if (sDrawFlags == flags && sDrawPack[0] == packs[0] && sDrawPack[1] == packs[1] && sDrawPack[2] == packs[2] &&
        sDrawPack[3] == packs[3] && sPlansValid) {
        return;
    }
    sPlansValid = 1;
    sDrawFlags = flags;
    sDrawPack[0] = packs[0]; sDrawPack[1] = packs[1]; sDrawPack[2] = packs[2]; sDrawPack[3] = packs[3];
    {
        int p, k;
        for (p = 0; p < 4; ++p) {
            uint32_t v = (uint32_t)(packs[p] + 0.5f);
            for (k = 0; k < 4; ++k) sCodes[p >> 1][p & 1][k] = (int)((v >> (4 * k)) & 15U);
        }
        sTwoCycle = (int)(((uint32_t)(flags + 0.5f)) & 1U);
        Ps3BuildPlan(&sPlanRgb[0], sCodes[0][0], 0, 0, 0);
        Ps3BuildPlan(&sPlanAlpha[0], sCodes[0][1], 1, 0, 0);
        Ps3BuildPlan(&sPlanRgb[1], sCodes[1][0], 0, 1, sPlanRgb[0].hasBasis);
        Ps3BuildPlan(&sPlanAlpha[1], sCodes[1][1], 1, 1, sPlanAlpha[0].hasBasis);
        Ps3BuildPlan(&sPlanRgb1[0], sCodes[1][0], 0, 1, 0);
        Ps3BuildPlan(&sPlanRgb1[1], sCodes[1][0], 0, 1, 1);
        Ps3BuildPlan(&sPlanAlpha1[0], sCodes[1][1], 1, 1, 0);
        Ps3BuildPlan(&sPlanAlpha1[1], sCodes[1][1], 1, 1, 1);
        /* TEST: title flame tiles output only texture 0 (the mask), white with its alpha */
        sFlameMaskTest = 0; /* mask test finished */
    }
}
#include "fast3d-rsx-shaders.h"

extern void oot_ps3_early_trace(const char* message);

/* 16384 vertices (2.9 MB per buffer): a Kokiri Forest frame (~8000) fits without
 * a mid-frame wrap, which needs a full GPU sync. */
enum { MAX_VERTICES = 16384, MAX_TEXTURES = 1024 };
enum { VERTEX_FLOATS = 44 };
typedef struct {
    gcmTexture image;
    uint32_t* pixels;
    uint32_t capacity; /* bytes allocated at pixels */
    uint32_t useFence; /* GPU fence covering the last draw that sampled it; 0 = never drawn */
    uint8_t wrapS, wrapT, linear;
} GpuTexture;

static uint32_t Ps3GcmWrap(uint8_t mode) {
    return mode == 2 ? GCM_TEXTURE_CLAMP_TO_EDGE : (mode == 1 ? GCM_TEXTURE_MIRRORED_REPEAT : GCM_TEXTURE_REPEAT);
}
/* Framebuffer textures (Fast3D CopyFramebuffer / SelectTextureFb): the pause menu
 * background is a copy of the game frame. Texture ids with this bit set name
 * framebuffer `id & 0xFF` instead of an uploaded texture. */
#define PS3_FB_TEXTURE_BIT 0x8000U
enum { MAX_FB_TEXTURES = 8 };
static GpuTexture sFbTextures[MAX_FB_TEXTURES];
static GpuTexture textures[MAX_TEXTURES];
/* The debug menu's text panel (ps3_debug_overlay.c): its own texture, outside the
 * game's texture ids. */
#define PS3_OVERLAY_TEXTURE_ID 0x4000U
static GpuTexture sOverlayTexture;
static GpuTexture* Ps3Texture(uint32_t id) {
    if (id == PS3_OVERLAY_TEXTURE_ID) return &sOverlayTexture;
    if (id & PS3_FB_TEXTURE_BIT) {
        const uint32_t fb = id & 0xFFU;
        return fb < MAX_FB_TEXTURES && sFbTextures[fb].pixels ? &sFbTextures[fb] : &textures[MAX_TEXTURES - 1];
    }
    return id && id < MAX_TEXTURES ? &textures[id - 1] : &textures[MAX_TEXTURES - 1];
}
void oot_ps3_rsx_texture_sampler(uint32_t id, int linear, int wrapS, int wrapT) {
    if (id & PS3_FB_TEXTURE_BIT) {
        GpuTexture* t = Ps3Texture(id);
        t->linear = (uint8_t)(linear ? 1 : 0);
        t->wrapS = t->wrapT = 2; /* a screen copy never repeats */
        return;
    }
    if (!id || id > MAX_TEXTURES) return;
    textures[id - 1].linear = (uint8_t)(linear ? 1 : 0);
    textures[id - 1].wrapS = (uint8_t)wrapS;
    textures[id - 1].wrapT = (uint8_t)wrapT;
}
static uint32_t sDumpedIds[16];
static uint32_t sDumpedCount;
/* Diagnostic: print a texture's alpha as a 32x16 ASCII picture, once per id. */
static void Ps3DumpTextureAscii(const GpuTexture* t, uint32_t id) {
    static const char ramp[] = " .:-=+*#%@";
    uint32_t i, x, y;
    const uint32_t w = t->image.width, h = t->image.height;
    for (i = 0; i < sDumpedCount; ++i) if (sDumpedIds[i] == id) return;
    if (sDumpedCount >= 16 || !t->pixels || !w || !h) return;
    sDumpedIds[sDumpedCount++] = id;
    {
        char head[96];
        snprintf(head, sizeof(head), "graph: Play texdump id=%u %ux%u (alpha)", (unsigned)id, (unsigned)w, (unsigned)h);
        oot_ps3_early_trace(head);
    }
    for (y = 0; y < 16; ++y) {
        char row[80] = "graph: Play texdump |";
        size_t n = strlen(row);
        for (x = 0; x < 32; ++x) {
            const uint32_t px = x * w / 32U, py = y * h / 16U;
            const uint8_t a = ((const uint8_t*)t->pixels)[(py * w + px) * 4U + 3U];
            row[n++] = ramp[(a * 9U) / 255U];
        }
        row[n++] = '|';
        row[n] = '\0';
        oot_ps3_early_trace(row);
    }
}
static gcmContextData* ctx;
static rsxVertexProgram* vp;
static rsxFragmentProgram* fp;
static void* vpCode;
static uint32_t fpOffset;
static uint32_t* fpMemory;
/* Lean program for normal surfaces (material 0); the full one only for the title
 * logo/lettering materials. sCurrentFp: 0 = full, 1 = lean, -1 = unknown. */
static rsxFragmentProgram* fpGeneric;
static uint32_t fpGenericOffset;
static uint32_t* fpGenericMemory;
static rsxFragmentProgram* fpGenericClip; /* lean + clip(): cut-out surfaces only */
static uint32_t fpGenericClipOffset;
static uint32_t* fpGenericClipMemory;
static int sCurrentFp = -1; /* 0 = full, 1 = lean solid, 2 = lean cut-out */
static float* vertices[2];
static uint16_t* indices;
static uint32_t indexOffset;
static uint32_t frameIndex;
static uint32_t vertexCursor;
/* GPU vertex pipeline (fast3d_ps3_tnl.vcg): its program, its own record ring
 * (16-byte N64-style vertices) and which vertex program is loaded. */
static rsxVertexProgram* vpTnl;
/* Debug menu "GPU vertex path": 2 = white probe shader (positions only). */
static rsxVertexProgram* vpTnlMain;
static rsxVertexProgram* vpTnlProbe;
static rsxVertexProgram* vpTnlAttrProbe; /* mode 4: raw model coordinates, no constants */
static void* vpTnlMainCode;
static void* vpTnlProbeCode;
static void* vpTnlAttrProbeCode;
static int sTnlDebugMode;
static int sTnlIndexDraw; /* self-test: draw the T&L records with an index list like the CPU path */
void oot_ps3_rsx_tnl_debug_mode(int mode) {
    sTnlDebugMode = mode;
}
static void* vpTnlCode;
static int sCurrentVp = -1; /* 0 = pass-through (CPU path), 1 = T&L */
enum { TNL_MAX_RECORDS = 65536 };
static uint8_t* tnlRecords[2];
static uint32_t tnlCursor;
static int sCullEnabled;
static unsigned int sTnlDraws, sTnlVerts, sTnlVpSwitches, sCpuDraws, sTnlConstUploads;
static unsigned int sCombHits, sCombMisses;
static uint32_t* depthBuffer;
static uint32_t depthOffset;
static int prepared;
static uint32_t frameWidth, frameHeight;
/* Framebuffer texture being drawn (on the frame; oot_ps3_rsx_set_target), -1 = none.
 * Viewport and scissor y are flipped against the surface height. */
static uint32_t sTargetHeight;
static int sTargetFb = -1;

/* GPU fences. The RSX writes each queued reference value into the REF register when
 * it reaches it, so REF tells how far the GPU has got. The CPU only waits when it is
 * about to reuse memory that unfinished GPU work still reads; it no longer stalls on
 * every texture upload (rsxFinish with a fixed value never waited at all, and with a
 * fresh value cost 20-30 ms per frame). REF starts at 0xFFFFFFFF; values start at 0x101. */
static uint32_t sFenceSubmitted = 0x100;
static uint32_t sFrameFence[2];

static int fence_passed(uint32_t value) {
    return (int32_t)(gcmGetControlRegister()->ref - value) >= 0;
}

static uint32_t fence_submit(void) {
    ++sFenceSubmitted;
    rsxSetReferenceCommand(ctx, sFenceSubmitted);
    rsxFlushBuffer(ctx);
    return sFenceSubmitted;
}

static int sTargetFbForStall = -1; /* render target when a wait began (stall report) */
static void fence_wait(uint32_t value) {
    unsigned long long t0;
    unsigned spins = 0;
    int reported = 0;
    if (value == 0 || fence_passed(value)) return;
    t0 = oot_ps3_microseconds_now();
    while (!fence_passed(value)) {
        if (++spins > 64) sysThreadYield();
        if (!reported && (spins & 1023U) == 0 && oot_ps3_microseconds_now() - t0 > 3000000ULL) {
            /* The RSX stopped: say so (the watchdog ends the run later). */
            char line[128];
            reported = 1;
            snprintf(line, sizeof(line), "graph: Play rsx GPU STALL: fence %u not reached in 3 s (ref=%u) target=%d",
                     (unsigned)value, (unsigned)gcmGetControlRegister()->ref, sTargetFbForStall);
            oot_ps3_early_trace(line);
        }
    }
    sPs3GpuWaitUs += oot_ps3_microseconds_now() - t0;
}

/* Full sync: everything queued so far has finished on the GPU. */
static void gpu_wait(void) {
    fence_wait(fence_submit());
}

/* Texture memory replaced while the GPU may still sample it is freed once its fence passes. */
enum { MAX_DEFERRED_FREES = 2048 };
static struct { void* ptr; uint32_t fence; } sDeferredFrees[MAX_DEFERRED_FREES];
static uint32_t sDeferredFreeCount;

static void deferred_free_collect(int all) {
    uint32_t i = 0;
    while (i < sDeferredFreeCount) {
        if (all || fence_passed(sDeferredFrees[i].fence)) {
            rsxFree(sDeferredFrees[i].ptr);
            sDeferredFrees[i] = sDeferredFrees[--sDeferredFreeCount];
        } else {
            ++i;
        }
    }
}

static void deferred_free(void* ptr, uint32_t fence) {
    if (fence == 0 || fence_passed(fence)) {
        rsxFree(ptr);
        return;
    }
    if (sDeferredFreeCount == MAX_DEFERRED_FREES) {
        gpu_wait();
        deferred_free_collect(1);
    }
    sDeferredFrees[sDeferredFreeCount].ptr = ptr;
    sDeferredFrees[sDeferredFreeCount].fence = fence;
    sDeferredFreeCount++;
}

static int gpu_prepare(void) {
    uint32_t bytes;
    if (prepared) return 1;
    ctx = (gcmContextData*)oot_ps3_shipwright_video_rsx_context();
    if (!ctx) return 0;
    vp = (rsxVertexProgram*)oot_ps3_rsx_vpo;
    fp = (rsxFragmentProgram*)oot_ps3_rsx_fpo;
    rsxVertexProgramGetUCode(vp, &vpCode, &bytes);
    { void* code; rsxFragmentProgramGetUCode(fp, &code, &bytes);
      fpMemory = (uint32_t*)rsxMemalign(64, bytes);
      if (!fpMemory) return 0;
      memcpy(fpMemory, code, bytes);
      if (rsxAddressToOffset(fpMemory, &fpOffset)) return 0;
    }
    fpGeneric = (rsxFragmentProgram*)oot_ps3_rsx_fpo_generic;
    { void* code; rsxFragmentProgramGetUCode(fpGeneric, &code, &bytes);
      fpGenericMemory = (uint32_t*)rsxMemalign(64, bytes);
      if (!fpGenericMemory) return 0;
      memcpy(fpGenericMemory, code, bytes);
      if (rsxAddressToOffset(fpGenericMemory, &fpGenericOffset)) return 0;
    }
    fpGenericClip = (rsxFragmentProgram*)oot_ps3_rsx_fpo_generic_clip;
    { void* code; rsxFragmentProgramGetUCode(fpGenericClip, &code, &bytes);
      fpGenericClipMemory = (uint32_t*)rsxMemalign(64, bytes);
      if (!fpGenericClipMemory) return 0;
      memcpy(fpGenericClipMemory, code, bytes);
      if (rsxAddressToOffset(fpGenericClipMemory, &fpGenericClipOffset)) return 0;
    }
    vertices[0] = (float*)rsxMemalign(128, MAX_VERTICES * VERTEX_FLOATS * sizeof(float));
    vertices[1] = (float*)rsxMemalign(128, MAX_VERTICES * VERTEX_FLOATS * sizeof(float));
    indices = (uint16_t*)rsxMemalign(128, MAX_VERTICES * sizeof(uint16_t));
    if (!vertices[0] || !vertices[1] || !indices ||
        rsxAddressToOffset(indices, &indexOffset)) return 0;
    for (uint32_t i = 0; i < MAX_VERTICES; ++i) indices[i] = (uint16_t)i;
    /* Both the real T&L program and the transform-only probe (every vertex white). */
    vpTnlMain = (rsxVertexProgram*)oot_ps3_rsx_vpo_tnl;
    vpTnlProbe = (rsxVertexProgram*)oot_ps3_rsx_vpo_tnl_probe;
    rsxVertexProgramGetUCode(vpTnlMain, &vpTnlMainCode, &bytes);
    rsxVertexProgramGetUCode(vpTnlProbe, &vpTnlProbeCode, &bytes);
    vpTnlAttrProbe = (rsxVertexProgram*)oot_ps3_rsx_vpo_tnl_attrprobe;
    rsxVertexProgramGetUCode(vpTnlAttrProbe, &vpTnlAttrProbeCode, &bytes);
    vpTnl = vpTnlMain;
    vpTnlCode = vpTnlMainCode;
    tnlRecords[0] = (uint8_t*)rsxMemalign(128, TNL_MAX_RECORDS * 16);
    tnlRecords[1] = (uint8_t*)rsxMemalign(128, TNL_MAX_RECORDS * 16);
    if (!tnlRecords[0] || !tnlRecords[1]) vpTnl = NULL; /* GPU path unavailable; CPU path still works */
    prepared = 1;
    { const uint8_t white[4] = { 255, 255, 255, 255 };
      if (!oot_ps3_rsx_texture(MAX_TEXTURES, white, 1, 1)) return 0;
    }
    oot_ps3_early_trace("fast3d-rsx: shader programs and vertex buffers ready");
    return 1;
}

static int gpu_depth(uint32_t width, uint32_t height) {
    if (depthBuffer && frameWidth == width && frameHeight == height) return 1;
    if (depthBuffer) { gpu_wait(); rsxFree(depthBuffer); depthBuffer = NULL; }
    depthBuffer = (uint32_t*)rsxMemalign(128, (size_t)width * height * 4);
    if (!depthBuffer || rsxAddressToOffset(depthBuffer, &depthOffset)) return 0;
    frameWidth = width; frameHeight = height;
    return 1;
}

/* Binds a linear colour + Z24S8 surface and resets viewport/scissor to all of it. */
static void Ps3SetSurface(uint32_t format, uint32_t colorOffset, uint32_t colorPitch, uint32_t zOffset,
                          uint32_t zPitch, uint32_t width, uint32_t height) {
    gcmSurface surface;
    float scale[4], offset[4];
    memset(&surface, 0, sizeof(surface));
    surface.colorFormat = (u8)format;
    surface.colorTarget = GCM_SURFACE_TARGET_0;
    surface.colorLocation[0] = GCM_LOCATION_RSX;
    surface.colorOffset[0] = colorOffset;
    surface.colorPitch[0] = colorPitch;
    for (unsigned i = 1; i < 4; ++i) {
        surface.colorLocation[i] = GCM_LOCATION_RSX;
        surface.colorPitch[i] = 64;
    }
    surface.depthFormat = GCM_SURFACE_ZETA_Z24S8;
    surface.depthLocation = GCM_LOCATION_RSX;
    surface.depthOffset = zOffset;
    surface.depthPitch = zPitch;
    surface.type = GCM_SURFACE_TYPE_LINEAR;
    surface.antiAlias = GCM_SURFACE_CENTER_1;
    surface.width = (u16)width;
    surface.height = (u16)height;
    rsxSetSurface(ctx, &surface);
    scale[0] = width * .5f; scale[1] = height * -.5f;
    scale[2] = .5f; scale[3] = 0;
    offset[0] = width * .5f; offset[1] = height * .5f;
    offset[2] = .5f; offset[3] = 0;
    rsxSetViewport(ctx, 0, 0, (u16)width, (u16)height, 0.0f, 1.0f, scale, offset);
    for (int i = 0; i < 8; ++i) rsxSetViewportClip(ctx, i, width, height);
    rsxSetScissor(ctx, 0, 0, (u16)width, (u16)height);
    sTargetHeight = height;
}

/* ---- GPU vertex path self-test --------------------------------------------------
 * Once, in the first frame: draws a triangle over the screen centre in several ways
 * and reads the centre pixel back, so one run shows which variant reaches the screen.
 * 0 CPU path (control), 1 attribute probe + float records, 2 attribute probe + packed
 * s16/u8 records, 3 = 2 drawn with an index list, 4 T&L program + float records,
 * 5 T&L program + packed records. Results: trace "rsx-selftest" lines and the debug
 * menu (oot_ps3_rsx_selftest_summary). */
int oot_ps3_rsx_tnl_draw(const void* records, uint32_t count, float* consts, int drawDirty, uint32_t firstConst,
                         uint32_t numConst, uint32_t texture0_id, uint32_t texture1_id, int alpha, int cullMode,
                         int cullFlip, int floatAttribs);
int oot_ps3_rsx_triangles(const float* vbo, uint32_t stride, uint32_t triangles, uint32_t texture0_id,
                          uint32_t texture1_id, int alpha, uint32_t inputs, int material);
const uint32_t* oot_ps3_shipwright_video_target_pixels(uint32_t* width, uint32_t* height);
static char sSelfTestSummary[64] = "GPU self-test: not run";
const char* oot_ps3_rsx_selftest_summary(void) {
    return sSelfTestSummary;
}
static uint32_t Ps3SelfTestPixel(uint32_t width, uint32_t height) {
    uint32_t w = 0, h = 0;
    const uint32_t* px;
    gpu_wait();
    px = oot_ps3_shipwright_video_target_pixels(&w, &h);
    if (px == NULL || w != width || h != height) return 0xDEADBEEFU;
    return px[(size_t)(h / 2) * w + w / 2] & 0x00FFFFFFU;
}
static void Ps3SelfTestClear(void) {
    rsxSetClearColor(ctx, 0);
    rsxSetClearDepthStencil(ctx, 0xffffff00);
    rsxClearSurface(ctx, GCM_CLEAR_R | GCM_CLEAR_G | GCM_CLEAR_B | GCM_CLEAR_A | GCM_CLEAR_Z);
}
/* Raw test draw: three float vertices (44 floats each, the CPU path's layout) drawn with
 * vertex program `prog`. fullBinding: all 11 inputs like the CPU path, else only POS,
 * TEX0 (2 floats) and COLOR0 like the GPU path (the rest switched off with size 0).
 * indexDraw: rsxDrawIndexArray (CPU path) or rsxDrawVertexArray (GPU path). */
static void Ps3RawTestDraw(rsxVertexProgram* prog, void* code, const float pos[3][4], int fullBinding,
                           int indexDraw) {
    GpuTexture* white = &textures[MAX_TEXTURES - 1];
    float* dst = vertices[frameIndex];
    uint32_t offset, i, a;
    float vtx[VERTEX_FLOATS];
    for (i = 0; i < 3; ++i) {
        memset(vtx, 0, sizeof(vtx));
        memcpy(vtx, pos[i], 4 * sizeof(float));
        vtx[24] = vtx[25] = vtx[26] = vtx[27] = 1.0f; /* TEXCOORD5 = k4 = white */
        vtx[32] = vtx[33] = vtx[34] = vtx[35] = 1.0f; /* COLOR0 */
        vtx[36] = vtx[37] = vtx[38] = vtx[39] = 1.0f; /* COLOR1 */
        memcpy(dst + i * VERTEX_FLOATS, vtx, sizeof(vtx));
    }
    rsxLoadVertexProgram(ctx, prog, code);
    rsxLoadFragmentProgramLocation(ctx, fpGeneric, fpGenericOffset, GCM_LOCATION_RSX);
    for (i = 0; i < 2; ++i) {
        rsxTextureControl(ctx, i, GCM_TRUE, 0, 0, GCM_TEXTURE_MAX_ANISO_1);
        rsxLoadTexture(ctx, i, &white->image);
        rsxTextureWrapMode(ctx, i, GCM_TEXTURE_CLAMP_TO_EDGE, GCM_TEXTURE_CLAMP_TO_EDGE, GCM_TEXTURE_CLAMP_TO_EDGE, 0,
                           GCM_TEXTURE_ZFUNC_LESS, 0);
        rsxTextureFilter(ctx, i, 0, GCM_TEXTURE_NEAREST, GCM_TEXTURE_NEAREST, GCM_TEXTURE_CONVOLUTION_QUINCUNX);
    }
    rsxSetBlendEnable(ctx, GCM_FALSE);
    rsxAddressToOffset(dst, &offset);
    if (fullBinding) {
        static const uint8_t kAttr[11] = { GCM_VERTEX_ATTRIB_POS, GCM_VERTEX_ATTRIB_TEX0, GCM_VERTEX_ATTRIB_TEX1,
                                           GCM_VERTEX_ATTRIB_TEX2, GCM_VERTEX_ATTRIB_TEX3, GCM_VERTEX_ATTRIB_TEX4,
                                           GCM_VERTEX_ATTRIB_TEX5, GCM_VERTEX_ATTRIB_TEX6, GCM_VERTEX_ATTRIB_COLOR0,
                                           GCM_VERTEX_ATTRIB_COLOR1, GCM_VERTEX_ATTRIB_TEX7 };
        for (a = 0; a < 11; ++a)
            rsxBindVertexArrayAttrib(ctx, kAttr[a], 0, offset + a * 4 * sizeof(float), VERTEX_FLOATS * sizeof(float), 4,
                                     GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    } else {
        static const uint8_t kOff[8] = { GCM_VERTEX_ATTRIB_COLOR1, GCM_VERTEX_ATTRIB_TEX1, GCM_VERTEX_ATTRIB_TEX2,
                                         GCM_VERTEX_ATTRIB_TEX3, GCM_VERTEX_ATTRIB_TEX4, GCM_VERTEX_ATTRIB_TEX5,
                                         GCM_VERTEX_ATTRIB_TEX6, GCM_VERTEX_ATTRIB_TEX7 };
        rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_POS, 0, offset, VERTEX_FLOATS * sizeof(float), 4,
                                 GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
        rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX0, 0, offset + 4 * sizeof(float),
                                 VERTEX_FLOATS * sizeof(float), 2, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
        rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_COLOR0, 0, offset + 32 * sizeof(float),
                                 VERTEX_FLOATS * sizeof(float), 4, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
        for (a = 0; a < 8; ++a)
            rsxBindVertexArrayAttrib(ctx, kOff[a], 0, 0, 0, 0, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    }
    if (indexDraw) {
        rsxDrawIndexArray(ctx, GCM_TYPE_TRIANGLES, indexOffset, 3, GCM_INDEX_TYPE_16B, GCM_LOCATION_RSX);
    } else {
        rsxDrawVertexArray(ctx, GCM_TYPE_TRIANGLES, 0, 3);
    }
}

static void Ps3TnlSelfTest(uint32_t width, uint32_t height) {
    /* Clip-space triangle for the pass-through program; raw coordinates for the probes
     * (they divide by 2048). */
    static const float kClip[3][4] = { { -0.5f, -0.5f, 0.5f, 1.0f }, { 0.5f, -0.5f, 0.5f, 1.0f }, { 0.0f, 0.5f, 0.5f, 1.0f } };
    static const float kRaw[3][4] = { { -1024.0f, -1024.0f, 0.0f, 40.0f }, { 1024.0f, -1024.0f, 0.0f, 40.0f },
                                      { 0.0f, 1024.0f, 0.0f, 40.0f } };
    static const char* const kNames[7] = {
        "CPU path (control)", "pass-through program, full binding, index list",
        "pass-through program, full binding, vertex array", "attr probe, full binding, index list",
        "attr probe 2 (all outputs), full binding, index list", "attr probe 2 (all outputs), GPU-path binding, vertex array",
        "attr probe, GPU-path binding, vertex array (old variant)",
    };
    rsxVertexProgram* probe2 = (rsxVertexProgram*)oot_ps3_rsx_vpo_tnl_attrprobe2;
    void* probe2Code;
    uint32_t bytes, results[7];
    char line[200];
    int v, used;
    rsxVertexProgramGetUCode(probe2, &probe2Code, &bytes);
    for (v = 0; v < 7; ++v) {
        Ps3SelfTestClear();
        rsxSetDepthTestEnable(ctx, GCM_FALSE);
        rsxSetDepthWriteEnable(ctx, GCM_FALSE);
        rsxSetCullFaceEnable(ctx, GCM_FALSE);
        sCullEnabled = 0;
        rsxSetColorMask(ctx, GCM_COLOR_MASK_B | GCM_COLOR_MASK_G | GCM_COLOR_MASK_R | GCM_COLOR_MASK_A);
        sDepthTest = sDepthWrite = sZmodeDecal = 0;
        switch (v) {
            case 0: {
                /* The real CPU path: x, y, z, w, u, v; combiner D = TEXEL0 of the white texture. */
                const float texel0 = 8.0f * 4096.0f;
                const float packs[4] = { texel0, texel0, texel0, texel0 };
                const float vbo[3][6] = { { -0.5f, -0.5f, 0.5f, 1.0f, 0.0f, 0.0f },
                                          { 0.5f, -0.5f, 0.5f, 1.0f, 0.0f, 0.0f },
                                          { 0.0f, 0.5f, 0.5f, 1.0f, 0.0f, 0.0f } };
                sCurrentVp = -1;
                sCurrentFp = -1;
                oot_ps3_rsx_set_draw_state(4U, 0xFFFFU, 0xFFFFU, 6U, 4U, 0.0f, packs);
                oot_ps3_rsx_triangles(&vbo[0][0], 6U, 1U, 0U, 0U, 0, 0U, 0);
            } break;
            case 1: Ps3RawTestDraw(vp, vpCode, kClip, 1, 1); break;
            case 2: Ps3RawTestDraw(vp, vpCode, kClip, 1, 0); break;
            case 3: Ps3RawTestDraw(vpTnlAttrProbe, vpTnlAttrProbeCode, kRaw, 1, 1); break;
            case 4: Ps3RawTestDraw(probe2, probe2Code, kRaw, 1, 1); break;
            case 5: Ps3RawTestDraw(probe2, probe2Code, kRaw, 0, 0); break;
            default: Ps3RawTestDraw(vpTnlAttrProbe, vpTnlAttrProbeCode, kRaw, 0, 0); break;
        }
        results[v] = Ps3SelfTestPixel(width, height);
        tnlCursor = 0;
        vertexCursor = 0;
    }
    sTnlDebugMode = 0;
    sCurrentVp = -1;
    sCurrentFp = -1;
    used = snprintf(sSelfTestSummary, sizeof(sSelfTestSummary), "GPU self-test:");
    for (v = 0; v < 7; ++v) {
        const int pass = results[v] != 0xDEADBEEFU && (results[v] & 0xFF) > 0x80 && ((results[v] >> 8) & 0xFF) > 0x80 &&
                         ((results[v] >> 16) & 0xFF) > 0x80;
        if (used < (int)sizeof(sSelfTestSummary) - 3)
            used += snprintf(sSelfTestSummary + used, sizeof(sSelfTestSummary) - (size_t)used, " %d%s", v, pass ? "ok" : "X");
        snprintf(line, sizeof(line), "graph: Play rsx-selftest %d %s: centre pixel %06x -> %s", v, kNames[v],
                 (unsigned)results[v], pass ? "PASS" : "FAIL");
        oot_ps3_early_trace(line);
    }
    oot_ps3_early_trace(sSelfTestSummary);
}

int oot_ps3_rsx_begin(uint32_t width, uint32_t height) {
    if (!gpu_prepare() || !gpu_depth(width, height)) return 0;
    ctx = (gcmContextData*)oot_ps3_shipwright_video_rsx_context();
    if (!ctx) return 0;
    Ps3SetSurface(GCM_SURFACE_X8R8G8B8, oot_ps3_shipwright_video_rsx_offset(), width * 4, depthOffset, width * 4, width,
                  height);
    sTargetFb = -1;
    sTargetFbForStall = -1;
    {
        static int sSelfTested = 0;
        if (!sSelfTested && vpTnlMain != NULL && tnlRecords[0] != NULL) {
            sSelfTested = 1;
            rsxSetBlendFunc(ctx, GCM_SRC_ALPHA, GCM_ONE_MINUS_SRC_ALPHA, GCM_SRC_ALPHA, GCM_ONE_MINUS_SRC_ALPHA);
            Ps3TnlSelfTest(width, height);
        }
    }
    rsxSetCullFaceEnable(ctx, GCM_FALSE);
    rsxSetBlendFunc(ctx, GCM_SRC_ALPHA, GCM_ONE_MINUS_SRC_ALPHA,
                    GCM_SRC_ALPHA, GCM_ONE_MINUS_SRC_ALPHA);
    rsxSetDepthTestEnable(ctx, GCM_FALSE);
    rsxSetDepthWriteEnable(ctx, GCM_FALSE);
    rsxSetColorMask(ctx, GCM_COLOR_MASK_B | GCM_COLOR_MASK_G |
                         GCM_COLOR_MASK_R | GCM_COLOR_MASK_A);
    rsxSetClearColor(ctx, 0);
    rsxSetClearDepthStencil(ctx, 0xffffff00);
    rsxClearSurface(ctx, GCM_CLEAR_R | GCM_CLEAR_G | GCM_CLEAR_B | GCM_CLEAR_A | GCM_CLEAR_Z);
    rsxLoadVertexProgram(ctx, vp, vpCode);
    sCurrentVp = 0;
    sCullEnabled = 0;
    rsxLoadFragmentProgramLocation(ctx, fp, fpOffset, GCM_LOCATION_RSX);
    sCurrentFp = 0;
    rsxTextureControl(ctx, 0, GCM_TRUE, 0, 0, GCM_TEXTURE_MAX_ANISO_1);
    vertexCursor = 0;
    {
        static int sResolutionLogged = 0;
        if (!sResolutionLogged) {
            char line[96];
            sResolutionLogged = 1;
            snprintf(line, sizeof(line), "graph: Play rsx render target %ux%u", (unsigned)width, (unsigned)height);
            oot_ps3_early_trace(line);
        }
    }
    oot_ps3_early_trace("fast3d-rsx: native title framebuffer active");
    return 1;
}

static int Ps3UploadRgba(GpuTexture* t, const uint8_t* rgba, uint32_t width, uint32_t height);
int oot_ps3_rsx_texture(uint32_t id, const uint8_t* rgba, uint32_t width, uint32_t height) {
    if (!prepared || !id || id > MAX_TEXTURES || !rgba || !width || !height ||
        width > 512 || height > 512) return 0;
    return Ps3UploadRgba(&textures[id - 1], rgba, width, height);
}

/* Debug overlay: upload its RGBA panel (nearest filtering, clamped). */
int oot_ps3_rsx_overlay_upload(const uint8_t* rgba, uint32_t width, uint32_t height) {
    if (!prepared || !rgba || !width || !height || width > 512 || height > 512) return 0;
    if (!Ps3UploadRgba(&sOverlayTexture, rgba, width, height)) return 0;
    sOverlayTexture.linear = 0;
    sOverlayTexture.wrapS = sOverlayTexture.wrapT = 2;
    return 1;
}

/* Waits for the GPU and returns the frame drawn so far (debug-menu screenshot). */
const uint32_t* oot_ps3_shipwright_video_target_pixels(uint32_t* width, uint32_t* height);
const uint32_t* oot_ps3_rsx_capture_frame(uint32_t* width, uint32_t* height) {
    if (!prepared || !ctx) return NULL;
    gpu_wait();
    return oot_ps3_shipwright_video_target_pixels(width, height);
}

static int Ps3UploadRgba(GpuTexture* t, const uint8_t* rgba, uint32_t width, uint32_t height) {
    uint32_t bytes;
    bytes = width * height * 4;
    /* Overwrite in place only when no unfinished GPU work samples this texture;
     * otherwise upload into fresh memory and free the old copy after its fence. */
    if (t->pixels && (t->capacity < bytes || (t->useFence != 0 && !fence_passed(t->useFence)))) {
        deferred_free(t->pixels, t->useFence);
        t->pixels = NULL;
    }
    if (!t->pixels) {
        t->pixels = (uint32_t*)rsxMemalign(128, bytes);
        if (!t->pixels) {
            gpu_wait(); /* out of RSX memory: release everything pending and retry */
            deferred_free_collect(1);
            t->pixels = (uint32_t*)rsxMemalign(128, bytes);
        }
        if (!t->pixels) return 0;
        t->capacity = bytes;
    }
    // Fast3D supplies RGBA bytes. Copy them unchanged: the remap below turns
    // raw RGBA in an A8R8G8B8 texture into correct channels (same scheme as the
    // working SM64 PS3 port). Reordering here as well rotated every channel,
    // putting alpha into red.
    memcpy(t->pixels, rgba, bytes);
    memset(&t->image, 0, sizeof(t->image));
    t->image.format = GCM_TEXTURE_FORMAT_A8R8G8B8 | GCM_TEXTURE_FORMAT_LIN;
    t->image.mipmap = 1;
    t->image.dimension = GCM_TEXTURE_DIMS_2D;
    t->image.location = GCM_LOCATION_RSX;
    t->image.depth = 1;
    t->image.remap = (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_B_SHIFT) |
                     (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_G_SHIFT) |
                     (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_R_SHIFT) |
                     (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_A_SHIFT) |
                     (GCM_TEXTURE_REMAP_COLOR_B << GCM_TEXTURE_REMAP_COLOR_A_SHIFT) |
                     (GCM_TEXTURE_REMAP_COLOR_G << GCM_TEXTURE_REMAP_COLOR_B_SHIFT) |
                     (GCM_TEXTURE_REMAP_COLOR_R << GCM_TEXTURE_REMAP_COLOR_G_SHIFT) |
                     (GCM_TEXTURE_REMAP_COLOR_A << GCM_TEXTURE_REMAP_COLOR_R_SHIFT);
    t->image.width = width;
    t->image.height = height;
    t->image.pitch = width * 4;
    if (rsxAddressToOffset(t->pixels, &t->image.offset)) return 0;
    rsxInvalidateTextureCache(ctx, GCM_INVALIDATE_TEXTURE);
    return 1;
}

int oot_ps3_rsx_triangles(const float* vbo, uint32_t stride, uint32_t triangles,
                          uint32_t texture0_id, uint32_t texture1_id, int alpha,
                          uint32_t inputs, int material) {
    uint32_t count, base, offset;
    GpuTexture* t0;
    GpuTexture* t1;
    if (!prepared || !vbo || !triangles || triangles > MAX_VERTICES / 3 ||
        stride < 4 || stride > 32) return 0;
    unsigned long long ps3T0 = oot_ps3_microseconds_now();
    count = triangles * 3;
    if (vertexCursor + count > MAX_VERTICES) {
        gpu_wait(); vertexCursor = 0;
    }
    base = vertexCursor;
    {
        /* Lean shader state: plain 0/1 flags instead of the packed float decoded per pixel. */
        const uint32_t flagBits = (uint32_t)(sDrawFlags + 0.5f);
        sLeanAlpha = (flagBits & 2U) ? 1.0f : 0.0f;
        sLeanEdge = (flagBits & 8U) ? 1.0f : 0.0f;
        sLeanThreshold = (flagBits & 16U) ? 1.0f : 0.0f;
    }
    for (uint32_t i = 0; i < count; ++i) {
        const float* src = vbo + i * stride;
        /* Built in cached memory, then written to RSX local memory in one sequential
         * copy: the CPU must never read back from RSX memory (fog's *= and the uv
         * copy did, which is very slow). */
        float* const outVertex = vertices[frameIndex] + (base + i) * VERTEX_FLOATS;
        float vtx[VERTEX_FLOATS];
        float* dst = vtx;
        memcpy(dst, src, 4 * sizeof(float));
        dst[4] = dst[5] = dst[6] = dst[7] = 0.0f;
        if (sDrawUv0 != 0xFFFFU && sDrawUv0 + 2U <= stride) {
            dst[4] = src[sDrawUv0]; dst[5] = src[sDrawUv0 + 1];
        }
        if (sDrawUv1 != 0xFFFFU && sDrawUv1 + 2U <= stride) {
            dst[6] = src[sDrawUv1]; dst[7] = src[sDrawUv1 + 1];
        } else {
            dst[6] = dst[4]; dst[7] = dst[5];
        }
        {
            float in[3][4];
            Ps3RgbForm r0, r1;
            Ps3AlphaForm a0, a1;
            const Ps3RgbForm* rf;
            const Ps3AlphaForm* af;
            uint32_t c, b;
            for (c = 0; c < 3; ++c) {
                const uint32_t at = sDrawInputOff + c * sDrawInputPer;
                in[c][0] = in[c][1] = in[c][2] = in[c][3] = 1.0f;
                if (c < inputs && at + sDrawInputPer <= stride) {
                    in[c][0] = src[at]; in[c][1] = src[at + 1]; in[c][2] = src[at + 2];
                    if (sDrawInputPer == 4U) in[c][3] = src[at + 3];
                }
            }
#if PS3_COMBINER_PLAN
            {
                float rgbOut[5][3], alphaOut[3];
                Ps3EvalForms(in, rgbOut, alphaOut);
                for (b = 0; b < 5; ++b) {
                    dst[8 + 4 * b] = rgbOut[b][0];
                    dst[9 + 4 * b] = rgbOut[b][1];
                    dst[10 + 4 * b] = rgbOut[b][2];
                    dst[11 + 4 * b] = 0.0f;
                }
                dst[11] = alphaOut[0]; dst[15] = alphaOut[1]; dst[19] = alphaOut[2];
                (void)r0; (void)r1; (void)a0; (void)a1; (void)rf; (void)af;
            }
#else
            Ps3RgbCycle(&r0, 0, in, NULL);
            Ps3AlphaCycle(&a0, 0, in, NULL);
            rf = &r0; af = &a0;
            if (sTwoCycle) {
                Ps3RgbCycle(&r1, 1, in, &r0);
                Ps3AlphaCycle(&a1, 1, in, &a0);
                rf = &r1; af = &a1;
            }
            for (b = 0; b < 5; ++b) {
                dst[8 + 4 * b] = rf->k[b][0];
                dst[9 + 4 * b] = rf->k[b][1];
                dst[10 + 4 * b] = rf->k[b][2];
                dst[11 + 4 * b] = 0.0f;
            }
            dst[11] = af->k[0]; dst[15] = af->k[1]; dst[19] = af->k[2];
#endif
            /* N64 fog, as LUS's shaders do it: rgb = mix(rgb, fog.rgb, fog.a), alpha untouched.
             * The combiner output is linear in its coefficients, so the mix folds into them:
             * every k scales by (1 - f) and the constant term gains fog.rgb * f. */
            if (sDrawFog != 0xFFFFU && sDrawFog + 4U <= stride) {
                const float f = src[sDrawFog + 3];
                const float keep = 1.0f - f;
                for (b = 0; b < 5; ++b) {
                    dst[8 + 4 * b] *= keep;
                    dst[9 + 4 * b] *= keep;
                    dst[10 + 4 * b] *= keep;
                }
                dst[24] += src[sDrawFog] * f;
                dst[25] += src[sDrawFog + 1] * f;
                dst[26] += src[sDrawFog + 2] * f;
            }
            if (sFlameMaskTest) {
                for (b = 0; b < 5; ++b) dst[8 + 4 * b] = dst[9 + 4 * b] = dst[10 + 4 * b] = dst[11 + 4 * b] = 0.0f;
                dst[24] = dst[25] = dst[26] = 1.0f; /* rgb = white */
                dst[11] = 1.0f;                     /* alpha = tex0.a (mask) */
            }
            if (material == 0) {
                dst[28] = sLeanAlpha; dst[29] = sLeanEdge; dst[30] = sLeanThreshold; dst[31] = 0.0f;
            } else {
                dst[28] = (float)material; dst[29] = sDrawFlags; dst[30] = 0.0f; dst[31] = 0.0f;
            }
            memcpy(dst + 32, in[0], 4 * sizeof(float));
            memcpy(dst + 36, in[1], 4 * sizeof(float));
            memcpy(dst + 40, in[2], 4 * sizeof(float));
        }
        memcpy(outVertex, vtx, sizeof(vtx));
    }
    unsigned long long ps3T1 = oot_ps3_microseconds_now();
    sPs3FillUs += ps3T1 - ps3T0;
    t0 = Ps3Texture(texture0_id);
    t1 = Ps3Texture(texture1_id);
    if (!t0->pixels || !t1->pixels) return 0;
    t0->useFence = t1->useFence = sFenceSubmitted + 1; /* next fence follows this draw */
    if (sCurrentVp != 0) {
        rsxLoadVertexProgram(ctx, vp, vpCode);
        sCurrentVp = 0;
        ++sTnlVpSwitches;
    }
    if (sCullEnabled) {
        rsxSetCullFaceEnable(ctx, GCM_FALSE); /* the CPU path culls itself */
        sCullEnabled = 0;
    }
    ++sCpuDraws;
    rsxTextureControl(ctx, 0, GCM_TRUE, 0, 0, GCM_TEXTURE_MAX_ANISO_1);
    rsxTextureControl(ctx, 1, GCM_TRUE, 0, 0, GCM_TEXTURE_MAX_ANISO_1);
    /* The texture cache is invalidated when a texture is uploaded; doing it on every
     * draw forced the RSX to drain its pipeline hundreds of times per frame. */
    {
        /* Solid surfaces use a program without clip() so the RSX keeps early depth rejection. */
        const int wantFp = material != 0 ? 0 : ((sLeanEdge > 0.5f || sLeanThreshold > 0.5f) ? 2 : 1);
        if (wantFp != sCurrentFp) {
            if (wantFp == 1) rsxLoadFragmentProgramLocation(ctx, fpGeneric, fpGenericOffset, GCM_LOCATION_RSX);
            else if (wantFp == 2) rsxLoadFragmentProgramLocation(ctx, fpGenericClip, fpGenericClipOffset, GCM_LOCATION_RSX);
            else rsxLoadFragmentProgramLocation(ctx, fp, fpOffset, GCM_LOCATION_RSX);
            sCurrentFp = wantFp;
        }
    }
    rsxLoadTexture(ctx, 0, &t0->image);
    rsxLoadTexture(ctx, 1, &t1->image);
    rsxTextureWrapMode(ctx, 0, Ps3GcmWrap(t0->wrapS), Ps3GcmWrap(t0->wrapT),
                       GCM_TEXTURE_CLAMP_TO_EDGE, 0, GCM_TEXTURE_ZFUNC_LESS, 0);
    rsxTextureWrapMode(ctx, 1, Ps3GcmWrap(t1->wrapS), Ps3GcmWrap(t1->wrapT),
                       GCM_TEXTURE_CLAMP_TO_EDGE, 0, GCM_TEXTURE_ZFUNC_LESS, 0);
    rsxTextureFilter(ctx, 0, 0, t0->linear ? GCM_TEXTURE_LINEAR : GCM_TEXTURE_NEAREST,
                     t0->linear ? GCM_TEXTURE_LINEAR : GCM_TEXTURE_NEAREST, GCM_TEXTURE_CONVOLUTION_QUINCUNX);
    rsxTextureFilter(ctx, 1, 0, t1->linear ? GCM_TEXTURE_LINEAR : GCM_TEXTURE_NEAREST,
                     t1->linear ? GCM_TEXTURE_LINEAR : GCM_TEXTURE_NEAREST, GCM_TEXTURE_CONVOLUTION_QUINCUNX);
    rsxSetBlendEnable(ctx, alpha ? GCM_TRUE : GCM_FALSE);
    rsxAddressToOffset(vertices[frameIndex] + base * VERTEX_FLOATS, &offset);
    rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_POS, 0, offset + 0 * sizeof(float),
                             VERTEX_FLOATS * sizeof(float), 4, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX0, 0, offset + 4 * sizeof(float),
                             VERTEX_FLOATS * sizeof(float), 4, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX1, 0, offset + 8 * sizeof(float),
                             VERTEX_FLOATS * sizeof(float), 4, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX2, 0, offset + 12 * sizeof(float),
                             VERTEX_FLOATS * sizeof(float), 4, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX3, 0, offset + 16 * sizeof(float),
                             VERTEX_FLOATS * sizeof(float), 4, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX4, 0, offset + 20 * sizeof(float),
                             VERTEX_FLOATS * sizeof(float), 4, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX5, 0, offset + 24 * sizeof(float),
                             VERTEX_FLOATS * sizeof(float), 4, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX6, 0, offset + 28 * sizeof(float),
                             VERTEX_FLOATS * sizeof(float), 4, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_COLOR0, 0, offset + 32 * sizeof(float),
                             VERTEX_FLOATS * sizeof(float), 4, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_COLOR1, 0, offset + 36 * sizeof(float),
                             VERTEX_FLOATS * sizeof(float), 4, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX7, 0, offset + 40 * sizeof(float),
                             VERTEX_FLOATS * sizeof(float), 4, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxSetDepthTestEnable(ctx, sDepthTest ? GCM_TRUE : GCM_FALSE);
    rsxSetDepthWriteEnable(ctx, sDepthWrite ? GCM_TRUE : GCM_FALSE);
    /* LESS for normal surfaces (the first coplanar polygon keeps winning instead of
     * flickering), LEQUAL + polygon offset for decals: same as the PC renderers. */
    rsxSetDepthFunc(ctx, sZmodeDecal ? GCM_LEQUAL : GCM_LESS);
    rsxSetPolygonOffsetFillEnable(ctx, sZmodeDecal ? GCM_TRUE : GCM_FALSE);
    if (sZmodeDecal) rsxSetPolygonOffset(ctx, -2.0f, -2.0f);
    /* Draw samples off: writing ~40 trace lines mid-frame stalled that frame by
     * 0.5-0.8 s (the "FPS drop when the logo appears"). Change 0 to 1 to debug. */
    if ((0 && ((sPs3RsxFrames == 40U || sPs3RsxFrames == 300U) && sPs3RsxDraws < 8U)) ||
        (0 && sTwoCycle && texture1_id != 0 && sCodes[0][1][0] == 10 && sCodes[0][1][1] == 12 && sCodes[0][1][3] == 8 && sPs3TwoTexLogged++ < 12U)) {
        const float* v0 = vertices[frameIndex] + base * VERTEX_FLOATS;
        char line[240];
        snprintf(line, sizeof(line),
                 "graph: Play rsx-sample f=%u d=%u tris=%u tex=%u/%u alpha=%d mat=%d codes=%d%d%d%d/%d%d%d%d 2cyc=%d "
                 "nonlin=%u K0=(%.2f,%.2f,%.2f) K4=(%.2f,%.2f,%.2f) a=(%.2f,%.2f,%.2f)",
                 (unsigned)sPs3RsxFrames, (unsigned)sPs3RsxDraws, (unsigned)triangles,
                 (unsigned)texture0_id, (unsigned)texture1_id, alpha, material,
                 sCodes[0][0][0], sCodes[0][0][1], sCodes[0][0][2], sCodes[0][0][3],
                 sCodes[0][1][0], sCodes[0][1][1], sCodes[0][1][2], sCodes[0][1][3], sTwoCycle,
                 (unsigned)sNonlinearVerts,
                 (double)v0[8], (double)v0[9], (double)v0[10],
                 (double)v0[24], (double)v0[25], (double)v0[26],
                 (double)v0[11], (double)v0[15], (double)v0[19]);
        oot_ps3_early_trace(line);
        {
            const float* p1 = v0 + VERTEX_FLOATS;
            const float* p2 = v0 + 2 * VERTEX_FLOATS;
            snprintf(line, sizeof(line),
                     "graph: Play rsx-pos d=%u v0=(%.1f,%.1f,%.1f,%.1f) v1=(%.1f,%.1f,%.1f,%.1f) v2=(%.1f,%.1f,%.1f,%.1f)",
                     (unsigned)sPs3RsxDraws,
                     (double)v0[0], (double)v0[1], (double)v0[2], (double)v0[3],
                     (double)p1[0], (double)p1[1], (double)p1[2], (double)p1[3],
                     (double)p2[0], (double)p2[1], (double)p2[2], (double)p2[3]);
            oot_ps3_early_trace(line);
        }
        {
            const uint8_t* p1 = (const uint8_t*)t1->pixels;
            snprintf(line, sizeof(line),
                     "graph: Play rsx-sample2 d=%u c1=%d%d%d%d/%d%d%d%d K1=(%.2f,%.2f,%.2f) K2=(%.2f,%.2f,%.2f) "
                     "K3=(%.2f,%.2f,%.2f) texel1=%02x%02x%02x%02x %ux%u wrap0=%u/%u wrap1=%u/%u blend=%d depth=%d/%d",
                     (unsigned)sPs3RsxDraws,
                     sCodes[1][0][0], sCodes[1][0][1], sCodes[1][0][2], sCodes[1][0][3],
                     sCodes[1][1][0], sCodes[1][1][1], sCodes[1][1][2], sCodes[1][1][3],
                     (double)v0[12], (double)v0[13], (double)v0[14],
                     (double)v0[16], (double)v0[17], (double)v0[18],
                     (double)v0[20], (double)v0[21], (double)v0[22],
                     p1 ? p1[0] : 0, p1 ? p1[1] : 0, p1 ? p1[2] : 0, p1 ? p1[3] : 0,
                     (unsigned)t1->image.width, (unsigned)t1->image.height,
                     (unsigned)t0->wrapS, (unsigned)t0->wrapT, (unsigned)t1->wrapS, (unsigned)t1->wrapT,
                     alpha, sDepthTest, sDepthWrite);
            oot_ps3_early_trace(line);
        }
        {
            const uint8_t* q0 = (const uint8_t*)t0->pixels;
            const uint8_t* q1 = (const uint8_t*)t1->pixels;
            const uint32_t w0 = t0->image.width, h0 = t0->image.height;
            const uint32_t w1 = t1->image.width, h1 = t1->image.height;
            const uint32_t c0 = ((h0 / 2U) * w0 + w0 / 2U) * 4U;
            const uint32_t c1 = ((h1 / 2U) * w1 + w1 / 2U) * 4U;
            snprintf(line, sizeof(line),
                     "graph: Play rsx-sample3 d=%u tex0 id=%u %ux%u corner=%02x%02x%02x%02x center=%02x%02x%02x%02x "
                     "tex1 id=%u %ux%u corner=%02x%02x%02x%02x center=%02x%02x%02x%02x",
                     (unsigned)sPs3RsxDraws,
                     (unsigned)texture0_id, (unsigned)w0, (unsigned)h0,
                     q0 ? q0[0] : 0, q0 ? q0[1] : 0, q0 ? q0[2] : 0, q0 ? q0[3] : 0,
                     q0 ? q0[c0] : 0, q0 ? q0[c0 + 1] : 0, q0 ? q0[c0 + 2] : 0, q0 ? q0[c0 + 3] : 0,
                     (unsigned)texture1_id, (unsigned)w1, (unsigned)h1,
                     q1 ? q1[0] : 0, q1 ? q1[1] : 0, q1 ? q1[2] : 0, q1 ? q1[3] : 0,
                     q1 ? q1[c1] : 0, q1 ? q1[c1 + 1] : 0, q1 ? q1[c1 + 2] : 0, q1 ? q1[c1 + 3] : 0);
            oot_ps3_early_trace(line);
        }
        {
            const float* va = vertices[frameIndex] + base * VERTEX_FLOATS;
            const float* vb = va + 1 * VERTEX_FLOATS;
            const float* vc = va + 2 * VERTEX_FLOATS;
            const uint8_t* q0 = (const uint8_t*)t0->pixels;
            const uint32_t w0 = t0->image.width, h0 = t0->image.height;
            const uint32_t top = ((0U) * w0 + w0 / 2U) * 4U + 3U;
            const uint32_t mid = ((h0 / 2U) * w0 + w0 / 2U) * 4U + 3U;
            const uint32_t bot = ((h0 - 1U) * w0 + w0 / 2U) * 4U + 3U;
            snprintf(line, sizeof(line),
                     "graph: Play rsx-uv d=%u v0 uv0=(%.3f,%.3f) uv1=(%.3f,%.3f) v1 uv0=(%.3f,%.3f) uv1=(%.3f,%.3f) "
                     "v2 uv0=(%.3f,%.3f) uv1=(%.3f,%.3f) maskA top/mid/bot=%02x/%02x/%02x layout=uv%u/uv%u",
                     (unsigned)sPs3RsxDraws,
                     (double)va[4], (double)va[5], (double)va[6], (double)va[7],
                     (double)vb[4], (double)vb[5], (double)vb[6], (double)vb[7],
                     (double)vc[4], (double)vc[5], (double)vc[6], (double)vc[7],
                     q0 ? q0[top] : 0, q0 ? q0[mid] : 0, q0 ? q0[bot] : 0,
                     (unsigned)sDrawUv0, (unsigned)sDrawUv1);
            oot_ps3_early_trace(line);
        }
    }
    if (sFlameMaskTest) {
        Ps3DumpTextureAscii(t0, texture0_id);
        Ps3DumpTextureAscii(t1, texture1_id);
    }
    sPs3RsxDraws++;
    rsxDrawIndexArray(ctx, GCM_TYPE_TRIANGLES, indexOffset, count,
                      GCM_INDEX_TYPE_16B, GCM_LOCATION_RSX);
    vertexCursor += count;
    sPs3SubmitUs += oot_ps3_microseconds_now() - ps3T1;
    return 1;
}

/* ---- GPU vertex pipeline ------------------------------------------------------ */

/* Combiner constants C[0..24] for fast3d_ps3_tnl.vcg. Within one draw only the shade
 * colour S varies per vertex (prim/env/fog are constants of the draw), so each
 * combiner form is a function of S. The CPU forms (Ps3EvalForms, the same code the
 * CPU fill uses) are probed at a few values of S; when they are affine in S the
 * coefficients are exact and the RSX evaluates them per vertex. Otherwise (shade
 * multiplied by shade, value-dependent branches) this returns 0 and the draw stays on
 * the CPU path. oot_ps3_rsx_set_draw_state must have been called for the draw. */
static void Ps3TnlEval(const OotPs3TnlInput* inputs, uint32_t numInputs, int alphaInputs, const float S[4],
                       float out[18]) {
    float in[3][4], rgb[5][3], a[3];
    uint32_t c;
    int b, ch;
    for (c = 0; c < 3; ++c) {
        in[c][0] = in[c][1] = in[c][2] = in[c][3] = 1.0f;
        if (c < numInputs) {
            for (ch = 0; ch < 3; ++ch) in[c][ch] = inputs[c].rgbShade ? S[ch] : inputs[c].value[ch];
            if (alphaInputs) in[c][3] = inputs[c].alphaShade ? S[3] : inputs[c].value[3];
        }
    }
    Ps3EvalForms(in, rgb, a);
    for (b = 0; b < 5; ++b) for (ch = 0; ch < 3; ++ch) out[b * 3 + ch] = rgb[b][ch];
    out[15] = a[0]; out[16] = a[1]; out[17] = a[2];
}

/* Results of oot_ps3_rsx_tnl_combiner by draw state and inputs: the same objects
 * draw with the same combiner and colours every frame, and the probing below costs
 * ~15 evaluations of the CPU forms. */
typedef struct {
    float packs[4];
    float flags;
    uint32_t numInputs;
    int alphaInputs;
    OotPs3TnlInput inputs[3];
} Ps3TnlCombKey;
typedef struct {
    Ps3TnlCombKey key;
    int used;
    int ok;
    float m[25][4];
} Ps3TnlCombEntry;
enum { TNL_COMB_CACHE = 512 };
static Ps3TnlCombEntry sCombCache[TNL_COMB_CACHE];
static int Ps3TnlCombinerUncached(const OotPs3TnlInput* inputs, uint32_t numInputs, int alphaInputs, float out[25][4]);

int oot_ps3_rsx_tnl_combiner(const OotPs3TnlInput* inputs, uint32_t numInputs, int alphaInputs, float out[25][4]) {
    Ps3TnlCombKey key;
    uint32_t h = 2166136261U, i;
    const uint8_t* bytes;
    Ps3TnlCombEntry* e;
    if (numInputs > 3) return 0;
    memset(&key, 0, sizeof(key));
    memcpy(key.packs, sDrawPack, sizeof(key.packs));
    key.flags = sDrawFlags;
    key.numInputs = numInputs;
    key.alphaInputs = alphaInputs;
    memcpy(key.inputs, inputs, numInputs * sizeof(OotPs3TnlInput));
    bytes = (const uint8_t*)&key;
    for (i = 0; i < sizeof(key); ++i) h = (h ^ bytes[i]) * 16777619U;
    e = &sCombCache[h & (TNL_COMB_CACHE - 1)];
    if (e->used && memcmp(&e->key, &key, sizeof(key)) == 0) {
        ++sCombHits;
        if (e->ok) memcpy(out, e->m, sizeof(e->m));
        return e->ok;
    }
    ++sCombMisses;
    e->key = key;
    e->used = 1;
    e->ok = Ps3TnlCombinerUncached(inputs, numInputs, alphaInputs, e->m);
    if (e->ok) memcpy(out, e->m, sizeof(e->m));
    return e->ok;
}

static int Ps3TnlCombinerUncached(const OotPs3TnlInput* inputs, uint32_t numInputs, int alphaInputs, float out[25][4]) {
    static const float base[4] = { 0.3125f, 0.53125f, 0.71875f, 0.46875f };
    static const float check[4] = { 0.828125f, 0.171875f, 0.421875f, 0.90625f };
    const float h = 0.125f;
    float f0[18], fi[4][18], fc[18], d[4][18], k[18];
    int i, j, b;
    if (numInputs > 3) return 0;
    Ps3TnlEval(inputs, numInputs, alphaInputs, base, f0);
    for (i = 0; i < 4; ++i) {
        float S[4] = { base[0], base[1], base[2], base[3] };
        S[i] += h;
        Ps3TnlEval(inputs, numInputs, alphaInputs, S, fi[i]);
        for (j = 0; j < 18; ++j) d[i][j] = (fi[i][j] - f0[j]) * (1.0f / h);
    }
    for (j = 0; j < 18; ++j) k[j] = f0[j] - d[0][j] * base[0] - d[1][j] * base[1] - d[2][j] * base[2] - d[3][j] * base[3];
    /* Verify at a generic point and at the edges: the CPU forms branch on exact zeros
     * (a texture term times a shade that is 0), so a fit at generic values alone can
     * disagree there. Any mismatch keeps the draw on the CPU path. */
    {
        static const float points[9][4] = {
            { 0.828125f, 0.171875f, 0.421875f, 0.90625f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f, 1.0f },
            { 0.0f, 1.0f, 0.0f, 1.0f }, { 1.0f, 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 1.0f },
            { 1.0f, 1.0f, 1.0f, 0.0f }, { 0.0f, 0.5f, 1.0f, 0.25f }, { 0.75f, 0.0f, 0.0f, 0.0f },
        };
        int p;
        (void)check;
        for (p = 0; p < 9; ++p) {
            const float* S = points[p];
            Ps3TnlEval(inputs, numInputs, alphaInputs, S, fc);
            for (j = 0; j < 18; ++j) {
                const float predicted = k[j] + d[0][j] * S[0] + d[1][j] * S[1] + d[2][j] * S[2] + d[3][j] * S[3];
                const float diff = predicted - fc[j];
                if (diff > 0.002f || diff < -0.002f) return 0;
            }
        }
    }
    for (b = 0; b < 5; ++b) {
        for (i = 0; i < 5; ++i) {
            const float* src = i < 4 ? d[i] : k;
            out[5 * b + i][0] = src[b * 3 + 0];
            out[5 * b + i][1] = src[b * 3 + 1];
            out[5 * b + i][2] = src[b * 3 + 2];
            out[5 * b + i][3] = b < 3 ? src[15 + b] : 0.0f;
        }
    }
    return 1;
}

/* Draws `count` T&L records (16 bytes each: s16 x,y,z, u16 palette slot, s16 s,t,
 * u8 colour/normal x4) with `numConst` vec4 constants loaded at C[0]. */
/* Diagnostic: what fast3d_ps3_tnl.vcg computes for one record, evaluated on the
 * CPU from the same constants (traced for a few draws; compare with what shows). */
static void Ps3TnlCpuCheck(const uint8_t* rec, const float* C4, uint32_t numConst, int draw) {
    const float (*C)[4] = (const float (*)[4])C4;
    int16_t ob[3], tc[2];
    uint16_t slot;
    const uint8_t* cn = rec + 12;
    float clip[4], S[4], k[5][4], n[3], lit[3];
    int b, i, c;
    char line[300];
    memcpy(ob, rec, 6);
    memcpy(&slot, rec + 6, 2);
    memcpy(tc, rec + 8, 4);
    b = (int)slot; /* the record holds the constant index 40 + 15 * slot */
    if ((uint32_t)b + 15U > numConst) {
        snprintf(line, sizeof(line), "graph: Play tnl-check draw %d: slot %u outside the uploaded constants (%u)",
                 draw, (unsigned)slot, (unsigned)numConst);
        oot_ps3_early_trace(line);
        return;
    }
    for (c = 0; c < 4; ++c) clip[c] = ob[0] * C[b][c] + ob[1] * C[b + 1][c] + ob[2] * C[b + 2][c] + C[b + 3][c];
    for (c = 0; c < 3; ++c) n[c] = cn[c] >= 128 ? (float)cn[c] - 256.0f : (float)cn[c];
    for (c = 0; c < 3; ++c) lit[c] = C[30][c];
    for (i = 0; i < 7; ++i) {
        const float d = (n[0] * C[b + 6 + i][0] + n[1] * C[b + 6 + i][1] + n[2] * C[b + 6 + i][2]) / 127.0f;
        for (c = 0; c < 3; ++c) lit[c] += (d > 0.0f ? d : 0.0f) * C[31 + i][c];
    }
    for (c = 0; c < 3; ++c) {
        if (lit[c] > 1.0f) lit[c] = 1.0f;
        S[c] = C[b + 4][0] > 0.5f ? lit[c] : cn[c] / 255.0f;
    }
    S[3] = cn[3] / 255.0f;
    for (i = 0; i < 5; ++i) {
        for (c = 0; c < 4; ++c) {
            k[i][c] = S[0] * C[5 * i][c] + S[1] * C[5 * i + 1][c] + S[2] * C[5 * i + 2][c] + S[3] * C[5 * i + 3][c] +
                      C[5 * i + 4][c];
        }
    }
    snprintf(line, sizeof(line),
             "graph: Play tnl-check draw %d: ob=(%d,%d,%d) slot=%u clip=(%.1f,%.1f,%.1f,%.1f) S=(%.2f,%.2f,%.2f,%.2f) "
             "k0=(%.2f,%.2f,%.2f,%.2f) k4=(%.2f,%.2f,%.2f) uv0=(%.3f,%.3f) lit=%.0f",
             draw, ob[0], ob[1], ob[2], (unsigned)slot, (double)clip[0], (double)clip[1], (double)clip[2],
             (double)clip[3], (double)S[0], (double)S[1], (double)S[2], (double)S[3], (double)k[0][0],
             (double)k[0][1], (double)k[0][2], (double)k[0][3], (double)k[4][0], (double)k[4][1], (double)k[4][2],
             (double)(floorf(tc[0] * C[b + 5][0]) * C[25][0] + C[25][2]),
             (double)(floorf(tc[1] * C[b + 5][1]) * C[25][1] + C[25][3]), (double)C[b + 4][0]);
    oot_ps3_early_trace(line);
}

int oot_ps3_rsx_tnl_draw(const void* records, uint32_t count, float* consts, int drawDirty, uint32_t firstConst,
                         uint32_t numConst, uint32_t texture0_id, uint32_t texture1_id, int alpha, int cullMode,
                         int cullFlip, int floatAttribs) {
    GpuTexture* t0;
    GpuTexture* t1;
    uint8_t* dst;
    uint32_t offset;
    unsigned long long t0us;
    /* Float mode (USRDIR/tnl_float, diagnostic): 48-byte float vertices instead of the
     * packed 16-byte records, to rule out the s16/u8 attribute formats. */
    const uint32_t units = floatAttribs ? 3U : 1U;
    static int sChecks = 0;
    static unsigned int sCheckDraw = 0;
    if (!prepared || !vpTnl || !records || !count || count * units > TNL_MAX_RECORDS || numConst > 460U) return 0;
    t0us = oot_ps3_microseconds_now();
    if (tnlCursor + count * units > TNL_MAX_RECORDS) {
        gpu_wait();
        tnlCursor = 0;
    }
    dst = tnlRecords[frameIndex] + (size_t)tnlCursor * 16U;
    if (floatAttribs) {
        float tmp[12];
        uint32_t i;
        for (i = 0; i < count; ++i) {
            const uint8_t* r = (const uint8_t*)records + (size_t)i * 16U;
            int16_t ob[3], tc[2];
            uint16_t slot;
            memcpy(ob, r, 6);
            memcpy(&slot, r + 6, 2);
            memcpy(tc, r + 8, 4);
            tmp[0] = ob[0]; tmp[1] = ob[1]; tmp[2] = ob[2]; tmp[3] = slot;
            tmp[4] = tc[0]; tmp[5] = tc[1]; tmp[6] = 0.0f; tmp[7] = 0.0f;
            tmp[8] = r[12]; tmp[9] = r[13]; tmp[10] = r[14]; tmp[11] = r[15];
            memcpy(dst + (size_t)i * 48U, tmp, sizeof(tmp));
        }
    } else {
        memcpy(dst, records, (size_t)count * 16U);
    }
    if (sChecks < 12 && (++sCheckDraw % 997U) == 0U) {
        Ps3TnlCpuCheck((const uint8_t*)records, consts, numConst, (int)sCheckDraw);
        ++sChecks;
    }
    t0 = Ps3Texture(texture0_id);
    t1 = Ps3Texture(texture1_id);
    if (!t0->pixels || !t1->pixels) return 0;
    t0->useFence = t1->useFence = sFenceSubmitted + 1;
    {
        const uint32_t flagBits = (uint32_t)(sDrawFlags + 0.5f);
        sLeanAlpha = (flagBits & 2U) ? 1.0f : 0.0f;
        sLeanEdge = (flagBits & 8U) ? 1.0f : 0.0f;
        sLeanThreshold = (flagBits & 16U) ? 1.0f : 0.0f;
        consts[29 * 4 + 0] = sLeanAlpha;
        consts[29 * 4 + 1] = sLeanEdge;
        consts[29 * 4 + 2] = sLeanThreshold;
        consts[29 * 4 + 3] = 0.0f;
    }
    {
        /* 1 = T&L, 2 = white probe, 3 = attribute probe (raw model coordinates) */
        const int wantVp = sTnlDebugMode == 2 ? 2 : (sTnlDebugMode == 4 || sTnlDebugMode == 5) ? 3 : 1;
        if (sCurrentVp != wantVp) {
            vpTnl = wantVp == 2 ? vpTnlProbe : wantVp == 3 ? vpTnlAttrProbe : vpTnlMain;
            vpTnlCode = wantVp == 2 ? vpTnlProbeCode : wantVp == 3 ? vpTnlAttrProbeCode : vpTnlMainCode;
            rsxLoadVertexProgram(ctx, vpTnl, vpTnlCode);
            sCurrentVp = wantVp;
            ++sTnlVpSwitches;
            drawDirty = 1; /* a program load can overwrite constants: send this draw's again */
        }
    }
    /* Only the constants changed since the last T&L draw (the caller tracks them);
     * C[29] (fragment state flags) every time. */
    if (drawDirty) {
        rsxLoadVertexProgramParameterBlock(ctx, vpTnl->const_start, 40U, consts);
        sTnlConstUploads += 40U;
    } else {
        rsxLoadVertexProgramParameterBlock(ctx, vpTnl->const_start + 29U, 1, consts + 29U * 4U);
    }
    if (numConst > firstConst) {
        rsxLoadVertexProgramParameterBlock(ctx, vpTnl->const_start + firstConst, numConst - firstConst,
                                           consts + firstConst * 4U);
        sTnlConstUploads += numConst - firstConst;
    }
    rsxTextureControl(ctx, 0, GCM_TRUE, 0, 0, GCM_TEXTURE_MAX_ANISO_1);
    rsxTextureControl(ctx, 1, GCM_TRUE, 0, 0, GCM_TEXTURE_MAX_ANISO_1);
    {
        const int wantFp = (sLeanEdge > 0.5f || sLeanThreshold > 0.5f) ? 2 : 1;
        if (wantFp != sCurrentFp) {
            if (wantFp == 1) rsxLoadFragmentProgramLocation(ctx, fpGeneric, fpGenericOffset, GCM_LOCATION_RSX);
            else rsxLoadFragmentProgramLocation(ctx, fpGenericClip, fpGenericClipOffset, GCM_LOCATION_RSX);
            sCurrentFp = wantFp;
        }
    }
    rsxLoadTexture(ctx, 0, &t0->image);
    rsxLoadTexture(ctx, 1, &t1->image);
    rsxTextureWrapMode(ctx, 0, Ps3GcmWrap(t0->wrapS), Ps3GcmWrap(t0->wrapT),
                       GCM_TEXTURE_CLAMP_TO_EDGE, 0, GCM_TEXTURE_ZFUNC_LESS, 0);
    rsxTextureWrapMode(ctx, 1, Ps3GcmWrap(t1->wrapS), Ps3GcmWrap(t1->wrapT),
                       GCM_TEXTURE_CLAMP_TO_EDGE, 0, GCM_TEXTURE_ZFUNC_LESS, 0);
    rsxTextureFilter(ctx, 0, 0, t0->linear ? GCM_TEXTURE_LINEAR : GCM_TEXTURE_NEAREST,
                     t0->linear ? GCM_TEXTURE_LINEAR : GCM_TEXTURE_NEAREST, GCM_TEXTURE_CONVOLUTION_QUINCUNX);
    rsxTextureFilter(ctx, 1, 0, t1->linear ? GCM_TEXTURE_LINEAR : GCM_TEXTURE_NEAREST,
                     t1->linear ? GCM_TEXTURE_LINEAR : GCM_TEXTURE_NEAREST, GCM_TEXTURE_CONVOLUTION_QUINCUNX);
    rsxSetBlendEnable(ctx, alpha ? GCM_TRUE : GCM_FALSE);
    rsxAddressToOffset(dst, &offset);
    /* 5 = signed 16-bit (unnormalised), 7 = unsigned byte 0..255: RSX vertex formats
     * not named in PSL1GHT's headers (libgcm S32K / UB256). Unused arrays get size 0. */
    if (floatAttribs) {
        rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_POS, 0, offset + 0, 48, 4, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
        rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX0, 0, offset + 16, 48, 2, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
        rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_COLOR0, 0, offset + 32, 48, 4, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    } else {
        rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_POS, 0, offset + 0, 16, 4, 5, GCM_LOCATION_RSX);
        rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX0, 0, offset + 8, 16, 2, 5, GCM_LOCATION_RSX);
        rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_COLOR0, 0, offset + 12, 16, 4, 7, GCM_LOCATION_RSX);
    }
    rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_COLOR1, 0, 0, 0, 0, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX1, 0, 0, 0, 0, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX2, 0, 0, 0, 0, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX3, 0, 0, 0, 0, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX4, 0, 0, 0, 0, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX5, 0, 0, 0, 0, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX6, 0, 0, 0, 0, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(ctx, GCM_VERTEX_ATTRIB_TEX7, 0, 0, 0, 0, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    {
        /* The probes (debug modes 2 and 4) draw without depth test or culling. */
        const int probe = sTnlDebugMode == 2 || sTnlDebugMode == 4 || sTnlDebugMode == 5;
        if (probe) cullMode = 0;
        rsxSetDepthTestEnable(ctx, (sDepthTest && !probe) ? GCM_TRUE : GCM_FALSE);
        rsxSetDepthWriteEnable(ctx, (sDepthWrite && !probe) ? GCM_TRUE : GCM_FALSE);
    }
    rsxSetDepthFunc(ctx, sZmodeDecal ? GCM_LEQUAL : GCM_LESS);
    rsxSetPolygonOffsetFillEnable(ctx, sZmodeDecal ? GCM_TRUE : GCM_FALSE);
    if (sZmodeDecal) rsxSetPolygonOffset(ctx, -2.0f, -2.0f);
    /* N64 culling (cullMode 1 back, 2 front, 3 both). N64 back faces are clockwise
     * in normalised device coordinates; cullFlip swaps the RSX's front face in case
     * the hardware sees the winding the other way round (USRDIR/tnl_cullflip). */
    if (cullMode != 0) {
        rsxSetFrontFace(ctx, cullFlip ? GCM_FRONTFACE_CW : GCM_FRONTFACE_CCW);
        rsxSetCullFace(ctx, cullMode == 1 ? GCM_CULL_BACK : (cullMode == 2 ? GCM_CULL_FRONT : GCM_CULL_ALL));
        if (!sCullEnabled) {
            rsxSetCullFaceEnable(ctx, GCM_TRUE);
            sCullEnabled = 1;
        }
    } else if (sCullEnabled) {
        rsxSetCullFaceEnable(ctx, GCM_FALSE);
        sCullEnabled = 0;
    }
    if (sTnlIndexDraw) {
        rsxDrawIndexArray(ctx, GCM_TYPE_TRIANGLES, indexOffset, count, GCM_INDEX_TYPE_16B, GCM_LOCATION_RSX);
    } else {
        rsxDrawVertexArray(ctx, GCM_TYPE_TRIANGLES, 0, count);
    }
    tnlCursor += count * units;
    ++sTnlDraws;
    sTnlVerts += count;
    sPs3SubmitUs += oot_ps3_microseconds_now() - t0us;
    return 1;
}

/* Per-frame T&L counters for the trace (draws on each path, vertex program switches). */
void oot_ps3_rsx_take_tnl_stats(unsigned int* tnlDraws, unsigned int* tnlVerts, unsigned int* cpuDraws,
                                unsigned int* vpSwitches, unsigned int* constVec4, unsigned int* combHits,
                                unsigned int* combMisses) {
    *tnlDraws = sTnlDraws; *tnlVerts = sTnlVerts; *cpuDraws = sCpuDraws; *vpSwitches = sTnlVpSwitches;
    *constVec4 = sTnlConstUploads; *combHits = sCombHits; *combMisses = sCombMisses;
    sTnlDraws = sTnlVerts = sCpuDraws = sTnlVpSwitches = sTnlConstUploads = sCombHits = sCombMisses = 0;
}

/* Copies the frame drawn so far (the current render target) into framebuffer
 * texture `fb`, allocated on first use at the render size. The RSX blit runs in
 * command order; the idle waits keep it from racing the 3D pipeline. */
int oot_ps3_rsx_copy_fb(int fb) {
    GpuTexture* t;
    uint32_t bytes, srcOffset, x, y;
    if (!prepared || !ctx || fb < 0 || fb >= MAX_FB_TEXTURES || !frameWidth || !frameHeight) return 0;
    t = &sFbTextures[fb];
    bytes = frameWidth * frameHeight * 4U;
    if (t->pixels && (t->image.width != frameWidth || t->image.height != frameHeight)) {
        deferred_free(t->pixels, t->useFence);
        t->pixels = NULL;
    }
    if (!t->pixels) {
        t->pixels = (uint32_t*)rsxMemalign(128, bytes);
        if (!t->pixels) {
            oot_ps3_early_trace("graph: Play rsx framebuffer copy: out of RSX memory");
            return 0;
        }
        t->capacity = bytes;
        memset(&t->image, 0, sizeof(t->image));
        /* The render target is X8R8G8B8 in memory: ARGB sampling with alpha read as 1. */
        t->image.format = GCM_TEXTURE_FORMAT_A8R8G8B8 | GCM_TEXTURE_FORMAT_LIN;
        t->image.mipmap = 1;
        t->image.dimension = GCM_TEXTURE_DIMS_2D;
        t->image.location = GCM_LOCATION_RSX;
        t->image.depth = 1;
        t->image.remap = (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_B_SHIFT) |
                         (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_G_SHIFT) |
                         (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_R_SHIFT) |
                         (GCM_TEXTURE_REMAP_TYPE_ONE << GCM_TEXTURE_REMAP_TYPE_A_SHIFT) |
                         (GCM_TEXTURE_REMAP_COLOR_B << GCM_TEXTURE_REMAP_COLOR_B_SHIFT) |
                         (GCM_TEXTURE_REMAP_COLOR_G << GCM_TEXTURE_REMAP_COLOR_G_SHIFT) |
                         (GCM_TEXTURE_REMAP_COLOR_R << GCM_TEXTURE_REMAP_COLOR_R_SHIFT) |
                         (GCM_TEXTURE_REMAP_COLOR_A << GCM_TEXTURE_REMAP_COLOR_A_SHIFT);
        t->image.width = (u16)frameWidth;
        t->image.height = (u16)frameHeight;
        t->image.pitch = frameWidth * 4U;
        t->wrapS = t->wrapT = 2;
        t->linear = 1;
        if (rsxAddressToOffset(t->pixels, &t->image.offset)) { rsxFree(t->pixels); t->pixels = NULL; return 0; }
        {
            char line[96];
            snprintf(line, sizeof(line), "graph: Play rsx framebuffer texture %d allocated %ux%u", fb,
                     (unsigned)frameWidth, (unsigned)frameHeight);
            oot_ps3_early_trace(line);
        }
    } else if (t->useFence != 0 && !fence_passed(t->useFence)) {
        fence_wait(t->useFence); /* a draw still samples the previous copy */
    }
    srcOffset = oot_ps3_shipwright_video_rsx_offset();
    rsxSetWaitForIdle(ctx);
    /* Blit in blocks of at most 1024x1024 pixels. */
    for (y = 0; y < frameHeight; y += 1024U) {
        const uint32_t h = frameHeight - y < 1024U ? frameHeight - y : 1024U;
        for (x = 0; x < frameWidth; x += 1024U) {
            const uint32_t w = frameWidth - x < 1024U ? frameWidth - x : 1024U;
            rsxSetTransferImage(ctx, GCM_TRANSFER_LOCAL_TO_LOCAL, t->image.offset, frameWidth * 4U, x, y,
                                srcOffset, frameWidth * 4U, x, y, w, h, 4);
        }
    }
    rsxSetWaitForIdle(ctx);
    rsxInvalidateTextureCache(ctx, GCM_INVALIDATE_TEXTURE);
    return 1;
}

/* Render target switch (Fast3D StartDrawToFramebuffer): fb < 0 = the game frame,
 * otherwise framebuffer texture `fb` (SoH draws the pause-menu Link into one, then
 * samples it on the equipment page). Binding a second RSX surface froze the GPU on
 * the console (EBOOTs 11:29-11:49), so a framebuffer is drawn on the frame itself -
 * these framebuffers are screen-sized and drawn first in the frame (the game fills
 * them black before drawing) - and copied into its texture with the proven blit when
 * the game switches back; the depth buffer is then cleared. What the frame shows
 * there is drawn over afterwards (the pause background covers the screen). */
void oot_ps3_rsx_clear(int color, int depth);
int oot_ps3_rsx_set_target(int fb, uint32_t width, uint32_t height) {
    if (!prepared || !ctx || !frameWidth || fb == sTargetFb) return 1;
    if (sTargetFb >= 0) {
        const int done = sTargetFb;
        sTargetFb = -1;
        sTargetFbForStall = -1;
        if (!oot_ps3_rsx_copy_fb(done)) return 0;
        oot_ps3_rsx_clear(0, 1);
    }
    if (fb < 0) return 1;
    if (fb >= MAX_FB_TEXTURES) return 0;
    {
        static int sSwitchLogged = 0;
        if (sSwitchLogged < 6) {
            char line[112];
            ++sSwitchLogged;
            snprintf(line, sizeof(line), "graph: Play rsx target -> framebuffer %d %ux%u (drawn on the %ux%u frame)", fb,
                     (unsigned)width, (unsigned)height, (unsigned)frameWidth, (unsigned)frameHeight);
            oot_ps3_early_trace(line);
        }
    }
    sTargetFb = fb;
    sTargetFbForStall = fb;
    return 1;
}

void oot_ps3_rsx_clear(int color, int depth) {
    if (!prepared || !ctx) return;
    rsxSetClearColor(ctx, 0);
    rsxSetClearDepthStencil(ctx, 0xffffff00);
    rsxClearSurface(ctx, (color ? GCM_CLEAR_R | GCM_CLEAR_G | GCM_CLEAR_B | GCM_CLEAR_A : 0) |
                         (depth ? GCM_CLEAR_Z : 0));
}

void oot_ps3_rsx_end(void) {
    if (!prepared) return;
    if (sTargetFb >= 0) oot_ps3_rsx_set_target(-1, 0, 0); /* never present with a texture bound as target */
    sFrameFence[frameIndex] = fence_submit();
    frameIndex ^= 1U;
    /* The other vertex buffer was last used two frames ago; wait only for that frame. */
    fence_wait(sFrameFence[frameIndex]);
    vertexCursor = 0;
    tnlCursor = 0;
    deferred_free_collect(0);
    sPs3RsxFrames++;
    sPs3RsxDraws = 0;
}

void oot_ps3_rsx_viewport(int x, int y, int width, int height) {
    float scale[4], offset[4];
    if (!prepared || !ctx || width <= 0 || height <= 0) return;
    y = (int)sTargetHeight - y - height;
    scale[0] = width * .5f; scale[1] = height * -.5f;
    scale[2] = .5f; scale[3] = 0;
    offset[0] = x + width * .5f; offset[1] = y + height * .5f;
    offset[2] = .5f; offset[3] = 0;
    rsxSetViewport(ctx, x, y, width, height, 0.0f, 1.0f, scale, offset);
}

void oot_ps3_rsx_scissor(int x, int y, int width, int height) {
    if (!prepared || !ctx || width <= 0 || height <= 0) return;
    rsxSetScissor(ctx, x, (int)sTargetHeight - y - height, width, height);
}
