// -----------------------------------------------------------------------------
//  src/theme.cpp  --  ImGuiStyle + font atlas construction
// -----------------------------------------------------------------------------
#include "obsidian/theme.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <initializer_list>

namespace obsidian {
namespace {

Palette     g_palette = PaletteEmber();
ThemeConfig g_cfg;

ImFont* g_regular = nullptr;
ImFont* g_bold    = nullptr;
ImFont* g_title   = nullptr;
ImFont* g_mono    = nullptr;

bool FileExists(const char* path)
{
    if (!path || !*path) return false;
    // std::ifstream rather than fopen: MSVC's /sdl turns fopen into a C4996
    // error, and this stays fully portable across the headless builds.
    std::ifstream f(path, std::ios::binary);
    return f.good();
}

const char* FirstExisting(std::initializer_list<const char*> candidates)
{
    for (const char* c : candidates)
        if (FileExists(c)) return c;
    return nullptr;
}

} // namespace

// -----------------------------------------------------------------------------
//  Fonts
// -----------------------------------------------------------------------------
bool LoadFonts(const ThemeConfig& cfg)
{
    g_cfg = cfg;
    ImGuiIO& io = ImGui::GetIO();

    // The atlas is locked between NewFrame() and Render()/EndFrame(); rebuilding
    // it mid-frame trips an assert. Refuse and report instead, so a DPI change
    // requested from inside a frame is a no-op rather than a crash. Callers that
    // change ui_scale at runtime should do it between frames.
    if (io.Fonts->Locked)
        return false;

    // Re-entrant: wipe any previously built atlas. NOTE: if a renderer backend
    // already uploaded the atlas texture you must let it invalidate/re-create it
    // (ImGui_ImplDX11_InvalidateDeviceObjects or a backend restart).
    io.Fonts->Clear();
    g_regular = g_bold = g_title = g_mono = nullptr;

    const char* reg = cfg.font_regular ? cfg.font_regular : FirstExisting({
        "C:/Windows/Fonts/segoeui.ttf",                       // Windows 10/11
        "C:/Windows/Fonts/tahoma.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",    // Linux
        "/Library/Fonts/Arial.ttf",                           // macOS
        "/System/Library/Fonts/Supplemental/Arial.ttf",
    });
    const char* bold = cfg.font_bold ? cfg.font_bold : FirstExisting({
        "C:/Windows/Fonts/seguisb.ttf",                       // Segoe UI Semibold
        "C:/Windows/Fonts/segoeuib.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/Library/Fonts/Arial Bold.ttf",
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
    });
    const char* mono = cfg.font_mono ? cfg.font_mono : FirstExisting({
        "C:/Windows/Fonts/consola.ttf",                       // Consolas
        "C:/Windows/Fonts/cascadiamono.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/Library/Fonts/Courier New.ttf",
        "/System/Library/Fonts/Menlo.ttc",
    });

    const float s = cfg.ui_scale > 0.0f ? cfg.ui_scale : 1.0f;

    ImFontConfig fc;
    fc.OversampleH = cfg.oversample_h > 0 ? cfg.oversample_h : 2;
    fc.OversampleV = 1;
    fc.PixelSnapH  = cfg.pixel_snap;

    const ImWchar* ranges = io.Fonts->GetGlyphRangesDefault();

    if (reg)
        g_regular = io.Fonts->AddFontFromFileTTF(reg, cfg.base_font_size * s, &fc, ranges);
    if (bold) {
        g_bold  = io.Fonts->AddFontFromFileTTF(bold, cfg.base_font_size * s, &fc, ranges);
        g_title = io.Fonts->AddFontFromFileTTF(bold, cfg.title_font_size * s, &fc, ranges);
    }
    if (mono)
        g_mono = io.Fonts->AddFontFromFileTTF(mono, cfg.mono_font_size * s, &fc, ranges);

    if (!g_regular)
        g_regular = io.Fonts->AddFontDefault(&fc);   // last-resort ProggyClean

    io.Fonts->Build();
    io.FontGlobalScale = 1.0f;
    return g_regular != nullptr && (bold != nullptr);
}

ImFont* FontRegular() { return g_regular ? g_regular : ImGui::GetIO().Fonts->Fonts.Size ? ImGui::GetIO().Fonts->Fonts[0] : nullptr; }
ImFont* FontBold()    { return g_bold ? g_bold : FontRegular(); }
ImFont* FontTitle()   { return g_title ? g_title : FontBold(); }
ImFont* FontMono()    { return g_mono ? g_mono : FontRegular(); }

ScopedFont::ScopedFont(ImFont* f) : pushed(false)
{
    if (f && f->IsLoaded()) { ImGui::PushFont(f); pushed = true; }
}
ScopedFont::~ScopedFont() { if (pushed) ImGui::PopFont(); }

// -----------------------------------------------------------------------------
//  Metrics
// -----------------------------------------------------------------------------
float S(float v) { return v * (g_cfg.ui_scale > 0.0f ? g_cfg.ui_scale : 1.0f); }

float TitleBarHeight() { return FMax(S(44.0f), g_cfg.title_font_size * (g_cfg.ui_scale > 0 ? g_cfg.ui_scale : 1.0f) + S(20.0f)); }
float RowHeight()      { return S(26.0f); }
float ControlHeight()  { return S(28.0f); }

// -----------------------------------------------------------------------------
//  Palette accessors
// -----------------------------------------------------------------------------
const Palette& ActivePalette()        { return g_palette; }
void           SetActivePalette(const Palette& p) { g_palette = p; }
const ThemeConfig& ActiveConfig()     { return g_cfg; }

// -----------------------------------------------------------------------------
//  Style
// -----------------------------------------------------------------------------
void ApplyTheme(const Palette& pal, const ThemeConfig& cfg)
{
    g_palette = pal;
    g_cfg     = cfg;

    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();   // full reset so repeated ApplyTheme() calls never drift

    const float sa = pal.SurfaceAlphaScale();
    const float ta = pal.TextAlphaScale();
    const float ea = pal.EdgeAlphaScale();

    const ImVec4 accent     = pal.accent;
    const ImVec4 accent_hi  = Shade(accent, 0.18f);
    const ImVec4 accent_lo  = Mix(accent, pal.accent_2, 0.55f);

    // ---- text ---------------------------------------------------------------
    st.Colors[ImGuiCol_Text]                  = Fade(pal.text,      ta);
    st.Colors[ImGuiCol_TextDisabled]          = Fade(pal.text_faint, ta);

    // ---- surfaces -----------------------------------------------------------
    // WindowBg stays meaningful for *stock* ImGui windows. ObsidianWindow pushes
    // its own transparent WindowBg and paints the glass itself.
    st.Colors[ImGuiCol_WindowBg]              = Fade(pal.window_bottom, sa);
    st.Colors[ImGuiCol_ChildBg]               = Fade(pal.panel,         0.35f * sa);
    st.Colors[ImGuiCol_PopupBg]               = Fade(pal.popup,         sa);
    st.Colors[ImGuiCol_MenuBarBg]             = Fade(pal.header,        sa);
    st.Colors[ImGuiCol_TitleBg]               = Fade(pal.header,        sa);
    st.Colors[ImGuiCol_TitleBgActive]         = Fade(Shade(pal.header, 0.04f), sa);
    st.Colors[ImGuiCol_TitleBgCollapsed]      = Fade(Shade(pal.header, -0.25f), sa);
    st.Colors[ImGuiCol_Border]                = Fade(pal.edge,  0.55f * ea);
    st.Colors[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);
    st.Colors[ImGuiCol_FrameBg]               = Fade(pal.field,        sa);
    st.Colors[ImGuiCol_FrameBgHovered]        = Fade(pal.field_hover,  sa);
    st.Colors[ImGuiCol_FrameBgActive]         = Fade(pal.field_active, sa);

    // ---- controls -----------------------------------------------------------
    st.Colors[ImGuiCol_Button]                = Fade(pal.panel,       0.85f * sa);
    st.Colors[ImGuiCol_ButtonHovered]         = Fade(accent,          0.18f * sa);
    st.Colors[ImGuiCol_ButtonActive]          = Fade(accent,          0.32f * sa);
    st.Colors[ImGuiCol_CheckMark]             = accent;
    st.Colors[ImGuiCol_SliderGrab]            = accent_hi;
    st.Colors[ImGuiCol_SliderGrabActive]      = accent;
    st.Colors[ImGuiCol_Header]                = Fade(accent, 0.16f * sa);
    st.Colors[ImGuiCol_HeaderHovered]         = Fade(accent, 0.24f * sa);
    st.Colors[ImGuiCol_HeaderActive]          = Fade(accent, 0.34f * sa);
    st.Colors[ImGuiCol_ResizeGrip]            = Fade(pal.edge,   0.30f * ea);
    st.Colors[ImGuiCol_ResizeGripHovered]     = Fade(accent,     0.55f);
    st.Colors[ImGuiCol_ResizeGripActive]      = Fade(accent,     0.90f);
    st.Colors[ImGuiCol_Separator]             = Fade(pal.edge_soft, 0.90f * ea);
    st.Colors[ImGuiCol_SeparatorHovered]      = Fade(accent, 0.60f);
    st.Colors[ImGuiCol_SeparatorActive]       = Fade(accent, 0.90f);

    // ---- tabs (stock tabs: used only if the app opts out of obsidian::TabBar)
    st.Colors[ImGuiCol_Tab]                   = Fade(pal.panel,   0.70f * sa);
    st.Colors[ImGuiCol_TabHovered]            = Fade(accent,      0.40f);
    st.Colors[ImGuiCol_TabActive]             = Fade(accent,      0.26f);
    st.Colors[ImGuiCol_TabUnfocused]          = Fade(pal.panel,   0.45f * sa);
    st.Colors[ImGuiCol_TabUnfocusedActive]    = Fade(accent,      0.14f);

    // ---- scrollbars: thin, borderless, accent on grab ------------------------
    st.Colors[ImGuiCol_ScrollbarBg]           = ImVec4(0, 0, 0, 0.10f * sa);
    st.Colors[ImGuiCol_ScrollbarGrab]         = Fade(pal.edge, 0.80f * ea);
    st.Colors[ImGuiCol_ScrollbarGrabHovered]  = Fade(accent_lo, 0.75f);
    st.Colors[ImGuiCol_ScrollbarGrabActive]   = Fade(accent,    0.95f);

    // ---- misc ---------------------------------------------------------------
    st.Colors[ImGuiCol_TextSelectedBg]        = Fade(accent, 0.35f);
    st.Colors[ImGuiCol_DragDropTarget]        = Fade(accent, 0.85f);
    st.Colors[ImGuiCol_NavHighlight]          = Fade(accent, 0.80f);
    st.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1, 1, 1, 0.55f);
    st.Colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.6f, 0.7f, 0.9f, 0.20f);
    st.Colors[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.02f, 0.03f, 0.05f, 0.62f);
    st.Colors[ImGuiCol_PlotLines]             = Fade(accent_lo, 0.90f);
    st.Colors[ImGuiCol_PlotLinesHovered]      = Fade(pal.danger, 0.90f);
    st.Colors[ImGuiCol_PlotHistogram]         = Fade(accent, 0.80f);
    st.Colors[ImGuiCol_PlotHistogramHovered]  = Fade(accent_hi, 1.00f);

    // ---- geometry (authored at scale 1.0, then scaled in one place) ----------
    st.WindowPadding     = ImVec2(10.0f, 10.0f);
    st.FramePadding      = ImVec2(10.0f,  5.0f);
    st.CellPadding       = ImVec2( 8.0f,  5.0f);
    st.ItemSpacing       = ImVec2( 9.0f,  8.0f);
    st.ItemInnerSpacing  = ImVec2( 7.0f,  4.0f);
    st.TouchExtraPadding = ImVec2( 0.0f,  0.0f);
    st.IndentSpacing     = 14.0f;
    st.ColumnsMinSpacing = 8.0f;
    st.ScrollbarSize     = 9.0f;
    st.GrabMinSize       = 9.0f;
    st.LogSliderDeadzone = 4.0f;
    st.SeparatorTextPadding      = ImVec2(16.0f, 3.0f);
    st.DisplayWindowPadding      = ImVec2(16.0f, 16.0f);
    st.DisplaySafeAreaPadding    = ImVec2( 3.0f,  3.0f);
    st.MouseCursorScale          = 1.0f;

    st.WindowRounding    = pal.corner_lg;
    st.ChildRounding     = pal.corner;
    st.FrameRounding     = pal.corner_sm;
    st.PopupRounding     = pal.corner_lg;
    st.ScrollbarRounding = 999.0f;
    st.GrabRounding      = pal.corner_sm;
    st.TabRounding       = pal.corner_sm;

    st.WindowBorderSize  = 1.0f;
    st.ChildBorderSize   = 0.0f;   // panels paint their own hairline
    st.PopupBorderSize   = 0.0f;   // popups paint their own hairline
    st.FrameBorderSize   = 0.0f;
    st.TabBorderSize     = 0.0f;
    st.TabBarBorderSize  = 0.0f;

    st.WindowTitleAlign      = ImVec2(0.0f, 0.5f);
    st.WindowMenuButtonPosition = ImGuiDir_Right;
    st.ColorButtonPosition   = ImGuiDir_Right;
    st.ButtonTextAlign       = ImVec2(0.5f, 0.5f);
    st.SelectableTextAlign   = ImVec2(0.0f, 0.5f);

    if (cfg.ui_scale != 1.0f && cfg.ui_scale > 0.0f)
        st.ScaleAllSizes(cfg.ui_scale);

    // ---- io config ----------------------------------------------------------
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigWindowsMoveFromTitleBarOnly = cfg.move_from_title_bar_only;
    io.ConfigInputTextCursorBlink        = true;
    io.ConfigInputTextEnterKeepActive    = false;
    io.ConfigDragClickToInputText        = true;
    io.ConfigWindowsResizeFromEdges      = true;
    io.ConfigMemoryCompactTimer          = -1.0f;   // never free the atlas behind our back

    if (cfg.nav_keyboard) io.ConfigFlags |=  ImGuiConfigFlags_NavEnableKeyboard;
    else                  io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
}

} // namespace obsidian
