# PSP game-client port progress

## Goal and acceptance gate

Port the real local game runtime to PSP-3008 (64 MB): legacy places, Lua scripts,
local player, character/controller/camera, collisions, jumping, classic animation,
death and respawning. Existing GU scene viewer is a foundation, not completion.
Validate Crossroads plus real script-driven obby places, then package a PRX-backed
PBP in `G:\OpenRBX-PSP` and `G:\OpenRBX-PSP.zip`. Preserve desktop modifications.
Real PSP testing must be distinguished from PPSSPP testing. ME acceleration is not
implemented or claimed; profile a suitable workload before choosing an ME path.

## Current work

- Existing platform: streaming legacy XML geometry, native GU/GUM renderer, browser,
  30 FPS VBlank pacing, MEMSIZE=1 and 32 MiB reserved heap. Earlier viewer build
  validated in PPSSPP's 64 MB model; that is not gameplay validation.
- Implemented: retain instance hierarchy, typed properties, structured values,
  referent links, script source and non-rendered objects during streaming loading.
- Implemented: portable instance storage and hierarchy operations, adapted from
  `Client/App/v8tree/Instance.cpp` (ordered children, parenting, lookup, clone,
  recursive Remove). Runtime references are stable integer handles for MIPS.
- Implemented: repository Lua 5.1.1 VM, retaining its C++ exception handling;
  three explicit staging adaptations remove Boost-only bookkeeping, modernize
  exception signatures and fix nullable upvalue conversion. Desktop VM untouched.
- Implemented and host-tested: script-local globals, loaded-instance handles,
  scalar/Ref property access, hierarchy methods, Instance.new, Clone/Remove,
  service identity, heartbeat wait/delay, bounded VM heap/logs/thread count,
  non-yielding instruction limit and repeated pending-coroutine teardown.
- Added portable `math3d`, `schema`, and `script_values`: inherited class lookup,
  shared immutable engine defaults with lazy property materialization, canonical
  size/CFrame aliases, numeric/Ref/Vector3/CFrame properties, rigid transforms,
  Euler/quaternion/axis-angle/look-at construction and Lua value operations.
- Added yielding signals: Connect/connect, disconnect, connected, Wait/wait,
  typed Value.Changed, ordinary property changes, ChildAdded/ChildRemoved,
  ancestry/descendent notifications, native Touched bridge and RunService
  Stepped(time,dt)/Heartbeat(dt). Native world and character contacts emit Touched.
- Added script discovery without duplicate starts, owner-detach/disable task and
  connection cancellation, shared globals table, and script names in error chunks.
- Added graph-to-GU synchronization: in-place remeshing for changed fixed-topology
  parts, bounded full rebuild only on geometry topology/membership changes,
  including reparented ancestors. This is not physics.
- `main.cpp` now starts actual place scripts, advances them using simulation
  seconds (camera clamping is separate), synchronizes geometry and reports Lua
  messages/heap/threads. Browser pauses the runtime. The current native build is
  verified in the 64 MB emulator; the verification sections record exact results.
- World physics and local-player source are now implemented; see the dated
  verification sections below for what has actually been tested.
- Implemented generation-checked instance reclamation with Lua/native ownership;
  stress evidence and remaining lifetime caveats are recorded in Phase 5.
- Still missing: full class semantics, gameplay acceptance
  in real obby places and ME investigation. Recognizing a
  class or retaining its properties does not implement that class's behavior.
  Do not package as playable.
- Lua-held backups and Ref graphs survive collection; retired local rigs release
  their owned callbacks. General cross-heap event cycles outside session teardown
  still require a broader audit; bounded tests are not proof for every script.

## Next work

1. Validate the integrated local-player/controller/animation build in PPSSPP and
   exercise real Crossroads gameplay, then real script-driven obby interactions.
2. Exercise real obby assets/scripts and expand runtime coverage. Address measured
   respawn rebuild hitches and verify longer regeneration/lifetime scenarios.
3. Exercise real places, bounded reload/respawn memory, profile CPU/GPU workloads,
   investigate ME feasibility, update package and compatibility documentation.

## Verification history

- Previous accepted viewer: 14 ASan/UBSan loader checks, PBP MEMSIZE/PRX verification,
  PPSSPP 1.20.4 64 MB model with Crossroads and render test. Physical PSP untested.
