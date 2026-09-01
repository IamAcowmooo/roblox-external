#include <Windows.h>
#include <cmath>
#include <unordered_map>
#include "flight.h"
#include "globals.h"
#include "memory.h"
#include "cache.h"
#include "offsets.h"
#include "game.h"

namespace features {

    // ---------------------------------------------------------------------
    // Why this is written the way it is:
    //
    //   attempt 1 - write AssemblyLinearVelocity  -> works, but the humanoid
    //               state machine re-applies gravity / overrides it.
    //   attempt 2 - write Position                -> the collision/assembly solver
    //               rewrites the position every physics step, so the character
    //               snaps back (the "iffy" position write).
    //   attempt 3 - PlatformStand + velocity      -> PlatformStand stops the
    //               humanoid from fighting our velocity, so writing velocity is
    //               honoured cleanly. Idle velocity (0) = hover, WASD = fly.
    //
    // The velocity write is the one Primitive write confirmed to actually stick,
    // so flight now drives velocity directly instead of trying to win a position
    // fight. Collisions are disabled while flying (ghost mode) and restored when
    // you stop, so nothing for the solver to resolve against.
    // ---------------------------------------------------------------------

    struct FVec3 {
        float x = 0, y = 0, z = 0;
        FVec3 operator+(const FVec3& o) const { return { x + o.x, y + o.y, z + o.z }; }
        FVec3 operator-(const FVec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
        FVec3 operator*(float s)        const { return { x * s, y * s, z * s }; }
        float magnitude() const { return sqrtf(x * x + y * y + z * z); }
        FVec3 normalize() const {
            float m = magnitude();
            if (m < 0.0001f) return { 0, 0, 0 };
            return { x / m, y / m, z / m };
        }
    };

    static instance cached_camera{};
    static DWORD    last_camera_lookup = 0;

    static instance GetCamera() {
        DWORD now = GetTickCount();
        if (cached_camera.is_valid() && (now - last_camera_lookup) < 1000)
            return cached_camera;

        instance dm = game::ReadDatamodel(g_base_address);
        if (!dm.is_valid()) return instance{};
        instance ws = dm.read_service("Workspace");
        if (!ws.is_valid()) return instance{};
        cached_camera = read<instance>(ws.address + Offsets::Workspace::CurrentCamera);
        last_camera_lookup = now;
        return cached_camera;
    }

    static bool s_active = false;
    static bool s_key_was_down = false;

    // saved collision flags, keyed by part instance
    static std::unordered_map<uintptr_t, uint8_t> s_saved_flags;

    // world gravity neutralisation (hover without falling), restored on stop
    static uintptr_t s_world_addr     = 0;
    static float     s_orig_gravity   = 196.2f;

    bool IsFlying() { return s_active; }

    static instance LocalCharacter() {
        instance dm = game::ReadDatamodel(g_base_address);
        if (!dm.is_valid()) return instance{};
        instance players = dm.read_service("Players");
        if (!players.is_valid()) return instance{};
        instance local = players.local_player();
        if (!local.is_valid()) return instance{};
        return local.model_instance();
    }

    static void SetCharacterCollision(bool enable_collision) {
        instance ch = LocalCharacter();
        if (!ch.is_valid()) return;

        for (const instance& child : ch.get_children()) {
            if (!child.is_valid()) continue;
            uintptr_t prim = read<uintptr_t>(child.address + Offsets::BasePart::Primitive);
            if (!is_valid_address(prim)) continue;

            if (!enable_collision) {
                uint8_t flags = read<uint8_t>(prim + Offsets::Primitive::Flags);
                if (s_saved_flags.find(child.address) == s_saved_flags.end())
                    s_saved_flags[child.address] = flags;
                write<uint8_t>(prim + Offsets::Primitive::Flags,
                               (uint8_t)(flags & ~Offsets::PrimitiveFlags::CanCollide));
            } else {
                auto it = s_saved_flags.find(child.address);
                if (it != s_saved_flags.end())
                    write<uint8_t>(prim + Offsets::Primitive::Flags, it->second);
            }
        }

        if (enable_collision) s_saved_flags.clear();
    }

    static void SetGravity(float value) {
        if (!is_valid_address(s_world_addr)) return;
        write<float>(s_world_addr + Offsets::World::Gravity, value);
    }

