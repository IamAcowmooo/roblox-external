#include <Windows.h>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <vector>
#include <mutex>

#include "imgui/imgui.h"
#include "obsidian/obsidian.h"

#include "globals.h"
#include "memory.h"
#include "game.h"
#include "cache.h"
#include "offsets.h"
#include "features/skybox_changer/skybox_changer.h"
#include "features/config/config.h"

// ------------------------------------------------------------------
// GUI = Glass Obsidian (../glass-obsidian). The old hand-rolled ui::
// toolkit, custom title bar, tab pill and window-sizing preamble are
// gone; obsidian::ObsidianWindow owns the chrome (collapse animation,
// resize, drag) and every control is an obsidian widget. The ui page
// still drives transparency / rounding / accent / rainbow - they are
// folded into the palette every frame, so nothing is lost.
// ------------------------------------------------------------------
namespace obs = obsidian;

static int* s_waiting_key_ptr = nullptr;

// live status read-out shown in the title bar. obsidian::WindowConfig
// borrows the pointer, so the storage must outlive the window (static).
static char s_status_text[96] = "waiting for roblox";

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
    float w = ImGui::GetContentRegionAvail().x;
    if (s_waiting_key_ptr == &key) {
        if (obs::Button("...waiting for a key", ImVec2(w, 0), obs::ButtonKind_Primary))
            s_waiting_key_ptr = nullptr;
        return false;
    }
    char buf[96];
    snprintf(buf, sizeof(buf), "%s  [%s]", label, KeyName(key));
    if (obs::Button(buf, ImVec2(w, 0))) { s_waiting_key_ptr = &key; return true; }
    return false;
}

// ---- theme: the ui page's settings applied to a Glass Obsidian palette ----
static obs::Palette BuildMenuPalette() {
    // crimson keeps the old red-accent look; the accent is overridden below
    obs::Palette p = obs::PaletteCrimson();

    // rainbow cycles the accent; switching it off restores the saved accent
    static bool  s_rainbow_was_on = false;
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

    const ImVec4 accent(ui_accent_color[0], ui_accent_color[1], ui_accent_color[2], 1.0f);
    p.accent      = accent;
    p.accent_2    = obs::Shade(accent, -0.22f);
    p.accent_dim  = obs::Fade(accent, 0.60f);
    p.accent_glow = obs::Fade(accent, 0.30f);
    p.inner_glow  = obs::Fade(accent, 0.045f);

    // old 'menu transparency' slider (0..90% see-through) -> surface opacity
    p.opacity = 1.0f - (ui_transparency / 100.0f);

    const float r = ui_rounded_corners ? ui_corner_radius : 0.0f;
    p.corner_lg = r;
    p.corner    = r * 0.66f;
    p.corner_sm = r * 0.42f;
    return p;
}

// ---- small text helpers on top of the live palette ----
static void DimWrapped(const char* text) {
    const obs::Palette& p = obs::ActivePalette();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(obs::Fade(p.text_faint, p.TextAlphaScale()), "%s", text);
    ImGui::PopTextWrapPos();
}

static void PageHeader(const char* title, const char* subtitle) {
    const obs::Palette& p = obs::ActivePalette();
    ImFont* tf = obs::FontTitle();
    if (tf) ImGui::PushFont(tf);
    ImGui::TextColored(obs::Fade(p.text, p.TextAlphaScale()), "%s", title);
    if (tf) ImGui::PopFont();
    if (subtitle && *subtitle) DimWrapped(subtitle);
    obs::Spacer(obs::S(2));
}

static void KV(const char* key, const char* fmt, ...) {
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    obs::KeyValue(key, buf);
}

static void KVC(const char* key, const ImVec4* col, const char* fmt, ...) {
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    obs::KeyValue(key, buf, col);
}

// ---- two extra tab icons (an IconDrawFn is just drawlist/centre/size/colour)
static void IconWorld(ImDrawList* d, ImVec2 c, float s, ImU32 col) {
    d->AddCircle(c, s * 0.36f, col, 22, s * 0.09f);
    d->AddLine(ImVec2(c.x - s * 0.42f, c.y + s * 0.06f), ImVec2(c.x + s * 0.42f, c.y + s * 0.06f), col, s * 0.07f);
    d->AddLine(ImVec2(c.x - s * 0.30f, c.y - s * 0.22f), ImVec2(c.x + s * 0.30f, c.y - s * 0.22f), col, s * 0.07f);
}