- Active goal progress classification: implementation progress (new runtime sources
  and loader integration); no blocker, completion unproven.
- Crossroads source inspection: 1,963 serialized instances, 79,947 properties,
  32 Script instances (including templates under StarterPack). Loader adds the
  DataModel root. Full host scene uses 10.897 MiB including 27,504 triangles;
  this is a 64-bit host allocation-accounting figure, NOT measured PSP memory.
  Shared property/class/component-name storage and load-time capacity compaction
  keep the full model within the loader budget without discarding script data.
- Host runtime and Lua suites passed under ASan/UBSan with halt_on_error and leak
  detection enabled after correcting legacy nullable-upvalue UB. 17 loader
  acceptance/rejection cases passed. Source staging preserves Lua copyright.
- Final formatted-source build passed all host suites (halt-on-error ASan/UBSan),
  Crossroads loading and PSP cross-compilation. The MIPS build links the actual
  engine Lua VM and produces a verified **797,654-byte PBP**: MEMSIZE=1, MIPS PRX
  type 0xffa0. Formatter dry-run passed. Engine Lua's existing style/fallthrough
  warnings remain; no desktop sources were changed in this goal phase.
- Latest PBP copied only to the development PPSSPP Memory Stick, not the release
  package. Booted under PPSSPP 1.20.4 with PSPModel=1 (64 MB), D3D11, 1x rendering.
  Crossroads loaded and rendered at steady 30 FPS with full retained instance
  data: **7.16 MiB scene accounting, 7.33 MiB live heap / 32 MiB heap budget**.
  Switched to RenderTest (0.03 MiB heap), then back to Crossroads (7.33 MiB heap).
  This validates metadata loading and that particular reload sequence, not Lua
  execution on MIPS, gameplay, arbitrary reload counts or physical hardware.
- Cropped development screenshots (no unrelated desktop contents):
  `build-psp/screenshots/screenshot-1794484464438-ca36429c719f2.png` (Crossroads),
  `build-psp/screenshots/screenshot-1794484484212-1640bd1737f78.png` (RenderTest),
  `build-psp/screenshots/screenshot-1794484489924-4e839f25ea1d2.png` (Crossroads reload).
- Build/test logs on authorized server: `~/0drel/openrbx-psp/psp/host-test.log`,
  `psp-build.log`, `pbp-check.log`. Source staging tar is local `build-psp-source.tar.gz`.
- `G:\OpenRBX-PSP` and its ZIP still contain the earlier accepted viewer. Do not
  confuse that artifact with current runtime source or claim gameplay validation.

## Full-goal completion checklist (not achieved)

| Requirement | Current evidence / remaining work |
| --- | --- |
| Instances, serialization, services and lifecycle | Graph, shared defaults, class ancestry and import tested; full class behavior and ongoing object reclamation remain. |
| Lua, bindings, signals, scheduler | Engine VM/value bindings/yielding signals tested on host. Real Crossroads scripts and dynamic Lua-to-GU path verified on MIPS in PPSSPP; Sound and other unsupported APIs remain. |
| Physics, character, humanoid, joints, classic animation | Basic fixed-step world collisions/rigid assemblies now tested on host and MIPS. Player/controller, humanoid states, Motor rig and classic animation remain. Full solver features still partial. |
| Integrated PSP game controls/camera/browser/GU | Browser/camera/dynamic scene sync verified; playable controller/camera remain. |
| Local player, death, respawn, clean reload | One metadata place-switch sequence and 25 Lua teardown cycles pass; actual player lifecycle remains. |
| MIPS/VFPU/GPU optimization, 64 MB, ME investigation | Native GU/GUM and measured heap fit; gameplay profiling and ME workload/cache/synchronization investigation remain. No ME execution claimed. |
| 30 FPS, extended memory, costs | MEMSIZE=1, 32 MiB heap, VBlank cap verified; measure final gameplay costs later. |
| Real Crossroads and script-driven obby acceptance | Crossroads geometry/metadata only; no obby acceptance yet (currently only Crossroads available under G:\OpenRBX\places). |
| PRX-backed PBP, reproducible package and compatibility report | Development PBP verified; release folder/ZIP still accepted viewer. Update only after integrated verification. |
| Emulator versus physical PSP | 64 MB PPSSPP rendering/metadata verified; runtime gameplay and physical PSP unverified. |

