#include <Windows.h>
#include "infinite_jump.h"
#include "globals.h"
#include "memory.h"
#include "cache.h"
#include "offsets.h"

namespace features {

    // True infinite jump: each fresh press of space = ONE jump impulse, and it
    // works mid-air, so you can tap-tap-tap to chain jumps forever.
    //
    // The old version re-asserted Humanoid::Jump = true every frame the bar was
    // HELD, which made holding space rocket you straight up forever. Instead we
    // edge-trigger on the press and write the upward velocity directly — the
    // velocity write is the one Primitive write confirmed working, and because
    // it doesn't care whether you're grounded, jumps chain in the air. Gravity
    // still arcs each jump back down, so it feels like jumping, not flying.

    static bool s_space_was_down = false;

    void RunInfiniteJump() {
        const cache::LocalPlayerData& lp = cache::GetLocalPlayer();
        if (!infinite_jump_enabled || !lp.valid || !is_valid_address(lp.hrp_primitive)) {
            s_space_was_down = false;
            return;
        }

        bool down = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
        bool pressed = down && !s_space_was_down;   // fire once per press
        s_space_was_down = down;
        if (!pressed) return;

        // set (not add) the vertical velocity so each tap gives a clean, equal
        // jump regardless of how fast you were falling
        float vel[3] = {};
        read_raw(lp.hrp_primitive + Offsets::Primitive::AssemblyLinearVelocity, vel, sizeof(vel));
        vel[1] = infinite_jump_power;
        write_raw(lp.hrp_primitive + Offsets::Primitive::AssemblyLinearVelocity, vel, sizeof(vel));
    }
}

