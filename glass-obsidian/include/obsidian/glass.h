// -----------------------------------------------------------------------------
//  obsidian/glass.h  --  the Glass Obsidian widget & decoration layer
// -----------------------------------------------------------------------------
//  Everything here is built on ImGui's PUBLIC API only (no imgui_internal.h),
//  so it drops into any 1.90+ project without patching ImGui.
//
//  Two techniques do most of the work:
//
//  1. Clip-band gradients. ImDrawList has no rounded-rect gradient fill
//     (AddRectFilledMultiColor ignores rounding), so GradientFillRounded() draws
//     N *non-overlapping* horizontal clip bands, each filling the full rounded
//     silhouette with one gradient stop. Non-overlapping matters: equal-alpha
//     layers that overlap would compound to a higher alpha and produce visible
//     seams. Cost is ~14 quads per surface, which is nothing.
//
//  2. Invisible-native + overlay. Interactive controls (slider, toggle, tab)
//     use a real ImGui Button/Checkbox with every colour pushed to transparent,
//     then paint custom glass geometry over the item rect. That keeps native
//     hit-testing, drag capture, keyboard/gamepad nav, focus ring and
//     IsItemDeactivated* semantics for free, instead of re-deriving them from
//     InvisibleButton and losing accessibility.
// -----------------------------------------------------------------------------
#pragma once

#include "obsidian/palette.h"
#include "obsidian/detail.h"

#include <climits>   // INT_MIN (SliderInt default)
#include <cmath>     // NAN     (Slider default_value)

