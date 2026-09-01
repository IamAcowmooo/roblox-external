# HANDOFF / MASTER NOTES

**Repo:** `Glockerz/roblox-external`
**Branch:** `arena/01a05e8a-roblox-external` (`main` untouched by request)
**Last commit:** `(see §10 changelog — HEAD of this branch)`
**Target client:** `version-f5a60436d48947d3` (`0.736.0.7361348`)

A Roblox usermode **external** cheat. It never injects — it reads/writes the Roblox
process with `ReadProcessMemory` / `WriteProcessMemory` and draws its own transparent
DX11 + ImGui overlay on top.

---

## 1. Quick start

### Build
1. Open `roblox external.slnx` in Visual Studio.
2. **If you're on VS 2022:** the project asks for toolset `v145` (VS 2026). Change it to `v143`:
   ```powershell
   (Get-Content "roblox external\roblox external.vcxproj") -replace 'v145','v143' | Set-Content "roblox external\roblox external.vcxproj"
   ```
3. Set **Release** + **x64**, then Build (`Ctrl+Shift+B`).
4. Output: `x64\Release\roblox external.exe` (solution build) or
   `roblox external\x64\Release\roblox external.exe` (project build).

Requires the **Desktop development with C++** workload and the Windows 10/11 SDK.
No package manager — ImGui (1.91.1) is vendored.
Libs linked: `d3d11 dxgi dwmapi shell32`.

> ⚠️ Release builds with **AVX2** (`EnableEnhancedInstructionSet`). Needs a ~2013+ CPU.
> If you hit an illegal-instruction crash, drop it to `StreamingSIMDExtensions2`.

