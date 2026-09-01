#include <Windows.h>
#include "infinite_jump.h"
#include "globals.h"
#include "memory.h"
#include "cache.h"
#include "offsets.h"

namespace features {

    // Method 2 - engine driven.
    //
    // The old approach ran the jump arc ourselves by writing Position every frame.
    // That is unreliable because the humanoid state machine and the assembly solver
    // overwrite Position / velocity on every physics step, so the writes never
    // stick. Instead we ask the Humanoid itself to jump: keep JumpPower synced to
    // the slider and re-assert Humanoid::Jump = true every frame the space bar is
    // held. The engine performs the jump with its own physics, and because we keep
    // re-asserting it, it fires again the instant the character can jump again -
    // that is what makes it infinite.

    void RunInfiniteJump() {
        if (!infinite_jump_enabled) return;

        const cache::LocalPlayerData& lp = cache::GetLocalPlayer();
        if (!lp.valid || !is_valid_address(lp.humanoid_address)) return;

        bool down = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
        if (down) {
            // keep the engine's jump strength matched to the slider, then ask it to jump
            write<float>(lp.humanoid_address + Offsets::Humanoid::JumpPower, infinite_jump_power);
            write<uint8_t>(lp.humanoid_address + Offsets::Humanoid::Jump, 1);
        }
    }
}
