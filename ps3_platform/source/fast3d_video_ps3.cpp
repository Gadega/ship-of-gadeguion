// SPDX-License-Identifier: GPL-3.0-or-later
// Native Fast3D framebuffer backend. Geometry comes exclusively from the
// Shipwright display lists and the user's locally extracted O2R archive.
// The Mario 64 PS3 port informed the RSX buffer/flip structure; no source
// from its source-only graphics interpreter is copied into this runtime.
#include "fast/interpreter.h"
#include "fast/backends/gfx_window_manager_api.h"
#include "ps3_platform/shipwright_video_ps3.h"
#include "ps3_platform/fast3d_rsx_ps3.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <stdio.h>
#include <utility>
#include <vector>
extern "C" unsigned long long oot_ps3_microseconds_now(void);
extern "C" void oot_ps3_fast3d_add_run(unsigned long long us);

extern "C" void oot_ps3_early_trace(const char*);
extern "C" void oot_ps3_rsx_texture_sampler(uint32_t id, int linear, int wrapS, int wrapT);
extern "C" int oot_ps3_rsx_copy_fb(int fb);
extern "C" void oot_ps3_debug_overlay_frame(unsigned int screenWidth, unsigned int screenHeight);
extern "C" int oot_ps3_rsx_set_target(int fb, uint32_t width, uint32_t height);
extern "C" void oot_ps3_rsx_set_depth(int test, int write);
extern "C" void oot_ps3_rsx_set_decal(int decal);
extern "C" void oot_ps3_rsx_set_draw_state(uint32_t uv0Off, uint32_t uv1Off, uint32_t fogOff,
                                          uint32_t inputOff, uint32_t inputPer, float flags,
                                          const float packs[4]);

