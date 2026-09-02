// -----------------------------------------------------------------------------
//  src/window.cpp  --  ObsidianWindow: collapse-safe resizable glass window
// -----------------------------------------------------------------------------
//  Read the header comment in obsidian/window.h first: it explains the three
//  failure modes this class is built to avoid. The short version is that all
//  size decisions are made inside the SetNextWindowSizeConstraints() callback,
//  which ImGui runs every frame with this frame's DesiredSize already reflecting
//  any in-progress user resize. Nothing is forced with ImGuiCond_Always.
// -----------------------------------------------------------------------------
#include "obsidian/window.h"
#include "obsidian/theme.h"
#include "obsidian/detail.h"

#include <cmath>
#include <cstring>

namespace obsidian {

// -----------------------------------------------------------------------------
//  Title-bar icons that need a direction (IconDrawFn has no room for arguments)
// -----------------------------------------------------------------------------
namespace {
// Faceted obsidian shard: four triangles with a light-to-dark ramp, so it reads
// as cut volcanic glass rather than a flat diamond.
void DrawShard(ImDrawList* d, ImVec2 c, float s, const ImVec4& a1, const ImVec4& a2)
{
    const ImVec2 T(c.x,              c.y - s);
    const ImVec2 R(c.x + s * 0.64f,  c.y - s * 0.10f);
    const ImVec2 B(c.x + s * 0.16f,  c.y + s);
    const ImVec2 L(c.x - s * 0.58f,  c.y + s * 0.34f);
    const ImVec2 C(c.x - s * 0.04f,  c.y + s * 0.06f);

    d->AddCircleFilled(c, s * 1.5f, ImGui::ColorConvertFloat4ToU32(Fade(a1, 0.10f)), 20);
    d->AddTriangleFilled(T, R, C, ImGui::ColorConvertFloat4ToU32(Shade(a1, 0.30f)));
    d->AddTriangleFilled(T, C, L, ImGui::ColorConvertFloat4ToU32(a1));
    d->AddTriangleFilled(R, B, C, ImGui::ColorConvertFloat4ToU32(Mix(a1, a2, 0.55f)));
    d->AddTriangleFilled(C, B, L, ImGui::ColorConvertFloat4ToU32(a2));
    d->AddLine(T, R, ImGui::ColorConvertFloat4ToU32(Fade(ImVec4(1, 1, 1, 1), 0.55f)), s * 0.10f);
    d->AddLine(T, L, ImGui::ColorConvertFloat4ToU32(Fade(ImVec4(1, 1, 1, 1), 0.28f)), s * 0.10f);
    d->AddLine(C, T, ImGui::ColorConvertFloat4ToU32(Fade(ImVec4(1, 1, 1, 1), 0.18f)), s * 0.08f);
}
} // namespace

// -----------------------------------------------------------------------------
//  Construction
// -----------------------------------------------------------------------------
ObsidianWindow::ObsidianWindow()
{
    m_cfg.title = "Window";
}

ObsidianWindow::ObsidianWindow(const WindowConfig& cfg) : m_cfg(cfg) {}

float ObsidianWindow::TitleBarHeightPx() const
{
    return m_cfg.title_bar_height > 0.0f ? S(m_cfg.title_bar_height) : TitleBarHeight();
}

float ObsidianWindow::CornerR() const
{
    const Palette& p = ActivePalette();
    return m_cfg.corner_radius > 0.0f ? S(m_cfg.corner_radius) : p.corner_lg;
}

ImVec2 ObsidianWindow::WorkArea() const
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    return ImVec2(vp->WorkSize.x, vp->WorkSize.y);
}

ImVec2 ObsidianWindow::ClampSize(const ImVec2& s) const
{
    const float tb    = TitleBarHeightPx();
    const float min_w = S(m_cfg.min_width);
    const float min_h = FMax(S(m_cfg.min_height), tb + S(12.0f));
    const ImVec2 work = WorkArea();
    const float max_w = work.x > 0.0f ? work.x : FLT_MAX;
    const float max_h = work.y > 0.0f ? work.y : FLT_MAX;
    return ImVec2(FClamp(s.x, min_w, max_w), FClamp(s.y, min_h, max_h));
}

