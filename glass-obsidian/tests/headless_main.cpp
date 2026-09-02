// -----------------------------------------------------------------------------
//  tests/headless_main.cpp
// -----------------------------------------------------------------------------
//  Drives the REAL theme and the REAL ObsidianWindow with no GPU, no window
//  system and no image library:
//
//    1. an invariant suite that simulates mouse input (resize-grip drags, rapid
//       collapse toggles, off-screen placement) and asserts the collapse/resize
//       behaviour, and
//    2. PNG screenshots produced by tools/soft_raster.h, so the design can be
//       reviewed and regression-diffed.
//
//  Usage:  headless_tests [output_dir]
//  Exit code is the number of failed invariants.
// -----------------------------------------------------------------------------
#include "demo_ui.h"
#include "../tools/soft_raster.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace obs = obsidian;

namespace {

const int   DISP_W = 1440;
const int   DISP_H = 900;
const float MIN_W  = 480.0f;
const float MIN_H  = 300.0f;

// =============================================================================
//  App: owns the single ImGui context, the font atlas and the rasteriser
// =============================================================================
struct App
{
    softras::Framebuffer fb;
    softras::Raster      raster;
    float last_scale = -1.0f;

    App()
    {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)DISP_W, (float)DISP_H);
        io.DeltaTime   = 1.0f / 60.0f;
        io.IniFilename = nullptr;          // the window state machine is authoritative
        fb.Resize(DISP_W, DISP_H);
        Move(4.0f, 4.0f);                  // neutral: no widget hovered
    }
    ~App() { ImGui::DestroyContext(); }

    void SetScale(float scale)
    {
        if (scale == last_scale) return;
        last_scale = scale;
        obs::ThemeConfig cfg;
        cfg.ui_scale = scale;
        obs::LoadFonts(cfg);
        // Headless stand-in for the backend's texture upload: tag the atlas with
        // a sentinel so the rasteriser knows which commands are glyph draws.
        ImGui::GetIO().Fonts->SetTexID((ImTextureID)1);
        raster.SetFontAtlas((ImTextureID)1);
    }

    void Frame(const std::function<void()>& ui)
    {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)DISP_W, (float)DISP_H);
        io.DeltaTime   = 1.0f / 60.0f;
        ImGui::NewFrame();
        ui();
        ImGui::Render();
    }

    // ---- simulated input ----------------------------------------------------
    void Move(float x, float y) { ImGui::GetIO().AddMousePosEvent(x, y); }
    void Down()                 { ImGui::GetIO().AddMouseButtonEvent(0, true); }
    void Up()                   { ImGui::GetIO().AddMouseButtonEvent(0, false); }

    void Capture(const char* path)
    {
        fb.Clear(ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        raster.Render(ImGui::GetDrawData(), fb);
        if (softras::SavePNG(path, fb)) printf("    wrote %s\n", path);
        else                            printf("    FAILED to write %s\n", path);
    }
};

// =============================================================================
//  Scene: one ObsidianWindow + one demo::State, stepped through App::Frame
// =============================================================================
struct Scene
{
    App&                app;
    demo::State         st;
    obs::ObsidianWindow win;
    bool                open = true;
    bool                last_body = false;

    Scene(App& a, const char* title, ImVec2 size, ImVec2 pos = ImVec2(-1e9f, -1e9f),
          float collapse_time = 0.20f)
        : app(a)
    {
        obs::WindowConfig c;
        c.title         = title;
        c.initial_size  = size;
        c.initial_pos   = pos;
        c.min_width     = MIN_W;
        c.min_height    = MIN_H;
        c.collapse_time = collapse_time;
        win = obs::ObsidianWindow(c);
    }

    void Step(int n = 1)
    {
        for (int i = 0; i < n; ++i) {
            // Font re-bake must happen outside the frame (the atlas is locked
            // between NewFrame and Render).
            app.SetScale(st.ui_scale);
            obs::ThemeConfig cfg;
            cfg.ui_scale = st.ui_scale;
            app.Frame([&] {
                obs::ApplyTheme(demo::BuildPalette(st), cfg);

                if (st.stress_frames > 0) { win.ToggleCollapse(); --st.stress_frames; }

                obs::DrawAmbientBackdrop(1.0f);
                if (win.Begin(&open)) {
                    last_body = true;
                    demo::Draw(st, win);
                } else {
                    last_body = false;
                }
                win.End();
            });
        }
    }

