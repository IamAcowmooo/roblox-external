#include <Windows.h>
#include <chrono>
#include "walkspeed.h"
#include "globals.h"
#include "memory.h"
#include "cache.h"
#include "offsets.h"

namespace features {

    static float cached_walkspeed = 16.0f;
    static bool  active = false;

    void RunWalkspeed() {
        const cache::LocalPlayerData& lp = cache::GetLocalPlayer();

        // no keybind set -> always on while enabled; otherwise hold-to-use
        bool want = walkspeed_enabled &&
                    (walkspeed_keybind == 0 || (GetAsyncKeyState(walkspeed_keybind) & 0x8000) != 0);

        if (want) {
            if (!active) {
                // remember the original so it can be restored when we stop
                if (lp.valid && is_valid_address(lp.humanoid_address)) {
                    cached_walkspeed = read<float>(lp.humanoid_address + Offsets::Humanoid::Walkspeed);
                    if (cached_walkspeed <= 0 || cached_walkspeed > 200) cached_walkspeed = 16.0f;
                }
                active = true;
            }
            if (lp.valid && is_valid_address(lp.humanoid_address)) {
                write<float>(lp.humanoid_address + Offsets::Humanoid::Walkspeed, walkspeed_value);
                write<float>(lp.humanoid_address + Offsets::Humanoid::WalkspeedCheck, walkspeed_value);
            }
        } else if (active) {
            if (lp.valid && is_valid_address(lp.humanoid_address)) {
                write<float>(lp.humanoid_address + Offsets::Humanoid::Walkspeed, cached_walkspeed);
                write<float>(lp.humanoid_address + Offsets::Humanoid::WalkspeedCheck, cached_walkspeed);
            }
            active = false;
        }
    }
}