namespace Fast {
void GfxSetInstance(std::shared_ptr<Interpreter> gfx);

struct ShaderProgram {
    uint64_t id0;
    uint64_t id1;
    CCFeatures features{};
};

class Ps3Window final : public GfxWindowBackend {
  public:
    void Init(const char*, const char*, bool, uint32_t width, uint32_t height, int32_t, int32_t) override {
        width_ = width;
        height_ = height;
    }
    void Close() override { mIsRunning = false; }
    void SetKeyboardCallbacks(bool (*)(int), bool (*)(int), void (*)()) override {}
    void SetMouseCallbacks(bool (*)(int), bool (*)(int)) override {}
    void SetFullscreenChangedCallback(void (*)(bool)) override {}
    void SetFullscreen(bool) override {}
    void GetActiveWindowRefreshRate(uint32_t* value) override { if (value) *value = 60; }
    void SetCursorVisibility(bool) override {}
    void SetMousePos(int32_t, int32_t) override {}
    void GetMousePos(int32_t* x, int32_t* y) override { if (x) *x = 0; if (y) *y = 0; }
    void GetMouseDelta(int32_t* x, int32_t* y) override { GetMousePos(x, y); }
    void GetMouseWheel(float* x, float* y) override { if (x) *x = 0; if (y) *y = 0; }
    bool GetMouseState(uint32_t) override { return false; }
    void SetMouseCapture(bool) override {}
    bool IsMouseCaptured() override { return false; }
    void GetDimensions(uint32_t* width, uint32_t* height, int32_t* x, int32_t* y) override {
        if (width) *width = width_;
        if (height) *height = height_;
        if (x) *x = 0;
        if (y) *y = 0;
    }
    void SetDimensions(uint32_t width, uint32_t height, int32_t, int32_t) override {
        width_ = width; height_ = height;
    }
    Ship::WindowRect GetPrimaryMonitorRect() override {
        return { 0, 0, static_cast<int32_t>(width_), static_cast<int32_t>(height_) };
    }
    void HandleEvents() override {}
    bool IsFrameReady() override { return mIsRunning; }
    void SwapBuffersBegin() override {}
    void SwapBuffersEnd() override {}
    double GetTime() override { return 0.0; }
    int GetTargetFps() override { return static_cast<int>(mTargetFps); }
    void SetTargetFps(int fps) override { if (fps > 0) mTargetFps = static_cast<uint32_t>(fps); }
    void SetMaxFrameLatency(int) override {}
    const char* GetKeyName(int) override { return ""; }
    bool CanDisableVsync() override { return false; }
    bool IsRunning() override { return mIsRunning; }
    void Destroy() override {}
    bool IsFullscreen() override { return true; }
  private:
    uint32_t width_ = 320;
    uint32_t height_ = 240;
};

class Ps3Renderer final : public GfxRenderingAPI {
    struct TextureState {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> pixels;
    };
    struct Point {
        float x, y, z, u, v;
        uint32_t color;
        uint8_t opacity;
    };
  public:
    const char* GetName() override { return "PS3 native Fast3D"; }
    int GetMaxTextureSize() override { return 512; }
    // RSX's negative Y viewport scale already maps NDC up to screen up.
    // Asking Fast3D to invert Y as well turns the title lettering upside down.
    GfxClipParameters GetClipParameters() override { return { true, !gpuActive_ }; }
    void UnloadShader(ShaderProgram*) override {}
    void LoadShader(ShaderProgram* program) override { active_ = program; }
    void ClearShaderCache() override { shaders_.clear(); active_ = nullptr; }
    ShaderProgram* CreateAndLoadNewShader(uint64_t id0, uint64_t id1) override {
        const auto key = std::make_pair(id0, id1);
        ShaderProgram& program = shaders_[key];
        program.id0 = id0;
        program.id1 = id1;
        gfx_cc_get_features(id0, id1, &program.features);
        // Log each distinct material once. The generic PS3 fragment shader
        // currently ignores these upstream color-combiner operations.
        if (gpuActive_ && shaders_.size() <= 16) {
            char diagnostic[180];
            const CCFeatures& f = program.features;
            snprintf(diagnostic, sizeof(diagnostic),
                     "fast3d-material: id=%08lx/%08lx tex=%d%d alpha=%d cycles=%d inputs=%d",
                     static_cast<unsigned long>(id0), static_cast<unsigned long>(id1),
                     f.usedTextures[0], f.usedTextures[1], f.opt_alpha,
                     f.opt_2cyc, f.numInputs);
            oot_ps3_early_trace(diagnostic);
            snprintf(diagnostic, sizeof(diagnostic),
                     "fast3d-combine: rgb0=%d,%d,%d,%d alpha0=%d,%d,%d,%d rgb1=%d,%d,%d,%d",
                     f.c[0][0][0], f.c[0][0][1], f.c[0][0][2], f.c[0][0][3],
                     f.c[0][1][0], f.c[0][1][1], f.c[0][1][2], f.c[0][1][3],
                     f.c[1][0][0], f.c[1][0][1], f.c[1][0][2], f.c[1][0][3]);
            oot_ps3_early_trace(diagnostic);
        }
        active_ = &program;
        return active_;
    }
    ShaderProgram* LookupShader(uint64_t id0, uint64_t id1) override {
        const auto it = shaders_.find(std::make_pair(id0, id1));
        return it == shaders_.end() ? nullptr : &it->second;
    }
    void ShaderGetInfo(ShaderProgram* program, uint8_t* inputs, bool used[2]) override {
        if (inputs) *inputs = program ? static_cast<uint8_t>(program->features.numInputs) : 0;
        if (used) {
            used[0] = program && program->features.usedTextures[0];
            used[1] = program && program->features.usedTextures[1];
        }
    }
    uint32_t NewTexture() override {
        if (textures_.size() >= 1023) return 0;
        textures_.emplace_back();
        return static_cast<uint32_t>(textures_.size());
    }
    void SelectTexture(int tile, uint32_t id) override {
        if (tile >= 0 && tile < 2) { selected_[tile] = id; uploadTile_ = tile; }
    }
    void UploadTexture(const uint8_t* rgba, uint32_t width, uint32_t height) override {
        const uint32_t id = selected_[uploadTile_];
        if (!rgba || !id || id > textures_.size() || width > 512 || height > 512) return;
        TextureState& texture = textures_[id - 1];
        const size_t bytes = static_cast<size_t>(width) * height * 4;
        if (texture.width == width && texture.height == height &&
            texture.pixels.size() == bytes &&
            std::memcmp(texture.pixels.data(), rgba, bytes) == 0) return;
        texture.width = width; texture.height = height;
        texture.pixels.assign(rgba, rgba + bytes);
        if (gpuActive_ && diagnosticUploads_ < 5) {
            char diagnostic[150];
            snprintf(diagnostic, sizeof(diagnostic),
                     "fast3d-upload: id=%u tile=%d size=%ux%u rgba=%u,%u,%u,%u",
                     id, uploadTile_, width, height,
                     static_cast<unsigned>(rgba[0]), static_cast<unsigned>(rgba[1]),
                     static_cast<unsigned>(rgba[2]), static_cast<unsigned>(rgba[3]));
            oot_ps3_early_trace(diagnostic);
            ++diagnosticUploads_;
        }
        if (gpuActive_ && !oot_ps3_rsx_texture(id, rgba, width, height))
            oot_ps3_early_trace("fast3d-rsx: texture upload failed");
        ++uploads_;
    }
    void SetSamplerParameters(int tile, bool linear, uint32_t cms, uint32_t cmt) override {
        if (tile < 0 || tile > 1 || !gpuActive_) return;
        // N64 tile modes: bit 1 = G_TX_CLAMP, bit 0 = G_TX_MIRROR (same mapping as Fast3D's PC backends).
        const auto mode = [](uint32_t v) -> int { return (v & 2U) ? 2 : ((v & 1U) ? 1 : 0); };
        oot_ps3_rsx_texture_sampler(selected_[tile], linear ? 1 : 0, mode(cms), mode(cmt));
    }
    void SetDepthTestAndMask(bool test, bool write) override {
        depthTest_ = test;
        depthWrite_ = write;
        oot_ps3_rsx_set_depth(test ? 1 : 0, write ? 1 : 0);
    }
    void SetZmodeDecal(bool decal) override { oot_ps3_rsx_set_decal(decal ? 1 : 0); }
    void SetViewport(int x, int y, int width, int height) override {
        if (gpuActive_) oot_ps3_rsx_viewport(x, y, width, height);
        vx_ = x / renderScale_; vy_ = y / renderScale_;
        vw_ = width / renderScale_; vh_ = height / renderScale_;
    }
    void SetScissor(int x, int y, int width, int height) override {
        if (gpuActive_) oot_ps3_rsx_scissor(x, y, width, height);
        sx_ = x / renderScale_; sy_ = y / renderScale_;
        sw_ = width / renderScale_; sh_ = height / renderScale_;
    }
    void SetUseAlpha(bool value) override { alpha_ = value; }
    bool GpuActive() const { return gpuActive_; }
    // GPU vertex pipeline: the combiner state of the current shader (as DrawTriangles
    // sets it for the CPU path) and its constants / draw.
    bool TnlSetCombinerState() {
        if (!gpuActive_ || !active_) return false;
        const CCFeatures& lf = active_->features;
        float packs[4];
        const int order[4][2] = { { 0, 0 }, { 0, 1 }, { 1, 0 }, { 1, 1 } };
        for (int p = 0; p < 4; ++p) {
            float v = 0.0f;
            for (int k = 3; k >= 0; --k) {
                int code = lf.c[order[p][0]][order[p][1]][k];
                if (code < 0 || code > 15) code = 0;
                v = v * 16.0f + static_cast<float>(code);
            }
            packs[p] = v;
        }
        const float flags = (lf.opt_2cyc ? 1.0f : 0.0f) + (lf.opt_alpha ? 2.0f : 0.0f) +
                            (lf.opt_texture_edge ? 8.0f : 0.0f) + (lf.opt_alpha_threshold ? 16.0f : 0.0f);
        oot_ps3_rsx_set_draw_state(0xFFFFU, 0xFFFFU, 0xFFFFU, 4U, alpha_ ? 4U : 3U, flags, packs);
        return true;
    }
    int TnlPrepare(const void* inputs, uint32_t numInputs, int alphaInputs, float consts[25][4]) {
        if (!TnlSetCombinerState()) return 0;
        if (static_cast<uint32_t>(active_->features.numInputs) != numInputs) return 0;
        return oot_ps3_rsx_tnl_combiner(static_cast<const OotPs3TnlInput*>(inputs), numInputs, alphaInputs, consts);
    }
    int TnlSubmit(const void* records, uint32_t count, float* consts, int drawDirty, uint32_t firstConst,
                  uint32_t numConst, int cullMode, int cullFlip, int floatAttribs) {
        if (!TnlSetCombinerState()) return 0;
        const bool tex0 = active_->features.usedTextures[0];
        const bool tex1 = active_->features.usedTextures[1];
        submitted_ += count / 3U;
        if (!oot_ps3_rsx_tnl_draw(records, count, consts, drawDirty, firstConst, numConst, tex0 ? selected_[0] : 0,
                                  tex1 ? selected_[1] : 0, alpha_ ? 1 : 0, cullMode, cullFlip, floatAttribs)) {
            oot_ps3_early_trace("graph: Play tnl draw failed");
            return 0;
        }
        valid_ += count / 3U;
        return 1;
    }
    void DrawTriangles(float vbo[], size_t length, size_t triangles) override {
        submitted_ += static_cast<uint32_t>(triangles);
        if (gpuActive_) {
            const size_t stride = triangles ? length / (triangles * 3) : 0;
            if (triangles && stride >= 4 && stride <= 32 &&
                length == triangles * 3 * stride) {
                const bool tex0 = active_ && active_->features.usedTextures[0];
                const bool tex1 = active_ && active_->features.usedTextures[1];
                if (active_ && diagnosticSamples_ < 12) {
                    char diagnostic[190];
                    const float* color = vbo + stride - 4;
                    snprintf(diagnostic, sizeof(diagnostic),
                             "fast3d-vbo: stride=%u tris=%u ids=%u,%u rgba=%d,%d,%d,%d uv=%d,%d",
                             static_cast<unsigned>(stride), static_cast<unsigned>(triangles),
                             tex0 ? selected_[0] : 0, tex1 ? selected_[1] : 0,
                             static_cast<int>(color[0] * 255), static_cast<int>(color[1] * 255),
                             static_cast<int>(color[2] * 255), static_cast<int>(color[3] * 255),
                             static_cast<int>(vbo[4] * 1000), static_cast<int>(vbo[5] * 1000));
                    oot_ps3_early_trace(diagnostic);
                    ++diagnosticSamples_;
                }
                if (tex1 && !titleDualTextureTraced_) {
                    oot_ps3_early_trace(selected_[1] ?
                        "fast3d-rsx: title TEXEL1 texture path submitted" :
                        "fast3d-rsx: title TEXEL1 texture missing; white fallback used");
                    titleDualTextureTraced_ = true;
                }
                // These three material signatures come from Shipwright's
                // actual color-combiner commands, including both title cycles.
                const CCFeatures& features = active_->features;
                int material = 0;
                if (false && /* legacy title hack; the generic combiner is exact now */
                    features.opt_2cyc && features.numInputs == 3 &&
                    features.c[1][0][0] == SHADER_INPUT_1 &&
                    features.c[1][0][1] == SHADER_INPUT_3 &&
                    features.c[1][0][2] == SHADER_COMBINED &&
                    features.c[1][0][3] == SHADER_INPUT_3) {
                    material = features.c[0][0][0] == SHADER_TEXEL0 ? 1 :
                               features.c[0][0][0] == SHADER_TEXEL1 ? 2 : 0;
                }
                if (material && !nativeCombinerTraced_) {
                    oot_ps3_early_trace("fast3d-rsx: native title combiner selected");
                    nativeCombinerTraced_ = true;
                }
                if (material == 2 && !letteringUvTraced_) {
                    oot_ps3_early_trace("fast3d-rsx: native Y orientation; original title UVs");
                    letteringUvTraced_ = true;
                }
                {
                    // Real Fast3D vertex layout: pos, [uv (+clampS)(+clampT)] per texture,
                    // [fog rgba], [grayscale rgba], then the combiner input colors.
                    const CCFeatures& lf = active_->features;
                    uint32_t off = 4U;
                    uint32_t uvOff[2] = { 0xFFFFU, 0xFFFFU };
                    for (int t = 0; t < 2; ++t) {
                        if (!lf.usedTextures[t]) continue;
                        uvOff[t] = off;
                        off += 2U + (lf.clamp[t][0] ? 1U : 0U) + (lf.clamp[t][1] ? 1U : 0U);
                    }
                    uint32_t fogOff = 0xFFFFU;
                    if (lf.opt_fog) { fogOff = off; off += 4U; }
                    if (lf.opt_grayscale) off += 4U;
                    const uint32_t per = alpha_ ? 4U : 3U;
                    const uint32_t inputFloats = static_cast<uint32_t>(lf.numInputs) * per;
                    const uint32_t stride32 = static_cast<uint32_t>(stride);
                    uint32_t inputOff = off;
                    if (inputOff + inputFloats != stride32) {
                        static int layoutTraced = 0;
                        if (layoutTraced < 4) {
                            char msg[180];
                            snprintf(msg, sizeof(msg),
                                     "fast3d-layout: stride=%u expected=%u inputs=%d per=%u fog=%d gray=%d; using vertex tail",
                                     stride32, inputOff + inputFloats, lf.numInputs, per, (int)lf.opt_fog,
                                     (int)lf.opt_grayscale);
                            oot_ps3_early_trace(msg);
                            ++layoutTraced;
                        }
                        if (stride32 >= 4U + inputFloats) inputOff = stride32 - inputFloats;
                        fogOff = 0xFFFFU; // layout not understood: don't read fog from a guessed offset
                    }
                    float packs[4];
                    const int order[4][2] = { { 0, 0 }, { 0, 1 }, { 1, 0 }, { 1, 1 } };
                    for (int p = 0; p < 4; ++p) {
                        float v = 0.0f;
                        for (int k = 3; k >= 0; --k) {
                            int code = lf.c[order[p][0]][order[p][1]][k];
                            if (code < 0 || code > 15) code = 0;
                            v = v * 16.0f + static_cast<float>(code);
                        }
                        packs[p] = v;
                    }
                    const float flags = (lf.opt_2cyc ? 1.0f : 0.0f) + (lf.opt_alpha ? 2.0f : 0.0f) +
                                        0.0f /* fog: folded into the per-vertex coefficients (fogOff) */ + (lf.opt_texture_edge ? 8.0f : 0.0f) +
                                        (lf.opt_alpha_threshold ? 16.0f : 0.0f);
                    oot_ps3_rsx_set_draw_state(uvOff[0], uvOff[1], fogOff, inputOff, per, flags, packs);
                }
                if (oot_ps3_rsx_triangles(vbo, static_cast<uint32_t>(stride),
                        static_cast<uint32_t>(triangles), tex0 ? selected_[0] : 0,
                        tex1 ? selected_[1] : 0, alpha_,
                        static_cast<uint32_t>(features.numInputs), material)) {
                    valid_ += static_cast<uint32_t>(triangles);
                } else {
                    oot_ps3_early_trace("fast3d-rsx: triangle submission failed");
                }
            }
            return;
        }
        if (!pixels_ || !triangles || length < triangles * 3 * 4) return;
        const size_t stride = length / (triangles * 3);
        if (stride < 4 || stride > 32) return;
        const bool textured = active_ && active_->features.usedTextures[0];
        const TextureState* texture = nullptr;
        if (textured && selected_[0] && selected_[0] <= textures_.size()) {
            texture = &textures_[selected_[0] - 1];
            if (texture->pixels.empty()) texture = nullptr;
        }
        if (!textured && alpha_ && stride == 8 && triangles == 2 &&
            TryFullscreenBlackFade(vbo, stride)) {
            valid_ += 2;
            if (targetFb_ != 0) offscreenWrites_ += static_cast<uint32_t>(width_ * height_);
            fadeTraced_ = true;
            oot_ps3_early_trace("fast3d: native full-screen fade fast path completed");
            return;
        }
        for (size_t tri = 0; tri < triangles; ++tri) {
            Point points[3]{};
            bool valid = true;
            for (size_t i = 0; i < 3; ++i) {
                const float* p = vbo + (tri * 3 + i) * stride;
                if (!std::isfinite(p[0]) || !std::isfinite(p[1]) ||
                    !std::isfinite(p[2]) || !std::isfinite(p[3]) ||
                    p[3] <= 0.00001f) { valid = false; break; }
                const float invW = 1.0f / p[3];
                if (std::fabs(p[0] * invW) > 16.0f || std::fabs(p[1] * invW) > 16.0f) {
                    valid = false; break;
                }
                points[i].x = vx_ + (p[0] * invW + 1.0f) * vw_ * 0.5f;
                points[i].y = vy_ + (p[1] * invW + 1.0f) * vh_ * 0.5f;
                points[i].z = p[2] * invW;
                points[i].u = textured && stride >= 6 ? p[4] : 0.0f;
                points[i].v = textured && stride >= 6 ? p[5] : 0.0f;
                const float* c = p + (stride >= 8 ? stride - 4 : 0);
                const auto byte = [](float f) -> uint32_t {
                    return static_cast<uint32_t>(std::max(0.0f, std::min(255.0f, f * 255.0f)));
                };
                points[i].color = (byte(c[0]) << 16) | (byte(c[1]) << 8) | byte(c[2]);
                points[i].opacity = (!textured && alpha_ && active_ &&
                                     active_->features.numInputs == 1 && stride == 8)
                                         ? static_cast<uint8_t>(byte(c[3])) : 255U;
            }
            if (valid) { ++valid_; DrawTriangle(points, texture); }
        }
        if (!textured && alpha_ && stride == 8 && !fadeTraced_) {
            oot_ps3_early_trace("fast3d: translucent title fade composited");
            fadeTraced_ = true;
        }
    }
    void Init() override {}
    void OnResize() override {}
    void StartFrame() override {
        submitted_ = valid_ = written_ = uploads_ = 0;
        colored_ = textured_ = clears_ = 0;
        targetFb_ = 0;
        drewOffscreen_ = false;
        offscreenWrites_ = 0;
        fadeTraced_ = false;
        titleDualTextureTraced_ = false;
        displayWidth_ = oot_ps3_shipwright_video_width();
        displayHeight_ = oot_ps3_shipwright_video_height();
        gpuActive_ = oot_ps3_rsx_begin(displayWidth_, displayHeight_) != 0;
        if (gpuActive_) {
            renderScale_ = 1;
            width_ = displayWidth_; height_ = displayHeight_;
            pixels_ = nullptr;
            vx_ = vy_ = sx_ = sy_ = 0;
            vw_ = sw_ = static_cast<int>(width_);
            vh_ = sh_ = static_cast<int>(height_);
            oot_ps3_early_trace("fast3d-rsx: drawing upstream game triangles on RSX");
            return;
        }
        oot_ps3_early_trace("fast3d-rsx: unavailable; native CPU fallback active");
        if (!oot_ps3_shipwright_video_acquire_fast3d(&displayPixels_, &displayWidth_, &displayHeight_)) {
            displayPixels_ = pixels_ = nullptr;
            oot_ps3_early_trace("fast3d-stats: framebuffer acquisition failed");
            return;
        }
        renderScale_ = (displayWidth_ % 3U == 0U && displayHeight_ % 3U == 0U) ? 3 : 2;
        width_ = displayWidth_ / static_cast<uint32_t>(renderScale_);
        height_ = displayHeight_ / static_cast<uint32_t>(renderScale_);
        renderPixels_.assign(static_cast<size_t>(width_) * height_, 0);
        gamePixels_.assign(static_cast<size_t>(width_) * height_, 0);
        pixels_ = renderPixels_.data();
        oot_ps3_early_trace("fast3d: reduced internal raster, full PS3 output mode");
        vx_ = vy_ = sx_ = sy_ = 0;
        vw_ = sw_ = static_cast<int>(width_);
        vh_ = sh_ = static_cast<int>(height_);
        depth_.assign(static_cast<size_t>(width_) * height_, 1.0f);
    }
    void EndFrame() override {}
    void FinishRender() override {
        if (gpuActive_) {
            char status[112];
            snprintf(status, sizeof(status), "fast3d-rsx: upstream triangles=%u submitted=%u",
                     submitted_, valid_);
            oot_ps3_early_trace(status);
            oot_ps3_debug_overlay_frame(displayWidth_, displayHeight_);
            oot_ps3_rsx_end();
            oot_ps3_shipwright_video_present_fast3d();
            oot_ps3_early_trace("fast3d-rsx: actual Shipwright frame presented");
            gpuActive_ = false;
            return;
        }
        char status[144];
        snprintf(status, sizeof(status),
                 "fast3d-stats: triangles=%u valid=%u pixels=%u uploads=%u size=%ux%u",
                 submitted_, valid_, written_, uploads_, width_, height_);
        oot_ps3_early_trace(status);
        // Shipwright renders into its game framebuffer, then clears the
        // window framebuffer for the PC GUI. No GUI is submitted on PS3:
        // present the completed game target instead of that empty window.
        const uint32_t* frame = drewOffscreen_ && offscreenWrites_ ? gamePixels_.data() : renderPixels_.data();
        if (drewOffscreen_ && offscreenWrites_)
            oot_ps3_early_trace("fast3d: native offscreen game framebuffer presented");
        if (frame && width_ && height_) {
            uint32_t nonblack = 0;
            const size_t count = static_cast<size_t>(width_) * height_;
            for (size_t i = 0; i < count; ++i) {
                if (frame[i] & 0x00FFFFFFU) ++nonblack;
            }
            snprintf(status, sizeof(status),
                     "fast3d-output: nonblack=%u colored-writes=%u textured-writes=%u clears=%u center=%08x",
                     nonblack, colored_, textured_, clears_, frame[count / 2]);
            oot_ps3_early_trace(status);
        }
        if (frame && displayPixels_) {
            for (uint32_t y = 0; y < height_; ++y) {
                uint32_t* dest = displayPixels_ + static_cast<size_t>(y * renderScale_) * displayWidth_;
                const uint32_t* src = frame + static_cast<size_t>(y) * width_;
                for (uint32_t x = 0; x < width_; ++x) {
                    const uint32_t value = src[x];
                    for (int i = 0; i < renderScale_; ++i) dest[static_cast<size_t>(x) * renderScale_ + i] = value;
                }
                for (int row = 1; row < renderScale_; ++row) {
                    std::memcpy(dest + static_cast<size_t>(row) * displayWidth_, dest,
                                static_cast<size_t>(displayWidth_) * sizeof(uint32_t));
                }
            }
            oot_ps3_shipwright_video_present_fast3d();
        }
        pixels_ = nullptr;
        displayPixels_ = nullptr;
    }
    int CreateFramebuffer() override { return nextFramebuffer_++; }
    void UpdateFramebufferParameters(int fb, uint32_t width, uint32_t height, uint32_t, bool, bool, bool,
                                     bool) override {
        if (!IsScreenFb(fb)) fbSizes_[fb] = { width, height };
    }
    // Interpreter::Init creates mGameFb then mGameFbMsaaResolved first (ids 1 and 2).
    // On PS3 the game frame goes straight to the display (nothing pastes mGameFb onto
    // the screen), so those, like 0, are the screen; only later ones are textures.
    static bool IsScreenFb(int fb) { return fb >= 0 && fb <= 2; }
    // Drawing into a framebuffer (gsSPSetFB: the pause-menu Link) renders into its RSX
    // texture, which SelectTextureFb then samples; fb 0 is the game frame.
    void StartDrawToFramebuffer(int fb, float) override {
        targetFb_ = fb;
        if (gpuActive_) {
            const auto size = fbSizes_.find(fb);
            if (!IsScreenFb(fb) && size != fbSizes_.end()) {
                if (!oot_ps3_rsx_set_target(FbSlot(fb), size->second.first, size->second.second)) {
                    oot_ps3_early_trace("graph: Play rsx framebuffer target failed");
                }
            } else {
                oot_ps3_rsx_set_target(-1, 0, 0);
            }
            return;
        }
        if (fb != 0) {
            drewOffscreen_ = true;
            pixels_ = gamePixels_.data();
        } else {
            pixels_ = renderPixels_.data();
        }
    }
    // Framebuffer ids map to the 8 RSX framebuffer texture slots, first come first
    // served (the pause-Link canvas, gPauseFrameBuffer - which can be -1 or a large
    // id -, ...), so a copy or render and the draw that samples it always agree.
    int FbSlot(int fb) {
        // Copy commands carry the id in 11 bits: gPauseFrameBuffer -1 arrives as 2047
        // there and as -1 in SelectTextureFb. Compare the 11-bit form.
        fb &= 0x7FF;
        for (int i = 0; i < fbSlotCount_; ++i) {
            if (fbSlotIds_[i] == fb) return i;
        }
        if (fbSlotCount_ < 8) {
            fbSlotIds_[fbSlotCount_] = fb;
            return fbSlotCount_++;
        }
        return 7;
    }
    // The game frame is drawn straight into the display buffer, so every copy reads
    // the current render target (SoH: the pause menu background, motion blur, ...).
    void CopyFramebuffer(int dst, int, int, int, int, int, int, int, int, int) override {
        static int sLogged = 0;
        if (sLogged < 4) {
            char line[80];
            snprintf(line, sizeof(line), "graph: Play rsx framebuffer copy dst=%d slot=%d", dst, FbSlot(dst));
            oot_ps3_early_trace(line);
            ++sLogged;
        }
        if (gpuActive_ && !oot_ps3_rsx_copy_fb(FbSlot(dst))) oot_ps3_early_trace("graph: Play rsx framebuffer copy failed");
    }
    void ClearFramebuffer(bool color, bool depth) override {
        if (gpuActive_) {
            // PC draws its GUI after the game target. Keep the already submitted
            // game frame intact when it clears the unused GUI framebuffer.
            // A framebuffer texture target (pause Link) is always cleared.
            if ((!IsScreenFb(targetFb_) || submitted_ == 0) && (color || depth)) oot_ps3_rsx_clear(color, depth);
            return;
        }
        if (!pixels_) return;
        const size_t count = static_cast<size_t>(width_) * height_;
        if (color) {
            std::fill_n(pixels_, count, 0x00000000U);
            ++clears_;
            if (targetFb_ != 0) offscreenWrites_ = 0;
        }
        if (depth) std::fill(depth_.begin(), depth_.end(), 1.0f);
    }
    void ReadFramebufferToCPU(int, uint32_t, uint32_t, uint16_t*) override {}
    void ResolveMSAAColorBuffer(int, int) override {}
    std::unordered_map<std::pair<float, float>, uint16_t, hash_pair_ff>
    GetPixelDepth(int, const std::set<std::pair<float, float>>&) override { return {}; }
    void* GetFramebufferTextureId(int) override { return nullptr; }
    void SelectTextureFb(int fb) override {
        selected_[0] = 0x8000U | static_cast<uint32_t>(FbSlot(fb)); // fast3d_rsx_ps3.c PS3_FB_TEXTURE_BIT
        uploadTile_ = 0;
    }
    void DeleteTexture(uint32_t id) override {
        if (id && id <= textures_.size()) textures_[id - 1].pixels.clear();
    }
    void SetTextureFilter(FilteringMode mode) override { filter_ = mode; }
    FilteringMode GetTextureFilter() override { return filter_; }
    void SetSrgbMode() override {}
    ImTextureID GetTextureById(int) override { return ImTextureID(); }
    void SetCurrentPrimDepth(float) override {}
  private:
    bool TryFullscreenBlackFade(const float* vbo, size_t stride) {
        if (!pixels_ || depthTest_ || vx_ != 0 || vy_ != 0 || sx_ != 0 || sy_ != 0 ||
            vw_ != static_cast<int>(width_) || vh_ != static_cast<int>(height_) ||
            sw_ != static_cast<int>(width_) || sh_ != static_cast<int>(height_)) return false;
        float minX = 10.0f, maxX = -10.0f, minY = 10.0f, maxY = -10.0f;
        const float alpha = vbo[7];
        if (!std::isfinite(alpha) || alpha < 0.0f || alpha > 1.0f) return false;
        for (size_t i = 0; i < 6; ++i) {
            const float* vertex = vbo + i * stride;
            if (!std::isfinite(vertex[3]) || vertex[3] <= 0.0f ||
                !std::isfinite(vertex[0]) || !std::isfinite(vertex[1]) ||
                std::fabs(vertex[4]) > 0.00001f || std::fabs(vertex[5]) > 0.00001f ||
                std::fabs(vertex[6]) > 0.00001f || std::fabs(vertex[7] - alpha) > 0.00001f)
                return false;
            const float x = vertex[0] / vertex[3];
            const float y = vertex[1] / vertex[3];
            minX = std::min(minX, x); maxX = std::max(maxX, x);
            minY = std::min(minY, y); maxY = std::max(maxY, y);
        }
        if (minX > -0.95f || maxX < 0.95f || minY > -0.95f || maxY < 0.95f) return false;
        const uint32_t opacity = static_cast<uint32_t>(alpha * 255.0f);
        const uint32_t inverse = 255U - opacity;
        const size_t count = static_cast<size_t>(width_) * height_;
        for (size_t i = 0; i < count; ++i) {
            const uint32_t old = pixels_[i];
            const uint32_t faded = (((((old >> 16) & 255U) * inverse) / 255U) << 16) |
                                   (((((old >> 8) & 255U) * inverse) / 255U) << 8) |
                                   (((old & 255U) * inverse) / 255U);
            pixels_[i] = faded;
            if (faded & 0x00FFFFFFU) ++colored_;
        }
        written_ += static_cast<uint32_t>(count);
        return true;
    }
    static float Edge(const Point& a, const Point& b, float x, float y) {
        return (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
    }
    void DrawTriangle(const Point (&p)[3], const TextureState* texture) {
        const float area = Edge(p[0], p[1], p[2].x, p[2].y);
        if (std::fabs(area) < 0.00001f) return;
        int x0 = std::max(0, std::max(sx_, static_cast<int>(std::floor(std::min({p[0].x, p[1].x, p[2].x})))));
        int y0 = std::max(0, std::max(sy_, static_cast<int>(std::floor(std::min({p[0].y, p[1].y, p[2].y})))));
        int x1 = std::min(static_cast<int>(width_), std::min(sx_ + sw_, static_cast<int>(std::ceil(std::max({p[0].x, p[1].x, p[2].x})))));
        int y1 = std::min(static_cast<int>(height_), std::min(sy_ + sh_, static_cast<int>(std::ceil(std::max({p[0].y, p[1].y, p[2].y})))));
        if (x0 >= x1 || y0 >= y1) return;
        const float invArea = 1.0f / area;
        const float stepAx = (p[2].y - p[1].y) * invArea;
        const float stepBx = (p[0].y - p[2].y) * invArea;
        const float stepAy = -(p[2].x - p[1].x) * invArea;
        const float stepBy = -(p[0].x - p[2].x) * invArea;
        float rowA = Edge(p[1], p[2], x0 + 0.5f, y0 + 0.5f) * invArea;
        float rowB = Edge(p[2], p[0], x0 + 0.5f, y0 + 0.5f) * invArea;
        for (int y = y0; y < y1; ++y, rowA += stepAy, rowB += stepBy) {
          float a = rowA, b = rowB;
          for (int x = x0; x < x1; ++x, a += stepAx, b += stepBx) {
            const float c = 1.0f - a - b;
            if (a < 0.0f || b < 0.0f || c < 0.0f) continue;
            const size_t offset = static_cast<size_t>(y) * width_ + x;
            const float z = p[0].z * a + p[1].z * b + p[2].z * c;
            if (depthTest_ && z > depth_[offset]) continue;
            uint32_t color = p[0].color;
            if (texture && texture->width && texture->height) {
                const float u = p[0].u * a + p[1].u * b + p[2].u * c;
                const float v = p[0].v * a + p[1].v * b + p[2].v * c;
                const int tx = ((static_cast<int>(u * texture->width) % static_cast<int>(texture->width)) + texture->width) % texture->width;
                const int ty = ((static_cast<int>(v * texture->height) % static_cast<int>(texture->height)) + texture->height) % texture->height;
                const uint8_t* rgba = &texture->pixels[(static_cast<size_t>(ty) * texture->width + tx) * 4];
                if (alpha_ && rgba[3] < 128) continue;
                color = (static_cast<uint32_t>(rgba[0]) << 16) |
                        (static_cast<uint32_t>(rgba[1]) << 8) | rgba[2];
            }
            if (!texture && alpha_ && p[0].opacity != 255U) {
                const uint32_t opacity = static_cast<uint32_t>(
                    p[0].opacity * a + p[1].opacity * b + p[2].opacity * c);
                const uint32_t old = pixels_[offset];
                const uint32_t inverse = 255U - opacity;
                color = (((((color >> 16) & 255U) * opacity + ((old >> 16) & 255U) * inverse) / 255U) << 16) |
                        (((((color >> 8) & 255U) * opacity + ((old >> 8) & 255U) * inverse) / 255U) << 8) |
                        ((((color & 255U) * opacity + (old & 255U) * inverse) / 255U));
            }
            pixels_[offset] = color;
            ++written_;
            if (targetFb_ != 0) ++offscreenWrites_;
            if (texture) ++textured_;
            if (color & 0x00FFFFFFU) ++colored_;
            if (depthWrite_) depth_[offset] = z;
          }
        }
    }
    std::map<std::pair<uint64_t, uint64_t>, ShaderProgram> shaders_;
    std::vector<TextureState> textures_;
    std::vector<float> depth_;
    uint32_t submitted_ = 0, valid_ = 0, written_ = 0, uploads_ = 0;
    uint32_t colored_ = 0, textured_ = 0, clears_ = 0;
    int targetFb_ = 0;
    bool drewOffscreen_ = false;
    bool fadeTraced_ = false;
    bool titleDualTextureTraced_ = false;
    unsigned diagnosticSamples_ = 0;
    unsigned diagnosticUploads_ = 0;
    bool nativeCombinerTraced_ = false;
    bool letteringUvTraced_ = false;
    bool gpuActive_ = false;
    ShaderProgram* active_ = nullptr;
    uint32_t* pixels_ = nullptr;
    uint32_t* displayPixels_ = nullptr;
    std::vector<uint32_t> renderPixels_;
    std::vector<uint32_t> gamePixels_;
    uint32_t offscreenWrites_ = 0;
    uint32_t width_ = 0, height_ = 0;
    uint32_t displayWidth_ = 0, displayHeight_ = 0;
    int renderScale_ = 1;
    uint32_t selected_[2]{};
    int uploadTile_ = 0;
    int vx_ = 0, vy_ = 0, vw_ = 320, vh_ = 240;
    int sx_ = 0, sy_ = 0, sw_ = 320, sh_ = 240;
    int nextFramebuffer_ = 1;
    std::map<int, std::pair<uint32_t, uint32_t>> fbSizes_;
    int fbSlotIds_[8]{};
    int fbSlotCount_ = 0;
    bool depthTest_ = false, depthWrite_ = false, alpha_ = false;
    FilteringMode filter_ = FILTER_NONE;
};
} // namespace Fast

// soh/frame_interpolation.cpp: matrix replacements for an in-between frame at `step` (0..1).
std::unordered_map<Mtx*, MtxF> FrameInterpolation_Interpolate(float step);

// One draw + present. index = draw number within the game update (0..2), t = how far
// between the previous and current update (1 = the real frame, no replacements).
static Fast::Ps3Renderer* sPs3Renderer;

// GPU vertex pipeline switches (USRDIR/tnl_on: try the GPU path, which still draws 3D
// black - off by default since 2026-11-08; USRDIR/tnl_cullflip:
// the RSX front face the other way round if models show their back faces).
static int Ps3FlagFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f == nullptr) return 0;
    fclose(f);
    return 1;
}
static int sTnlEnabled = -1;
// Debug menu: the experimental GPU vertex path, remembered with USRDIR/tnl_on.
extern "C" int oot_ps3_tnl_requested(void) {
    if (sTnlEnabled < 0) sTnlEnabled = Ps3FlagFile("/dev_hdd0/game/OOTP30048/USRDIR/tnl_on") ? 1 : 0;
    return sTnlEnabled;
}
extern "C" void oot_ps3_tnl_set_requested(int on) {
    sTnlEnabled = on ? 1 : 0;
    if (on) {
        FILE* flag = fopen("/dev_hdd0/game/OOTP30048/USRDIR/tnl_on", "wb");
        if (flag != nullptr) fclose(flag);
    } else {
        remove("/dev_hdd0/game/OOTP30048/USRDIR/tnl_on");
    }
    oot_ps3_early_trace(on ? "graph: Play GPU vertex pipeline on (debug menu)" : "graph: Play GPU vertex pipeline off (debug menu)");
}
extern "C" int oot_ps3_tnl_enabled(void) {
    int& sEnabled = sTnlEnabled;
    if (sEnabled < 0) {
        sEnabled = Ps3FlagFile("/dev_hdd0/game/OOTP30048/USRDIR/tnl_on") ? 1 : 0;
        oot_ps3_early_trace(sEnabled ? "graph: Play GPU vertex pipeline on (USRDIR/tnl_on)"
                                     : "graph: Play GPU vertex pipeline off (USRDIR/tnl_on turns it on)");
    }
    return sEnabled && sPs3Renderer != nullptr && sPs3Renderer->GpuActive();
}
extern "C" int oot_ps3_tnl_prepare(const void* inputs, uint32_t numInputs, int alphaInputs, float consts[25][4]) {
    return sPs3Renderer != nullptr ? sPs3Renderer->TnlPrepare(inputs, numInputs, alphaInputs, consts) : 0;
}
extern "C" int oot_ps3_tnl_submit(const void* records, uint32_t count, float* consts, int drawDirty,
                                  uint32_t firstConst, uint32_t numConst, int cullMode) {
    static int sCullFlip = -1;
    static int sFloat = -1;
    if (sCullFlip < 0) sCullFlip = Ps3FlagFile("/dev_hdd0/game/OOTP30048/USRDIR/tnl_cullflip");
    if (sFloat < 0) {
        sFloat = Ps3FlagFile("/dev_hdd0/game/OOTP30048/USRDIR/tnl_float");
        if (sFloat) oot_ps3_early_trace("graph: Play GPU vertex pipeline: float vertex attributes (USRDIR/tnl_float)");
    }
    return sPs3Renderer != nullptr ? sPs3Renderer->TnlSubmit(records, count, consts, drawDirty, firstConst, numConst,
                                                             cullMode, sCullFlip, sFloat)
                                   : 0;
}