    // Drag a point of the window from its current screen position by (dx, dy)
    // over `steps` frames, with the button held the whole time.
    void DragFrom(ImVec2 screen_pt, float dx, float dy, int steps = 12)
    {
        app.Move(screen_pt.x, screen_pt.y);
        Step(2);
        app.Down();
        Step(1);
        for (int i = 1; i <= steps; ++i)
            { app.Move(screen_pt.x + dx * (float)i / (float)steps,
                       screen_pt.y + dy * (float)i / (float)steps); Step(1); }
        app.Up();
        Step(3);
        app.Move(4.0f, 4.0f);
        Step(1);
    }

    ImVec2 BottomRight() const
    {
        const ImVec2 p = win.WindowPos(), s = win.CurrentSize();
        return ImVec2(p.x + s.x - 2.0f, p.y + s.y - 2.0f);
    }
    ImVec2 RightEdge() const
    {
        const ImVec2 p = win.WindowPos(), s = win.CurrentSize();
        return ImVec2(p.x + s.x - 2.0f, p.y + s.y * 0.5f);
    }
};

// =============================================================================
//  Result plumbing
// =============================================================================
std::vector<demo::Invariant> g_results;

void Report(const char* name, bool pass, const char* fmt, ...)
{
    demo::Invariant iv;
    iv.name = name;
    iv.pass = pass;
    char buf[192];
    va_list args; va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    iv.detail = buf;
    g_results.push_back(iv);
    printf("  [%s] %-52s %s\n", pass ? "PASS" : "FAIL", name, buf);
}

bool Near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }
bool Finite(ImVec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); }

