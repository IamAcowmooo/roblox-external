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
    // Why it used to feel weak / intermittent: a SINGLE-frame velocity write races
    // with the game's own physics step (the humanoid/assembly solver overwrites
    // AssemblyLinearVelocity every step). If the game wrote after us, our impulse
    // was swallowed - which is exactly the "sometimes works, usually not at the
    // configured power" behaviour.
    //
    // Fix: hold the impulse open for a short window (~18ms = several physics
    // steps) and re-assert the vertical velocity every overlay tick inside that
    // window. The write still SETs vel.y (never +=), so every tap gives one
    // clean, equal jump at infinite_jump_power regardless of how fast you were
    // falling, and holding space no longer rockets you upward.

    static bool  s_space_was_down = false;
    static DWORD s_impulse_until  = 0;

    void RunInfiniteJump() {
        const cache::LocalPlayerData& lp = cache::GetLocalPlayer();
        if (!infinite_jump_enabled || !lp.valid || !is_valid_address(lp.hrp_primitive)) {
            s_space_was_down = false;
            s_impulse_until  = 0;
            return;
        }

        bool down    = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
        bool pressed = down && !s_space_was_down;   // fire once per press
        s_space_was_down = down;

        if (pressed) {
            // ~18ms of re-asserted impulse: long enough to survive the physics
            // race, short enough to still feel like a single tap-jump.
            s_impulse_until = GetTickCount() + 18;
        }

        if ((int)(GetTickCount() - s_impulse_until) >= 0) return;  // window closed

        // SET (not add) the vertical velocity so each tap gives a clean, equal
        // jump. Preserve x/z so the jump never kills your forward momentum.
        float vel[3] = {};
        if (!read_raw(lp.hrp_primitive + Offsets::Primitive::AssemblyLinearVelocity, vel, sizeof(vel))) return;
        vel[1] = infinite_jump_power;
        write_raw(lp.hrp_primitive + Offsets::Primitive::AssemblyLinearVelocity, vel, sizeof(vel));
    }
}