## Phase 2 verification: values, signals and running place scripts

- All host checks passed with ASan/UBSan halt-on-error and leak detection, including
  the 17 loader cases and new `bindings.lua`/`bindings_check.cpp` suite. This tests
  transform constructor/operators/inverse/variadic conversions, inherited default
  properties, aliases, typed Value.Changed, yielding callback and signal-wait
  arguments, disconnects, native Touched delivery, descendant notifications,
  50 in-place part moves without world-buffer growth/reallocation, 20 ancestor
  detach/reattach cycles, and complete Lua teardown. These are not physics tests.
- New diagnostic `place_scripts_check` runs the actual Crossroads scripts for
  10 simulated seconds: leaderboard startup, five regeneration coroutines, and
  projectile timers execute. One remaining startup error is explicit:
  `RocketScript:63: play is not a valid member` (Sound API/audio unported).
  The audit deliberately does NOT equate its exit status with full compatibility.
- Verified current MIPS PBP: **894,082 bytes**, title `OpenRBX PSP - Runtime WIP`,
  MEMSIZE=1, PRX type 0xffa0. Native GU/GUM and the repository's C++ Lua VM link.
- Executed in PPSSPP 1.20.4, PSPModel=1, D3D11/1x: ScriptRenderTest ran its actual
  Lua while the block visibly moved/rotated/faded. Approximately 30 FPS, 0.04 MiB
  live heap, one suspended coroutine. Two distinct game-only screenshots:
  `build-psp/screenshots/screenshot-1794486152386-3c0c98c99f2d1.png`,
  `build-psp/screenshots/screenshot-1794486158213-3dc21c9e9a447.png`.
- Crossroads executed actual place scripts on emulated MIPS: initially 24 threads,
  1,749 parts and 13.24 MiB heap, then five regeneration waiters, **1,730 parts,
  25,984 triangles, ~30 FPS and 13.08 MiB live heap** after its 19 expired
  projectile parts detached. This directly exercises signal waiting and graph
  removal through the real Lua/GU integration; it is still not player gameplay.
  Screenshot: `build-psp/screenshots/screenshot-1794486187874-7213dace20211.png`.
- Switched from active Crossroads scripts to ScriptRenderTest: heap returned to
  0.05 MiB and one coroutine, with no stale Crossroads script output. Screenshot:
  `build-psp/screenshots/screenshot-1794486207159-0539e2bfd792a.png`.
- Current development PBP is only in the emulator memory stick. The G: release
  folder/ZIP remain the accepted viewer; neither has been relabeled playable.
- Next turn should implement physics/player/controller integration. Do not confuse
  the validation fixture with the required script-driven obby gameplay acceptance.

## Phase 3 verification: native world physics and copy-on-write snapshots

- Added `collision` and `PhysicsWorld`: fixed 240 Hz, retained elapsed-time debt,
  gravity -196.2 studs/s², time-scaled angular damping, OBB SAT, sphere/box,
  sphere/wedge and convex wedge SAT, BVH queries, linear contact impulses,
  restitution/friction, sleeping and physical-contact Touched dispatch.
- Porting evidence: `SimBody::step` semi-implicit integration; `Constants` gravity
  and `Units::kmsAccelerationToRbx` factor 20; `JointBuilder` / `Joint` plane and
  0.35-stud face-overlap limits; Weld/Snap/Glue surface compatibility rules.
  The old engine uses Ball primitives for both ball and cylinder Part shapes
  (`PartInstance::setPartTypeXml`); this collision behavior is preserved.
- Most Crossroads parts are UNANCHORED (1,579 serialized Part/SpawnLocation items),
  so they must not simply be treated as a frozen map. Added rigid surface
  assemblies and explicit Weld/Snap/Glue links, C0/C1 rest-frame alignment and
  anchored propagation. Loose bodies remain dynamic.
- Initial host physics audit: 1,750 active physical parts, 138 assemblies, 1,612
  connecting edges; no part moved >2 studs during the first 0.5 seconds. This is
  diagnostic stability evidence, not full Crossroads gameplay acceptance.
