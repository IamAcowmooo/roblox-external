// -----------------------------------------------------------------------------
//  src/glass.cpp  --  Glass Obsidian drawing primitives + widgets
// -----------------------------------------------------------------------------
#include "obsidian/glass.h"
#include "obsidian/theme.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <string>

namespace obsidian {

// =============================================================================
//  Animation
// =============================================================================
namespace anim {
namespace {
std::unordered_map<std::string, float>& GlobalStore()
{
    static std::unordered_map<std::string, float> s;
    return s;
}
float Approach(float cur, float target, float speed)
{
    float dt = ImGui::GetIO().DeltaTime;
    if (!(dt > 0.0f)) dt = 1.0f / 60.0f;
    const float k = 1.0f - std::exp(-speed * dt);   // frame-rate independent
    cur += (target - cur) * k;
    if (std::fabs(target - cur) < 0.0015f) cur = target;
    return cur;
}
} // namespace

float Step(ImGuiID id, float target, float speed)
{
    ImGuiStorage* st = ImGui::GetStateStorage();
    const float cur  = st->GetFloat(id, target);
    const float next = Approach(cur, target, speed);
    st->SetFloat(id, next);
    return next;
}

float Step(const char* seed, float target, float speed)
{
    return Step(ImGui::GetID(seed), target, speed);
}

float StepGlobal(const char* seed, float target, float speed)
{
    auto& store = GlobalStore();
    auto it = store.find(seed);
    const float cur = (it == store.end()) ? target : it->second;
    const float next = Approach(cur, target, speed);
    store[seed] = next;
    return next;
}

float Ease(float t)
{
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

float EaseOut(float t)
{
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
}

float Pulse(float hz)
{
    return 0.5f + 0.5f * static_cast<float>(std::sin(ImGui::GetTime() * 6.2831853f * hz));
}
} // namespace anim

// =============================================================================
//  Metrics
// =============================================================================
const Metrics& M()
{
    static Metrics m;
    static float cached_scale = -1.0f;
    const float sc = S(1.0f);
    if (sc != cached_scale) {
        cached_scale = sc;
        m.pad_panel = 12.0f * sc;
        m.pad_row   =  9.0f * sc;
        m.track_h   =  8.0f * sc;
        m.control_h = 26.0f * sc;
        m.knob_d    = 15.0f * sc;
        m.toggle_w  = 38.0f * sc;
        m.toggle_h  = 20.0f * sc;
    }
    return m;
}

// =============================================================================
//  Small local helpers
// =============================================================================
namespace {

ImU32 U32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }

void ToUpper(const char* in, char* out, size_t n)
{
    size_t i = 0;
    if (n == 0) return;
    for (; in[i] && i + 1 < n; ++i) out[i] = (char)toupper((unsigned char)in[i]);
    out[i] = '\0';
}

// Draw text with letter-spacing (tracking). Used for section headers, where
// +1.2px tracking is what separates "designed" from "typed".
float DrawTracked(ImDrawList* d, ImFont* f, float size, ImVec2 pos, ImU32 col,
                  const char* text, float tracking)
{
    if (!f) { d->AddText(pos, col, text); return ImGui::CalcTextSize(text).x; }
    float x = pos.x;
    char ch[2] = { 0, 0 };
    for (const char* s = text; *s; ++s) {
        ch[0] = *s;
        d->AddText(f, size, ImVec2(x, pos.y), col, ch);
        x += f->CalcTextSizeA(size, FLT_MAX, -1.0f, ch).x + tracking;
    }
    return (x - pos.x) - tracking;
}

float MeasureTracked(ImFont* f, float size, const char* text, float tracking)
{
    if (!f) return ImGui::CalcTextSize(text).x;
    float w = 0.0f;
    char ch[2] = { 0, 0 };
    for (const char* s = text; *s; ++s) {
        ch[0] = *s;
        w += f->CalcTextSizeA(size, FLT_MAX, -1.0f, ch).x + tracking;
    }
    return w > 0.0f ? w - tracking : 0.0f;
}

ImTextureID g_blur_tex = (ImTextureID)0;
ImVec4      g_blur_uv  = ImVec4(0, 0, 1, 1);

} // namespace

float TextWidth(ImFont* f, const char* text)
{
    if (!text) return 0.0f;
    if (!f) return ImGui::CalcTextSize(text).x;
    return f->CalcTextSizeA(f->FontSize, FLT_MAX, -1.0f, text).x;
}

const char* VisibleLabel(const char* label)
{
    if (!label) return "";
    if (label[0] == '#' && label[1] == '#') return "";   // "##id" / "###id"
    return label;
}

void SetBackdropBlurTexture(ImTextureID tex, const ImVec4& uv_rect)
{
    g_blur_tex = tex; g_blur_uv = uv_rect;
}
ImTextureID GetBackdropBlurTexture() { return g_blur_tex; }

// =============================================================================
//  Drawing primitives
// =============================================================================

void GradientFillRounded(ImDrawList* d, const ImVec2& a, const ImVec2& b,
                         const ImVec4& top, const ImVec4& bottom,
                         float rounding, int bands)
{
    const float h = b.y - a.y;
    if (h <= 0.0f || b.x <= a.x) return;

    // Identical stops: one quad, no clips.
    if (top.x == bottom.x && top.y == bottom.y && top.z == bottom.z && top.w == bottom.w) {
        d->AddRectFilled(a, b, U32(top), rounding);
        return;
    }

    // Fewer bands for short surfaces -- keeps vertex cost proportional to area.
    int n = std::min(bands, std::max(2, (int)(h / 5.0f)));

    for (int i = 0; i < n; ++i) {
        const float t0 = (float)i / (float)n;
        const float t1 = (float)(i + 1) / (float)n;
        const ImVec4 c = Mix(top, bottom, (t0 + t1) * 0.5f);
        // Bands are exactly adjacent (never overlapping) so equal-alpha layers
        // cannot compound into seams.
        const ImVec2 cmin(a.x, a.y + h * t0);
        const ImVec2 cmax(b.x, a.y + h * t1);
        d->PushClipRect(cmin, cmax, true);
        d->AddRectFilled(a, b, U32(c), rounding);
        d->PopClipRect();
    }
}

void GradientFillRoundedH(ImDrawList* d, const ImVec2& a, const ImVec2& b,
                          const ImVec4& left, const ImVec4& right,
                          float rounding, int bands)
{
    const float w = b.x - a.x;
    if (w <= 0.0f || b.y <= a.y) return;
    if (left.x == right.x && left.y == right.y && left.z == right.z && left.w == right.w) {
        d->AddRectFilled(a, b, U32(left), rounding);
        return;
    }
    int n = std::min(bands, std::max(2, (int)(w / 5.0f)));
    for (int i = 0; i < n; ++i) {
        const float t0 = (float)i / (float)n;
        const float t1 = (float)(i + 1) / (float)n;
        const ImVec4 c = Mix(left, right, (t0 + t1) * 0.5f);
        d->PushClipRect(ImVec2(a.x + w * t0, a.y), ImVec2(a.x + w * t1, b.y), true);
        d->AddRectFilled(a, b, U32(c), rounding);
        d->PopClipRect();
    }
}

void SoftShadow(ImDrawList* d, const ImVec2& a, const ImVec2& b, const ImVec4& col,
                float rounding, float thickness, const ImVec2& offset,
                bool spill_outside_window)
{
    if (col.w <= 0.0f) return;
    // PushClipRectFullScreen() abandons the CURRENT clip -- i.e. the panel or
    // child we are inside -- so a glow drawn with it escapes the panel and even
    // the whole window (visible as stray bars on the desktop). Only a top-level
    // window's shadow may spill.
    if (spill_outside_window) d->PushClipRectFullScreen();
    d->AddShadowRect(a, b, U32(col), thickness, offset, ImDrawFlags_None, rounding);
    if (spill_outside_window) d->PopClipRect();
}

void StrokeSheen(ImDrawList* d, const ImVec2& a, const ImVec2& b, const ImVec4& col,
                 float rounding)
{
    if (col.w <= 0.0f) return;
    const float h = b.y - a.y;
    // Three clipped strokes of increasing strength => specular that fades down.
    const float frac[3]  = { 0.62f, 0.30f, 0.13f };
    const float gain[3]  = { 0.30f, 0.55f, 1.00f };
    for (int i = 0; i < 3; ++i) {
        d->PushClipRect(ImVec2(a.x - 1.0f, a.y - 1.0f),
                        ImVec2(b.x + 1.0f, a.y + h * frac[i]), true);
        d->AddRect(a, b, U32(Fade(col, gain[i])), rounding, 0, 1.0f);
        d->PopClipRect();
    }
}

void StrokeEdge(ImDrawList* d, const ImVec2& a, const ImVec2& b, const ImVec4& col,
                float rounding, float thickness, bool inner)
{
    if (col.w <= 0.0f) return;
    const float o = inner ? thickness * 0.5f : 0.0f;
    d->AddRect(ImVec2(a.x + o, a.y + o), ImVec2(b.x - o, b.y - o), U32(col),
               rounding > o ? rounding - o : 0.0f, 0, thickness);
}

void EdgeGlow(ImDrawList* d, const ImVec2& a, const ImVec2& b, const ImVec4& col,
              float rounding, int edge)
{
    if (col.w <= 0.0f) return;
    const float w = b.x - a.x, h = b.y - a.y;
    const int   n = 6;
    for (int i = 0; i < n; ++i) {
        const float t = (float)i / (float)n;
        const float alpha = col.w * (1.0f - t) * (1.0f - t);
        ImVec2 p0 = a, p1 = b;
        switch (edge) {
        case 0: p1.y = a.y + h * (t + 1.0f / n) * 0.42f; break;               // top
        case 1: p0.y = b.y - h * (t + 1.0f / n) * 0.42f; break;               // bottom
        case 2: p1.x = a.x + w * (t + 1.0f / n) * 0.42f; break;               // left
        default: break;
        }
        if (edge == 3) {   // all-round inner bloom: just stroke with fading alpha
            d->AddRect(ImVec2(a.x + t * 3.0f, a.y + t * 3.0f),
                       ImVec2(b.x - t * 3.0f, b.y - t * 3.0f),
                       U32(Fade(col, alpha * 0.5f)), rounding, 0, 1.0f);
            continue;
        }
        d->PushClipRect(p0, p1, true);
        d->AddRectFilled(a, b, U32(Fade(col, alpha)), rounding);
        d->PopClipRect();
    }
}

void Hairline(ImDrawList* d, const ImVec2& a, const ImVec2& b, const ImVec4& col)
{
    d->AddLine(a, b, U32(col), 1.0f);
}

void AccentRule(ImDrawList* d, const ImVec2& a, float width, const ImVec4& col, float thickness)
{
    if (width <= 1.0f || col.w <= 0.0f) return;
    // Fade out towards both ends: sin(pi*t) envelope, 24 slices.
    const int n = 24;
    for (int i = 0; i < n; ++i) {
        const float t0 = (float)i / n, t1 = (float)(i + 1) / n;
        const float env = std::sin(3.14159265f * (t0 + t1) * 0.5f);
        d->AddRectFilled(ImVec2(a.x + width * t0, a.y - thickness * 0.5f),
                         ImVec2(a.x + width * t1, a.y + thickness * 0.5f),
                         U32(Fade(col, env)));
    }
}

// -----------------------------------------------------------------------------
//  Icons (vector, no icon font required)
// -----------------------------------------------------------------------------
namespace icon {
namespace {
ImVec2 Rot(ImVec2 v, Dir dir)
{
    switch (dir) {
    case Right: return ImVec2(-v.y,  v.x);
    case Down:  return ImVec2(-v.x, -v.y);
    case Left:  return ImVec2( v.y, -v.x);
    default:    return v;
    }
}
// Stroke weight derived from the icon size keeps every icon optically
// consistent at any scale, and keeps the 4-argument IconDrawFn signature.
inline float W(float size, float k = 0.115f) { return size * k; }
} // namespace

void Chevron(ImDrawList* d, ImVec2 c, float size, ImU32 col, Dir dir, float thick)
{
    const float s = size * 0.5f;
    const ImVec2 p0 = c + Rot(ImVec2(-s * 0.62f, -s * 0.42f), dir);
    const ImVec2 p1 = c + Rot(ImVec2( 0.0f,       s * 0.42f), dir);
    const ImVec2 p2 = c + Rot(ImVec2( s * 0.62f, -s * 0.42f), dir);
    d->PathLineTo(p0); d->PathLineTo(p1); d->PathLineTo(p2);
    d->PathStroke(col, ImDrawFlags_None, thick);
}
void ChevronUp(ImDrawList* d, ImVec2 c, float s, ImU32 col)    { Chevron(d, c, s, col, Up,    W(s, 0.14f)); }
void ChevronDown(ImDrawList* d, ImVec2 c, float s, ImU32 col)  { Chevron(d, c, s, col, Down,  W(s, 0.14f)); }
void ChevronLeft(ImDrawList* d, ImVec2 c, float s, ImU32 col)  { Chevron(d, c, s, col, Left,  W(s, 0.14f)); }
void ChevronRight(ImDrawList* d, ImVec2 c, float s, ImU32 col) { Chevron(d, c, s, col, Right, W(s, 0.14f)); }

void Check(ImDrawList* d, ImVec2 c, float size, ImU32 col)
{
    const float s = size * 0.5f;
    d->PathLineTo(ImVec2(c.x - s * 0.72f, c.y + s * 0.02f));
    d->PathLineTo(ImVec2(c.x - s * 0.20f, c.y + s * 0.56f));
    d->PathLineTo(ImVec2(c.x + s * 0.78f, c.y - s * 0.52f));
    d->PathStroke(col, ImDrawFlags_None, W(size, 0.15f));
}

void Close(ImDrawList* d, ImVec2 c, float size, ImU32 col)
{
    const float s = size * 0.32f, t = W(size, 0.13f);
    d->AddLine(ImVec2(c.x - s, c.y - s), ImVec2(c.x + s, c.y + s), col, t);
    d->AddLine(ImVec2(c.x + s, c.y - s), ImVec2(c.x - s, c.y + s), col, t);
}

void Minus(ImDrawList* d, ImVec2 c, float size, ImU32 col)
{
    const float s = size * 0.32f;
    d->AddLine(ImVec2(c.x - s, c.y), ImVec2(c.x + s, c.y), col, W(size, 0.13f));
}

void Plus(ImDrawList* d, ImVec2 c, float size, ImU32 col)
{
    const float s = size * 0.32f, t = W(size, 0.13f);
    d->AddLine(ImVec2(c.x - s, c.y), ImVec2(c.x + s, c.y), col, t);
    d->AddLine(ImVec2(c.x, c.y - s), ImVec2(c.x, c.y + s), col, t);
}

void Search(ImDrawList* d, ImVec2 c, float size, ImU32 col)
{
    const float t = W(size, 0.12f);
    const float r = size * 0.28f;
    const ImVec2 cc(c.x - size * 0.07f, c.y - size * 0.07f);
    d->AddCircle(cc, r, col, 0, t);
    const float k = r * 0.7071f;
    d->AddLine(ImVec2(cc.x + k, cc.y + k),
               ImVec2(c.x + size * 0.40f, c.y + size * 0.40f), col, t);
}

void Gear(ImDrawList* d, ImVec2 c, float size, ImU32 col)
{
    const float t = W(size, 0.115f);
    const float r0 = size * 0.20f, r1 = size * 0.44f;
    d->AddCircle(c, size * 0.15f, col, 0, t);
    for (int i = 0; i < 8; ++i) {
        const float ang = (float)i * (6.2831853f / 8.0f);
        const float ca = std::cos(ang), sa = std::sin(ang);
        d->AddLine(ImVec2(c.x + ca * r0, c.y + sa * r0),
                   ImVec2(c.x + ca * r1, c.y + sa * r1), col, t);
    }
}

void Power(ImDrawList* d, ImVec2 c, float size, ImU32 col)
{
    const float t = W(size, 0.13f);
    const float r = size * 0.34f;
    // Gap at the top of the ring (the universal power glyph): sweep the long
    // way around from just right of 12 o'clock to just left of it.
    d->PathArcTo(ImVec2(c.x, c.y + size * 0.05f), r, 3.14159265f * 1.72f,
                 3.14159265f * 3.28f, 22);
    d->PathStroke(col, ImDrawFlags_None, t);
    d->AddLine(ImVec2(c.x, c.y - size * 0.42f), ImVec2(c.x, c.y - size * 0.08f), col, t);
}

void Shield(ImDrawList* d, ImVec2 c, float size, ImU32 col)
{
    const float w = size * 0.38f, h = size * 0.46f;
    d->PathLineTo(ImVec2(c.x,             c.y - h));
    d->PathLineTo(ImVec2(c.x + w,         c.y - h * 0.50f));
    d->PathLineTo(ImVec2(c.x + w * 0.84f, c.y + h * 0.36f));
    d->PathLineTo(ImVec2(c.x,             c.y + h));
    d->PathLineTo(ImVec2(c.x - w * 0.84f, c.y + h * 0.36f));
    d->PathLineTo(ImVec2(c.x - w,         c.y - h * 0.50f));
    d->PathStroke(col, ImDrawFlags_Closed, W(size, 0.115f));
}

void Eye(ImDrawList* d, ImVec2 c, float size, ImU32 col)
{
    const float w = size * 0.46f, h = size * 0.24f;
    d->PathLineTo(ImVec2(c.x - w, c.y));
    d->PathBezierCubicCurveTo(ImVec2(c.x - w * 0.4f, c.y - h * 2.1f),
                              ImVec2(c.x + w * 0.4f, c.y - h * 2.1f),
                              ImVec2(c.x + w, c.y), 16);
    d->PathBezierCubicCurveTo(ImVec2(c.x + w * 0.4f, c.y + h * 2.1f),
                              ImVec2(c.x - w * 0.4f, c.y + h * 2.1f),
                              ImVec2(c.x - w, c.y), 16);
    d->PathStroke(col, ImDrawFlags_Closed, W(size, 0.115f));
    d->AddCircleFilled(c, size * 0.11f, col, 12);
}

void Sliders(ImDrawList* d, ImVec2 c, float size, ImU32 col)
{
    const float t = W(size, 0.11f);
    const float w = size * 0.42f;
    const float xs[3] = { -0.14f, 0.18f, -0.02f };
    for (int i = 0; i < 3; ++i) {
        const float y = c.y + (float)(i - 1) * size * 0.28f;
        d->AddLine(ImVec2(c.x - w, y), ImVec2(c.x + w, y), col, t);
        d->AddCircleFilled(ImVec2(c.x + xs[i] * size, y), size * 0.085f, col, 10);
    }
}

void Crosshair(ImDrawList* d, ImVec2 c, float size, ImU32 col)
{
    const float t = W(size, 0.115f);
    const float r = size * 0.32f, g = size * 0.10f, L = size * 0.46f;
    d->AddCircle(c, r, col, 0, t);
    d->AddLine(ImVec2(c.x - L, c.y), ImVec2(c.x - g, c.y), col, t);
    d->AddLine(ImVec2(c.x + g, c.y), ImVec2(c.x + L, c.y), col, t);
    d->AddLine(ImVec2(c.x, c.y - L), ImVec2(c.x, c.y - g), col, t);
    d->AddLine(ImVec2(c.x, c.y + g), ImVec2(c.x, c.y + L), col, t);
}

void Palette(ImDrawList* d, ImVec2 c, float size, ImU32 col)
{
    d->AddCircle(c, size * 0.42f, col, 0, W(size, 0.115f));
    const float r = size * 0.22f;
    for (int i = 0; i < 3; ++i) {
        const float ang = 3.14159265f * (0.30f + 0.45f * (float)i);
        d->AddCircleFilled(ImVec2(c.x + std::cos(ang) * r * 1.4f,
                                  c.y - std::sin(ang) * r * 1.4f), size * 0.055f, col, 8);
    }
}

void Keyboard(ImDrawList* d, ImVec2 c, float size, ImU32 col)
{
    const ImVec2 a(c.x - size * 0.46f, c.y - size * 0.28f);
    const ImVec2 b(c.x + size * 0.46f, c.y + size * 0.28f);
    d->AddRect(a, b, col, size * 0.12f, 0, W(size, 0.10f));
    const float y0 = c.y - size * 0.10f;
    for (int i = 0; i < 3; ++i)
        d->AddRectFilled(ImVec2(c.x - size * 0.28f + (float)i * size * 0.22f, y0),
                         ImVec2(c.x - size * 0.17f + (float)i * size * 0.22f, y0 + size * 0.075f), col);
    d->AddRectFilled(ImVec2(c.x - size * 0.17f, c.y + size * 0.06f),
                     ImVec2(c.x + size * 0.17f, c.y + size * 0.135f), col);
}
} // namespace icon

// =============================================================================
//  Surfaces
// =============================================================================
namespace {

void DrawPanelChrome(PanelFlags flags, float rounding_override = -1.0f)
{
    const Palette& p = ActivePalette();
    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetWindowPos();
    const ImVec2 b = ImVec2(a.x + ImGui::GetWindowSize().x, a.y + ImGui::GetWindowSize().y);
    if (b.x <= a.x || b.y <= a.y) return;

    const float r = rounding_override >= 0.0f ? rounding_override : p.corner;
    const float sa = p.SurfaceAlphaScale();

    ImVec4 base = p.panel;
    if (flags & PanelFlags_Inset)    base = Shade(p.panel, -0.30f);
    if (flags & PanelFlags_Elevated) base = Shade(p.panel,  0.12f);
    base.w *= sa;

    if (flags & PanelFlags_Elevated)
        SoftShadow(d, a, b, Fade(p.shadow, 0.55f), r, S(18.0f), ImVec2(0, S(6.0f)));

    // Real backdrop blur if the renderer supplied one, otherwise the
    // translucency approximation (gradient + sheen).
    if (g_blur_tex) {
        d->AddImageRounded(g_blur_tex, a, b,
                           ImVec2(g_blur_uv.x, g_blur_uv.y), ImVec2(g_blur_uv.z, g_blur_uv.w),
                           U32(ImVec4(1, 1, 1, 0.85f * sa)), r);
    }

    GradientFillRounded(d, a, b, Shade(base, 0.10f), Shade(base, -0.12f), r);

    if (!(flags & PanelFlags_NoSheen))
        StrokeSheen(d, a, b, Fade(p.sheen, 0.75f), r);

    EdgeGlow(d, a, b, Fade(p.inner_glow, 1.0f), r, 0);

    if (!(flags & PanelFlags_NoBorder))
        StrokeEdge(d, a, b, Fade(p.edge, (flags & PanelFlags_Elevated) ? 0.75f : 0.50f) , r);

    if (flags & PanelFlags_AccentTop) {
        d->PushClipRect(ImVec2(a.x - 1.0f, a.y - 1.0f), ImVec2(b.x + 1.0f, a.y + S(3.0f)), true);
        GradientFillRoundedH(d, a, ImVec2(b.x, a.y + S(2.0f)),
                             Fade(p.accent, 0.05f), Fade(p.accent, 0.85f), r, 10);
        d->PopClipRect();
    }
}

// Panels are tracked with a depth counter so that a stray PanelEnd() (or one
// issued after PanelBegin() bailed out early) can never unbalance ImGui's
// Begin/End stack -- it becomes a no-op instead of an assertion.
int& PanelDepth()
{
    static int depth = 0;
    return depth;
}

} // namespace

bool PanelBegin(const char* id, const ImVec2& size, PanelFlags flags)
{
    const Palette& p = ActivePalette();
    const Metrics& m = M();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 want  = size;
    if (want.x <= 0.0f) want.x = avail.x;

    // Height semantics:
    //   y >  0  fixed height
    //   y == 0  fit the content (AutoResizeY)  -- the common case
    //   y <  0  fill the parent, handed straight to BeginChild (which resolves
    //           negative sizes against the parent rect). Resolving it here with
    //           GetContentRegionAvail() would be wrong inside a SameLine row:
    //           the second column would read an already-consumed region and come
    //           out a different height from the first.
    const bool auto_h = (want.y == 0.0f) || (flags & PanelFlags_AutoResizeY);
    const bool fill_h = want.y < 0.0f;

    // Elevated panels need their shadow on the PARENT draw list (a child's
    // list is clipped to the child rect, so the shadow would be cut off).
    if ((flags & PanelFlags_Elevated) && !auto_h && !fill_h) {
        const ImVec2 pa = ImGui::GetCursorScreenPos();
        const ImVec2 pb = ImVec2(pa.x + want.x, pa.y + (want.y > 0.0f ? want.y : avail.y));
        SoftShadow(ImGui::GetWindowDrawList(), pa, pb, Fade(p.shadow, 0.5f),
                   p.corner, S(20.0f), ImVec2(0, S(7.0f)));
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(m.pad_panel, m.pad_panel));

    ImGuiChildFlags cf = ImGuiChildFlags_AlwaysUseWindowPadding;
    if (auto_h) cf |= ImGuiChildFlags_AutoResizeY;

    const bool open = ImGui::BeginChild(id, want, cf, ImGuiWindowFlags_None);
    ++PanelDepth();                     // BeginChild was called: EndChild is now mandatory
    if (open)
        DrawPanelChrome(flags & ~PanelFlags_Elevated);   // shadow already drawn by parent
    return open;
}

void PanelEnd()
{
    if (PanelDepth() <= 0) return;      // nothing open: ignore rather than assert
    --PanelDepth();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

ScopedPanel::ScopedPanel(const char* id, const ImVec2& size, PanelFlags flags)
    : m_open(PanelBegin(id, size, flags))
{
}
ScopedPanel::~ScopedPanel() { PanelEnd(); }

void SectionHeader(const char* label, bool accent_rule)
{
    const Palette& p = ActivePalette();
    const float w = ImGui::GetContentRegionAvail().x;
    const float line_h = ImGui::GetTextLineHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* d = ImGui::GetWindowDrawList();

    char up[128];
    ToUpper(VisibleLabel(label), up, sizeof(up));

    ImFont* f = FontBold();
    const float fs = (f ? f->FontSize : line_h) * 0.80f;
    const float tracking = S(1.3f);
    const ImVec4 col = Fade(p.text_faint, p.TextAlphaScale());

    // Vertically centre the smaller tracked text on the standard line height.
    const float tw = MeasureTracked(f, fs, up, tracking);
    DrawTracked(d, f, fs, ImVec2(pos.x, pos.y + (line_h - fs) * 0.5f), U32(col), up, tracking);

    if (accent_rule)
        AccentRule(d, ImVec2(pos.x + tw + S(11.0f), pos.y + line_h * 0.5f),
                   w - tw - S(11.0f), Fade(p.edge, 0.55f * p.EdgeAlphaScale()), 1.0f);

    ImGui::Dummy(ImVec2(w, line_h));
}

void KeyValue(const char* key, const char* value, const ImVec4* value_col)
{
    const Palette& p = ActivePalette();
    const Metrics& m = M();
    const float w = ImGui::GetContentRegionAvail().x;
    const float line_h = ImGui::GetTextLineHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* d = ImGui::GetWindowDrawList();

    const float ta = p.TextAlphaScale();
    d->AddText(pos, U32(Fade(p.text_dim, ta)), VisibleLabel(key));

    ImFont* mono = FontMono();
    const float vw = TextWidth(mono, value);
    const ImVec4 vc = value_col ? *value_col : p.text;
    if (mono) d->AddText(mono, mono->FontSize, ImVec2(pos.x + w - vw, pos.y), U32(Fade(vc, ta)), value);
    else      d->AddText(ImVec2(pos.x + w - vw, pos.y), U32(Fade(vc, ta)), value);

    // dotted leader between the two, only if there is room
    const float kx = pos.x + TextWidth(nullptr, key) + S(8.0f);
    const float vx = pos.x + w - vw - S(8.0f);
    if (vx - kx > S(24.0f)) {
        const float y = pos.y + line_h * 0.62f;
        for (float x = kx; x < vx; x += S(5.0f))
            d->AddRectFilled(ImVec2(x, y), ImVec2(x + S(1.4f), y + S(1.4f)),
                             U32(Fade(p.text_faint, 0.45f * ta)));
    }
    (void)m;
    ImGui::Dummy(ImVec2(w, line_h));
}

void Badge(const char* label, const ImVec4& tint, bool pulse)
{
    const Palette& p = ActivePalette();
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float line_h = ImGui::GetTextLineHeight();
    const float pad_x = S(9.0f);
    const char* vlabel = VisibleLabel(label);
    const float tw = TextWidth(nullptr, vlabel);
    const float dot_r = S(3.0f);
    const float h = line_h + S(6.0f);
    const float w = pad_x * 2.0f + tw + dot_r * 2.0f + S(7.0f);

    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 b(a.x + w, a.y + h);
    const float r = h * 0.5f;

    const float glow = pulse ? (0.45f + 0.55f * anim::Pulse(0.9f)) : 1.0f;

    GradientFillRounded(d, a, b, Fade(tint, 0.20f), Fade(tint, 0.09f), r);
    StrokeEdge(d, a, b, Fade(tint, 0.40f), r);
    d->AddCircleFilled(ImVec2(a.x + pad_x + dot_r, a.y + h * 0.5f), dot_r,
                       U32(Fade(tint, glow)), 12);
    if (pulse && glow > 0.6f)
        d->AddCircle(ImVec2(a.x + pad_x + dot_r, a.y + h * 0.5f),
                     dot_r + S(3.0f) * glow, U32(Fade(tint, 0.25f * (glow - 0.6f) / 0.4f)), 14, S(1.2f));
    d->AddText(ImVec2(a.x + pad_x + dot_r * 2.0f + S(7.0f), a.y + (h - line_h) * 0.5f),
               U32(Fade(Mix(tint, p.text, 0.35f), p.TextAlphaScale())), vlabel);

    ImGui::Dummy(ImVec2(w, h));
}

void Spacer(float h) { ImGui::Dummy(ImVec2(0.0f, h)); }

void VSeparator(float indent)
{
    const Palette& p = ActivePalette();
    const float line_h = ImGui::GetTextLineHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x + indent, pos.y + S(2.0f)),
                                        ImVec2(pos.x + indent, pos.y + line_h - S(2.0f)),
                                        U32(Fade(p.edge_soft, 0.9f)), 1.0f);
    ImGui::Dummy(ImVec2(indent + S(1.0f), line_h));
}