static void IconInfo(ImDrawList* d, ImVec2 c, float s, ImU32 col) {
    d->AddCircle(c, s * 0.44f, col, 0, s * 0.09f);
    d->AddCircleFilled(ImVec2(c.x, c.y - s * 0.20f), s * 0.055f, col, 8);
    d->AddLine(ImVec2(c.x, c.y - s * 0.05f), ImVec2(c.x, c.y + s * 0.24f), col, s * 0.10f);
}

static const char*     kPages[8]     = { "aimbot", "esp", "misc", "world", "keybinds", "ui", "config", "debug" };
static obs::IconDrawFn kPageIcons[8] = {
    obs::icon::Crosshair, obs::icon::Eye,    obs::icon::Sliders, IconWorld,
    obs::icon::Keyboard,  obs::icon::Palette, obs::icon::Gear,   IconInfo
};

static const char* kAimTypes[]  = { "camera", "mouse" };
static const char* kBones[]     = { "head", "upper torso", "lower torso", "left hand",
                                    "right hand", "left foot", "right foot" };
static const char* kBoxStyles[] = { "full", "corners" };

// accent presets offered on the ui page (rgb hex, same tokens as the built-in
// obsidian palettes - the surfaces stay obsidian, only the accent changes)
struct AccentPreset { const char* name; unsigned int rgb; };
static const AccentPreset kAccents[] = {
    { "crimson", 0xC71414u },
    { "ember",   0xE9A13Bu },
    { "aurora",  0x59D2FEu },
    { "verdant", 0x63E6A8u },
};

// the one and only menu window. constructed on first use (inside a frame, so
// the display size is already known) and kept alive for the app's lifetime.
static obs::ObsidianWindow& MenuWindow() {
    static obs::ObsidianWindow win([] {
        obs::WindowConfig wc;
        wc.title        = "roblox external";
        wc.initial_size = ImVec2(900.0f, 600.0f);
        wc.min_width    = 560.0f;   // same while collapsed and expanded
        wc.min_height   = 380.0f;
        wc.subtitle     = s_status_text;   // borrowed pointer -> static storage
        ImVec2 d = ImGui::GetIO().DisplaySize;
        if (d.x > 0.0f && d.y > 0.0f)
            wc.initial_pos = ImVec2(d.x * 0.5f - wc.initial_size.x * 0.5f,
                                    d.y * 0.5f - wc.initial_size.y * 0.5f);
        return wc;
    }());
    return win;
}

// ==================================================================
//  pages
// ==================================================================
static void PageAimbot(const obs::Palette& pal) {
    PageHeader("aimbot", "locks the camera to the nearest target in fov");
    obs::Toggle("enabled", &aimbot_enabled);
    obs::Combo("aim type", &aimbot_aim_type, kAimTypes, IM_ARRAYSIZE(kAimTypes));
    obs::Combo("target bone", &aimbot_part, kBones, IM_ARRAYSIZE(kBones));
    obs::Toggle("sticky aim", &sticky_aim);
    obs::Toggle("prediction", &prediction_enabled);
    if (prediction_enabled) {
        obs::Slider("pred x", &prediction_x, 1.0f, 50.0f, "%.0f");
        obs::Slider("pred y", &prediction_y, 1.0f, 50.0f, "%.0f");
    }
    obs::Slider("smooth x", &smoothing_x, 2.0f, 20.0f, "%.1f");
    obs::Slider("smooth y", &smoothing_y, 2.0f, 20.0f, "%.1f");
    obs::Toggle("humanizer", &humanizer_enabled);
    if (humanizer_enabled) {
        obs::Slider("humanize amount", &humanizer_strength, 0.0f, 1.0f, "%.2f");
        DimWrapped("reaction delay + curved, slightly jittery aim");
    }
    obs::Slider("fov size", &fov_size, 10.0f, 500.0f, "%.0f");
    obs::Toggle("show fov", &show_fov);
    ImGui::Separator();
    obs::Toggle("team check", &team_check);
    obs::Toggle("wall check", &wall_check);
    if (wall_check) DimWrapped("won't lock through another player's body");
    (void)pal;
}