- Added host tests for fixed-step frame independence, gravity units, floor
  landing, trigger contacts, wake-by-script velocity, sleeping, studs/inlets,
  explicit Welds with C0/C1, retained backlog and OBB/wedge narrow phase. All passed
  under ASan/UBSan; the longer 3-second Crossroads audit settles with zero awake
  assemblies and zero parts displaced by >2 studs. Current audit: 0.726 MiB physics
  accounting on the 64-bit host (not a measured PSP total).
- `main.cpp` now integrates physics between Lua and mesh synchronization and
  exposes sync/step/publish frame costs and awake assemblies in the HUD. Native
  emulator results and limitations are recorded below.
- Added asset-mode XML loading and cross-DataModel subtree import (owns copied
  symbols and remaps Ref properties) for reuse of actual character assets.
  Staged the existing `character.rbxm`, `humanoidAnimate.rbxm` and
  `humanoidHealth.rbxm` from the local desktop content in `ports/psp/content`.
  This is preparation, NOT player spawning/animation implementation yet.
- Added `PhysicsRenderTest.rbxl` as a regression fixture for falling bodies and
  script response to real contacts. Anchored RenderTest's deliberately static
  primitive samples now that simulation is enabled; real places are not frozen.
- Remaining limitations of this first solver: no collision-driven angular
  impulse, breakable/spring/hinge constraints, continuous high-speed collision,
  character controller or local-player lifecycle. These must not be described
  as a finished game/physics port. Current release ZIP is still the old viewer.
- Active goal turn classification: implementation progress. No external blocker;
  the full goal and gameplay acceptance gate remain unchanged and unachieved.

### Corrections and performance findings

- Avoided accidental auto-welding on unrelated world rebuilds: pre-existing auto
  links persist, and new links are considered only for new/resized/resurfaced
  parts or explicit MakeJoints. A landed body stays free after another part is
  created. BreakJoints removes surface and explicit links; Lua MakeJoints and
  BreakJoints call the real native world implementation, not stubs.
- Preserve sleep state for unchanged assemblies across unrelated membership
  changes. New distant parts and future player spawns must not wake the entire map.
- Found a severe native idle-cost bug: earlier development builds could drop to
  ~0.8 FPS despite no awake bodies. Fixed by not parsing/publishing unchanged
  bodies and advancing sleeping-world time without redundant collision loops.
- Active-path work now updates only changed BVH leaves/ancestors, not the whole
  static tree four times per 240 Hz tick. Translation-only assemblies avoid full
  per-part matrix multiplication on every contact correction. Rotation paths
  remain covered by rigid-rest-frame tests. Initial world settling still spikes;
  do not present the cold load as constant 30 FPS.
- Added copy-on-write PropertyList snapshots. Clones share immutable arrays until
  mutation; Ref remapping detaches only reference-bearing lists. Const iteration
  never detaches. Tests mutate original and clone independently, test Ref remapping,
  import across model lifetimes and load all three actual character assets.
- Crossroads script-only host model accounting dropped from ~18.744 MiB to
  **10.057 MiB** without deleting serialized properties. Detached instance slots
  are NOT reclaimed yet, so this does not satisfy bounded respawn memory by itself.
- Added real include dependencies (`-MMD -MP`) to PSP builds and force object
  rebuilds when Makefile flags change; otherwise header layout changes could
  silently link old Instance/DataModel ABI users.

### Current verified development artifact and emulator evidence

- Latest PBP: **965,410 bytes**, MEMSIZE=1, MIPS PRX type 0xffa0, title
  `OpenRBX PSP - Runtime WIP`. Source and build are on the authorized server under
  `~/0drel/openrbx-psp/psp`; PBP is installed only in the development emulator.
- Full host suites passed after the copy-on-write change; the final sleeping-state
  preservation change also passed the physics audit and targeted regression test.
  Integrated physics/Lua test covers falling bodies triggering the actual place
  script, Lua MakeJoints/BreakJoints, stable vertex buffers and complete teardown.
