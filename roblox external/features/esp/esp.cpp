#include <Windows.h>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>
#include "esp.h"
#include "globals.h"
#include "memory.h"
#include "cache.h"
#include "offsets.h"
#include "game.h"
#include "process.h"
#include "imgui/imgui.h"

namespace features {

    struct Vec2 {
        float x;
        float y;
    };

    struct Vec3 {
        float x;
        float y;
        float z;
    };

    struct Matrix4 {
        float data[16];
    };

    struct Box2D {
        float min_x;        float min_y;

        float max_x;
        float max_y;
        bool valid;
    };

    static bool ReadRaw(uint64_t address, void* buffer, size_t size) {
        return read_raw(address, buffer, size);
    }

    static bool ReadVec2(uint64_t address, Vec2& out) {
        return ReadRaw(address, &out, sizeof(out));
    }

    static bool ReadVec3(uint64_t address, Vec3& out) {
        return ReadRaw(address, &out, sizeof(out));
    }

    static float LengthSq(const Vec3& v) {
        return v.x * v.x + v.y * v.y + v.z * v.z;
    }

    static Vec3 Normalize(const Vec3& v) {
        float len_sq = LengthSq(v);
        if (len_sq <= 0.000001f) return { 0.0f, 0.0f, 0.0f };
        float inv = 1.0f / sqrtf(len_sq);
        return { v.x * inv, v.y * inv, v.z * inv };
    }

    static Vec3 Sub(const Vec3& a, const Vec3& b) {
        return { a.x - b.x, a.y - b.y, a.z - b.z };
    }

    static int FindEntityPartIndex(const cache::EspEntity& entity, const char* part_name) {
        for (size_t i = 0; i < entity.primitive_count; ++i) {
            if (strcmp(entity.part_names[i], part_name) == 0) return (int)i;
        }
        return -1;
    }


    static instance cached_camera{};
    static DWORD last_camera_lookup = 0;

    static instance GetCameraInstance() {
        DWORD now = GetTickCount();
        if (cached_camera.is_valid() && (now - last_camera_lookup) < 2000)
            return cached_camera;

        instance dm = game::ReadDatamodel(g_base_address);
        if (!dm.is_valid()) return instance{};
        instance ws = dm.read_service("Workspace");
        if (!ws.is_valid()) return instance{};
        cached_camera = read<instance>(ws.address + Offsets::Workspace::CurrentCamera);
        last_camera_lookup = now;
        return cached_camera;
    }

    static float Dot3(const Vec3& a, const Vec3& b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }

    // Build the clip matrix from the CAMERA itself (position/rotation/fov), not
    // from VisualEngine::ViewMatrix. The camera offsets are the ones every other
    // feature uses and are confirmed working, while the ViewMatrix layout on this
    // client was the thing making boxes drift off the character with distance.
    static bool GetCamera(Matrix4& view, Vec2& viewport) {
        instance ve = read<instance>(g_base_address + Offsets::VisualEngine::Pointer);
        if (!ve.is_valid()) return false;
        if (!ReadVec2(ve.address + Offsets::VisualEngine::Dimensions, viewport)) return false;
        if (viewport.x <= 0.0f || viewport.y <= 0.0f) return false;

        instance cam = GetCameraInstance();
        if (!cam.is_valid()) return false;

        Vec3 pos{};
        float rot[9]{};
        if (!ReadVec3(cam.address + Offsets::Camera::Position, pos)) return false;
        if (!ReadRaw(cam.address + Offsets::Camera::Rotation, rot, sizeof(rot))) return false;

        float fov = read<float>(cam.address + Offsets::Camera::FieldOfView);
        if (fov < 0.1f || fov > 3.0f) fov = 1.2217f;   // ~70 deg fallback

        // camera basis out of the Roblox rotation matrix (column-major: right/up/-forward)
        Vec3 right = {  rot[0],  rot[3],  rot[6] };
        Vec3 up    = {  rot[1],  rot[4],  rot[7] };
        Vec3 fwd   = { -rot[2], -rot[5], -rot[8] };

        float tan_half = tanf(fov * 0.5f);
        float aspect   = viewport.x / viewport.y;
        float scale_x  = 1.0f / (tan_half * aspect);
        float scale_y  = 1.0f / tan_half;

        // column-major matrix matching WorldToScreen's layout:
        //   clip.x = x*m0 + y*m1 + z*m2 + m3  = dot(right, p-pos) * scale_x
        //   clip.y = x*m4 + y*m5 + z*m6 + m7  = dot(up,    p-pos) * scale_y
        //   clip.w = x*m12+ y*m13+ z*m14+ m15 = dot(fwd,   p-pos)
        float* m = view.data;
        m[0]  = right.x * scale_x;  m[1]  = right.y * scale_x;  m[2]  = right.z * scale_x;
        m[3]  = -Dot3(right, pos) * scale_x;
        m[4]  = up.x * scale_y;     m[5]  = up.y * scale_y;     m[6]  = up.z * scale_y;
        m[7]  = -Dot3(up, pos) * scale_y;
        m[8]  = 0.0f;               m[9]  = 0.0f;               m[10] = 0.0f;               m[11] = 0.0f;
        m[12] = fwd.x;              m[13] = fwd.y;              m[14] = fwd.z;
        m[15] = -Dot3(fwd, pos);
        return true;
    }

    // ---------------------------------------------------------------------
    // Roblox renders into its window's CLIENT area, so everything WorldToScreen
    // produces is in client-space pixels. Our ImGui overlay, however, is a
    // full-screen window anchored at the desktop origin (0,0). If the game is
    // windowed (or has a title bar / borders / is on a scaled display) the two
    // coordinate systems differ by the client-area origin, plus a scale factor
    // when the backbuffer Dimensions don't match the client rect in pixels.
    //
    // That offset is CONSTANT in pixels, which is exactly why the bug only
    // looked broken "at long distance": up close the player fills a few hundred
    // pixels so a ~30px vertical shift is barely noticeable, but far away the
    // character is only a handful of pixels tall, so the same 30px shift parks
    // the whole box/name above their head.
    //
    // Convert client-space -> desktop-space before drawing.
    struct OverlayTransform {
        float offset_x = 0.0f;
        float offset_y = 0.0f;
        float scale_x  = 1.0f;
        float scale_y  = 1.0f;
    };

    static OverlayTransform cached_transform{};
    static DWORD last_transform_lookup = 0;

    static const OverlayTransform& GetOverlayTransform(const Vec2& viewport) {
        DWORD now = GetTickCount();
        // refresh often enough to follow window drags/resizes without hammering
        // the win32 API every projected point.
        if ((now - last_transform_lookup) < 250 && last_transform_lookup != 0)
            return cached_transform;
        last_transform_lookup = now;

        OverlayTransform t{};
        HWND rbx = process::GetRobloxWindow();
        if (rbx) {
            RECT client{};
            POINT origin{ 0, 0 };
            if (GetClientRect(rbx, &client) && ClientToScreen(rbx, &origin)) {
                float client_w = (float)(client.right - client.left);
                float client_h = (float)(client.bottom - client.top);
                if (client_w > 0.0f && client_h > 0.0f) {
                    t.offset_x = (float)origin.x;
                    t.offset_y = (float)origin.y;
                    // Dimensions is the render target size; if it differs from
                    // the client rect (DPI scaling / resolution scaling) map
                    // between them instead of assuming 1:1.
                    if (viewport.x > 0.0f && viewport.y > 0.0f) {
                        t.scale_x = client_w / viewport.x;
                        t.scale_y = client_h / viewport.y;
                    }
                }
            }
        }
        cached_transform = t;
        return cached_transform;
    }

