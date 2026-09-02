// -----------------------------------------------------------------------------
//  app/demo_ui.h  --  the Glass Obsidian showcase / test bench UI
// -----------------------------------------------------------------------------
//  Platform-agnostic: the Win32+DX11 main, the SDL2+GL3 main and the headless
//  verification harness all drive this same code, so what you see in the
//  screenshots is exactly what a windowed build draws.
// -----------------------------------------------------------------------------
#pragma once

#include "obsidian/obsidian.h"

#include <string>
#include <vector>

namespace demo {

struct Invariant
{
    std::string name;
    bool        pass = false;
    std::string detail;
};

struct State
{
    // ---- appearance ---------------------------------------------------------
    int   palette    = 0;      // index into obsidian::PaletteNameList()
    float opacity    = 1.00f;  // 0.35 .. 1.0
    float ui_scale   = 1.00f;  // 0.85 .. 1.5
    float corner     = 12.0f;

    // ---- navigation ---------------------------------------------------------
    int tab = 0;

    // ---- component playground ----------------------------------------------
    bool wireframe = false, shadows = true, vsync = true;
    bool telemetry = false, auto_save = true, notifications = true;
    bool high_contrast = false, reduce_motion = false;

    float volume = 62.0f, brightness = 0.80f, blur = 6.0f;
    float sensitivity = 1.00f, zoom = 90.0f, panel_opacity = 0.90f;
    int   quality = 2, samples = 1, export_mode = 0;

    char project[64] = "obsidian-ui-kit";
    char filter[64]  = "";

    float progress = 0.68f;
    int   inner_tab = 0;

    // ---- window bench -------------------------------------------------------
    std::vector<Invariant> invariants;
    int stress_frames = 0;     // set >0 to auto-toggle collapse every frame
};

// Build the palette described by the state (preset + opacity + corner radius).
obsidian::Palette BuildPalette(const State& st);

// Draw the body. Call between ObsidianWindow::Begin() and ::End().
void Draw(State& st, obsidian::ObsidianWindow& win);

const char* TabLabel(int i);
int         TabCount();

} // namespace demo