// Keep the title bar reachable: a window you cannot grab back is worse than one
// that is partly off-screen. Applied only when we are not mid-drag so it can
// never fight the user.
ImVec2 ObsidianWindow::ClampPos(const ImVec2& pos, float title_bar) const
{
    if (!m_cfg.clamp_to_work_area) return pos;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float x0 = vp->WorkPos.x;
    const float y0 = vp->WorkPos.y;
    const float x1 = x0 + vp->WorkSize.x;
    const float y1 = y0 + vp->WorkSize.y;
    const float w  = m_current.x > 0.0f ? m_current.x : S(m_cfg.min_width);
    const float grab = S(72.0f);   // minimum strip of title bar kept on screen
    return ImVec2(FClamp(pos.x, x0 - w + grab, x1 - grab),
                  FClamp(pos.y, y0 - S(4.0f), y1 - title_bar));
}

// -----------------------------------------------------------------------------
//  Animation
// -----------------------------------------------------------------------------
void ObsidianWindow::AdvanceAnimations()
{
    float dt = ImGui::GetIO().DeltaTime;
    if (!(dt > 0.0f)) dt = 1.0f / 60.0f;

    // Constant-duration linear timelines, eased at the point of use. Exponential
    // smoothing would asymptotically approach the target and leave a sub-pixel
    // error forever -- which reads as a window that never quite stops resizing.
    const float target = m_want_collapsed ? 0.0f : 1.0f;
    if (m_cfg.collapse_time <= 0.0f) {
        m_anim_exp = target;
    } else {
        const float dv = dt / m_cfg.collapse_time;
        if (m_anim_exp < target)      m_anim_exp = FMin(target, m_anim_exp + dv);
        else if (m_anim_exp > target) m_anim_exp = FMax(target, m_anim_exp - dv);
    }

    if (m_anim_size < 1.0f) {
        const float dur = m_cfg.resize_time > 0.0f ? m_cfg.resize_time : 1e-4f;
        m_anim_size = FMin(1.0f, m_anim_size + dt / dur);
    }
}

bool ObsidianWindow::IsAnimating() const
{
    const float target = m_want_collapsed ? 0.0f : 1.0f;
    return m_anim_exp != target || m_anim_size < 1.0f;
}

void ObsidianWindow::HandleUiScaleChange()
{
    const float sc = S(1.0f);
    if (m_frames > 0 && m_ui_scale > 0.0f && sc != m_ui_scale) {
        // DPI changed under us: rescale the restore size and morph to it from
        // wherever the window actually is, instead of letting the old pixel size
        // linger (which is what makes a scaled UI look clipped or oversized).
        const float k = sc / m_ui_scale;
        m_size_from  = m_current;
        m_expanded   = ClampSize(VMul(m_expanded, k));
        m_anim_size  = 0.0f;
        m_pin_height = true;
        m_pin_width  = true;
    }
    m_ui_scale = sc;
}

// -----------------------------------------------------------------------------
//  Collapse / size control
// -----------------------------------------------------------------------------
void ObsidianWindow::Collapse()
{
    if (!m_cfg.allow_collapse) return;
    m_want_collapsed = true;
    m_pin_height = true;      // the height is ours until we are back at m_expanded
}

void ObsidianWindow::Expand()
{
    m_want_collapsed = false;
    m_pin_height = true;
}

void ObsidianWindow::ToggleCollapse()
{
    if (m_want_collapsed) Expand(); else Collapse();
}

void ObsidianWindow::SetExpandedSize(const ImVec2& size, bool animate)
{
    const ImVec2 s = ClampSize(size);
    if (VEqual(s, m_expanded)) return;

    // Both axes are ours for the duration of the morph.
    m_pin_height = true;
    m_pin_width  = true;

    if (animate && m_cfg.resize_time > 0.0f) {
        // Morph from where the window visually is. While collapsed the visible
        // height is the title bar, which is not a useful morph origin, so use
        // the persisted expanded size instead.
        m_size_from = m_want_collapsed ? m_expanded : m_current;
        m_anim_size = 0.0f;
    } else {
        m_size_from = m_expanded;
        m_anim_size = 1.0f;
    }
    m_expanded = s;
}

void ObsidianWindow::QueueSize(const ImVec2& size)
{
    // Immediate, one-shot: no timeline, just pin both axes until we arrive.
    m_expanded   = ClampSize(size);
    m_size_from  = m_expanded;
    m_anim_size  = 1.0f;
    m_pin_height = true;
    m_pin_width  = true;
}