extern "C" int oot_ps3_fast3d_render_commands_interp(void* commands, int index, float t, int interpolate) {
    if (!commands) return 0;
    if (!oot_ps3_shipwright_video_start()) return 0;
    static Fast::Ps3Window window;
    static Fast::Ps3Renderer renderer;
    static std::shared_ptr<Fast::Interpreter> interpreter;
    sPs3Renderer = &renderer;
    static std::unordered_map<Mtx*, MtxF> matrixReplacements;
    if (interpolate) {
        matrixReplacements = FrameInterpolation_Interpolate(t);
    } else {
        matrixReplacements.clear();
    }
    if (!interpreter) {
        oot_ps3_early_trace("fast3d: before native interpreter init");
        interpreter = std::make_shared<Fast::Interpreter>();
        Fast::GfxSetInstance(interpreter);
        interpreter->Init(&window, &renderer, "OoT PS3", true,
                          oot_ps3_shipwright_video_width(), oot_ps3_shipwright_video_height(), 0, 0);
        // G_SETTIMG with an OTR path otherwise goes through LoadResourceProcess every
        // time (~130 us each, ~7 ms per Kokiri Forest frame).
        interpreter->SetResolvedResourceCacheEnabled(true);
        oot_ps3_early_trace("fast3d: native interpreter initialized");
    }
    interpreter->mInterpolationIndex = index;
    interpreter->mInterpolationT = interpolate ? t : 1.0f;
    interpreter->StartFrame();
    oot_ps3_early_trace("fast3d: before real display-list execution");
    {
        const unsigned long long ps3RunT0 = oot_ps3_microseconds_now();
        interpreter->Run(static_cast<Gfx*>(commands), matrixReplacements);
        oot_ps3_fast3d_add_run(oot_ps3_microseconds_now() - ps3RunT0);
    }
    interpreter->EndFrame();
    oot_ps3_early_trace("fast3d: real display-list execution completed");
    return 1;
}

extern "C" int oot_ps3_fast3d_render_commands(void* commands) {
    return oot_ps3_fast3d_render_commands_interp(commands, 0, 1.0f, 0);
}