static void PageEsp(const obs::Palette& pal) {
    PageHeader("esp", "player boxes, bars and extras");
    obs::Toggle("enabled", &esp_enabled);
    ImGui::Separator();
    obs::Toggle("box", &box_esp);
    if (box_esp) {
        obs::Combo("box style", &box_esp_type, kBoxStyles, IM_ARRAYSIZE(kBoxStyles));
        obs::Toggle("fill", &box_fill);
        if (box_fill) {
            obs::Toggle("gradient", &box_fill_gradient);
            if (box_fill_gradient) {
                obs::Toggle("rotate", &box_fill_gradient_rotate);
                ImGui::ColorEdit4("fill top", box_fill_top);
                ImGui::ColorEdit4("fill bottom", box_fill_bottom);
            }
            ImGui::ColorEdit4("fill color", box_fill_top);
        }
        ImGui::ColorEdit4("box color", box_esp_color);
    }
    obs::Toggle("health bar", &healthbar);
    if (healthbar) ImGui::ColorEdit4("health bar color", healthbar_color);
    obs::Toggle("health text", &health_text);
    if (health_text) ImGui::ColorEdit4("health text color", health_text_color);
    obs::Toggle("name", &name);
    if (name) ImGui::ColorEdit4("name color", name_color);
    obs::Toggle("distance", &distance);
    if (distance) ImGui::ColorEdit4("distance color", distance_color);
    obs::Toggle("rig type", &rig_type);
    if (rig_type) ImGui::ColorEdit4("rig type color", rig_type_color);
    obs::Toggle("tool", &tool_esp);
    if (tool_esp) ImGui::ColorEdit4("tool color", tool_color);
    obs::Slider("render dist", &esp_render_distance, 0.0f, 2000.0f, "%.0f");
    obs::Toggle("team check", &team_check);
    obs::Toggle("wall check", &wall_check);
    if (wall_check) DimWrapped("hides players occluded by another player");
    ImGui::Separator();
    obs::Toggle("skeleton", &skeleton_esp);
    if (skeleton_esp) ImGui::ColorEdit4("skeleton color", skeleton_color);
    obs::Toggle("aim viewer", &aimviewer);
    obs::Toggle("china hat", &chinahat);
    if (chinahat) ImGui::ColorEdit4("hat color", chinahat_color);
    ImGui::Separator();
    obs::Toggle("chams", &chams_enabled);
    if (chams_enabled) ImGui::ColorEdit4("chams color", chams_color);
    ImGui::Separator();
    obs::Toggle("expanded hitbox", &render_expanded_hitbox);
    if (render_expanded_hitbox) {
        obs::Toggle("hitbox expander", &hitbox_expander_enabled);
        obs::Slider("hitbox size", &hitbox_expander_value, 1.0f, 50.0f, "%.0f");
    }
    (void)pal;
}

static void PageMisc(const obs::Palette& pal) {
    PageHeader("misc", "movement and player features");
    DimWrapped("binds are optional - 'none' means always on while enabled");
    ImGui::Separator();
    obs::Toggle("noclip", &noclip_enabled);
    ImGui::Separator();
    obs::Toggle("walkspeed", &walkspeed_enabled);
    if (walkspeed_enabled) obs::Slider("speed", &walkspeed_value, 0.0f, 200.0f, "%.0f");
    ImGui::Separator();
    obs::Toggle("flight", &flight_enabled);
    if (flight_enabled) {
        obs::Slider("fly speed", &flight_value, 10.0f, 250.0f, "%.0f");
        obs::Toggle("hold instead of toggle", &flight_hold_mode);
        DimWrapped("wasd = move, space = up, lshift/lctrl = down");
    }
    ImGui::Separator();
    obs::Toggle("click teleport", &click_teleport_enabled);
    if (click_teleport_enabled) {
        obs::Slider("tp distance", &click_teleport_distance, 5.0f, 200.0f, "%.0f studs");
        DimWrapped("left-click in-game to teleport there (no keybind)");
    }
    ImGui::Separator();
    obs::Toggle("infinite jump", &infinite_jump_enabled);
    if (infinite_jump_enabled) {
        obs::Slider("jump power", &infinite_jump_power, 25.0f, 150.0f, "%.0f");
        DimWrapped("tap space in mid-air to jump again");
    }
    ImGui::Separator();
    obs::Toggle("fov changer", &fov_changer_enabled);
    if (fov_changer_enabled) obs::Slider("field of view", &fov_value, 20.0f, 120.0f, "%.0f");
    ImGui::Separator();
    obs::Toggle("inventory checker", &inventory_checker_enabled);
    if (inventory_checker_enabled) DimWrapped("cursor over a player (or hold the key if bound)");
    (void)pal;
}

