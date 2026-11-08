#pragma once
#include <stdint.h>
typedef void* ImTextureID;
struct ImVec2 { float x; float y; ImVec2(float ix = 0.0f, float iy = 0.0f) : x(ix), y(iy) {} };
struct ImVec4 { float x; float y; float z; float w; ImVec4(float ix = 0.0f, float iy = 0.0f, float iz = 0.0f, float iw = 0.0f) : x(ix), y(iy), z(iz), w(iw) {} };
struct ImRect { ImVec2 Min; ImVec2 Max; };
template <typename T> class ImVector {};
namespace ImGui {
inline bool Begin(const char*, bool* = nullptr, int = 0) { return false; }
inline void End() {}
inline void Text(const char*, ...) {}
inline bool Button(const char*, const ImVec2& = ImVec2()) { return false; }
} // namespace ImGui