// =============================================================================
//  Buttons
// =============================================================================
namespace {

// Push the style set that makes a native control fully invisible while keeping
// all of its behaviour. Popped by EndInvisible().
void BeginInvisible()
{
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,   ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
}
void EndInvisible()
{
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(5);
}

void DrawFocusRing(ImDrawList* d, const ImVec2& a, const ImVec2& b, float rounding, const Palette& p)
{
    const float t = anim::Step("##focus", 1.0f, 22.0f);
    StrokeEdge(d, ImVec2(a.x - S(2.0f), a.y - S(2.0f)), ImVec2(b.x + S(2.0f), b.y + S(2.0f)),
               Fade(p.accent, 0.75f * t), rounding + S(2.0f), S(1.6f), false);
}

} // namespace

bool Button(const char* label, const ImVec2& size, ButtonKind kind)
{
    const Palette& p = ActivePalette();
    const float ta = p.TextAlphaScale();
    const float sa = p.SurfaceAlphaScale();

    ImVec2 sz = size;
    if (sz.x <= 0.0f) sz.x = TextWidth(nullptr, VisibleLabel(label)) + S(26.0f);
    if (sz.y <= 0.0f) sz.y = ControlHeight();

    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 b(a.x + sz.x, a.y + sz.y);
    const float r = p.corner;

    ImGui::PushID(label);
    BeginInvisible();
    const bool pressed = ImGui::Button("##b", sz);
    EndInvisible();
    const bool hov = ImGui::IsItemHovered();
    const bool act = ImGui::IsItemActive();
    const bool foc = ImGui::IsItemFocused();
    const ImGuiID id = ImGui::GetItemID();
    ImGui::PopID();

    ImDrawList* d = ImGui::GetWindowDrawList();
    const float ht = anim::Step(id,         hov ? 1.0f : 0.0f, 16.0f);
    const float at = anim::Step(id ^ 0x9E3779B9u, act ? 1.0f : 0.0f, 26.0f);

    const ImVec2 ia(a.x + at * S(1.0f), a.y + at * S(1.0f));
    const ImVec2 ib(b.x - at * S(1.0f), b.y - at * S(1.0f));

    switch (kind) {
    case ButtonKind_Primary:
    case ButtonKind_Danger: {
        const ImVec4 c1 = (kind == ButtonKind_Danger) ? p.danger : p.accent;
        const ImVec4 c2 = (kind == ButtonKind_Danger) ? Shade(p.danger, -0.28f) : p.accent_2;
        if (ht > 0.01f) {
            d->AddShadowRect(ia, ib, U32(Fade(c1, 0.42f * ht)), S(14.0f), ImVec2(0, S(4.0f)),
                             ImDrawFlags_None, r);
        }
        GradientFillRounded(d, ia, ib,
                            Mix(Shade(c1, 0.16f), c1, at),
                            Mix(c2, Shade(c2, -0.18f), at), r);
        StrokeSheen(d, ia, ib, Fade(ImVec4(1, 1, 1, 1), 0.22f), r);
        StrokeEdge(d, ia, ib, Fade(Shade(c1, 0.35f), 0.55f + 0.45f * ht), r);
        break;
    }
    case ButtonKind_Ghost: {
        if (ht > 0.01f) {
            GradientFillRounded(d, ia, ib, Fade(p.panel_hover, 0.85f * ht * sa),
                                Fade(p.panel, 0.55f * ht * sa), r);
            StrokeEdge(d, ia, ib, Fade(p.edge, 0.35f * ht), r);
        }
        break;
    }
    default: {
        GradientFillRounded(d, ia, ib,
                            Mix(Fade(p.panel, 0.92f * sa), Fade(p.panel_hover, 0.98f * sa), ht),
                            Mix(Fade(Shade(p.panel, -0.18f), 0.92f * sa), Fade(p.panel, 0.95f * sa), ht), r);
        if (ht > 0.01f) StrokeSheen(d, ia, ib, Fade(p.sheen, 0.6f * ht), r);
        StrokeEdge(d, ia, ib, Mix(Fade(p.edge, 0.55f), Fade(p.accent, 0.55f), ht), r);
        if (ht > 0.01f) EdgeGlow(d, ia, ib, Fade(p.accent, 0.09f * ht), r, 3);
        break;
    }
    }

    // label
    ImVec4 tcol;
    switch (kind) {
    case ButtonKind_Primary:
    case ButtonKind_Danger: tcol = p.text_on_accent; break;
    case ButtonKind_Ghost:  tcol = Mix(p.text_dim, p.text, ht); break;
    default:                tcol = Mix(p.text_dim, p.text, 0.35f + 0.65f * ht); break;
    }
    const float tw = TextWidth(nullptr, VisibleLabel(label));
    const float line_h = ImGui::GetTextLineHeight();
    d->AddText(ImVec2(ia.x + (sz.x - tw) * 0.5f, ia.y + (sz.y - line_h) * 0.5f + at * S(0.5f)),
               U32(Fade(tcol, ta)), VisibleLabel(label));

    if (foc) DrawFocusRing(d, a, b, r, p);
    return pressed;
}