- PPSSPP 1.20.4, 64 MB PSP model, D3D11/1x: Crossroads reaches **30 FPS**, 1,730
  visible parts / 25,984 triangles, five Lua waiters, zero awake / 119 assemblies,
  **8.82 MiB live heap / 32 MiB budget**. Settled physics cost approximately 1.2 ms:
  sync 1.1 ms, step/publish rounded to 0.0 ms. This replaces the earlier 13.77 MiB
  pre-COW physics build; RAM accounting is not OS/module/VRAM total.
  Game-only screenshot: `build-psp/screenshots/screenshot-1794489969169-91f6654a185be.png`.
- PhysicsRenderTest: falling box and ball both hit the non-colliding trigger;
  actual Lua changes their colors and prints the contact name. About 30 FPS,
  0.9 ms physics while two bodies are awake (step 0.2, publish 0.7 ms).
  Switching from Crossroads frees snapshots/VM/world state: approximately 0.06
  MiB live heap and only the new map's messages remain.
  Screenshot: `build-psp/screenshots/screenshot-1794490098808-0cff30bfe59b2.png`.
- Known runtime error remains `RocketScript:63: play is not a valid member`
  (Sound API/audio). No physical PSP hardware test and no ME acceleration claimed.
- Optional real obby file path was requested asynchronously; only Crossroads is
  currently present in `G:\OpenRBX\places`. Do not block independent player work
  waiting for that preference, and do not count a synthetic fixture as real obby
  gameplay acceptance.
- At this checkpoint, G: release folder and ZIP remained the accepted static
  viewer; local avatar/controller/death/respawn had not yet been implemented.

## Phase 4 — local player vertical slice (2026-11-12)

- Added `player.h/cpp`, a game-session backend importing the actual supplied
  character, health and animation assets. Builds the original shoulder/hip/neck
  C0/C1 frames from desktop Humanoid; original animation Lua drives DesiredAngle
  and native Motors advance CurrentAngle at the original time-scaled velocity.
- Creates Players.LocalPlayer and Player.Character with Lua CreateLocalPlayer,
  LoadCharacter, GetPlayerFromCharacter and Humanoid.TakeDamage bindings. Startup
  executes place-script registration before the default Player is added, so the
  real Crossroads leaderboard sees ChildAdded and attaches its death handler.
- Fixed-step 240 Hz OBB character movement, 16 studs/s walking, jump velocity 50,
  gravity 196.2, support detection, step-up, collision response, native Touched,
  external Torso.CFrame teleport handling and humanoid state signals. This is a
  first controller, not every original Humanoid state or constraint.
- Health reaches zero once, emits Died and breaks joints; limbs return to dynamic
  world physics. Respawns a fresh rig after three seconds while retaining Player.
  Local-session close removes Player and clears Players.LocalPlayer.
- Main input now drives movement/jump, orbit/pitch/zoom following camera and
  Square reset. Reload closes player/VM/world before clearing the model.
- Fixed an animation startup race: team-color initialization's wait(1) used to
  postpone connecting humanoid events. Schedule that operation separately in the
  PSP animation asset. Regression now jumps immediately after the rig settles,
  and checks actual Lua-selected shoulder angles before one second has elapsed.
- Added a fast kinematic pose-update path to the physics BVH for independent
  avatar limbs; moving the character must not rebuild every static assembly.

### Verified evidence

- Full host suite passed under ASan/UBSan with leak detection: Lua, bindings,
  hierarchy, world physics, physics/Lua integration, new player checks and 17
  loader cases. Player checks cover original rig rendering, actual animation
  Lua/Motors, immediate jump, arms-up descent, 16 studs/s movement, jump arc,
  landing, Lua player APIs, real kill-brick script, death, respawn and teardown.
- Additional actual Crossroads host run (six seconds, movement and jump): real
  leaderboard loaded; local character alive/grounded at (-311,3.8,477),
  `leaderstats` exists, 1,736 visible parts. Only logged runtime error is the
  already-known unsupported Sound `play` in RocketScript. Host scene accounting
  12.12 MiB is not PSP heap or total process memory.
- Development PBP **1,003,610 bytes**, verified MEMSIZE=1 and relocatable MIPS PRX
  type 0xffa0. Installed only into development emulator with the three assets.
