#include <Windows.h>
#include <cstring>
#include <cstdio>
#include <vector>
#include <mutex>
#include <string>
#include <cfloat>
#include <cctype>
#include <cmath>
#include "imgui/imgui.h"
#include "globals.h"
#include "memory.h"
#include "game.h"
#include "cache.h"
#include "offsets.h"
#include "features/skybox_changer/skybox_changer.h"
#include "features/config/config.h"

static int* s_waiting_key_ptr = nullptr;

// window collapse state (shared by the window-sizing preamble and the title-bar
// controls so the collapse button can actually shrink/restore the window)
static bool   s_minimized = false;
static ImVec2 s_restore_size(780.0f, 560.0f);
static bool   s_size_queued = false;
static ImVec2 s_queued_size;
static constexpr float kTitleBarH = 48.0f;

static const char* KeyName(int key) {
    if (key == 0) return "none";
    if (key == VK_LBUTTON) return "lmb";
    if (key == VK_RBUTTON) return "rmb";
    if (key == VK_MBUTTON) return "mmb";
    if (key == VK_XBUTTON1) return "mouse4";
    if (key == VK_XBUTTON2) return "mouse5";
    LONG lp = (MapVirtualKeyA(key, 0) << 16) | 1;
    static char buf[64]{};
    if (GetKeyNameTextA(lp, buf, sizeof(buf)) > 0) return buf;
    return "???";
}

void TickKeybinds() {
    if (!s_waiting_key_ptr) return;

    for (int vk = 1; vk < 256; ++vk) {
        if (vk == VK_LBUTTON || vk == VK_RBUTTON) continue;
        if (GetAsyncKeyState(vk) & 0x8000) {
            while (GetAsyncKeyState(vk) & 0x8000) Sleep(1);
            *s_waiting_key_ptr = vk;
            s_waiting_key_ptr = nullptr;
            return;
        }
    }

    POINT pt;
    GetCursorPos(&pt);
    HWND hw = WindowFromPoint(pt);
    if (hw) {
        WPARAM wp = 0;
        if (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) wp = XBUTTON1;
        else if (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) wp = XBUTTON2;
        if (wp) {
            while ((GetAsyncKeyState(VK_XBUTTON1) & 0x8000) || (GetAsyncKeyState(VK_XBUTTON2) & 0x8000)) Sleep(1);
            *s_waiting_key_ptr = (wp == XBUTTON1) ? VK_XBUTTON1 : VK_XBUTTON2;
            s_waiting_key_ptr = nullptr;
            return;
        }
    }
}

static bool keybind_button(const char* label, int& key) {
    if (s_waiting_key_ptr == &key) {
        if (ImGui::Button("...", ImVec2(-1, 0))) s_waiting_key_ptr = nullptr;
        return false;
    }
    char buf[64];
    sprintf_s(buf, "%s: [%s]", label, KeyName(key));
    if (ImGui::Button(buf, ImVec2(-1, 0))) { s_waiting_key_ptr = &key; return true; }
    return false;
}


// ------------------------------------------------------------------
// UI toolkit — a modern, web-app / CSS-style dark theme: neutral slate
// surfaces, 1px borders, soft shadows and one strong accent (your red).
// This replaces the old rose-tinted glass. Layout is unchanged: tabs
// stay at the top, the log keeps clear+copy, and the ui page still
// drives transparency / rounding / accent / rainbow.
// ------------------------------------------------------------------
namespace ui {
    static const ImU32 TXT_MAIN  = IM_COL32(232, 235, 240, 255);
    static const ImU32 TXT_DIM   = IM_COL32(148, 156, 168, 255);

    // uniform metrics so every row lines up
    static constexpr float ROW_H     = 30.0f;
    static constexpr float SLIDER_H  = 44.0f;
    static constexpr float PAD_X     = 12.0f;
    static constexpr float GAP       = 6.0f;
    static constexpr float ROUND     = 10.0f;

    static float s_ui_reveal = 0.0f;

    // frame-rate independent smooth approach (0..1); used for hover glows,
    // the rail pill and the one-time reveal fade
    inline float Ease(float& state, float target, float speed) {
        float dt = ImGui::GetIO().DeltaTime;
        if (dt <= 0.0f || dt > 0.25f) dt = 1.0f / 60.0f;
        state += (target - state) * (1.0f - expf(-speed * dt));
        return state;
    }

    // every glass surface fades in once on the first frame
    inline float GlassA() {
        Ease(s_ui_reveal, 1.0f, 9.0f);
        return (1.0f - (ui_transparency / 100.0f)) * s_ui_reveal;
    }
    inline float AnimT() { return (float)ImGui::GetTime(); }

    inline ImU32 Mix(ImU32 a, ImU32 b, float t) {
        if (t < 0.0f) t = 0.0f; if (t > 1.0f) t = 1.0f;
        int ar = (a >> 0) & 0xFF, ag = (a >> 8) & 0xFF, ab = (a >> 16) & 0xFF, aa = (a >> 24) & 0xFF;
        int br = (b >> 0) & 0xFF, bg = (b >> 8) & 0xFF, bb = (b >> 16) & 0xFF, ba = (b >> 24) & 0xFF;
        int r = ar + (int)((br - ar) * t);
        int g = ag + (int)((bg - ag) * t);
        int bl = ab + (int)((bb - ab) * t);
        int al = aa + (int)((ba - aa) * t);
        return IM_COL32(r, g, bl, al);
    }

    // live accent (driven by the ui page, so rainbow reaches every widget)
    inline ImU32 Accent(float alpha = 1.0f) {
        return IM_COL32((int)(ui_accent_color[0] * 255.0f),
                        (int)(ui_accent_color[1] * 255.0f),
                        (int)(ui_accent_color[2] * 255.0f),
                        (int)(alpha * 255.0f));
    }
    // ---- reference palette (Rose theme, Style.cpp) + its Fade() ----
    inline ImU32 Fade(ImU32 c, float a) {
        int r = (c >> 0) & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
        return IM_COL32(r, g, b, (int)(a * 255.0f * GlassA()));
    }
    inline ImU32 ColBackdrop()  { return IM_COL32(12, 14, 18, 255); }
    inline ImU32 ColSurface()   { return IM_COL32(20, 23, 29, 255); }
    inline ImU32 ColElevated()  { return IM_COL32(26, 30, 38, 255); }
    inline ImU32 ColHeader()    { return IM_COL32(16, 18, 23, 255); }
    inline ImU32 ColOutline()   { return IM_COL32(58, 65, 77, 255); }
    inline ImU32 ColHighlight() { return IM_COL32(240, 242, 246, 255); }
    inline ImU32 ColControl()   { return IM_COL32(26, 30, 38, 255); }
    inline ImU32 ColSelected()  { return IM_COL32(40, 45, 55, 255); }
    inline ImU32 ColHovered()   { return IM_COL32(36, 41, 51, 255); }
    inline ImU32 ColPressed()   { return IM_COL32(16, 18, 23, 255); }
    inline ImU32 ColGroove()    { return IM_COL32(44, 50, 61, 255); }
    inline ImU32 ColKnob()      { return IM_COL32(240, 242, 246, 255); }
    inline ImU32 ColTab()       { return IM_COL32(18, 20, 25, 255); }
    inline ImU32 ColShade()     { return IM_COL32(0, 0, 0, 255); }