bool IconButton(const char* id, IconDrawFn draw, const ImVec2& size, ButtonKind kind,
                const char* tooltip)
{
    const Palette& p = ActivePalette();
    ImVec2 sz = size;
    if (sz.x <= 0.0f) sz.x = ControlHeight();
    if (sz.y <= 0.0f) sz.y = sz.x;

    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 b(a.x + sz.x, a.y + sz.y);

    ImGui::PushID(id);
    BeginInvisible();
    const bool pressed = ImGui::Button("##ib", sz);
    EndInvisible();
    const bool hov = ImGui::IsItemHovered();
    const bool act = ImGui::IsItemActive();
    const bool foc = ImGui::IsItemFocused();
    const ImGuiID iid = ImGui::GetItemID();
    if (tooltip && hov) ImGui::SetTooltip("%s", tooltip);
    ImGui::PopID();

    ImDrawList* d = ImGui::GetWindowDrawList();
    const float ht = anim::Step(iid, hov ? 1.0f : 0.0f, 16.0f);
    const float at = anim::Step(iid ^ 0x9E3779B9u, act ? 1.0f : 0.0f, 26.0f);
    const float r  = FMin(sz.x, sz.y) * 0.30f;

    if (ht > 0.01f || kind == ButtonKind_Default) {
        const ImVec2 ia(a.x + at, a.y + at), ib(b.x - at, b.y - at);
        GradientFillRounded(d, ia, ib,
                            Fade(kind == ButtonKind_Danger ? p.danger : p.panel_hover,
                                 (kind == ButtonKind_Ghost ? 0.85f : 1.0f) * (0.35f + 0.65f * ht) * p.SurfaceAlphaScale()),
                            Fade(p.panel, 0.55f * ht * p.SurfaceAlphaScale()), r);
        StrokeEdge(d, ia, ib, Fade(kind == ButtonKind_Danger ? p.danger : p.edge,
                                   0.30f + 0.45f * ht), r);
    }

    const ImVec4 ic = (kind == ButtonKind_Danger) ? Mix(p.text_dim, p.danger, ht)
                                                  : Mix(p.text_dim, p.text, ht);
    if (draw)
        draw(d, ImVec2(a.x + sz.x * 0.5f, a.y + sz.y * 0.5f + at * S(0.5f)),
             FMin(sz.x, sz.y) * 0.50f, U32(Fade(ic, p.TextAlphaScale())));
    if (foc) DrawFocusRing(d, a, b, r, p);
    return pressed;
}

