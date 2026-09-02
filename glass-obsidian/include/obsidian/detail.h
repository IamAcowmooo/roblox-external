// -----------------------------------------------------------------------------
//  obsidian/detail.h  --  tiny portable math helpers
// -----------------------------------------------------------------------------
//  ImGui's IM_MAX / IM_MIN / IM_CLAMP / IM_FLOOR macros live in
//  imgui_internal.h, and ImVec2's arithmetic operators only exist when
//  IMGUI_DEFINE_MATH_OPERATORS is defined (which is an imconfig.h choice, not a
//  guarantee). This library sticks to imgui.h's public surface, so it brings its
//  own equivalents rather than depending on either.
// -----------------------------------------------------------------------------
#pragma once

#include "imgui.h"
#include <cfloat>

namespace obsidian {

inline float FMax(float a, float b) { return a >= b ? a : b; }
inline float FMin(float a, float b) { return a <= b ? a : b; }
inline float FClamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int   IClamp(int v, int lo, int hi)       { return v < lo ? lo : (v > hi ? hi : v); }

inline ImVec2 VAdd(const ImVec2& a, const ImVec2& b) { return ImVec2(a.x + b.x, a.y + b.y); }
inline ImVec2 VSub(const ImVec2& a, const ImVec2& b) { return ImVec2(a.x - b.x, a.y - b.y); }
inline ImVec2 VMul(const ImVec2& a, float s)         { return ImVec2(a.x * s, a.y * s); }
inline ImVec2 VMul(const ImVec2& a, const ImVec2& b) { return ImVec2(a.x * b.x, a.y * b.y); }
inline ImVec2 VMin(const ImVec2& a, const ImVec2& b) { return ImVec2(FMin(a.x, b.x), FMin(a.y, b.y)); }
inline ImVec2 VMax(const ImVec2& a, const ImVec2& b) { return ImVec2(FMax(a.x, b.x), FMax(a.y, b.y)); }
inline ImVec2 VClamp(const ImVec2& v, const ImVec2& lo, const ImVec2& hi)
{
    return ImVec2(FClamp(v.x, lo.x, hi.x), FClamp(v.y, lo.y, hi.y));
}
inline ImVec2 VLerp(const ImVec2& a, const ImVec2& b, float t)
{
    return ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
}
inline bool VEqual(const ImVec2& a, const ImVec2& b, float eps = 0.01f)
{
    return FMin(a.x, b.x) + eps > FMax(a.x, b.x) && FMin(a.y, b.y) + eps > FMax(a.y, b.y);
}

} // namespace obsidian