    // reference 2-stop top->bottom gradient, rounded. ImGui's rounded fill has no
    // multi-colour variant, so three stacked bands fake it (invisible for subtle
    // gradients like the reference's).
    inline void Gradient(ImDrawList* d, const ImVec2& a, const ImVec2& b, float r,
                         ImU32 top, ImU32 bottom) {
        ImU32 mid = Mix(top, bottom, 0.5f);
        d->AddRectFilled(a, b, bottom, r);
        float h = b.y - a.y;
        if (h > 6.0f) {
            d->AddRectFilled(a, ImVec2(b.x, a.y + h * 0.62f), mid, r, ImDrawFlags_RoundCornersTop);
            d->AddRectFilled(a, ImVec2(b.x, a.y + h * 0.30f), top, r, ImDrawFlags_RoundCornersTop);
        }
    }

    // soft, web-style ambient glow: one faint accent bloom near the top-right
    // corner (replaces the old multi-colour plasma blobs)
    inline void PlasmaBackdrop(ImDrawList* d, const ImVec2& a, const ImVec2& b) {
        float w = b.x - a.x, h = b.y - a.y;
        if (w < 8.0f || h < 8.0f) return;

        d->PushClipRect(a, b, true);
        float t = AnimT();
        ImVec2 c(b.x - w * 0.18f + sinf(t * 0.30f) * w * 0.03f,
                 a.y + h * 0.10f + cosf(t * 0.26f) * h * 0.02f);
        float R = (w < h ? w : h) * 0.55f;
        for (int k = 8; k >= 1; --k) {
            float rr = R * k / 8.0f;
            float al = 2.5f * (1.0f - k / 9.0f);
            d->AddCircleFilled(c, rr, Fade(Accent(255), al / 255.0f), 48);
        }
        d->PopClipRect();
    }

    // reference glass surface, faithfully following the source's window.cpp:
    //   shadow(Shade) -> gradient(Header.fade -> Surface.fade) -> outline(Outline)
    inline void GlassPanel(ImDrawList* d, const ImVec2& a, const ImVec2& b, float r,
                           float header_fade, float surface_fade, bool shadow) {
        if (shadow)
            d->AddShadowRect(a, b, Fade(ColShade(), 0.63f), 26.0f, ImVec2(0.0f, 10.0f),
                             ImDrawFlags_None, r);
        Gradient(d, a, b, r, Fade(ColHeader(), header_fade), Fade(ColSurface(), surface_fade));
        d->AddRect(a, b, Fade(ColOutline(), 0.125f), r, 0, 1.0f);
    }

    // quiet uppercase section label + a faint rule underneath
    inline void Section(const char* text) {
        ImGui::Dummy(ImVec2(0, 8));
        ImVec2 p = ImGui::GetCursorScreenPos();
        float full = ImGui::GetContentRegionAvail().x;
        ImDrawList* d = ImGui::GetWindowDrawList();
        char up[64];
        int i = 0;
        for (; text[i] && i < 62; ++i) up[i] = (char)toupper((unsigned char)text[i]);
        up[i] = '\0';
        d->AddText(p, TXT_DIM, up);
        float ty = p.y + ImGui::GetTextLineHeight() + 4.0f;
        d->AddLine(ImVec2(p.x, ty), ImVec2(p.x + full, ty), Fade(ColOutline(), 0.16f), 1.0f);
        ImGui::Dummy(ImVec2(0, ImGui::GetTextLineHeight() + 9.0f));
    }

    // page title with a small accent bar + subtitle
    inline void PageHeader(const char* title, const char* subtitle) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* d = ImGui::GetWindowDrawList();
        d->AddRectFilled(ImVec2(p.x, p.y + 2),
                         ImVec2(p.x + 3, p.y + ImGui::GetTextLineHeight() - 2),
                         Accent(), 1.5f);
        ImGui::Indent(10.0f);
        ImGui::TextColored(ImVec4(232 / 255.0f, 235 / 255.0f, 240 / 255.0f, 1.0f), "%s", title);
        if (subtitle && subtitle[0])
            ImGui::TextDisabled("%s", subtitle);
        ImGui::Unindent(10.0f);
        ImGui::Dummy(ImVec2(0, 6));
    }

    // flat toggle row: quiet elevated card + Knob-coloured switch, no glow
    inline bool Toggle(const char* label, bool* v) {
        float full = ImGui::GetContentRegionAvail().x;
        float h = ROW_H;
        ImVec2 p = ImGui::GetCursorScreenPos();

        ImGui::InvisibleButton(label, ImVec2(full, h));
        bool pressed = ImGui::IsItemClicked();
        if (pressed) *v = !*v;
        bool hov = ImGui::IsItemHovered();

        ImDrawList* d = ImGui::GetWindowDrawList();
        GlassPanel(d, p, ImVec2(p.x + full, p.y + h), ROUND,
                   hov ? 0.40f : 0.30f, hov ? 0.30f : 0.22f, false);
        if (*v)
            d->AddRect(p, ImVec2(p.x + full, p.y + h), Accent(0.55f), ROUND, 0, 1.0f);

        d->AddText(ImVec2(p.x + PAD_X, p.y + (h - ImGui::GetTextLineHeight()) * 0.5f),
                   *v ? TXT_MAIN : TXT_DIM, label);

        float tw = 40.0f, th = 20.0f;
        float tx = p.x + full - tw - 12.0f;
        float ty = p.y + (h - th) * 0.5f;

        d->AddRectFilled(ImVec2(tx, ty), ImVec2(tx + tw, ty + th),
                         *v ? Accent() : ColGroove(), th * 0.5f);

        float kr = 7.0f;
        float kx = *v ? (tx + tw - kr - 3.0f) : (tx + kr + 3.0f);
        float ky = ty + th * 0.5f;
        d->AddCircleFilled(ImVec2(kx, ky), kr, ColKnob());

        ImGui::Dummy(ImVec2(0, GAP - 2));
        return pressed;
    }

    // flat slider row: label + accent value + groove/fill + Knob dot, no glow
    inline bool Slider(const char* label, float* v, float mn, float mx, const char* fmt = "%.0f") {
        float full = ImGui::GetContentRegionAvail().x;
        float h = SLIDER_H;
        ImVec2 p = ImGui::GetCursorScreenPos();

        ImGui::PushID(label);
        ImGui::InvisibleButton("##s", ImVec2(full, h));
        bool active = ImGui::IsItemActive();
        bool hov = ImGui::IsItemHovered();

        float trx = p.x + PAD_X;
        float trw = full - PAD_X * 2.0f;
        float try_ = p.y + h - 15.0f;

        if (active) {
            float mxpos = ImGui::GetIO().MousePos.x;
            float r = (mxpos - trx) / (trw > 0 ? trw : 1.0f);
            if (r < 0) r = 0; if (r > 1) r = 1;
            *v = mn + (mx - mn) * r;
        }

        ImDrawList* d = ImGui::GetWindowDrawList();
        GlassPanel(d, p, ImVec2(p.x + full, p.y + h), ROUND,
                   (hov || active) ? 0.40f : 0.30f, (hov || active) ? 0.30f : 0.22f, false);

        d->AddText(ImVec2(p.x + PAD_X, p.y + 8), TXT_MAIN, label);

        char buf[64];
        snprintf(buf, sizeof(buf), fmt, *v);
        ImVec2 ts = ImGui::CalcTextSize(buf);
        d->AddText(ImVec2(p.x + full - ts.x - PAD_X, p.y + 8), Accent(), buf);

        d->AddRectFilled(ImVec2(trx, try_), ImVec2(trx + trw, try_ + 5), ColGroove(), 2.5f);
        float ratio = (mx - mn) != 0.0f ? (*v - mn) / (mx - mn) : 0.0f;
        if (ratio < 0) ratio = 0; if (ratio > 1) ratio = 1;
        float fillw = trw * ratio;
        if (fillw > 0.0f)
            d->AddRectFilled(ImVec2(trx, try_), ImVec2(trx + fillw, try_ + 5), Accent(), 2.5f);

        float kx = trx + fillw;
        float ky = try_ + 2.5f;
        float kr = (active || hov) ? 7.0f : 5.5f;
        d->AddCircleFilled(ImVec2(kx, ky), kr, ColKnob());

        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, GAP - 2));
        return active;
    }
}