// =============================================================================
//  Toggle
// =============================================================================
bool ToggleOnly(const char* id, bool* v)
{
    const Palette& p = ActivePalette();
    const Metrics& m = M();
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 b(a.x + m.toggle_w, a.y + m.toggle_h);

    ImGui::PushID(id);
    BeginInvisible();
    const bool clicked = ImGui::Button("##t", ImVec2(m.toggle_w, m.toggle_h));
    EndInvisible();
    const bool hov = ImGui::IsItemHovered();
    const bool act = ImGui::IsItemActive();
    const bool foc = ImGui::IsItemFocused();
    const ImGuiID tid = ImGui::GetItemID();
    ImGui::PopID();

    if (clicked) *v = !*v;

    ImDrawList* d = ImGui::GetWindowDrawList();
    const float on = anim::Step(tid, *v ? 1.0f : 0.0f, 17.0f);
    const float ht = anim::Step(tid ^ 0x9E3779B9u, hov ? 1.0f : 0.0f, 16.0f);
    const float e  = anim::Ease(on);
    const float r  = m.toggle_h * 0.5f;

    // track
    const ImVec4 off_col = Mix(p.field, p.field_hover, ht);
    GradientFillRounded(d, a, b,
                        Mix(Shade(off_col, 0.06f), p.accent, e),
                        Mix(Shade(off_col, -0.10f), p.accent_2, e), r);
    StrokeEdge(d, a, b, Mix(Fade(p.edge, 0.85f), Fade(Shade(p.accent, 0.3f), 0.9f), e), r);

    // glow when on
    if (on > 0.02f) {
        d->AddShadowRect(a, b, U32(Fade(p.accent, 0.45f * on)), S(11.0f), ImVec2(0, 0),
                         ImDrawFlags_None, r);
    }

    // knob (stretches slightly while pressed -- the classic iOS feel)
    const float kd  = m.toggle_h - S(5.0f);
    const float stretch = act ? S(3.0f) : 0.0f;
    const float kx0 = a.x + S(2.5f) + e * (m.toggle_w - kd - S(5.0f)) - stretch * 0.5f;
    const ImVec2 ka(kx0, a.y + (m.toggle_h - kd) * 0.5f);
    const ImVec2 kb(kx0 + kd + stretch, ka.y + kd);
    const float kr = kd * 0.5f;
    d->AddShadowRect(ka, kb, U32(ImVec4(0, 0, 0, 0.42f)), S(4.0f), ImVec2(0, S(1.5f)),
                     ImDrawFlags_None, kr);
    GradientFillRounded(d, ka, kb, ImVec4(1, 1, 1, 0.99f), ImVec4(0.86f, 0.88f, 0.92f, 0.99f), kr);
    if (on > 0.5f)
        icon::Check(d, ImVec2((ka.x + kb.x) * 0.5f, (ka.y + kb.y) * 0.5f), kd * 0.66f,
                    U32(Fade(p.text_on_accent, (on - 0.5f) * 2.0f)));

    if (foc) DrawFocusRing(d, a, b, r, p);
    return clicked;
}