    static bool WorldToScreen(const Vec3& world, Vec2& out, const Matrix4& view, const Vec2& viewport) {
        const float* m = view.data;
        float w_x = world.x * m[12] + world.y * m[13] + world.z * m[14] + m[15];
        if (w_x < 0.01f) return false;
        float screen_x = world.x * m[0] + world.y * m[1] + world.z * m[2] + m[3];
        float screen_y = world.x * m[4] + world.y * m[5] + world.z * m[6] + m[7];
        float inv_w = 1.0f / w_x;
        out.x = (viewport.x * 0.5f * screen_x * inv_w) + (viewport.x * 0.5f);
        out.y = -(viewport.y * 0.5f * screen_y * inv_w) + (viewport.y * 0.5f);
        if (out.x != out.x || out.y != out.y) return false;

        // client-space -> overlay/desktop-space
        const OverlayTransform& t = GetOverlayTransform(viewport);
        out.x = out.x * t.scale_x + t.offset_x;
        out.y = out.y * t.scale_y + t.offset_y;
        return true;
    }

    // Canonical Roblox body-part sizes (studs). This client build reports ~0 from
    // Primitive::Size, so we fall back to these instead of letting a zero-sized
    // part collapse onto its centre point - that's what made the box float above
    // the character and shrink as they got further away.
    static void CanonicalPartSize(const char* name, Vec3& out) {
        if (!name) { out = { 1, 1, 1 }; return; }
        // Exact name matches (confirmed via float probes for this client build)
        if (!strcmp(name, "Head"))                        { out = { 2, 1, 1 }; return; }
        if (!strcmp(name, "HumanoidRootPart"))            { out = { 2, 2, 1 }; return; }
        if (!strcmp(name, "Torso") || !strcmp(name, "UpperTorso")) { out = { 2, 2, 1 }; return; }
        if (!strcmp(name, "LowerTorso"))                  { out = { 2, 1, 1 }; return; }
        if (!strcmp(name, "Left Arm")  || !strcmp(name, "Right Arm"))  { out = { 1, 2, 1 }; return; }
        if (!strcmp(name, "LeftUpperArm")  || !strcmp(name, "RightUpperArm")) { out = { 1, 2, 1 }; return; }
        if (!strcmp(name, "LeftLowerArm")  || !strcmp(name, "RightLowerArm")) { out = { 1, 2, 1 }; return; }
        if (!strcmp(name, "Left Leg")  || !strcmp(name, "Right Leg"))  { out = { 1, 2, 1 }; return; }
        if (!strcmp(name, "LeftUpperLeg")  || !strcmp(name, "RightUpperLeg")) { out = { 1, 2, 1 }; return; }
        if (!strcmp(name, "LeftLowerLeg")  || !strcmp(name, "RightLowerLeg")) { out = { 1, 2, 1 }; return; }
        if (!strcmp(name, "LeftHand") || !strcmp(name, "RightHand")) { out = { 1, 1, 1 }; return; }
        if (!strcmp(name, "LeftFoot") || !strcmp(name, "RightFoot")) { out = { 1, 1, 1 }; return; }
        // Category-based fallback for any unrecognized part name.
        // Roblox R6/R15 body parts follow predictable size patterns.
        // Check for substrings that might appear on parts with unusual names.
        if (name != nullptr) {
            // R15 limb parts
            if (strstr(name, "UpperArm") || strstr(name, "LowerArm")) { out = { 1, 2, 1 }; return; }
            if (strstr(name, "UpperLeg") || strstr(name, "LowerLeg")) { out = { 1, 2, 1 }; return; }
            // Hand/Foot variants
            if (strstr(name, "Hand") || strstr(name, "Foot")) { out = { 1, 1, 1 }; return; }
            // Head variant
            if (strstr(name, "Head")) { out = { 2, 1, 1 }; return; }
            // Torso variants
            if (strstr(name, "Torso") || strstr(name, "UpperTorso") || strstr(name, "LowerTorso")) { out = { 2, 2, 1 }; return; }
        }
        // Default fallback - unit cube (better than zero-sized part collapsing)
        out = { 1, 1, 1 };}

    // Wall check: segment-vs-AABB ray against the other players' cached parts.
    // (An external can't call the game's raycast and we don't have the world
    // primitives list, so this is line-of-sight through OTHER PLAYERS only.)
    static bool SegmentHitsAABB(const Vec3& from, const Vec3& to,
                                const Vec3& center, const Vec3& half) {
        float org[3] = { from.x, from.y, from.z };
        float dir[3] = { to.x - from.x, to.y - from.y, to.z - from.z };
        float c[3]   = { center.x, center.y, center.z };
        float h[3]   = { half.x, half.y, half.z };
        float tmin = 0.0f, tmax = 1.0f;
        for (int i = 0; i < 3; ++i) {
            float e = org[i], f = dir[i], mn = c[i] - h[i], mx = c[i] + h[i];
            if (f > -1e-6f && f < 1e-6f) {
                if (e < mn || e > mx) return false;
            } else {
                float t1 = (mn - e) / f;
                float t2 = (mx - e) / f;
                if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
                if (t1 > tmin) tmin = t1;
                if (t2 < tmax) tmax = t2;
                if (tmin > tmax) return false;
            }
        }
        return true;
    }

    static bool LOSClear(const Vec3& from, const Vec3& to, uintptr_t skip_player) {
        if (!wall_check) return true;
        auto ents_snap = cache::GetEspSnapshot();
        const auto& ents = *ents_snap;
        for (const auto& ent : ents) {
            if (ent.player_address == skip_player) continue;
            for (size_t i = 0; i < ent.primitive_count; ++i) {
                uintptr_t p = ent.primitives[i];
                if (!is_valid_address(p)) continue;
                Vec3 c{};
                if (!ReadVec3(p + Offsets::Primitive::Position, c)) continue;
                Vec3 size{};
                CanonicalPartSize(ent.part_names[i], size);
                Vec3 half = { size.x * 0.5f, size.y * 0.5f, size.z * 0.5f };
                if (SegmentHitsAABB(from, to, c, half)) return false;
            }
        }
        return true;
    }

