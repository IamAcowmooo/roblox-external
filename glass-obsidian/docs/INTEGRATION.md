# Integrating Glass Obsidian into an existing ImGui application

This guide is for **any** Dear ImGui app that wants to swap its window chrome and
widgets for the obsidian set. It assumes you already have a working ImGui frame
loop (any backend) and Dear ImGui **1.90 or newer** (the widget set uses the
`ImGuiChildFlags` API); it is developed and verified against 1.91.1.

Nothing here is specific to any particular application — it is the same recipe
the demo (`app/demo_ui.cpp`) and both windowed mains follow.

---

## 1. Add the library to your build

Compile these translation units into your project (C++17):

```
src/palette.cpp  src/theme.cpp  src/glass.cpp  src/window.cpp
```

and put `include/` on your include path. The library uses **only the public
ImGui API** — it never includes `imgui_internal.h`, so it links cleanly against
your existing ImGui checkout (do not compile the vendored
`third_party/imgui` copy as well; one ImGui per binary).

With CMake you can instead `add_subdirectory(path/to/glass-obsidian)` and link
the `obsidian` target (it brings its own imgui core only when built standalone).

## 2. One-time setup, right after `ImGui::CreateContext()`

```cpp
obsidian::ThemeConfig cfg;
cfg.ui_scale = 1.0f;                 // fold your DPI scale in here if you like
obsidian::LoadFonts(cfg);            // Segoe UI / DejaVu / ProggyClean fallback
obsidian::ApplyTheme(obsidian::PaletteEmber(), cfg);
```

`LoadFonts` replaces the font atlas, so call it **before** the backend uploads
font textures (or invalidate/recreate them afterwards, see the demo mains).
It returns `false` if it had to fall back to ProggyClean — the UI still works.

## 3. Per frame

```cpp
obsidian::ApplyTheme(myPalette, cfg);      // cheap; safe every frame, and it is
                                           // what makes runtime palette/opacity
                                           // changes repaint instantly
obsidian::DrawAmbientBackdrop(1.0f);       // optional: ambient wash BEHIND the
                                           // window (background draw list)

bool open = true;
if (win.Begin(&open)) {
    // ... your body, only submitted while expanded ...
}
win.End();
```

`ApplyTheme` writes plain `ImGuiStyle` values, so your **stock ImGui widgets
stay visually consistent** next to the obsidian ones while you migrate
piecewise.

## 4. Replace the window + resize code

Delete every `SetNextWindowSize(..., ImGuiCond_Always)`, every state-dependent
`SetNextWindowSizeConstraints()` pair, and any hand-rolled collapse animation.
All of it is replaced by one persistent object:

```cpp
obsidian::WindowConfig wc;
wc.title        = "My Tool";
wc.initial_size = ImVec2(860, 560);
wc.min_width    = 480;             // SAME while collapsed and expanded, on purpose
obsidian::ObsidianWindow win(wc);  // construct once, keep across frames
```

Programmatic control: `win.Collapse() / Expand() / ToggleCollapse()`,
`win.SetExpandedSize(sz)` (animated), `win.QueueSize(sz)` (one-shot, clamped),
`win.SetCollapsed(bool)`. While collapsed `Begin()` returns `false` and the body
is not submitted at all, so a collapsed overlay costs ~0 widgets per frame.

Why this fixes the classic resize bugs (short version — see README for the full
explanation): sizes are decided **inside** `SetNextWindowSizeConstraints()`,
which ImGui calls every frame with this frame's desired size (no one-frame lag,
nothing overwrites the user's drag); one shared minimum width removes the
sideways pop on expand; per-axis morph pins release only when an axis actually
arrives, so interrupted animations still land exactly on target.

## 5. Widget mapping cheat-sheet

| Existing call                          | Obsidian replacement                                    |
| -------------------------------------- | ------------------------------------------------------- |
| `ImGui::Begin/End` + size constraints  | `obsidian::ObsidianWindow` (`Begin(&open)` / `End()`)    |
| `ImGui::BeginChild/EndChild` panels    | `obsidian::ScopedPanel p("id", size, flags)` (RAII) or `PanelBegin/PanelEnd` |
| `ImGui::Checkbox`                      | `obsidian::Toggle(label, &v, hint)` / `ToggleOnly(id, &v)` |
| `ImGui::SliderFloat`                   | `obsidian::Slider(label, &v, min, max, fmt, step, def, opts)` |
| `ImGui::SliderInt`                     | `obsidian::SliderInt(label, &v, min, max, fmt, def)`     |
| `ImGui::InputText`                     | `obsidian::TextInput(label, buf, size, placeholder)`     |
| `ImGui::Combo`                         | `obsidian::Combo(label, &idx, items, count)`             |
| `ImGui::Button`                        | `obsidian::Button(label, size)` / `IconButton(id, fn, size)` |
| `ImGui::BeginTabBar/BeginTabItem`      | `obsidian::TabBar(id, labels, count, &selected)` (+ icon variant) |
| `ImGui::ProgressBar`                   | `obsidian::ProgressBar(label, frac, tint)`               |
| `ImGui::CollapsingHeader` group titles | `obsidian::SectionHeader(label)`                         |
| label + value read-outs                | `obsidian::KeyValue(key, value, value_col)`              |
| `ImGui::BeginTooltip` info bubbles     | `obsidian::HelpMarker(text)`                             |

Panel height semantics: `y > 0` fixed, `y == 0` fits content, `y < 0` fills the
parent (safe for `SameLine` columns — pass `-1`, never a precomputed height).
Labels starting with `##` are IDs only and are not drawn.

## 6. Pitfalls the hard way taught us

- **Never `PushClipRectFullScreen()` inside a child window** for glows or
  shadows: it abandons the child's clip rect and the glow lands outside your
  window. `obsidian::SoftShadow` has an explicit `spill_outside_window` flag
  that only top-level chrome may use.
- **Re-bake fonts outside `NewFrame()/Render()`** — the atlas is locked inside a
  frame. See the `ui_scale` handling in `app/main_win32_dx11.cpp`.
- Pass **static/owned strings** to `WindowConfig::title` and widget labels; the
  config borrows pointers, it never copies them.
- If you draw custom vector art, derive colours from
  `obsidian::ActivePalette()` so palette switches repaint it for free.

## 7. Verifying your migration

The headless harness (`tests/headless_main.cpp`) is backend-free: it drives real
ImGui frames, asserts 13 collapse/resize invariants and writes PNG screenshots
through a software rasteriser. Point it at your own body code the way
`app/demo_ui.cpp` is plugged in and you get the same guarantees on your layout
without a GPU or window system.