bool Toggle(const char* label, bool* v, const char* hint)
{
    const Palette& p = ActivePalette();
    const Metrics& m = M();
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = FMax(m.control_h, m.toggle_h + S(4.0f));
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const ImVec2 b(a.x + w, a.y + h);

    ImGui::PushID(label);
    BeginInvisible();
    const bool clicked = ImGui::Button("##row", ImVec2(w, h));
    EndInvisible();
    const bool hov = ImGui::IsItemHovered();
    const bool foc = ImGui::IsItemFocused();
    const ImGuiID rid = ImGui::GetItemID();
    ImGui::PopID();

    if (clicked) *v = !*v;

    ImDrawList* d = ImGui::GetWindowDrawList();
    const float ht = anim::Step(rid, hov ? 1.0f : 0.0f, 16.0f);
    const float on = anim::Step(rid ^ 0x85EBCA6Bu, *v ? 1.0f : 0.0f, 17.0f);
    const float ta = p.TextAlphaScale();

    // row wash
    if (ht > 0.01f) {
        GradientFillRounded(d, a, b, Fade(p.panel_hover, 0.55f * ht * p.SurfaceAlphaScale()),
                            Fade(p.panel, 0.25f * ht * p.SurfaceAlphaScale()), p.corner_sm);
        d->PushClipRect(ImVec2(a.x - 1.0f, a.y - 1.0f), ImVec2(a.x + S(3.0f), b.y + 1.0f), true);
        GradientFillRounded(d, ImVec2(a.x, a.y), ImVec2(a.x + S(2.0f), b.y),
                            Fade(p.accent, 0.9f * ht), Fade(p.accent_2, 0.25f * ht), S(1.0f), 4);
        d->PopClipRect();
    }

    // label
    const float line_h = ImGui::GetTextLineHeight();
    const ImVec4 lcol = Mix(p.text_dim, p.text, 0.25f + 0.75f * on);
    d->AddText(ImVec2(a.x + S(2.0f), a.y + (h - line_h) * 0.5f), U32(Fade(lcol, ta)), VisibleLabel(label));

    // hint, right aligned past the switch
    if (hint && *hint) {
        const float hw = TextWidth(nullptr, hint);
        d->AddText(ImVec2(b.x - m.toggle_w - S(14.0f) - hw, a.y + (h - line_h) * 0.5f),
                   U32(Fade(p.text_faint, 0.85f * ta)), hint);
    }

    // switch, right aligned
    const float e = anim::Ease(on);
    const ImVec2 ta_(b.x - m.toggle_w - S(2.0f), a.y + (h - m.toggle_h) * 0.5f);
    const ImVec2 tb_(ta_.x + m.toggle_w, ta_.y + m.toggle_h);
    const float r = m.toggle_h * 0.5f;

    const ImVec4 off_col = Mix(p.field, p.field_hover, ht);
    GradientFillRounded(d, ta_, tb_, Mix(Shade(off_col, 0.06f), p.accent, e),
                        Mix(Shade(off_col, -0.10f), p.accent_2, e), r);
    StrokeEdge(d, ta_, tb_, Mix(Fade(p.edge, 0.85f), Fade(Shade(p.accent, 0.3f), 0.9f), e), r);
    if (on > 0.02f) {
        d->AddShadowRect(ta_, tb_, U32(Fade(p.accent, 0.40f * on)), S(10.0f), ImVec2(0, 0),
                         ImDrawFlags_None, r);
    }
    const float kd = m.toggle_h - S(5.0f);
    const float kx = ta_.x + S(2.5f) + e * (m.toggle_w - kd - S(5.0f));
    const ImVec2 ka(kx, ta_.y + (m.toggle_h - kd) * 0.5f), kb(kx + kd, ka.y + kd);
    d->AddShadowRect(ka, kb, U32(ImVec4(0, 0, 0, 0.40f)), S(4.0f), ImVec2(0, S(1.5f)),
                     ImDrawFlags_None, kd * 0.5f);
    GradientFillRounded(d, ka, kb, ImVec4(1, 1, 1, 0.99f), ImVec4(0.86f, 0.88f, 0.92f, 0.99f), kd * 0.5f);

    if (foc) DrawFocusRing(d, a, b, p.corner_sm, p);
    return clicked;
}

// =============================================================================
//  Slider
// =============================================================================
namespace {

// Apply an optional power curve so ranges like 1..1000 can be finely controlled
// at the low end.
float CurveApply(float t, float power)
{
    if (power == 1.0f) return t;
    return std::pow(t, power);
}
float CurveInverse(float v, float power)
{
    if (power == 1.0f) return v;
    return std::pow(v, 1.0f / power);
}

float SnapTo(float v, float step, float vmin)
{
    if (step <= 0.0f) return v;
    return vmin + std::round((v - vmin) / step) * step;
}

// Draws label above + value read-out; returns the y offset where the track starts.
float DrawFieldLabel(const char* label, float width, const char* value_text,
                     bool label_dim, float value_emphasis, bool show_value)
{
    const Palette& p = ActivePalette();
    const float ta = p.TextAlphaScale();
    const float line_h = ImGui::GetTextLineHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* d = ImGui::GetWindowDrawList();

    if (label && *label) {
        const ImVec4 lc = label_dim ? p.text_faint : p.text_dim;
        d->AddText(pos, U32(Fade(lc, ta)), VisibleLabel(label));
    }
    if (show_value && value_text && *value_text) {
        ImFont* mono = FontMono();
        const float vw = TextWidth(mono, value_text);
        const ImVec4 vc = Mix(p.text_dim, p.accent, value_emphasis);
        if (mono) d->AddText(mono, mono->FontSize, ImVec2(pos.x + width - vw, pos.y),
                             U32(Fade(vc, ta)), value_text);
        else      d->AddText(ImVec2(pos.x + width - vw, pos.y), U32(Fade(vc, ta)), value_text);
    }
    ImGui::Dummy(ImVec2(width, line_h));
    return line_h;
}

} // namespace

