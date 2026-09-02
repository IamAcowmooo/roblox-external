// -----------------------------------------------------------------------------
//  app/demo_ui.cpp  --  showcase + interactive test bench for the window fix
// -----------------------------------------------------------------------------
#include "demo_ui.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace obs = obsidian;

namespace demo {
namespace {

// ---------------------------------------------------------------------------
//  Two extra icons, declared locally to show how easy it is to extend the set:
//  an IconDrawFn is just (ImDrawList*, centre, size, colour).
// ---------------------------------------------------------------------------
void IconPanel(ImDrawList* d, ImVec2 c, float s, ImU32 col)
{
    const ImVec2 a(c.x - s * 0.46f, c.y - s * 0.40f);
    const ImVec2 b(c.x + s * 0.46f, c.y + s * 0.40f);
    d->AddRect(a, b, col, s * 0.16f, 0, s * 0.09f);
    d->AddLine(ImVec2(a.x + s * 0.06f, a.y + s * 0.22f),
               ImVec2(b.x - s * 0.06f, a.y + s * 0.22f), col, s * 0.09f);
}

void IconInfo(ImDrawList* d, ImVec2 c, float s, ImU32 col)
{
    d->AddCircle(c, s * 0.44f, col, 0, s * 0.09f);
    d->AddCircleFilled(ImVec2(c.x, c.y - s * 0.20f), s * 0.055f, col, 8);
    d->AddLine(ImVec2(c.x, c.y - s * 0.05f), ImVec2(c.x, c.y + s * 0.24f), col, s * 0.10f);
}

const char* kTabs[5] = { "Overview", "Components", "Palette", "Window", "About" };
obs::IconDrawFn kTabIcons[5] = { obs::icon::Eye, obs::icon::Sliders, obs::icon::Palette,
                                 IconPanel, IconInfo };

const char* TabLabel(int) ; // fwd (defined below)

// ---------------------------------------------------------------------------
//  Little helpers
// ---------------------------------------------------------------------------
void TextWrappedDim(const char* fmt, ...)
{
    const obs::Palette& p = obs::ActivePalette();
    char buf[1024];
    va_list args; va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(obs::Fade(p.text_dim, p.TextAlphaScale()), "%s", buf);
    ImGui::PopTextWrapPos();
}

void CodeLine(const char* line)
{
    const obs::Palette& p = obs::ActivePalette();
    ImFont* mono = obs::FontMono();
    if (mono) ImGui::PushFont(mono);
    ImGui::TextColored(obs::Fade(obs::Mix(p.text_dim, p.accent, 0.35f), p.TextAlphaScale()), "%s", line);
    if (mono) ImGui::PopFont();
}

// Big number + caption, used by the stat cards.
void StatValue(const char* value, const char* caption, const ImVec4& tint)
{
    const obs::Palette& p = obs::ActivePalette();
    ImFont* title = obs::FontTitle();
    if (title) ImGui::PushFont(title);
    ImGui::TextColored(obs::Fade(tint, p.TextAlphaScale()), "%s", value);
    if (title) ImGui::PopFont();
    ImGui::TextColored(obs::Fade(p.text_faint, p.TextAlphaScale()), "%s", caption);
}

// ---------------------------------------------------------------------------
//  Tabs
// ---------------------------------------------------------------------------
void DrawOverview(State& st)
{
    const obs::Palette& p = obs::ActivePalette();

    // ---- hero ---------------------------------------------------------------
    if (obs::ScopedPanel panel("hero", ImVec2(0, 0), obs::PanelFlags_Elevated | obs::PanelFlags_AccentTop); panel) {
        ImFont* title = obs::FontTitle();
        if (title) ImGui::PushFont(title);
        ImGui::TextColored(obs::Fade(p.text, p.TextAlphaScale()), "Glass Obsidian");
        if (title) ImGui::PopFont();

        TextWrappedDim("A design system for Dear ImGui: layered translucent surfaces, a specular "
                       "top edge on every panel, gradient accents, animated controls drawn as vector "
                       "paths, and a window class whose collapse and resize behaviour is driven "
                       "entirely from a size-constraint callback.");

        ImGui::Spacing();
        obs::Badge("ImGui 1.91+", p.info);
        ImGui::SameLine(0, obs::S(8));
        obs::Badge("public API only", p.ok);
        ImGui::SameLine(0, obs::S(8));
        obs::Badge("no icon font", p.accent);
        ImGui::SameLine(0, obs::S(8));
        obs::Badge("DX11 / GL / headless", p.warn);
    }

    obs::Spacer(obs::S(4));

    // ---- stat cards ---------------------------------------------------------
    const float gap = obs::S(10);
    float avail = ImGui::GetContentRegionAvail().x;
    const float card_w = (avail - gap * 2.0f) / 3.0f;
    const float card_h = obs::S(96);

    if (obs::ScopedPanel panel("c1", ImVec2(card_w, card_h), obs::PanelFlags_None); panel) {
        StatValue("14", "surfaces & primitives", p.accent);
        obs::Spacer(obs::S(2));
        obs::ProgressBar("##p1", 0.85f, &p.accent, nullptr);
    }
    ImGui::SameLine(0, gap);
    if (obs::ScopedPanel panel("c2", ImVec2(card_w, card_h), obs::PanelFlags_None); panel) {
        StatValue("11", "animated controls", p.info);
        obs::Spacer(obs::S(2));
        obs::ProgressBar("##p2", 0.70f, &p.info, nullptr);
    }
    ImGui::SameLine(0, gap);
    if (obs::ScopedPanel panel("c3", ImVec2(card_w, card_h), obs::PanelFlags_None); panel) {
        StatValue("0", "internal headers", p.ok);
        obs::Spacer(obs::S(2));
        obs::ProgressBar("##p3", 1.00f, &p.ok, nullptr);
    }

    obs::Spacer(obs::S(4));

    // ---- two lower panels ---------------------------------------------------
    avail = ImGui::GetContentRegionAvail().x;
    const float half = (avail - gap) * 0.5f;

    if (obs::ScopedPanel panel("start", ImVec2(half, -1), obs::PanelFlags_None); panel) {
        obs::SectionHeader("Integration");
        CodeLine("obsidian::ThemeConfig cfg;");
        CodeLine("obsidian::LoadFonts(cfg);");
        CodeLine("obsidian::ApplyTheme(obsidian::PaletteEmber(), cfg);");
        CodeLine("");
        CodeLine("obsidian::WindowConfig wc;");
        CodeLine("wc.title = \"My Tool\";");
        CodeLine("obsidian::ObsidianWindow win(wc);");
        CodeLine("");
        CodeLine("if (win.Begin(&open)) {");
        CodeLine("    // ... your content ...");
        CodeLine("}");
        CodeLine("win.End();");
    }
    ImGui::SameLine(0, gap);
    if (obs::ScopedPanel panel("notes", ImVec2(half, -1), obs::PanelFlags_Inset); panel) {
        obs::SectionHeader("Why the resize fix works");
        TextWrappedDim("Size is decided inside SetNextWindowSizeConstraints(), which ImGui runs "
                       "every frame with this frame's DesiredSize -- so there is no one-frame lag "
                       "and nothing overwrites the user's drag afterwards.");
        obs::Spacer(obs::S(6));
        obs::KeyValue("Forced sizes per frame", "0");
        obs::KeyValue("Constraint rects", "1 (shared)");
        obs::KeyValue("Min width, collapsed", "same as expanded");
        obs::KeyValue("Body height floor", "1 px");
        obs::Spacer(obs::S(6));
        TextWrappedDim("Open the Window tab to run the invariants against a live window.");
    }
    (void)st;
}

void DrawComponents(State& st)
{
    const obs::Palette& p = obs::ActivePalette();
    const float gap = obs::S(10);
    const float half = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;

    // ---- left column --------------------------------------------------------
    if (obs::ScopedPanel panel("left", ImVec2(half, -1), obs::PanelFlags_None); panel) {
        obs::SectionHeader("Switches");
        obs::Toggle("Hardware shadows",     &st.shadows,       "per-frame");
        obs::Toggle("VSync",                &st.vsync,         "adaptive");
        obs::Toggle("Wireframe overlay",    &st.wireframe,     nullptr);
        obs::Toggle("Crash telemetry",      &st.telemetry,     "opt-in");
        obs::Toggle("Auto-save workspace",  &st.auto_save,     "30 s");
        obs::Toggle("Desktop notifications",&st.notifications, nullptr);

        obs::Spacer(obs::S(6));
        obs::SectionHeader("Buttons");
        if (obs::Button("Primary action", ImVec2(0, 0), obs::ButtonKind_Primary))
            st.progress = 0.0f;
        ImGui::SameLine(0, obs::S(8));
        obs::Button("Default");
        ImGui::SameLine(0, obs::S(8));
        obs::Button("Ghost", ImVec2(0, 0), obs::ButtonKind_Ghost);
        ImGui::Spacing();
        obs::Button("Destructive", ImVec2(0, 0), obs::ButtonKind_Danger);
        ImGui::SameLine(0, obs::S(8));
        obs::IconButton("search", obs::icon::Search, ImVec2(0, 0), obs::ButtonKind_Default, "Search");
        ImGui::SameLine(0, obs::S(6));
        obs::IconButton("gear", obs::icon::Gear, ImVec2(0, 0), obs::ButtonKind_Ghost, "Settings");
        ImGui::SameLine(0, obs::S(6));
        obs::IconButton("power", obs::icon::Power, ImVec2(0, 0), obs::ButtonKind_Danger, "Shut down");

        obs::Spacer(obs::S(6));
        obs::SectionHeader("Nested tabs");
        static const char* inner[3] = { "General", "Advanced", "Diagnostics" };
        obs::TabBar("inner", inner, 3, &st.inner_tab);
        obs::Spacer(obs::S(4));
        if (st.inner_tab == 0) {
            obs::Toggle("High contrast text", &st.high_contrast, nullptr);
            obs::Toggle("Reduce motion",      &st.reduce_motion, "no easing");
        } else if (st.inner_tab == 1) {
            obs::SliderInt("MSAA samples", &st.samples, 0, 4, "%d", 1);
            obs::Slider("Motion blur", &st.blur, 0.0f, 16.0f, "%.1f px", 0.5f, 6.0f);
        } else {
            obs::KeyValue("Frame time", "7.41 ms");
            obs::KeyValue("Draw calls", "184");
            obs::KeyValue("Vertices", "62 480");
            obs::ProgressBar("Atlas usage", 0.42f, &p.info, "42%");
        }
    }
    ImGui::SameLine(0, gap);

    // ---- right column -------------------------------------------------------
    if (obs::ScopedPanel panel("right", ImVec2(half, -1), obs::PanelFlags_None); panel) {
        obs::SectionHeader("Sliders");
        obs::Slider("Master volume", &st.volume, 0.0f, 100.0f, "%.0f%%", 1.0f, 62.0f);
        obs::Slider("Brightness", &st.brightness, 0.0f, 1.0f, "%.2f", 0.01f, 0.80f);
        obs::Slider("Field of view", &st.zoom, 40.0f, 120.0f, "%.0f deg", 1.0f, 90.0f);
        obs::SliderOpts pow_opts; pow_opts.power = 2.4f;
        obs::Slider("Sensitivity (curved)", &st.sensitivity, 0.05f, 8.0f, "%.3f", 0.0f, 1.0f, pow_opts);
        obs::SliderInt("Render scale", &st.quality, 0, 4, "level %d", 2);

        obs::Spacer(obs::S(6));
        obs::SectionHeader("Fields");
        obs::TextInput("Project name", st.project, sizeof(st.project), "untitled");
        obs::TextInput("Filter", st.filter, sizeof(st.filter), "type to filter...");
        static const char* quals[4] = { "Low", "Medium", "High", "Ultra" };
        obs::Combo("Quality preset", &st.quality, quals, 4);
        static const char* modes[3] = { "PNG (lossless)", "JPEG (small)", "WebP (balanced)" };
        obs::Combo("Export format", &st.export_mode, modes, 3);

        obs::Spacer(obs::S(6));
        obs::SectionHeader("Feedback");
        obs::ProgressBar("Encoding", st.progress, &p.accent, nullptr);
        obs::ProgressBar("Disk", 0.31f, &p.ok, "31%");
        obs::ProgressBar("Thermal", 0.88f, &p.danger, "88%");
        ImGui::Spacing();
        obs::Badge("live", p.ok, true);
        ImGui::SameLine(0, obs::S(8));
        obs::Badge("3 warnings", p.warn);
        ImGui::SameLine(0, obs::S(8));
        obs::Badge("degraded", p.danger, true);
    }
}

void DrawPaletteTab(State& st)
{
    const obs::Palette& p = obs::ActivePalette();
    const float gap = obs::S(10);

    obs::SectionHeader("Accent presets");
    int count = 0;
    const char* const* names = obs::PaletteNameList(&count);
    const float card_w = (ImGui::GetContentRegionAvail().x - gap * (float)(count - 1)) / (float)count;
    for (int i = 0; i < count; ++i) {
        if (i) ImGui::SameLine(0, gap);
        const obs::Palette* pal = obs::PaletteByName(names[i]);
        if (!pal) continue;

        ImGui::PushID(i);
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const float h = obs::S(76);
        const bool clicked = ImGui::InvisibleButton("##card", ImVec2(card_w, h));
        const bool hov = ImGui::IsItemHovered();
        const ImVec2 b(a.x + card_w, a.y + h);
        ImDrawList* d = ImGui::GetWindowDrawList();

        obs::GradientFillRounded(d, a, b, obs::Shade(obs::Hex(0x12161F), 0.02f),
                                 obs::Hex(0x080B11), p.corner);
        // accent swatch
        const ImVec2 sa(a.x + obs::S(10), a.y + obs::S(10));
        const ImVec2 sb(a.x + card_w - obs::S(10), a.y + obs::S(34));
        obs::GradientFillRoundedH(d, sa, sb, pal->accent, pal->accent_2, obs::S(6));
        obs::StrokeEdge(d, sa, sb, obs::Fade(ImVec4(1,1,1,1), 0.18f), obs::S(6));

        const float tw = obs::TextWidth(nullptr, names[i]);
        d->AddText(ImVec2(a.x + (card_w - tw) * 0.5f, a.y + obs::S(42)),
                   ImGui::ColorConvertFloat4ToU32(obs::Fade(st.palette == i ? p.text : p.text_dim,
                                                            p.TextAlphaScale())), names[i]);
        obs::StrokeEdge(d, a, b, st.palette == i ? obs::Fade(p.accent, 0.9f)
                                                 : obs::Fade(p.edge, 0.45f + 0.4f * hov), p.corner);
        if (st.palette == i)
            obs::EdgeGlow(d, a, b, obs::Fade(p.accent, 0.12f), p.corner, 3);
        if (clicked) st.palette = i;
        ImGui::PopID();
    }

    obs::Spacer(obs::S(6));

    const float half = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
    if (obs::ScopedPanel panel("tune", ImVec2(half, -1), obs::PanelFlags_None); panel) {
        obs::SectionHeader("Tuning");
        obs::Slider("Surface opacity", &st.opacity, 0.35f, 1.00f, "%.2f", 0.01f, 1.00f);
        obs::Slider("UI scale", &st.ui_scale, 0.85f, 1.50f, "%.2fx", 0.01f, 1.00f);
        obs::Slider("Corner radius", &st.corner, 0.0f, 20.0f, "%.0f px", 1.0f, 12.0f);
        obs::Spacer(obs::S(4));
        TextWrappedDim("Opacity scales every surface alpha while text and hairlines follow a "
                       "gentler curve, so the UI stays legible as the glass gets thinner. "
                       "UI scale re-bakes the font atlas and rescales the window in place.");
    }
    ImGui::SameLine(0, gap);
    if (obs::ScopedPanel panel("ramp", ImVec2(half, -1), obs::PanelFlags_Inset); panel) {
        obs::SectionHeader("Surface ramp (ascending elevation)");
        struct Row { const char* n; ImVec4 c; };
        const Row rows[] = {
            { "backdrop",  p.backdrop },
            { "window",    p.window_bottom },
            { "header",    p.header },
            { "panel",     p.panel },
            { "field",     p.field },
            { "edge",      p.edge },
            { "text dim",  p.text_dim },
            { "text",      p.text },
        };
        ImDrawList* d = ImGui::GetWindowDrawList();
        for (const Row& r : rows) {
            const ImVec2 a = ImGui::GetCursorScreenPos();
            const float sz = obs::S(16);
            d->AddRectFilled(a, ImVec2(a.x + sz * 1.6f, a.y + sz),
                             ImGui::ColorConvertFloat4ToU32(r.c), obs::S(4));
            obs::StrokeEdge(d, a, ImVec2(a.x + sz * 1.6f, a.y + sz), obs::Fade(p.edge, 0.7f), obs::S(4));
            // the dummy must reserve the CHIP's width, otherwise SameLine puts
            // the label back on top of the swatch
            ImGui::Dummy(ImVec2(sz * 1.6f, sz));
            ImGui::SameLine(0, obs::S(10));
            ImGui::TextColored(obs::Fade(p.text_dim, p.TextAlphaScale()), "%s", r.n);
            ImGui::SameLine(0, obs::S(6));
            char hex[16];
            snprintf(hex, sizeof(hex), "#%02X%02X%02X",
                     (int)(r.c.x * 255.0f), (int)(r.c.y * 255.0f), (int)(r.c.z * 255.0f));
            ImFont* mono = obs::FontMono();
            if (mono) ImGui::PushFont(mono);
            ImGui::TextColored(obs::Fade(p.text_faint, p.TextAlphaScale()), "%s", hex);
            if (mono) ImGui::PopFont();
        }
    }
}

void DrawWindowBench(State& st, obs::ObsidianWindow& win)
{
    const obs::Palette& p = obs::ActivePalette();
    const float gap = obs::S(10);

    if (obs::ScopedPanel panel("ctl", ImVec2(0, 0), obs::PanelFlags_AutoResizeY | obs::PanelFlags_None); panel) {
        obs::SectionHeader("Collapse");
        if (obs::Button("Collapse", ImVec2(obs::S(110), 0))) win.Collapse();
        ImGui::SameLine(0, obs::S(8));
        if (obs::Button("Expand", ImVec2(obs::S(110), 0), obs::ButtonKind_Primary)) win.Expand();
        ImGui::SameLine(0, obs::S(8));
        if (obs::Button("Toggle", ImVec2(obs::S(110), 0))) win.ToggleCollapse();
        ImGui::SameLine(0, obs::S(8));
        obs::HelpMarker("Equivalent to double-clicking the title bar.");

        obs::SectionHeader("Animated size presets");
        if (obs::Button("Compact 620x420")) win.SetExpandedSize(ImVec2(obs::S(620), obs::S(420)));
        ImGui::SameLine(0, obs::S(8));
        if (obs::Button("Default 860x560")) win.SetExpandedSize(ImVec2(obs::S(860), obs::S(560)));
        ImGui::SameLine(0, obs::S(8));
        if (obs::Button("Wide 1180x640")) win.SetExpandedSize(ImVec2(obs::S(1180), obs::S(640)));
        ImGui::SameLine(0, obs::S(8));
        if (obs::Button("Reset (instant)", ImVec2(0, 0), obs::ButtonKind_Ghost))
            win.SetExpandedSize(ImVec2(obs::S(860), obs::S(560)), false);

        obs::SectionHeader("Stress");
        if (obs::Button(st.stress_frames > 0 ? "Stop rapid toggle" : "Rapid toggle x180",
                        ImVec2(0, 0), st.stress_frames > 0 ? obs::ButtonKind_Danger
                                                           : obs::ButtonKind_Default))
            st.stress_frames = st.stress_frames > 0 ? 0 : 180;
        ImGui::SameLine(0, obs::S(8));
        if (obs::Button("QueueSize(980x600)")) win.QueueSize(ImVec2(obs::S(980), obs::S(600)));
        ImGui::SameLine(0, obs::S(8));
        obs::HelpMarker("QueueSize is a one-shot request: it is consumed by the constraint "
                        "callback on the next frame and then forgotten, unlike a per-frame "
                        "SetNextWindowSize(ImGuiCond_Always).");
    }

    obs::Spacer(obs::S(4));
    const float half = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;

    if (obs::ScopedPanel panel("live", ImVec2(half, -1), obs::PanelFlags_None); panel) {
        obs::SectionHeader("Live state");
        char buf[64];
        const ImVec2 cur = win.CurrentSize();
        const ImVec2 exp = win.ExpandedSize();
        snprintf(buf, sizeof(buf), "%.0f x %.0f", cur.x, cur.y);
        obs::KeyValue("Current size", buf);
        snprintf(buf, sizeof(buf), "%.0f x %.0f", exp.x, exp.y);
        obs::KeyValue("Restore size", buf);
        snprintf(buf, sizeof(buf), "%.3f", win.Expansion());
        obs::KeyValue("Expansion", buf, &p.accent);
        snprintf(buf, sizeof(buf), "%.1f px", win.TitleBarHeightPx());
        obs::KeyValue("Title bar", buf);
        obs::KeyValue("Collapsed", win.IsCollapsed() ? "yes" : "no");
        obs::KeyValue("Animating", win.IsAnimating() ? "yes" : "no",
                      win.IsAnimating() ? &p.warn : &p.ok);
        obs::Spacer(obs::S(4));
        obs::ProgressBar("collapse / expand", win.Expansion(), &p.accent, nullptr);
        obs::Spacer(obs::S(4));
        TextWrappedDim("Watch Restore size while you drag the bottom-right grip: it tracks your "
                       "drag live, and it is what a later Expand() puts you back to.");
    }
    ImGui::SameLine(0, gap);

    if (obs::ScopedPanel panel("inv", ImVec2(half, -1), obs::PanelFlags_None); panel) {
        obs::SectionHeader("Invariants (headless suite)");
        if (st.invariants.empty()) {
            TextWrappedDim("No results in this build. The headless harness (tests/headless_main.cpp) "
                           "fills this list by driving a real window with simulated input.");
        } else {
            int passed = 0;
            for (const Invariant& iv : st.invariants) {
                obs::Badge(iv.pass ? "PASS" : "FAIL", iv.pass ? p.ok : p.danger);
                ImGui::SameLine(0, obs::S(8));
                ImGui::TextColored(obs::Fade(p.text, p.TextAlphaScale()), "%s", iv.name.c_str());
                if (!iv.detail.empty()) {
                    ImFont* mono = obs::FontMono();
                    if (mono) ImGui::PushFont(mono);
                    ImGui::TextColored(obs::Fade(p.text_faint, p.TextAlphaScale()), "  %s",
                                       iv.detail.c_str());
                    if (mono) ImGui::PopFont();
                }
                if (iv.pass) ++passed;
            }
            obs::Spacer(obs::S(4));
            char sum[64];
            snprintf(sum, sizeof(sum), "%d / %d", passed, (int)st.invariants.size());
            obs::KeyValue("Passed", sum, passed == (int)st.invariants.size() ? &p.ok : &p.danger);
        }
    }
}

void DrawAbout()
{
    const obs::Palette& p = obs::ActivePalette();

    if (obs::ScopedPanel panel("about", ImVec2(0, -1), obs::PanelFlags_None); panel) {
        obs::SectionHeader("What this is");
        TextWrappedDim("Glass Obsidian is a self-contained styling layer for Dear ImGui. It adds a "
                       "token-driven palette, a set of drawing primitives for layered translucent "
                       "surfaces, animated replacements for the stock controls, and a window class "
                       "that fixes the classic \"resizing breaks when collapsed\" cluster of bugs.");

        obs::SectionHeader("Rendering techniques");
        obs::KeyValue("Gradient in a rounded rect", "clip-band fill");
        obs::KeyValue("Top specular highlight", "3 clipped strokes");
        obs::KeyValue("Drop shadow", "ImDrawList::AddShadowRect, full-screen clip");
        obs::KeyValue("Accent bloom", "stacked alpha-faded bands");
        obs::KeyValue("Controls", "invisible native widget + overlay paint");
        obs::KeyValue("Icons", "vector paths (no icon font)");

        obs::SectionHeader("Portability");
        TextWrappedDim("The library includes only imgui.h. It does not use imgui_internal.h, does "
                       "not rely on IMGUI_DEFINE_MATH_OPERATORS, and does not use the IM_MAX / "
                       "IM_CLAMP macros (those live in imgui_internal.h). It compiles against a "
                       "stock imconfig.h.");

        obs::SectionHeader("Verification");
        TextWrappedDim("tools/soft_raster.h contains a CPU rasteriser for ImDrawData and a "
                       "dependency-free PNG writer, so the theme can be rendered and diffed with no "
                       "GPU, no window system and no image library. tests/headless_main.cpp drives a "
                       "real window with simulated mouse input and asserts the collapse/resize "
                       "invariants listed on the Window tab.");

        obs::SectionHeader("License");
        TextWrappedDim("MIT. Dear ImGui (third_party/imgui) is MIT, (c) Omar Cornut.");
        obs::Spacer(obs::S(2));
        obs::Badge("MIT", p.ok);
    }
}

} // namespace

// ---------------------------------------------------------------------------
int TabCount() { return 5; }
const char* TabLabel(int i) { return (i >= 0 && i < 5) ? kTabs[i] : ""; }

obs::Palette BuildPalette(const State& st)
{
    int count = 0;
    const char* const* names = obs::PaletteNameList(&count);
    const obs::Palette* base = obs::PaletteByName(names[obs::IClamp(st.palette, 0, count - 1)]);
    obs::Palette p = base ? *base : obs::PaletteEmber();

    p.opacity   = obs::FClamp(st.opacity, 0.20f, 1.00f);
    p.corner_lg = st.corner;
    p.corner    = st.corner * 0.66f;
    p.corner_sm = st.corner * 0.42f;
    if (st.high_contrast) {
        p.text       = obs::Hex(0xFFFFFF, 1.0f);
        p.text_dim   = obs::Hex(0xD6DCE8, 1.0f);
        p.text_faint = obs::Hex(0xA8B1C2, 1.0f);
        p.edge       = obs::Shade(p.edge, 0.30f);
    }
    return p;
}

void Draw(State& st, obs::ObsidianWindow& win)
{
    obs::TabBarIcons("main_tabs", kTabs, kTabIcons, 5, &st.tab);
    obs::Spacer(obs::S(10));

    switch (st.tab) {
    case 0: DrawOverview(st);        break;
    case 1: DrawComponents(st);      break;
    case 2: DrawPaletteTab(st);      break;
    case 3: DrawWindowBench(st, win);break;
    default: DrawAbout();            break;
    }
}

} // namespace demo