    static bool ComputeBoxForPrimitives(const cache::EspEntity& entity, const Matrix4& view, const Vec2& viewport, Box2D& out_box) {
        if (entity.primitive_count == 0) {
            out_box.valid = false;
            return false;
        }

        // Build ONE world-space axis-aligned box around the character from each
        // part's centre + its canonical body size, then project that box's 8
        // corners. We deliberately do NOT read Primitive::Size / Primitive::Rotation
        // here: both offsets are unreliable on this client (Size reads ~0, Rotation
        // can be garbage), and applying them is what made the box drift above the
        // character and wobble with distance. Body-part sizes are essentially
        // constant in Roblox, so canonical sizes give a stable, distance-independent
        // box that hugs the character.
        float min_x = 1e30f, min_y = 1e30f, min_z = 1e30f;
        float max_x = -1e30f, max_y = -1e30f, max_z = -1e30f;
        bool any = false;

        for (size_t i = 0; i < entity.primitive_count; ++i) {
            uintptr_t primitive = entity.primitives[i];
            if (!is_valid_address(primitive)) continue;
            Vec3 pos{};
            Vec3 size{};
            if (!ReadVec3(primitive + Offsets::Primitive::Position, pos)) continue;
            CanonicalPartSize(entity.part_names[i], size);

            float hx = size.x * 0.5f;
            float hy = size.y * 0.5f;
            float hz = size.z * 0.5f;

            if (pos.x - hx < min_x) min_x = pos.x - hx;
            if (pos.x + hx > max_x) max_x = pos.x + hx;
            if (pos.y - hy < min_y) min_y = pos.y - hy;
            if (pos.y + hy > max_y) max_y = pos.y + hy;
            if (pos.z - hz < min_z) min_z = pos.z - hz;
            if (pos.z + hz > max_z) max_z = pos.z + hz;
            any = true;
        }

        if (!any) {
            out_box.valid = false;
            return false;
        }

        const Vec3 corners[8] = {
            { min_x, min_y, min_z }, { min_x, min_y, max_z },
            { min_x, max_y, min_z }, { min_x, max_y, max_z },
            { max_x, min_y, min_z }, { max_x, min_y, max_z },
            { max_x, max_y, min_z }, { max_x, max_y, max_z },
        };

        bool has_point = false;
        float scr_min_x = 0.0f, scr_min_y = 0.0f, scr_max_x = 0.0f, scr_max_y = 0.0f;
        for (int c = 0; c < 8; ++c) {
            Vec2 pt{};
            if (WorldToScreen(corners[c], pt, view, viewport)) {
                if (!has_point) {
                    scr_min_x = scr_max_x = pt.x;
                    scr_min_y = scr_max_y = pt.y;
                    has_point = true;
                } else {
                    if (pt.x < scr_min_x) scr_min_x = pt.x;
                    if (pt.x > scr_max_x) scr_max_x = pt.x;
                    if (pt.y < scr_min_y) scr_min_y = pt.y;
                    if (pt.y > scr_max_y) scr_max_y = pt.y;
                }
            }
        }

        if (!has_point) {
            out_box.valid = false;
            return false;
        }

        float w = scr_max_x - scr_min_x;
        float h = scr_max_y - scr_min_y;
        if (w <= 1.0f || h <= 1.0f) {
            out_box.valid = false;
            return false;
        }

        out_box.min_x = scr_min_x;
        out_box.min_y = scr_min_y;
        out_box.max_x = scr_max_x;
        out_box.max_y = scr_max_y;
        out_box.valid = true;
        return true;
    }



    static bool ReadPos(uintptr_t primitive, Vec3& out) {
        if (!is_valid_address(primitive)) return false;
        return ReadVec3(primitive + Offsets::Primitive::Position, out);
    }

    static bool PartToScreen(uintptr_t primitive, const Matrix4& view, const Vec2& viewport, Vec2& out) {
        if (!primitive) return false;
        Vec3 pos{};
        if (!ReadPos(primitive, pos)) return false;
        return WorldToScreen(pos, out, view, viewport);
    }

