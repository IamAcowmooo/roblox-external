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
// liquid glass UI toolkit.
//
// No backdrop blur is available on a transparent DX11 overlay, so the
// "liquid glass" look is faked with layered translucent gradients:
//   - a bright top catch-light and a soft bottom reflection,
//   - a deep vertical gradient so panels read as glass, not flat colour,
//   - soft drop shadows + accent bloom (layered translucent halos),
//   - a slow animated shimmer so the surface feels alive.
// Every widget is driven by the live accent + transparency from the ui page.
// ------------------------------------------------------------------
namespace ui {
    static const ImU32 OFF_TRACK = IM_COL32(40, 43, 60, 255);
    static const ImU32 TXT_MAIN  = IM_COL32(228, 231, 242, 255);
    static const ImU32 TXT_DIM   = IM_COL32(143, 146, 160, 255);
    static const ImU32 WHITE     = IM_COL32(255, 255, 255, 255);

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
    inline ImU32 AccentLight(float alpha = 1.0f) {
        return Mix(Accent(alpha), IM_COL32(255, 255, 255, (int)(alpha * 255.0f)), 0.36f);
    }
    inline ImU32 AccentDeep(float alpha = 1.0f) {
        return IM_COL32((int)(ui_accent_color[0] * 0.55f * 255.0f),
                        (int)(ui_accent_color[1] * 0.55f * 255.0f),
                        (int)(ui_accent_color[2] * 0.55f * 255.0f),
                        (int)(alpha * 255.0f));
    }

    inline ImU32 PanelBase()  { return IM_COL32(26, 29, 44, (int)(118.0f * GlassA())); }
    inline ImU32 GlassTop()   { return IM_COL32(30, 33, 50, (int)(150.0f * GlassA())); }
    inline ImU32 GlassDeep()  { return IM_COL32(15, 17, 27, (int)(165.0f * GlassA())); }

    // ---- minimal vector glyphs for the left rail ----
    enum RailIconKind { RK_AIMBOT, RK_ESP, RK_MISC, RK_WORLD, RK_KEYS, RK_UI, RK_CONFIG, RK_DEBUG };

    inline void RailIcon(ImDrawList* d, const ImVec2& c, float s, int kind, ImU32 col) {
        const float th = 1.7f;
        switch (kind) {
        case RK_AIMBOT: {
            d->AddCircle(c, s * 0.72f, col, 40, th);
            d->AddLine(ImVec2(c.x, c.y - s * 0.84f), ImVec2(c.x, c.y - s * 0.34f), col, th);
            d->AddLine(ImVec2(c.x, c.y + s * 0.34f), ImVec2(c.x, c.y + s * 0.84f), col, th);
            d->AddLine(ImVec2(c.x - s * 0.84f, c.y), ImVec2(c.x - s * 0.34f, c.y), col, th);
            d->AddLine(ImVec2(c.x + s * 0.34f, c.y), ImVec2(c.x + s * 0.84f, c.y), col, th);
            d->AddCircleFilled(c, s * 0.10f, col);
            break;
        }
        case RK_ESP: {
            d->AddEllipse(c, ImVec2(s * 0.85f, s * 0.55f), col, 0.0f, 40, th);
            d->AddCircleFilled(c, s * 0.30f, col);
            break;
        }
        case RK_MISC: {
            ImVec2 p[6];
            p[0] = ImVec2(c.x + s * 0.12f, c.y - s * 0.95f);
            p[1] = ImVec2(c.x - s * 0.45f, c.y + s * 0.10f);
            p[2] = ImVec2(c.x - s * 0.02f, c.y + s * 0.10f);
            p[3] = ImVec2(c.x - s * 0.18f, c.y + s * 0.95f);
            p[4] = ImVec2(c.x + s * 0.42f, c.y - s * 0.10f);
            p[5] = ImVec2(c.x - s * 0.02f, c.y - s * 0.10f);
            d->AddConvexPolyFilled(p, 6, col);
            break;
        }
        case RK_WORLD: {
            d->AddCircle(c, s * 0.80f, col, 40, th);
            d->AddEllipse(c, ImVec2(s * 0.40f, s * 0.80f), col, 0.0f, 36, th);
            d->AddLine(ImVec2(c.x - s * 0.80f, c.y), ImVec2(c.x + s * 0.80f, c.y), col, th);
            break;
        }
        case RK_KEYS: {
            d->AddRect(ImVec2(c.x - s * 0.80f, c.y - s * 0.60f),
                       ImVec2(c.x + s * 0.80f, c.y + s * 0.60f), col, s * 0.18f, 0, th);
            for (int r = 0; r < 2; ++r)
                for (int q = 0; q < 4; ++q)
                    d->AddCircleFilled(ImVec2(c.x - s * 0.45f + q * s * 0.30f,
                                              c.y - s * 0.22f + r * s * 0.44f), s * 0.07f, col);
            break;
        }
        case RK_UI: {
            for (int r = 0; r < 3; ++r) {
                float y = c.y - s * 0.60f + r * s * 0.60f;
                d->AddLine(ImVec2(c.x - s * 0.80f, y), ImVec2(c.x + s * 0.80f, y), col, th);
                float kx = c.x - s * 0.80f + (0.35f + 0.25f * (float)r) * s * 1.6f;
                d->AddCircleFilled(ImVec2(kx, y), s * 0.16f, col);
            }
            break;
        }
        case RK_CONFIG: {
            d->AddRect(ImVec2(c.x - s * 0.72f, c.y - s * 0.80f),
                       ImVec2(c.x + s * 0.72f, c.y + s * 0.80f), col, s * 0.20f, 0, th);
            d->AddRectFilled(ImVec2(c.x, c.y - s * 0.80f),
                             ImVec2(c.x + s * 0.72f, c.y - s * 0.28f), col, s * 0.20f, ImDrawFlags_RoundCornersTopRight);
            d->AddRectFilled(ImVec2(c.x - s * 0.42f, c.y + s * 0.42f),
                             ImVec2(c.x + s * 0.42f, c.y + s * 0.58f), col, s * 0.08f);
            break;
        }
        case RK_DEBUG: {
            d->AddCircle(ImVec2(c.x - s * 0.10f, c.y - s * 0.15f), s * 0.50f, col, 40, th);
            d->AddLine(ImVec2(c.x + s * 0.26f, c.y + s * 0.05f), ImVec2(c.x + s * 0.72f, c.y + s * 0.52f), col, th);
            break;
        }
        default: break;
        }
    }

