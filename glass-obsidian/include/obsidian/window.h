// -----------------------------------------------------------------------------
//  obsidian/window.h  --  collapse-safe, resizable glass window
// -----------------------------------------------------------------------------
//  WHY THIS EXISTS
//  ---------------
//  "Resizing breaks when the UI is collapsed" is almost never one bug; it is a
//  cluster of three that all come from driving window size the same way:
//
//    (1) LEVEL-TRIGGERED SIZE FORCING. Calling
//            SetNextWindowSize(sz, ImGuiCond_Always)
//        every frame (or on a queued flag) overwrites whatever the user is doing
//        with the resize grip. The window feels "sticky" or snaps back.
//
//    (2) ONE-FRAME STATE LAG. Whether a window is collapsed is only known AFTER
//        Begin(), but SetNextWindowSize* must be called BEFORE it. Code that
//        reads last frame's collapsed flag therefore applies the wrong size for
//        exactly one frame -> a visible jump on every toggle.
//
//    (3) STATE-DEPENDENT CONSTRAINTS. Using a different
//        SetNextWindowSizeConstraints() min/max per state means the moment the
//        state flips, the other state's limits retroactively clamp the size you
//        were trying to restore (typically: min height 400 clamps the collapsed
//        title bar, or min width 560 pops the window wider on expand). The
//        restore value is silently lost and the window comes back the wrong size.
//
//  THE FIX
//  -------
//  All three disappear if size is decided inside a
//  SetNextWindowSizeConstraints() CALLBACK instead of by forcing sizes:
//
//    * ImGui invokes CalcWindowSizeAfterConstraint() every frame from Begin()
//      (imgui.cpp: "window->SizeFull = CalcWindowSizeAfterConstraint(...)"),
//      and hands the callback BOTH CurrentSize and DesiredSize for *this* frame.
//      DesiredSize already contains the user's in-progress resize. So there is
//      no lag (fixes 2) and nothing is overwritten after the fact (fixes 1).
//
//    * One constraint rect is used in every state, so limits never change
//      underneath the window (fixes 3). The state-dependent part -- pinning the
//      height to the title bar while collapsed, and interpolating it during the
//      animation -- lives in the callback, where writing DesiredSize.y is
//      authoritative for that frame.
//
//    * When settled and expanded the callback degenerates to a pure min/max
//      clamp: it does not touch DesiredSize, so the user's drag is untouched.
//
//  Plus the small things that cause the remaining 10% of the pain:
//    * min WIDTH is identical in both states -> no sideways pop on expand.
//    * width stays user-driven while collapsed and is folded back into the
//      restore size, so collapsing never loses a width change.
//    * animations are constant-duration linear timers eased at the use site, so
//      they land exactly (an exponential approach never quite reaches its
//      target and reads as a permanent 1px jitter).
//    * the body child height is clamped to >= 1px, so a fully collapsed window
//      never produces a negative/zero-height child.
//    * NoSavedSettings by default: the .ini cannot re-apply a stale size behind
//      the state machine's back. SaveLayout()/RestoreLayout() replace it.
//
//  Public ImGui API only.
// -----------------------------------------------------------------------------
#pragma once

#include "obsidian/glass.h"

namespace obsidian {

struct WindowConfig
{
    // Geometry
    ImVec2 initial_size      = ImVec2(760.0f, 520.0f);
    ImVec2 initial_pos       = ImVec2(-1.0e9f, -1.0e9f);  // < -1e8 => let ImGui decide
    float  title_bar_height  = 0.0f;     // 0 => obsidian::TitleBarHeight()
    float  min_width         = 480.0f;   // SAME in both states, on purpose
    float  min_height        = 300.0f;   // enforced only while expanded
    float  corner_radius     = 0.0f;     // 0 => palette corner_lg

    // Animation (seconds; <= 0 snaps instantly)
    float  collapse_time     = 0.20f;
    float  resize_time       = 0.18f;

    // Behaviour
    bool   allow_collapse        = true;   // double-click title bar / chevron
    bool   allow_resize          = true;   // edges + bottom-right grip
    bool   start_collapsed       = false;
    bool   lock_width_when_collapsed = false;  // false = width stays user-driven
    bool   clamp_to_work_area    = true;   // always keep the title bar reachable
    bool   no_saved_settings     = true;   // see header note about the .ini
    bool   draw_shadow           = true;