static void PageWorld(const obs::Palette& pal) {
    PageHeader("world", "replace the game's skybox");
    obs::Toggle("skybox changer", &skybox_changer_enabled);
    if (skybox_changer_enabled) {
        if (skybox_type < 0 || skybox_type >= features::k_skybox_count) skybox_type = 0;
        obs::Combo("skybox", &skybox_type, features::k_skybox_names, features::k_skybox_count);
        if (skybox_debug_msg[0]) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(obs::Fade(pal.text_dim, pal.TextAlphaScale()), "%s", skybox_debug_msg);
            ImGui::PopTextWrapPos();
        }
    }
}

static void PageKeybinds(const obs::Palette& pal) {
    PageHeader("keybinds", "click a bind then press any key or mouse button");
    ImGui::Separator();

    obs::SectionHeader("menu");
    keybind_button("toggle menu", menu_toggle_keybind);
    if (menu_toggle_keybind == 0)
        ImGui::TextColored(obs::Fade(pal.danger, 1.0f),
                           "unbound - you won't be able to reopen the menu!");

    obs::SectionHeader("combat");
    keybind_button("aimbot", aimbot_keybind);

    obs::SectionHeader("movement");
    keybind_button("noclip", noclip_keybind);
    keybind_button("walkspeed", walkspeed_keybind);
    keybind_button("flight", flight_keybind);
    keybind_button("inventory checker", inventory_checker_keybind);

    ImGui::Separator();
    DimWrapped("leave a bind as 'none' to keep the feature");
    DimWrapped("always on; set a key to hold-to-use instead.");
}

static void PageUi(const obs::Palette& pal) {
    PageHeader("ui", "glass obsidian theme controls");
    const float w = ImGui::GetContentRegionAvail().x;

    obs::SectionHeader("window");
    obs::Slider("menu transparency", &ui_transparency, 0.0f, 90.0f, "%.0f%%");
    obs::Toggle("rounded corners", &ui_rounded_corners);
    if (ui_rounded_corners) obs::Slider("corner radius", &ui_corner_radius, 0.0f, 24.0f, "%.0f px");
    obs::Spacer(obs::S(4));

    obs::SectionHeader("accent");
    ImGui::ColorEdit3("accent color", ui_accent_color);
    obs::Toggle("rainbow accent", &ui_rainbow);

    // preset swatches: click to adopt that accent
    {
        const int      n     = IM_ARRAYSIZE(kAccents);
        const float    gap   = obs::S(8);
        const float    cw    = (w - gap * (n - 1)) / n;
        const float    ch    = obs::S(30);
        ImDrawList*    d     = ImGui::GetWindowDrawList();
        const float    line_h = ImGui::GetTextLineHeight();
        for (int i = 0; i < n; ++i) {
            if (i) ImGui::SameLine(0, gap);
            ImGui::PushID(i);
            const ImVec2 a = ImGui::GetCursorScreenPos();
            const bool clicked = ImGui::InvisibleButton("##swatch", ImVec2(cw, ch));
            const bool hov     = ImGui::IsItemHovered();
            const ImVec2 b(a.x + cw, a.y + ch);
            const ImVec4 col = obs::Hex(kAccents[i].rgb, 1.0f);

            obs::GradientFillRounded(d, a, b, obs::Shade(col, 0.12f), obs::Shade(col, -0.18f),
                                     pal.corner_sm);
            if (hov) obs::StrokeEdge(d, a, b, obs::Fade(pal.text, 0.65f), pal.corner_sm);

            const float tw = obs::TextWidth(nullptr, kAccents[i].name);
            d->AddText(ImVec2(a.x + (cw - tw) * 0.5f, a.y + (ch - line_h) * 0.5f),
                       ImGui::ColorConvertFloat4ToU32(pal.text_on_accent), kAccents[i].name);
            if (clicked) {
                ui_accent_color[0] = col.x;
                ui_accent_color[1] = col.y;
                ui_accent_color[2] = col.z;
            }
            ImGui::PopID();
        }
    }
    obs::Spacer(obs::S(4));

    obs::SectionHeader("reset");
    if (obs::Button("reset to default theme", ImVec2(w, 0))) {
        ui_transparency    = 6.0f;
        ui_rounded_corners = true;
        ui_corner_radius   = 16.0f;
        ui_rainbow         = false;
        ui_accent_color[0] = 0.78f; ui_accent_color[1] = 0.08f; ui_accent_color[2] = 0.08f;
    }
}