    // fake bloom: several translucent accent halos behind an element
    inline void AccentHalo(ImDrawList* d, const ImVec2& a, const ImVec2& b, float r, float strength = 1.0f) {
        d->AddShadowRect(a, b, Accent((int)(70.0f * strength) / 255.0f), 26.0f, ImVec2(0, 0), ImDrawFlags_None, r);
        d->AddShadowRect(a, b, Accent((int)(38.0f * strength) / 255.0f), 12.0f, ImVec2(0, 0), ImDrawFlags_None, r);
    }

    // rounded vertical gradient (AddRectFilledMultiColor has no rounding, so we
    // stack three rounded bands instead - the seams are straight horizontal lines)
    inline void RoundedGradient(ImDrawList* d, const ImVec2& a, const ImVec2& b, float r,
                                ImU32 top, ImU32 mid, ImU32 bottom) {
        d->AddRectFilled(a, b, bottom, r);
        float h = b.y - a.y;
        if (h > 6.0f) {
            d->AddRectFilled(a, ImVec2(b.x, a.y + h * 0.62f), mid, r, ImDrawFlags_RoundCornersTop);
            d->AddRectFilled(a, ImVec2(b.x, a.y + h * 0.30f), top, r, ImDrawFlags_RoundCornersTop);
        }
    }