ObsidianWindow::LayoutState ObsidianWindow::SaveLayout() const
{
    LayoutState s;
    s.pos      = m_pos;
    s.size     = m_expanded;
    s.collapsed = m_want_collapsed;
    return s;
}

void ObsidianWindow::RestoreLayout(const LayoutState& s)
{
    m_expanded = ClampSize(s.size);
    m_pos      = s.pos;
    m_want_collapsed = s.collapsed && m_cfg.allow_collapse;
    // Snap without animating: restoring a saved layout should not play a
    // collapse animation on startup.
    m_anim_exp   = m_want_collapsed ? 0.0f : 1.0f;
    m_anim_size  = 1.0f;
    m_size_from  = m_expanded;
    m_pin_height = true;      // released by End() once we have arrived
    m_pin_width  = true;
    ImGui::SetNextWindowPos(m_pos, ImGuiCond_Always);
}

// -----------------------------------------------------------------------------
//  THE FIX: everything about size happens here.
//  ImGui calls this from Begin() every frame, with DesiredSize already updated
//  for the user's in-progress resize, so there is no state lag and nothing gets
//  overwritten after the fact.
// -----------------------------------------------------------------------------
void ObsidianWindow::SizeConstraintThunk(ImGuiSizeCallbackData* data)
{
    static_cast<ObsidianWindow*>(data->UserData)->SizeConstraint(data);
}

void ObsidianWindow::SizeConstraint(ImGuiSizeCallbackData* data)
{
    const float   tb      = TitleBarHeightPx();
    const float   min_w   = S(m_cfg.min_width);
    const ImVec2  work    = WorkArea();
    const float   max_w   = work.x > 0.0f ? work.x : FLT_MAX;
    const float   max_h   = work.y > 0.0f ? work.y : FLT_MAX;

    ImVec2 want = data->DesiredSize;

    // Where the window is heading this frame (morph-aware).
    ImVec2 target = m_expanded;
    if (m_anim_size < 1.0f)
        target = VLerp(m_size_from, m_expanded, anim::Ease(m_anim_size));

    const bool collapsing = m_want_collapsed || m_anim_exp < 1.0f;

    // ---- width -------------------------------------------------------------
    // Identical minimum in both states. A per-state min width is what makes a
    // window "pop" wider the instant it expands.
    if (m_pin_width || (m_cfg.lock_width_when_collapsed && collapsing))
        want.x = target.x;
    want.x = FClamp(want.x, min_w, max_w);

    // ---- height ------------------------------------------------------------
    if (m_pin_height) {
        // Ours until the window has visibly arrived. Interpolated between the
        // title bar and the target so collapse/expand is a real animation.
        want.y = tb + (target.y - tb) * anim::Ease(m_anim_exp);
    }
    // Floor differs by state on purpose: while collapsing the floor must be the
    // title bar or the animation can never finish; while expanded it is the
    // caller's min_height.
    const float min_h = collapsing ? tb : FMax(tb, S(m_cfg.min_height));
    want.y = FClamp(want.y, min_h, max_h);

    data->DesiredSize = want;
}

