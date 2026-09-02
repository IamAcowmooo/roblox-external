#pragma once

namespace features {
    void RunSkyboxChanger();

    // shared with the menu's skybox combo. inline variables so every
    // translation unit sees them (const namespace-scope variables would
    // get internal linkage and fail to link across TUs).
    inline constexpr int k_skybox_count = 23;
    inline const char* const k_skybox_names[k_skybox_count] = {
        "Piss", "Peach", "Saku", "Purple", "Retro", "Space", "Sea", "Night V2",
        "Dark", "Anime", "Beach", "Space V2", "Pink", "Rainbow", "Forest", "Night",
        "Lava", "Rainy", "Green", "Volcanic", "Minecraft", "Lucid", "Nebulous"
    };
}