- PPSSPP 1.20.4, 64 MB model: Crossroads player follows analog movement; 30 FPS,
  about **8.92–8.95 MiB live heap**, settled avatar+world physics ~5.5–7.1 ms.
  Original place script prints `DIED EVENT: Player KILLED by no one` on Square
  reset; body breaks apart and a fresh rig respawns at the original spawn.
  Screenshots: `build-psp/screenshots/screenshot-1794491582236-c66901b95706e.png`
  (spawn), `screenshot-1794491617257-bb62ba644c62c.png` (movement),
  `screenshot-1794491634018-39defc516e66.png` (death),
  `screenshot-1794491646110-ca1a9ab39ea55.png` (respawn).
- Reload from Crossroads to PlayerTest drops live heap to about **0.18 MiB**;
  walking into its real Lua kill brick breaks the rig and it respawns. Settled
  PlayerTest reaches 30 FPS. Synthetic fixture is regression evidence, NOT the
  required real obby acceptance.
- No physical PSP test and no Media Engine execution claimed. Camera wall
  avoidance, tools, audio, additional humanoid states/constraint fidelity and
  detached-instance reclamation remain unfinished. Repeated respawns currently
  retain detached nodes: do NOT claim bounded long-session memory.
- Full goal remains active and unachieved; G: release remains the old viewer.

## Phase 5 — lifetime/respawn memory and profiling (2026-11-12)

- Previous goal turn was implementation progress, not a wait or a blocker.
- Added native mark/sweep over hierarchy and Ref graphs, with Lua Instance and
  Signal userdata finalizers retaining/releasing their handles. Detached backups
  remain valid while scripts hold them; unreferenced Ref cycles can be collected.
- Handles encode generation and slot. Reuse cannot make a stale Lua, physics,
  joint or renderer handle resolve to another instance. Generation exhaustion
  retires a slot rather than wrapping identity. Slots remain capped at 16,000.
- LocalPlayer owns its template and live rig explicitly. Retirement disconnects
  obsolete humanoid callbacks even when a persistent place script owns them
  (the real Crossroads leaderboard captures each old humanoid in a closure).
  Lua Remove remains detachment, not an implicit invalidation of Lua-held objects.
- Physics caches index by slot but validate generation; physical slots are reused
  instead of appending bodies indefinitely. Joint change keys include identity
  as well as revision. Renderer checks collected mesh handles without rebuilding
  all geometry when only non-rendered garbage was freed.
- Collect once per simulated second after script-owner cleanup. Added fixed-layout
  `profile.h` snapshot and `tools/ppsspp_check.py`: controller input and measurements
  use the owned emulator's loopback API, not foreground desktop input. The helper
  validates a matching nm map and profile ABI; no private STL layout assumptions.
- Host suite now compiles one consistent adapted engine/VM core and links each
  behavioral check, avoiding repeated full core compilation for every test.

### Verified runs

- First full ASan/UBSan/leak suite passed all engine, player, lifetime and 17
  loader checks. Lifetime checks include hierarchy/Ref roots, unreachable cycles,
  Lua-held backup/signal retention, stale handle safety, renderer/physics cleanup,
  128 character replacements and 12 natural three-second death/respawn cycles.
  Final full suite also passed the joint-generation regression: a collected Weld
  replaced in the same slot with the same property revision must apply its new
  rest frame, rather than being mistaken for the previous joint.
- Crossroads host stress: arena slots stay **3,858 -> 3,858**, live instances
  **3,800 -> 3,797** after releasing the test's saved backup. Scene accounting
  **12,616,048 -> 12,610,960 bytes** and physics **772,292 -> 772,132 bytes** in
  the final run (64-bit host accounting). Only the
  known RocketScript Sound error is allowed; a separate error counter catches
  additional Lua failures, including errors hidden by the bounded text log.
- Profiled development PBP: **1,015,402 bytes**, MEMSIZE=1, MIPS PRX 0xffa0,
  SHA256 `b6f605127f7aec234f0b8e2c73212118d55b60b8d0c6420cf94fa5e021f4ed89`.
- Final joint-identity build: **1,015,482 bytes**, MEMSIZE=1 / MIPS PRX 0xffa0,
  SHA256 `61149a6143b7729f1574d29e6411133862d5db44334b9b5943c5017e5014e1e2`.
  Installed in the development emulator, not the G: release.