// -----------------------------------------------------------------------------
//  Begin / End
// -----------------------------------------------------------------------------
bool ObsidianWindow::Begin(bool* p_open)
{
    m_begun     = true;
    m_body_open = false;
    m_body_begun = false;

    if (m_frames == 0) {
        // Adopt the current ui_scale as the baseline BEFORE checking for changes,
        // otherwise an app that starts at anything but 1.0 would morph from 0x0.
        m_ui_scale       = S(1.0f);
        m_expanded       = ClampSize(VMul(m_cfg.initial_size, m_ui_scale));
        m_size_from      = m_expanded;
        m_want_collapsed = m_cfg.start_collapsed;
        m_anim_exp       = m_cfg.start_collapsed ? 0.0f : 1.0f;
        m_anim_size      = 1.0f;
        m_current        = m_expanded;
        m_pin_height     = true;   // released by End() once we have arrived
        m_pin_width      = true;
    } else {
        HandleUiScaleChange();
    }
    ++m_frames;

    AdvanceAnimations();

    const float tb = TitleBarHeightPx();
    const float r  = CornerR();

    ImGui::SetNextWindowSize(m_expanded, ImGuiCond_FirstUseEver);
    if (m_cfg.initial_pos.x > -1.0e8f)
        ImGui::SetNextWindowPos(m_cfg.initial_pos, ImGuiCond_FirstUseEver);

    // ONE constraint rect for every state; the state-dependent logic lives in the
    // callback. min.y is the title bar so collapsing is always legal.
    ImGui::SetNextWindowSizeConstraints(ImVec2(S(m_cfg.min_width), tb),
                                        ImVec2(FLT_MAX, FLT_MAX),
                                        &ObsidianWindow::SizeConstraintThunk, this);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
                           | ImGuiWindowFlags_NoCollapse
                           | ImGuiWindowFlags_NoScrollbar
                           | ImGuiWindowFlags_NoScrollWithMouse
                           | ImGuiWindowFlags_NoMove          // we own the drag handle
                           | m_cfg.extra_flags;
    if (!m_cfg.allow_resize)     flags |= ImGuiWindowFlags_NoResize;
    if (m_cfg.no_saved_settings) flags |= ImGuiWindowFlags_NoSavedSettings;

    // We paint our own glass, so the stock window background must be invisible.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   r);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(0, 0, 0, 0));
    m_style_pushed = true;

    m_visible = ImGui::Begin(m_cfg.title ? m_cfg.title : "Window", nullptr, flags);
    if (!m_visible)
        return false;   // End() still balances ImGui::End() and the style stack

    m_current = ImGui::GetWindowSize();
    m_pos     = ImGui::GetWindowPos();

    // Keep the title bar reachable (never while the user is dragging it).
    if (!m_dragging) {
        const ImVec2 clamped = ClampPos(m_pos, tb);
        if (!VEqual(clamped, m_pos)) {
            ImGui::SetWindowPos(clamped);
            m_pos = clamped;
        }
    }

    DrawChrome();
    DrawTitleBar(p_open);

    // Body: skipped entirely once collapsed (nothing to draw, and it keeps the
    // per-frame cost of a collapsed overlay near zero).
    if (m_anim_exp > 0.002f) {
        const float body_h = FMax(1.0f, m_current.y - tb);   // never <= 0
        ImGui::SetCursorScreenPos(ImVec2(m_pos.x, m_pos.y + tb));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(14.0f), S(13.0f)));
        m_body_begun = true;
        m_body_open  = ImGui::BeginChild("##obs_body", ImVec2(0.0f, body_h),
                                         ImGuiChildFlags_AlwaysUseWindowPadding,
                                         ImGuiWindowFlags_None);
        return m_body_open;
    }
    return false;
}

void ObsidianWindow::End()
{
    if (!m_begun) return;

    if (m_body_begun) {
        ImGui::EndChild();          // required even when BeginChild returned false
        ImGui::PopStyleVar();
        m_body_begun = false;
    }

    if (m_visible) {
        const ImVec2 sz = ImGui::GetWindowSize();
        m_current = sz;
        m_pos     = ImGui::GetWindowPos();

        // Release each pin only once the window has actually ARRIVED. Checking
        // "is the timeline finished" instead would release one frame early: the
        // size we then read back is still the mid-animation one, clamped up to
        // min_height, and it would be captured as the new restore size -- which
        // is exactly how a window ends up expanding to the wrong height.
        if (m_pin_width && std::fabs(sz.x - m_expanded.x) <= 0.5f)
            m_pin_width = false;
        if (m_pin_height && !m_want_collapsed && m_anim_exp >= 1.0f &&
            std::fabs(sz.y - m_expanded.y) <= 0.5f)
            m_pin_height = false;

        // Fold the user's own resizing back into the restore size, per axis, for
        // every axis we are not currently driving. This is what makes a width
        // change made while collapsed survive the next expand.
        if (!m_pin_width)
            m_expanded.x = sz.x;
        if (!m_pin_height && !m_want_collapsed)
            m_expanded.y = sz.y;

        ImGui::End();
    }

    if (m_style_pushed) {
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
        m_style_pushed = false;
    }

    m_body_open  = false;
    m_visible    = false;
    m_begun      = false;
}

