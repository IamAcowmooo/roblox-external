// -----------------------------------------------------------------------------
//  obsidian/palette.h  --  design tokens for the "Glass Obsidian" UI language
// -----------------------------------------------------------------------------
//  Every colour/size the theme and the widget layer can consult lives here, so
//  re-skinning the whole UI is a matter of swapping one Palette struct.
//
//  Conventions
//    * Colours are authored as 8-bit hex (0xRRGGBB) + alpha, then normalised to
//      the ImVec4 (0..1) range ImGui expects.
//    * Surfaces are ordered by elevation:  backdrop < window < panel < field.
//      Higher elevation == lighter == closer to the viewer. Keeping that
//      monotonic is what makes the UI read as "layered glass" instead of noise.
//    * `sheen` is the specular highlight along the TOP edge of a surface
//      (light from above). `edge` is the hairline silhouette. Both are drawn at
//      very low alpha; they do the heavy lifting for the glass read.
//
//  Public domain / MIT -- see LICENSE.
// -----------------------------------------------------------------------------
#pragma once

#include "imgui.h"

namespace obsidian {

// 0xRRGGBB -> ImVec4 with alpha
inline ImVec4 Hex(unsigned int rgb, float a = 1.0f)
{
    return ImVec4(((rgb >> 16) & 0xFF) / 255.0f,
                  ((rgb >>  8) & 0xFF) / 255.0f,
                  ((rgb >>  0) & 0xFF) / 255.0f,
                  a);
}

// Multiply only the alpha channel (used everywhere for the global opacity knob).
inline ImVec4 Fade(const ImVec4& c, float a) { return ImVec4(c.x, c.y, c.z, c.w * a); }

// Perceptual-ish lerp between two colours.
inline ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t)
{
    return ImVec4(a.x + (b.x - a.x) * t,
                  a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t,
                  a.w + (b.w - a.w) * t);
}

// Lighten (t>0) or darken (t<0) towards white/black without touching alpha.
inline ImVec4 Shade(const ImVec4& c, float t)
{
    return t >= 0.0f ? Mix(c, ImVec4(1, 1, 1, c.w), t)
                     : Mix(c, ImVec4(0, 0, 0, c.w), -t);
}

struct Palette
{
    // ---- surfaces (ascending elevation) -------------------------------------
    ImVec4 backdrop;        // the void behind everything (desktop / clear colour)
    ImVec4 window_top;      // main window gradient, top stop
    ImVec4 window_bottom;   // main window gradient, bottom stop
    ImVec4 header;          // title bar
    ImVec4 panel;           // inset panel / group
    ImVec4 panel_hover;     // inset panel, hovered
    ImVec4 field;           // input / slider track
    ImVec4 field_hover;
    ImVec4 field_active;
    ImVec4 popup;           // combo & context menus
    ImVec4 shadow;          // drop shadow tint

    // ---- lines & light ------------------------------------------------------
    ImVec4 edge;            // hairline silhouette
    ImVec4 edge_soft;       // interior separators
    ImVec4 sheen;           // top-edge specular highlight
    ImVec4 inner_glow;      // faint accent wash inside the window

    // ---- text ---------------------------------------------------------------
    ImVec4 text;
    ImVec4 text_dim;
    ImVec4 text_faint;
    ImVec4 text_on_accent;  // label colour that sits on a filled accent surface

    // ---- accent -------------------------------------------------------------
    ImVec4 accent;          // primary
    ImVec4 accent_2;        // secondary -- gradients run accent -> accent_2
    ImVec4 accent_dim;      // unfocused / disabled accent
    ImVec4 accent_glow;     // bloom tint around active controls

    // ---- semantics ----------------------------------------------------------
    ImVec4 ok;
    ImVec4 warn;
    ImVec4 danger;
    ImVec4 info;

    // ---- geometry -----------------------------------------------------------
    float corner_lg = 12.0f;   // windows, popups
    float corner    =  8.0f;   // panels, buttons, fields
    float corner_sm =  5.0f;   // small controls, knobs, badges
    float corner_pill = 999.0f;// fully rounded (clamped to half-height at draw)

    // ---- global opacity -----------------------------------------------------
    // Scales the alpha of every surface. 1.0 = opaque card, 0.35 = see-through
    // glass. Text/edge alpha is scaled by a gentler curve so the UI stays
    // legible when the surfaces go transparent.
    float opacity = 1.0f;

    float SurfaceAlphaScale() const { return opacity; }
    float TextAlphaScale()    const { return 0.35f + 0.65f * opacity; }
    float EdgeAlphaScale()    const { return 0.55f + 0.45f * opacity; }
};

// ---------------------------------------------------------------------------
//  Built-in palettes.
//  All four share the same obsidian surface ramp; only the accent pair (and a
//  hint of tint in the surfaces) changes, so switching preset never breaks
//  contrast ratios for text.
// ---------------------------------------------------------------------------
Palette PaletteEmber();     // molten gold over cold volcanic glass  (default)
Palette PaletteAurora();    // cyan -> violet iridescence
Palette PaletteVerdant();   // jade / mint
Palette PaletteCrimson();   // garnet / rose

// Named lookup, handy for a settings dropdown. Returns nullptr if unknown.
const Palette* PaletteByName(const char* name);
const char* const* PaletteNameList(int* out_count);

} // namespace obsidian