namespace obsidian {

// =============================================================================
//  Animation
// =============================================================================
namespace anim {

// Frame-rate independent exponential approach towards `target`, persisted per
// ImGui ID in the current window's storage. Returns the animated value.
// `speed` is roughly "1/e settling time" in 1/speed seconds (14 => ~70ms feel).
float Step(ImGuiID id, float target, float speed = 14.0f);
float Step(const char* seed, float target, float speed = 14.0f);

// A global (per-context) animated value, for things like the tab indicator that
// live outside any single widget's ID.
float StepGlobal(const char* seed, float target, float speed = 14.0f);

float Ease(float t);      // smoothstep, clamped
float EaseOut(float t);   // 1-(1-t)^3, clamped
float Pulse(float hz);    // 0..1 sine pulse driven by io.Time -- status LEDs
} // namespace anim

// =============================================================================
//  Drawing primitives (take any ImDrawList; used by widgets and by your own UI)
// =============================================================================

// N-band vertical gradient inside a rounded silhouette. See header note.
void GradientFillRounded(ImDrawList* d, const ImVec2& a, const ImVec2& b,
                         const ImVec4& top, const ImVec4& bottom,
                         float rounding, int bands = 14);

// Horizontal variant (accent bars, progress fills).
void GradientFillRoundedH(ImDrawList* d, const ImVec2& a, const ImVec2& b,
                          const ImVec4& left, const ImVec4& right,
                          float rounding, int bands = 14);

// Drop shadow. By default it is clipped to the enclosing region (a panel's
// glow must stay inside its panel -- see the note in glass.cpp). Pass
// spill_outside_window = true ONLY for a top-level window's shadow, which is
// supposed to fall on whatever is behind the window.
void SoftShadow(ImDrawList* d, const ImVec2& a, const ImVec2& b,
                const ImVec4& col, float rounding, float thickness = 26.0f,
                const ImVec2& offset = ImVec2(0.0f, 10.0f),
                bool spill_outside_window = false);

// Specular highlight hugging the top edge, fading downwards. This is the single
// most important detail for the "glass" read: it implies light from above.
void StrokeSheen(ImDrawList* d, const ImVec2& a, const ImVec2& b,
                 const ImVec4& col, float rounding);

// Hairline silhouette. `inner` insets by 0.5px so 1px strokes land on pixels.
void StrokeEdge(ImDrawList* d, const ImVec2& a, const ImVec2& b,
                const ImVec4& col, float rounding, float thickness = 1.0f,
                bool inner = true);

// A soft accent bloom bleeding in from an edge (used for active tabs/focus).
void EdgeGlow(ImDrawList* d, const ImVec2& a, const ImVec2& b,
              const ImVec4& col, float rounding, int edge /*0=top,1=bottom,2=left,3=all*/);

void Hairline(ImDrawList* d, const ImVec2& a, const ImVec2& b, const ImVec4& col);

// Accent underline that fades out to both sides (section dividers).
void AccentRule(ImDrawList* d, const ImVec2& a, float width, const ImVec4& col,
                float thickness = 2.0f);

// Vector icons -- drawn as paths so the theme needs no icon font.
// Every icon here has EXACTLY the IconDrawFn signature (ImDrawList*, centre,
// size, colour) and derives its own stroke weight from `size`, so any of them
// can be passed straight to IconButton()/TabBarIcons(). A function with a
// defaulted extra parameter would NOT be convertible -- default arguments are
// not part of a function's type.
namespace icon {
enum Dir { Up = 0, Right = 1, Down = 2, Left = 3 };

// General chevron with explicit direction and stroke weight (not an IconDrawFn).
void Chevron(ImDrawList* d, ImVec2 c, float size, ImU32 col, Dir dir, float thick);
// IconDrawFn-compatible variants:
void ChevronUp(ImDrawList* d, ImVec2 c, float size, ImU32 col);
void ChevronDown(ImDrawList* d, ImVec2 c, float size, ImU32 col);
void ChevronLeft(ImDrawList* d, ImVec2 c, float size, ImU32 col);
void ChevronRight(ImDrawList* d, ImVec2 c, float size, ImU32 col);

void Check(ImDrawList* d, ImVec2 c, float size, ImU32 col);
void Close(ImDrawList* d, ImVec2 c, float size, ImU32 col);
void Minus(ImDrawList* d, ImVec2 c, float size, ImU32 col);
void Plus(ImDrawList* d, ImVec2 c, float size, ImU32 col);
void Search(ImDrawList* d, ImVec2 c, float size, ImU32 col);
void Gear(ImDrawList* d, ImVec2 c, float size, ImU32 col);
void Power(ImDrawList* d, ImVec2 c, float size, ImU32 col);
void Shield(ImDrawList* d, ImVec2 c, float size, ImU32 col);
void Eye(ImDrawList* d, ImVec2 c, float size, ImU32 col);
void Sliders(ImDrawList* d, ImVec2 c, float size, ImU32 col);
void Crosshair(ImDrawList* d, ImVec2 c, float size, ImU32 col);
void Palette(ImDrawList* d, ImVec2 c, float size, ImU32 col);
void Keyboard(ImDrawList* d, ImVec2 c, float size, ImU32 col);
} // namespace icon

// =============================================================================
//  Layout metrics
// =============================================================================
struct Metrics
{
    float pad_panel = 12.0f;   // padding inside a glass panel
    float pad_row   =  9.0f;   // vertical gap between rows
    float track_h   =  8.0f;   // slider / progress bar thickness
    float control_h = 26.0f;   // hit area height of a control row
    float knob_d    = 15.0f;   // slider knob diameter
    float toggle_w  = 38.0f;
    float toggle_h  = 20.0f;
};
const Metrics& M();            // live metrics (already ui_scale'd)

// =============================================================================
//  Surfaces
// =============================================================================
enum PanelFlags_
{
    PanelFlags_None        = 0,
    PanelFlags_Inset       = 1 << 0,  // darker, recessed (nested groups)
    PanelFlags_Elevated    = 1 << 1,  // lighter + stronger shadow (cards)
    PanelFlags_NoBorder    = 1 << 2,
    PanelFlags_NoSheen     = 1 << 3,
    PanelFlags_AutoResizeY = 1 << 4,
    PanelFlags_AccentTop   = 1 << 5,  // accent hairline across the top edge
};
typedef int PanelFlags;

// Frosted glass panel. Width:  x <= 0 fills the available width.
// Height:  y > 0 fixed; y == 0 fits the content; y < 0 fills the remaining
// height of the parent region.
//
// IMPORTANT: PanelEnd() must be called even when PanelBegin() returns false --
// exactly like ImGui::Begin()/End(). Forgetting it leaves ImGui inside a child
// window and trips "Must call EndChild() and not End()!". Prefer ScopedPanel
// below, which makes that impossible.
bool PanelBegin(const char* id, const ImVec2& size = ImVec2(0, 0),
                PanelFlags flags = PanelFlags_None);
void PanelEnd();

// RAII panel. C++17 if-init keeps it to one line:
//
//     if (obsidian::ScopedPanel p("settings", ImVec2(0, 0), PanelFlags_Inset); p)
//     {
//         obsidian::Toggle("Enabled", &enabled);
//     }
//
// The guard always closes the child, whether or not it opened.
struct ScopedPanel
{
    explicit ScopedPanel(const char* id, const ImVec2& size = ImVec2(0, 0),
                         PanelFlags flags = PanelFlags_None);
    ~ScopedPanel();
    ScopedPanel(const ScopedPanel&) = delete;
    ScopedPanel& operator=(const ScopedPanel&) = delete;