// -----------------------------------------------------------------------------
//  Chrome
// -----------------------------------------------------------------------------
void ObsidianWindow::DrawChrome()
{
    const Palette& p = ActivePalette();
    const float sa = p.SurfaceAlphaScale();
    ImDrawList* d = ImGui::GetWindowDrawList();

    const ImVec2 a = m_pos;
    const ImVec2 b(a.x + m_current.x, a.y + m_current.y);
    const float r = CornerR();

    if (m_cfg.draw_shadow)
        SoftShadow(d, a, b, Fade(p.shadow, 0.80f), r, S(36.0f), ImVec2(0.0f, S(13.0f)), true);

    // Body: cold obsidian, lighter at the top (light from above).
    GradientFillRounded(d, a, b, Fade(p.window_top, sa), Fade(p.window_bottom, sa), r, 18);

    // Specular top edge + faint accent wash inside the glass.
    StrokeSheen(d, a, b, Fade(p.sheen, 1.0f), r);
    EdgeGlow(d, a, b, Fade(p.inner_glow, 1.0f), r, 0);

    // Accent line riding the top edge, fading out towards both corners.
    AccentRule(d, ImVec2(a.x + r, a.y + 0.75f), m_current.x - r * 2.0f,
               Fade(p.accent, 0.42f * sa), S(1.5f));

    StrokeEdge(d, a, b, Fade(p.edge, 0.90f), r);
}

