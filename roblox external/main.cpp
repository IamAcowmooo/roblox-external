#include <Windows.h>
#include <cstdio>
#include "memory.h"
#include "process.h"
#include "game.h"
#include "offsets.h"
#include "globals.h"
#include "cache.h"
#include "overlay.hpp"

#include "features/aimbot/aimbot.h"
#include "features/esp/esp.h"
#include "features/flight/flight.h"
#include "features/noclip/noclip.h"
#include "features/walkspeed/walkspeed.h"
#include "features/click_teleport/click_teleport.h"
#include "features/infinite_jump/infinite_jump.h"
#include "features/fov_changer/fov_changer.h"
#include "features/inventory_checker/inventory_checker.h"
#include "features/skybox_changer/skybox_changer.h"
#include "features/hitbox_expander/hitbox_expander.h"

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_win32.h"
#include "imgui/backends/imgui_impl_dx11.h"

void RenderMenu();
void TickKeybinds();

namespace overlay {
    void render_ui() {
        TickKeybinds();

        if (esp_enabled) {
            features::RenderESP();
            if (skeleton_esp) features::RenderSkeletonESP();
            if (chinahat) features::RenderChinaHatESP();
            if (aimviewer) features::RenderAimViewer();
            if (chams_enabled) features::RenderChams();
            if (render_expanded_hitbox) features::RenderExpandedHitbox();
        }
        if (aimbot_enabled) features::RenderFOV();
        if (inventory_checker_enabled) features::RenderInventoryChecker();

        if (g_state.menu_open) {
            // the obsidian window owns its own position/size/collapse state
            RenderMenu();
        }
    }
}

static void FeatureLoop() {
    while (true) {
        if (g_base_address) {
            if (aimbot_enabled) features::RunAimbot();
            if (flight_enabled) features::RunFlight();
            if (noclip_enabled) features::RunNoclip();
            if (walkspeed_enabled) features::RunWalkspeed();
            if (click_teleport_enabled) features::RunClickTeleport();
            if (infinite_jump_enabled) features::RunInfiniteJump();
            if (fov_changer_enabled) features::RunFovChanger();
            if (skybox_changer_enabled) features::RunSkyboxChanger();
            if (hitbox_expander_enabled) features::RunHitboxExpander();
        }
        Sleep(1);
    }
}


// returns false once the process we're attached to has exited
static bool RobloxStillAlive() {
    HANDLE h = mem::roblox_h.load();
    if (!h || h == INVALID_HANDLE_VALUE) return false;
    DWORD code = 0;
    if (!GetExitCodeProcess(h, &code)) return false;
    return code == STILL_ACTIVE;
}

static void DetachRoblox() {
    clear_instance_caches();
    g_base_address = 0;
    mem::process_id.store(0);
    HANDLE old = mem::roblox_h.exchange(nullptr);
    if (old && old != INVALID_HANDLE_VALUE) CloseHandle(old);
    g_memory.Handle = nullptr;
}

// handles the initial attach AND re-attaching after roblox is closed/reopened,
// so the overlay can be launched before roblox and just keeps working
static DWORD WINAPI AttachLoop(LPVOID) {
    bool warned_handle = false;

    while (true) {
        if (g_base_address && !RobloxStillAlive()) {
            LogLine("roblox closed - waiting for it to come back");
            DetachRoblox();
        }

        if (!g_base_address) {
            uint32_t pid = 0;
            uintptr_t base = 0;
            if (process::FindRoblox(pid, base)) {
                mem::process_id.store(pid);
                if (mem::grabroblox_h()) {
                    g_base_address = base;
                    warned_handle = false;
                    LogLine("attached to roblox - pid %u, base 0x%llx",
                            pid, (unsigned long long)base);
                } else {
                    if (!warned_handle) {
                        LogLine("found roblox but couldn't open a handle - run as administrator");
                        warned_handle = true;
                    }
                    mem::process_id.store(0);
                }
            }
        }

        Sleep(1000);
    }
    return 0;
}

_Use_decl_annotations_ int WINAPI WinMain(HINSTANCE hI, HINSTANCE hP, LPSTR lpC, int nS) {
    (void)hI; (void)hP; (void)lpC; (void)nS;

    LogLine("roblox external started");
    LogLine("waiting for roblox...");

    cache::StartThread();
    CreateThread(nullptr, 0, [](LPVOID) -> DWORD { FeatureLoop(); return 0; }, nullptr, 0, nullptr);
    CreateThread(nullptr, 0, AttachLoop, nullptr, 0, nullptr);

    overlay::run();

    cache::StopThread();
    overlay::shutdown();
    return 0;
}