    // the core liquid glass surface: shadow + vertical gradient + catch-light +
    // bottom reflection + hairline border (+ optional accent bloom + shimmer sweep)
    inline void LiquidPanel(ImDrawList* d, const ImVec2& a, const ImVec2& b, float r,
                            ImU32 base, ImU32 border, bool shadow, float bloom = 0.0f, bool shimmer = false) {
        if (shadow)
            d->AddShadowRect(a, b, IM_COL32(0, 0, 0, 120), 18.0f, ImVec2(0.0f, 5.0f), ImDrawFlags_None, r);

        if (bloom > 0.01f)
            AccentHalo(d, a, b, r, bloom);

        ImU32 top    = Mix(base, IM_COL32(255, 255, 255, 26), 0.55f);
        ImU32 mid    = base;
        ImU32 bottom = Mix(base, IM_COL32(6, 7, 13, 120), 0.55f);
        RoundedGradient(d, a, b, r, top, mid, bottom);

        float h = b.y - a.y;
        if (h > 6.0f) {
            // bright catch-light across the top edge
            d->AddLine(ImVec2(a.x + r, a.y + 0.5f), ImVec2(b.x - r, a.y + 0.5f),
                       IM_COL32(255, 255, 255, 46), 1.0f);
            // soft top glow band
            d->AddRectFilled(a, ImVec2(b.x, a.y + h * 0.30f),
                             IM_COL32(255, 255, 255, 8), r, ImDrawFlags_RoundCornersTop);
            // faint reflection along the bottom
            d->AddLine(ImVec2(a.x + r, b.y - 1.0f), ImVec2(b.x - r, b.y - 1.0f),
                       IM_COL32(255, 255, 255, 18), 1.0f);
        }

        if (shimmer) {
            float t = AnimT();
            float sw = (b.x - a.x);
            float sx = a.x + fmodf(t * 34.0f, sw + 240.0f) - 120.0f;
            d->AddLine(ImVec2(sx, a.y), ImVec2(sx + 90.0f, b.y),
                       IM_COL32(255, 255, 255, 9), 1.0f);
        }

        d->AddRect(a, b, border, r, 0, 1.0f);
        d->AddRect(ImVec2(a.x + 0.5f, a.y + 0.5f), ImVec2(b.x - 0.5f, b.y - 0.5f),
                   IM_COL32(255, 255, 255, 9), r, 0, 1.0f);
    }

