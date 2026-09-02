// -----------------------------------------------------------------------------
//  obsidian/theme.h  --  ImGui style + font setup for the Glass Obsidian theme
// -----------------------------------------------------------------------------
//  Two entry points:
//
//      obsidian::LoadFonts(cfg);      // once, before the first frame
//      obsidian::ApplyTheme(pal);     // any time (cheap, safe per-frame)
//
//  ApplyTheme() is intentionally re-entrant: the demo calls it whenever the
//  palette or the global opacity slider changes, and it fully rebuilds the
//  ImGuiStyle from tokens so nothing drifts out of sync.
//
//  Public API only -- this header does not include imgui_internal.h.
// -----------------------------------------------------------------------------
#pragma once

#include "obsidian/detail.h"
#include "obsidian/palette.h"

namespace obsidian {

struct ThemeConfig
{
    float ui_scale        = 1.00f;  // global multiplier on padding/rounding/sizes
    float base_font_size  = 15.0f;  // body text
    float title_font_size = 19.0f;  // window titles / section headers
    float mono_font_size  = 13.0f;  // numeric read-outs

    // Explicit font files. When null, LoadFonts() walks a list of common
    // platform paths (Segoe UI on Windows, DejaVu on Linux, system fonts on
    // macOS) and falls back to ImGui's built-in ProggyClean if nothing is found.
    const char* font_regular = nullptr;
    const char* font_bold    = nullptr;
    const char* font_mono    = nullptr;

    int   oversample_h = 2;         // glyph atlas quality (1 = smaller atlas)
    bool  pixel_snap   = true;      // crisp horizontal stems at small sizes
    bool  move_from_title_bar_only = true;
    bool  nav_keyboard = false;     // arrow-key UI navigation
};

// Build the font atlas. Call once after ImGui::CreateContext() and before the
// first NewFrame(). Returns false if it had to fall back to ProggyClean (the UI
// still works, it just looks worse).
bool LoadFonts(const ThemeConfig& cfg = ThemeConfig());

// Rebuild ImGuiStyle from a palette. Safe to call every frame.
void ApplyTheme(const Palette& pal, const ThemeConfig& cfg = ThemeConfig());

// The palette ApplyTheme() last installed. Widget drawing reads its live tokens
// from here, which is how a runtime palette switch repaints custom widgets.
const Palette& ActivePalette();
void           SetActivePalette(const Palette& p);

const ThemeConfig& ActiveConfig();

// Font handles (may be null if LoadFonts() was never called -- callers must
// null-check before PushFont, or use the Scoped* helpers below which do).
ImFont* FontRegular();
ImFont* FontBold();
ImFont* FontTitle();
ImFont* FontMono();

// RAII font push that tolerates a null font (no-op instead of asserting).
struct ScopedFont
{
    ScopedFont(ImFont* f);
    ~ScopedFont();
    bool pushed;
};

// Effective sizes after ui_scale -- widgets use these so everything stays in
// proportion when the user bumps the DPI scale.
float S(float v);                       // scale a length
float TitleBarHeight();                 // recommended custom title bar height
float RowHeight();                      // standard control row height
float ControlHeight();                  // standard field/button height

} // namespace obsidian