    bool         Open() const { return m_open; }
    explicit operator bool() const { return m_open; }

private:
    bool m_open;
};

// Small dim uppercase label with a fading accent rule -- group divider.
void SectionHeader(const char* label, bool accent_rule = true);

// label ............ value  (value right-aligned, dim->bright)
void KeyValue(const char* key, const char* value, const ImVec4* value_col = nullptr);

// Rounded status chip: pulsing dot + label.
void Badge(const char* label, const ImVec4& tint, bool pulse = false);

void Spacer(float h);
void VSeparator(float indent = 0.0f);

// =============================================================================
//  Controls
// =============================================================================
enum ButtonKind_
{
    ButtonKind_Default = 0,   // glass, subtle
    ButtonKind_Primary,       // accent gradient fill
    ButtonKind_Danger,        // danger gradient fill
    ButtonKind_Ghost,         // borderless, text only until hovered
};
typedef int ButtonKind;

// size.x <= 0 => fit label + padding;  size.y <= 0 => ControlHeight().
bool Button(const char* label, const ImVec2& size = ImVec2(0, 0),
            ButtonKind kind = ButtonKind_Default);

// Square icon button. `draw` is one of the icon::* functions.
typedef void (*IconDrawFn)(ImDrawList*, ImVec2, float, ImU32);
bool IconButton(const char* id, IconDrawFn draw, const ImVec2& size = ImVec2(0, 0),
                ButtonKind kind = ButtonKind_Ghost, const char* tooltip = nullptr);

// Pill switch + label + optional dim hint text on the right.
bool Toggle(const char* label, bool* v, const char* hint = nullptr);
// Toggle with no label (for dense rows / table cells).
bool ToggleOnly(const char* id, bool* v);

struct SliderFlags_
{
    bool  show_label  = true;
    bool  show_value  = true;
    bool  integer     = false;
    float power       = 1.0f;   // != 1 => exponential curve (e.g. 2.0 for FOV-ish)
};
struct SliderOpts : SliderFlags_ {};

// Glass slider: gradient fill, glowing knob, value bubble while dragging.
// CTRL+click resets to `default_value` (when not NaN). Returns true on change.
bool Slider(const char* label, float* v, float vmin, float vmax,
            const char* fmt = "%.2f", float step = 0.0f,
            float default_value = NAN, const SliderOpts& opts = SliderOpts());

bool SliderInt(const char* label, int* v, int vmin, int vmax,
               const char* fmt = "%d", int default_value = INT_MIN);

// Text field with animated focus ring + optional placeholder.
bool TextInput(const char* label, char* buf, size_t buf_size,
               const char* placeholder = nullptr);

// Combo with vector chevron and glass popup.
bool Combo(const char* label, int* current, const char* const* items, int count);

// Animated tab bar with a sliding indicator. Returns true when selection changed.
bool TabBar(const char* id, const char* const* labels, int count, int* selected);
// Icon + label tab bar (icons may be null entries).
bool TabBarIcons(const char* id, const char* const* labels, const IconDrawFn* icons,
                 int count, int* selected);

// Progress / meter bar.
void ProgressBar(const char* label, float frac, const ImVec4* tint = nullptr,
                 const char* value_text = nullptr);

// Help marker: dim "?" that reveals `text` on hover.
void HelpMarker(const char* text);

// Text metrics helper used by the widgets (f == nullptr => current font).
float TextWidth(ImFont* f, const char* text);

// Strips ImGui's "##id" / "###id" suffix, returning "" when the label is purely
// an ID. Every widget runs its label through this before drawing, so passing
// "##foo" hides the text but keeps the ID unique.
const char* VisibleLabel(const char* label);

// =============================================================================
//  Ambient backdrop
// =============================================================================
// Paints a full-screen atmospheric background (deep gradient + soft accent
// blooms + faint grid). In a real overlay you would skip this and let the game
// show through; it exists so the glass has something to sit on in demos and in
// windowed (non-overlay) mode.
void DrawAmbientBackdrop(float intensity = 1.0f);

// Optional real backdrop blur: if your renderer can hand ImGui a texture
// holding a blurred copy of what is behind the window, panels will sample it
// and you get genuine frosted glass instead of the translucency approximation.
// Pass 0/NULL to keep the approximation. UV rect is in the texture's space.
void SetBackdropBlurTexture(ImTextureID tex, const ImVec4& uv_rect = ImVec4(0, 0, 1, 1));
ImTextureID GetBackdropBlurTexture();

} // namespace obsidian