- PPSSPP 1.20.4 / 64 MB / D3D11: **20 Square resets passed**, 21 distinct
  character handles, constant Player 3833; all samples have Health=100, grounded,
  runtimeError=0 and no new Lua errors. Live instances **3,797**, arena **3,854**
  at every sample. Live heap **9,308,264–9,312,584 bytes (~8.88 MiB)**, not total
  process/OS memory. Collector **5.136–6.149 ms** once per simulated second.
  Raw versioned evidence: `build-psp/lifetime-emulator.jsonl`.
- Initial native samples alternate between **29.97 FPS / ~7.04 ms physics** and
  **~5.44 FPS / 148.43 ms physics** after resets. Subsequent probing disproved
  the initial interpretation that all low-FPS samples were brief respawn hitches:
  some slot reuse patterns caused persistent full-world rebuilds. See correction
  below. The bounded-memory measurements remain valid; constant-30 is unproven.
- GUI capture from the occluded emulator was unavailable; the memory/lifecycle
  run used the versioned snapshot. Earlier Phase 4 screenshots establish visible
  gameplay. No physical PSP test is claimed.

### Next acceptance work

- Reviewed actual ME interfaces and coordination requirements; see
  `ME-INVESTIGATION.md`. No ME execution/acceleration claimed; main CPU fallback
  remains the only implemented path.
- Fetched an actual archived obby, not a generated fixture: OpenBLOX-Community's
  `2008 - Obstakle course!!!.rbxl`, 21,214,937 bytes,
  SHA256 `cf0ae1af8093aadca156bc75c5c859988cf9502982816ada4d9f65bc9032acd3`.
  Reproduce with `tools/fetch_acceptance.py`; local copy is under
  `build-psp/acceptance`, server copy under `places`. Not bundled by default.
- That map has 5,564 Parts, 998 Scripts, 3,024 Sounds, nine SpawnLocations and
  embedded binary image content. Current loader rejects it at the 256 KiB
  leaf/property limit; the largest binary leaf is ~836 KiB. Ordinary script
  properties were not oversized: only 19 unique script sources, the largest
  23,110 bytes. Extend bounded inline-asset handling and
  inspect subsequent runtime/script budgets; do not truncate source or count
  this rejection as obby gameplay acceptance.
- Audit longer regeneration/event lifetimes and optimize respawn world rebuilds.
  General cross-heap callback cycles outside explicit session retirement still
  need scrutiny. Real obby gameplay, camera avoidance, tool/audio compatibility
  and final G: package remain unfinished. Full goal stays active.

### Correction: reused physics tombstone identity

- Probing the 1,015,482-byte build after five resets found persistent ~5.57 FPS
  and ~223 ms physics, despite stable instance counts. Earlier FPS attribution
  was too narrow. Those builds are development evidence, not release candidates.
- Root cause: a physical slot reused for a non-Part (notably Snap) retained its
  old Body.id. synchronize reset that slot's joint change key every frame, so an
  unchanged joint forced another full-world rebuild. Clearing its active flag
  alone was insufficient.
- Changed inactive cache entries to generation-correct tombstones, clearing old
  kinematic/geometry identity as well. Added a rebuild counter and a behavioral
  invariant after each respawn/collection: an unchanged steady frame must not
  rebuild the world. Full host suite and Crossroads stress now pass, including
  that invariant through 128 replacements and 12 natural deaths.
- Final-key build's five-reset memory run still passed: 3,797 live / 3,854 slots,
  heap 9,324,184–9,327,336 bytes; no new Lua/runtime errors. Evidence is in
  `build-psp/lifetime-final-emulator.jsonl`. That memory success did not detect
  the performance regression; the new steady-rebuild assertion addresses it.
- Corrected build: **1,015,810-byte PBP**, MEMSIZE=1 / MIPS PRX 0xffa0,
  SHA256 `b7d281729c3117c190252cf98723ae7972180ded5e3a339721b62aa6a5402ebf`.
  This supersedes the earlier lifetime-development binaries. Installed only in
  the development emulator; final G: release is still pending gameplay gates.
- Corrected native run: **20 resets with an additional steady check after each**
  passed. All 41 snapshots show 29.9700–29.9701 FPS, ~7.043–7.047 ms physics,
  3,797 live / 3,854 slots, health=100 and the same Player. Heap range
  **9,323,944–9,327,448 bytes**, GC **5.136–5.242 ms**. Fresh character identities
  and no new runtime/Lua errors remain verified. Evidence:
  `build-psp/lifetime-tombstone-emulator.jsonl`.