    static void DrawSkeletonLine(ImDrawList* draw, const Vec2& a, const Vec2& b, ImU32 color) {
        draw->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), IM_COL32(0, 0, 0, 255), 3.0f);
        draw->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), color, 1.0f);
    }

    static void DrawBone(ImDrawList* draw, uintptr_t from, uintptr_t to, const Matrix4& view, const Vec2& viewport, ImU32 color) {
        if (!from || !to) return;
        Vec2 a{}, b{};
        if (!PartToScreen(from, view, viewport, a)) return;
        if (!PartToScreen(to, view, viewport, b)) return;
        DrawSkeletonLine(draw, a, b, color);
    }

    void RenderAimViewer() {
        if (!aimviewer) return;

        Matrix4 view{};
        Vec2 viewport{};
        if (!GetCamera(view, viewport)) return;

        auto skeletons_snap = cache::GetSkeletonSnapshot();
        const auto& skeletons = *skeletons_snap;
        if (skeletons.empty()) return;

        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        if (!draw) return;

        for (const cache::SkeletonEntity& skel : skeletons) {
            if (!skel.head) continue;

            Vec3 head_pos{};
            if (!ReadVec3(skel.head + Offsets::Primitive::Position, head_pos)) continue;

            Vec3 look_direction{ 0.0f, 0.0f, -1.0f };
            bool has_valid_direction = false;

            float head_rot[9]{};
            if (ReadRaw(skel.head + Offsets::Primitive::Rotation, head_rot, sizeof(head_rot))) {
                look_direction = { -head_rot[2], -head_rot[5], -head_rot[8] };
                float len_sq = LengthSq(look_direction);
                if (len_sq > 0.0001f) {
                    look_direction = Normalize(look_direction);
                    has_valid_direction = true;
                }
            }

            if (!has_valid_direction && skel.upper_torso) {
                Vec3 torso_pos{};
                if (ReadVec3(skel.upper_torso + Offsets::Primitive::Position, torso_pos)) {
                    look_direction = Normalize(Sub(head_pos, torso_pos));
                    has_valid_direction = LengthSq(look_direction) > 0.0001f;
                }
            }

            if (!has_valid_direction) continue;

            Vec3 ray_end = {
                head_pos.x + look_direction.x * 40.0f,
                head_pos.y + look_direction.y * 40.0f,
                head_pos.z + look_direction.z * 40.0f
            };

            Vec2 screen_start{};
            Vec2 screen_end{};
            if (!WorldToScreen(head_pos, screen_start, view, viewport)) continue;
            if (!WorldToScreen(ray_end, screen_end, view, viewport)) continue;

            draw->AddLine(
                ImVec2(screen_start.x, screen_start.y),
                ImVec2(screen_end.x, screen_end.y),
                IM_COL32(255, 0, 0, 255),
                2.0f);
        }
    }

    void RenderSkeletonESP() {
        Matrix4 view{};
        Vec2 viewport{};
        if (!GetCamera(view, viewport)) return;

        auto skeletons_snap = cache::GetSkeletonSnapshot();
        const auto& skeletons = *skeletons_snap;
        if (skeletons.empty()) return;

        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        if (!draw) return;

        ImU32 color = IM_COL32(
            (int)(skeleton_color[0] * 255.0f),
            (int)(skeleton_color[1] * 255.0f),
            (int)(skeleton_color[2] * 255.0f),
            (int)(skeleton_color[3] * 255.0f)
        );

        for (const cache::SkeletonEntity& skel : skeletons) {
            if (!skel.head || !skel.upper_torso) continue;

            if (skel.is_r15) {
                DrawBone(draw, skel.head, skel.upper_torso, view, viewport, color);
                DrawBone(draw, skel.upper_torso, skel.lower_torso, view, viewport, color);
                DrawBone(draw, skel.upper_torso, skel.left_upper_arm, view, viewport, color);
                DrawBone(draw, skel.left_upper_arm, skel.left_lower_arm, view, viewport, color);
                DrawBone(draw, skel.left_lower_arm, skel.left_hand, view, viewport, color);
                DrawBone(draw, skel.upper_torso, skel.right_upper_arm, view, viewport, color);
                DrawBone(draw, skel.right_upper_arm, skel.right_lower_arm, view, viewport, color);
                DrawBone(draw, skel.right_lower_arm, skel.right_hand, view, viewport, color);
                DrawBone(draw, skel.lower_torso, skel.left_upper_leg, view, viewport, color);
                DrawBone(draw, skel.left_upper_leg, skel.left_lower_leg, view, viewport, color);
                DrawBone(draw, skel.left_lower_leg, skel.left_foot, view, viewport, color);
                DrawBone(draw, skel.lower_torso, skel.right_upper_leg, view, viewport, color);
                DrawBone(draw, skel.right_upper_leg, skel.right_lower_leg, view, viewport, color);
                DrawBone(draw, skel.right_lower_leg, skel.right_foot, view, viewport, color);
            }
            else {
                DrawBone(draw, skel.head, skel.upper_torso, view, viewport, color);
                DrawBone(draw, skel.upper_torso, skel.left_hand, view, viewport, color);
                DrawBone(draw, skel.upper_torso, skel.right_hand, view, viewport, color);
                DrawBone(draw, skel.upper_torso, skel.left_foot, view, viewport, color);
                DrawBone(draw, skel.upper_torso, skel.right_foot, view, viewport, color);
            }
        }
    }

    // NOTE: points coming out of WorldToScreen are already in overlay/desktop
    // space, so the visible rect has to be transformed the same way rather than
    // compared against the raw render dimensions.
    static bool OnScreen(const Vec2& pt, const Vec2& viewport) {
        const OverlayTransform& t = GetOverlayTransform(viewport);
        float left   = t.offset_x;
        float top    = t.offset_y;
        float right  = t.offset_x + viewport.x * t.scale_x;
        float bottom = t.offset_y + viewport.y * t.scale_y;
        return pt.x >= left && pt.x <= right && pt.y >= top && pt.y <= bottom;
    }

    void RenderChinaHatESP() {
        if (!chinahat) return;

        Matrix4 view{};
        Vec2 viewport{};
        if (!GetCamera(view, viewport)) return;

        auto entities_snap = cache::GetEspSnapshot();
        const auto& entities = *entities_snap;
        const cache::LocalPlayerData& lp = cache::GetLocalPlayer();
        if (entities.empty()) return;

        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        if (!draw) return;

        ImU32 color = IM_COL32(
            (int)(chinahat_color[0] * 255.0f),
            (int)(chinahat_color[1] * 255.0f),
            (int)(chinahat_color[2] * 255.0f),
            (int)(chinahat_color[3] * 255.0f * 0.42f)
        );

        const float hat_height = 1.0f;
        const float hat_radius = 1.5f;
        const int segments = 48;

        for (const cache::EspEntity& entity : entities) {
            int head_idx = FindEntityPartIndex(entity, "Head");
            if (head_idx < 0) continue;

            uintptr_t head_prim = entity.primitives[head_idx];
            if (!is_valid_address(head_prim)) continue;

            if (lp.valid && esp_render_distance > 0.0f) {
                float dx = entity.root_x - lp.x;
                float dy = entity.root_y - lp.y;
                float dz = entity.root_z - lp.z;
                float dist = sqrtf(dx * dx + dy * dy + dz * dz);
                if (dist > esp_render_distance) continue;
            }

            Vec3 head_pos{};
            if (!ReadVec3(head_prim + Offsets::Primitive::Position, head_pos)) continue;

            Vec3 apex_pos = { head_pos.x, head_pos.y + hat_height + 0.15f, head_pos.z };

            std::vector<Vec3> base_points;
            base_points.reserve(segments);
            for (int i = 0; i < segments; ++i) {
                float angle = (2.0f * 3.14159f * i) / segments;
                base_points.emplace_back(Vec3{
                    head_pos.x + hat_radius * cosf(angle),
                    head_pos.y + 0.2f,
                    head_pos.z + hat_radius * sinf(angle)
                });
            }

            Vec2 apex_screen{};
            if (!WorldToScreen(apex_pos, apex_screen, view, viewport)) continue;

            std::vector<ImVec2> base_screen;
            base_screen.reserve(segments);
            bool any_on_screen = OnScreen(apex_screen, viewport);
            bool all_projected = true;
            for (const auto& point : base_points) {
                Vec2 screen_pos{};
                if (WorldToScreen(point, screen_pos, view, viewport)) {
                    base_screen.emplace_back(screen_pos.x, screen_pos.y);
                    any_on_screen |= OnScreen(screen_pos, viewport);
                } else {
                    all_projected = false;
                    break;
                }
            }
            if (!all_projected || !any_on_screen) continue;

            draw->Flags |= ImDrawListFlags_AntiAliasedFill | ImDrawListFlags_AntiAliasedLines;

            const float apex_soft_radius = 2.0f;
            for (size_t i = 0; i < segments; ++i) {
                size_t next = (i + 1) % segments;
                ImVec2 apex_offset = ImVec2(
                    apex_screen.x + cosf((2.0f * 3.14159f * i) / segments) * apex_soft_radius,
                    apex_screen.y + sinf((2.0f * 3.14159f * i) / segments) * apex_soft_radius
                );
                draw->AddTriangleFilled(
                    apex_offset,
                    base_screen[i],
                    base_screen[next],
                    color
                );
            }

            ImU32 base_color = IM_COL32(
                (int)(chinahat_color[0] * 255.0f),
                (int)(chinahat_color[1] * 255.0f),
                (int)(chinahat_color[2] * 255.0f),
                (int)(chinahat_color[3] * 255.0f * 0.6f * 0.42f)
            );
            draw->AddConvexPolyFilled(base_screen.data(), segments, base_color);

            ImU32 outline_color = IM_COL32(0, 0, 0, 100);
            for (size_t i = 0; i < segments; ++i) {
                size_t next = (i + 1) % segments;
                draw->AddLine(base_screen[i], base_screen[next], outline_color, 1.2f);
            }

            draw->Flags &= ~(ImDrawListFlags_AntiAliasedFill | ImDrawListFlags_AntiAliasedLines);
        }
    }

    static ImFont* GetEspFont() {
        ImGuiIO& io = ImGui::GetIO();
        if (io.Fonts && io.Fonts->Fonts.Size > 1)
            return io.Fonts->Fonts[1];
        if (io.Fonts && io.Fonts->Fonts.Size > 0)
            return io.Fonts->Fonts[0];
        return nullptr;
    }

    static void DrawTextWithShadow(ImDrawList* draw, float font_size, const ImVec2& position, ImU32 color, const char* text) {
        if (!draw || !text) return;
        ImFont* font = GetEspFont();
        ImU32 shadow = IM_COL32(0, 0, 0, 255);
        if (font) {
            for (int i = -1; i <= 1; i++) {
                for (int j = -1; j <= 1; j++) {
                    if (i == 0 && j == 0) continue;
                    draw->AddText(font, font_size, ImVec2(position.x + i, position.y + j), shadow, text);
                }
            }
            draw->AddText(font, font_size, position, color, text);
        } else {
            for (int i = -1; i <= 1; i++) {
                for (int j = -1; j <= 1; j++) {
                    if (i == 0 && j == 0) continue;
                    draw->AddText(nullptr, font_size, ImVec2(position.x + i, position.y + j), shadow, text);
                }
            }
            draw->AddText(nullptr, font_size, position, color, text);
        }
    }

    static void RenderHealthBar(ImDrawList* draw, float x1, float y1, float x2, float y2, float health, float max_health) {
        float health_percent = (max_health > 0.0f) ? (health / max_health) : 0.0f;
        health_percent = (std::max)(0.0f, (std::min)(1.0f, health_percent));

        float box_height = y2 - y1;
        float bar_gap = (std::max)(2.0f, (std::min)(7.0f, box_height * 0.035f));
        float bar_x = x1 - bar_gap;

        ImU32 outline = IM_COL32(0, 0, 0, 255);
        ImU32 background = IM_COL32(45, 45, 45, 220);

        draw->AddRectFilled(ImVec2(bar_x, y1 - 1), ImVec2(bar_x + 1, y2 + 1), outline);
        draw->AddRectFilled(ImVec2(bar_x + 2, y1 - 1), ImVec2(bar_x + 3, y2 + 1), outline);
        draw->AddRectFilled(ImVec2(bar_x, y1 - 1), ImVec2(bar_x + 3, y1), outline);
        draw->AddRectFilled(ImVec2(bar_x, y2), ImVec2(bar_x + 3, y2 + 1), outline);

        draw->AddRectFilled(ImVec2(bar_x + 1, y1), ImVec2(bar_x + 2, y2), background);

        float fill_height = box_height * health_percent;
        int r = (int)((1.0f - health_percent) * 255.0f);
        int g = (int)(health_percent * 255.0f);
        ImU32 bar_color = IM_COL32(r, g, 0, 255);

        draw->AddRectFilled(
            ImVec2(bar_x + 1, floorf(y2 - fill_height)),
            ImVec2(bar_x + 2, ceilf(y2)),
            bar_color
        );
    }

    static void RenderName(ImDrawList* draw, float x1, float y1, float x2, float y2, const char* name) {
        if (!name || name[0] == '\0') return;
        const float font_size = 12.0f;
        ImFont* font = GetEspFont();
        ImVec2 text_size = font ? font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, name) : ImGui::CalcTextSize(name);
        float center_x = (x1 + x2) * 0.5f;
        float x_pos = floorf(center_x - text_size.x * 0.5f + 0.5f);
        float y_pos = floorf(y1 - text_size.y - 2.0f + 0.5f);
        ImU32 col = IM_COL32(
            (int)(name_color[0] * 255.0f),
            (int)(name_color[1] * 255.0f),
            (int)(name_color[2] * 255.0f),
            (int)(name_color[3] * 255.0f)
        );
        DrawTextWithShadow(draw, font_size, ImVec2(x_pos, y_pos), col, name);
    }

    static void RenderHealthText(ImDrawList* draw, float x1, float y1, float x2, float y2, float health) {
        char buf[32];
        sprintf_s(buf, "[%.0f]", health);
        const float font_size = 12.0f;
        ImFont* font = GetEspFont();
        ImVec2 text_size = font ? font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, buf) : ImGui::CalcTextSize(buf);
        float x_pos = x1 - 6.0f - text_size.x;
        if (healthbar) {
            x_pos -= 7.0f;
        }
        float y_pos = floorf(y1 - 3.0f + 0.5f);
        ImU32 col = IM_COL32(
            (int)(health_text_color[0] * 255.0f),
            (int)(health_text_color[1] * 255.0f),
            (int)(health_text_color[2] * 255.0f),
            (int)(health_text_color[3] * 255.0f)
        );
        DrawTextWithShadow(draw, font_size, ImVec2(x_pos, y_pos), col, buf);
    }

    static void RenderRigType(ImDrawList* draw, float x1, float y1, float x2, float y2, bool is_r15) {
        const char* text = is_r15 ? "[R15]" : "[R6]";
        const float font_size = 12.0f;
        ImFont* font = GetEspFont();
        ImVec2 text_size = font ? font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, text) : ImGui::CalcTextSize(text);
        float x_pos = floorf(x2 + 5.0f + 0.5f);
        float y_pos = floorf(y1 - text_size.y + 10.0f + 0.5f);
        ImU32 col = IM_COL32(
            (int)(rig_type_color[0] * 255.0f),
            (int)(rig_type_color[1] * 255.0f),
            (int)(rig_type_color[2] * 255.0f),
            (int)(rig_type_color[3] * 255.0f)
        );
        DrawTextWithShadow(draw, font_size, ImVec2(x_pos, y_pos), col, text);
    }

    static void RenderTool(ImDrawList* draw, float x1, float y1, float x2, float y2, const char* tool_name, bool has_distance) {
        char buf[96];
        if (!tool_name || tool_name[0] == '\0') {
            buf[0] = '['; buf[1] = 'N'; buf[2] = 'o'; buf[3] = 'n'; buf[4] = 'e'; buf[5] = ']'; buf[6] = '\0';
        } else {
            buf[0] = '[';
            int i = 1;
            for (const char* p = tool_name; *p && i < 92; ++p) {
                if (*p != '[' && *p != ']') buf[i++] = *p;
            }
            buf[i++] = ']';
            buf[i] = '\0';
        }
        const float font_size = 12.0f;
        ImFont* font = GetEspFont();
        ImVec2 text_size = font ? font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, buf) : ImGui::CalcTextSize(buf);
        float center_x = (x1 + x2) * 0.5f;
        float x_pos = floorf(center_x - text_size.x * 0.5f + 0.5f);
        float y_pos = floorf(y2 + (has_distance ? 18.0f : 3.0f) + 0.5f);
        ImU32 col = IM_COL32(
            (int)(tool_color[0] * 255.0f),
            (int)(tool_color[1] * 255.0f),
            (int)(tool_color[2] * 255.0f),
            (int)(tool_color[3] * 255.0f)
        );
        DrawTextWithShadow(draw, font_size, ImVec2(x_pos, y_pos), col, buf);
    }

    static void RenderDistance(ImDrawList* draw, float x1, float y1, float x2, float y2, float distance) {
        char buf[32];
        sprintf_s(buf, "%.0f studs", distance);
        const float font_size = 12.0f;
        ImFont* font = GetEspFont();
        ImVec2 text_size = font ? font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, buf) : ImGui::CalcTextSize(buf);
        float center_x = (x1 + x2) * 0.5f;
        float x_pos = floorf(center_x - text_size.x * 0.5f + 0.5f);
        float y_pos = floorf(y2 + 2.0f + 0.5f);
        ImU32 col = IM_COL32(
            (int)(distance_color[0] * 255.0f),
            (int)(distance_color[1] * 255.0f),
            (int)(distance_color[2] * 255.0f),
            (int)(distance_color[3] * 255.0f)
        );
        DrawTextWithShadow(draw, font_size, ImVec2(x_pos, y_pos), col, buf);
    }

    void RenderChams() {
        Matrix4 view{};
        Vec2 viewport{};
        if (!GetCamera(view, viewport)) return;

        auto entities_snap = cache::GetEspSnapshot();
        const auto& entities = *entities_snap;
        if (entities.empty()) return;

        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        if (!draw) return;

        ImU32 fill = IM_COL32(
            (int)(chams_color[0] * 255.0f),
            (int)(chams_color[1] * 255.0f),
            (int)(chams_color[2] * 255.0f),
            (int)(chams_color[3] * 255.0f)
        );

        static const int faces[6][4] = {
            {0, 1, 3, 2},
            {4, 6, 7, 5},
            {0, 4, 5, 1},
            {2, 3, 7, 6},
            {0, 2, 6, 4},
            {1, 5, 7, 3},
        };

        for (const cache::EspEntity& entity : entities) {
            for (size_t i = 0; i < entity.primitive_count; ++i) {
                uintptr_t prim = entity.primitives[i];
                if (!is_valid_address(prim)) continue;

                struct { float rot[9]; Vec3 pos; } rp{};
                if (!ReadRaw(prim + Offsets::Primitive::Rotation, &rp, sizeof(rp))) continue;

                // Use canonical part size instead of reading Primitive::Size,
                // which reads ~0 on this client and causes parts to collapse/
                // float above the character. CanonicalPartSize uses known body-part
                // dimensions that stay stable at distance.
                Vec3 sz;
                CanonicalPartSize(entity.part_names[i], sz);

                float hx = sz.x * 0.5f, hy = sz.y * 0.5f, hz = sz.z * 0.5f;

                float lx[8] = {-hx, -hx, -hx, -hx, hx, hx, hx, hx};
                float ly[8] = {-hy, -hy, hy, hy, -hy, -hy, hy, hy};
                float lz[8] = {-hz, hz, -hz, hz, -hz, hz, -hz, hz};

                Vec3 corners[8];
                for (int c = 0; c < 8; ++c) {
                    corners[c].x = rp.rot[0] * lx[c] + rp.rot[1] * ly[c] + rp.rot[2] * lz[c] + rp.pos.x;
                    corners[c].y = rp.rot[3] * lx[c] + rp.rot[4] * ly[c] + rp.rot[5] * lz[c] + rp.pos.y;
                    corners[c].z = rp.rot[6] * lx[c] + rp.rot[7] * ly[c] + rp.rot[8] * lz[c] + rp.pos.z;
                }

                Vec2 screen[8];
                bool ok[8];
                for (int c = 0; c < 8; ++c)
                    ok[c] = WorldToScreen(corners[c], screen[c], view, viewport);

                for (int f = 0; f < 6; ++f) {
                    int a = faces[f][0], b = faces[f][1], ci = faces[f][2], d = faces[f][3];
                    if (!ok[a] || !ok[b] || !ok[ci] || !ok[d]) continue;
                    ImVec2 pts[4] = {
                        {screen[a].x, screen[a].y},
                        {screen[b].x, screen[b].y},
                        {screen[ci].x, screen[ci].y},
                        {screen[d].x, screen[d].y},
                    };
                    draw->AddConvexPolyFilled(pts, 4, fill);
                }
            }
        }
    }

    void RenderExpandedHitbox() {
        if (!render_expanded_hitbox) return;
        if (!hitbox_expander_enabled) return;

        Matrix4 view{};
        Vec2 viewport{};
        if (!GetCamera(view, viewport)) return;

        auto entities_snap = cache::GetEspSnapshot();
        const auto& entities = *entities_snap;
        if (entities.empty()) return;

        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        if (!draw) return;

        ImU32 color = IM_COL32(255, 50, 50, 255);

        const float size = hitbox_expander_value;
        const float hx = size * 0.5f;
        const float hy = size * 0.5f;
        const float hz = size * 0.5f;

        static const int faces[6][4] = {
            {0, 1, 3, 2},
            {4, 6, 7, 5},
            {0, 4, 5, 1},
            {2, 3, 7, 6},
            {0, 2, 6, 4},
            {1, 5, 7, 3},
        };

        for (const cache::EspEntity& entity : entities) {
            if (!entity.character_address) continue;

            instance character{ entity.character_address };
            if (!character.is_valid()) continue;

            uintptr_t hrp_prim = 0;
            for (const instance& child : character.get_children()) {
                if (!child.is_valid()) continue;
                if (child.get_name() != "HumanoidRootPart") continue;
                hrp_prim = read<uintptr_t>(child.address + Offsets::BasePart::Primitive);
                break;
            }

            if (!is_valid_address(hrp_prim)) continue;

            struct { float rot[9]; Vec3 pos; } rp{};
            if (!ReadRaw(hrp_prim + Offsets::Primitive::Rotation, &rp, sizeof(rp))) continue;

            float lx[8] = {-hx, -hx, -hx, -hx, hx, hx, hx, hx};
            float ly[8] = {-hy, -hy, hy, hy, -hy, -hy, hy, hy};
            float lz[8] = {-hz, hz, -hz, hz, -hz, hz, -hz, hz};

            Vec3 corners[8];
            for (int c = 0; c < 8; ++c) {
                corners[c].x = rp.rot[0] * lx[c] + rp.rot[1] * ly[c] + rp.rot[2] * lz[c] + rp.pos.x;
                corners[c].y = rp.rot[3] * lx[c] + rp.rot[4] * ly[c] + rp.rot[5] * lz[c] + rp.pos.y;
                corners[c].z = rp.rot[6] * lx[c] + rp.rot[7] * ly[c] + rp.rot[8] * lz[c] + rp.pos.z;
            }

            Vec2 screen[8];
            bool ok[8];
            for (int c = 0; c < 8; ++c)
                ok[c] = WorldToScreen(corners[c], screen[c], view, viewport);

            for (int f = 0; f < 6; ++f) {
                int a = faces[f][0], b = faces[f][1], ci = faces[f][2], d = faces[f][3];
                if (!ok[a] || !ok[b] || !ok[ci] || !ok[d]) continue;
                draw->AddLine(ImVec2(screen[a].x, screen[a].y), ImVec2(screen[b].x, screen[b].y), color, 1.5f);
                draw->AddLine(ImVec2(screen[b].x, screen[b].y), ImVec2(screen[ci].x, screen[ci].y), color, 1.5f);
                draw->AddLine(ImVec2(screen[ci].x, screen[ci].y), ImVec2(screen[d].x, screen[d].y), color, 1.5f);
                draw->AddLine(ImVec2(screen[d].x, screen[d].y), ImVec2(screen[a].x, screen[a].y), color, 1.5f);
            }
        }
    }

    void RenderESP() {
        Matrix4 view{};
        Vec2 viewport{};
        if (!GetCamera(view, viewport)) return;

        auto entities_snap = cache::GetEspSnapshot();
        const auto& entities = *entities_snap;
        if (entities.empty()) return;

        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        if (!draw) return;

        const cache::LocalPlayerData& lp = cache::GetLocalPlayer();

        Vec3 cam_pos{};
        bool have_cam = false;
        if (wall_check) {
            instance cam = GetCameraInstance();
            if (cam.is_valid() && ReadVec3(cam.address + Offsets::Camera::Position, cam_pos))
                have_cam = true;
        }

        for (const cache::EspEntity& entity : entities) {
            if (esp_render_distance > 0.0f && lp.valid) {
                float dx = entity.root_x - lp.x;
                float dy = entity.root_y - lp.y;
                float dz = entity.root_z - lp.z;
                float dist = sqrtf(dx * dx + dy * dy + dz * dz);
                if (dist > esp_render_distance) continue;
            }

            if (wall_check && have_cam &&
                !(entity.root_x == 0.0f && entity.root_y == 0.0f && entity.root_z == 0.0f)) {
                Vec3 root = { entity.root_x, entity.root_y, entity.root_z };
                if (!LOSClear(cam_pos, root, entity.player_address)) continue;
            }

            Box2D box{};
            if (!ComputeBoxForPrimitives(entity, view, viewport, box)) continue;
            if (!box.valid) continue;

            float x1 = floorf(box.min_x);
            float y1 = floorf(box.min_y);
            float x2 = floorf(box.max_x);
            float y2 = floorf(box.max_y);

            if (box_esp) {
                ImU32 black = IM_COL32(0, 0, 0, 255);
                ImU32 white = IM_COL32(
                    (int)(box_esp_color[0] * 255.0f),
                    (int)(box_esp_color[1] * 255.0f),
                    (int)(box_esp_color[2] * 255.0f),
                    (int)(box_esp_color[3] * 255.0f)
                );

                float X1 = x1 - 1.0f, Y1 = y1 - 1.0f, X2 = x2 + 1.0f, Y2 = y2 + 1.0f;
                if (box_esp_type == 1) {
                    float box_w = X2 - X1;
                    float box_h = Y2 - Y1;
                    float len = (std::min)((std::min)(box_w, box_h) * 0.25f, 50.0f);
                    len = (std::min)(len, (std::min)(box_w, box_h) * 0.5f - 1.0f);
                    float X1L = X1 + len, Y1L = Y1 + len, X2L = X2 - len, Y2L = Y2 - len;

                    if (box_fill) {
                        ImU32 fill_c = IM_COL32((int)(box_fill_top[0] * 255), (int)(box_fill_top[1] * 255), (int)(box_fill_top[2] * 255), (int)(box_fill_top[3] * 255));
                        if (box_fill_gradient && box_fill_gradient_rotate) {
                            float t = (float)ImGui::GetTime() * 2.0f;
                            float s = sinf(t), c = cosf(t);
                            ImU32 c1 = IM_COL32((int)(box_fill_top[0] * 255), (int)(box_fill_top[1] * 255), (int)(box_fill_top[2] * 255), (int)(box_fill_top[3] * 255));
                            ImU32 c2 = IM_COL32((int)(box_fill_bottom[0] * 255), (int)(box_fill_bottom[1] * 255), (int)(box_fill_bottom[2] * 255), (int)(box_fill_bottom[3] * 255));
                            ImVec4 v1 = ImGui::ColorConvertU32ToFloat4(c1);
                            ImVec4 v2 = ImGui::ColorConvertU32ToFloat4(c2);
                            ImU32 c_tl = ImGui::ColorConvertFloat4ToU32(ImVec4(v1.x + (v2.x - v1.x) * ((s + 1.0f) * 0.5f), v1.y + (v2.y - v1.y) * ((s + 1.0f) * 0.5f), v1.z + (v2.z - v1.z) * ((s + 1.0f) * 0.5f), v1.w + (v2.w - v1.w) * ((s + 1.0f) * 0.5f)));
                            ImU32 c_tr = ImGui::ColorConvertFloat4ToU32(ImVec4(v1.x + (v2.x - v1.x) * ((c + 1.0f) * 0.5f), v1.y + (v2.y - v1.y) * ((c + 1.0f) * 0.5f), v1.z + (v2.z - v1.z) * ((c + 1.0f) * 0.5f), v1.w + (v2.w - v1.w) * ((c + 1.0f) * 0.5f)));
                            ImU32 c_br = ImGui::ColorConvertFloat4ToU32(ImVec4(v1.x + (v2.x - v1.x) * ((-s + 1.0f) * 0.5f), v1.y + (v2.y - v1.y) * ((-s + 1.0f) * 0.5f), v1.z + (v2.z - v1.z) * ((-s + 1.0f) * 0.5f), v1.w + (v2.w - v1.w) * ((-s + 1.0f) * 0.5f)));
                            ImU32 c_bl = ImGui::ColorConvertFloat4ToU32(ImVec4(v1.x + (v2.x - v1.x) * ((-c + 1.0f) * 0.5f), v1.y + (v2.y - v1.y) * ((-c + 1.0f) * 0.5f), v1.z + (v2.z - v1.z) * ((-c + 1.0f) * 0.5f), v1.w + (v2.w - v1.w) * ((-c + 1.0f) * 0.5f)));
                            draw->AddRectFilledMultiColor(ImVec2(X1 + 2, Y1 + 2), ImVec2(X2 - 2, Y2 - 2), c_tl, c_tr, c_br, c_bl);
                        } else if (box_fill_gradient) {
                            ImU32 c1 = IM_COL32((int)(box_fill_top[0] * 255), (int)(box_fill_top[1] * 255), (int)(box_fill_top[2] * 255), (int)(box_fill_top[3] * 255));
                            ImU32 c2 = IM_COL32((int)(box_fill_bottom[0] * 255), (int)(box_fill_bottom[1] * 255), (int)(box_fill_bottom[2] * 255), (int)(box_fill_bottom[3] * 255));
                            draw->AddRectFilledMultiColor(ImVec2(X1 + 2, Y1 + 2), ImVec2(X2 - 2, Y2 - 2), c1, c1, c2, c2);
                        } else {
                            draw->AddRectFilled(ImVec2(X1 + 2, Y1 + 2), ImVec2(X2 - 2, Y2 - 2), fill_c);
                        }
                    }

                    draw->AddRectFilled(ImVec2(X1 - 1, Y1 - 1), ImVec2(X1L + 1, Y1 + 1), black);
                    draw->AddRectFilled(ImVec2(X1 - 1, Y1 - 1), ImVec2(X1 + 1, Y1L + 1), black);
                    draw->AddRectFilled(ImVec2(X2L - 1, Y1 - 1), ImVec2(X2 + 1, Y1 + 1), black);
                    draw->AddRectFilled(ImVec2(X2 - 1, Y1 - 1), ImVec2(X2 + 1, Y1L + 1), black);
                    draw->AddRectFilled(ImVec2(X1 - 1, Y2 - 1), ImVec2(X1L + 1, Y2 + 1), black);
                    draw->AddRectFilled(ImVec2(X1 - 1, Y2L - 1), ImVec2(X1 + 1, Y2 + 1), black);
                    draw->AddRectFilled(ImVec2(X2L - 1, Y2 - 1), ImVec2(X2 + 1, Y2 + 1), black);
                    draw->AddRectFilled(ImVec2(X2 - 1, Y2L - 1), ImVec2(X2 + 1, Y2 + 1), black);

                    draw->AddRectFilled(ImVec2(X1 + 1, Y1 + 1), ImVec2(X1L + 1, Y1 + 2), white);
                    draw->AddRectFilled(ImVec2(X1 + 1, Y1 + 1), ImVec2(X1 + 2, Y1L + 1), white);
                    draw->AddRectFilled(ImVec2(X2L - 1, Y1 + 1), ImVec2(X2 - 1, Y1 + 2), white);
                    draw->AddRectFilled(ImVec2(X2 - 2, Y1 + 1), ImVec2(X2 - 1, Y1L + 1), white);
                    draw->AddRectFilled(ImVec2(X1 + 1, Y2 - 2), ImVec2(X1L + 1, Y2 - 1), white);
                    draw->AddRectFilled(ImVec2(X1 + 1, Y2L - 1), ImVec2(X1 + 2, Y2 - 1), white);
                    draw->AddRectFilled(ImVec2(X2L - 1, Y2 - 2), ImVec2(X2 - 1, Y2 - 1), white);
                    draw->AddRectFilled(ImVec2(X2 - 2, Y2L - 1), ImVec2(X2 - 1, Y2 - 1), white);
                } else {
                    if (box_fill) {
                        ImU32 fill_c = IM_COL32((int)(box_fill_top[0] * 255), (int)(box_fill_top[1] * 255), (int)(box_fill_top[2] * 255), (int)(box_fill_top[3] * 255));
                        if (box_fill_gradient && box_fill_gradient_rotate) {
                            float t = (float)ImGui::GetTime() * 2.0f;
                            float s = sinf(t), c = cosf(t);
                            ImU32 c1 = IM_COL32((int)(box_fill_top[0] * 255), (int)(box_fill_top[1] * 255), (int)(box_fill_top[2] * 255), (int)(box_fill_top[3] * 255));
                            ImU32 c2 = IM_COL32((int)(box_fill_bottom[0] * 255), (int)(box_fill_bottom[1] * 255), (int)(box_fill_bottom[2] * 255), (int)(box_fill_bottom[3] * 255));
                            ImVec4 v1 = ImGui::ColorConvertU32ToFloat4(c1);
                            ImVec4 v2 = ImGui::ColorConvertU32ToFloat4(c2);
                            ImU32 c_tl = ImGui::ColorConvertFloat4ToU32(ImVec4(v1.x + (v2.x - v1.x) * ((s + 1.0f) * 0.5f), v1.y + (v2.y - v1.y) * ((s + 1.0f) * 0.5f), v1.z + (v2.z - v1.z) * ((s + 1.0f) * 0.5f), v1.w + (v2.w - v1.w) * ((s + 1.0f) * 0.5f)));
                            ImU32 c_tr = ImGui::ColorConvertFloat4ToU32(ImVec4(v1.x + (v2.x - v1.x) * ((c + 1.0f) * 0.5f), v1.y + (v2.y - v1.y) * ((c + 1.0f) * 0.5f), v1.z + (v2.z - v1.z) * ((c + 1.0f) * 0.5f), v1.w + (v2.w - v1.w) * ((c + 1.0f) * 0.5f)));
                            ImU32 c_br = ImGui::ColorConvertFloat4ToU32(ImVec4(v1.x + (v2.x - v1.x) * ((-s + 1.0f) * 0.5f), v1.y + (v2.y - v1.y) * ((-s + 1.0f) * 0.5f), v1.z + (v2.z - v1.z) * ((-s + 1.0f) * 0.5f), v1.w + (v2.w - v1.w) * ((-s + 1.0f) * 0.5f)));
                            ImU32 c_bl = ImGui::ColorConvertFloat4ToU32(ImVec4(v1.x + (v2.x - v1.x) * ((-c + 1.0f) * 0.5f), v1.y + (v2.y - v1.y) * ((-c + 1.0f) * 0.5f), v1.z + (v2.z - v1.z) * ((-c + 1.0f) * 0.5f), v1.w + (v2.w - v1.w) * ((-c + 1.0f) * 0.5f)));
                            draw->AddRectFilledMultiColor(ImVec2(x1 + 1, y1 + 1), ImVec2(x2 - 1, y2 - 1), c_tl, c_tr, c_br, c_bl);
                        } else if (box_fill_gradient) {
                            ImU32 c1 = IM_COL32((int)(box_fill_top[0] * 255), (int)(box_fill_top[1] * 255), (int)(box_fill_top[2] * 255), (int)(box_fill_top[3] * 255));
                            ImU32 c2 = IM_COL32((int)(box_fill_bottom[0] * 255), (int)(box_fill_bottom[1] * 255), (int)(box_fill_bottom[2] * 255), (int)(box_fill_bottom[3] * 255));
                            draw->AddRectFilledMultiColor(ImVec2(x1 + 1, y1 + 1), ImVec2(x2 - 1, y2 - 1), c1, c1, c2, c2);
                        } else {
                            draw->AddRectFilled(ImVec2(x1 + 1, y1 + 1), ImVec2(x2 - 1, y2 - 1), fill_c);
                        }
                    }
                    draw->AddRectFilled(ImVec2(x1 - 1, y1 - 1), ImVec2(x2 + 1, y1), black);
                    draw->AddRectFilled(ImVec2(x1 - 1, y2), ImVec2(x2 + 1, y2 + 1), black);
                    draw->AddRectFilled(ImVec2(x1 - 1, y1), ImVec2(x1, y2), black);
                    draw->AddRectFilled(ImVec2(x2, y1), ImVec2(x2 + 1, y2), black);
                    draw->AddRectFilled(ImVec2(x1, y1), ImVec2(x2, y1 + 1), white);
                    draw->AddRectFilled(ImVec2(x1, y2 - 1), ImVec2(x2, y2), white);
                    draw->AddRectFilled(ImVec2(x1, y1 + 1), ImVec2(x1 + 1, y2 - 1), white);
                    draw->AddRectFilled(ImVec2(x2 - 1, y1 + 1), ImVec2(x2, y2 - 1), white);
                    draw->AddRectFilled(ImVec2(x1 + 1, y1 + 1), ImVec2(x2 - 1, y1 + 2), black);
                    draw->AddRectFilled(ImVec2(x1 + 1, y2 - 2), ImVec2(x2 - 1, y2 - 1), black);
                    draw->AddRectFilled(ImVec2(x1 + 1, y1 + 2), ImVec2(x1 + 2, y2 - 2), black);
                    draw->AddRectFilled(ImVec2(x2 - 2, y1 + 2), ImVec2(x2 - 1, y2 - 2), black);
                }
            }

            if (healthbar)
                RenderHealthBar(draw, x1, y1, x2, y2, entity.health, entity.max_health);
            if (health_text)
                RenderHealthText(draw, x1, y1, x2, y2, entity.health);
            if (name)
                RenderName(draw, x1, y1, x2, y2, entity.name);

            if (distance && lp.valid) {
                float dx = entity.root_x - lp.x;
                float dy = entity.root_y - lp.y;
                float dz = entity.root_z - lp.z;
                float dist = sqrtf(dx * dx + dy * dy + dz * dz);
                RenderDistance(draw, x1, y1, x2, y2, dist);
            }
            if (rig_type)
                RenderRigType(draw, x1, y1, x2, y2, entity.is_r15);
            if (tool_esp)
                RenderTool(draw, x1, y1, x2, y2, entity.tool_name, distance && lp.valid);
        }
    }
}