static void PageConfig(const obs::Palette& pal) {
    PageHeader("config", "save and load named presets");
    const float w = ImGui::GetContentRegionAvail().x;

    static char config_name_buf[128] = "";
    static char rename_buf[128] = "";
    static std::vector<std::string> config_list = config::GetConfigList();
    static int selected_config = -1;

    obs::TextInput("config name", config_name_buf, sizeof(config_name_buf), "new config name");

    const float bw = (w - obs::S(8) * 2.0f) / 3.0f;
    if (obs::Button("save", ImVec2(bw, 0), obs::ButtonKind_Primary)) {
        if (config_name_buf[0] != '\0') {
            config::Save(config_name_buf);
            config_list = config::GetConfigList();
        }
    }
    ImGui::SameLine(0, obs::S(8));
    if (obs::Button("load", ImVec2(bw, 0))) {
        if (config_name_buf[0] != '\0') config::Load(config_name_buf);
    }
    ImGui::SameLine(0, obs::S(8));
    if (obs::Button("delete", ImVec2(bw, 0), obs::ButtonKind_Danger)) {
        if (config_name_buf[0] != '\0') {
            config::Delete(config_name_buf);
            config_list = config::GetConfigList();
            selected_config = -1;
        }
    }

    obs::Spacer(obs::S(2));
    obs::TextInput("rename to", rename_buf, sizeof(rename_buf), "new name");
    if (obs::Button("rename", ImVec2(w, 0))) {
        if (config_name_buf[0] != '\0' && rename_buf[0] != '\0') {
            config::Rename(config_name_buf, rename_buf);
            config_list = config::GetConfigList();
            snprintf(config_name_buf, sizeof(config_name_buf), "%s", rename_buf);
        }
    }

    obs::Spacer(obs::S(2));
    if (obs::Button("open config folder", ImVec2(w, 0)))
        config::OpenConfigFolder();

    obs::Spacer(obs::S(4));
    obs::SectionHeader("saved configs");
    if (obs::Button("refresh list", ImVec2(w, 0))) {
        config_list = config::GetConfigList();
        selected_config = -1;
    }

    if (obs::PanelBegin("cfglist", ImVec2(0, obs::S(150)), obs::PanelFlags_Inset)) {
        for (int i = 0; i < (int)config_list.size(); ++i) {
            bool is_selected = (selected_config == i);
            if (ImGui::Selectable(config_list[i].c_str(), is_selected)) {
                selected_config = i;
                snprintf(config_name_buf, sizeof(config_name_buf), "%s", config_list[i].c_str());
            }
        }
    }
    obs::PanelEnd();
    (void)pal;
}