- Independent six-second steady probe passed **17/17** near-30 samples after the
  reset run (`build-psp/lifetime-fixed-steady.jsonl`). This proves recovery to
  steady performance, not absence of brief synchronous spawn/rebuild stalls.
- Added an explicit native-body-slot-to-Weld regression. The corrected core
  passes it; an isolated host variant reverting only tombstone identity cleanup
  fails with `Part-to-joint tombstone repeatedly invalidates an unchanged joint`.
  The broader respawn loop alone did not force that exact slot pattern on the
  small host fixture, so this targeted case is required. An earlier zero-filled
  emulator probe was not a valid negative control and is excluded from evidence;
  the helper now rejects unloaded state before gameplay/performance checks.
- Obby inspection also counts **10,184 objects / 269,786 properties / 5,536,551
  leaf-text bytes**. The current 200,000 serialized-property limit will need
  attention after inline content loading, within a measured memory budget.

## Phase 6: real obby loading and startup (in progress)

- Raised binary XML leaf limit to 1 MiB (scripts remain 256 KiB), serialized
  property limit to 500,000; retained model limit remains 12 MiB. Compact only
  schema-equivalent defaults, preserve unknown/overridden values, pool identical
  loaded PropertyLists with copy-on-write, and avoid detaching non-Ref lists in
  reference resolution. Added original SurfaceData defaults for all six faces.
- Loader model budget now uses PSP-layout storage estimates (`pspBytes()`), with
  ABI sizes asserted by the native cross build; `bytes()` still measures the
  actual host layout. Neither estimate is total process memory.
- Prime startup scripts in bounded batches without advancing simulation time or
  synthesizing frame signals. This permits the archived obby's many one-shot
  registrations without exceeding the simultaneous coroutine limit.
- ASan/UBSan/leak suite passed, including 19 loader cases, default/COW/reference
  regressions and 1,000 one-shot plus 300 yielding startup scripts. Actual obby
  host movement/jump and falling death followed by fresh respawn passed. This
  was not a checkpoint/script-kill or full-course acceptance pass.
- Archived obby: 5,555 initially visible parts, 68,664 triangles; host Scene
  20.114 MiB, retained PSP-model estimate 11.596 MiB. Native runtime: roughly
  20 FPS, 21,591,048-byte live malloc heap, 10,217 live instances / 13,661 slots,
  health 100, runtime error 0, but 84 Lua errors. Missing Color3/Part.Color,
  Players.NumPlayers, NPC Humanoid:MoveTo and checkpoint behavior remain work;
  referenced Train objects are absent from the archived file itself.
- Crossroads settled native run: 29.97 FPS, 5,556,328-byte live malloc heap,
  physics 8.213 ms, health 100, runtime error 0 and the known Sound Lua error.
- Current development PRX-backed PBP: 1,033,098 bytes, MEMSIZE=1, SHA256
  `81f602e00ccfe4006094453fec45de8ba94c3529a59933af460804030014cfe9`.
  Matching local map is `build-psp/obby-symbols.txt`. Final G: release is still
  pending broader gameplay compatibility; physical PSP remains untested.

### Blank/blurred emulator output investigation

- User-reported output contained only a blurred blue/white background, no HUD.
  CPU and versioned profile confirmed simulation continued. Normal startup of
  the exact same PBP rendered both browser and obby character/HUD correctly.
- Reproduced the exact failure by launching PPSSPP v1.20.4/D3D11 with Windows
  `Start-Process -WindowStyle Minimized`; restoring did not repair the output.
  A normal-window restart did. No unsupported game-side display changes added:
  the SDK's sceGuDispBuffer already initializes LCD mode.
- Added `tools/run_ppsspp.ps1` to launch visibly for future validation. This is
  a workaround for the observed emulator startup failure, not a PSP renderer
  fix. Emulator GPU readback also failed in this environment; use cropped,
  verified window captures for visual checks, not profile-only acceptance.
- Verified normal-launch obby image:
  `build-psp/screenshots/screenshot-1794509184583-322a397869dbd.png`.