bool Slider(const char* label, float* v, float vmin, float vmax, const char* fmt,
            float step, float default_value, const SliderOpts& opts)
{
    const Palette& p = ActivePalette();
    const Metrics& m = M();
    const float ta = p.TextAlphaScale();
    const float w = ImGui::GetContentRegionAvail().x;
    const float track_area = S(22.0f);
    const float line_h = ImGui::GetTextLineHeight();

    ImGui::PushID(label);

    char vbuf[64];
    // NB: the format string decides the argument type. Passing a double to a
    // "%d" format is undefined behaviour (this printed garbage like
    // "level 288899019" once), so integers go through as int.
    if (opts.integer) snprintf(vbuf, sizeof(vbuf), fmt ? fmt : "%.0f", (int)(int)*v);
    else              snprintf(vbuf, sizeof(vbuf), fmt ? fmt : "%.2f", (double)*v);

    // ---- layout: [label row] then [track hit-area]. Reserving the label row
    // with Dummy keeps the cursor maths trivial; the text itself is painted at
    // the end so the drag-time value pill sits on top of it.
    const ImVec2 label_pos = ImGui::GetCursorScreenPos();
    if (opts.show_label) ImGui::Dummy(ImVec2(w, line_h));

    // ---- interaction surface -------------------------------------------------
    // A real (but fully transparent) Button rather than InvisibleButton: we keep
    // native hit-testing, drag capture, nav focus and click activation.
    const ImVec2 slot_a = ImGui::GetCursorScreenPos();
    BeginInvisible();
    ImGui::Button("##track", ImVec2(w, track_area));
    EndInvisible();
    const bool hov = ImGui::IsItemHovered();
    const bool act = ImGui::IsItemActive();
    const bool foc = ImGui::IsItemFocused();
    const ImGuiID sid = ImGui::GetItemID();

    const ImVec2 slot_b(slot_a.x + w, slot_a.y + track_area);
    const float knob_r = m.knob_d * 0.5f;
    const float usable = FMax(1.0f, w - knob_r * 2.0f);
    const float range  = (vmax > vmin) ? (vmax - vmin) : 1.0f;

    auto value_from_x = [&](float mx) {
        float t = (mx - (slot_a.x + knob_r)) / usable;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        float nv = vmin + CurveApply(t, opts.power) * range;
        nv = SnapTo(nv, step, vmin);
        return nv < vmin ? vmin : (nv > vmax ? vmax : nv);
    };

    bool changed = false;
    if (act) {
        const float nv = value_from_x(ImGui::GetIO().MousePos.x);
        if (nv != *v) { *v = nv; changed = true; }
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (foc) {
        const float nudge = (step > 0.0f) ? step : range * 0.01f;
        float nv = *v;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)  || ImGui::IsKeyPressed(ImGuiKey_DownArrow)) nv -= nudge;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow))   nv += nudge;
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) nv = vmin;
        if (ImGui::IsKeyPressed(ImGuiKey_End))  nv = vmax;
        nv = SnapTo(nv < vmin ? vmin : (nv > vmax ? vmax : nv), step, vmin);
        if (nv != *v) { *v = nv; changed = true; }
    }
    // CTRL+click resets, matching stock ImGui slider behaviour.
    if (hov && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        ImGui::GetIO().KeyCtrl && !std::isnan(default_value)) {
        *v = default_value;
        changed = true;
    }

    // ---- paint ----------------------------------------------------------------
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float ht = anim::Step(sid, (hov || act) ? 1.0f : 0.0f, 16.0f);
    const float at = anim::Step(sid ^ 0x9E3779B9u, act ? 1.0f : 0.0f, 24.0f);

    const float t = CurveInverse((*v - vmin) / range, opts.power);
    const float cy = (slot_a.y + slot_b.y) * 0.5f;
    const float th = m.track_h * (1.0f + 0.18f * ht);
    const ImVec2 ra(slot_a.x, cy - th * 0.5f);
    const ImVec2 rb(slot_b.x, cy + th * 0.5f);
    const float rr = th * 0.5f;
    const float knob_x = slot_a.x + knob_r + t * usable;

    // track
    GradientFillRounded(d, ra, rb, Shade(Mix(p.field, p.field_hover, ht), -0.10f),
                        Shade(Mix(p.field, p.field_hover, ht), 0.08f), rr);
    StrokeEdge(d, ra, rb, Fade(p.edge, 0.40f + 0.25f * ht), rr);

    // filled portion
    if (knob_x > ra.x + 1.0f) {
        const ImVec2 fb(knob_x, rb.y);
        GradientFillRoundedH(d, ra, fb, Fade(p.accent, 0.92f), Fade(p.accent_2, 0.98f), rr);
        StrokeSheen(d, ra, fb, Fade(ImVec4(1, 1, 1, 1), 0.18f), rr);
        if (ht > 0.01f) {
            d->AddShadowRect(ra, fb, U32(Fade(p.accent, 0.45f * ht)), S(9.0f), ImVec2(0, 0),
                             ImDrawFlags_None, rr);
        }
    }

    // knob: soft drop shadow, accent bloom while engaged, white cap, inner dot
    const float kr = knob_r * (0.82f + 0.18f * ht + 0.10f * at);
    const ImVec2 kc(knob_x, cy);
    d->AddShadowRect(ImVec2(kc.x - kr, kc.y - kr), ImVec2(kc.x + kr, kc.y + kr),
                     U32(ImVec4(0, 0, 0, 0.45f)), S(6.0f), ImVec2(0, S(2.0f)), ImDrawFlags_None, kr);
    if (ht > 0.02f) {
        d->AddShadowRect(ImVec2(kc.x - kr, kc.y - kr), ImVec2(kc.x + kr, kc.y + kr),
                         U32(Fade(p.accent, 0.55f * ht)), S(12.0f), ImVec2(0, 0),
                         ImDrawFlags_None, kr);
    }
    d->AddCircleFilled(kc, kr, U32(ImVec4(0.96f, 0.97f, 0.99f, 1.0f)), 24);
    d->AddCircle(kc, kr, U32(Fade(Mix(p.text_faint, p.accent, ht), 0.55f)), 24, S(1.0f));
    d->AddCircleFilled(kc, kr * 0.34f, U32(Fade(Mix(p.edge, p.accent, 0.35f + 0.65f * ht), 0.9f)), 14);

    if (foc) DrawFocusRing(d, slot_a, slot_b, p.corner_sm, p);

    // ---- label row (painted last, on top) --------------------------------------
    if (opts.show_label && label && *label) {
        const ImVec4 lc = Mix(p.text_dim, p.text, 0.35f + 0.65f * ht);
        d->AddText(ImVec2(label_pos.x, label_pos.y), U32(Fade(lc, ta)), VisibleLabel(label));
    }
    if (opts.show_value) {
        ImFont* mono = FontMono();
        const float vw = TextWidth(mono, vbuf);
        const float vy = label_pos.y + (line_h - (mono ? mono->FontSize : line_h)) * 0.5f;
        const ImVec2 vp(label_pos.x + w - vw, vy);

        // While dragging the read-out gets an accent pill -- that is the
        // "value bubble", kept inside the row so it can never be clipped by the
        // panel or overflow the window during a collapse animation.
        if (at > 0.02f) {
            const ImVec2 pa(vp.x - S(7.0f), label_pos.y - S(1.5f));
            const ImVec2 pb(vp.x + vw + S(7.0f), label_pos.y + line_h + S(1.5f));
            GradientFillRounded(d, pa, pb, Fade(p.accent, 0.30f * at), Fade(p.accent_2, 0.20f * at), p.corner_sm);
            StrokeEdge(d, pa, pb, Fade(p.accent, 0.55f * at), p.corner_sm);
        }
        const ImVec4 vc = Mix(p.text_dim, p.accent, FMax(ht * 0.75f, at));
        if (mono) d->AddText(mono, mono->FontSize, vp, U32(Fade(vc, ta)), vbuf);
        else      d->AddText(vp, U32(Fade(vc, ta)), vbuf);
    }

    if (hov && !act)
        ImGui::SetTooltip("%s\nCTRL+click to reset", (label && *label) ? VisibleLabel(label) : "Value");

    ImGui::PopID();
    return changed;
}

bool SliderInt(const char* label, int* v, int vmin, int vmax, const char* fmt, int default_value)
{
    float f = (float)*v;
    SliderOpts o; o.integer = true;
    const bool changed = Slider(label, &f, (float)vmin, (float)vmax, fmt, 1.0f,
                                (default_value == INT_MIN) ? NAN : (float)default_value, o);
    if (changed) {
        int iv = (int)std::lround(f);
        iv = iv < vmin ? vmin : (iv > vmax ? vmax : iv);
        if (iv != *v) { *v = iv; return true; }
    }
    return false;
}