static void PageDebug(const obs::Palette& pal) {
    PageHeader("debug", "diagnostics, logs and offset write tests");
    DimWrapped("if a feature does nothing, check these values. 0x0 or 'INVALID' means "
               "that offset is wrong for your client version.");
    ImGui::Separator();

    KV("base address", "0x%llX", (unsigned long long)g_base_address);

    instance ve = read<instance>(g_base_address + Offsets::VisualEngine::Pointer);
    KVC("visual engine", ve.is_valid() ? &pal.text : &pal.danger,
        "0x%llX%s", (unsigned long long)ve.address, ve.is_valid() ? "" : "  <- INVALID");

    instance dm = game::ReadDatamodel(g_base_address);
    KVC("datamodel", dm.is_valid() ? &pal.text : &pal.danger,
        "0x%llX%s", (unsigned long long)dm.address, dm.is_valid() ? "" : "  <- INVALID");

    if (dm.is_valid()) {
        KV("game name", "%s", dm.get_name().c_str());
        uint64_t place_id = read<uint64_t>(dm.address + Offsets::DataModel::PlaceId);
        bool loaded = read<bool>(dm.address + Offsets::DataModel::GameLoaded);
        KV("place id", "%llu", (unsigned long long)place_id);
        KV("game loaded", "%s", loaded ? "yes" : "no");

        if (place_id == 0) {
            ImGui::TextColored(obs::Fade(pal.warn, 1.0f),
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

        KVC("viewport", (dims[0] > 0.0f && dims[1] > 0.0f) ? &pal.text : &pal.danger,
            "%.0f x %.0f%s", dims[0], dims[1],
            (dims[0] > 0.0f && dims[1] > 0.0f) ? "" : "  <- INVALID (Dimensions offset wrong)");
        KV("view matrix", "%.2f %.2f %.2f %.2f", view[0], view[1], view[2], view[3]);
        KV(" ", "%.2f %.2f %.2f %.2f", view[4], view[5], view[6], view[7]);

        bool all_zero = true;
        for (int i = 0; i < 16; ++i) if (view[i] != 0.0f) { all_zero = false; break; }
        if (all_zero)
            ImGui::TextColored(obs::Fade(pal.danger, 1.0f),
                               "view matrix is all zero - ViewMatrix offset is wrong");
    }

    ImGui::Separator();

    const cache::LocalPlayerData& lp = cache::GetLocalPlayer();
    KVC("local player", lp.valid ? &pal.ok : &pal.danger, "%s", lp.valid ? "ok" : "INVALID");
    KV("humanoid", "0x%llX", (unsigned long long)lp.humanoid_address);
    KVC("hrp primitive", is_valid_address(lp.hrp_primitive) ? &pal.text : &pal.danger,
        "0x%llX", (unsigned long long)lp.hrp_primitive);
    KV("local pos", "%.1f, %.1f, %.1f", lp.x, lp.y, lp.z);

    auto ents_snap = cache::GetEspSnapshot();
    const auto& ents = *ents_snap;
    KV("cached players", "%d", (int)ents.size());
    if (ents.empty())
        ImGui::TextColored(obs::Fade(pal.warn, 1.0f),
                           "no other players cached (join a populated server)");

    ImGui::Separator();
    obs::SectionHeader("write test");
    DimWrapped("flight / click teleport write to the root part's Primitive. "
               "this proves whether those writes actually land.");

    if (is_valid_address(lp.humanoid_address)) {
        uintptr_t probe = read<uintptr_t>(lp.humanoid_address + Offsets::Humanoid::HumanoidRootPart);
        KV("hrp via 0x478", "0x%llX%s", (unsigned long long)probe,
           is_valid_address(probe) ? "" : "  (unused - offset is wrong)");
    }

    uintptr_t prim = lp.hrp_primitive;
    const float w = ImGui::GetContentRegionAvail().x;

    if (is_valid_address(prim)) {
        float pos[3] = {};
        read_raw(prim + Offsets::Primitive::Position, pos, sizeof(pos));
        KV("position read", "%.1f, %.1f, %.1f", pos[0], pos[1], pos[2]);

        float vel[3] = {};
        read_raw(prim + Offsets::Primitive::AssemblyLinearVelocity, vel, sizeof(vel));
        KV("velocity read", "%.1f, %.1f, %.1f", vel[0], vel[1], vel[2]);

        float psz[3] = {};
        read_raw(prim + Offsets::Primitive::Size, psz, sizeof(psz));
        KVC("part size read", (psz[0] > 0.05f || psz[1] > 0.05f) ? &pal.text : &pal.warn,
            "%.2f, %.2f, %.2f%s", psz[0], psz[1], psz[2],
            (psz[0] > 0.05f || psz[1] > 0.05f) ? "" : "  <- zero, esp uses canonical sizes");

        static char test_result[192] = "not run yet";

        if (obs::Button("test POSITION write (+10 studs up)", ImVec2(w, 0))) {
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

        if (obs::Button("test VELOCITY write (launch up)", ImVec2(w, 0))) {
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

        if (obs::Button("probe primitive floats (+0xA0..+0x1C0)", ImVec2(w, 0))) {
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

        DimWrapped(test_result);
    } else {
        ImGui::TextColored(obs::Fade(pal.warn, 1.0f),
                           "root part not resolved - spawn in, or names are failing");
    }

    ImGui::Separator();
    obs::SectionHeader("log");
    if (obs::Button("clear")) {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        g_log_lines.clear();
    }
    ImGui::SameLine(0, obs::S(6));
    if (obs::Button("copy")) {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        std::string all;
        for (const std::string& l : g_log_lines) { all += l; all += "\n"; }
        ImGui::SetClipboardText(all.c_str());
    }
    ImGui::SameLine(0, obs::S(6));
    ImGui::TextDisabled("paste it back in chat");

    if (obs::PanelBegin("logpanel", ImVec2(0, obs::S(190)), obs::PanelFlags_Inset)) {
        {
            std::lock_guard<std::mutex> lock(g_log_mutex);
            for (const std::string& l : g_log_lines)
                ImGui::TextUnformatted(l.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
            ImGui::SetScrollHereY(1.0f);
    }
    obs::PanelEnd();
}

// ==================================================================
//  menu frame
// ==================================================================
void RenderMenu() {
    const obs::Palette pal = BuildMenuPalette();
    obs::ApplyTheme(pal, obs::ActiveConfig());

    // title-bar status read-out (buffer is static; the window borrows it)
    if (g_base_address)
        snprintf(s_status_text, sizeof(s_status_text),
                 "attached \xc2\xb7 pid %u", (unsigned)mem::process_id.load());
    else
        snprintf(s_status_text, sizeof(s_status_text), "waiting for roblox");

    obs::ObsidianWindow& win = MenuWindow();
    win.Config().subtitle_tint = g_base_address ? pal.ok : pal.warn;

    static int s_page = 0;

    bool open = true;
    if (win.Begin(&open)) {
        obs::TabBarIcons("pages", kPages, kPageIcons, IM_ARRAYSIZE(kPages), &s_page);
        obs::Spacer(obs::S(10));

        // page panel fills the window except for the slim footer below it
        const float footer_h = obs::S(18);
        const float body_h   = ImGui::GetContentRegionAvail().y - footer_h - obs::S(10);
        if (obs::PanelBegin("page", ImVec2(0, body_h))) {
            ImGui::PushItemWidth(-1.0f);

            switch (s_page) {
            case 0:  PageAimbot(pal);  break;
            case 1:  PageEsp(pal);     break;
            case 2:  PageMisc(pal);    break;
            case 3:  PageWorld(pal);   break;
            case 4:  PageKeybinds(pal);break;
            case 5:  PageUi(pal);      break;
            case 6:  PageConfig(pal);  break;
            default: PageDebug(pal);   break;
            }

            ImGui::PopItemWidth();
        }
        obs::PanelEnd();

        // footer status line
        {
            char left[96], right[96];
            if (g_base_address)
                snprintf(left, sizeof(left), "attached  pid %u", (unsigned)mem::process_id.load());
            else
                snprintf(left, sizeof(left), "waiting for roblox...");
            snprintf(right, sizeof(right), "%s toggles menu", KeyName(menu_toggle_keybind));

            const ImVec4 col = obs::Fade(pal.text_faint, pal.TextAlphaScale());
            ImGui::TextColored(col, "%s", left);
            const float rw = obs::TextWidth(nullptr, right);
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - rw);
            ImGui::TextColored(col, "%s", right);
        }
    }
    win.End();

    if (!open) g_request_exit = true;
}