### Run
1. Start **Roblox** (windowed or borderless — overlays can't draw over exclusive fullscreen).
2. Run the exe **as Administrator** (needed to open a handle to Roblox).
3. The menu **opens automatically**. Toggle with **HOME** (rebindable in the keybinds tab).

Roblox does **not** need to be running first — the overlay launches immediately and
attaches in the background, and re-attaches automatically if Roblox closes/reopens.

**Exiting:** the `X` in the title bar, or right-click the **tray icon** → exit.
There's also a real **taskbar / Alt-Tab entry** ("roblox external"). No Task Manager needed.

---

## 2. Feature status

Honest state. "Unverified" = written and wired, never confirmed working in-game.

| Feature | Status | Notes |
|---|---|---|
| Walkspeed | ✅ **Confirmed working** | Writes `Humanoid::Walkspeed`. The one known-good write path. |
| ESP boxes | ⚠️ Rewritten, re-test | Box now uses canonical sizes + one world AABB (no Size/Rotation reads) — see §4.2 |
| ESP extras (name/health/distance/tool/skeleton/china hat) | ✅ **Confirmed working** | |
| Chams (regular) | ✅ **Confirmed working** | Does **not** use the deleted mesh backend |
| Aimbot + FOV circle | ✅ **Confirmed working** | Needs a keybind set |
| Noclip | ✅ **Confirmed working** | Needs a keybind set |
| Hitbox expander | ❓ Unverified | |
| Inventory checker | ✅ **Confirmed working** | Hold key with cursor over a player |
| FOV changer | ✅ **Confirmed working** | Radians bug fixed — see §4.3 |
| **Infinite jump** | ✅ **Confirmed working** | Edge-triggered: each tap of space writes `AssemblyLinearVelocity.y = jump power` — jumps chain mid-air (hold no longer rockets up) |
| **Flight** | ❌ **Broken** | 4 approaches tried — see §4.1 |
| **Click teleport** | ⚠️ Partly working | TP'd to cursor in 1st person; 3rd person + windowed fixed but unverified |
| **Skybox changer** | ❌ **Broken** | Invalidation-order fix applied, unverified — see §4.4 |
| Config save/load/rename | ❓ Unverified | Files under `GetConfigDir()` |

**Removed on request:** Blade Ball, Rivals skin changer, Phantom Forces special-casing,
korblox/rage, 3D ESP preview, mesh chams + memory mesh chams (see §5).

---

## 3. Architecture

```
roblox external/
  main.cpp        entry (WinMain), FeatureLoop thread, AttachLoop (auto-reconnect), render_ui()
  overlay.hpp     overlay window, D3D11, ImGui init + theme, input thread, tray icon, taskbar window
  menu.cpp        entire GUI: ui:: widgets, top tab bar + content well, all pages
  globals.h       every setting as an inline global + LogLine() + g_request_exit
  memory.h/.cpp   RPM/WPM wrappers, instance struct, name/classname/children readers
  process.h/.cpp  FindRoblox(), GetRobloxWindow()
  game.h/.cpp     ReadDatamodel(), GetGameName(), GetPlaceId()
  cache.h/.cpp    background thread (16ms) publishing immutable snapshots
  offsets.h       all offsets for the target client version
  features/       aimbot click_teleport config esp flight fov_changer
                  hitbox_expander infinite_jump inventory_checker noclip
                  skybox_changer walkspeed
```

### Threads
| Thread | Job |
|---|---|
| main / render | `discord_overlay::run()` — pumps messages, draws ImGui, owns click-through |
| cache | re-reads DataModel → Players → characters every 16ms, publishes snapshots |
| feature | `FeatureLoop()` — runs all `Run*` features every 1ms |
| attach | `AttachLoop()` — attach + auto-reconnect, 1s poll |
| input | `input_thread()` — `GetAsyncKeyState` polling, feeds ImGui, menu toggle |

> The overlay namespace is now simply called `overlay` (renamed from the old
> `discord_overlay` in a clean-up pass). `overlay.h` (a 6-line shim over
> `overlay.hpp`) was folded away; include `overlay.hpp` directly.

### Data flow
`g_base_address + VisualEngine::Pointer` → `VisualEngine::FakeDataModel` →
`FakeDataModel::RealDataModel` → services by **class name** → players → characters → parts.

Rendering features must be called from `render_ui()` (render thread);
memory-only features from `FeatureLoop()`.

---

## 4. Open problems (with what's already been ruled out)

### 4.1 Flight + infinite jump — THE blocker

Five mechanisms tried:

| # | Approach | Result |
|---|---|---|
| 1 | Write `AssemblyLinearVelocity` | Nothing happens |
| 2 | Write `Primitive::Position` | **Sinks through the floor** (collision solver resolves the overlap) |
| 3 | `PlatformStand = true` + velocity | Ragdolls, still falls |
| 4 | Position + collisions disabled, self-integrated arc | Still broken |
| 5 | Velocity-driven flight + engine-driven jump | **Infinite jump ✅ confirmed; flight re-test pending** |

**Latest attempt (unverified, needs the debug-tab write test):**
- **Flight** now writes the *desired* `AssemblyLinearVelocity` (move dir × speed) every
  frame instead of zeroing it, so the solver agrees with the position write instead of
  fighting it — collisions still disabled while flying.
- **Infinite jump** is now edge-triggered on space and writes the **upward velocity**
  directly (`AssemblyLinearVelocity.y = jump power`, SET not +=) — one jump per tap,
  works mid-air so jumps chain forever, gravity still arcs each one. The old
  hold-space version re-asserted `Humanoid::Jump = true` every frame, which is why
  holding space rocketed the character straight up. (Velocity write is confirmed
  working via the debug-tab "test VELOCITY write" button.)

**Key evidence:** walkspeed (a **Humanoid** field write) works, and now
**`AssemblyLinearVelocity` (a Primitive write) is confirmed working too** ("test velocity"
button). That proves `hrp_primitive` is valid and the Primitive write path works — the
remaining question is purely *which position/CFrame field the solver actually obeys*.

**Also confirmed:** `Humanoid::HumanoidRootPart (0x478)` reads back **`0x0`** — it is wrong
for this client build. The root part is currently resolved by **name** instead
(`"HumanoidRootPart"` among the character's children), which does work since ESP renders.

**⏭️ NEXT STEP — re-test the position write (now writes BOTH CFrames):**
Debug tab → **"test POSITION write (+10 studs up)"** (now writes `0xEC` **and** `0x134`
and reports each delta) and **"test VELOCITY write"**.

**Probe result (user pasted it) — offsets are now CONFIRMED (twice, two independent
samples at different positions/orientations):**
```
+0x0C8..0x0F4 : CFrame #1   rotation = -0.64,0,1,0 / 0,1,0 / 0.64,0,0.77
                              position = (-41.37, 3.11, -10.50)   -> 0xEC ✓
+0x0F8..0x10C : AssemblyLinear/AngularVelocity (0,0,0 idle)        -> 0xF8 ✓ / 0x104 ✓
+0x110..0x13C : CFrame #2   SAME rotation, position at 0x134       -> 0x110 / 0x134
+0x1BC..0x1C4 : Size = (2.00, 2.00, 1.00)                          -> 0x1BC ✓
```
So `Primitive::Position (0xec)`, `Rotation (0xc8)`, `Size (0x1bc)` are all **correct**.
The position **write** is reverted because the primitive holds a **second CFrame at
0x110** (translation `0x134`) that re-syncs the one at `0xC8`. Flight + click teleport
now write **both** `Position` and `Position2` every frame; the debug button writes both
and reports each delta so we can confirm which (if either) sticks. If neither sticks,
the real source of truth is a 3rd structure (assembly solver) and we need a fuller dump.

**Second probe sample** (different spawn, player at `(-24.63, 3.11, 164.76)`, yawed ~90°):
rotation `0xC8` = `-0.04,1,-1 …`, position `0xEC` = `(-24.63, 3.11, 164.76)`,
CFrame #2 position `0x134` = identical, and this time **velocity `0xF8` = `(-15.99, 0, 0.64)`
was non-zero** — confirming the velocity read/write is live and matches the character's
actual motion. Layout is byte-for-byte consistent with the first sample.

### 4.2 ESP boxes sit slightly high / float at distance
`Primitive::Size (0x1bc)` and `Primitive::Rotation (0xc8)` are now **confirmed valid**
(the float probe shows Size = (2,2,1) and a sane rotation matrix). The box path still
**ignores Size/Rotation** and builds one world-space axis-aligned box per part from its
centre + canonical body size (`esp.cpp → CanonicalPartSize()`: Head 2×1×1, torso 2×2×1,
arms/legs 1×2×1, hands/feet 1×1×1), then projects that single box's 8 corners. If the box
still floats at distance, the remaining suspect is the world-to-screen projection, not
the part data.

### 4.3 FOV changer
Was writing **degrees** into a field Roblox stores in **radians** — 70 became ~4010°,
which wrapped the projection and flipped the screen. Now converts and sanity-checks the
range. Click teleport had the same bug and was fixed too. Both unverified.

### 4.4 Skybox changer
It was doing: invalidate → write textures → set `SkyValid`/`LightingValid` back to **true**,
which tells the renderer its cached sky is still valid so it never re-uploads. Now it
invalidates *after* writing, **and** it re-applies the six face strings every 2.5s while
enabled (a core script or the renderer reverting the sky can no longer permanently undo
it). Still unverified in-game. The world tab prints a live status string
(`skybox_debug_msg`) — read it: `No Sky in game`, `FAIL: RenderView invalid`,
`Skybox applied (...)` each point somewhere different.

---

## 5. Offsets — important

`offsets.h` matches the user-supplied dump for `version-f5a60436d48947d3`, verified
key-by-key (57 critical offsets: 0 mismatched, 0 missing).

**Known-wrong in this dump:**
- `Humanoid::HumanoidRootPart = 0x478` → reads `0x0`. Unused; name lookup is used instead.
- `Primitive::Position` / `Primitive::Size` → **suspect**, pending the write test.

**Entries the dump gave as `0x0`** (dumper couldn't resolve them). The previous known-good
values were kept rather than zeroing, and none are referenced in code:
`MouseService::SensitivityPointer`, `World::{worldStepsPerSec, FallenPartsDestroyHeight,
AirProperties, Primitives}`, `AirProperties::{AirDensity, GlobalWind}`,
`{Local,Module,}Script::ByteCode`.

### ⚠️ The `NameContainer` gotcha — read before updating offsets
The dump splits the instance name into **two** offsets:
```
Instance::NameContainer = 0x70
Instance::Name          = 0x8
```
`get_name()` must therefore do a **two-step** read (same pattern as `get_class_name`):
```cpp
uintptr_t container = read<uintptr_t>(address + Offsets::Instance::NameContainer);
return fetchstring(container + Offsets::Instance::Name);
```
Older dumps had a single absolute `Instance::Name = 0xb0`. Getting this wrong silently
returns **garbage names**, which breaks every name-based lookup — player detection, body
parts, skeletons, the root part — while class-name lookups keep working. That mismatch
caused a long stretch of "nothing works but walkspeed". Fixed in `8906f70`.

`offsets_dump.txt` (repo root) holds the full dump this `offsets.h` was generated
from (client `version-f5a60436d48947d3`). If you re-dump, paste the new dump there
and diff it against `offsets.h`.

---

## 6. Diagnostics built in

- **debug tab** — base address, VisualEngine, DataModel, game name, place id, game loaded,
  viewport, view matrix, local player, humanoid, hrp primitive, position/velocity/size
  read-backs, cached player count, and the two **write test** buttons.
- **log tab** — in-GUI log (replaces the old console), 200-line ring buffer, `LogLine(...)`.
- **world tab** — live skybox status string.
- "not in a game" warning when `place id == 0` (home page has no world/players).

---

## 7. Performance work already done

- **Zero-copy cache snapshots.** `GetEspEntities()` returned `EspEntity` **by value** from
  9 call sites/frame — ~1.4 MB of memcpy per frame at 30 players. Now
  `shared_ptr<const vector<...>>`.
- **`EspEntity` shrunk 5,320 → 2,760 bytes** (`part_names[64][64]` → `[64][24]`).
- **Batched child reads** — one `ReadProcessMemory` for the whole child array instead of
  one syscall per child.
- **Class-name cache** keyed by descriptor pointer (descriptors are stable, shared per class).
- **Single-pass traversal** — `fetch_entities` walked each child list 3× per player, now 1×.
- **`read_string_raw`** tries 128→64→32→16 bytes; a fixed 200-byte read fails entirely if
  it crosses into unmapped memory, which silently blanked names near page boundaries.
- **Release flags:** `/Ox`, Speed, AnySuitable inlining, omit frame pointers, string pooling,
  fast FP, AVX2, `/MP`, LTCG, `OptimizeReferences`, `COMDATFolding`.
- **Vendor tree slimming.** The whole `Clipper2/` tree (C++ lib + C# + Delphi + DLL
  wrappers + tests/examples) was removed — ~2.7 MB / 230 files across four languages —
  along with the dead `DrawMergedPoly` that pulled it in. Dead `esp.cpp` helpers
  (convex hull, R15 chain lookup, mesh-logging sets) and ~20 unused globals in
  `globals.h` were also deleted. `offsets.h` dropped two junk entries not present in
  the dump (`Humanoid::PlatformStatePointer = 0xb9fe4b32`, `Instance::Attribute*`).

> On "use a faster language": C++ is already correct here. The workload is **syscall-bound**
> (`ReadProcessMemory` ≈ 1–3µs each), not CPU-bound. Rust/C/asm would make identical
> syscalls at identical speed. Wins come from *fewer, larger* reads and less copying.

---

## 8. Malware audit (done at the start — clean)

No RAT/backdoor/stealer/dropper/persistence. Checked: outbound endpoints, HTTP verbs,
injection APIs, persistence, credential theft, encoded payloads, build-event hijacking,
binary artifacts. Only "risky-looking" APIs are inherent to the design:
`WriteProcessMemory` into Roblox, and `ShellExecuteA` to open the config folder.

Since the mesh backend was deleted, the project makes **no outbound network requests at all**.

---

## 9. Housekeeping / possible next cleanups

- ~~Rename namespace `discord_overlay`~~ → done, it's `overlay` now.
- ~~`overlay.h` shim~~ → folded away, include `overlay.hpp`.
- ~~`.vcxproj.filters`~~ → regenerated (imgui + per-feature folders).
- ~~Clipper2 vendor tree~~ → removed entirely (see §7). The only user
  (`DrawMergedPoly` in esp.cpp) was dead code left over from the mesh backend.
- ~~Uncompiled vendored ImGui extras~~ → removed: `imgui_demo.cpp`, `imadd.cpp`,
  `TextEditor.*`, `imgui_toggle*`, `addons/`, `includes.h`, `imgui_offset_rect.h`,
  `misc/freetype/`. The whole repo is now C/C++ only and the ImGui tree contains
  exactly what the project compiles.

## 10. Changelog (this branch)

```
(wip)  UI restyled to the reference's exact glass recipe (Rose): animated
       accent backdrop + translucent Header.Fade->Surface.Fade gradient + quiet
       outline/shadow, flat Knob-colour controls (no glow/spark/catch-light);
       tabs stay at the TOP. Decoded the float probe: Position/Rotation/Size
       confirmed, 2nd CFrame at 0x110/0x134; flight + click teleport now write
       both positions. Second probe re-confirmed the layout; velocity write
       confirmed working; infinite jump rebuilt as edge-triggered velocity
       impulses (per-tap, mid-air chainable) instead of hold-to-ascend.
       both positions; write-test button reports both deltas.
87837e4 Liquid-glass UI remake + robust ESP box + offset probe
9c56489 Fix rainbow accent not resetting; red accent restored; compact + glassier
       UI; canonical ESP body-part sizes; velocity-driven flight + engine-driven
       infinite jump; skybox periodic reapply; delete uncompiled ImGui extras
5897afd Glassmorphic UI overhaul: frosted panels, soft shadows, indigo accent,
       new title bar + status chip + footer; rename namespace -> overlay; fold
       overlay.h; regenerate .vcxproj.filters; fix KeyName dangling pointer
5897afd Remove Clipper2 vendor tree (C++/C#/Delphi/DLL) + dead DrawMergedPoly,
       dead esp.cpp helpers and unused globals; prune junk offsets
79a5dba Remove orphaned mesh cham backend and all references
c24c828 Revert mesh backend enablement (headers incompatible), drop /GS- sdl conflict
2a24d66 Enable mesh cham implementations, menu opens on start, accent on separators, part-size guard
a4fd54c Zero-copy cache snapshots, shrink EspEntity, full release optimizations, fix write test target
ecafe69 Even nav grid, transparency across all backgrounds, add position write test
a008b4b Fix FOV radians bug, rewrite flight + infinite jump, live accent, uniform UI metrics
bf15a63 Condense tabs, UI config page + FOV changer, taskbar window, HRP via humanoid, skybox invalidation
0d07619 PHETAMINE-style UI, tray icon + in-app exit, toggle flight, impulse infinite jump
544490a Add infinite jump + inventory checker, remove korblox, click teleport client coords
6ae48aa Position-based flight, click teleport 3rd person, modern GUI, remove 3d esp preview, consolidate keybinds
2d985a2 Remove console for in-gui log, keybinds tab, optimize memory reads
b22c49a Fix ESP box rotation accuracy, improve flight, add click teleport
8906f70 Fix Instance name reads (NameContainer), auto-reconnect, click-through outside menu
a5c179a Restore focus to Roblox when menu closes, flag not-in-game state
dfff981 Remove game-specific features, glassy red theme, debug tab
a18a7f1 Replace Discord overlay hook with standalone transparent overlay window
bf8e898 Change menu toggle key from INSERT to HOME
015cc98 Update offsets to client version-f5a60436d48947d3
```

---

## 11. TL;DR for whoever picks this up

1. **Run the two write tests in the debug tab.** Click teleport (and the position-corrector
   half of flight) are still blocked on that measurement. Infinite jump no longer depends on
   it (it writes Humanoid fields now), so test it separately.
2. If the position write doesn't stick → re-dump `Primitive::Position` and `Primitive::Size`.
3. Check `part size read` in the same tab to settle the ESP box offset.
4. Read the skybox status string in the world tab before touching that feature.
5. When updating offsets, **mind the `NameContainer` two-step read** (§5).