// =============================================================================
//  The invariant suite
// =============================================================================
void RunSuite(App& app)
{
    printf("\n== ObsidianWindow collapse/resize invariants ==\n");
    const float TB = obs::TitleBarHeight();

    // ---- 1. collapse settles exactly on the title bar -----------------------
    {
        Scene s(app, "T1", ImVec2(860, 560), ImVec2(80, 60));
        s.Step(10);
        s.win.Collapse();
        s.Step(40);
        const float h = s.win.CurrentSize().y;
        Report("collapse settles on title bar height", Near(h, TB, 1.0f),
               "h=%.1f expected=%.1f", h, TB);
    }

    // ---- 2. expand restores the exact pre-collapse size ---------------------
    {
        Scene s(app, "T2", ImVec2(860, 560), ImVec2(80, 60));
        s.Step(10);
        const ImVec2 before = s.win.CurrentSize();
        s.win.Collapse(); s.Step(40);
        s.win.Expand();   s.Step(40);
        const ImVec2 after = s.win.CurrentSize();
        Report("expand restores exact pre-collapse size",
               Near(before.x, after.x, 1.0f) && Near(before.y, after.y, 1.0f),
               "%.0fx%.0f -> %.0fx%.0f", before.x, before.y, after.x, after.y);
    }

    // ---- 3. THE regression: a user resize survives collapse+expand ----------
    {
        Scene s(app, "T3", ImVec2(860, 560), ImVec2(80, 60));
        s.Step(10);
        const ImVec2 target(1024, 680);
        const ImVec2 sz = s.win.CurrentSize();
        s.DragFrom(s.BottomRight(), target.x - sz.x, target.y - sz.y, 14);
        const ImVec2 resized = s.win.CurrentSize();
        const bool drag_ok = Near(resized.x, target.x, 3.0f) && Near(resized.y, target.y, 3.0f);

        s.win.Collapse(); s.Step(40);
        const float collapsed_h = s.win.CurrentSize().y;
        const bool collapse_ok = Near(collapsed_h, TB, 1.0f);

        s.win.Expand(); s.Step(40);
        const ImVec2 back = s.win.CurrentSize();
        const bool restore_ok = Near(back.x, resized.x, 1.5f) && Near(back.y, resized.y, 1.5f);

        Report("user resize survives collapse + expand",
               drag_ok && collapse_ok && restore_ok,
               "drag %.0fx%.0f (want %.0fx%.0f) -> collapsed h=%.0f -> restored %.0fx%.0f",
               resized.x, resized.y, target.x, target.y, collapsed_h, back.x, back.y);
    }

    // ---- 4. no sideways pop: min width is the same in both states -----------
    {
        Scene s(app, "T4", ImVec2(860, 560), ImVec2(80, 60));
        s.Step(10);
        s.win.Collapse(); s.Step(40);

        const float min_w = obs::S(MIN_W);
        const ImVec2 sz = s.win.CurrentSize();
        s.DragFrom(s.RightEdge(), (min_w - 60.0f) - sz.x, 0.0f, 12);
        const float collapsed_w = s.win.CurrentSize().x;

        s.win.Expand(); s.Step(40);
        const float expanded_w = s.win.CurrentSize().x;

        Report("no sideways pop on expand (shared min width)",
               Near(collapsed_w, min_w, 2.5f) && Near(expanded_w, min_w, 2.5f),
               "collapsed w=%.0f, expanded w=%.0f, min=%.0f", collapsed_w, expanded_w, min_w);
    }

    // ---- 5. a settled window is bit-stable (no permanent sub-pixel jitter) --
    {
        Scene s(app, "T5", ImVec2(860, 560), ImVec2(80, 60));
        s.Step(25);
        const ImVec2 a = s.win.CurrentSize();
        s.Step(40);
        const ImVec2 b = s.win.CurrentSize();
        Report("settled size is bit-stable over 40 idle frames",
               a.x == b.x && a.y == b.y, "%.3fx%.3f vs %.3fx%.3f", a.x, a.y, b.x, b.y);
    }

    // ---- 6. the constraint never fights an in-progress user drag ------------
    {
        Scene s(app, "T6", ImVec2(860, 560), ImVec2(80, 60));
        s.Step(10);
        const ImVec2 sz = s.win.CurrentSize();
        const ImVec2 from = s.BottomRight();
        app.Move(from.x, from.y); s.Step(2);
        app.Down(); s.Step(1);
        bool monotonic = true;
        float prev = sz.x;
        for (int i = 1; i <= 10; ++i) {
            app.Move(from.x + 14.0f * (float)i, from.y + 9.0f * (float)i);
            s.Step(1);
            const float w = s.win.CurrentSize().x;
            if (w < prev - 1.0f) monotonic = false;
            prev = w;
        }
        app.Up(); s.Step(3);
        const ImVec2 fin = s.win.CurrentSize();
        Report("expanded drag is followed, not pinned",
               monotonic && Near(fin.x, sz.x + 140.0f, 4.0f),
               "monotonic=%d final=%.0f want=%.0f", (int)monotonic, fin.x, sz.x + 140.0f);
    }

    // ---- 7. rapid toggling stays in bounds and lands on the requested state -
    {
        Scene s(app, "T7", ImVec2(860, 560), ImVec2(80, 60));
        s.Step(10);
        const ImVec2 sz = s.win.CurrentSize();
        bool in_bounds = true, all_finite = true;
        bool expect_collapsed = false;
        for (int i = 0; i < 120; ++i) {
            s.win.ToggleCollapse();
            expect_collapsed = !expect_collapsed;
            s.Step(1);
            const ImVec2 c = s.win.CurrentSize();
            if (!Finite(c)) all_finite = false;
            if (c.y < TB - 1.0f || c.y > sz.y + 1.0f) in_bounds = false;
            if (c.x < MIN_W - 1.0f || c.x > (float)DISP_W) in_bounds = false;
        }
        s.Step(60);
        const ImVec2 settled = s.win.CurrentSize();
        const float want_h = expect_collapsed ? TB : sz.y;
        Report("120 rapid toggles stay in bounds and settle correctly",
               in_bounds && all_finite && Near(settled.y, want_h, 1.0f) &&
               s.win.IsCollapsed() == expect_collapsed,
               "bounds=%d finite=%d final h=%.0f want=%.0f",
               (int)in_bounds, (int)all_finite, settled.y, want_h);
    }

    // ---- 8. an off-screen window keeps its title bar reachable --------------
    {
        Scene s(app, "T8", ImVec2(860, 560), ImVec2(-4000, -3000));
        s.Step(6);
        const ImVec2 p = s.win.WindowPos();
        const ImVec2 sz = s.win.CurrentSize();
        const bool ok = p.y <= (float)DISP_H - TB && p.y >= -4.0f &&
                        p.x <= (float)DISP_W - obs::S(72.0f) &&
                        p.x >= -sz.x + obs::S(72.0f);
        Report("off-screen window is pulled back to a grabbable position", ok,
               "pos=%.0f,%.0f size=%.0fx%.0f", p.x, p.y, sz.x, sz.y);
    }

    // ---- 9. body content is not submitted while collapsed -------------------
    {
        Scene s(app, "T9", ImVec2(860, 560), ImVec2(80, 60));
        s.Step(10);
        const bool body_when_expanded = s.last_body;
        s.win.Collapse(); s.Step(40);
        const bool body_when_collapsed = s.last_body;
        Report("body is skipped while collapsed (0-cost overlay)",
               body_when_expanded && !body_when_collapsed,
               "expanded=%d collapsed=%d", (int)body_when_expanded, (int)body_when_collapsed);
    }

    // ---- 10. QueueSize is one-shot and respects the minimums ----------------
    {
        Scene s(app, "T10", ImVec2(860, 560), ImVec2(80, 60));
        s.Step(10);
        s.win.QueueSize(ImVec2(100, 100));        // far below the minimums
        s.Step(4);
        const ImVec2 clamped = s.win.CurrentSize();
        s.Step(30);
        const ImVec2 later = s.win.CurrentSize();
        Report("QueueSize is one-shot and clamped to the minimums",
               clamped.x >= obs::S(MIN_W) - 1.0f && clamped.y >= obs::S(MIN_H) - 1.0f &&
               later.x == clamped.x && later.y == clamped.y,
               "immediate=%.0fx%.0f after 30 frames=%.0fx%.0f (min %.0fx%.0f)",
               clamped.x, clamped.y, later.x, later.y, obs::S(MIN_W), obs::S(MIN_H));
    }

    // ---- 11. SetExpandedSize animates and lands exactly ---------------------
    {
        Scene s(app, "T11", ImVec2(860, 560), ImVec2(80, 60));
        s.Step(10);
        const ImVec2 from = s.win.CurrentSize();
        s.win.SetExpandedSize(ImVec2(980, 640));
        s.Step(5);
        const ImVec2 mid = s.win.CurrentSize();
        s.Step(40);
        const ImVec2 fin = s.win.CurrentSize();
        const bool animated = mid.y > from.y + 1.0f && mid.y < 640.0f - 1.0f;
        Report("SetExpandedSize animates then lands exactly",
               animated && Near(fin.x, 980.0f, 1.5f) && Near(fin.y, 640.0f, 1.5f),
               "from %.0f mid %.0f final %.0fx%.0f", from.y, mid.y, fin.x, fin.y);
    }

    // ---- 12. collapsing while a resize morph is in flight -------------------
    {
        Scene s(app, "T12", ImVec2(860, 560), ImVec2(80, 60));
        s.Step(10);
        s.win.SetExpandedSize(ImVec2(1040, 660));
        s.Step(3);                       // morph mid-flight
        s.win.Collapse();                // interrupted by a collapse
        s.Step(60);
        const float collapsed_h = s.win.CurrentSize().y;
        s.win.Expand();
        s.Step(60);
        const ImVec2 back = s.win.CurrentSize();
        Report("collapse interrupts a resize morph cleanly",
               Near(collapsed_h, TB, 1.0f) && Near(back.y, 660.0f, 2.0f) &&
               Near(back.x, 1040.0f, 2.0f),
               "collapsed h=%.0f restored=%.0fx%.0f (want 1040x660)",
               collapsed_h, back.x, back.y);
    }

    // ---- 13. a ui_scale change rescales the window instead of clipping it ---
    {
        Scene s(app, "T13", ImVec2(860, 560), ImVec2(80, 60));
        s.Step(10);
        const ImVec2 at1 = s.win.CurrentSize();
        s.st.ui_scale = 1.30f;
        s.Step(20);
        const ImVec2 at13 = s.win.CurrentSize();
        s.st.ui_scale = 1.00f;
        s.Step(20);
        const ImVec2 back1 = s.win.CurrentSize();
        Report("ui_scale change rescales the window in place",
               at13.x > at1.x * 1.2f && Near(back1.x, at1.x, 2.0f),
               "1.00x=%.0f 1.30x=%.0f back=%.0f", at1.x, at13.x, back1.x);
    }
}

