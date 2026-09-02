// -----------------------------------------------------------------------------
//  src/palette.cpp  --  built-in Glass Obsidian palettes
// -----------------------------------------------------------------------------
#include "obsidian/palette.h"

#include <cstring>

namespace obsidian {
namespace {

// The shared obsidian surface ramp. Accents are layered on top of this so every
// preset keeps identical contrast between text and surface.
Palette BaseObsidian()
{
    Palette p;

    // surfaces, ascending elevation
    p.backdrop      = Hex(0x05060A, 1.00f);
    p.window_top    = Hex(0x141821, 0.97f);
    p.window_bottom = Hex(0x090C12, 0.98f);
    p.header        = Hex(0x0E121A, 0.99f);
    p.panel         = Hex(0x171D29, 0.52f);
    p.panel_hover   = Hex(0x1E2532, 0.66f);
    p.field         = Hex(0x0A0E15, 0.86f);
    p.field_hover   = Hex(0x131926, 0.92f);
    p.field_active  = Hex(0x182031, 0.96f);
    p.popup         = Hex(0x0C1017, 0.98f);
    p.shadow        = Hex(0x000000, 0.62f);

    // lines & light
    p.edge          = Hex(0x2E384A, 1.00f);
    p.edge_soft     = Hex(0x1D2431, 1.00f);
    p.sheen         = Hex(0xFFFFFF, 0.055f);
    p.inner_glow    = Hex(0xFFFFFF, 0.020f);

    // text
    p.text          = Hex(0xE8ECF4, 1.00f);
    p.text_dim      = Hex(0x99A2B4, 1.00f);
    p.text_faint    = Hex(0x5F6879, 1.00f);

    // semantics
    p.ok            = Hex(0x5BC98C, 1.00f);
    p.warn          = Hex(0xE5B84B, 1.00f);
    p.danger        = Hex(0xE4614F, 1.00f);
    p.info          = Hex(0x59A8E8, 1.00f);

    p.opacity       = 1.0f;
    return p;
}

// Finish a palette: derive the accent-dependent tokens from the accent pair.
Palette WithAccent(Palette p, unsigned int a1, unsigned int a2, unsigned int on_accent)
{
    p.accent         = Hex(a1, 1.00f);
    p.accent_2       = Hex(a2, 1.00f);
    p.accent_dim     = Fade(Mix(p.accent, p.text_faint, 0.55f), 0.75f);
    p.accent_glow    = Hex(a1, 0.30f);
    p.text_on_accent = Hex(on_accent, 1.00f);

    // A whisper of the accent inside the window so the glass feels lit by it.
    p.inner_glow     = Hex(a1, 0.045f);
    p.field_active   = Mix(p.field_active, p.accent, 0.10f);
    p.panel_hover    = Mix(p.panel_hover, p.accent, 0.05f);
    return p;
}

} // namespace

Palette PaletteEmber()
{
    // Molten gold over cold volcanic glass. Dark text on the accent fill.
    Palette p = BaseObsidian();
    p.window_top    = Hex(0x15171E, 0.97f);   // faint warm shift
    p.window_bottom = Hex(0x0A0B10, 0.98f);
    return WithAccent(p, 0xE9A13B, 0xD9553F, 0x0A0C11);
}

Palette PaletteAurora()
{
    Palette p = BaseObsidian();
    p.window_top    = Hex(0x121826, 0.97f);
    p.window_bottom = Hex(0x080A12, 0.98f);
    return WithAccent(p, 0x59D2FE, 0x8B7CF6, 0x05121A);
}

Palette PaletteVerdant()
{
    Palette p = BaseObsidian();
    p.window_top    = Hex(0x121A19, 0.97f);
    p.window_bottom = Hex(0x070C0B, 0.98f);
    return WithAccent(p, 0x63E6A8, 0x3FBF8F, 0x04140D);
}

Palette PaletteCrimson()
{
    Palette p = BaseObsidian();
    p.window_top    = Hex(0x191419, 0.97f);
    p.window_bottom = Hex(0x0C080B, 0.98f);
    // Dark red fill -> light text.
    return WithAccent(p, 0xF2637C, 0xB4304F, 0xFFF1F3);
}

const Palette* PaletteByName(const char* name)
{
    if (!name) return nullptr;
    static const Palette ember   = PaletteEmber();
    static const Palette aurora  = PaletteAurora();
    static const Palette verdant = PaletteVerdant();
    static const Palette crimson = PaletteCrimson();

    const ImVec4* key = nullptr; (void)key;
    if (strcmp(name, "Ember")   == 0) return &ember;
    if (strcmp(name, "Aurora")  == 0) return &aurora;
    if (strcmp(name, "Verdant") == 0) return &verdant;
    if (strcmp(name, "Crimson") == 0) return &crimson;
    return nullptr;
}

const char* const* PaletteNameList(int* out_count)
{
    static const char* names[] = { "Ember", "Aurora", "Verdant", "Crimson" };
    if (out_count) *out_count = (int)IM_ARRAYSIZE(names);
    return names;
}

} // namespace obsidian