// =============================================================================
//  Text input
// =============================================================================
bool TextInput(const char* label, char* buf, size_t buf_size, const char* placeholder)
{
    const Palette& p = ActivePalette();
    const Metrics& m = M();
    const float w = ImGui::GetContentRegionAvail().x;
    const float line_h = ImGui::GetTextLineHeight();

    ImGui::PushID(label);

    // label above
    const ImVec2 lpos = ImGui::GetCursorScreenPos();
    if (label && *label)
        ImGui::GetWindowDrawList()->AddText(lpos, U32(Fade(p.text_dim, p.TextAlphaScale())), VisibleLabel(label));
    ImGui::Dummy(ImVec2(w, line_h + S(4.0f)));

    ImGui::PushStyleColor(ImGuiCol_FrameBg,        Fade(p.field, p.SurfaceAlphaScale()));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Fade(p.field_hover, p.SurfaceAlphaScale()));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  Fade(p.field_active, p.SurfaceAlphaScale()));
    ImGui::PushStyleColor(ImGuiCol_Border,         ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(10.0f), S(6.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, p.corner_sm);
    ImGui::SetNextItemWidth(w);
    const bool changed = ImGui::InputText("##ti", buf, buf_size);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);

    const bool act = ImGui::IsItemActive();
    const bool hov = ImGui::IsItemHovered();
    const ImVec2 a = ImGui::GetItemRectMin();
    const ImVec2 b = ImGui::GetItemRectMax();
    const ImGuiID iid = ImGui::GetItemID();
    ImDrawList* d = ImGui::GetWindowDrawList();

    const float ht = anim::Step(iid, hov ? 1.0f : 0.0f, 16.0f);
    const float ft = anim::Step(iid ^ 0x9E3779B9u, act ? 1.0f : 0.0f, 20.0f);

    StrokeEdge(d, a, b, Mix(Fade(p.edge, 0.55f), Fade(p.accent, 0.95f), ft), p.corner_sm);
    StrokeSheen(d, a, b, Fade(p.sheen, 0.45f), p.corner_sm);
    if (ft > 0.01f) {
        d->AddShadowRect(a, b, U32(Fade(p.accent, 0.40f * ft)), S(10.0f), ImVec2(0, 0),
                         ImDrawFlags_None, p.corner_sm);
        EdgeGlow(d, a, b, Fade(p.accent, 0.10f * ft), p.corner_sm, 3);
    }

    // placeholder (stock InputText has none)
    if (placeholder && buf[0] == '\0' && ft < 0.5f) {
        const float pad = S(10.0f);
        d->AddText(ImVec2(a.x + pad, a.y + (b.y - a.y - line_h) * 0.5f),
                   U32(Fade(p.text_faint, 0.85f * p.TextAlphaScale() * (1.0f - ft * 2.0f))),
                   placeholder);
    }
    (void)m;
    ImGui::PopID();
    return changed;
}