    // Chrome
    bool   show_mark             = true;   // obsidian shard logo
    bool   show_collapse_button  = true;
    bool   show_close_button     = true;
    const char* title            = "Window";
    const char* subtitle         = nullptr;  // small right-aligned status read-out
    ImVec4      subtitle_tint    = ImVec4(0, 0, 0, 0);  // w == 0 => text_dim

    ImGuiWindowFlags extra_flags = ImGuiWindowFlags_None;
};

class ObsidianWindow
{
public:
    ObsidianWindow();
    explicit ObsidianWindow(const WindowConfig& cfg);

    // Returns true when the body should be drawn. ALWAYS call End() afterwards,
    // even when it returns false (mirrors ImGui::Begin/End).
    bool Begin(bool* p_open = nullptr);
    void End();

    // ---- collapse (edge-triggered; safe to call from anywhere, any frame) ----
    void Collapse();
    void Expand();
    void ToggleCollapse();
    bool IsCollapsed()  const { return m_want_collapsed; }
    bool IsAnimating()  const;
    float Expansion()   const { return m_anim_exp; }   // 0 = collapsed, 1 = expanded

    // ---- size ---------------------------------------------------------------
    ImVec2 ExpandedSize() const { return m_expanded; }   // what Expand() restores
    ImVec2 CurrentSize()  const { return m_current; }
    ImVec2 WindowPos()    const { return m_pos; }
    float  TitleBarHeightPx() const;

    // Programmatic resize. Animates by default and, unlike per-frame
    // SetNextWindowSize(Cond_Always), stops touching the size once it lands.
    void SetExpandedSize(const ImVec2& size, bool animate = true);

    // One-shot request (config load, "reset layout"): applied immediately through
    // the same constraint path (no animation), then the pins release and the
    // window belongs to the user again.
    void QueueSize(const ImVec2& size);

    // ---- layout persistence (replaces the .ini when no_saved_settings) ------
    struct LayoutState { ImVec2 pos; ImVec2 size; bool collapsed; };
    LayoutState SaveLayout() const;
    void        RestoreLayout(const LayoutState& s);

    WindowConfig&       Config()       { return m_cfg; }
    const WindowConfig& Config() const { return m_cfg; }

private:
    void   AdvanceAnimations();
    void   HandleUiScaleChange();
    ImVec2 ClampSize(const ImVec2& s) const;
    ImVec2 ClampPos(const ImVec2& pos, float title_bar) const;
    ImVec2 WorkArea() const;

    void SizeConstraint(ImGuiSizeCallbackData* data);
    static void SizeConstraintThunk(ImGuiSizeCallbackData* data);

    void  DrawChrome();
    bool  DrawTitleBar(bool* p_open);
    float CornerR() const;

    WindowConfig m_cfg;

    bool   m_want_collapsed = false;
    float  m_anim_exp       = 1.0f;   // collapse/expand timeline
    float  m_anim_size      = 1.0f;   // programmatic-resize timeline (1 = idle)
    ImVec2 m_size_from      = ImVec2(0, 0);

    ImVec2 m_expanded       = ImVec2(760, 520);

    // Per-axis "the size is mine right now" flags. A pin is ENGAGED by a
    // collapse, an expand, a morph or a QueueSize, and is RELEASED only once the
    // window has visibly arrived at m_expanded on that axis. Releasing on the
    // frame the timeline ends instead would hand control back one frame early:
    // DesiredSize would still hold the previous frame's mid-animation height,
    // get clamped up to min_height, and that clamped value would then be
    // captured as the new restore size.
    bool   m_pin_height     = true;
    bool   m_pin_width      = true;

    ImVec2 m_current        = ImVec2(0, 0);
    ImVec2 m_pos            = ImVec2(0, 0);
    float  m_ui_scale       = 1.0f;

    bool   m_body_open      = false;   // Begin() returned true for the body
    bool   m_body_begun     = false;   // BeginChild was called -> End() must EndChild
    bool   m_style_pushed   = false;
    bool   m_begun          = false;
    bool   m_visible        = false;
    int    m_frames         = 0;

    bool   m_dragging       = false;
    ImVec2 m_drag_start     = ImVec2(0, 0);
};

} // namespace obsidian