    static void StopFlying() {
        const cache::LocalPlayerData& lp = cache::GetLocalPlayer();
        if (s_active) {
            SetCharacterCollision(true);
            if (lp.valid && is_valid_address(lp.humanoid_address))
                write<bool>(lp.humanoid_address + Offsets::Humanoid::PlatformStand, false);
        }
        SetGravity(s_orig_gravity);
        s_world_addr = 0;
        s_active = false;
    }

    void RunFlight() {
        if (!flight_enabled) {
            StopFlying();
            s_key_was_down = false;
            return;
        }

        // no keybind set -> fly whenever enabled; otherwise the key drives it
        bool key_down = (flight_keybind == 0) || ((GetAsyncKeyState(flight_keybind) & 0x8000) != 0);

        bool want = s_active;
        if (flight_keybind == 0) {
            want = true;                              // always on while enabled
        } else if (flight_hold_mode) {
            want = key_down;
        } else if (key_down && !s_key_was_down) {
            want = !s_active;
        }
        s_key_was_down = key_down;

        if (!want) { StopFlying(); return; }

        const cache::LocalPlayerData& lp = cache::GetLocalPlayer();
        if (!lp.valid || !is_valid_address(lp.hrp_primitive)) { StopFlying(); return; }

        if (!s_active) {
            s_active = true;
            s_saved_flags.clear();
            SetCharacterCollision(false);   // ghost mode while flying

            // PlatformStand: the humanoid stops overriding our velocity (no more
            // gravity / state-machine fights), so zero velocity = hover.
            if (is_valid_address(lp.humanoid_address))
                write<bool>(lp.humanoid_address + Offsets::Humanoid::PlatformStand, true);

            // Belt and braces: zero the local physics-world gravity so the
            // character can't be dragged down between writes. Remember it to
            // restore on stop.
            instance dm = game::ReadDatamodel(g_base_address);
            if (dm.is_valid()) {
                instance ws = dm.read_service("Workspace");
                if (ws.is_valid()) {
                    uintptr_t world = read<uintptr_t>(ws.address + Offsets::Workspace::World);
                    if (is_valid_address(world)) {
                        s_world_addr = world;
                        float g = read<float>(world + Offsets::World::Gravity);
                        if (g > 0.0f && g < 2000.0f) s_orig_gravity = g;
                        write<float>(world + Offsets::World::Gravity, 0.0f);
                    }
                }
            }
        }

        // re-assert each tick so a respawn or a core script can't un-latch us
        if (is_valid_address(lp.humanoid_address))
            write<bool>(lp.humanoid_address + Offsets::Humanoid::PlatformStand, true);
        SetGravity(0.0f);

        instance cam = GetCamera();
        if (!cam.is_valid()) return;

        float rot[9] = {};
        if (!read_raw(cam.address + Offsets::Camera::Rotation, rot, sizeof(rot))) return;

        FVec3 look  = { -rot[2], -rot[5], -rot[8] };
        FVec3 right = {  rot[0],  rot[3],  rot[6] };

        FVec3 dir{};
        if (GetAsyncKeyState('W')         & 0x8000) dir = dir + look;
        if (GetAsyncKeyState('S')         & 0x8000) dir = dir - look;
        if (GetAsyncKeyState('A')         & 0x8000) dir = dir - right;
        if (GetAsyncKeyState('D')         & 0x8000) dir = dir + right;
        if (GetAsyncKeyState(VK_SPACE)    & 0x8000) dir = dir + FVec3{ 0, 1, 0 };
        if (GetAsyncKeyState(VK_LCONTROL) & 0x8000) dir = dir - FVec3{ 0, 1, 0 };
        if (GetAsyncKeyState(VK_LSHIFT)   & 0x8000) dir = dir - FVec3{ 0, 1, 0 };

        float vel[3] = { 0.0f, 0.0f, 0.0f };
        if (dir.magnitude() > 0.0001f) {
            dir = dir.normalize();
            vel[0] = dir.x * flight_value;
            vel[1] = dir.y * flight_value;
            vel[2] = dir.z * flight_value;
        }

        // the one write that reliably lands: drive velocity, let the solver move us.
        write_raw(lp.hrp_primitive + Offsets::Primitive::AssemblyLinearVelocity, vel, sizeof(vel));

        float ang[3] = { 0, 0, 0 };
        write_raw(lp.hrp_primitive + Offsets::Primitive::AssemblyAngularVelocity, ang, sizeof(ang));
    }
}