// =============================================================================
//  Screenshots
// =============================================================================
void Shoot(App& app, const std::string& dir)
{
    printf("\n== screenshots ==\n");
    const ImVec2 big(1180, 760), pos(130, 66);

    // Overview
    {
        Scene s(app, "Glass Obsidian", big, pos);
        s.Step(40);
        app.Capture((dir + "/01-overview.png").c_str());
    }
    // Components
    {
        Scene s(app, "Glass Obsidian", big, pos);
        s.st.tab = 1;
        s.Step(40);
        app.Capture((dir + "/02-components.png").c_str());
    }
    // Palette
    {
        Scene s(app, "Glass Obsidian", big, pos);
        s.st.tab = 2;
        s.Step(40);
        app.Capture((dir + "/03-palette.png").c_str());
    }
    // Window bench, with the suite results on screen
    {
        Scene s(app, "Glass Obsidian", ImVec2(1180, 780), ImVec2(130, 56));
        s.st.tab = 3;
        s.st.invariants = g_results;
        s.Step(45);
        app.Capture((dir + "/04-window-bench.png").c_str());
    }
    // About
    {
        Scene s(app, "Glass Obsidian", big, pos);
        s.st.tab = 4;
        s.Step(40);
        app.Capture((dir + "/05-about.png").c_str());
    }
    // Collapsed
    {
        Scene s(app, "Glass Obsidian", big, pos);
        s.Step(20);
        s.win.Collapse();
        s.Step(40);
        app.Capture((dir + "/06-collapsed.png").c_str());
    }
    // Mid-collapse (the animation, frozen ~40% through)
    {
        Scene s(app, "Glass Obsidian", big, pos);
        s.st.tab = 1;
        s.Step(30);
        s.win.Collapse();
        s.Step(5);
        app.Capture((dir + "/07-collapsing.png").c_str());
    }
    // Accent presets at 0.78 opacity, so the translucency is visible
    {
        const char* names[4]   = { "aurora", "verdant", "crimson", "ember-thin" };
        const int   presets[4] = { 1, 2, 3, 0 };
        const char* titles[4]  = { "Aurora preset", "Verdant preset",
                                   "Crimson preset", "Ember, thin glass" };
        for (int i = 0; i < 4; ++i) {
            // NOTE: pass a static string, never a temporary's c_str() -- the
            // config only borrows the pointer.
            Scene s(app, titles[i], ImVec2(980, 620), ImVec2(230, 140));
            s.st.tab = 1;
            s.st.palette = presets[i];
            s.st.opacity = (i == 3) ? 0.62f : 0.80f;
            s.Step(45);
            char path[256];
            snprintf(path, sizeof(path), "%s/08-%s.png", dir.c_str(), names[i]);
            app.Capture(path);
        }
    }
    // Narrow layout (compact preset) -- components must still be usable
    {
        Scene s(app, "Glass Obsidian", ImVec2(620, 560), ImVec2(410, 170));
        s.st.tab = 1;
        s.Step(45);
        app.Capture((dir + "/09-compact.png").c_str());
    }
    // ui_scale 1.30 (DPI bump)
    {
        Scene s(app, "Glass Obsidian", ImVec2(1120, 720), ImVec2(160, 90));
        s.st.tab = 3;
        s.st.invariants = g_results;
        s.st.ui_scale = 1.30f;
        s.Step(50);
        app.Capture((dir + "/10-ui-scale-130.png").c_str());
        s.st.ui_scale = 1.00f;
    }
}

} // namespace

int main(int argc, char** argv)
{
    std::string dir = (argc > 1) ? argv[1] : ".";
    printf("Glass Obsidian -- headless verification\n");
    printf("  display %dx%d, imgui %s\n", DISP_W, DISP_H, IMGUI_VERSION);

    App app;
    app.SetScale(1.0f);
    obs::ApplyTheme(obs::PaletteEmber());

    RunSuite(app);

    int failed = 0;
    for (const demo::Invariant& iv : g_results) if (!iv.pass) ++failed;
    printf("\n  %d / %zu invariants passed\n", (int)g_results.size() - failed, g_results.size());

    Shoot(app, dir);

    printf("\n%s\n", failed == 0 ? "ALL INVARIANTS PASSED" : "SOME INVARIANTS FAILED");
    return failed;
}