bool ObsidianWindow::DrawTitleBar(bool* p_open)
{
    const Palette& p = ActivePalette();
    const float ta = p.TextAlphaScale();
    ImDrawList* d = ImGui::GetWindowDrawList();

    const ImVec2 a = m_pos;
    const float  tb = TitleBarHeightPx();
    const float  r  = CornerR();
    const ImVec2 tb_b(a.x + m_current.x, a.y + tb);

    // ---- header surface -----------------------------------------------------
    // Clip to the title band but fill the FULL rounded rect: that way the band
    // inherits the window's top-corner rounding exactly, with no seam.
    d->PushClipRect(ImVec2(a.x - 1.0f, a.y - 1.0f), ImVec2(tb_b.x + 1.0f, tb_b.y + 0.5f), true);
    GradientFillRounded(d, a, ImVec2(a.x + m_current.x, a.y + m_current.y),
                        Fade(Shade(p.header, 0.08f), p.SurfaceAlphaScale()),
                        Fade(p.header, p.SurfaceAlphaScale()), r, 10);
    d->PopClipRect();

    // hairline under the header + accent wash bleeding into the body
    Hairline(d, ImVec2(a.x + r * 0.5f, tb_b.y), ImVec2(tb_b.x - r * 0.5f, tb_b.y),
             Fade(p.edge, 0.85f));
    AccentRule(d, ImVec2(a.x + r, tb_b.y), m_current.x - r * 2.0f, Fade(p.accent, 0.30f), S(1.0f));

    const float wash = S(30.0f) * (0.35f + 0.65f * m_anim_exp);
    if (wash > S(2.0f) && m_current.y > tb + wash) {
        d->PushClipRect(ImVec2(a.x, tb_b.y), ImVec2(a.x + m_current.x, tb_b.y + wash), true);
        const int bands = 6;
        for (int i = 0; i < bands; ++i) {
            const float t0 = (float)i / bands, t1 = (float)(i + 1) / bands;
            const float al = 0.055f * (1.0f - t0) * (1.0f - t0);
            d->AddRectFilled(ImVec2(a.x, tb_b.y + wash * t0),
                             ImVec2(a.x + m_current.x, tb_b.y + wash * t1),
                             ImGui::ColorConvertFloat4ToU32(Fade(p.accent, al)));
        }
        d->PopClipRect();
    }

    // ---- left: mark + title -------------------------------------------------
    const float mid_y = a.y + tb * 0.5f;
    float x = a.x + S(15.0f);

    if (m_cfg.show_mark) {
        DrawShard(d, ImVec2(x + S(10.0f), mid_y), S(10.0f), p.accent, p.accent_2);
        x += S(28.0f);
    }

    if (m_cfg.title && *m_cfg.title) {
        ImFont* tf = FontTitle();
        const float fs = tf ? tf->FontSize : tb * 0.46f;
        const ImVec2 ts = tf ? tf->CalcTextSizeA(fs, FLT_MAX, -1.0f, m_cfg.title)
                             : ImGui::CalcTextSize(m_cfg.title);
        const ImVec4 tcol = Mix(p.text, p.accent, 0.12f);
        if (tf) d->AddText(tf, fs, ImVec2(x, mid_y - ts.y * 0.5f),
                           ImGui::ColorConvertFloat4ToU32(Fade(tcol, ta)), m_cfg.title);
        else    d->AddText(ImVec2(x, mid_y - ts.y * 0.5f),
                           ImGui::ColorConvertFloat4ToU32(Fade(tcol, ta)), m_cfg.title);
        x += ts.x;
    }

    // ---- right: buttons -----------------------------------------------------
    const bool want_collapse_btn = m_cfg.show_collapse_button && m_cfg.allow_collapse;
    const bool want_close_btn    = m_cfg.show_close_button && p_open != nullptr;
    const int  nbtn = (want_collapse_btn ? 1 : 0) + (want_close_btn ? 1 : 0);
    const float btn  = FMin(S(30.0f), tb - S(10.0f));
    const float gap  = S(4.0f);
    const float buttons_w = nbtn > 0 ? ((float)nbtn * btn + ((float)nbtn - 1.0f) * gap + S(12.0f)) : S(6.0f);

    // subtitle / status read-out sits just left of the buttons
    if (m_cfg.subtitle && *m_cfg.subtitle && nbtn >= 0) {
        ImFont* mono = FontMono();
        const float fs = mono ? mono->FontSize : ImGui::GetTextLineHeight();
        const ImVec2 ss = mono ? mono->CalcTextSizeA(fs, FLT_MAX, -1.0f, m_cfg.subtitle)
                               : ImGui::CalcTextSize(m_cfg.subtitle);
        const float sx = a.x + m_current.x - buttons_w - ss.x - S(10.0f);
        if (sx > x + S(16.0f)) {
            const ImVec4 sc = (m_cfg.subtitle_tint.w > 0.0f) ? m_cfg.subtitle_tint : p.text_dim;
            if (mono) d->AddText(mono, fs, ImVec2(sx, mid_y - ss.y * 0.5f),
                                 ImGui::ColorConvertFloat4ToU32(Fade(sc, ta)), m_cfg.subtitle);
            else      d->AddText(ImVec2(sx, mid_y - ss.y * 0.5f),
                                 ImGui::ColorConvertFloat4ToU32(Fade(sc, ta)), m_cfg.subtitle);
        }
    }

    // ---- drag region (the window is ImGuiWindowFlags_NoMove; we own moving) --
    // Spans everything except the buttons, so the two never overlap and hit
    // testing stays unambiguous.
    const float drag_w = FMax(1.0f, m_current.x - buttons_w);
    ImGui::SetCursorScreenPos(ImVec2(a.x, a.y));
    ImGui::PushID("##obs_title");
    ImGui::InvisibleButton("drag", ImVec2(drag_w, tb));
    const bool drag_hov = ImGui::IsItemHovered();
    const bool drag_act = ImGui::IsItemActive();
    const bool drag_started = ImGui::IsItemActivated();
    ImGui::PopID();

    if (drag_started) {
        m_drag_start = ImGui::GetWindowPos();
        m_dragging   = true;
    }
    if (m_dragging) {
        if (drag_act && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
            const ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
            ImGui::SetWindowPos(ClampPos(VAdd(m_drag_start, delta), tb));
        } else if (!drag_act) {
            m_dragging = false;
        }
    }
    if (drag_hov && m_cfg.allow_collapse &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        ToggleCollapse();

    if (nbtn > 0) {
        const float by = a.y + (tb - btn) * 0.5f;
        float bx = a.x + m_current.x - buttons_w + S(6.0f);

        if (want_collapse_btn) {
            ImGui::SetCursorScreenPos(ImVec2(bx, by));
            if (IconButton("##obs_collapse", m_want_collapsed ? icon::ChevronDown : icon::ChevronUp,
                           ImVec2(btn, btn), ButtonKind_Ghost,
                           m_want_collapsed ? "Expand" : "Collapse"))
                ToggleCollapse();
            bx += btn + gap;
        }
        if (want_close_btn) {
            ImGui::SetCursorScreenPos(ImVec2(bx, by));
            if (IconButton("##obs_close", icon::Close, ImVec2(btn, btn),
                           ButtonKind_Danger, "Close"))
                *p_open = false;
        }
    }

    return true;
}

} // namespace obsidian