    // section header: accent tick + letter-spaced caption + fading rule
    inline void Section(const char* text) {
        ImGui::Dummy(ImVec2(0, 7));
        ImVec2 p = ImGui::GetCursorScreenPos();
        float h = ImGui::GetTextLineHeight();
        float full = ImGui::GetContentRegionAvail().x;
        ImDrawList* d = ImGui::GetWindowDrawList();

        d->AddRectFilled(ImVec2(p.x, p.y + 2), ImVec2(p.x + 3, p.y + h - 2), Accent(), 1.5f);
        d->AddCircleFilled(ImVec2(p.x + 1.5f, p.y + h * 0.5f), 6.0f, Accent(0.28f));

        // letter-spaced uppercase caption
        char up[128];
        size_t n = 0;
        for (const char* c = text; *c && n < sizeof(up) - 1; ++c)
            up[n++] = (char)toupper((unsigned char)*c);
        up[n] = '\0';

        char spaced[256];
        size_t m = 0;
        for (size_t i = 0; i < n && m < sizeof(spaced) - 2; ++i) {
            spaced[m++] = up[i];
            if (i + 1 < n) { spaced[m++] = ' '; }
        }
        spaced[m] = '\0';

        ImGui::SetCursorScreenPos(ImVec2(p.x + 13, p.y));
        ImGui::TextColored(ImVec4(0.90f, 0.91f, 0.96f, 1.0f), "%s", spaced);

        float tx = p.x + 13.0f + ImGui::CalcTextSize(spaced).x + 12.0f;
        if (full - tx > 10.0f)
            d->AddRectFilledMultiColor(ImVec2(tx, p.y + h * 0.5f), ImVec2(p.x + full, p.y + h * 0.5f + 1.0f),
                                       Accent(0.30f), Accent(0.30f), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
        ImGui::Dummy(ImVec2(0, 5));
    }

    // page title + subtitle + animated accent rule
    inline void PageHeader(const char* title, const char* subtitle) {
        ImGui::TextColored(ImVec4(0.97f, 0.97f, 1.0f, 1.0f), "%s", title);
        if (subtitle && subtitle[0])
            ImGui::TextDisabled("%s", subtitle);

        ImVec2 p = ImGui::GetCursorScreenPos();
        float full = ImGui::GetContentRegionAvail().x;
        ImDrawList* d = ImGui::GetWindowDrawList();
        d->AddRectFilledMultiColor(ImVec2(p.x, p.y), ImVec2(p.x + full, p.y + 2.0f),
                                   Accent(0.95f), AccentLight(0.95f), Accent(0.0f), Accent(0.0f));
        // travelling highlight on the rule
        float t = AnimT();
        float sx = p.x + fmodf(t * 60.0f, full + 120.0f) - 60.0f;
        d->AddRectFilled(ImVec2(sx, p.y), ImVec2(sx + 30.0f, p.y + 2.0f), IM_COL32(255, 255, 255, 70));
        ImGui::Dummy(ImVec2(0, 9));
    }

    // pill switch with a glowing, sliding knob
    inline bool Toggle(const char* label, bool* v) {
        float full = ImGui::GetContentRegionAvail().x;
        float h = ROW_H;
        ImVec2 p = ImGui::GetCursorScreenPos();

        ImGui::InvisibleButton(label, ImVec2(full, h));
        bool pressed = ImGui::IsItemClicked();
        if (pressed) *v = !*v;
        bool hov = ImGui::IsItemHovered();

        ImDrawList* d = ImGui::GetWindowDrawList();
        LiquidPanel(d, p, ImVec2(p.x + full, p.y + h), ROUND, PanelBase(),
                    hov ? Accent(0.85f) : Accent(0.30f), false, *v ? 0.55f : 0.0f, false);

        d->AddText(ImVec2(p.x + PAD_X, p.y + (h - ImGui::GetTextLineHeight()) * 0.5f),
                   *v ? TXT_MAIN : TXT_DIM, label);

        float tw = 42.0f, th = 22.0f;
        float tx = p.x + full - tw - 12.0f;
        float ty = p.y + (h - th) * 0.5f;

        // track
        d->AddRectFilled(ImVec2(tx, ty), ImVec2(tx + tw, ty + th),
                         *v ? Accent() : OFF_TRACK, th * 0.5f);
        if (*v) {
            d->AddRectFilled(ImVec2(tx, ty), ImVec2(tx + tw, ty + th * 0.5f),
                             AccentLight(0.65f), th * 0.5f, ImDrawFlags_RoundCornersTop);
            AccentHalo(d, ImVec2(tx, ty), ImVec2(tx + tw, ty + th), th * 0.5f, 0.8f);
        }

        // knob
        float kr = 8.0f;
        float kx = *v ? (tx + tw - kr - 3.0f) : (tx + kr + 3.0f);
        float ky = ty + th * 0.5f;
        if (hov) d->AddCircleFilled(ImVec2(kx, ky), kr + 3.0f, Accent(0.22f));
        d->AddCircleFilled(ImVec2(kx, ky), kr, WHITE);
        d->AddCircleFilled(ImVec2(kx, ky - 2.0f), kr * 0.55f, IM_COL32(255, 255, 255, 60));
        d->AddCircle(ImVec2(kx, ky), kr, *v ? AccentLight(0.9f) : IM_COL32(255, 255, 255, 30), 24, 1.0f);

        ImGui::Dummy(ImVec2(0, GAP - 2));
        return pressed;
    }

    // label + right-aligned value + glowing fill track
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
        LiquidPanel(d, p, ImVec2(p.x + full, p.y + h), ROUND, PanelBase(),
                    (hov || active) ? Accent(0.9f) : Accent(0.30f), false,
                    (hov || active) ? 0.45f : 0.0f, false);

        d->AddText(ImVec2(p.x + PAD_X, p.y + 8), TXT_MAIN, label);

        char buf[64];
        snprintf(buf, sizeof(buf), fmt, *v);
        ImVec2 ts = ImGui::CalcTextSize(buf);
        d->AddText(ImVec2(p.x + full - ts.x - PAD_X, p.y + 8), Accent(), buf);

        // track groove
        d->AddRectFilled(ImVec2(trx, try_), ImVec2(trx + trw, try_ + 5), OFF_TRACK, 2.5f);
        float ratio = (mx - mn) != 0.0f ? (*v - mn) / (mx - mn) : 0.0f;
        if (ratio < 0) ratio = 0; if (ratio > 1) ratio = 1;
        float fillw = trw * ratio;
        if (fillw > 0.0f) {
            d->AddRectFilled(ImVec2(trx, try_), ImVec2(trx + fillw, try_ + 5), Accent(), 2.5f);
            d->AddRectFilled(ImVec2(trx, try_), ImVec2(trx + fillw, try_ + 2.5f),
                             AccentLight(0.55f), 2.5f, ImDrawFlags_RoundCornersTop);
            // travelling spark on the fill
            float t = AnimT();
            float sx = trx + fmodf(t * 70.0f, fillw + 60.0f) - 30.0f;
            d->AddRectFilled(ImVec2(sx, try_), ImVec2(sx + 22.0f, try_ + 5), IM_COL32(255, 255, 255, 46), 2.5f);
        }

        // knob
        float kx = trx + fillw;
        float ky = try_ + 2.5f;
        float kr = (active || hov) ? 7.0f : 5.5f;
        if (active || hov) d->AddCircleFilled(ImVec2(kx, ky), kr + 4.0f, Accent(0.25f));
        d->AddCircleFilled(ImVec2(kx, ky), kr, WHITE);
        d->AddCircleFilled(ImVec2(kx, ky - 1.5f), kr * 0.5f, IM_COL32(255, 255, 255, 70));
        d->AddCircle(ImVec2(kx, ky), kr, Accent(0.8f), 20, 1.2f);

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
        st.Colors[ImGuiCol_Border]              = ImVec4(accent.x, accent.y, accent.z, 0.38f);
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
        st.Colors[ImGuiCol_WindowBg]        = ImVec4(0.055f, 0.060f, 0.090f, 0.90f * a);
        st.Colors[ImGuiCol_ChildBg]         = ImVec4(0.10f, 0.11f, 0.16f, 0.16f * a);
        st.Colors[ImGuiCol_PopupBg]         = ImVec4(0.040f, 0.043f, 0.066f, 0.96f * a);
        st.Colors[ImGuiCol_FrameBg]         = ImVec4(0.16f, 0.17f, 0.24f, 0.45f * a);
        st.Colors[ImGuiCol_FrameBgHovered]  = ImVec4(accent.x, accent.y, accent.z, 0.18f * a);
        st.Colors[ImGuiCol_FrameBgActive]   = ImVec4(accent.x, accent.y, accent.z, 0.32f * a);
        st.Colors[ImGuiCol_TitleBg]         = ImVec4(0.040f, 0.043f, 0.066f, 0.96f * a);
        st.Colors[ImGuiCol_TitleBgActive]   = ImVec4(0.040f, 0.043f, 0.066f, 0.96f * a);
        st.Colors[ImGuiCol_MenuBarBg]       = ImVec4(0.040f, 0.043f, 0.066f, 0.96f * a);
        st.Colors[ImGuiCol_Button]          = ImVec4(0.16f, 0.17f, 0.24f, 0.55f * a);
        st.Colors[ImGuiCol_ScrollbarBg]     = ImVec4(0.02f, 0.02f, 0.04f, 0.40f * a);
    }