void RenderMenu() {
    // live UI customisation (ui page)
    {
        ImGuiStyle& st = ImGui::GetStyle();
        float r = ui_rounded_corners ? ui_corner_radius : 0.0f;
        st.WindowRounding = r;
        st.ChildRounding  = r;
        st.FrameRounding  = r;
        st.PopupRounding  = r;
        st.ScrollbarRounding = 8.0f;
        st.GrabRounding   = 8.0f;

        // remember the accent we're overwriting, and restore it the moment the
        // rainbow switch is turned back off (otherwise it stays on the last hue)
        static bool s_rainbow_was_on = false;
        static float s_prev_accent[3] = { 0.78f, 0.08f, 0.08f };
        if (ui_rainbow) {
            if (!s_rainbow_was_on) {
                s_prev_accent[0] = ui_accent_color[0];
                s_prev_accent[1] = ui_accent_color[1];
                s_prev_accent[2] = ui_accent_color[2];
                s_rainbow_was_on = true;
            }
            float t = (float)ImGui::GetTime() * 0.25f;
            ImGui::ColorConvertHSVtoRGB(t - (long)t, 1.0f, 1.0f,
                                        ui_accent_color[0], ui_accent_color[1], ui_accent_color[2]);
        } else if (s_rainbow_was_on) {
            s_rainbow_was_on = false;
            ui_accent_color[0] = s_prev_accent[0];
            ui_accent_color[1] = s_prev_accent[1];
            ui_accent_color[2] = s_prev_accent[2];
        }

        ImVec4 accent(ui_accent_color[0], ui_accent_color[1], ui_accent_color[2], 1.0f);
        st.Colors[ImGuiCol_Border]              = ImVec4(0.22f, 0.25f, 0.31f, 0.50f);
        st.Colors[ImGuiCol_CheckMark]           = accent;
        st.Colors[ImGuiCol_SliderGrab]          = ImVec4(accent.x, accent.y, accent.z, 0.55f);
        st.Colors[ImGuiCol_SliderGrabActive]    = accent;
        st.Colors[ImGuiCol_FrameBgHovered]      = ImVec4(accent.x, accent.y, accent.z, 0.18f);
        st.Colors[ImGuiCol_FrameBgActive]       = ImVec4(accent.x, accent.y, accent.z, 0.32f);
        st.Colors[ImGuiCol_Header]              = ImVec4(accent.x, accent.y, accent.z, 0.22f);
        st.Colors[ImGuiCol_HeaderHovered]       = ImVec4(accent.x, accent.y, accent.z, 0.45f);
        st.Colors[ImGuiCol_HeaderActive]        = accent;
        st.Colors[ImGuiCol_Separator]           = ImVec4(1.0f, 1.0f, 1.0f, 0.08f);
        st.Colors[ImGuiCol_SeparatorHovered]    = ImVec4(accent.x, accent.y, accent.z, 0.6f);
        st.Colors[ImGuiCol_SeparatorActive]     = accent;
        st.Colors[ImGuiCol_ButtonHovered]       = ImVec4(accent.x, accent.y, accent.z, 0.30f);
        st.Colors[ImGuiCol_ButtonActive]        = ImVec4(accent.x, accent.y, accent.z, 0.55f);
        st.Colors[ImGuiCol_ScrollbarGrab]       = ImVec4(accent.x, accent.y, accent.z, 0.30f);
        st.Colors[ImGuiCol_ScrollbarGrabHovered]= ImVec4(accent.x, accent.y, accent.z, 0.60f);
        st.Colors[ImGuiCol_ScrollbarGrabActive] = accent;
        st.Colors[ImGuiCol_ResizeGrip]          = ImVec4(accent.x, accent.y, accent.z, 0.30f);
        st.Colors[ImGuiCol_ResizeGripHovered]   = ImVec4(accent.x, accent.y, accent.z, 0.60f);
        st.Colors[ImGuiCol_ResizeGripActive]    = accent;
        st.Colors[ImGuiCol_TextSelectedBg]      = ImVec4(accent.x, accent.y, accent.z, 0.45f);
        st.Colors[ImGuiCol_NavHighlight]        = accent;
        st.Colors[ImGuiCol_PlotLines]           = accent;
        st.Colors[ImGuiCol_PlotHistogram]       = accent;
        st.Colors[ImGuiCol_DragDropTarget]      = accent;
        st.Colors[ImGuiCol_Tab]                 = ImVec4(0.12f, 0.13f, 0.19f, 0.80f);
        st.Colors[ImGuiCol_TabHovered]          = ImVec4(accent.x, accent.y, accent.z, 0.60f);
        st.Colors[ImGuiCol_TabActive]           = ImVec4(accent.x, accent.y, accent.z, 0.85f);

        // every background alpha scales with the transparency slider
        float a = 1.0f - (ui_transparency / 100.0f);
        st.Colors[ImGuiCol_WindowBg]        = ImVec4(0.047f, 0.055f, 0.071f, 0.98f * a);
        st.Colors[ImGuiCol_ChildBg]         = ImVec4(0.078f, 0.090f, 0.114f, 0.30f * a);
        st.Colors[ImGuiCol_PopupBg]         = ImVec4(0.086f, 0.098f, 0.125f, 0.99f * a);
        st.Colors[ImGuiCol_FrameBg]         = ImVec4(0.086f, 0.098f, 0.125f, 0.55f * a);
        st.Colors[ImGuiCol_FrameBgHovered]  = ImVec4(accent.x, accent.y, accent.z, 0.16f * a);
        st.Colors[ImGuiCol_FrameBgActive]   = ImVec4(accent.x, accent.y, accent.z, 0.30f * a);
        st.Colors[ImGuiCol_TitleBg]         = ImVec4(0.055f, 0.063f, 0.082f, 0.98f * a);
        st.Colors[ImGuiCol_TitleBgActive]   = ImVec4(0.055f, 0.063f, 0.082f, 0.98f * a);
        st.Colors[ImGuiCol_MenuBarBg]       = ImVec4(0.055f, 0.063f, 0.082f, 0.98f * a);
        st.Colors[ImGuiCol_Button]          = ImVec4(0.086f, 0.098f, 0.125f, 0.60f * a);
        st.Colors[ImGuiCol_ScrollbarBg]     = ImVec4(0.024f, 0.027f, 0.035f, 0.40f * a);
    }

    ImGui::SetNextWindowSize(ImVec2(780.0f, 560.0f), ImGuiCond_FirstUseEver);
    if (s_size_queued) {
        ImGui::SetNextWindowSize(s_queued_size, ImGuiCond_Always);
        s_size_queued = false;
    }
    if (s_minimized)
        ImGui::SetNextWindowSizeConstraints(ImVec2(200.0f, kTitleBarH), ImVec2(FLT_MAX, kTitleBarH));
    else
        ImGui::SetNextWindowSizeConstraints(ImVec2(560.0f, 400.0f), ImVec2(FLT_MAX, FLT_MAX));

    ImGui::Begin("roblox external", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    // ---- window card: shadow -> flat surface -> accent bloom -> border ----
    if (!s_minimized) {
        ImDrawList* d = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();
        ImVec2 ws = ImGui::GetWindowSize();
        float wr = ui_rounded_corners ? ui_corner_radius : 0.0f;
        ImVec2 b1(wp.x + ws.x, wp.y + ws.y);

        d->PushClipRectFullScreen();
        d->AddShadowRect(wp, b1, ui::Fade(ui::ColShade(), 0.60f), 28.0f,
                         ImVec2(0.0f, 10.0f), ImDrawFlags_None, wr);
        d->PopClipRect();

        // flat, near-opaque surface (web-app card) with a faint accent bloom
        d->AddRectFilled(wp, b1, ui::Fade(ui::ColBackdrop(), 0.98f), wr);
        ui::PlasmaBackdrop(d, ImVec2(wp.x + wr, wp.y + wr), ImVec2(b1.x - wr, b1.y - wr));

        d->AddRect(wp, b1, ui::Fade(ui::ColOutline(), 0.30f), wr, 0, 1.0f);
    }

    // ---- custom title bar ----
    {
        ImDrawList* d = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();
        ImVec2 ws = ImGui::GetWindowSize();
        float wr = ui_rounded_corners ? ui_corner_radius : 0.0f;
        const float bar_h = kTitleBarH;
        float t = ui::AnimT();

        ImVec2 a(wp.x, wp.y), b(wp.x + ws.x, wp.y + bar_h);

        // header gradient (rounded top corners)
        ui::Gradient(d, a, b, wr, ui::Fade(ui::ColHeader(), 0.42f), ui::Fade(ui::ColSurface(), 0.30f));
        d->AddLine(ImVec2(a.x + wr, b.y - 1), ImVec2(b.x - wr, b.y - 1), ui::Fade(ui::ColOutline(), 0.10f), 1.0f);

        // drag anywhere on the bar (left of the window controls) to move the window
        ImGui::SetCursorScreenPos(ImVec2(wp.x, wp.y));
        ImGui::InvisibleButton("##titlebar", ImVec2(ws.x - 150.0f, bar_h));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            ImGui::SetWindowPos(ImVec2(wp.x + delta.x, wp.y + delta.y));
        }

        // title + subtitle (the reference writes a heading and nothing else)
        d->AddText(ImVec2(wp.x + 16.0f, wp.y + 9.0f), ui::TXT_MAIN, "roblox external");
        d->AddText(ImVec2(wp.x + 16.0f, wp.y + 26.0f), ui::TXT_DIM, "usermode overlay  \xc2\xb7  v0.736");

        // status (dot + text, no chip box)
        {
            bool attached = g_base_address != 0;
            float pulse = 0.5f + 0.5f * sinf(t * 3.0f);
            ImU32 dot = attached ? IM_COL32((int)(60 + 120 * pulse), 225, (int)(90 + 60 * pulse), 255)
                                 : IM_COL32(240, 180, 60, 255);
            const char* st = attached ? "attached" : "waiting for roblox";
            ImVec2 ts = ImGui::CalcTextSize(st);
            float cx = wp.x + ws.x - 150.0f - ts.x - 16.0f;
            float cy = wp.y + (bar_h - 24.0f) * 0.5f;
            d->AddText(ImVec2(cx, cy + 4.0f), ui::TXT_DIM, st);
            d->AddCircleFilled(ImVec2(cx - 10.0f, cy + 12.0f), 4.0f, dot);
        }

        // window controls (quiet, reference-style)
        {
            const float btnw = 30.0f, btnh = 30.0f, gap = 8.0f;
            float x0 = wp.x + ws.x - btnw * 2.0f - gap - 14.0f;
            float y0 = wp.y + (bar_h - btnh) * 0.5f;

            ImGui::SetCursorScreenPos(ImVec2(x0, y0));
            if (ImGui::InvisibleButton("##min", ImVec2(btnw, btnh))) {
                if (s_minimized) {
                    // expand back to the size we had before collapsing
                    s_minimized = false;
                    s_queued_size = s_restore_size;
                    s_size_queued = true;
                } else {
                    // collapse to just the title bar
                    s_restore_size = ws;
                    s_minimized = true;
                    s_queued_size = ImVec2(ws.x, bar_h);
                    s_size_queued = true;
                }
            }
            bool mhov = ImGui::IsItemHovered();
            d->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + btnw, y0 + btnh),
                             mhov ? ui::Fade(ui::ColHovered(), 0.9f) : ui::Fade(ui::ColControl(), 0.7f), 9.0f);
            d->AddRect(ImVec2(x0, y0), ImVec2(x0 + btnw, y0 + btnh), ui::Fade(ui::ColOutline(), 0.12f), 9.0f, 0, 1.0f);
            if (s_minimized) {
                // restore glyph: a plus (click to expand)
                d->AddLine(ImVec2(x0 + 15, y0 + 9), ImVec2(x0 + 15, y0 + 21), ui::ColKnob(), 1.6f);
                d->AddLine(ImVec2(x0 + 9, y0 + 15), ImVec2(x0 + 21, y0 + 15), ui::ColKnob(), 1.6f);
            } else {
                // minimize glyph: a line (click to collapse)
                d->AddLine(ImVec2(x0 + 9, y0 + 15), ImVec2(x0 + 21, y0 + 15), ui::ColKnob(), 1.6f);
            }

            float x1 = x0 + btnw + gap;
            ImGui::SetCursorScreenPos(ImVec2(x1, y0));
            if (ImGui::InvisibleButton("##close", ImVec2(btnw, btnh))) g_request_exit = true;
            bool chov = ImGui::IsItemHovered();
            d->AddRectFilled(ImVec2(x1, y0), ImVec2(x1 + btnw, y0 + btnh),
                             chov ? IM_COL32(224, 74, 84, 240) : ui::Fade(ui::ColControl(), 0.7f), 9.0f);
            d->AddRect(ImVec2(x1, y0), ImVec2(x1 + btnw, y0 + btnh), ui::Fade(ui::ColOutline(), 0.12f), 9.0f, 0, 1.0f);
            d->AddLine(ImVec2(x1 + 10, y0 + 10), ImVec2(x1 + 20, y0 + 20), ui::ColKnob(), 1.6f);
            d->AddLine(ImVec2(x1 + 20, y0 + 10), ImVec2(x1 + 10, y0 + 20), ui::ColKnob(), 1.6f);
        }

        if (s_minimized) { ImGui::End(); return; }
    }

    // ---- top tab bar + content well ----
    static int   s_page    = 0;
    static float s_pill_x  = -1.0f;
    static float s_glow[8] = { 0.0f };

    const char* kPages[] = { "aimbot", "esp", "misc", "world", "keybinds", "ui", "config", "debug" };
    const int   n = IM_ARRAYSIZE(kPages);

    ImDrawList* d = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    ImVec2 ws = ImGui::GetWindowSize();

    const float bar_h    = 48.0f;
    const float footer_h = 28.0f;
    const float pad      = 12.0f;
    const float tab_h    = 34.0f;
    const float tab_y    = wp.y + bar_h + 10.0f;
    const float tab_gap  = 6.0f;
    const float tab_x0   = wp.x + pad;
    const float tab_w    = ((ws.x - pad * 2.0f) - tab_gap * (n - 1)) / n;

    // hover glow per tab
    for (int i = 0; i < n; ++i) {
        float tx = tab_x0 + i * (tab_w + tab_gap);
        ImGui::SetCursorScreenPos(ImVec2(tx, tab_y));
        ImGui::InvisibleButton(kPages[i], ImVec2(tab_w, tab_h));
        bool clicked = ImGui::IsItemClicked();
        bool hov = ImGui::IsItemHovered();
        if (clicked) s_page = i;
        ui::Ease(s_glow[i], (hov && i != s_page) ? 1.0f : 0.0f, 22.0f);
        if (s_glow[i] > 0.01f)
            d->AddRectFilled(ImVec2(tx, tab_y), ImVec2(tx + tab_w, tab_y + tab_h),
                             ui::Fade(ui::ColHovered(), 0.6f * s_glow[i]), 10.0f);
    }

    // accent pill sliding under the active tab (segmented-control style)
    {
        float target_x = tab_x0 + s_page * (tab_w + tab_gap);
        if (s_pill_x < 0.0f) s_pill_x = target_x;
        ui::Ease(s_pill_x, target_x, 20.0f);
        d->AddRectFilled(ImVec2(s_pill_x, tab_y), ImVec2(s_pill_x + tab_w, tab_y + tab_h),
                         ui::Accent(), 9.0f);
    }

    // tab labels
    for (int i = 0; i < n; ++i) {
        float tx = tab_x0 + i * (tab_w + tab_gap);
        ImVec2 sz = ImGui::CalcTextSize(kPages[i]);
        bool active = (i == s_page);
        float mix = active ? 1.0f : (s_glow[i] > 0.01f ? 0.55f : 0.0f);
        ImU32 col = ui::Mix(ui::TXT_DIM, ui::TXT_MAIN, mix);
        d->AddText(ImVec2(tx + (tab_w - sz.x) * 0.5f, tab_y + (tab_h - sz.y) * 0.5f), col, kPages[i]);
    }

    // ---- content well ----
    float well_x = wp.x + pad;
    float well_y = tab_y + tab_h + 10.0f;
    float well_w = ws.x - pad * 2.0f;
    float well_h = (wp.y + ws.y - footer_h - 8.0f) - well_y;
    if (well_h < 80.0f) well_h = 80.0f;
    ui::GlassPanel(d, ImVec2(well_x, well_y), ImVec2(well_x + well_w, well_y + well_h), 16.0f,
                   0.22f, 0.08f, false);

    ImGui::SetCursorScreenPos(ImVec2(well_x + 16.0f, well_y + 12.0f));
    ImGui::BeginChild("content", ImVec2(well_w - 32.0f, well_h - 24.0f), false,
                      ImGuiWindowFlags_NoBackground);
    ImGui::PushItemWidth(-1.0f);

        if (s_page == 0) {
            ui::PageHeader("aimbot", "locks the camera to the nearest target in fov");
            ui::Toggle("enabled", &aimbot_enabled);
            ImGui::Combo("aim type", &aimbot_aim_type, "camera\0mouse\0");
            ImGui::Combo("target bone", &aimbot_part, "head\0upper torso\0lower torso\0left hand\0right hand\0left foot\0right foot\0");
            ui::Toggle("sticky aim", &sticky_aim);
            ui::Toggle("prediction", &prediction_enabled);
            if (prediction_enabled) {
                ui::Slider("pred x", &prediction_x, 1.0f, 50.0f);
                ui::Slider("pred y", &prediction_y, 1.0f, 50.0f);
            }
            ui::Slider("smooth x", &smoothing_x, 2.0f, 20.0f, "%.1f");
            ui::Slider("smooth y", &smoothing_y, 2.0f, 20.0f, "%.1f");
            ui::Toggle("humanizer", &humanizer_enabled);
            if (humanizer_enabled) {
                ui::Slider("humanize amount", &humanizer_strength, 0.0f, 1.0f, "%.2f");
                ImGui::TextDisabled("reaction delay + curved, slightly jittery aim");
            }
            ui::Slider("fov size", &fov_size, 10.0f, 500.0f);
            ui::Toggle("show fov", &show_fov);
            ImGui::Separator();
            ui::Toggle("team check", &team_check);
            ui::Toggle("wall check", &wall_check);
            if (wall_check) ImGui::TextDisabled("won't lock through another player's body");
        }

        if (s_page == 1) {
            ui::PageHeader("esp", "player boxes, bars and extras");
            ui::Toggle("enabled", &esp_enabled);
            ImGui::Separator();
            ui::Toggle("box", &box_esp);
            if (box_esp) {
                ImGui::Combo("box style", &box_esp_type, "full\0corners\0");
                ui::Toggle("fill", &box_fill);
                if (box_fill) {
                    ui::Toggle("gradient", &box_fill_gradient);
                    if (box_fill_gradient) {
                        ui::Toggle("rotate", &box_fill_gradient_rotate);
                        ImGui::ColorEdit4("fill top", box_fill_top);
                        ImGui::ColorEdit4("fill bottom", box_fill_bottom);
                    }
                    ImGui::ColorEdit4("fill color", box_fill_top);
                }
                ImGui::ColorEdit4("box color", box_esp_color);
            }
            ui::Toggle("health bar", &healthbar);
            if (healthbar) ImGui::ColorEdit4("health bar color", healthbar_color);
            ui::Toggle("health text", &health_text);
            if (health_text) ImGui::ColorEdit4("health text color", health_text_color);
            ui::Toggle("name", &name);
            if (name) ImGui::ColorEdit4("name color", name_color);
            ui::Toggle("distance", &distance);
            if (distance) ImGui::ColorEdit4("distance color", distance_color);
            ui::Toggle("rig type", &rig_type);
            if (rig_type) ImGui::ColorEdit4("rig type color", rig_type_color);
            ui::Toggle("tool", &tool_esp);
            if (tool_esp) ImGui::ColorEdit4("tool color", tool_color);
            ui::Slider("render dist", &esp_render_distance, 0.0f, 2000.0f, "%.0f");
            ui::Toggle("team check", &team_check);
            ui::Toggle("wall check", &wall_check);
            if (wall_check) ImGui::TextDisabled("hides players occluded by another player");
            ImGui::Separator();
            ui::Toggle("skeleton", &skeleton_esp);
            if (skeleton_esp) ImGui::ColorEdit4("skeleton color", skeleton_color);
            ui::Toggle("aim viewer", &aimviewer);
            ui::Toggle("china hat", &chinahat);
            if (chinahat) ImGui::ColorEdit4("hat color", chinahat_color);
            ImGui::Separator();
            ui::Toggle("chams", &chams_enabled);
            if (chams_enabled) ImGui::ColorEdit4("chams color", chams_color);
            ImGui::Separator();
            ui::Toggle("expanded hitbox", &render_expanded_hitbox);
            if (render_expanded_hitbox) {
                ui::Toggle("hitbox expander", &hitbox_expander_enabled);
                ui::Slider("hitbox size", &hitbox_expander_value, 1.0f, 50.0f);
            }
        }

        if (s_page == 2) {
            ui::PageHeader("misc", "movement and player features");
            ImGui::TextDisabled("binds are optional - 'none' means always on while enabled");
            ImGui::Separator();
            ui::Toggle("noclip", &noclip_enabled);
            ImGui::Separator();
            ui::Toggle("walkspeed", &walkspeed_enabled);
            if (walkspeed_enabled) ui::Slider("speed", &walkspeed_value, 0.0f, 200.0f);
            ImGui::Separator();
            ui::Toggle("flight", &flight_enabled);
            if (flight_enabled) {
                ui::Slider("fly speed", &flight_value, 10.0f, 250.0f);
                ui::Toggle("hold instead of toggle", &flight_hold_mode);
                ImGui::TextDisabled("wasd = move, space = up, lshift/lctrl = down");
            }
            ImGui::Separator();
            ui::Toggle("click teleport", &click_teleport_enabled);
            if (click_teleport_enabled) {
                ui::Slider("tp distance", &click_teleport_distance, 5.0f, 200.0f, "%.0f studs");
                ImGui::TextDisabled("left-click in-game to teleport there (no keybind)");
            }
            ImGui::Separator();
            ui::Toggle("infinite jump", &infinite_jump_enabled);
            if (infinite_jump_enabled) {
                ui::Slider("jump power", &infinite_jump_power, 25.0f, 150.0f);
                ImGui::TextDisabled("tap space in mid-air to jump again");
            }
            ImGui::Separator();
            ui::Toggle("fov changer", &fov_changer_enabled);
            if (fov_changer_enabled) ui::Slider("field of view", &fov_value, 20.0f, 120.0f);
            ImGui::Separator();
            ui::Toggle("inventory checker", &inventory_checker_enabled);
            if (inventory_checker_enabled) ImGui::TextDisabled("cursor over a player (or hold the key if bound)");
            ImGui::Separator();
        }

        if (s_page == 3) {
            ui::PageHeader("world", "replace the game's skybox");
            ui::Toggle("skybox changer", &skybox_changer_enabled);
            if (skybox_changer_enabled) {
                ImGui::Combo("skybox", &skybox_type,
                    "Piss\0Peach\0Saku\0Purple\0Retro\0Space\0Sea\0Night V2\0"
                    "Dark\0Anime\0Beach\0Space V2\0Pink\0Rainbow\0Forest\0Night\0"
                    "Lava\0Rainy\0Green\0Volcanic\0Minecraft\0Lucid\0Nebulous\0");
                if (skybox_debug_msg[0]) {
                    ImGui::TextWrapped("%s", skybox_debug_msg);
                }
            }
        }

        if (s_page == 4) {
            ui::PageHeader("keybinds", "click a bind then press any key or mouse button");
            ImGui::Separator();

            ImGui::Text("menu");
            keybind_button("toggle menu", menu_toggle_keybind);
            if (menu_toggle_keybind == 0)
                ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "unbound - you won't be able to reopen the menu!");
            ImGui::Separator();

            ImGui::Text("combat");
            keybind_button("aimbot", aimbot_keybind);
            ImGui::Separator();

            ImGui::Text("movement");
            keybind_button("noclip", noclip_keybind);
            keybind_button("walkspeed", walkspeed_keybind);
            keybind_button("flight", flight_keybind);
            keybind_button("inventory checker", inventory_checker_keybind);
            ImGui::Separator();

            ImGui::TextDisabled("leave a bind as 'none' to keep the feature");
            ImGui::TextDisabled("always on; set a key to hold-to-use instead.");
        }

        if (s_page == 7) {
            ui::PageHeader("debug", "diagnostics, logs and offset write tests");
            ImGui::TextWrapped("if a feature does nothing, check these values. "
                               "0x0 or 'INVALID' means that offset is wrong for your client version.");
            ImGui::Separator();

            ImGui::Text("base address   : 0x%llX", (unsigned long long)g_base_address);

            instance ve = read<instance>(g_base_address + Offsets::VisualEngine::Pointer);
            ImGui::Text("visual engine  : 0x%llX %s", (unsigned long long)ve.address,
                        ve.is_valid() ? "" : "<- INVALID");

            instance dm = game::ReadDatamodel(g_base_address);
            ImGui::Text("datamodel      : 0x%llX %s", (unsigned long long)dm.address,
                        dm.is_valid() ? "" : "<- INVALID");

            if (dm.is_valid()) {
                ImGui::Text("game name      : %s", dm.get_name().c_str());
                uint64_t place_id = read<uint64_t>(dm.address + Offsets::DataModel::PlaceId);
                bool loaded = read<bool>(dm.address + Offsets::DataModel::GameLoaded);
                ImGui::Text("place id       : %llu", (unsigned long long)place_id);
                ImGui::Text("game loaded    : %s", loaded ? "yes" : "no");

                if (place_id == 0) {
                    ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1),
                        "you are not in a game yet (home page / menu).\n"
                        "join an actual experience - there is no world or\n"
                        "player list to read until then.");
                }
            }

            ImGui::Separator();

            if (ve.is_valid()) {
                float view[16]{};
                float dims[2]{};
                read_raw(ve.address + Offsets::VisualEngine::ViewMatrix, view, sizeof(view));
                read_raw(ve.address + Offsets::VisualEngine::Dimensions, dims, sizeof(dims));

                ImGui::Text("viewport       : %.0f x %.0f %s", dims[0], dims[1],
                            (dims[0] > 0.0f && dims[1] > 0.0f) ? "" : "<- INVALID (Dimensions offset wrong)");
                ImGui::Text("view matrix    : %.2f %.2f %.2f %.2f", view[0], view[1], view[2], view[3]);
                ImGui::Text("                 %.2f %.2f %.2f %.2f", view[4], view[5], view[6], view[7]);

                bool all_zero = true;
                for (int i = 0; i < 16; ++i) if (view[i] != 0.0f) { all_zero = false; break; }
                if (all_zero) ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1),
                                                 "view matrix is all zero - ViewMatrix offset is wrong");
            }

            ImGui::Separator();

            const cache::LocalPlayerData& lp = cache::GetLocalPlayer();
            ImGui::Text("local player   : %s", lp.valid ? "ok" : "INVALID");
            ImGui::Text("humanoid       : 0x%llX", (unsigned long long)lp.humanoid_address);
            ImGui::Text("hrp primitive  : 0x%llX", (unsigned long long)lp.hrp_primitive);
            ImGui::Text("local pos      : %.1f, %.1f, %.1f", lp.x, lp.y, lp.z);

            auto ents_snap = cache::GetEspSnapshot();
            const auto& ents = *ents_snap;
            ImGui::Text("cached players : %d", (int)ents.size());
            if (ents.empty())
                ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1), "no other players cached (join a populated server)");

            ImGui::Separator();
            ui::Section("write test");
            ImGui::TextWrapped("flight / click teleport write to the root part's Primitive. "
                               "this proves whether those writes actually land.");

            if (is_valid_address(lp.humanoid_address)) {
                uintptr_t probe = read<uintptr_t>(lp.humanoid_address + Offsets::Humanoid::HumanoidRootPart);
                ImGui::Text("hrp via 0x478   : 0x%llX %s",
                            (unsigned long long)probe,
                            is_valid_address(probe) ? "" : "(unused - offset is wrong)");
            }

            uintptr_t prim = lp.hrp_primitive;
            ImGui::Text("hrp primitive   : 0x%llX %s",
                        (unsigned long long)prim,
                        is_valid_address(prim) ? "" : "<- INVALID");

            if (is_valid_address(prim)) {
                float pos[3] = {};
                read_raw(prim + Offsets::Primitive::Position, pos, sizeof(pos));
                ImGui::Text("position read   : %.1f, %.1f, %.1f", pos[0], pos[1], pos[2]);

                float vel[3] = {};
                read_raw(prim + Offsets::Primitive::AssemblyLinearVelocity, vel, sizeof(vel));
                ImGui::Text("velocity read   : %.1f, %.1f, %.1f", vel[0], vel[1], vel[2]);

                float psz[3] = {};
                read_raw(prim + Offsets::Primitive::Size, psz, sizeof(psz));
                ImGui::Text("part size read  : %.2f, %.2f, %.2f %s", psz[0], psz[1], psz[2],
                            (psz[0] > 0.05f || psz[1] > 0.05f) ? "" : "<- zero, esp uses canonical sizes");

                static char test_result[192] = "not run yet";

                if (ImGui::Button("test POSITION write (+10 studs up)", ImVec2(-1, 0))) {
                    float b1[3] = {}, b2[3] = {};
                    read_raw(prim + Offsets::Primitive::Position,  b1, sizeof(b1));
                    read_raw(prim + Offsets::Primitive::Position2, b2, sizeof(b2));
                    float target[3] = { b1[0], b1[1] + 10.0f, b1[2] };
                    bool w1 = write_raw(prim + Offsets::Primitive::Position,  target, sizeof(target));
                    bool w2 = write_raw(prim + Offsets::Primitive::Position2, target, sizeof(target));
                    float a1[3] = {}, a2[3] = {};
                    read_raw(prim + Offsets::Primitive::Position,  a1, sizeof(a1));
                    read_raw(prim + Offsets::Primitive::Position2, a2, sizeof(a2));
                    snprintf(test_result, sizeof(test_result),
                             "POS(0xEC): wpm=%s dY %.2f | POS2(0x134): wpm=%s dY %.2f",
                             w1 ? "ok" : "FAIL", a1[1] - b1[1],
                             w2 ? "ok" : "FAIL", a2[1] - b2[1]);
                    LogLine("%s", test_result);
                }

                if (ImGui::Button("test VELOCITY write (launch up)", ImVec2(-1, 0))) {
                    float v[3] = { 0.0f, 100.0f, 0.0f };
                    bool wrote = write_raw(prim + Offsets::Primitive::AssemblyLinearVelocity, v, sizeof(v));
                    float back[3] = {};
                    read_raw(prim + Offsets::Primitive::AssemblyLinearVelocity, back, sizeof(back));
                    snprintf(test_result, sizeof(test_result),
                             "VEL: wpm=%s wrote y=100 read back y=%.2f %s",
                             wrote ? "ok" : "FAILED", back[1],
                             (back[1] > 50.0f) ? "LANDED" : "did NOT stick");
                    LogLine("%s", test_result);
                }

                if (ImGui::Button("probe primitive floats (+0xA0..+0x1C0)", ImVec2(-1, 0))) {
                    LogLine("--- primitive float probe (hrp 0x%llX) ---", (unsigned long long)prim);
                    char line[128];
                    for (int off = 0xA0; off <= 0x1C0; off += 0x10) {
                        float vals[4] = {};
                        read_raw(prim + off, vals, sizeof(vals));
                        snprintf(line, sizeof(line), "  +0x%03X: %9.2f %9.2f %9.2f %9.2f",
                                 off, vals[0], vals[1], vals[2], vals[3]);
                        LogLine("%s", line);
                    }
                    LogLine("--- probe end - send this to find Position/Size ---");
                }

                ImGui::TextWrapped("%s", test_result);
            } else {
                ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1),
                                   "root part not resolved - spawn in, or names are failing");
            }

            ImGui::Separator();
            ui::Section("log");
            if (ImGui::Button("clear", ImVec2(80, 0))) {
                std::lock_guard<std::mutex> lock(g_log_mutex);
                g_log_lines.clear();
            }
            ImGui::SameLine();
            if (ImGui::Button("copy", ImVec2(80, 0))) {
                std::lock_guard<std::mutex> lock(g_log_mutex);
                std::string all;
                for (const std::string& l : g_log_lines) { all += l; all += "\n"; }
                ImGui::SetClipboardText(all.c_str());
            }
            ImGui::SameLine();
            ImGui::TextDisabled("paste it back in chat");
            ImGui::Separator();
            ImGui::BeginChild("logscroll", ImVec2(0, 200), true);
            {
                std::lock_guard<std::mutex> lock(g_log_mutex);
                for (const std::string& l : g_log_lines)
                    ImGui::TextUnformatted(l.c_str());
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
                ImGui::SetScrollHereY(1.0f);
            ImGui::EndChild();
        }

        if (s_page == 5) {
            ui::PageHeader("ui", "theme and window look");
            ui::Section("window");
            ui::Slider("menu transparency", &ui_transparency, 0.0f, 90.0f, "%.0f%%");
            ui::Toggle("rounded corners", &ui_rounded_corners);
            if (ui_rounded_corners) ui::Slider("corner radius", &ui_corner_radius, 0.0f, 24.0f, "%.0f px");
            ImGui::Dummy(ImVec2(0, 4));

            ui::Section("accent");
            ImGui::ColorEdit3("accent color", ui_accent_color);
            ui::Toggle("rainbow accent", &ui_rainbow);
            ImGui::Dummy(ImVec2(0, 4));

            ui::Section("reset");
            if (ImGui::Button("reset to default theme", ImVec2(-1, 0))) {
                ui_transparency = 6.0f;
                ui_rounded_corners = true;
                ui_corner_radius = 16.0f;
                ui_rainbow = false;
                ui_accent_color[0] = 0.78f; ui_accent_color[1] = 0.08f; ui_accent_color[2] = 0.08f;
            }
        }

        if (s_page == 6) {
            static char config_name_buf[128] = "";
            static char rename_buf[128] = "";
            static std::vector<std::string> config_list = config::GetConfigList();
            static int selected_config = -1;

            ui::PageHeader("config", "save and load named presets");

            ImGui::InputText("config name", config_name_buf, sizeof(config_name_buf));

            if (ImGui::Button("Save", ImVec2(-1, 0))) {
                if (config_name_buf[0] != '\0') {
                    config::Save(config_name_buf);
                    config_list = config::GetConfigList();
                }
            }

            if (ImGui::Button("Load", ImVec2(-1, 0))) {
                if (config_name_buf[0] != '\0') {
                    config::Load(config_name_buf);
                }
            }

            if (ImGui::Button("Delete", ImVec2(-1, 0))) {
                if (config_name_buf[0] != '\0') {
                    config::Delete(config_name_buf);
                    config_list = config::GetConfigList();
                    selected_config = -1;
                }
            }

            ImGui::Separator();
            ImGui::InputText("rename to", rename_buf, sizeof(rename_buf));
            if (ImGui::Button("Rename", ImVec2(-1, 0))) {
                if (config_name_buf[0] != '\0' && rename_buf[0] != '\0') {
                    config::Rename(config_name_buf, rename_buf);
                    config_list = config::GetConfigList();
                    strncpy_s(config_name_buf, rename_buf, sizeof(config_name_buf) - 1);
                }
            }

            ImGui::Separator();
            if (ImGui::Button("Open Config Folder", ImVec2(-1, 0))) {
                config::OpenConfigFolder();
            }

            ImGui::Separator();
            ImGui::Text("saved configs:");

            if (ImGui::Button("Refresh List", ImVec2(-1, 0))) {
                config_list = config::GetConfigList();
                selected_config = -1;
            }

            ImGui::BeginChild("config_list", ImVec2(-1, 150), true);
            for (int i = 0; i < (int)config_list.size(); ++i) {
                bool is_selected = (selected_config == i);
                if (ImGui::Selectable(config_list[i].c_str(), is_selected)) {
                    selected_config = i;
                    strncpy_s(config_name_buf, config_list[i].c_str(), sizeof(config_name_buf) - 1);
                }
            }
            ImGui::EndChild();
        }

    ImGui::PopItemWidth();
    ImGui::EndChild();

    // ---- footer status bar ----
    {
        ImDrawList* fd = ImGui::GetWindowDrawList();
        float fx = well_x;
        float fy = wp.y + ws.y - footer_h;
        float fw = (wp.x + ws.x - 16.0f) - fx;
        fd->AddLine(ImVec2(fx, fy), ImVec2(fx + fw, fy), ui::Fade(ui::ColOutline(), 0.10f), 1.0f);

        char left[96];
        if (g_base_address) {
            snprintf(left, sizeof(left), "attached  pid %u", (unsigned)mem::process_id.load());
        } else {
            snprintf(left, sizeof(left), "waiting for roblox...");
        }
        fd->AddText(ImVec2(fx, fy + 7), ui::TXT_DIM, left);

        char right[96];
        snprintf(right, sizeof(right), "%s toggles menu", KeyName(menu_toggle_keybind));
        ImVec2 rts = ImGui::CalcTextSize(right);
        fd->AddText(ImVec2(fx + fw - rts.x, fy + 7), ui::TXT_DIM, right);
    }

    ImGui::End();
}