// =============================================================================
//  Combo
// =============================================================================
bool Combo(const char* label, int* current, const char* const* items, int count)
{
    const Palette& p = ActivePalette();
    const float w = ImGui::GetContentRegionAvail().x;
    const float line_h = ImGui::GetTextLineHeight();
    bool changed = false;

    ImGui::PushID(label);

    const ImVec2 lpos = ImGui::GetCursorScreenPos();
    if (label && *label)
        ImGui::GetWindowDrawList()->AddText(lpos, U32(Fade(p.text_dim, p.TextAlphaScale())), VisibleLabel(label));
    ImGui::Dummy(ImVec2(w, line_h + S(4.0f)));

    ImGui::PushStyleColor(ImGuiCol_FrameBg,        Fade(p.field, p.SurfaceAlphaScale()));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Fade(p.field_hover, p.SurfaceAlphaScale()));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  Fade(p.field_active, p.SurfaceAlphaScale()));
    ImGui::PushStyleColor(ImGuiCol_Border,         ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_PopupBg,        Fade(p.popup, 0.995f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(S(10.0f), S(6.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, p.corner_sm);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, p.corner);
    ImGui::SetNextItemWidth(w);

    const char* preview = (*current >= 0 && *current < count) ? items[*current] : "";
    const bool open = ImGui::BeginCombo("##cb", preview);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(5);

    const bool hov = ImGui::IsItemHovered();
    const ImGuiID iid = ImGui::GetItemID();
    const ImVec2 a = ImGui::GetItemRectMin();
    const ImVec2 b = ImGui::GetItemRectMax();
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float ht = anim::Step(iid, (hov || open) ? 1.0f : 0.0f, 16.0f);

    StrokeEdge(d, a, b, Mix(Fade(p.edge, 0.55f), Fade(p.accent, 0.9f), ht), p.corner_sm);
    StrokeSheen(d, a, b, Fade(p.sheen, 0.45f), p.corner_sm);
    icon::Chevron(d, ImVec2(b.x - S(14.0f), (a.y + b.y) * 0.5f), S(13.0f),
                  U32(Fade(Mix(p.text_faint, p.accent, ht), p.TextAlphaScale())),
                  open ? icon::Up : icon::Down, S(1.6f));

    if (open) {
        for (int i = 0; i < count; ++i) {
            const bool sel = (i == *current);
            if (sel) ImGui::PushStyleColor(ImGuiCol_Text, p.accent);
            if (ImGui::Selectable(items[i], sel)) { *current = i; changed = true; }
            if (sel) { ImGui::PopStyleColor(); ImGui::SetItemDefaultFocus(); }
        }
        ImGui::EndCombo();
    }

    ImGui::PopID();
    return changed;
}

// =============================================================================
//  Tab bar
// =============================================================================
namespace {

bool TabBarImpl(const char* id, const char* const* labels, const IconDrawFn* icons,
                int count, int* selected)
{
    if (count <= 0) return false;
    const Palette& p = ActivePalette();
    const float ta = p.TextAlphaScale();
    const bool has_icons = icons != nullptr;

    const float pad_x   = S(15.0f);
    const float icon_w  = has_icons ? S(20.0f) : 0.0f;
    const float bar_h   = S(has_icons ? 36.0f : 34.0f);
    const float avail   = ImGui::GetContentRegionAvail().x;

    // measure
    float widths[32];
    IM_ASSERT(count <= 32);
    float total = 0.0f;
    for (int i = 0; i < count; ++i) {
        float tw = TextWidth(nullptr, VisibleLabel(labels[i]));
        widths[i] = tw + pad_x * 2.0f + icon_w;
        total += widths[i];
    }
    const float scale = (total > avail && avail > 0.0f) ? (avail / total) : 1.0f;
    if (scale < 1.0f) { for (int i = 0; i < count; ++i) widths[i] *= scale; total = avail; }

    const ImVec2 bar_a = ImGui::GetCursorScreenPos();

    ImGui::PushID(id);

    // invisible native buttons in a natural horizontal row
    struct Slot { ImVec2 a, b; bool hov, foc, pressed; ImGuiID id; };
    Slot slots[32];
    for (int i = 0; i < count; ++i) {
        if (i) ImGui::SameLine(0.0f, 0.0f);
        ImGui::PushID(i);
        BeginInvisible();
        // Use Button's own return value for the click: sampling IsItemActive()
        // afterwards is always false on the release frame, because
        // ButtonBehavior() calls ClearActiveID() while processing the release -
        // which made tabs impossible to switch by clicking.
        const bool pressed = ImGui::Button("##tab", ImVec2(widths[i], bar_h));
        EndInvisible();
        slots[i].a   = ImGui::GetItemRectMin();
        slots[i].b   = ImGui::GetItemRectMax();
        slots[i].hov = ImGui::IsItemHovered();
        slots[i].foc = ImGui::IsItemFocused();
        slots[i].id  = ImGui::GetItemID();
        slots[i].pressed = pressed;
        ImGui::PopID();
    }

    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec2 bar_b(bar_a.x + total, bar_a.y + bar_h);
    const float r = p.corner;

    // strip background
    GradientFillRounded(d, bar_a, bar_b, Fade(Shade(p.panel, 0.05f), 0.85f * p.SurfaceAlphaScale()),
                        Fade(Shade(p.panel, -0.14f), 0.85f * p.SurfaceAlphaScale()), r);
    StrokeEdge(d, bar_a, bar_b, Fade(p.edge, 0.45f), r);

    // sliding indicator
    const int sel = IClamp(*selected, 0, count - 1);   // int clamp (FClamp would narrow)
    int hov_idx = -1;
    for (int i = 0; i < count; ++i) if (slots[i].hov) hov_idx = i;

    char keyx[128];
    snprintf(keyx, sizeof(keyx), "obs_tab_%s_x", id);
    char keyw[128];
    snprintf(keyw, sizeof(keyw), "obs_tab_%s_w", id);

    const float target_x = slots[sel].a.x + S(3.0f);
    const float target_w = widths[sel] - S(6.0f);
    const float ix = anim::StepGlobal(keyx, target_x, 15.0f);
    const float iw = anim::StepGlobal(keyw, target_w, 15.0f);

    const ImVec2 ia(ix, bar_a.y + S(3.0f));
    const ImVec2 ib(ix + iw, bar_b.y - S(3.0f));
    const float ir = p.corner_sm;

    GradientFillRounded(d, ia, ib, Fade(p.accent, 0.20f), Fade(p.accent_2, 0.09f), ir);
    StrokeEdge(d, ia, ib, Fade(p.accent, 0.42f), ir);
    d->AddShadowRect(ia, ib, U32(Fade(p.accent, 0.38f)), S(13.0f), ImVec2(0, S(2.0f)),
                     ImDrawFlags_None, ir);
    // bright underline
    GradientFillRoundedH(d, ImVec2(ia.x + ir, ib.y - S(2.0f)), ImVec2(ib.x - ir, ib.y),
                         Fade(p.accent, 0.15f), Fade(p.accent_2, 0.95f), S(1.0f), 8);

    // per-tab hover wash + labels
    const float line_h = ImGui::GetTextLineHeight();
    for (int i = 0; i < count; ++i) {
        const Slot& s = slots[i];
        const bool is_sel = (i == sel);
        const float ht = anim::Step(s.id, s.hov ? 1.0f : 0.0f, 16.0f);

        if (!is_sel && ht > 0.01f) {
            GradientFillRounded(d, ImVec2(s.a.x + S(3.0f), s.a.y + S(3.0f)),
                                ImVec2(s.b.x - S(3.0f), s.b.y - S(3.0f)),
                                Fade(p.panel_hover, 0.60f * ht * p.SurfaceAlphaScale()),
                                Fade(p.panel, 0.25f * ht * p.SurfaceAlphaScale()), ir);
        }

        ImVec4 tcol = is_sel ? Mix(p.text, p.accent, 0.30f)
                             : Mix(p.text_faint, p.text_dim, ht);
        float content_w = TextWidth(nullptr, VisibleLabel(labels[i]));
        float cx = s.a.x + (widths[i] - content_w - icon_w) * 0.5f;

        if (has_icons && icons[i]) {
            icons[i](d, ImVec2(cx + icon_w * 0.5f, (s.a.y + s.b.y) * 0.5f), icon_w * 0.72f,
                     U32(Fade(is_sel ? p.accent : Mix(p.text_faint, p.text_dim, ht), ta)));
            cx += icon_w;
        }
        d->AddText(ImVec2(cx, s.a.y + (bar_h - line_h) * 0.5f), U32(Fade(tcol, ta)), VisibleLabel(labels[i]));

        if (s.foc) DrawFocusRing(d, s.a, s.b, ir, p);
    }

    bool changed = false;
    for (int i = 0; i < count; ++i)
        if (slots[i].pressed && i != *selected) {
            *selected = i; changed = true;
        }

    ImGui::PopID();
    return changed;
}

} // namespace

bool TabBar(const char* id, const char* const* labels, int count, int* selected)
{
    return TabBarImpl(id, labels, nullptr, count, selected);
}

bool TabBarIcons(const char* id, const char* const* labels, const IconDrawFn* icons,
                 int count, int* selected)
{
    return TabBarImpl(id, labels, icons, count, selected);
}

// =============================================================================
//  Progress
// =============================================================================
void ProgressBar(const char* label, float frac, const ImVec4* tint, const char* value_text)
{
    const Palette& p = ActivePalette();
    const Metrics& m = M();
    const float w = ImGui::GetContentRegionAvail().x;
    const float line_h = ImGui::GetTextLineHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* d = ImGui::GetWindowDrawList();

    frac = frac < 0.0f ? 0.0f : (frac > 1.0f ? 1.0f : frac);
    const ImVec4 c1 = tint ? *tint : p.accent;
    const ImVec4 c2 = tint ? Shade(*tint, -0.20f) : p.accent_2;

    if (label && *label)
        d->AddText(pos, U32(Fade(p.text_dim, p.TextAlphaScale())), VisibleLabel(label));
    if (value_text && *value_text) {
        const float vw = TextWidth(FontMono(), value_text);
        ImFont* mono = FontMono();
        const ImVec2 vp(pos.x + w - vw, pos.y);
        if (mono) d->AddText(mono, mono->FontSize, vp, U32(Fade(p.text, p.TextAlphaScale())), value_text);
        else      d->AddText(vp, U32(Fade(p.text, p.TextAlphaScale())), value_text);
    }

    const float ty = pos.y + line_h + S(6.0f);
    const ImVec2 ra(pos.x, ty), rb(pos.x + w, ty + m.track_h);
    const float rr = m.track_h * 0.5f;
    GradientFillRounded(d, ra, rb, Shade(p.field, -0.12f), Shade(p.field, 0.06f), rr);
    StrokeEdge(d, ra, rb, Fade(p.edge, 0.40f), rr);
    if (frac > 0.002f) {
        const ImVec2 fb(ra.x + w * frac, rb.y);
        GradientFillRoundedH(d, ra, fb, Fade(c1, 0.95f), Fade(c2, 0.95f), rr);
        StrokeSheen(d, ra, fb, Fade(ImVec4(1, 1, 1, 1), 0.18f), rr);
        d->AddShadowRect(ra, fb, U32(Fade(c1, 0.40f)), S(8.0f), ImVec2(0, 0), ImDrawFlags_None, rr);
    }

    ImGui::Dummy(ImVec2(w, line_h + S(6.0f) + m.track_h));
}

// =============================================================================
//  Help marker
// =============================================================================
void HelpMarker(const char* text)
{
    const Palette& p = ActivePalette();
    const float d_sz = ImGui::GetTextLineHeight() * 0.85f;
    const ImVec2 a = ImGui::GetCursorScreenPos();

    ImGui::PushID("##help");
    BeginInvisible();
    ImGui::Button("##h", ImVec2(d_sz, d_sz));
    EndInvisible();
    const bool hov = ImGui::IsItemHovered();
    const ImGuiID iid = ImGui::GetItemID();
    ImGui::PopID();

    ImDrawList* d = ImGui::GetWindowDrawList();
    const float ht = anim::Step(iid, hov ? 1.0f : 0.0f, 16.0f);
    const ImVec2 c(a.x + d_sz * 0.5f, a.y + d_sz * 0.5f);
    d->AddCircle(c, d_sz * 0.48f, U32(Fade(Mix(p.text_faint, p.accent, ht), p.TextAlphaScale())),
                 18, S(1.1f));
    const char* q = "?";
    const float qw = TextWidth(nullptr, q);
    d->AddText(ImVec2(c.x - qw * 0.5f, a.y - S(0.5f)),
               U32(Fade(Mix(p.text_faint, p.accent, ht), p.TextAlphaScale())), q);

    if (hov) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(S(280.0f));
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::SameLine(0.0f, S(5.0f));
}

// =============================================================================
//  Ambient backdrop
// =============================================================================
void DrawAmbientBackdrop(float intensity)
{
    const Palette& p = ActivePalette();
    ImDrawList* d = ImGui::GetBackgroundDrawList();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const ImVec2 o(0.0f, 0.0f);

    // Deep base gradient as ONE multi-colour quad: AddRectFilledMultiColor is
    // interpolated per pixel, while the banded helpers would show flat slices
    // on a near-black background.
    const ImVec4 gtop = Shade(p.backdrop, 0.22f);
    const ImVec4 gbot = Shade(p.backdrop, -0.20f);
    d->AddRectFilledMultiColor(o, ds, U32(gtop), U32(gtop), U32(gbot), U32(gbot));

    // two large soft accent blooms (thin concentric rings approximate a gaussian)
    const ImVec2 centres[2] = { ImVec2(ds.x * 0.22f, ds.y * 0.26f), ImVec2(ds.x * 0.80f, ds.y * 0.74f) };
    const ImVec4 tints[2]   = { p.accent, p.accent_2 };
    const float radii[2]    = { FMax(ds.x, ds.y) * 0.42f, FMax(ds.x, ds.y) * 0.34f };;
    for (int b = 0; b < 2; ++b) {
        const int rings = 96;
        for (int i = rings; i >= 1; --i) {
            const float t = (float)i / (float)rings;
            const float a = 0.085f * intensity * (1.0f - t) * (1.0f - t);
            d->AddCircleFilled(centres[b], radii[b] * t, U32(Fade(tints[b], a)), 64);
        }
    }

    // faint engineering grid
    const float grid = S(46.0f);
    const ImVec4 gl = Fade(Shade(p.edge, 0.55f), 0.16f * intensity);
    for (float x = 0.0f; x < ds.x; x += grid) d->AddLine(ImVec2(x, 0), ImVec2(x, ds.y), U32(gl), 1.0f);
    for (float y = 0.0f; y < ds.y; y += grid) d->AddLine(ImVec2(0, y), ImVec2(ds.x, y), U32(gl), 1.0f);

    // vignette: four smooth quads, each spanning exactly half its axis so
    // opposite edges meet where both alphas are ~0 (no seam, no double darkening)
    const ImU32 k0 = U32(ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    const ImU32 kt = U32(ImVec4(0.0f, 0.0f, 0.0f, 0.55f * intensity));
    const ImU32 kb = U32(ImVec4(0.0f, 0.0f, 0.0f, 0.62f * intensity));
    const ImU32 ks = U32(ImVec4(0.0f, 0.0f, 0.0f, 0.40f * intensity));
    const float hx = ds.x * 0.5f, hy = ds.y * 0.5f;
    d->AddRectFilledMultiColor(ImVec2(0.0f, 0.0f), ImVec2(ds.x, hy), kt, kt, k0, k0);
    d->AddRectFilledMultiColor(ImVec2(0.0f, hy), ds, k0, k0, kb, kb);
    d->AddRectFilledMultiColor(ImVec2(0.0f, 0.0f), ImVec2(hx, ds.y), ks, k0, k0, ks);
    d->AddRectFilledMultiColor(ImVec2(hx, 0.0f), ds, k0, ks, ks, k0);
}

} // namespace obsidian