    ImGui::SetNextWindowSize(ImVec2(780.0f, 560.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(480.0f, 360.0f), ImVec2(FLT_MAX, FLT_MAX));

    ImGui::Begin("roblox external", nullptr,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    // ---- window glass card ----
    {
        ImDrawList* d = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();
        ImVec2 ws = ImGui::GetWindowSize();
        float wr = ui_rounded_corners ? ui_corner_radius : 0.0f;

        // soft drop shadow
        d->PushClipRectFullScreen();
        d->AddShadowRect(wp, ImVec2(wp.x + ws.x, wp.y + ws.y),
                         IM_COL32(0, 0, 0, (int)(150.0f * ui::GlassA())), 30.0f,
                         ImVec2(0.0f, 12.0f), ImDrawFlags_None, wr);
        d->PopClipRect();

        // glass body gradient + top sheen + accent hairline
        ui::RoundedGradient(d, wp, ImVec2(wp.x + ws.x, wp.y + ws.y), wr,
                            ui::GlassTop(), ui::GlassTop(), ui::GlassDeep());
        d->AddRectFilled(wp, ImVec2(wp.x + ws.x, wp.y + ws.y * 0.18f),
                         IM_COL32(255, 255, 255, 6), wr, ImDrawFlags_RoundCornersTop);
        d->AddRect(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), ui::Accent(0.5f), wr, 0, 1.2f);
        d->AddRect(ImVec2(wp.x + 0.5f, wp.y + 0.5f), ImVec2(wp.x + ws.x - 0.5f, wp.y + ws.y - 0.5f),
                   IM_COL32(255, 255, 255, 10), wr, 0, 1.0f);
    }

    // ---- custom title bar ----
    {
        static bool s_minimized = false;

        ImDrawList* d = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();
        ImVec2 ws = ImGui::GetWindowSize();
        float ga = ui::GlassA();
        float wr = ui_rounded_corners ? ui_corner_radius : 0.0f;
        const float bar_h = 48.0f;
        float t = ui::AnimT();

        ImVec2 a(wp.x, wp.y), b(wp.x + ws.x, wp.y + bar_h);

        // deep glass header (rounded top corners)
        ui::RoundedGradient(d, a, b, wr,
                            ui::Mix(IM_COL32(24, 27, 43, 255), IM_COL32(255, 255, 255, 255), 0.10f),
                            IM_COL32(20, 23, 37, (int)(225.0f * ga)),
                            IM_COL32(13, 15, 24, (int)(225.0f * ga)));
        d->AddRectFilled(a, ImVec2(b.x, wp.y + 1.0f), IM_COL32(255, 255, 255, 8), wr, ImDrawFlags_RoundCornersTop);

        // animated aurora wash (soft accent glow that breathes)
        {
            float breathe = 0.5f + 0.5f * sinf(t * 0.9f);
            float cx = wp.x + ws.x * (0.18f + 0.64f * breathe);
            d->AddCircleFilled(ImVec2(cx, wp.y - 30.0f), 150.0f, ui::Accent((int)(20.0f) / 255.0f), 72);
            d->AddCircleFilled(ImVec2(cx, wp.y - 30.0f), 90.0f, ui::Accent((int)(16.0f) / 255.0f), 72);
        }

        // catch-light + hairline
        d->AddRectFilled(a, ImVec2(b.x, wp.y + bar_h * 0.45f), IM_COL32(255, 255, 255, 6), wr, ImDrawFlags_RoundCornersTop);
        d->AddLine(ImVec2(a.x, b.y - 1), ImVec2(b.x, b.y - 1), ui::Accent(0.5f), 1.0f);

        // drag anywhere on the bar (left of the window controls) to move the window
        ImGui::SetCursorScreenPos(ImVec2(wp.x, wp.y));
        ImGui::InvisibleButton("##titlebar", ImVec2(ws.x - 150.0f, bar_h));
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 delta = ImGui::GetIO().MouseDelta;
            ImGui::SetWindowPos(ImVec2(wp.x + delta.x, wp.y + delta.y));
        }

        // brand
        {
            ImVec2 bp(wp.x + 15.0f, wp.y);
            float by = bp.y + (bar_h - 22.0f) * 0.5f;
            d->AddRectFilled(ImVec2(bp.x, by), ImVec2(bp.x + 22, by + 22), ui::Accent(), 6.0f);
            d->AddRectFilled(ImVec2(bp.x, by), ImVec2(bp.x + 22, by + 11), ui::AccentLight(0.65f), 6.0f, ImDrawFlags_RoundCornersTop);
            d->AddRect(ImVec2(bp.x, by), ImVec2(bp.x + 22, by + 22), ui::AccentLight(0.9f), 6.0f, 0, 1.0f);
            d->AddCircleFilled(ImVec2(bp.x + 11, by + 11), 3.2f, ui::WHITE);

            d->AddText(ImVec2(bp.x + 31, by + 1), IM_COL32(238, 240, 247, 255), "ROBLOX EXTERNAL");
            d->AddText(ImVec2(bp.x + 31, by + 17), ui::TXT_DIM, "usermode overlay  \xc2\xb7  v0.736");
        }

        // status chip
        {
            bool attached = g_base_address != 0;
            float pulse = 0.5f + 0.5f * sinf(t * 3.0f);
            ImU32 dot = attached ? IM_COL32((int)(60 + 120 * pulse), 225, (int)(90 + 60 * pulse), 255)
                                 : IM_COL32(240, 180, 60, 255);
            const char* st = attached ? "attached" : "waiting for roblox";
            ImVec2 ts = ImGui::CalcTextSize(st);
            float cx = wp.x + ws.x - 150.0f - ts.x - 36.0f;
            float cy = wp.y + (bar_h - 24.0f) * 0.5f;
            d->AddRectFilled(ImVec2(cx, cy), ImVec2(cx + ts.x + 32.0f, cy + 24.0f), IM_COL32(11, 13, 21, 150), 12.0f);
            d->AddRect(ImVec2(cx, cy), ImVec2(cx + ts.x + 32.0f, cy + 24.0f), IM_COL32(255, 255, 255, 18), 12.0f, 0, 1.0f);
            d->AddCircleFilled(ImVec2(cx + 14.0f, cy + 12.0f), 6.5f, ui::Mix(dot, IM_COL32(0, 0, 0, 0), 0.0f));
            d->AddCircleFilled(ImVec2(cx + 14.0f, cy + 12.0f), 4.0f, dot);
            d->AddText(ImVec2(cx + 26.0f, cy + 4.0f), ui::TXT_DIM, st);
        }

        // window controls
        {
            const float btnw = 30.0f, btnh = 30.0f, gap = 8.0f;
            float x0 = wp.x + ws.x - btnw * 2.0f - gap - 14.0f;
            float y0 = wp.y + (bar_h - btnh) * 0.5f;

            ImGui::SetCursorScreenPos(ImVec2(x0, y0));
            if (ImGui::InvisibleButton("##min", ImVec2(btnw, btnh))) s_minimized = !s_minimized;
            bool mhov = ImGui::IsItemHovered();
            d->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + btnw, y0 + btnh), mhov ? IM_COL32(64, 68, 92, 220) : IM_COL32(30, 33, 49, 190), 9.0f);
            d->AddRect(ImVec2(x0, y0), ImVec2(x0 + btnw, y0 + btnh), IM_COL32(255, 255, 255, 20), 9.0f, 0, 1.0f);
            d->AddLine(ImVec2(x0 + 9, y0 + 15), ImVec2(x0 + 21, y0 + 15), IM_COL32(220, 223, 233, 255), 1.6f);

            float x1 = x0 + btnw + gap;
            ImGui::SetCursorScreenPos(ImVec2(x1, y0));
            if (ImGui::InvisibleButton("##close", ImVec2(btnw, btnh))) g_request_exit = true;
            bool chov = ImGui::IsItemHovered();
            if (chov) d->AddCircleFilled(ImVec2(x1 + 15, y0 + 15), 18.0f, IM_COL32(224, 74, 84, 60));
            d->AddRectFilled(ImVec2(x1, y0), ImVec2(x1 + btnw, y0 + btnh), chov ? IM_COL32(224, 74, 84, 240) : IM_COL32(30, 33, 49, 190), 9.0f);
            d->AddRect(ImVec2(x1, y0), ImVec2(x1 + btnw, y0 + btnh), IM_COL32(255, 255, 255, 20), 9.0f, 0, 1.0f);
            d->AddLine(ImVec2(x1 + 10, y0 + 10), ImVec2(x1 + 20, y0 + 20), IM_COL32(240, 240, 245, 255), 1.6f);
            d->AddLine(ImVec2(x1 + 20, y0 + 10), ImVec2(x1 + 10, y0 + 20), IM_COL32(240, 240, 245, 255), 1.6f);
        }

        if (s_minimized) { ImGui::End(); return; }
    }

    // ---- left rail + content well (custom-ui inspired layout) ----
    static int    s_page    = 0;
    static float  s_pill_y  = -1.0f;
    static float  s_glow[8] = { 0.0f };

    const char* kPages[] = { "aimbot", "esp", "misc", "world", "keybinds", "ui", "config", "debug" };
    const int   kIcons[] = { ui::RK_AIMBOT, ui::RK_ESP, ui::RK_MISC, ui::RK_WORLD,
                             ui::RK_KEYS, ui::RK_UI, ui::RK_CONFIG, ui::RK_DEBUG };
    const int   n = IM_ARRAYSIZE(kPages);
    bool hov[8] = { false };

    ImDrawList* d = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    ImVec2 ws = ImGui::GetWindowSize();

    const float bar_h    = 48.0f;
    const float footer_h = 28.0f;
    const float rail_w   = 60.0f;
    const float rail_x   = wp.x + 12.0f;
    const float rail_y   = wp.y + bar_h + 12.0f;
    const float rail_bot = wp.y + ws.y - footer_h - 8.0f;
    const float slot     = 40.0f;
    const float gap      = 12.0f;
    const float slot_x   = rail_x + (rail_w - slot) * 0.5f;

    // rail backing
    ui::LiquidPanel(d, ImVec2(rail_x, rail_y), ImVec2(rail_x + rail_w, rail_bot), 14.0f,
                    IM_COL32(22, 25, 39, (int)(120.0f * ui::GlassA())),
                    ui::Accent(0.20f), false, 0.0f, false);

    // hit-test the icon slots (drives clicks, hover glow and tooltips)
    for (int i = 0; i < n; ++i) {
        float ty = rail_y + 10.0f + i * (slot + gap);
        ImGui::SetCursorScreenPos(ImVec2(slot_x, ty));
        ImGui::InvisibleButton(kPages[i], ImVec2(slot, slot));
        if (ImGui::IsItemClicked()) s_page = i;
        hov[i] = ImGui::IsItemHovered();
        ui::Ease(s_glow[i], (hov[i] && i != s_page) ? 1.0f : 0.0f, 22.0f);
        if (s_glow[i] > 0.01f)
            d->AddRectFilled(ImVec2(slot_x, ty), ImVec2(slot_x + slot, ty + slot),
                             IM_COL32(66, 71, 100, (int)(140.0f * s_glow[i] * ui::GlassA())), 11.0f);
        if (hov[i]) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(kPages[i]);
            ImGui::EndTooltip();
        }
    }

    // animated accent pill sliding to the active slot
    {
        float target_y = rail_y + 10.0f + s_page * (slot + gap);
        if (s_pill_y < 0.0f) s_pill_y = target_y;
        ui::Ease(s_pill_y, target_y, 18.0f);
        ui::AccentHalo(d, ImVec2(slot_x, s_pill_y), ImVec2(slot_x + slot, s_pill_y + slot), 11.0f, 0.9f);
        d->AddRectFilled(ImVec2(slot_x, s_pill_y), ImVec2(slot_x + slot, s_pill_y + slot),
                         ui::Accent(0.30f), 11.0f);
        d->AddRectFilled(ImVec2(slot_x, s_pill_y), ImVec2(slot_x + slot, s_pill_y + slot * 0.5f),
                         ui::AccentLight(0.26f), 11.0f, ImDrawFlags_RoundCornersTop);
        d->AddRect(ImVec2(slot_x, s_pill_y), ImVec2(slot_x + slot, s_pill_y + slot),
                   ui::AccentLight(0.75f), 11.0f, 0, 1.1f);
    }

    // draw the rail icons (active = white on accent, hovered = bright, idle = dim)
    for (int i = 0; i < n; ++i) {
        float ty = rail_y + 10.0f + i * (slot + gap);
        ImVec2 c(slot_x + slot * 0.5f, ty + slot * 0.5f);
        float mix = (i == s_page) ? 1.0f : (hov[i] ? 0.62f : 0.0f);
        ImU32 col = ui::Mix(ui::TXT_DIM, ui::WHITE, mix);
        ui::RailIcon(d, c, 13.0f, kIcons[i], col);
    }

    // ---- content well ----
    float well_x = rail_x + rail_w + 14.0f;
    float well_y = wp.y + bar_h + 12.0f;
    float well_w = (wp.x + ws.x - 16.0f) - well_x;
    float well_h = rail_bot - well_y;
    if (well_w < 120.0f) well_w = 120.0f;
    if (well_h < 80.0f)  well_h = 80.0f;
    ui::LiquidPanel(d, ImVec2(well_x, well_y), ImVec2(well_x + well_w, well_y + well_h), 16.0f,
                    IM_COL32(20, 23, 36, (int)(110.0f * ui::GlassA())),
                    ui::Accent(0.16f), false, 0.0f, false);

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
            ui::Slider("fov size", &fov_size, 10.0f, 500.0f);
            ui::Toggle("show fov", &show_fov);
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
            ImGui::TextDisabled("set keys for these in the keybinds tab");
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
            if (click_teleport_enabled) ui::Slider("tp distance", &click_teleport_distance, 5.0f, 200.0f, "%.0f studs");
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
            if (inventory_checker_enabled) ImGui::TextDisabled("hold the key with your cursor over a player");
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
            keybind_button("click teleport", click_teleport_keybind);
            keybind_button("inventory checker", inventory_checker_keybind);
            ImGui::Separator();

            ImGui::TextDisabled("all binds are hold-to-use except click teleport,");
            ImGui::TextDisabled("which fires once per press.");
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
                    float before[3] = {};
                    read_raw(prim + Offsets::Primitive::Position, before, sizeof(before));
                    float target[3] = { before[0], before[1] + 10.0f, before[2] };
                    bool wrote = write_raw(prim + Offsets::Primitive::Position, target, sizeof(target));
                    float after[3] = {};
                    read_raw(prim + Offsets::Primitive::Position, after, sizeof(after));
                    float delta = after[1] - before[1];
                    snprintf(test_result, sizeof(test_result),
                             "POS: wpm=%s y %.2f -> %.2f (delta %.2f) %s",
                             wrote ? "ok" : "FAILED", before[1], after[1], delta,
                             (delta > 5.0f) ? "LANDED" : "did NOT stick");
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
        fd->AddLine(ImVec2(fx, fy), ImVec2(fx + fw, fy), IM_COL32(255, 255, 255, 14), 1.0f);

        char left[96];
        if (g_base_address) {
            snprintf(left, sizeof(left), "attached  pid %u", (unsigned)mem::process_id.load());
        } else {
            snprintf(left, sizeof(left), "waiting for roblox...");
        }
        fd->AddText(ImVec2(fx, fy + 7), ui::TXT_DIM, left);

        char right[96];
        snprintf(right, sizeof(right), "%d fps  \xc2\xb7  %s toggles menu",
                 (int)ImGui::GetIO().Framerate, KeyName(menu_toggle_keybind));
        ImVec2 rts = ImGui::CalcTextSize(right);
        fd->AddText(ImVec2(fx + fw - rts.x, fy + 7), ui::TXT_DIM, right);
    }

    ImGui::End();
}
