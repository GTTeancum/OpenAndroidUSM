# Reconstruction policy

The objective is maintainable source-level C++, not an ARM compatibility
wrapper. Original symbols are preserved whenever available. Temporary names
such as `field_0x38` are allowed only while a member's semantics remain
unknown; they are replaced with descriptive names when behavior establishes
their purpose.

Every reconstructed function records its original virtual address in a nearby
comment or mapping file until the source database can map it automatically.
Behavior is checked against the original executable using deterministic inputs
and captured state wherever practical.

## Deterministic application harness

`diagnostics::AutoplayHarness` runs inside the normal Windows application
loop with a fixed synthetic clock and the real level loader, cinematic
players, gameplay runtimes, D3D11 renderer, and readback path. It does not
inject OS input. Scenario steps express reconstruction goals (`wait_gameplay`,
`move_to`, `move_to_3d`, `move_input`, `move_until_wall`, `move_until_cinematic`,
`move_until_state`, `wait_enemies_grounded`, `attack`, `jump`, `web_on`,
`web_off`, `teleport`, `start_cinematic`, `wait_death_screen`, `capture`, and
assertions). Audio assertions can require
an event to have played or stopped and can enforce minimum per-event play/stop
counts (`assert_audio_played`, `assert_audio_stopped`,
`assert_audio_play_count`, and `assert_audio_stop_count`)
while directives select the fixed tick, trace/capture cadence, render size, and
maximum run time. `move_input` holds explicit right/forward controller axes for
a deterministic duration, allowing traversal mechanics such as wall climbing
to be isolated without OS-level input automation. `move_until_wall` steers
toward a world-space target until the player reports native wall attachment.
`move_until_cinematic` proves that movement crossed the intended authored
trigger instead of merely reaching a coordinate. `start_time_ms` fast-forwards
persistent intro world commands for focused probes, reconstructs any tutorial
message whose authored timer still overlaps the destination, and suppresses
expired historical messages and audio that should not be replayed there. The
harness acknowledges an active tutorial just as it already
acknowledges an active QTE, preventing an infinite tutorial timer from masking
the gameplay state under test; normal user input remains unchanged.
`wait_cinematic_started` waits on the complete historical start set rather
than only the current active set, so short zero-time handoffs cannot escape a
probe between fixed ticks.

`move_to` deliberately checks horizontal proximity only. Traversal landings
use `move_to_3d` or an explicit `assert_near` plus landing-state check: both
horizontal distance and absolute height error must fit the supplied radius.
The 3D driver releases movement once horizontally aligned while waiting for
the real player physics to land; it never writes the player's position.
Movement timeouts include the actual coordinates, requested target, and
player state so a missed landing cannot be reported as route completion.

The dedicated Level 1 intro player follows the same native movie-UI teardown
as scheduler-owned cinematics before starting wrapper 1266. This includes
fast-forwarded diagnostic starts; replaying PlayDAECamera without its final
UnUse must not leave the HUD and controls disabled. The
`opening-gameplay-handoff` regression explicitly checks this boundary.

Every sampled frame records player pose, health, animation clock, state,
on-wall status, accepted input axes, gameplay camera area and pose, cinematic
ownership, death-screen state/alpha, restore/QTE state, and visible rooms. A
companion room-motion table records each room's object ID, movement gate,
waypoint link and current target, translated position, and native velocity. A companion enemy table
records every mutable enemy state. The event stream records script steps,
wall attachment/detachment, trigger and cinematic starts,
every dispatched cinematic command with its source cinematic ID, target
thread object, authored timestamp, name, and attributes, player impacts, state
transitions, audio requests, teleports, and captures. `enemies.csv` also records
each native collision cylinder, vertical velocity, and grounded state, while
`cinematics.csv` provides a complete static census of every loaded thread,
command, timestamp, and typed attribute. `collision-surfaces.csv` records each
room collision instance, native surface class, and world bounds;
`collision-triangles.csv` expands every face with its native physics flag,
world vertices, and normal. `player-states.csv` records the complete loaded
MC_STATE census, including class, motion type, parameters, animation IDs,
timings, and next state. Every audio callback records its resolved Vox event
name, action, loop flag, spatial flag, and effective playback volume; the
harness maintains per-event counts for end-to-end assertions instead of
treating a decoded resource as proof that gameplay dispatched it. Trigger/enemy
asset events include their complete authored transforms and spawn flags.
Object asset events likewise retain kind, room ownership, mesh, visibility,
collision flag, and full authored transform.
The underlying main/room scene-node census is logged separately so a
mesh-bearing node omitted from a typed runtime cannot disappear silently.
Camera-area asset events include each neighbor and switch time plus the
area's follow rate and all four control-point positions, directions,
distances, target offsets, and height offsets. Scenario completion and
failures are machine-readable in `summary.txt`. Explicit capture steps and
periodic D3D11 readbacks make visual regressions timestamp-addressable; review
still examines those frames in chronological order.

The current Release gate executes all 67 tracked scenarios, comprising 1549
explicit steps. Each run preserves its CSV/summary artifacts; intentional
readbacks are deleted after inspection by the default suite runner. The
readbacks cover intro
transforms and lighting, enemy animation, combat, both opening encounters, the
uninterrupted post-crash route through the 36.9-second level ending, wall
traversal, native player death through confirmation readiness,
and the separate complete 55-second campaign-end presentation.
They also cover the Level 2 bank opening, both tested bank-floor encounter
cohorts, and the first controllable Level 3 rooftop frame after its two-part
opening. Readbacks are removed after comparison so diagnostic screenshots do
not accumulate in the repository workspace. The Level 1 portion of the suite
executes all 386 unique command sites across the 34 state-reachable first-level
cinematics.

Diagnostic teleports intentionally bypass route traversal but keep gameplay,
combat, scripts, animation, and rendering live. They also relocalize the
gameplay camera across the complete authored area set because the native
neighbor-only camera transition is correct only for continuous movement. This
keeps route/navigation reconstruction and encounter-state reconstruction as
separate experiments.

`tools/ghidra/ExportSelectedDisassembly.java` complements the selected
decompilation exporter with address-stamped ARM instruction listings and the
adjacent literal pool. It is used when the decompiler elides constants or ABI
arguments that are material to a reviewed reconstruction. Its instruction
walk follows the complete function body rather than stopping at the first
non-contiguous address range, which is required for compiler-generated switch
islands in the player motion functions.

## Naming order

1. Original ELF symbol name
2. Original source-file association
3. Vtable slot and call-graph evidence
4. String and asset references
5. Observed runtime behavior

Names inferred rather than preserved from the binary are documented as such.

## GBMP archives

The shipped `.pack` files use normal ZIP local headers with the four-byte local
signature replaced by `GBMP` (`0x504D4247` as little-endian data). The remaining
header layout, raw Deflate streams, CRC-32 values, central directory, and end
record are ZIP-compatible. This behavior is preserved in the original
`irr::io::CZipReader::scanLocalHeader` at original address `0x00429124` (Ghidra
image address `0x00439124`). `filesystem::GbmpArchive` is the native C++ reader.

Observed shipped archives use flags `0`, compression method `8`, and include
sizes in every local header. The reader intentionally rejects encryption and
data descriptors until an actual resource requires those variants.

## BRES/BDAE resources

The `.bdae` meshes are `BRES` resource files. Their 32-byte header identifies
the byte order, complete file size, and a table containing the file offsets of
every serialized pointer field. `irr::res::File::Init` rebases those 32-bit
offsets in place on ARM. `assets::BresFile` instead records field-to-target
offset mappings without mutating the input, which is safe on x64 and keeps the
serialized layout available for validation.

## Irrlicht scenes

Level and room `.irr` resources are UTF-16 XML. The main scene has one
`irr_scene` root; room resources intentionally contain a sequence of top-level
`node` fragments. `assets::IrrScene` accepts both layouts and recovers stable
node IDs, hierarchy, transforms, visibility, semantic game type, and referenced
mesh path. Level 1 currently validates as 154 main-scene nodes and 60 nodes in
Room 1.

The persistent player is main-scene object 288, named `SpiderMan`. Its typed
scene metadata selects `spiderman_mesh.bdae`, `spiderman_anim.bdae`, initial
clip `kick_left_double_kick`, collision, initial camera area 283, linked intro
cinematic 1265, and campaign-end cinematic 1267. `LevelPlayerAsset` loads those
references from the scene rather than hard-coding parallel asset choices.
The level's `CameraArea` and `CamCtrlPoint` records are likewise typed. Each
area names four polygon control points; each point supplies its camera
direction, distance, target offset, and target-height offset. Thirty areas
live in the main scene and fourteen more live in linked room scenes. The
bootstrap resolves all 44 as one graph and converts room-local control points
through their serialized absolute transforms; this preserves the authored
route from initial area 283 through final boss area 10100.

`game::GameplayCamera` reconstructs `CCameraArea::ComputeAverageValues`
(original `0x002e4ec4`, image `0x002f4ec4`): it projects the player onto the
control plane, applies the area's `zFollowRate` to the off-plane displacement
as recovered from `CCameraArea::Update` at `0x002f539c`, blends the polygon
edges by reciprocal distance, distributes those weights to their endpoints,
and normalizes the blended direction. Area containment follows
`CCameraArea::ProcessAttr`/`IsPlayerIn` (`0x002f4aa4`/`0x002f3d70`): the
authored height is squared before comparison with squared plane distance, so
the game's intentional negative heights (including Level 1 area 339) remain
valid. It
then follows `CGameCamera::Update` (original `0x002e327c`) by placing the
camera at `target - direction * distance` and adding the preserved 120-unit
vertical target offset. The initial area 283 pose is regression-tested from
Spider-Man's serialized level-one start position.

## Level-selectable bootstrap and Level 2 bank milestone

The application accepts a level number from 1 through 12 and passes it through
the source-level bootstrap instead of selecting a parallel hard-coded asset
set. Level 2 is the second concrete bring-up target. Its active graph links ten
rooms (`Room14` through `Room23`), 49 camera areas, 40 cinematics, 44 enemies,
136 ordinary objects, 18 triggers, and 36 bonuses. The persistent player is
again scene object 288, initially at
`(-20654.707031, 9150.440430, 5.642347)` in camera area 10110. Unlike Level 1,
the shipped Level 2 main scene does not link an intro cinematic, so the
application begins directly in gameplay rather than inventing an opening
script.

The generic loader preserves several shipped irregularities needed by this
level. A mesh is required to decode only images actually referenced by its
buffers, not every stale entry in an editor material library; this permits
`break_wall_anim.bdae`, whose library advertises 51 images that its geometry
does not all bind. A malformed trailing apostrophe is removed from the Room 18
car animation path. Static BDAE content authored with the `AnimationFile`
semantic is accepted when it contains no animation library, and stale numeric
node animation references are cleared without discarding real CFF cinematic
actor streams. Finally, the Level 1 pack is mounted as an inherited fallback
because a Level 2 animated bank asset references `24_build.tga` from that
shared archive. These are asset-resolution rules derived from the shipped
graph, not replacement content.

Eleven Level 2 actors use the previously unhandled `RangeThug_molotov` game
type. They are now retained as enemy type 2 rather than disappearing into the
scenery path, which raises the loaded Level 2 enemy census from 30 to the
authored 41 ordinary actors and makes their death watchers reachable. Three
retained cinematic Rhino actors bring the complete runtime census to 44. Their data chain is also
typed: `THUG_MOLOTOV` range-map index 10 resolves to weapon type 5; attack
interval row 7 (`ENEMY_RANGE_ATTACK_BROKEN_BOTTLE`) supplies the 2000 ms
enemy-type-2 interval; range config map 7 supplies `RANGE_ATTACK_01`, a 1000 ms
animation window and 50 damage; and special-action record 11 fires at 50% of
`idle_throw_molotov_idle` with sound map 18. The original
`CBehaviorRangeAttack::ThrowMolotov` obtains a flame-grenade `CThrowObject`
pool and throws toward the captured player position. The portable path now
follows that recovered chain. `InitWeaponParent` at `0x003c0144` parents the
bottle to the animated `Bip01_R_Hand` node; `GetRangeWeapon` at `0x003c09cc`
allocates weapon type 5 from the flame-grenade pool and applies the configured
50-damage hit record; and `InitPoolProp` at `0x003592cc` maps it to
`CThrowObject` subtype 1. This distinction matters: `Throw` at `0x00359af4`
uses the subtype-1 1000 cm/s branch, a 0.5 second minimum flight time, and a
50 ms initial horizontal advance. Its recovered vertical acceleration is
`-2 * (abs(targetZ - originZ) + playerHeight) / flightTime^2`. The renderer
uses the shipped `w_flamegrenade.bdae` mesh and its `fly`/`explode_ready`
clips. On ground contact the 1333 ms `explode_ready` clip runs before
`molotov_bomb` is emitted and the original 250 cm proximity damage test is
performed. `level2-molotov-projectile-probe.usmauto` waits for an actual
projectile from authored enemy 642 and preserves its spawn/contact/ground/
explosion telemetry without sending host input.

The Level 2 chase scripts also exercise native command families that do not
occur in Level 1. `CCinematicThread::StartProgress` at `0x0037145c` accepts
only a Basic thread, resolves `ID_BOSS` and the `^SID^WayPoint`/
`^EID^WayPoint` pair, and initializes `CProgressBar`. Its `Init` at
`0x0031a7b0` flattens Z and makes the failure separation exactly 88 percent of
the two authored endpoint distance. `Update` at `0x0031a858` measures flat
player-to-boss distance; after `RhinoStop` (`0x0036fdf0`) it contracts the
allowed separation by 0.9 cm per millisecond and enters black-screen mode 1
when the boss escapes. `StopProgress` at `0x0036fdcc` clears the visible and
failed bytes while retaining the remaining state. `Draw2D` at `0x0031a960`
uses interface frames `0x44` and `0x45` at `(300,25)` and moves the marker by
`44 - trunc(88 * currentDistance / failureDistance)` on the native 480x320
canvas. The portable runtime follows those values, routes failure through the
native ten-millisecond confirmation fade, logs the complete progress state,
and has a WARP readback regression for the two HUD frames.

`CCinematicThread::OnRestore` at `0x0036fe0c` is also kept literal. The
`CLevel+0x60[current-player]` lookup resolves the active player—not a room—and
the handler writes `-1.0f` to `Unit+0x64`, the health field read by
`Player::IsDead` at `0x00340084`. The next player update performs the normal
`Player::CheckDeath` state-`0x80` transition. The focused Level 2 autoplay
trace observes the shipped cinematic 20053 command change health from 1000 to
-1 at 1800 ms and reach confirmation at 3800 ms.

The adjacent external-action commands are dispatched from their shipped CFF
attributes as well. `ThrowingSomething` at `0x00371364` deliberately reads the
misspelled `EnmeyID`, sends CBoss local message `0x59`, and carries
`ObjectID`; `StopAction` at `0x003713f4` sends `0x5e`.
`CBoss::ParseLocalAiMessage` at `0x0032dea8` selects behavior `0x12e`
(`CBehaviorPickUp`), enters its external state, and later clears it. The
portable enemy trace currently preserves that exact activation edge and
related object ID. Reconstructing the behavior's cloned `CThrowObject`
attachment/flight is tracked separately rather than pretending that the
message flag alone is the complete throw behavior.

`level2-opening.usmauto` verifies the direct gameplay bootstrap and native
readback. `level2-bank-combat.usmauto` exercises the first two authored enemy
gates: it destroys the four lower-floor actors and observes cinematic 654,
then uses an explicit diagnostic teleport to isolate the upper-floor cohort
and observes cinematic 20042. `level2-progress-command-probe.usmauto` proves
the shipped `Throwing -> StopAction -> StartProgress` order and HUD state;
`level2-scripted-restore-probe.usmauto` proves the authored health/death path.
The probes keep room rendering, enemy animation,
combat state, baked-lightmap materials, and cinematic dispatch live, but it is
not a claim of continuous traversal or complete Level 2 playability.

## Level 3 rooftop progression milestone

Level 3 is the next direct bootstrap target. Its scene links ten rooms
(`Room1` through `Room10`), 58 camera areas, 28 cinematics, 46 enemies, 103
ordinary objects, 12 triggers, 109 bonuses, 18 restore volumes, and seven
slides. Spider-Man is object 30348 at
`(-15118.238281, -5233.077148, 1390.249634)`, initially assigned to camera
area 30039 and linked opening cinematic 31110. Room 3 deliberately ships a
valid collision BDAE with no geometry records. The bootstrap now distinguishes
that authored empty mesh from a missing or malformed resource instead of
requiring every presentation shell to contain collision triangles.

The rooftop electric-beam asset exposed a separate BDAE layout that earlier
levels did not exercise. `electro_beam.bdae` is both its mesh and animation
file: it contains seven animation records and three clips (`fall`, `keep`, and
`release`). Four UV-offset animation records each contain two parallel
sampler/channel pairs, yielding eleven decoded channels in total. The
serialized sampler and channel structures are 20 and 16 bytes respectively,
not the shorter prefixes initially sufficient for one-channel skeleton data.
The typed animation reader now walks every pair, and channel type 10 is
preserved as a three-component scale track and applied during rigid or skinned
pose evaluation. The preserved `CColladaDatabase::getAnimationTrackEx` maps
channel types `0x101` through `0x105` to `CTextureTransformEx`.
Cross-checking asset names establishes `0x101` as texture-U offset and `0x102`
as texture-V offset: `aircraft_blue.bdae` names its `0x101` record `offsetU`,
while `electric_body_big.bdae` names its `0x102` record `offsetV`. D3D11 now
samples these scalar tracks on the geometry timeline, resolves their effect
targets to draw batches, and applies the animated per-material UV translation
in the vertex shader. The beam's four phase-shifted V sequences therefore
animate instead of remaining fixed on their first texture cells.

The five opening instances are the shipped animated objects 30946, 30949,
30950, 30951, and 30969. Their CFF nodes are initially hidden, use `fall`,
carry `AddColor=true`, and preserve their complete absolute transforms,
including the authored 1.48272 Z scale. This is not a visual approximation:
`CAnimatedObject::ProcessUserAttr` at `0x002fd560` reads `AddColor` and calls
`SetMaterialType(node, 0x0d)`, while
`CCommonGLMaterialRenderer_TRANSPARENT_ADD_COLOR::onSetMaterial` at
`0x004559b0` installs `GL_SRC_ALPHA, GL_ONE`. Applying only the embedded BDAE
alpha-channel material instead produces opaque black shafts because the
shipped lightning texture itself is fully opaque. The autoplay asset trace now
writes `object-assets.csv` with each CFF position, quaternion, scale, complete
4x4 absolute matrix, visibility, collision, `AddColor`, and initial animation;
the core gate fixes all five IDs and authored transforms.

The beam command routing also follows `CCinematicThread::SetAnim` at
`0x00372468`. Basic threads (type 1) resolve the command's `ObjectID`; object
threads animate their bound object and deliberately ignore that field. The
30408 script contains cross-ID records on beam threads 30950 and 30946, so
unconditionally resolving `ObjectID` left 30950 stuck in `fall` and restarted
30946 twice. The runtime now preserves the native distinction and the core
gate fixes both sides of that routing rule.

An unresolved animation name is not a command-stream error. Direct RE of
`AnimationProxy::GetAnimIdByName` (`0x0038e9b0`) shows that it returns `-1`;
`IAnimatedObject::SetAnim` (`0x00310fec`) checks that sentinel and leaves the
current animation untouched. Level 3 arrival cinematic 31110 exposes this
with the stale names `swing_idle_to_heroic_fall_idle` and
`heroic_fall_idle`, neither of which exists among the 242 shipped player
clips. The portable player, enemy, and object paths now preserve the native
no-op and continue dispatching later commands in the same tick. Autoplay logs
the complete player clip census and emits `cinematic_animation_unresolved`
with `native_result=no_op`, keeping the stale authoring visible without
changing its behavior.

Unlike Level 1's dedicated Collada-camera 1264/1265/1266 boot wrapper, the
Level 3 opening is an ordinary CFF graph authored through camera-thread
`ChangeCamera` commands. The application now seeds the Spider-Man node's
linked cinematic through the normal scheduler. Cinematic 31110 runs the
arrival and hands off at 11050 ms to Electro presentation 30408, which restores
controls at 20350 ms and starts encounter monitor 30407. This keeps the
authored player transforms, room visibility, interface lock, dialogue, sounds,
electric-beam object animation, and subsequent conditional monitor in one
runtime path.

Those player transforms now use the same timed native path as other bound
cinematic objects. `CCinematicThread::MoveObject` (`0x00370494`) stores the
current absolute position/quaternion and the next authored key, while
`CCinematicThread::DoExecChange` (`0x00371880`) evaluates the existing clock,
linearly blends position, quaternion-slerps rotation, and only then adds the
frame delta. The arrival trace therefore advances continuously through the
0/750/1050/1250 ms descent keys and the 5250/9050 ms walk keys. The focused
`level3-player-moveobject-probe.usmauto` records motion active/elapsed/duration
in `frames.csv`, exact per-key state as `player_move_object` events, and no
screen captures.

`InterfaceControl` is retained as frame telemetry as well as command telemetry.
`AttributionEnable` feeds the native `CLevel+0x2d` gate recovered from
`CCinematicThread::InterfaceControlCmd` (`0x003711e0`) and
`CLevel::Render2DInterface` (`0x00387a54`); disabling it clears the complete
gameplay HUD while leaving cinematic subtitles active. Runtime rebinding
restores all five interface defaults, and the WARP gate verifies that a HUD
draw can be removed without leaving stale vertices on the next frame.

`level3-opening.usmauto` requires both cinematic starts, waits for controls to
return, captures the first controllable rooftop frame, and verifies live
health. The core gate also fixes the complete asset census, the intentional
empty collision room, and all eleven electric-beam channels; WARP renders a
non-black, chromatic opening frame. This remains an opening-presentation
milestone, not a complete Level 3 traversal or combat acceptance claim.

The first playable encounter is now covered beyond that presentation gate.
Electro presentation 30408 issues `SetVisible` for enemies 30314, 31305, and
31306 from its basic thread, whose object ID is `-1`. The portable enemy
dispatcher previously ignored the command's explicit `ObjectID`, leaving all
three actors active but invisible. `CCinematicThread::SetVisible` at
`0x00371e9c` resolves a nonnegative `ObjectID` before falling back to the
thread owner; the runtime now follows that exact rule. The
`level3-first-encounter.usmauto` gate follows the shipped 31110 -> 30408 ->
30407 graph, requires all three actors to become visible, physics-active, and
grounded, defeats them through production combat, and verifies 30407's
authored `close`/hide sequence on barrier 30405 in the retained trace.
The asset trace also writes `trigger-assets.csv`, preserving each CFF trigger's
room, enabled/auto-disable state, box mode, four cinematic links, transform,
size, and complete absolute matrix. `level3-second-encounter.usmauto` uses
those records to follow trigger 31291 -> cinematic 31292 and trigger 30412 ->
30413 -> 30414. It requires the four room-4 actors to activate and ground,
executes production melee on their authored lower floor (the trigger also
spans the upper bridge), and automatically asserts barrier 30411's `close`
animation and hidden terminal state.

The next retained production-combat gates extend that route through the third
encounter. `level3-third-encounter.usmauto` follows trigger 40192 into monitor
40199, then trigger 40488 into the second reveal, waits for enemies 40024,
31309, 31310, and 40279 to settle onto the collision support recovered from
the shipped room, defeats all four through normal player attacks, and verifies
barrier 40191's authored `close`/hide result. This test deliberately stages the
two collision tiers separately; it does not infer a common floor from a
readback.

`level3-wave-progression.usmauto` and
`level3-full-progression.usmauto` trace the remaining shipped cinematic graph
with scoped diagnostic enemy damage while leaving the real cinematic,
conditional-monitor, object, collision, and terminal-level runtimes active.
The full route covers the three-stage room-7 wave, triggers 30898 and 30943,
the room-8 barrier 40654, both room-10 enemy cohorts, bosses 40317/40318, and
end cinematic 0. Its final authored command is `LevelEnd(GoToNext=true)` at
local cinematic time 11000 ms. The harness now exposes `wait_level_end`, logs
`level_ended` and `game_ended` in every sampled frame, and records their edge
transitions. The retained full-route trace proves the command at real time
49750 ms and keeps `level_ended=1` through harness completion. Diagnostic
damage makes this a graph-completeness gate, not a claim that the later waves
have all passed production-combat acceptance.

## Native gameplay and combat

`GameplayPlayer` implements renderer-independent ground movement, collision,
camera-relative controller input, named punch clips, authored impact timing,
and health. `LevelEnemyRuntime` owns the mutable state of all 34 level-one
enemies. Cinematic `DisableAI`, `EnableAI`, `SetVisible`, `SetAnim`, and
`MoveObject` commands feed that state directly; enabled enemies acquire the
player using their scene-authored awareness radius and chase at the authored
line speed.

Enemy animation commands preserve all authored playback fields.
`CCinematicThread::SetAnim` at `0x00372468` routes the clip, speed, loop, and
reverse values into `IAnimatedObject::SetAnimWithSpeed` at `0x0031104c`; the
latter sets the animation scale and jumps reverse playback to the clip end.
`LevelEnemyRuntime` mirrors that state explicitly, advances reverse clips
toward zero (wrapping only when authored to loop), and clamps ordinary
non-looping clips in the renderer. The deterministic enemy trace records the
clip clock, speed, loop, and reverse state. A short 200 ms capture probe avoids
phase-locking to the 2.5-second thug idle loop, while a core pose regression
proves that the decoded knife-thug mesh changes from its first sampled frame.

`LevelCinematicRuntime` handles the global encounter state without coupling
gameplay to Win32 or D3D. `DisableTrigger` and `EnableTrigger` follow
`CCinematicThread` at `0x0036fef0`/`0x0036ff38` and reset the native trigger
volume state through `CTrigger::SetEnabled`. `EnableCameraArea` and
`SetCameraArea` follow `0x00371990`/`0x00371a28`; they update the runtime copy
of the 44-area graph and preserve the original's successful no-op when an
area lookup fails. That behavior matters for Room 9 cinematic 30003, whose
shipped script contains a stale reference to nonexistent area 224.
`StartCinematic`, `GameEnd`, and `LevelEnd` preserve their named state and
attributes. All seven level-one `StartCinematic` commands occur at their
parent script's final timestamp, so the application queues each authored
handoff deterministically. The zero-time intro-start script now also disables
trigger 1263 before explicit cinematic 1265 playback, preventing the opening
sequence from retriggering during gameplay.

The remaining control-only commands in the linked-room script census are
also explicit. `Save` retains the authored checkpoint ID following
`CCinematicThread::SaveCheckpoint` (`0x00370384`); disk profile persistence is
outside this single-level target. `Unlock` preserves the two profile skill
bits used for ultimate and spider sense. `StartTimer` follows
`CCinematicThread::StartTimerOfBossRush` (`0x0036fe58`), whose only native
effect is releasing the final encounter's timing gate, and terminal
`Transport` records the game-over return request before the Windows target
exits. None of those three control flags directly emits graphics or audio.

Encounter conditions retain the per-thread blocking behavior recovered from
`CCinematicThread::executeCommand` at `0x00372aa4`. A false `IfEnemyDead`
leaves that command pending and suppresses only later commands in the same
cinematic thread; unrelated threads continue. The condition is retried on
subsequent updates and releases the authored trigger/cinematic chain only
after the referenced native enemy state is dead. This is materially different
from flattening every time-zero command into an unconditional batch.
`GameplayCinematicScheduler` also preserves the native manager's concurrent
list semantics recovered from `CCinematicManager::AddCinematic` (original
`0x0035f9bc`, image `0x0036f9bc`): it rejects a duplicate active asset but
does not replace unrelated active cinematics. This allows an encounter's
presentation script and its zero-time `IfEnemyDead` monitor to advance
simultaneously. Completed ordinary scripts are removed, while a terminal
presentation may retain its final authored frame.
`KillObject` and `IfObjectDestroyed` share the same native death transition
for enemy-backed objects: health reaches zero, AI stops, and the authored
one-shot death animation and behavior sound are selected. This covers the
enemy destruction gates used throughout the level-one encounter chain.

Enemy melee timing is not guessed. The typed
`EnemySpecialActionConfigDatabase` follows
`EnemyAttributeFile::ReadAnimSpeciaActionInfo` at `0x0033b9d8` and decodes all
230 records in `EnemysSpecialAnimConfigs.bin`. Knife thugs attach attack ID 6
to 45% and 75% of `idle_knife_at_idle`. Bat behavior state 11 exposes two
equally ranked lists: attack ID 7 at 47% of `idle_at1_idle`, and attack ID 11
at 70% of `idle_jump_at3_idle`. `CBehaviorMeleeAttack::StateEnter` at
`0x003baef8` ranks candidates by the absolute delta between target distance
and `EnemyAttackInfo+0x38`; its tie branch at `0x003bb244`-`0x003bb28c`
replaces the current winner only when `random(0,100) <= 49`. The portable
runtime now performs that comparison and uses the original Irrlicht integer
generator instead of an invented alternating selector. Those IDs resolve through
`EnemysAttackConfigs.bin` to the authored damage, hit-box reach, angular
sector, hit type, protection interval, and force fields. Core regressions pin
the knife's two 25-point contacts, the bat's 35-point standing contact, and
its 50-point jumping contact.

The melee-engagement handoff is likewise native-backed. Difficulty one sets
one melee and one ranged slot plus a 1000 ms base at
`CAIEntityManager::ResetMaxMeleeEngagingEntities` (`0x003744a4`).
`UnRegisterEntityForMeleeAttack` (`0x00375560`) replaces the gate with
`random(1000,2000)`, while `CanRegisterEntityForMeleeAttack` (`0x00375654`)
accepts the sole free slot only after that float timer is non-positive.
Registration at `0x00375710` also consumes and retains a
`random(5000,15000)` entry lease. The previous fixed 1000 ms handoff and
last-attacker round-robin rule had no executable counterpart and are removed.
`irr::os::Randomizer::rand` (`0x0043ae48`) starts from `0x0f0f0f0f` and uses
the recovered 40692/52774/3791/2147483399 Schrage recurrence; core tests pin
its exact integer sequence and half-open wrapper ranges. Player combat,
player state audio, and enemy behavior now consume one application-owned
instance, matching the native process-global stream instead of maintaining
independent subsystem or per-actor cursors.

Each opening attack also has an action-type-2 record at 2% of its clip.
`IBehaviorBase::SpecialAnimActionCheck` sends message `0x66` there, and
`CBehaviorMeleeAttack::onMessage` at `0x003ba670` registers Spider-Sense only
after the selected attack volume intersects the player. The runtime therefore
does not expose a counter merely because a wind-up animation has begun.

The same special-action path now drives the heavier level-one actors rather
than making them harmless chase targets. Type 4 selects
`idlebaz_rush_attack_idlebaz` and four attack-21 impacts (70 damage, 500 cm
reach); type 5 selects `idle_attack_hammer_idle` and attack 19 (50 damage,
300 cm); Sandman type 16 selects `ground_attack1` and attack 69 (75 damage,
400 cm). Their key percentages, sectors, damage, and reach come from the same
typed binary tables as the knife/bat attacks.

`EnemyRangeAttackConfigDatabase` follows
`EnemyAttributeFile::ReadEnemyRangeAttackInfo` at `0x0033b820` and decodes all
17 `EnemysRangeAttackConfigs.bin` records. Each record preserves its exported
ID and name plus the 16-byte runtime payload: attack-type map ID, animation
duration in milliseconds, projectile speed in centimeters per second, and
damage. `CBehaviorRangeAttack::StateEnter` at `0x003c143c` consumes the
duration and `UpdateAttack` at `0x003c20e0` consumes the damage; representative
map IDs and values are regression-tested from the shipped archive.

The weapon-selection chain is also data-driven. `EnemyAttributeConfigDatabase`
follows the ranged-attack vector embedded by `ReadAttributeInfo` at
`0x0033bf00`; enemy type 3 (`THUG_GUN`) authors map index 4. The recovered
26-entry table used by `CEnemy::InitEntityAttribute` at `0x003373e8` resolves
that index to weapon type 13, and `EnemyAttackIntervalConfigDatabase` decodes
the 24 rows and 25 enemy-type columns in `AttackIntervalTimeConfigs.bin`.
Weapon type 13 selects row 8, `ENEMY_RANGE_ATTACK_GUN_LINE`, with a 2000 ms
type-3 interval and ranged-config map 8 (1000 ms animation, 30 damage).

`LevelEnemyRuntime` uses that chain to alternate the authored
`idle_shoot_left_idle` and `idle_shoot_right_idle` clips. Their 50% special
action emits sound map 18 and creates a portable `EnemyGunLineState` rather
than an ARM object. Motion and lifetime use the recovered `CGunLine::Update`
constants (1500 cm/s and two seconds); each swept segment checks level
occlusion and the player before applying the configured damage. D3D11 draws a
depth-tested translucent tracer from this backend-independent state, and WARP
exercises the dynamic gun-line buffer.

`EnemyBehaviorConfigDatabase` reconstructs the four tables used by
`BehaviorStateFile`: 239 rows from `BehaviorAnimMapList.bin`, 202 animation
lists from `BehaviorAnimList.bin`, 63 sound maps from
`BehaviorSoundMapList.bin`, and 221 states from `BehaviorState.bin`. The
layouts follow `ReadAnimList` (`0x0033a2b0`), `ReadStateInfoList`
(`0x0033a3d8`), `ReadSoundMapInfos` (`0x0033a868`), and the original 25 enemy
type columns. This establishes that state 49,
`ENEMY_BEHAVIOR_HURT_STATE_COMMON`, randomly selects the three authored hurt
maps and sound rows 12--14, while state 71, `ENEMY_BEHAVIOR_DEAD_STATE`, uses
the `idle_onground` animation and sound row 15. Type columns zero and one map
those rows to the exact knife/bat Vox IDs 185--192.

The vector formerly labeled as a special action's successor list is now
identified as its behavior sound-map list: `IBehaviorBase::SpecialAnimActionCheck`
at `0x003a8c60` resolves and plays one entry when the authored animation key
frame is crossed. Knife and bat attack records both select sound map 16, which
resolves to Vox ID 178, `SFX_THUG_SWOOSH`. The native runtime queues these
key-frame cues even when an attack misses and selects exactly one cue through
`random(0,count)` at `0x003a8d84`--`0x003a8da0`. State animation mode 2 uses
the same bound at `IBehaviorBase::ParseAnimInfo`
(`0x003a8648`--`0x003a866a`), and both `SetState` overloads select state
voices through `random(0,count)` at `0x003a8a60`--`0x003a8a7a` and
`0x003a8b34`--`0x003a8b50`. These calls consume the global generator even
when the list has one entry; the former round-robin hurt and sound cursors
have been removed. Hurt and death
animations are one-shot states; D3D11 clamps them at the final authored pose
instead of wrapping. `EnemyBehaviorSoundBank` predecodes all 32 sounds used
by level-one enemy types 0, 1, 3, 4, 5, and 16 before gameplay. WARP captures
cover a mid-hurt pose and the
final prone death pose.

Player combat audio is likewise state-driven. `PlayerStateConfigDatabase`
follows `StateFile::ReadBasicState` (`0x0033d294`) and
`StateFile::ReadSoundConfig` (`0x0033d5f8`) to decode all 131 `MC_STATE.bin`
states and 38 `MC_SOUND.bin` configurations. The recovered
`k_state_idle_to_punch_right` record requests
`k_mc_sfx_swoosh_punch_lag` on entry, but `Player::PlaySound` at `0x0034905c`
retains it because its authored emitter is frame 2. `Player::UpdateSound` at
`0x003429d4` passes that emitter through `Player::CheckFrame`, placing the
swoosh 25 ms into the move. The separate `k_mc_sfx_punch_impact`
configuration is invoked only after the frame-7 hit is accepted, so it plays
at the 175 ms damage event and never on a miss. Those configurations select
Vox IDs 60/61 and 58/59. Type-1 multi-emitter configurations pair each frame
with the Vox ID at the same index; type-0 ranges retain deterministic variant
selection from the native global stream. `PlayerSFX` delegates those ranges
to `VoxSoundManager::Play2DRandom`/`Play3DRandom`; the exact inclusive range
and `random(count)` call are visible at `0x003dada0` and `0x003dafa8`.
`Player::CleanSound`/`StopSound` are mirrored on state
transitions, including termination of the looping ultimate-wheel cue.
`Player::OnHit` at `0x0034d790` maps the knife's hit type 100 to
`k_state_hurt_light` and both bat attacks' hit type 101 to
`k_state_hurt_heavy`. Their recovered Dummy root tracks move approximately
16.84 and 200.43 cm backward. Both states dispatch their authored entry audio,
and the ground hurt update now commits their physical and render displacement
instead of showing every contact as the same stationary light reaction.
`PlayerStateSoundBank` predecodes every referenced sound configuration and
dispatches it from the native gameplay state rather than filename heuristics.

Ground melee now follows that same state table instead of a local two-clip
approximation. `Player::UpdateKeyTrigger` (`0x0034d0a4`) reads each state's
button, predicate, and target-state triples only inside the two authored input
window frames. `Player::UpdateAttackParam` (`0x00340fa0`) advances the state's
hit-frame list, `Player::CheckAttackTarget` (`0x0034fca0`) consumes the four
motion parameters as damage, reach, and minimum/maximum angles, and
`Player::UpdateAttacks` (`0x00351204`) follows `nextStateId` when the current
clip ends. The normal-suit Square graph is therefore the shipped
`74 -> 75 -> 79 -> 90 -> 91 -> 78` chain. A lone state-74 punch hits at frame 7
and returns directly to idle after its 333 ms clip; it no longer enters the
unrelated 466 ms `punch_right_to_idle` clip. Repeated presses can chain after
the native frame-3 gate, and every linked state emits its own hit and sound
frames. Autoplay traces record the active state ID/name and wait until all
impacts have fired before issuing the next deterministic combo request.

Ground and airborne web attacks use the original state selection instead of
sharing the traversal-only swing action. `Player::GetGroundWebSpecialState`
(`0x00343f48`) selects the ground web graph beginning at state 58; its pellet
impact deals the authored 20 damage to the selected runtime enemy. A held Web
input continues to state 60, whose no-input successor state 62 sends an
entry-time zero-damage binding hit; state 62's own serialized 20 is not a
second pellet. A fresh movement direction is native virtual event 0 and routes
state 60 to state 64, whose motion-129 completion delivers 150. Further Web
presses select the state-65 back throw and the state-66/67 540-degree
continuation. A retained target more than 160 cm above a grounded player
selects state 63; its motion-127 attach is non-damaging, its primary completion
delivers 70, and its CobWeb remains owned until state exit.
`Player::GetAirWebSpecialState`
(`0x00343de4`) first asks `CEnemy::CanBeTiedUp` (`0x003304b4`) and
`CEnemy::CanBeDragTo` (`0x0032f7b8`): a bindable target selects state 101, a
drag-only target selects state 115, and only the no-target case retains the
web-swing path. The attack graph returns to the original airborne states 14 or
15 rather than falsely grounding the player.

Those target capabilities come from shipped data. `CEnemy::ResetBehavior`
(`0x00332cc0`) enables tying only when behavior-map slot 4 exists, and the
table at `0x004c0848` resolves slot 4 to `CBehaviorTiedUp`; the portable loader
therefore reads each list from `EnemysBehaviorConfigs.bin` rather than guessing
from enemy names. `CEnemy::InitEntityAttribute` (`0x003373e8`) independently
disables dragging for the two aircraft types. The button-combat autoplay gate
proves that one Square/X request removes 35 health from a real enemy, one
Circle/B ground-web request removes 20, and an airborne Circle/B request with
a bindable target enters native state 101. `CEnemy::ParseLocalAiMessage`
(`0x00331eb0`) converts web motions 110, 123, and 124 to tied-up message 0x69;
`CBehaviorTiedUp::onMessage` (`0x003c7558`) then enters serialized enemy state
35, selects the looping `tied` animation, and starts the 4000 ms duration from
`CBehaviorTiedUp::StateEnter` (`0x003c77d0`). The focused gate proves both the
ground-web health delta and separate ground/air targets entering that tied
state. Detailed events retain the selected enemy ID, state ID, delivery path,
binding flag, requested damage, actual health delta, and resulting enemy
behavior/animation.

The airborne target-special punch is state 86, motion `0x6c`.
`Player::SetNextStateId` aims its explicit 1400 cm/s velocity at the live
target's `Bip01_Head`, and `Player::UpdateAttackParam` retries the contact test
after frame 2 until `CheckAttackTarget` sets the accepted-contact latch. The
runtime follows that target, delivers 50 exactly once, retains effect 26 for
the travel interval, and emits effect 16 plus the authored air-diagonal-kick
and kick-impact audio on accepted contact. Air-web states 101/102 separately
retain the target and visible line while delivering only their authored
zero-damage grab contact.

Web-binding damage follows the dedicated cases in `Player::UpdateAttacks`
(`0x00351204`), not the ordinary MC_STATE hit-frame loop. Motion `0x7f`
issues a zero-damage attach at authored frame 7 and its 70-damage hit at the
primary drag-down clip finish; motion `0x81` damages at primary throw finish;
motion `0x82` damages at `soundTriggerFrame` and immediately releases its
CobWeb lines. State 103 owns two simultaneous lines and delivers 280 at frame
33. Deterministic first-encounter ledgers now pin the normal bind, directional
throw, drag-down, ground back/540 throw, punch-to-web throw, airborne target
kick/grab, air 720 throw, and no-Web kick-down paths.

State 99's serialized 100-damage sector is still collision-gated. With the
native enemy-cylinder center and the shipped first-encounter poses, its
animated `Bip01` origin is above the grounded target and the contact misses.
The subsequent state 103 delivers 280, while state 104 delivers 100 and its
buffered state-105 continuation delivers 80; the corresponding fresh-thug
remainders are 220 and 320 rather than counting an undelivered state-99 hit.

The same native function treats motions `0x6c`, `0x6d`, and `0x6f` as
physics-ended attacks: it copies the stored velocity to the character body,
then consumes the buffered state or normal successor as soon as
`Unit::IsFalling` is false. The portable controller now sweeps downward
motion against authored support and performs that early handoff. This fixes
state 104 tunnelling below the Room 1 road and makes state 105's authored
80-damage heavy kick and effect 20 reachable through ordinary input.

Melee overlap is likewise three-dimensional rather than a planar distance
shortcut. `EnemyAttributeFile::ReadAttributeInfo` (`0x0033bf00`) reads the
first two signed fields after each exported enemy name and converts them to
the collision radius and height consumed by `CEnemy::InitEntityAttribute`
(`0x003373e8`). Level-one knife, bat, and gun thugs use a 40-by-150 cm
cylinder, big and hammer thugs use 60-by-180 cm, and Sandman uses 100-by-200
cm. The portable sector test follows the vertical-cylinder/pie relationship
of `Physics::testCylinderPie` (`0x003d38f8`) and
`Physics::testPieCollision` (`0x003d5434`), including target-radius expansion
at the angular edges. Both player and enemy attacks now require overlapping
vertical spans, so a staged enemy one storey above the player cannot be hit
through the ceiling.

`WaitSpawn` is an actual lifecycle flag, not a request to render an enemy at
its scene coordinate. `CEnemy::ProcessUserAttr` (`0x00332870`) stores the flag
and initially calls `SetVisible(false)`; `CEnemy::SetVisible` (`0x00330858`)
clears it when a script reveals the actor. Visibility and physics activity are
separate native states. `CEnemy::Update` (`0x00333fd0`) calls
`Unit::UpdatePhysicsWithVisible` (`0x00323748`) to attach or detach the physics
object according to visibility, while `CCinematicThread::DisableAI`
(`0x00371f94`) and `EnableAI` (`0x0037206c`) explicitly deactivate and activate
the `PhysicsEntity`. `CEnemy::EnableAI` (`0x00330914`) also activates it.
`LevelEnemyRuntime` preserves that distinction: the visible first-encounter
actors remain physically inert while their reset cinematic has AI disabled;
the Room 2 `WaitSpawn` actors retain active physics and begin falling only
after their reveal, settling their authored cylinders onto the real level
collision before allowing pursuit or attacks.

`MoveObject` is a timed cinematic operation rather than a teleport followed
by delayed AI. `CCinematicThread::MoveObject` (`0x00370494`) operates on the
scene object bound at thread offset `0x60`, immediately applies its absolute
position and quaternion, then records the next `MoveObject` key when it is at
least 51 ms later. `CCinematicThread::DoExecChange` (`0x00371880`) linearly
interpolates position and slerps rotation, evaluates the current clock before
adding the frame delta, and reaches the authored endpoint when the next key is
dispatched. The player, enemy, and ordinary level-object runtimes all
reproduce this ordering. Cinematic 71 visibly runs actors 394, 395, and 397
from their staging coordinates into the first encounter before enabling
their physics and behavior; `enemies.csv`, `objects.csv`, and `frames.csv`
retain each motion's active flag, elapsed clock, and authored duration.

The Room 2 regression follows the complete authored chain. Crossing oriented
trigger 973 starts cinematic 974, which stages the rooftop spider-sense
exchange, kills temporary actor 400, reveals enemies 398, 399, and 401 at
4000 ms, shows the temporary web wall, and starts watcher cinematic 1140.
The three enemies fall from their serialized rooftop staging positions to the
street under runtime physics. Cinematic 1140 waits for all three native enemy
death states before opening the web wall and advancing the checkpoint. The
autoplay probe approaches the trigger from outside, waits for cinematic 974,
requires all three enemies to be visible with AI and physics active, waits for
all three to ground, and verifies their exact 500-point authored starting
health before ordinary combat. It also requires watcher 1140 to save
checkpoint 30027 after the last death. This keeps route traversal separate
while still testing the real spawn, presentation, physics, combat, and
completion path.

## Authored room objects

`LevelOneBootstrap` now materializes 106 mesh-bearing room instances rather
than treating room geometry as the entire visible world. The set includes all
77 `CDestroyableObject` props plus authored cars, drop objects, web walls,
hostages, static objects, stream pipes, and the opening slide bus. Mesh,
texture, and animation payloads are deduplicated into named archetypes while
each scene instance retains its original object ID and absolute transform.

`LevelObjectRuntime` is the renderer-independent state counterpart for
`CAnimatedObject`, `CDestroyableObject`, `CStaticObject`, and
`CDestroyableStreamPiping`. Cinematic `SetVisible`, `SetAnim`, `MoveObject`,
`Physics`, and `ShowStream` commands now address those instances directly.
Object-thread `MoveObject` ignores an explicit `ObjectID`, matching the bound
pointer used by native `CCinematicThread::MoveObject`, and interpolates its
complete authored position/quaternion to the following key.
This includes the three end-cinematic pipe hides and the animated web-wall
open/close sequence. `Physics` preserves the collider handoff performed by
`CDestroyableObject::SetPhysics` (`0x003061cc`). The called
`createDestoryableEntityPhysics` (`0x003d84b4`) creates an active behavior-4
triangle-mesh entity at the current scene-node transform, then explicitly
zeros velocity and gravity. The three level-one commands therefore do not
request autonomous rigid-body motion; later `MoveObject` commands remain the
only authored visual movement for those objects.

The opening firetruck is a separate native `CSlideCar`, object 1210
(`SlideCar_bus`), rather than an ordinary non-colliding room prop. Both
`CSlideCar` constructors (`0x0031c240` and `0x0031c474`) locate the child scene
node named `bbox`, pass its Collada bounds to `createTransmissionPhysics`,
place that physics entity at the car's absolute transform, disable gravity,
and register it. `LevelOneBootstrap` therefore recovers that authored child
box even though the enclosing IRR node says `Collision=false`.
`LevelCollision` rebuilds the car's 12 box triangles from the current
cinematic transform, so collision follows the moving firetruck rather than
remaining at its serialized start. The no-teleport post-crash regression now
jumps from the street to the approximately 319 cm truck roof, waits there,
then jumps to the approximately 616 cm shopping-center roof and reaches
cinematic 40027, the red-orb tutorial trigger. Room 1 contains no native
`0x20` climbable-wall surface on this route, so the harness does not invent a
wall attachment. After the modal tutorial is dismissed, the same route
resumes the interrupted landing, approaches the recovered roof edge, jumps to
the approximately 805 cm Room 2 roof, and enters hostage tutorial cinematic
972 without a diagnostic teleport.

The apparent broken shopping-center doorway is not changed by that crash.
Room 1 supplies it inside the single static `geometry01.bdae`; the room has no
morph controller and its IRR scene contains no mesh-bearing door object or
alternate intact/broken mesh. Crash cinematic 1212 has exactly 48 commands on
only the global, player (288), Sandman (1211), and bus (1210) threads. Its two
visibility commands hide Sandman and show jump hint 1216. The bus alone moves
and enters its `damage` animation at 600 ms; no command addresses or swaps a
door. The source-backed conclusion is therefore that the shipped level loads
the visible opening in that state before the bus arrives. A core regression
pins the room/cinematic inventory so an invented door lifecycle cannot be
introduced later.

Those commands live in alternate/editor cinematics 20006, 20010, and 20013.
A state-aware cinematic graph census seeds only enabled triggers, follows
`StartCinematic` and `PlayDAECamera` successors, and adds a disabled trigger's
edges only when a reachable `EnableTrigger` command names it. This proves that
34 scripts are reachable in the shipped first-level route. Tutorial cinematic
1172 and Room 9 cinematic 30003 are inactive because source triggers 1171 and
30005 are authored disabled and no reachable script enables them; this also
explains why 30003's stale camera-area ID 224 cannot affect normal play.
Across all 34 reachable scripts, every non-global thread object resolves to a
loaded main/room scene node.

The D3D11 backend uploads a dynamic mesh per instance but shares immutable
texture views by archetype, avoiding the original per-instance memory
explosion. Editor-only, unreferenced image slots remain index-stable and
untextured helper/shadow batches are omitted. A WARP capture verifies the
textured street furniture and props without white fallback geometry.
Ordinary scene and actor meshes use the shipped counter-clockwise front-face
winding with back-face culling. Procedural lines, particles, hints, HUD, and
cinematic UI retain a separate two-sided state. This distinction is required
by the Room 8 wall camera: its authored below-street viewpoint must see through
the one-sided pavement back faces while retaining all screen-space overlays.

## Collada mesh layout

The BRES root points to `SCollada`; its geometry library contains named
`SGeometry` records. `SMesh` then supplies an interleaved vertex stream and
0x3c-byte `SMeshBuffer` records. The level-one meshes use 16-bit indices and
component slots for float3 positions/normals, float2 UVs, and packed vertex
color. `assets::ColladaMeshFile` resolves these into typed vertices, primitive
groups, material names, and bounds. Its source comments retain the matching
original class and member names rather than anonymous address labels.

The adjacent Collada libraries are also typed: `SImage` records are 0x14
bytes, `SEffect` records are 0x5c bytes, and `SMaterial` records are 0x40
bytes. A material selects an effect and can also carry primary/secondary image
indices at offsets `0x24/0x28`; effect texture lists remain the fallback for
general Collada materials. The image's source path is the archive-facing texture name;
for example, Room 1 maps `alphatest` to `level01_alphatest.tga` and
`Material__54` to `041_building.tga`.

The original
`CCommonGLMaterialRenderer_ALPHA_TEST_NONTRANSPARENT::onSetMaterial` at Ghidra
image address `0x00397864` enables alpha testing with `GL_GREATER` and a 0.5
reference. The D3D11 material path preserves that behavior with an HLSL
`clip` shader variant for the recovered `alphatest` materials.

Room lighting is authored rather than synthesized at runtime. The interleaved
BDAE vertex declarations select `COLOR0`, and level-one `geometry01.bdae`
alone contains 1,507 distinct opaque colors ranging from `0xff000000` shadow
samples to `0xffffffff` unoccluded samples, with colored bounce light between
them. Six room materials also carry the `lightmap.tga` atlas through the
effect texture channel serialized at `SEffect +0x38/+0x3c`.
`CResFileManager::postLoadProcess` at `0x00425d88` resolves that channel
through the effect's mapping and image-reference tables, and
`CMaterial::prepareMaterial` at `0x0041c750` selects material type 7 only when
the resolved image name contains `lightmap`. This discriminator is essential:
many ordinary building buffers contain a secondary vertex component with
large tiling coordinates that must not sample the lightmap atlas.

`CCommonGLMaterialRenderer_SOLID::onSetMaterial` at image address `0x00455a08`
and `CCommonGLMaterialRenderer_LIGHTMAP::onSetMaterial` at `0x00455fe8` both
use modulation. The D3D11 paths therefore combine the diffuse texture and
decoded vertex color, then additionally modulate the six authored lightmap
materials by UV1. No directional-light approximation is applied over the
bake. Core tests census the Room 1 vertex colors and exact lightmap material
set; WARP captures preserve both the façade textures and baked road shadows.

Geometry-library coordinates are object-local. The adjacent `SVisualScene`
library begins at `SCollada` offsets `0x6c/0x70`; each visual scene owns
0x50-byte `SNode` records. A node supplies position, quaternion rotation,
scale, child nodes, and typed eight-byte instance records. Instance type 3 is
an `SInstanceGeometry`, whose local `#id` selects an `SGeometry`. This layout
matches the preserved `CColladaDatabase::constructNode` at Ghidra image
address `0x00419e80`. `ColladaMeshFile::sceneGeometries` now evaluates that
hierarchy, transforms positions and normals, and preserves a separate raw
geometry library. Room 1 resolves to 152 world-space geometry instances.

The transform convention is the original Irrlicht convention, not the usual
column-vector shorthand. `quaternion::getMatrix_transposed` at `0x00305f48`
writes the relative rotation, `ISceneNode::getRelativeTransformation` at
`0x004083c4` scales its complete rows, and `CMatrix4::transformVect` at
`0x002c2924` evaluates `point * matrix + translation`. For hierarchy updates,
`CMatrix4::mult34` at `0x002bac54` consequently emits `local * parent`.
Applying the opposite convention mirrored every non-identity scene-node
rotation around its pivot, which put several intro buildings in front of the
correct grey façade and made that façade appear rotated. A concrete
-90-degree Room 1 node now pins its native world-space bounds in CoreTests.
The same regression set covers a translated child pivot beneath a rotated
parent and a rotated, non-uniformly-scaled building. It also requires the
static affine evaluator and animation-pose matrix evaluator to produce the
same bounds for all 152 Room 1 instances at time zero. The intro-building
autoplay capture additionally verifies the corrected composition while
retaining the baked road shadows, police rig, and car.

## Cinematic command files

The `.cff` resources are UTF-16 XML fragments, not opaque bytecode. Each
`cinematicThread` targets the player, a scene object, or global level state and
contains timestamped named commands with typed attributes. The native
`game::CinematicScript` parser preserves those names and IDs. Level 1's start
chain is cinematic 1264 (start 1265 and disable trigger 1263), the 38-command
1265 intro, then cleanup cinematic 1266. The intro explicitly names its DAE
camera/character animations, 15 sound events, message strings, and visible
rooms.

The application executes 1266 through the same generic cinematic player as
every other script instead of duplicating its cleanup in a hard-coded
transition. Autoplay command telemetry retains the originating cinematic ID,
which makes the complete 1264/1265/1266 chain and every later command site
auditable without inferring provenance from timing.

`game::CinematicPlayer` provides the corresponding monotonic command
scheduler. It performs a stable merge of thread-local command streams, so
commands sharing a timestamp retain serialized thread order. The caller owns
the clock and command handlers; tests currently verify all 38 intro commands,
including the five events at time zero and the 41.8-second final event.

The intro's `camera_lv1_start.bdae` contains three typed `SAnimation` tracks:
camera rotation, camera translation, and camera-target translation. Each has
823 monotonic millisecond keys spanning 0 through 53,033 ms. The native
`assets::ColladaAnimationFile` resolves the 0x24-byte animation records and
0x0c-byte source descriptors, validates their integer-time/float-value
streams, and provides normalized interpolated samples.

The same resource contains one 0x1c-byte `SCamera`: `Camera01-camera`, a
perspective camera with a 45-degree vertical field of view, 1.5 authored
aspect ratio, 1-unit near plane, 1000-unit far plane, and target reference
`#Camera01.Target-node`. The intro CFF overrides the far plane to 10000.
`game::CinematicCamera` binds the named position/target tracks, retains the
level's Collada Z-up convention, and feeds sampled world-space poses to the
D3D11 view/projection path. WARP regression coverage renders the intro
environment from the actual time-zero camera rather than the earlier
normalized overview.

Gameplay animation banks also expose the adjacent `SLibraryAnimationClips` at
`SCollada` offsets `0x1c/0x20`. Each 0x0c-byte `SAnimationClip` is a preserved
name plus inclusive bank start/end timestamps; this matches the original
`CColladaDatabase::getAnimationClip` and `CTimelineController` clip accessors.
`spiderman_anim.bdae` contains 46 skeletal tracks and 242 named clips. The
native parser resolves names such as `idle_stand` (3133–4466 ms), `run`
(8033–8833 ms), and `walk` (15033–16100 ms), allowing source gameplay code to
select authored animation ranges rather than using guessed frame intervals.
Five FX attachment channels contain a duplicated pre-roll transform under a
serialized `0xe5555700` timestamp before their time-zero key; the native view
discards that sentinel and retains the monotonic authored bank timeline.
The `SChannel` target enum also distinguishes full float3 translation (1)
from scalar X/Y/Z channels (2/3/4) and quaternion rotation (5). This matters
for the gameplay bank: `Bip01-node-translation` is a scalar Z channel, while
`Dummy_center-node-translation` is a full vector. Preserving those component
semantics prevents scalar root height from incorrectly replacing all three
coordinates during skin evaluation.
Quaternion keys use the original shortest-path spherical interpolation rather
than component-wise interpolation. Joint-local matrices also follow
`ISceneNode::getRelativeTransformation` at image `0x004083c4`, which calls
`quaternion::getMatrix_transposed`; using the non-transposed helper produced a
coherent but inverted skeleton. The recovered `idle_stand` pose now remains
grounded with a 136-unit vertical extent, covered by a core pose regression.

The intro's `MustBeVisible` command names Rooms 1 through 5, but the main
scene's link table names all thirteen playable room scenes. The native level
bootstrap follows that table instead of a hard-coded intro subset and loads
all thirteen geometry, collision, navigation, scene, and texture sets as
separate D3D11 resources. The linked scenes contribute 27 triggers, 30 melee
enemies, the type-4 big thug, type-5 hammer thug, and both type-16 Sandman
instances, plus 43 cinematic objects. These actors retain their authored
health, meshes, animation banks, and initial clips; the big thug, for example,
uses its 800 health, `idlebaz` idle, and
`idle_death_on__ground_back` terminal pose. Forty-two cinematic scripts are
present;
Room 13's unreferenced editor object 1239 names a CFF absent from the shipped
archive, so it is retained as explicitly unavailable and never dispatched.
Room-owned trigger, enemy, and camera-control positions use their serialized
absolute transforms rather than room-local editor coordinates.

The D3D11 WARP regression also renders Room 13 through authored final camera
area 10100. Its collapsed asphalt slabs, exposed pipes, wreckage, and open
below-street background are geometry in the shipped final boss room rather
than a missing linked-room transform.

The bootstrap also loads `lvl01_sky.bdae`. The shipped IRR node is authored as
`!GameType=SkyPlane`; `CLevel::LoadNextObject` at `0x003853fc` maps that exact
value to `FpsSkyBoxSceneNode`, not the separate `CSkyBoxObject` class.
`FpsSkyBoxSceneNode::render` at `0x0039a998` starts with each BDAE child's
authored absolute transform, then adds the active camera X/Y and half its Z.
The D3D11 sky path reproduces that unusual camera-relative translation. This
keeps the 8.8k-unit skyline dome around the moving cinematic camera without
discarding the child transforms embedded in the shipped BDAE.

The sky has three evidenced material layers: opaque `01_sky_2.tga`, opaque
`caodi02.tga`, and variable-alpha `sky_01.tga`. D3D11 uses standard source-alpha
blending with depth reads but no depth writes for variable-alpha diffuse
materials, while keeping the recovered 0.5 alpha-test path separate. This
removes the skyline layer's transparent white background without treating
cutout foliage as blended geometry. The pale horizon stripes still visible in
some angles are pixels present in the shipped `01_sky_2.tga`, not the clear
color or missing room meshes.

The eight `PlayDAEAnim` commands are resolved through their CFF object IDs to
named Irrlicht scene nodes: Spider-Man (288), three thugs (1257–1259), the
hostage (1260), cop (1261), police car (1262), and web rope (1277).
`CinematicActorAsset` loads each original entity BDAE, texture set, scene
transform, command start time, and animation BDAE. Their recovered animations
range from two car tracks to Spider-Man's 40 tracks. D3D11 stores the room and
each actor as separate GPU mesh resources so their material libraries and
textures cannot collide. Actor resources become visible at their authored
`PlayDAEAnim` timestamps rather than all appearing at cinematic time zero.

## Collada controllers and animated poses

Controller type 0 is an `SSkin`. The 0x6c-byte payload contains a geometry URL,
64-byte bind-shape matrix, joint-name array, one 64-byte inverse-bind matrix per
joint, shared weight table, an influence count per vertex, and packed
`uint16_t {jointIndex, weightIndex}` pairs. Spider-Man validates as 38 joints,
641 controlled vertices, 12 shared weights, and 974 influence pairs. Joint
names such as `Bone1` are Collada scope IDs; the visual-scene nodes bridge them
to the animation channel node IDs.

The recovered `CColladaSkinnedMesh::prepareSkinData`,
`prepareSkeletonMtxCache`, and `skin` functions are preserved at original
addresses `0x00428240`, `0x00428674`, and `0x00428718`. The native
`assets::evaluateColladaPose` path follows their matrix order:
`worldJoint * inverseBind * bindShape`, normalizes vertex weights, and updates
positions and normals into D3D11 dynamic vertex buffers. Skinned character
channels already produce authored level-space poses, while rigid actors use
their serialized Irrlicht absolute transform. Rigid BDAE visual-scene nodes
are also sampled, covering the police-car and web animations. WARP regressions
exercise the intro at 0, 10, and 20 seconds; optional BMP captures verified a
textured Spider-Man and the four 16.2-second character entrances without
exploded geometry.

Animation streams are selected through the serialized `SAnimation` sampler,
not by assuming that the first two `SSource` records are input and output.
The sampler at `SAnimation + 0x10` stores the input/output source indices at
`+0x04`/`+0x08`; the portable loader resolves those indices before decoding
the streams. `CColladaDatabase::getAnimationTrackEx` at `0x00418348` maps
channel type 5 to `CQuaternionEx`, whose `getKeyBasedValue` at `0x0045182c`
consumes four quaternion components per key. Channel type 9 is not a compact
type-5 stream; it selects the distinct `CQuaternionAngleEx`. Its preserved
`getKeyBasedValueEx` at `0x004190e0` linearly samples one angle, calls
`quaternion::toAngleAxis` on the joint's existing rotation, and rebuilds the
quaternion with `fromAngleAxis`; a degenerate axis falls back to positive Y.
The portable loader and pose evaluator preserve that track type explicitly.
Treating those police limb channels as rotations constrained to local Z kept
the mesh bounded but left the officer's arms visibly dislocated. Level-one
regressions now check the serialized scalar angles and vertices dominated by
both forearm joints, in addition to the corrected skinned-pose bounds.

Spider-Man's material directly selects `spiderman_red.tga` as its primary
layer and the 32x32 `spiderman_rim.tga` sphere map as its secondary layer. Its
serialized secondary mode is 0, which `CMaterial::prepareMaterial` maps to
renderer index `0x0c`, `CCommonGLMaterialRenderer_REFLECTION_2_LAYER`.
That renderer's preserved `onSetMaterial` at `0x00455be0` sets
`GL_COMBINE_RGB` to `GL_ADD`. The D3D11 reflection variant therefore samples
the secondary layer with view-space normal sphere coordinates and adds it to
the lit diffuse result, reproducing the colored edge/rim highlights rather
than treating the 32x32 map as ordinary mesh UV data.

At the end of the 53,033 ms camera animation, the application now follows the
authored cinematic successor 1266 by releasing the intro camera and switching
object 288 from `spiderman_lv1_start.bdae` to the persistent player's
`spiderman_anim.bdae` bank. The same dynamic D3D11 actor buffer is reused,
starts on the named looping `idle_stand` clip, receives the serialized player
world transform, and is viewed through initial CameraArea 283. A WARP capture
regression covers this native cinematic-to-gameplay render transition.

## Vox sound events

The preserved `VoxSoundFile::LoadRecordFromFile` and `ReadBasicRecord`
functions (Ghidra `0x003da650` and `0x003da570`) load `/VoxSound.bin` or
`/VoxSounds.bin` into 0x30-byte event records. The supplied `configs.pack`
contains all 493 serialized records. `audio::VoxSoundTable` now decodes their
IDs, event names, resource paths, instance limits, volume/record flags, distance
ranges, and remaining preserved parameters. Its exact path mappings resolve
483 shipped Ogg resources; ten table targets are absent from the supplied
sound directory and remain explicit. `audio::SoundEventCatalog` retains the
510 unique direct file stems as a fallback and rejects five ambiguous stems,
while configured events take precedence. This resolves aliases such as
`SFX_THUG_KNIFE_HURT_1` to `sfx_thug_hurt_1.ogg` and
`SFX_VERTICAL_IMPACT` to `sfx_vertical_web_slam.ogg` without inference.

`audio::CinematicSoundBank` scans the typed CFF before playback and decodes
each unique resolvable event once, keeping Vorbis work off its scheduled frame.
The application advances `CinematicPlayer` from the same monotonic clock as
the camera and independently dispatches the authored 2D, 3D, loop, and stop
flags to XAudio2. All 19 unique level-one
intro event names now resolve through the recovered table and preload before
playback. The same deduplicated path scans all 42 runnable encounter scripts,
preloads their 60 unique sound events with no unresolved aliases, and tags
XAudio2 voices by Vox event name. Authored `Stop`/`Stop2D` commands can
therefore stop every matching active voice instead of leaking looping sounds
across cinematic boundaries.

Player state sounds preserve the same identity through
`PlayerStateSoundBank`: each decoded variant carries its Vox record ID and
event name to the application callback rather than being collapsed into a
generic player-state label. The deterministic full-opening probe reaches
gameplay after emitting 25 play requests: all 19 distinct authored intro
events, both downtown music layers, and four intentional repeated impacts or
movement cues. It asserts every one of the 19 authored event names directly.

All eight level-one cinematic `Play3D` commands resolve their thread object to
the live player, enemy, or static-object position. Enemy behavior cues use the
same path with their source enemy ID. Listener position follows the preserved
`CLevel+0x30c` policy: the constructor at `0x003815b4` initializes it to the
main character, `CCinematicThread::ListenerPositionCmd` at `0x0036fe28`
copies `ListenerOnMC`, and `CLevel::Update` at `0x003820bc` selects either the
live player or camera position while retaining camera look/up directions.
The eight shipped toggles across Levels 2--4 now drive that same state, and
the Electro replay trace verifies its false/true transition at 3700/7800 ms.
Active voices are re-panned as the selected listener moves. The
Windows backend applies the Vox record's minimum/maximum emitter range and
recovered distance-culling flag, then uses constant-power mono-to-stereo
panning. The serialized 0x14 float is preserved in `VoxSoundRecord` but is not
used as linear gain: the original `Get2DEmitter` and `Get3DEmitter` paths do
not consult that field, and most shipped records store zero there.

Both level-one `CTriggerSound` volumes are reconstructed from their authored
OBB dimensions. Following `CTriggerSound::Update`/`SetState` at `0x0036cb14`
and `0x0036ca34`, entering a volume starts its `SFX_FIRE_TRAP` 2D loop and
leaving it stops the same independently tagged XAudio2 voice. Start and stop
use the native 500 ms fades; the original event-ID 0xa1 half-volume special
case is retained even though level one's fire loop is event 146. An end-to-end
probe enters and leaves both volumes and requires the observed two
`SFX_FIRE_TRAP` starts and two stops; the trace also records both complete
authored volume transforms.

## Native interface sprites and player HUD

`assets::SpriteAtlas` follows `CSprite::LoadSpriteData` at original address
`0x002e88d4`. The compact `0xa9d1` stream is represented as named module,
frame-module, frame, animation-frame, and animation records with every index
and span bounds-checked. The shipped `interface.bsprite` resolves to 149
modules, 257 frame modules, 158 frames, 134 animation frames, and 40
animations. No touch-control frames are submitted by the Windows gameplay
path.

Despite their `.tga` names, the paired interface images are DDS resources.
`assets::DdsAtcTexture` validates the DDS header and `ATCA` FourCC, then
decodes ATC explicit-alpha blocks to RGBA8 using the MIT-licensed AMD
Compressonator reconstruction rules. The decoded 1024x1024 interface atlas is
uploaded once as a D3D11 shader resource.

The native HUD follows `CLevel::Render2DInterface` at original address
`0x00387a54`: UI item 0x14 is placed at authored 480x320 coordinates (46,32),
frame 0x1b supplies the health surround, frames 0x1c/0x1d supply delayed and
current health, frames 0x18/0x19 supply web power, and frame 0x1f supplies the
Spider-Man badge. `PlayerHudHealthState` reconstructs the immediate and
trailing damage values maintained by
`CLevel::UpdateInferfaceHealthAndWebPower` at `0x0037d860`. D3D11 renders the
result with the recovered 50 ms hold and 500 ms trailing drain through a
backend-only alpha-blended sprite pass after the 3D scene,
with a uniformly scaled and centered 3:2 safe canvas on widescreen displays.
WARP regressions compare frames before and after HUD submission and produce a
1280x720 capture for visual review.

## Collectible bonuses and health orbs

The 13 linked level-one rooms contain 56 `CBonus` nodes: 15 health pickups and
41 skill-point pickups. `CBonus::ProcessUserAttr` at `0x003937a8` maps their
serialized flags to `bonus_green` and `bonus_red` particle presets. The
portable bootstrap retains every object ID, room, world position, and type;
`LevelEffectRuntime` creates the authored stationary emitters and can disable
each source independently when collected.

`CBonus::Update` at `0x00393680` uses a 150-unit player-center radius and then
launches `CHealthOrbs`. `LevelBonusRuntime` reproduces the moving target at
player height +100, the roughly 2.04-second Hermite path from
`CHealthOrbs::Init`/`OnAnimate` (`0x003a190c`/`0x003a17c0`), difficulty health
amounts 80/50/35/20, and five skill points per authored red pickup. The D3D11
backend draws the frame-5 ribbon plus frame-15 health or frame-14 skill head
from `effects.bsprite`; arrival plays Vox record 98,
`SFX_ORBS_COLLECT`.

Skill pickups also follow `CBonusManager::Update`/`Draw2D`
(`0x0039413c`/`0x00393f8c`): points aggregate for one second, then rise and
fade above the projected player for two seconds. The total counter follows
`CLevel::SetShowSkillPointFrame`/`RenderSkillPoint`
(`0x0037f358`/`0x0038728c`) and stays visible for six seconds. Deterministic
tests cover every linked pickup, grant timing, amounts, and UI lifetime; WARP
captures verify stationary, ribbon, and HUD pixels. The application-level
pickup probe additionally requires the resolved `SFX_ORBS_COLLECT` event, so
the moving-orb arrival and its sound dispatch are tested as one lifecycle.

## Player states and authored jump traversal

`PlayerStateConfigDatabase` now preserves the portable fields recovered from
`StateFile::ReadBasicState` at `0x0033d294`, including `stateClass`,
`motionType`, the four motion parameters, primary and alternate animation
IDs, `nextStateId`, and transition records. `Player::SetNextStateId` at
`0x003491d0` selects the animation ID, while `Player::UpdateMove` at
`0x0035081c` consumes `nextStateId` after a clip finishes. The level-one data
therefore supplies the native jump graph rather than requiring inferred
animation names: state 13 (`k_state_jump_start`, motion 18) selects
`jump_ready_to_jump`, state 14 (`k_state_jump_fall`, motion 20) selects
`jump_to_fall`, state 30 (`k_state_jump_web_jump`, motion 22) selects
`jump_to_small_web_swing_to_fall`, and state 16 (`k_state_jump_land`, motion
23) selects `fall_to_idle`.

The 600 ms jump arc is authored on the `Dummy_center-node` translation track:
the first clip rises roughly 353 cm and the second returns to ground.
`Player::Update` at `0x00353494` passes that sampled dummy stream through
`Unit::UpdateDisplacement` at `0x00324df0`, so the portable player's physical
capsule now follows the authored root height. The render anchor stays at the
launch height because D3D11 applies the same track once during skinning; this
preserves the visual pose without double displacement while camera and trigger
queries receive the real airborne position. If the falling arc finds no
authored floor, state 15 continues with the recovered `-1200 cm/s` value at
image address `0x004c6a78`. Airborne horizontal motion uses the existing
recovered 700 cm/s controller-relative movement and the level collision wall
resolver. The no-teleport combat probe now jumps naturally into elevated
trigger 20035 and observes cinematic 20038; the harness accepts its historical
start because that zero-time cinematic can complete within the same fixed
tick.

An airborne Cross/A transition from states 13--15 enters state 30. Its 666 ms
dummy stream advances roughly 518 cm in the facing direction while descending
roughly 172 cm, after which authored `nextStateId` 15 resumes sustained fall.
That is the shipped traversal across Room 2's real no-floor strip between the
two street collision sections. `Player::UpdateMove` handles motion type 22 in
its airborne branch, so the portable implementation feeds both the physical
and compensating render streams through the same collision-aware root-motion
path instead of inventing a floor or weakening the adjacent walls.

## Authored street boundary recovery

The Room 1 player spawn at `(14701.2, -9696.75, 7.76)` overlaps the southern
street collision shell by roughly 29 cm at the native 50 cm margin. Preserving
the capsule's starting side during depenetration therefore trapped Spider-Man
outside the playable street and wedged ordinary movement near
`(14770.4, -8627.9, 6.96)`. `LevelCollision` now exposes an explicit player
depenetration policy: while an embedded player intentionally moves toward the
authored face normal, it resolves to that side; once clear, the same triangle
returns to normal two-sided collision. Enemy motion retains the conservative
start-side policy, including unsupported Room 4 spawn behavior.

A core regression advances from the exact authored spawn to the first
encounter in 35 cm ground-motion steps and requires arrival within 50 cm. The
`street-route-probe` application scenario repeats the route with normal player
input, no teleport, requires enemies 394, 395, and 397 to activate, and checks
that the player remains alive.

XInput A follows the original Cross route into `requestJump`. SoundConfig 8
(`k_mc_sfx_swoosh_jump`, six variants) plays on state 13 entry and
SoundConfig 11 (`k_mc_sfx_land`) plays on state 16 entry through the existing
predecoded XAudio2 state-sound path. Deterministic core regressions cover the
state IDs, authored clips, root-height arc, state-30 airborne transition and
forward displacement, landing, and sound cues; a WARP regression renders the
midpoint pose in the first gameplay camera area.

## Authored wall traversal

Wall traversal follows the native player-state records rather than treating a
vertical collision as ordinary ground motion. State 6
(`k_state_move_climb_wall`, motion 14) enters with `run_to_wall_climb`; state 1
(`k_state_idle_onwall`) holds `wall_climb_idle`; state 5
(`k_state_move_onwall`, motion 13) selects the six directional climb clips;
and state 7 (`k_state_move_exit_wall`, motion 15) uses
`wall_climb_to_roof_idle`. Cross transitions from the wall to states 8--11 and
the authored `wall_jump_up/down/left/right` clips.

`Player::CheckClimbableWall` at `0x0034863c` supplies the forward wall query
and 0.94 surface-suitability threshold, while `Player::CheckWallCanClimb` and
`Player::GetOnWallMoveDir` retain contact and orient controller motion in the
wall plane. `Player::UpdateMCSpeed` at `0x00346f50` scales each wall axis by
0.35 centimeters per millisecond (350 cm/s). `Player::SetNextStateId` at
`0x003491d0` supplies the attach, exit, and directional-jump state setup.

`PhysicsTriangleMeshShape::addSceneNodeInternal` at `0x003d9e94` assigns the
surface flags from the original node prefixes: ordinary `wall*` faces use
`0x20`, `jump_wall*` uses `0x10`, and `edge_wall*` uses `0x40`; unrelated
vertical collision uses flag 2 and ground uses flag 1.
`PhysicsTriangleMesh::constructMesh` at
`0x003d95d8` further separates regular wall and ground faces at its recovered
0.70710677 signed normal threshold (dot product with +Z, not absolute Z).
Downward-facing ceilings are wall-class surfaces, not ground support. Native
`double*` nodes add bit 4, preserving their explicit two-sided contract.
`LevelCollision::climbableWallContact` therefore
returns only the nearest native `0x20` vertical contact, plus a consistently
player-facing normal. This prevents invisible boundary collision from being
misclassified as a climbable wall.

`Physics::processCollision` at `0x003d5b5c` confirms that the field at native
`PhysicsEntity + 0x104` is an ignored-surface mask. `Player::SetNextStateId`
sets bit `0x10` for airborne jump states and conditionally for motion 16 wall
jumps, so portable airborne movement and landing queries ignore authored
`jump_wall` faces while ordinary running does not. `Player::CheckJumpWallCanPass`
at `0x0034284c` maps states 8--11 and 69--72 to up/down/left/right and uses the
recovered 400 cm vertical and 300 cm lateral clearance constants. The latter
family is the four on-wall attack states, not a second locomotion family.

Room 8 demonstrates why `jump_wall` is not a synonym for a regular wall. Its
0x10 faces are horizontal slabs filling gaps between separate 0x20 climb
segments. Ordinary wall movement stays on the established wall plane and is
stopped by each slab; `wall_jump_up` root motion crosses it and then reacquires
the next regular segment. `edge_wall01` is a horizontal 0x40 ledge marker.
The portable capsule-contact query selects state 7 only when that marker is
reached, matching `CheckClimbableWall(2)` instead of treating every mesh gap
as a rooftop.

`GameplayPlayer` keeps the native 50 cm cylinder offset and applies recovered
center-node root translations continuously through attach, exit, and jump
clips in the wall basis. Failed attach or jump reacquisition falls back to the
airborne state. Synthetic regressions cover native flag filtering, jump-wall
pass-through, edge contacts, attach/idle/climb/jump root displacement,
sloped-face contact reconciliation, and rejection of attacks during
attachment. Native state-5 input remains world-up: Bullet's persistent
`0x20` cylinder manifold supplies the normal-axis correction which follows
sloped wall faces. The portable controller repeats that contact correction
after each movement step so it cannot climb away from the mesh or bypass
authored camera areas. The `wall-edge-probe` autoplay
scenario stops at each authored slab, performs both required upward wall
jumps, and requires state 7 at `edge_wall01`; `move_until_state` makes the
transition deterministic rather than duration-guessed.

`Player::moveSideway` (`0x00340684`) uses the wall normal at Player+0x3dc
and the original vector class's left-handed `crossProduct` (`0x0034063c`).
The rightward wall basis is therefore `{-normal.y, normal.x, 0}`; using the
face direction reverses lateral input. Tests check both horizontal directions.
`Player::GetOnWallSpecialState` (`0x00342de0`) selects attacks 69--72 from
the nearest eligible wall target's cardinal direction; explicit movement
input takes precedence. Motion 131 emits the authored damage event, queries
all wall entities with the native any-axis sector, and returns to wall idle.
The query uses animated `Bip01`, the player's radius offset and the wall
normal slab, following `Player::CheckAttackTarget` (`0x0034fca0`) and
`Unit::CheckAttackByPosAndAxis` (`0x00324f58`). Wall attacks retain their
authored physical displacement rather than reusing ground target clipping.

The Level 6 wall cohort retains `OnWall`, `InAir`, and `Imobile`, native
attribute wall speed, and behavior slot 13 (`0x13b`). `CEnemy::CheckWall`
(`0x003305d8`) supplies six ordered 1000 cm rays; movement states 96--99,
idle 15, directional claw attacks 12--15, hurt 68, and death 72 use shipped
clips. `CEnemy::EnvironmentCheck` (`0x003341f4`) updates the wall axis when
the player is attached. Grounded-player distance handling comes from
`CEnemy::UpdateAI` (`0x00335aa0`), not ordinary ground pursuit. The automatic
route verifies a real four-punch wall kill before the rooftop fights.
Separate normal-flow scenarios exercise the player wall-web QTE described
below. Enemy wall jump-attack QTE variants 16--19 remain unimplemented;
ordinary claw combat and the player's web drag do not cover those variants.

`Player::OnHit` (`0x0034d790`) selects wall hurt state 50 for native hit
types above 103, otherwise 51. `EnemyAttackInfo+4` is preserved by the typed
config reader and carried through the melee hit event, matching
`IBehaviorBase::SpecialAnimActionCheck` (`0x003a8c60`). The reaction cancels
the pending attack, retains the wall axis, and finishes in wall idle.
`Player::Update` (`0x00353494`) still applies physical displacement after
`UpdateHurt` (`0x0035062c`); `GetAnimOffseted` (`0x003400f0`) does not mask
motion 205. The heavy clip `wall_idle_to_hurt` lasts 667 ms and moves the
base about 159.8 cm downward, while `wall_hurt` lasts 500 ms and has almost
zero net displacement. Core tests compare the live endpoint with the shipped
dummy stream. The portable traversal helper includes roof exit state 7, but
native `IsOnWall` (`0x003411cc`) excludes its motion 15; wall reactions must
not replace that exit with wall idle. The remaining generic ground/air hurt
classification is not a complete port of every `OnHit` branch.

The autoplay `climb_to x y z radius timeout` command supplies wall-space
stick input and verifies horizontal and vertical arrival. It never sets the
player position, pauses while a reaction or attack owns the animation, and
resumes ordinary climbing afterward. Fire-wall routes take the authored
clear channel around volume 836 instead of relying on the former missing
knockdown. `events.csv` also records all player clip names, durations, and
net dummy displacement, button-config durations/counts, and damage-volume
positions/sizes for reproducible route inspection.

Ground support now distinguishes ceilings from floors and resolves initial
capsule overlap. `createEnemyPhysics` (`0x003d8980`) puts the lower sphere
center one radius above the Unit base; `processSphereTriangle`
(`0x003d23cc`) therefore supports bases slightly below the floor while the
sphere center remains on its front side. Tests cover Level 6 actors 41322
and 41324 starting below their intended floor. This correction and signed
surface classification let the second rooftop wave fall to the playable
roof rather than park on the ceiling above it.

## Authored wall-web capture and release

Player state 73 (motion 132) now follows `Player::SetOnWallWebDir`
(`0x00342bf4`), `BeginQTE` (`0x0034c9b4`), `UpdateQTE` (`0x0034caf8`),
`DoQTEAction` (`0x0034c908`), and `ExitQTE` (`0x0034c888`). The separate,
renderer-independent `WallWebRuntime` selects the shipped seven directional
`wall_drag_start/keep/success/fail` families. The authored state record starts
capture at frame 3 and successful release at frame 9. The target must be an
eligible wall enemy within the state's 500 cm range and inside the active
camera frustum, matching `CGameCamera::IsPointInScreen` (`0x002f22fc`).

Button config 12 supplies a three-second, eight-press interaction. The shared
`ButtonMashProgress` follows `CQTEManager::Update` (`0x0038b240`): progress
loses one completed action after 500 ms without another press. A single press
is not success. The harness now has `set_auto_qte 0|1` and `qte_tap` commands
to test incomplete input without changing the earlier route's automatic QTE
handling. These commands remain entirely within the game process.

Capture delivers native message `0x130` semantics to the enemy and suspends
ordinary pursuit/attacks. The keep phase uses `wall_be_drag`. Success releases
the enemy into wall death and falling; failure returns it alive to wall idle.
Cancellation and checkpoint restoration release any captured target and stop
the hold sound. `CEnemy::UpdateForce` (`0x00331d78`) omits the ordinary wall
radius scene offset in Unit state 14; retaining that offset had embedded the
held enemy in the building. Target attachments use the evaluated animated
foot, forearm, or head specified by the drag direction, while Spider-Man's
strand starts at the selected `FX_RH`/`FX_LH` node. The native bound/fall voice
events and hold-loop fade are dispatched through the existing audio API.

Core tests cover all directional clips and attachment names, capture/release
timing, eight-press success, decay and timeout, cancellation, enemy health,
and camera eligibility. `level6-wall-web-probe` reaches the wall through the
normal level route and completes the drag. `level6-wall-web-failure-probe`
first lets its one press decay, verifies that the target survives and the
hold sound stops, then successfully retries. Both application scenarios pass;
the held and released states have separate native captures. `frames.csv`
records phase, target, direction, completed actions, and strand activity;
`enemies.csv` records capture state; `events.csv` records every applied
capture/hold/release/finish event. This does not claim complete reconstruction
of the separate enemy-initiated wall jump attacks.

## Authored web-grab and waypoint traversal

`LevelOneBootstrap` preserves all nine level-one `WebGrabPoint` records and
all fifteen main/room `WayPoint` records as typed C++ data. The web fields
retain the exact serialized spellings and meanings recovered from
`CWebGrabPoint::ProcessUserAttr` at `0x00327320`: linked direction control
point, rope length, `VisiableLength`, vertical/horizontal angles, exit speed,
control lock, target waypoint, and target slide. `CWebGrabPoint::Init` at
`0x00327470` resolves the optional waypoint position; the reconstruction also
resolves each linked `CamCtrlPoint` direction without discarding the original
object IDs. Waypoint records retain their two outgoing links, gravity,
standability, jump direction, travel time, camera-area link, enabled flag,
and electric-shock flag. This includes the authored 429-to-430 slide chain
and forced exits from grab points 383 and 443.

`WebGrabPointRuntime` reconstructs `Player::GetBestWebGrabPoint` at
`0x00344424`, `GetClosestWebGrabPoint` at `0x00344648`, and
`SearchWebGrabPoint` at `0x0034486c`. Candidates must have an unobstructed
collision-mesh segment from half a player radius above the scene anchor and
a normalized facing dot product greater than the recovered `0.2` constant.
The nearest candidate wins, after which its authored visible length is
checked exactly once; the red/green hint fallback remains separately
available as the closest line-of-sight point. Deterministic tests cover
facing, current-point rejection, nearest selection, the post-selection
range rule, and triangle occlusion.

The native player now follows the recovered traversal graph: state 17
`k_state_swing_web_throw` chooses `jump_to_throw_web_left/right`, state 18
`k_state_swing_hang` chooses `swing_hang_fwd_left/right`, and state 19
`k_state_swing_idle` chooses the matching release family. This distinction is
important because animation IDs 138–140 belong to slider-fall motion 25, not
the swing states. Circle press invokes the recovered grab search while
airborne; release advances to motion 28. SoundConfigs 9, 12, and 13 provide
the three web-throw variants, swing-start cue, and swing-end cue through the
predecoded XAudio2 player-state path.

`WebSwingRuntime` projects Spider-Man into the vertical plane supplied by the
grab point's linked direction, constrains him to the authored rope length,
and integrates the pendulum with the 120-based acceleration recovered from
`Player::GetPalstance` at `0x00341ff8`. Release uses the authored `OutSpeed`
times the recovered 1000 ms scale and preserves motion 28's doubled vertical
component. The gameplay API exposes only world-space strand endpoints; a
depth-tested, alpha-blended D3D11 line pass reconstructs the original
`CobWeb`/`CTexLineSceneNode` visual without leaking Direct3D types into game
logic. Core regressions cover throw, hang, release, rope constraint, sound
states, and ballistics, while WARP compares a captured web-strand frame.

Forced exits follow the linked-WayPoint branch in `Player::UpdateMove`
(`0x00350dd2`-`0x00350f94`). When `CWebGrabPoint+0x220` identifies a target,
the original changes to state 19, subtracts the release animation's root
displacement, divides the remaining displacement by the animation length,
and caps the resulting horizontal speed at 3000 cm/s. The reconstruction
now combines that duration-derived velocity with the recovered per-frame
`Dummy_center` displacement. The distinction is required by Level 3 point
30352: the release clip rises through a large mid-animation arc, clears the
far roof facade, and then lands at the linked waypoint. A straight chord to
the waypoint collides with the facade and leaves motion 28 latched at the
roof edge. Core coverage verifies the recovered mid-clip root position and
the final target; the chronological Level 3 route verifies the collision-side
landing. This also preserves the authored exits from points 383 and 443
without substituting ordinary run-speed steering.

`Player::CanEnableTriggerRestore` (`0x0033ff14`) excludes motions 26 and 27
(web throw and swing hang), death-class states, and dead players. Applying a
restore volume during the constrained swing had incorrectly returned the
Level 6 Room 7 route to its previous checkpoint. The portable restore runtime
now separates eligibility to *start* a recovery from advancement of an already
active recovery. Core tests check that distinction. The continuous Level 6
route now traverses its three authored grab points and intervening cable,
lands at checkpoint 41349, and enters the shipped bridge presentation without
a diagnostic teleport or synthetic floor.

## Level 6 collapsible bridge sections

The Room 8 capture revealed a loader omission, not a camera/FOV problem:
`CLevel::LoadNextObject` (`0x003853fc`) constructs `CBrokenBridge` for the
entire `BrokenBridge` prefix. The portable factory now includes all 19
authored S/M/L/A/B sections, their meshes and animation banks, and their
`bbox` transmission colliders. `CBrokenBridge::CBrokenBridge`
(`0x00301af4`), `Init` (`0x0030209c`), and `SetState` (`0x00300dec`)
explicitly select `idle` and register collision regardless of the IRR
node's `Collision=false`; treating these as generic static scene nodes is
insufficient.

`ProcessUserAttr` (`0x0030081c`) supplies the type, shake times, drop distances,
angles, durations, proximity distance, and linked-car speed. The portable
state sequence follows `Update` (`0x00301328`), `SetNextState` (`0x003012d4`),
and `CheckNestState` (`0x003012f4`). Type 2 starts its final fall on XY
proximity, including an airborne player. Other types begin on player contact;
type 1 receives both staged drops, while types 0/3 skip the second after
state 6. Shake animation, native camera-shake requests, Vox event `0x86`,
the nine authored smoke/rock splash placements, final -4000 cm/s fall with
1000 cm/s-squared gravity, visibility/collision removal, and checkpoint reset
are represented. Live state, remaining seconds, vertical velocity, and
phase-change events are logged. Core tests exercise every section's complete
state lifecycle and its actual collision height. The normal Level 6 route
crosses the first sections after the bridge presentation.

This is not yet a complete bridge-chase port. `GetSlideCarList`
(`0x00301da0`) and the linked `CSlideCar`/`CAreaDamage` motion and contact
propagation remain to be connected. In particular, the factory currently
loads the previously supported bus but not the other `SlideCar_*` vehicles.
The later vehicle-bearing sections must not be called verified gameplay.
The native transmission-body carry contribution to player displacement also
needs comparison before claiming complete moving-platform behavior.
The current non-type-2 activation uses the portable capsule-expanded
authored bounds as a contact approximation. The native test is physics
context flag `0x100`, including contacts on linked cars and damage areas;
that distinction remains open and is not covered by the first-crossing pass.

The next vehicle work has a concrete native sequence. `CSlideCar::Init`
(`0x0031bea0`) records its reset transform, starts state 0, and optionally
replaces layer zero with `cars_02.tga` in high-quality mode. The bridge's
`GetSlideCarList` picks cars and damage areas by their original XY positions
inside its rectangle and moves their bases to the bridge's top. State 4
updates their height and rotation with the tilting section; state 5 launches
cars at `CarRunSpeed` along rotated +X, reversing it for type 3. A car enters
state 2 at that launch. `CheckOnBridge` (`0x0031bdb0`) detects leaving the
saved rectangle or the bridge becoming invisible, and selects state 3 with
an additional -100 cm/s vertical impulse. `Update` (`0x0031be48`) then
uses native contact flags to select state 4 and the Unit removal path.
`ResetObject` (`0x0031bd7c`) restores state 0 and clears that removal flag.
These details are RE findings for the next implementation, not claims that
the vehicle sequence is already active in the portable runtime.

The rider path is also identified. `Unit::GetPhysicsContextFlags`
(`0x00322b20`) reads the physics context bitmask, not a scene-node bounds
test. `Unit::UpdateTransmission` (`0x00323388`) examines `0x80` manifold
contacts, tests both oriented contact normals against the native ground
threshold `0.70710677`, and records the supporting physics body. It sends
messages `0xd2`/`0xd1` when changing support and clears a previous support
without its `0x8000` persistence bit. `PhysicsEntity::preUpdate`
(`0x003d799c`) adds that body's linear/displacement velocity and its angular
contribution at the rider's relative position before updating the rider's
physics position. `Unit::UpdatePhysicsPosition` (`0x003243cc`) then copies
the result back to the render unit. This is distinct from snapping feet to
the current road height; the portable ground-height query currently does
not preserve this supporting-body/contact information. These functions are
the starting point for the remaining carry/contact reconstruction.

Comic-cover notices follow `CComicCover::RenderTip` at `0x00304228`,
`CTutorial::AddCoverInfo` at `0x0038c77c`, and
`CTutorial::RenderComicCoverInfo` at `0x0038c434`. The collected index is
stored as cover state but is never substituted into Main string `0x24a` or
`0x24b`; the previous trailing number was reconstruction-only and has been
removed. Native `CFont` consumes the strings' `^N` color controls, so the
single-color Windows notice strips those nonprinting tokens rather than
displaying them as text.

The notice renderer now also follows `RenderComicCoverInfo`'s sprite choice:
`tutorial.bsprite` frame 0 for the first five-second explanation, frame 15 for
the subsequent three-second reminder. It is explicitly a portrait-free
information panel, not a dialogue message or a modal tutorial. Both retain the
user-requested bottom placement. Core tests cover panel selection, expiry, and
return to portrait dialogue; WARP tests require the notice pixels to equal the
corresponding shipped tutorial panel, rather than accepting any changed text
pixels. Both process-local collectible captures were visually inspected after
the change; the former Windows-font rectangle is gone.

## Authored slider traversal

`LevelOneBootstrap` also materializes both level-one `Slide` objects as typed
`LevelSlideAsset` graphs. `CSlider::ProcessUserAttr` at `0x0031ce6c` supplies
the enabled, electric-shock, and entry-waypoint fields; `CSlider::Init` at
`0x0031e7cc` follows the first waypoint link. The resulting graphs preserve
Slide1038's 429-to-430 roof run and Slide1039's 445-to-446 exit run.

`LevelSlideRuntime` reconstructs the renderer-independent segment work from
`CSlider::Update` at `0x0031d920`. The nearest-segment search first applies
the recovered 640000 cm² outer radius, then requires the XY projection to be
within 50 cm. An ordinary candidate cannot be more than 30 cm below a player
who is above it, while a downward-falling player can catch a rope as much as
800 cm overhead. The final 200 cm of a terminal segment rejects an ordinary
catch; only web-release state 19 and slider-jump fall state 21 bypass that
terminal rejection. The exact eligible states are 0, 4, 14, 15, 16, 19, and
21. The runtime also preserves velocity, linked-segment switching, and final
waypoint gravity/electric metadata. Airborne player states can now enter state
23 (`k_state_trigger_slider_land`), advance through the authored
`fall_to_slide` clip, and loop state 22 (`k_state_trigger_slider_move`) with
its looping Vox 77 slide sound. On catch, the native update replaces the
current velocity with its 800 cm/s literal at `0x0031dd18`-`0x0031dd3a`;
only state 21 preserves the magnitude stored at `Player+0x454`. Forced web
releases retain their linked waypoint destination long enough for grab point
443 to feed waypoint 445 and the second authored slide, but do not make that
slide run faster. At a terminal waypoint, the native update preserves
the segment's horizontal body velocity in `Player+0x478/+0x47c`; the
`UseGravityWhenEnd` byte at waypoint offset `+0x29` selects the outgoing
vertical component. The portable handoff now retains that momentum instead of
dropping vertically at the endpoint; a no-gravity endpoint already touching
the receiving platform is reconciled through the same 75 cm controller step
support used by ordinary ground motion, matching the native collision body's
contact at the lip. Deterministic tests cover graph extraction, broad and
planar proximity, vertical and terminal gates, projected catches, segment
switching, terminal metadata, player state transitions, and audio dispatch.
The cinematic `StartSlide` command follows its preserved implementation at
`0x003701c0`: it validates that both named waypoint endpoints exist, while the
independent `CSlider` update performs the actual airborne proximity catch.

The end-to-end traversal probe logs both slide and waypoint asset graphs,
observes states 23 and 22 on each authored route, requires the resolved
`SFX_SLIDING` event, and verifies both terminal waypoints. Trigger 1149's
modal slide guide is captured at the bottom of the screen with the Xbox A
glyph; after acknowledgment, the same jump resumes the route. Slide1039
reaches waypoint 446 without activating restore volume 554. The determining
detail is the native world-space broadphase inside `obbox::test_obb`, not a
slider-state exception or an invented trigger bypass.

The uninterrupted post-crash route now continues from that landing through
the Room11 street encounter and Room9's five-enemy cohort. Cinematic 1155
hides web walls 1151 through 1153 and saves checkpoint 30032, after which the
route follows Room12's WebGrabPoints 1161 and 451 and uses state 30 for the
final gap. Trigger 1252 runs before trigger 1174; successor 1256 places the
player inside 1174, whose while-inside cinematic 1175 enables the boss. The
probe defeats boss 1199 and follows cinematics 1215 and 1238 through the
level ending without a diagnostic teleport.

## Authored encounter camera tracks

Eight level-one encounter cinematics use type-2 camera threads instead of a
Collada camera animation. `CinematicCameraTrack` reconstructs
`CCinematic::initCameraCurve` at `0x0036f2dc` and
`CCinematic::updateCameraThread` at `0x0036e9ac`. Each `ChangeCamera` command
stores a target, direction, and signed distance; the original materializes
the position as `target - direction * distance`, then independently blends
the target and position tracks. Intervals of 50 ms or less are held as hard
cuts. Longer intervals are linear unless the current command's `curve` flag
selects the recovered cyclic Catmull-Rom/Hermite path.

The bootstrap validates and retains these portable tracks alongside all 42
runnable level scripts. During native encounter playback the application now
uses the authored camera pose until the cinematic ends, then returns to the
active gameplay `CameraArea`. Tests cover all eight shipped tracks, signed
distance reconstruction, linear interpolation, and authored hard cuts.

## Controller-native quick-time events

`ButtonConfigDatabase` follows `ButtonConfigFile::LoadConfig` and
`ReadBasic` at `0x002fb9b4` and `0x002fb8d4`. It preserves all twenty shipped
`BCONFIG.bin` records, including interaction type/value, screen position,
duration, sprite animation IDs, required action count, and compound sequence.
The level-one QTE uses record 6 (`k_igm_button_qte_1_2`) with its authored
3900 ms response window.

`QuickTimeEventRuntime` reconstructs `CCinematicThread::StartQTE` at
`0x0037153c`, `Player::BeginQTE` at `0x0034c9b4`, and the success/failure
cinematic dispatch from `CQTEManager` at `0x0038a5e4` and `0x0038a630`.
Because the mobile interaction is a positional touch gesture, the native
controller route deliberately maps the abstract QTE action to XInput A. A
press selects authored success cinematic 20006; expiry selects authored fail
cinematic 20010. `InterfaceControl` now also preserves the original
`ControlEnable` and `BlackEnable` state, so gameplay input and encounter
triggers cannot interfere while the QTE owns control.

The source cinematic ID is retained from `CCinematicThread::StartQTE` and
removed before the outcome is dispatched, matching `CQTEManager::SuccessHandle`
and `FailHandle`. `CQTEManager::EndQTE` (`0x0038a5b0`) restores player input
and calls `Application::ResetSlowMotion` (`0x003e0598`); it does not change
the independent HUD, arrow, bands, or skip flags. This distinction matters
for Level 6's floor-break chain `41428 -> 41464/86 -> 87`.

Collada camera teardown is separate: `CCinematicThread::UnUseDAECamera`
(`0x00370710`) calls `OnOffDaeMovieUI(false)` (`0x0037043c`), which restores
controls and the HUD, removes cinematic bands and skip UI, and leaves the
objective-arrow flag unchanged. Starting `PlayDAECamera` calls the same
function with `true`; later `InterfaceControl` commands can override those
defaults. The portable lifecycle now handles both ends, including QTE
interruption. `assert_gameplay_ui` and `frames.csv`'s `cinematic_letterbox`
column prevent normal-flow checks from accepting an invisible HUD or stale
cinematic bands.

`PlayDAEAnim` (`0x003709ec`) reveals its bound scene object and deactivates a
Unit's physics while the Collada animator owns its pose. `UnUseDAEAnim`
(`0x00370624`) leaves the original scene object visible and reactivates its
physics after popping that animator. This is an actor lifecycle operation,
not a render-only replacement: Level 6's initially hidden enemy 89 must
remain visible and active after its floor-break animation. The same visibility
handoff is retained for ordinary level objects.

## In-level Collada cinematics

The intro's source-level Collada camera/actor path now covers the three other
level-one `PlayDAECamera` streams: before-boss cinematic 1254, ending 1238,
and campaign-end cinematic 1267. Bootstrap resolves the authored camera and every
`PlayDAEAnim` target back to its named scene node, loads ten cutscene actor
animations, preserves delayed starts (Sandman at 15,700 ms and Rhino at
28,650 ms), and extends playback until both the CFF commands and all Collada
tracks finish. The far-plane override and the `level end`, `game end`, and
successor fields remain typed on each `LevelCinematicAsset`; 1254 therefore
continues into authored cinematic 1256 after playback.

Clip selection follows the preserved playback path rather than treating a
whole BDAE file as one animation. `CCinematicThread::PlayDAEAnim` at
`0x003709ec` reads `clipID` and selects that clip on the pushed actor animator;
`CCinematicThread::PlayDAECamera` at `0x00370db0` forwards the same field to
`AnimCamera::SetClip` at `0x002f198c`. The latter delegates to
`CTimelineController::setClip` at `0x0042928c`, which resets current time to
the authored clip start and sets playback duration to `end - start`.
`CinematicActorAsset` and `CinematicCamera` now retain those exact ranges,
sample at `clip.start + localTime`, and complete at the selected clip end.
This is material in shipped data: Level 5's opening web-rope BDAE is 30,299 ms
long, but selected clip 0 ends at 8,666 ms. Using the entire file previously
prevented successor cinematic 18 and controls from returning. The regression
now observes cinematic 17 ending at that authored interval, cinematic 18
starting, room trigger 40948 launching conditional cinematic 40941, and slide
40662 becoming enabled after enemies 40841-40845 die. Autoplay records both
whole-file and selected-clip timing for every Collada camera and actor so this
class of mismatch remains directly auditable.

D3D11 uploads these actors as hidden dynamic meshes. During playback it
updates only the active cinematic set, replaces matching persistent player or
enemy meshes, samples the authored animated camera, and restores gameplay
actors afterward. Gameplay controls and enemy simulation are suspended while
a full Collada cinematic owns the scene. A WARP regression renders cinematic
1254 before and after the delayed Sandman entrance.

### Level 5 continuous player-flow regression

`level5-full-progression.usmauto` now exercises the entire shipped Level 5
route without diagnostic teleports or direct health changes. It begins with
cinematic 12/17/18, climbs the first wall, follows the room-owned slide and
three-WebGrabPoint traversal, clears every exposed symbiote cohort through the
ordinary player attack graph, and requires each conditional destruction
watcher to remove or unlock the next authored obstacle.

The final Room 8 route is derived from its collision/navigation meshes and CFF
graph. Spider-Man climbs `wall08_PIVOT`, crosses Object1540's 283 cm roof step,
defeats objects 20354/40957/40958/40984/40985 so cinematic 40974 selects the
checkpoint camera and objective arrow, and crosses the disconnected roof on
WebGrabPoint 40698. Entering trigger 59 starts cinematic 60 and successor 62,
which move Spider-Man and SecurityRobot 40846 to the final roof. Destroying
that 1,600-health robot runs watcher 40976 and exposes robots 40977/40978;
their destruction starts cinematic 15 and its native level-end flag. The
151-step process-local run reaches that flag with positive player health, and
captures each transition for visual inspection before its BMPs are pruned.

## Localized cinematic and tutorial UI

`LocalizedStringTable` parses the original newline-delimited key maps and
offset-indexed UTF-16LE language payloads from `xlsStrings.pack`. The native
bootstrap loads all 18 level-one dialogue strings and 17 tutorial strings in
English without embedding copyrighted game text in the repository.
`CinematicUiRuntime` follows `CCinematicThread::OnTutorial`, `ShowMessage`,
and `InterfaceControlCmd` at `0x003710d4`, `0x003725f8`, and `0x003711e0`:
timed messages expire independently, persistent tutorials dismiss through the
controller, black-screen tutorials dim gameplay, and cinematic black bands
follow the authored interface state. On Windows, the English touch/Xperia
instructions are adapted to the routed XInput actions. Supported `[A]`,
`[B]`, `[X]`, `[Y]`, `[LB]`, `[RB]`, `[LT]`, and `[RT]` tokens render from the
supplied `xboxButtons.png` atlas; the text labels remain the fallback when that
external texture is missing or invalid.
`CTutorial::AddInfo` (`0x0038dea0`) marks tutorials with `Timer < 1` as modal,
and `CLevel::Update` (`0x003820bc`) returns before world, player, cinematic,
enemy, and physics updates while one remains visible. The portable game loop
now makes the same distinction: persistent full-screen prompts stop the world,
whereas timed context prompts leave simulation running.

`ShowMessage` now follows the original `CTutorial` path rather than the earlier
temporary subtitle strip. `CTutorial::GetFrameIdByFace` (`0x0038b98c`) selects
the portrait from shipped `tutorial.bsprite`; `AddMessage` (`0x0038d7b4`)
wraps the localized string to 350 virtual pixels and pages it two lines at a
time; `ComputeTimeForEachPageOfMessage` (`0x0038ba6c`) apportions the authored
timer; and `RenderMessageInfo` (`0x0038be54`) assembles panel frame 1 and draws
the text with shipped `font_normal_white.bsprite` on the native 480x320 canvas.
The ordinary `Tutorial` command follows `CTutorial::AddInfo` (`0x0038dea0`)
and `CTutorial::Draw` (`0x0038e648`): it wraps at 390 virtual pixels and
selects the shipped single- or multi-line tutorial panel. The Windows
presentation anchors these context panels eight virtual pixels above the
bottom edge and replaces supported bracketed controller labels with Xbox
glyphs; Android touch icons are intentionally omitted. Numeric inline color markup is
still accepted for informational prompts. System-text rasterization is now
isolated to non-panel fallback prompts. The game-facing state contains no
DirectX or Windows handles. WARP regressions verify the native message and
tutorial panels, fonts, the blue Xbox X glyph, page transition, bands, and QTE
prompt.

Enemy cinematic health bars are reconstructed from
`CCinematicThread::ShowHealth` (`0x00370fb8`),
`CLevel::RegisterHealthBar` (`0x0037dfec`), and
`CLevel::ShowHealthBarOfEnemy` (`0x00387548`). The renderer uses the original
`interface.bsprite` surround, right-to-left fill, and enemy portrait frames.
The 480x320 anchor `(435, 32)` comes from UI item `0x15` in
`CreateAllItems_3x2` (`0x002eed48`); level one therefore displays distinct
authored Hammer Thug and Sandman bars. Cinematic `GetDamage` applies its
authored `DamageValue` to portable player state, including the QTE-failure
script's 200-point hit and normal hurt-audio dispatch.

Non-Collada cinematic player commands now remain source-level state changes as
well. `GameplayPlayer::applyCinematicCommand` resolves the authored player
thread and implements `DisableAI`, `EnableAI`, `SetAnim`, `MoveObject`, and
`GetDamage`. While `DisableAI` owns the player, regular controller locomotion
is suspended, named clips advance at their scripted speed/loop mode, and each
absolute position/quaternion rebuilds the native world transform. The QTE
success and failure branches therefore reproduce their scripted knockback,
landing, recovery, and final positions instead of leaving the gameplay player
idle underneath the recovered camera.

## Adaptive level music

Level-one music follows `CAIEntityManager::Update` at `0x00376444` rather than
starting an arbitrary soundtrack at application launch. The recovered first
entries of `LevelSound::levelSoundCalms` and `levelSoundActions` select Vox
records 4 and 5, `M_DOWNTOWN_CALM` and `M_DOWNTOWN_MIXED`; an active boss with
enemy type 16 selects record 22, `M_BOSS_SANDMAN`. Player death selects record
1, `M_LOSE`, as in `CLevel::UpdateBlackScreen` at `0x003805f0`. The Vox mode
field at `+0x2c` distinguishes the looping level/boss records (`3`) from the
one-shot loss cue (`4`), and all native transitions use a 500 ms fade.

`LevelMusicRuntime` keeps those decisions independent of the Windows backend.
The XAudio2 path decodes the four configured events up front and verifies that
the downtown calm and mixed tracks have identical sample format and frame
count. Both downtown voices then start together, with one silent, so combat
crossfades preserve the original music cursor instead of restarting the song.
Boss and loss tracks fade in as named voices while the two synchronized base
tracks fade out. Deterministic tests cover the exploration, ordinary combat,
Sandman, return-to-calm, and death transitions.

`TGameSetting::Reset` at `0x003e268c` initializes both profile-backed volume
controls to `0.9`. `CGameProfile::Load` applies the music value to VoxSound
group 1 and sound effects to group 2 through
`VoxSoundManager::SetGroupVolume`. `GameAudioMix` preserves those recovered
defaults as the native profile state. The Windows output applies a separately
named -6 dB music trim requested during port testing; this host calibration is
not represented as an original VoxSound constant. Named sound-effect, unnamed
PCM, and spatial paths retain the recovered `0.9` group value. Harness audio
events log the effective output volume, including the additional half-gain on
native trigger-sound record `0xa1`.

## Slow motion and authored camera shake

The global presentation timing path follows `Application::SetSlowMotion`,
`UpdateSlowMotion`, and `ResetSlowMotion` at `0x003e0690`, `0x003e05f0`, and
`0x003e0598`. `SetSlowMotion` commands scale the complete portable game-state
update by their authored denominator, hold for `TimeOn`, and linearly return
to real time over `TimeOnToEnd`. The reconstruction preserves the original
one-millisecond update on the ramp-completion frame. Rendering and XAudio2
continue on real time, matching the original application's separation of
state and sound updates. VoxSound IDs `0x186`/`0x187`, recovered as
`SFX_SPIDER_SENSE_IN` and `SFX_SPIDER_SENSE_OUT`, provide the optional
authored transition cues.

`ShakeCamera` and `StopShakeCamera` reproduce
`CGameCamera::StartShake`, `UpdateShake`, and `StopShake` at `0x002f2098`,
`0x002f20cc`, and `0x002f20c4`. Every original 50 ms update alternates the
offset sign, decays it by frames remaining over total frames, and applies the
authored X/Y/Z rates to camera position without disturbing its target. Native
rendering holds each recovered offset between those ticks so the effect keeps
its original duration on higher-refresh displays. Deterministic regressions
cover command validation, hold/ramp timing, transition audio cues, alternating
decay, and explicit shake cancellation.

## Cinematic particle effects

`EffectPresetDatabase` reconstructs the particle records loaded by
`EffectManager::LoadEffectPresets` at `0x003925c4` from the original
`effects.xml`, `effects.bsprite`, and shared `effects.tga` atlas. The portable
runtime handles `CCinematicThread::PlayEffect` at `0x003712f8`, preserves each
authored emitter's delay, lifetime/restart ranges, box, direction and three
angle ranges, size/color/speed variation, gravity, fade, size curve, spin,
attraction, pivot rotation, sprite frame, and material type, and exposes only
renderer-neutral billboard state.

`CFpsParticleBoxEmitter::deserializeAttributes`/`emitt`
(`0x0039c678`/`0x0039ccd8`) establish the native rate clamps, half-extent
box, strict emission interval, rounded batch cap, and per-particle random-call
order. Effects use the recovered `irr::os::Randomizer::rand` recurrence rather
than the former subsystem xorshift. `CFpsParticleSystemSceneNode::clone`,
`Restart`, `SetRandomLifeTime`, and `doParticleSystem` (`0x003a0c78`,
`0x0039fb4c`, `0x0039fae4`, `0x0039f34c`) supply the two lifetime
selections for thrown effects, negative-delay pre-roll, first-frame behavior,
150 ms delta rejection, finite/restarting emitter lifecycle, and strict
particle expiry. Core regressions pin the hit splash's exact initial
particle count and final randomizer state. The effect runtime can accept the
application-owned generator, but live integration remains deferred until the
native active-room emitter construction order is recovered; passing it while
all portable persistent emitters are active would consume the shared combat
stream in the wrong order.

The affector math follows the preserved implementations rather than treating
the XML values as generic forces. `CFpsParticleGravityAffector::affect` at
`0x0039d7d4` captures the incoming velocity and linearly reaches its target
over the configured lifetime interval. `CFpsParticleSpinAffector::affect` at
`0x0039e9c4` selects a total billboard angle for that interval.
`CFpsParticleRotationAffector::affect` at `0x0039df08` rotates particle
positions about its pivot using degrees per second. Emitters without a gravity
affector retain their velocity instead of being damped toward zero.
`CFpsParticleAttractionAffector::deserializeAttributes`/`affect`
(`0x0039bc7c`/`0x0039bda0`) apply the authored point, speed,
attract/repel flag, and per-axis mask. The point is translated by the thrown
effect root in `EffectManager::InitEffect` (`0x00391d90`), then each tick
moves particles by normalized point displacement times elapsed seconds and
speed. This restores the 800-unit inward motion in both red and black
super-web splashes. Affectors execute in serialized XML order; they are not
regrouped or sorted by kind or lifetime percentage.

The room loader also instantiates all 23 authored `CEffect` nodes across level
one. Their `SysMinLifeTime = SysMaxLifeTime = -1` emitters run continuously at
the preset particle rate, retain one-based room ownership for frustum/room
visibility, and cover the five persistent fire/smoke presets used by the
scene. Presets may contain several `FadeOut` color affectors. These are kept as
separate time intervals and evaluated in serialized order, matching
`CFpsParticleFadeOutAffector::affect` at `0x0039d2a8`; this preserves authored
transparent-to-opaque fade-in, color hold, and fade-out stages instead of
collapsing them into a permanently transparent final stage.

The D3D11 backend expands those states into camera-facing sprite quads and
separates standard vertex-alpha particles from `trans_add` particles. Ghidra's
preserved
`CCommonGLMaterialRenderer_TRANSPARENT_ADD_COLOR::onSetMaterial` at
`0x004559b0` calls `glBlendFunc(GL_SRC_ALPHA, GL_ONE)`, which maps directly to
the native additive blend state. A dedicated effect pixel shader retains the
world vertex shader's full interpolant signature; the HUD shader cannot be
shared because its compact signature assigns texture coordinates and color to
different compiled registers. WARP regressions verify preset decoding,
deterministic motion and expiry, and visible frames for all three effect types
authored by level one: `cartoon_hit_splash_big`, `explode_new`, and
`rock_splash`.

Particle width and height remain independent of the emitter scene-node scale.
`CFpsParticleSystemSceneNode::render` at `0x0039ff5c` reads the two dimensions
directly from `SFpsParticle` offsets `0x50/0x54` while submitting the billboard
vertices with an identity or translation-only world transform. The runtime
therefore applies the authored `ParticleWidth`/`ParticleHeight` and size-affector
targets without multiplying them by `Scale`. In particular, `big_firesomke`
now follows its native 100-to-230-unit smoke curve instead of producing the
incorrect 500-to-1150-unit red sheets. Core tests pin the decoded dimensions,
and a mature WARP fire/smoke readback bounds the effect's screen coverage.
Size affectors are also retained as an authored-order list rather than
collapsed into one emitter-wide target.
`CFpsParticleSizeAffector::affect` at `0x0039e41c`
captures the size at each interval boundary and selects that affector's target
variation once. This restores the authored 0–10% and 10–100% stages used by
`bigfire_xp` and `fire_on_wall` smoke, including `bigfire_xp`'s deliberate
serialization of the 10–100% growth stage before its 0–10% initializer.

## Room visibility and terminal cinematics

`CLevel::UpdateRooms` at `0x00381f24` combines the active camera frustum with
each `CameraArea`'s authored `mustInVisibleRoom`/`mustVisibleRoom` masks and
the cinematic room override. `CCinematicThread::MustBeVisibleRoom` at
`0x00370f0c` supplies that final override during the opening sequence. The
native bootstrap preserves one-based room ownership for geometry, objects,
and enemies; D3D11 tests each room bounding box against the current clip
volume and applies the resulting visibility to all three groups. This avoids
rendering disconnected level sections while retaining the opening's explicit
rooms 1-5 override.

Triggers retain their one-based room owner from the linked scene that supplied
them. `LevelTriggerRuntime` evaluates a trigger only while that room is active,
matching the original room-child update gate instead of running all 27 linked
room triggers globally at level start. When the intro releases its explicit
room override, visibility is immediately recalculated from the gameplay camera
before the first gameplay trigger update.

Player death does not start cinematic 1267. `Player::CheckDeath` at
`0x0034cfcc` selects state `0x80`; the player vtable entries at `+0x140` and
`+0x144` resolve to `Player::IsDead` (`0x00340084`) and
`Player::IsDeadOver` (`0x003400a0`). `CLevel::Update` at `0x003820bc` checks
`IsDeadOver` before ordinary level updates and initializes its own 2000 ms
`CBlackScreen`. `CLevel::UpdateBlackScreen` at `0x003805f0` disables controls,
stops current sounds, starts Vox record 0 (`M_LOSE`) with the native 500 ms
fade, and pushes `GS_Confirmation` message `0x30` when the fade reaches state
2. The portable `LevelDeathRuntime` preserves that path independently of
`CTriggerRestore`; deterministic traces require state 128, the death-screen
alpha/timing, confirmation readiness, `M_LOSE`, and the absence of a 1267
start.

The confirmation outcomes now retain their native restart split.
`CLevel::DoConfirmation` at `0x003836f8` sends Retry through
`RestartAtLastCheckPoint`; an existing checkpoint follows
`CLevel::RestartAtCheckPoint` (`0x0038344c`) and restores the serialized
runtime before applying the checkpoint's linked-waypoint, saved-transform, or
authored-node placement. With no checkpoint, `CLevel::RestartLevel` at
`0x00382b10` reloads the authored initial state and enqueues the player node's
linked cinematic. Level 1 links to 1265, so this branch does not replay the
1264 setup wrapper. Deterministic probes cover both branches, including exact
player/camera placement, health, cleared death state, and the 1265-to-1266
intro replay.

Selecting No pushes `GS_ExitMenu` mode 3 rather than opening another menu.
`GS_ExitMenu::Update` at `0x002c0150` advances once per 50 ms tick, clears the
failed state stack at state 16, creates `GS_MainMenu` through
`HandleGoToMainMenuWhenFail` (`0x002bfcb8`) at state 17, and removes the
transition at state 20. `GS_ExitMenu::Render` at `0x002c0020` supplies the
150-plus-state-times-7 black fade, then renders Main strings `0x13` and
`0x26a` at authored UI item `0x1b` (`385,283` in the 480-by-320 virtual
layout) with the packaged `font_outline_big` sprite. Core, WARP readback, and
autoplay tests pin the timing, stack suppression, label pixels, and terminal
main-menu request.

Cinematic 1267 is the separate authored `Cinematic_Gameover` campaign-end
presentation. The only non-constructor write to `CLevel+0x254` is in
`GS_Loading::Update` at `0x002c0418`, gated by `GS_Loading+0x114`.
`GS_ExitMenu::HandleGoToGameEnd` at `0x002bfdfc` sets that flag while creating
a level-zero reload, after which `CLevel::InitAfterRoomInit` launches the
player's `^EndGame^Cinematic`. Its Collada camera, four actors, sound/dialogue
commands, and 55-second `Transport` command run through the portable
cinematic path as an independent regression. The level-ending cinematic 1238
likewise retains its delayed Rhino entrance and final pose until
`CLevel::End`/`GameEnd` would leave the original level state; the Windows
first-level target presents that last frame and exits cleanly. WARP
regressions render both terminal sequences with their object-visibility
commands and camera-area masks applied.

Ordinary interactive play still leaves terminal state immediately. During an
autoplay run, however, the application keeps that immutable state alive until
the scenario completes, allowing assertions and ordered readbacks at the late
timestamps instead of treating process exit as evidence. The terminal probes
cover every authored command and named play/stop audio request in both 1238
and 1267. The death probe instead verifies the separate native black-screen
and confirmation path; a sky/ground frame after health exhaustion is not used
as evidence for death behavior.

## Native combat effects and ultimate timing

Normal-suit attack trails are driven by the packaged `MCHitEffects.bin`
records and their referenced Collada meshes rather than by generated desktop
particles. `Player::SetNextStateId` installs the ordinary attack effects and
the ultimate sequence's IDs 24, 22/23, and 25.

The complete native material-ID chain is now resolved. OpenGLES driver
`createMaterialRenderers` (`0x00443d70`) installs 27 built-in renderers, so
the first custom renderer registered by `Application::Init` (`0x003e1c78`)
is ID `0x1b`; `CMaterial::prepareMaterial` independently confirms that base by
assigning `0x1b` to the first registered `nontransparent_alpha_test` type.
The third and fourth registrations are consequently:

- `0x1d`: `ADDITIVE_MODULATE_NONTRANSPARENT`; its renderer at `0x00396f68`
  selects `GL_MODULATE`, enables blending, and installs
  `GL_SRC_ALPHA, GL_ONE`.
- `0x1e`: `TRANSPARENT_ALPHA_CHANNEL_WITH_VERTEX_ALPHA`; its renderer at
  `0x00397780` modulates texture RGBA with primary `COLOR0`, installs
  `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA`, and performs `GL_GREATER` against
  `SMaterial::MaterialTypeParam` at `+0x4c`.

The previous association of these IDs with the later ambient add/subtract
renderers was incorrect: those appear at IDs `0x24` and `0x25`. The D3D11
path now reproduces the actual `0x1d/0x1e` blend and alpha-test states and no
longer synthesizes coverage or applies ambient RGB arithmetic. The BDAE asset
census records `SEffect+0x2c`, which `CMaterial::prepareMaterial`
(`0x0041ca8c`--`0x0041ca9e`) copies to the alpha reference; all 57 material
records across the 31 shipped hit-effect meshes author `0.0`. The production
autoplay trace records every effect ID, bone, lifetime, scale, follow mode,
and native material selector, while renderer regressions pin the first punch
as a non-darkening additive pass plus the ultimate wheel and ultimate-in
pixels.

The event clock is native rather than an assumed 30 FPS conversion.
`Player::CheckFrame` at `0x003403fc` maps each authored combat frame through
integer `(frame * 2) / 3` and compares the result with the animation runtime's
fixed 50 ms frame counter. Thus an authored frame 7 punch lands at 175 ms
after the rounded-frame conversion, not 200 or 233 ms.
`Player::UpdateKeyTrigger` at `0x0034d0a4` treats the combo-window
fields differently: they are already runtime-frame values and are compared
directly. Core and autoplay gates pin both the damage/effect frame and the
input window, including all four distinct fast-kick impacts.

`CAnimObjEffect::Init` at `0x00390bb8` proves two different transform modes.
A live effect is parented to its bone and inherits the complete animated bone
transform. A snapshot effect copies only the bone's absolute position, then
uses the player quaternion passed by `Player::AddHitEffect`; the bone rotation
is intentionally discarded. The renderer now follows that split instead of
twisting ordinary punch and kick trails by the limb rotation. Matrix-level
tests cover both branches before the WARP pixel regressions run.
For a live effect, `CAnimObjEffect::Update` (`0x00390a88`) advances the
parented root node's relative position with the copied PhysicsEntity velocity.
That numeric offset is therefore transformed by the live bone basis; it is not
a world-space translation applied after the bone transform.
`CAnimObjEffect::IsAlive` (`0x00390a38`) gives a selected non-looping effect
animation precedence over the duration counter. The duration can still fade
alpha in `Update`, so animated effects retain separate survival and fade
clocks rather than disappearing when the wrong one expires first.

The ultimate in/out meshes also require native morph animation; rigid node
animation alone is incomplete. `CColladaDatabase::getAnimationTrackEx`
(`0x00418348`) maps `SChannel` type `0x0e` to `CWeightEx`.
`ISceneNodeAnimator::forceBind` (`0x00429870`) matches the channel target URI
to the morph controller ID and uses the byte at `SChannel+0x0c` as the target
index after the implicit base mesh. This is materially different from parsing
the textual suffix: `Object01-mesh-morpher-weights` addresses target 1, while
the two Object04 channels address targets 0 and 1. `CWeightEx` linearly samples
one float and copies it into that bound weight (`0x00451e3c`--`0x00451ef8`).

`CColladaMorphingMesh::instanciateMesh` (`0x004206c8`) builds its target array
as the source geometry followed by every serialized target and initial weight.
`CColladaMorphingMesh::morph` (`0x00420384`) restores the base weight to one on
each render preparation; when `SMorph+0x04` is zero it subtracts every target
weight from that base, otherwise it retains base one. It then accumulates both
positions and normals with the exact weights. The portable parser now retains
the method, controller-instance URI, initial weights, target indices, and raw
channel target index. Pose evaluation performs that blend before applying the
animated scene-node transform. The red and black ultimate banks each contain
two normalized morph controllers; deterministic asset events and numeric pose
regressions cover their slot bindings and the red bank's shrink/explode ends.

Enemy contact effects are separate from those limb trails.
`CEnemy::ProcessHitInfo` at `0x00330fe4` requests `cartoon_hit_splash` for an
accepted ordinary strike and `cartoon_hit_splash_big` for native hit types
`0x79`/`0x6a` (with `0x89` normalized to `0x79`).
`Unit::AddPlayerHitEffect` at `0x00324234` attaches the effect to the target's
animated `Bip01_Spine1` node. The runtime resolves that exact world-space bone
on every accepted hit; it does not synthesize an effect on a miss or place
contact feedback at Spider-Man's origin.

Opening-thug material variation also comes from serialized BDAE data.
`CMaterial::prepareMaterial` (`0x0041c7e2`--`0x0041c84e`) reads U/V
translation, rotation, and U/V scale from the first diffuse 0x1c-byte texture
record and passes them to `CMatrix4<float>::buildTextureTransform`
(`0x00419260`) with a zero center. The portable material now stores the six
relevant row-vector affine components and the D3D11 vertex shader applies the
same matrix. This matters immediately: `thug_bat_mesh.bdae` uses the identity
matrix over the lower-right quadrant of the shared `thug.tga` atlas, while
`thug_knife_mesh.bdae` authors `U=-0.498` over the same raw UV range to select
the lower-left quadrant. The gun mesh's UVs directly select the upper-right
quadrant. `CTextureTransformEx::applyValueEx` (`0x0041a5bc`) is a separate
animated path that replaces the static matrix with the current SData matrix;
it does not add an offset to the prepared transform. Core assertions and the
asset census retain both the raw UV bounds and matrix values so later
appearance work cannot regress into a guessed tint or texture choice.
The first-room instances are fixed by scene data: objects 394 and 397 use the
knife mesh, while 395 uses the bat mesh. A symbol-reference census of every
native random-number implementation (`random`, `rand`, `lrand48`, particle
`Rand`/`NRand`) finds no call from `CEnemy::ProcessUserAttr` (`0x00332870`),
`CEnemy::Init` (`0x00332f7c`), or material setup. The native texture and color
mutation helpers also have no enemy-construction caller, and the shared thug
animation bank contains no texture-transform channels. Consequently these
opening appearances are authored, deterministic weapon variants; adding a
same-archetype random palette would not match the shipped executable.

This result is not limited to the opening trio. A 12-level IRR/BDAE census
finds 178 thug instances and exactly seven authored presentation families:
46 knife, 46 bat, 16 gun, 18 electrode, 30 molotov, 11 big/bazooka, and 11
hammer. Every instance of a family names the same family mesh and material
presentation; the seven meshes select their own atlas region or dedicated
texture. No thug node serializes a per-instance color or texture override.
`CEnemy::ProcessUserAttr` (`0x00332870`) passes the authored `MeshFile`
directly to `IAnimatedObject` and, unlike `CAnimatedObject::ProcessUserAttr`
(`0x002fd560`), never executes the unrelated high-quality yellow-car texture
replacement. The complete references to global `random` (`0x003730b0`) and
material/vertex mutation helpers contain no enemy construction or reset
caller. Core tests now census all 178 instances across all 12 levels and pin
their exact game-type-to-mesh mapping. Replacing these families with random
tints would discard shipped variation rather than reconstruct it.

Enemy-on-player presentation is a separate native path. Opening attack rows
6, 7, and 11 all serialize zero horizontal/vertical force and zero post-hit
protection, and their special-action records have empty effect names.
`IBehaviorBase::NotifyEntityAttack` at `0x003a8100` consequently sends a null
`AIHitTargetInfo+0x30` effect pointer. The first-room knife/bat contacts use
their authored swoosh, light/heavy player reaction, and hurt-state sound; a
generic hit mesh on Spider-Man would not match this build.

The three first-encounter enemies (394, 395, and 397) each serialize exactly
500 health in `levelnew_01_0_Room1.irr`. At the new-profile defaults
(difficulty 1, attack upgrade 0), `Player::SendHitMessage` applies a 1.0
multiplier. The ordinary ground chain authors state totals of 35, 35, 55,
85, 85, and 80, but `Unit::CheckAttackByPos` tests every registered contact
separately. `createEnemyPhysics` (`0x003d8980`) stores the enemy cylinder's
local center as `{0,0,radius}` at shape offset `+0x08`; native
`Physics::testPieCollision` loads it at `0x003d5462` and transforms it through
`PhysicsEntity::localToWorld` (`0x003ce64c`). Against the correctly centered
grounded type-zero thug cylinder, both state-78 40-damage pulses contact. The
exact delivered ledger is therefore
`500 -> 465 -> 430 -> 375 -> 290 -> 205 -> 125 -> 90 -> 55 -> 0`.
A production-input autoplay scenario asserts every shipped starting-health
value and this collision-complete ledger; the core runtime independently
asserts every requested and actual delta, including the final health clamp.

`Player::SendHitMessage` also scales the shared hit record's vertical force
by `0.85` immediately before every target dispatch. Because the same record
is reused, a sector hit delivers successively compounded vertical forces.
The portable dispatch path preserves this behavior. Its normal-flow gate
also guards the associated collision failures found during the audit. An
airborne enemy may not mistake an overhead shopping-center surface for floor,
and the storefront shell is resolved from its authored triangle plane rather
than an approximation made from the longest projected edge. Enemy root
displacement is committed through that collision world, matching
`Unit::UpdateDisplacement` writing through the native `PhysicsEntity`. The
combined regression launches a bat thug at the exact Room 1 boundary and
requires it to land on valid support instead of becoming an unreachable live
target below the street.

`Player::DoUltimate` at `0x0034def4-0x0034df3a` supplies exact timing that the
decompiler's recovered prototype obscures. The literal pool loads `3.0f` into
the first argument, the current primary animation length is multiplied by
three for the hold duration, the ramp is zero, and stack booleans are
`force=true` and `sound=false`. The runtime therefore applies a 3x denominator
only across that opening clip, leaves rendering and audio on real time, and
does not emit the generic slow-motion enter/exit sounds. State IDs 107-113
also reject ordinary damage, matching `Player::IsCanBeHit`, so live enemy AI
cannot interrupt the special before its radial damage and authored splash.

## Native combo scoring and retry preservation

Skill points and combo score are separate native fields. `Player::AddSkillPoint`
at `0x00340474` updates `Player+0x6f0`, while `Player::GetComboScore` at
`0x003414e4` reads the integer at `Player+0x580`. `Player::SendHitMessage` at
`0x00345fe8` applies difficulty and upgrade damage scaling, samples the
target's health immediately before and after message dispatch, clamps the
health delta to zero, and passes that measured damage to `Player::AddCombo`
at `0x00340584`. The same AddCombo call is present in
`CBullet::CheckCollisions` at `0x0035cba0` for player projectiles.

`Player+0x4fd` selects the two combo buckets; it is the ultimate-mode flag,
not an airborne-state flag. `Player::DoUltimate` at `0x0034def4` sets it and
`Player::ResetObject` at `0x0034e258` clears it. Normal damage/count occupy
`+0x568/+0x560`; ultimate damage/count occupy `+0x56c/+0x564`.
`Player::UpdateComboState` at `0x003458d0` flushes after a strict 2000 ms
timeout or the forced-finish byte at `+0x584`, applies the shipped attack-hard,
attack-upgrade, magic-upgrade, and hard-level interpolation tables, performs
the native integer truncation after each bucket, and clears the pending
counts/damage. A newly reset profile selects difficulty 1 and upgrade index 0
in `TGameSetting::Reset` at `0x003e268c`; those exact defaults drive the
current direct-level runtime.

`Player::Save`/`Load` (`0x0034078c`/`0x003407f0`) serialize the accumulated
score but not the pending bucket state. Retry adds one further rule:
`CLevel::DoConfirmation` saves the live score before restarting and writes it
back to `Player+0x580` afterward, even when the checkpoint snapshot contains
an older value. The `combo-retry-probe` saves checkpoint 755 at score zero,
earns score through actual melee health deltas, dies through authored
`TriggerRestore` 539, and proves the reloaded checkpoint retains the newer
live total.

## World-space tutorial hint

Room 2 contains one authored `Hint` node (ID 1113) linked to Spider-Man (ID
288). `Hint::ProcessUserAttr` at `0x0033da84` loads animation 0 from
`hintbb.bsprite`; cinematic 974 makes the node visible from 1900 to 2400 ms
around the spider-sense tutorial. `HintBase::UpdatePosition` at `0x0033e414`
anchors it above `Bip01_Head`, while `CSpriteInstance::UpdateSpriteAnim` at
`0x002e9c10` advances its frame-duration counters in fixed 50 ms ticks and
pauses completely while the node is hidden.

`LevelHintRuntime` preserves the linked-object ID, cinematic visibility,
paused animation clock, and exact frame selection independently of the
renderer. The D3D11 path consumes the packaged sprite atlas and texture as a
depth-tested, alpha-blended camera-facing billboard, including authored
animation and frame-module offsets. Core tests prove the recovered frames 6
and 13 on their exact 100 ms boundary; a WARP regression shows and hides the
cue through the original `SetVisible` commands and verifies changed pixels
above the player.

## Animated environment and comic-cover objects

The generic room-object path now includes all 28 authored `AnimatedObject`
nodes and all 15 `Comic` nodes. Thirteen animated nodes are owned by the intro
or an in-level Collada cinematic and remain on the dedicated cinematic-actor
path; the other 15 are persistent environment props such as hint arrows,
objective arrows, damaged cars, fire meshes, and the animated fountain water.
This division prevents cinematic actors from being uploaded and drawn twice
while allowing `SetVisible`, `SetAnim`, and `MoveObject` to address ordinary
animated props through the renderer-independent `LevelObjectRuntime`.

Comic nodes follow the preserved `CComicCover` constructor and `Init` at
`0x00304720` and `0x00304440`: they are real collectible world objects backed
by `IAnimatedObject`, not editor markers. Their shared mesh retains a stale,
unreferenced leading `book.tga` image and calls its packaged reflection map
`envmap_ringx.tga` although `entities.pack` stores `envmap_ring.tga`. Archive
loading preserves all material image indices, fills only unused missing image
slots with an already decoded view, and resolves that exact shipped alias so
the D3D11 two-layer reflection material remains intact. WARP renders an
isolated cover and verifies that its runtime visibility changes the frame.

Collection follows `CComicCover::Update` at `0x003042c0`: the runtime tests
the translated authored collection box against the player box, marks the
cover collected, hides it, and emits the packaged collection event. The
instruction at `0x003043b0` supplies Vox record `0x63`, resolved as
`SFX_SPECIAL_COLLECT`; `CComicCover::RenderTip` at `0x00304228` selects the
localized five-second first notice and three-second subsequent notice. Core
tests cover the authored bounds, index, state transition, and recovered sound
ID, while the application probe requires both disappearance and the resolved
audio event.

## Native platform movers and electrical presentation

`PlatForm` and `ElectricPlatForm` are now reconstructed as authored level
objects rather than static scenery. `CWayPointMover::ProcessUserAttr` at
`0x003269f0` supplies `Line_Speed` and `Active`; `CPlatForm::ProcessUserAttr`
at `0x0031874c` adds `ParkDuration` and `ActiveForever`. The portable state
machine follows `CPlatForm::UpdatePark`, `UpdateMove`, `UpdateBrake`, and
`ReachDestination` at `0x003186a4`, `0x00318bac`, `0x00318cec`, and
`0x00318b70`. Cinematic `EnableAI` reaches the same `TurnOn` path used by the
original, while linked waypoints, parking, braking, and rider carry remain
renderer-independent.

`CPlatForm::Init` at `0x00318964` always constructs the separate
`platform_phy.bdae` transmission even when a scene node says
`Collision=false`. Its `_11` face is the support plane at local Z 4.80043 and
its `jump_wall_*` faces form the authored vertical sides. Dynamic collision
therefore follows each moving platform and carries a grounded player by the
same frame displacement. Core tests cover activation, travel, parking,
support, side collision, and rider carry. The chronological Level 3 route
then proves the Room 7 trigger-controlled lift and the Room 8 electrical grid
using ordinary jump/web-jump input on those exact collision surfaces. Its
338-step continuation clears Room 8's encounter, returns across the four-row
landing field, rides north/south movers 30855 and 30863, crosses the central
roof, and boards east/west movers 30864 and 30865. The second handoff waits
for 30865's authored east park and uses player state 30
(`k_state_jump_web_jump`), avoiding any dependence on the earlier room-load
phase without teleporting or moving an object diagnostically.

The same no-teleport run follows the remaining static grid through the
damage-free spur 30868, crosses Trigger 31108, and requires lower CheckPoint
31088. It then acquires Room 9's `wall01`, traverses the connected climb faces
to `edge_wall01`, exits onto `Object92`, and requires upper CheckPoint 31202
beside the three authored slide entries. The accepted run completes 338/338
steps at 211450 ms with positive health, providing chronological proof from
the Level 3 opening through the Room 9 slide platform.

The tall dark shapes below the electrical platforms are the packaged
`electro_beam.bdae` effect using `fx_lightning.tga`. Texture inspection shows
that this 256x256 image is fully opaque (all alpha values are 255); its black
background is removed by the authored additive blend (`GL_SRC_ALPHA`,
`GL_ONE`), not by texture alpha. The separate `electric_cable.bdae` asset does
contain transparency. The D3D11 material path preserves the additive state,
and the Level 3 render probe shows the colored lightning beams without black
rectangles.

## Falling room hazards

Room 8 contains five `DropArea`/`DropObject` pairs. The area update at
`0x00309dc0` is a one-shot player-volume test: entering it restarts the
authored `explode_new` effect and activates its linked chair. The chair state
machine at `0x00309a48` first makes the otherwise hidden mesh visible and
plays VoxSound ID `0x155`, recovered as
`SFX_BATTERY_CELL_EXPLOSION`, waits its authored 500-1000 ms delay, starts the
chair's `firesmoke_xp` effect, enables collision, and then falls. Its native
fixed-tick velocity increases by 10 up to 100 while Z is reduced by the prior
velocity; the portable runtime expresses the same progression in elapsed
time so higher display rates do not speed up the hazard.

`LevelDropRuntime` owns only trigger, delay, fall, visibility, and hit state.
It sends renderer-neutral transforms to `LevelObjectRuntime`, room-owned
one-shot requests to `LevelEffectRuntime`, and a single 100-point player hit
when the falling prop intersects the player's box. The D3D11 regression
renders an isolated activated chair and verifies the native dormant state is
invisible. Core tests cover all five authored links, exact effect names,
activation/delay transitions, falling motion, spatial sound metadata, and the
one-shot damage event. The end-to-end hazard probe also requires
`SFX_BATTERY_CELL_EXPLOSION`, tying the authored trigger to the live spatial
audio request.

## Authored damage volumes and hurt reactions

`CEffectDamage::ProcessUserAttr` at `0x00368ba4` builds an oriented box from
the node's absolute transform and `Sizes`, defaults damage values at or below
0.1 to 30, and retains the authored enable and damage-type fields. Level one
uses four enabled type-0 volumes: ID 703 in Room 1 and IDs 754, 836, and 837
along the Room 8 fire walls. They have no renderable mesh; their presentation
is the player's recovered hurt response.

`LevelDamageRuntime` reproduces `CEffectDamage::Update` at `0x00368a80` with
renderer-independent oriented containment and the native 1000 ms contact
cooldown, preventing a player who remains inside a volume from taking
damage every display frame. Native hit types are 133 for damage type 0 and
134 for type 1; both select heavy wall state 50 when attached. The wall hurt
animation finishes independently of that cooldown. The existing ground
response still uses the portable light/heavy mapping and minimum reaction
duration; its exact native classification remains a separate reconstruction
gap. A hit subtracts the authored 30 health and dispatches recovered audio.
Core tests
cover all four real records, containment, cooldown, health, and animation
timing; WARP verifies that the hurt pose changes the rendered player frame.

## Fall restore and black-screen transition

The 13 linked rooms contain 11 `TriggerRestore` volumes, each paired with a
`RestorePoint`. `CTriggerRestore::ProcessAttr` at `0x0036c170` constructs an
oriented fall volume from the absolute transform and `Sizes`; `Update` obtains
the player's position vector and calls the point overload of
`obbox::test_obb` at `0x003d45a4` (not a player-capsule overlap). The
first-level pairs carry 200 damage except trigger 1176, which carries 50. The
restore point returns the player to its authored absolute position and facing, resets
ordinary locomotion, and lets the nearest camera-area update select the new
room view.

The native point test has a world-space broadphase before its inverse-OBB
test: every absolute center delta must be at most twice the smallest `Sizes`
component. Restore 554 has a minimum size of 500, so that preliminary half
extent is 1000. The route point `(-6252.81, 6795.33, 1873.50)` is rejected by
that broadphase even though an inverse-OBB-only test would accept it; the
volume center is accepted. A regression test pins both outcomes.

The normal state-2 path in `CTriggerRestore::Update` at `0x0036bf50` and
`Draw2D` at `0x0036bb84` is preserved exactly: black alpha advances with
`elapsed * 256 / 1280`, the player is damaged and restored once at 1280 ms,
the opaque frame is held through 1792 ms, and then controls and alpha reset.
`LevelRestoreRuntime` owns that timing and point containment independently of D3D11;
the application suppresses ordinary input and additional hazard hits during
the transition. The renderer adds the resulting alpha as a final full-screen
color quad. Deterministic tests cover every shipped link and exact transition
boundaries, while WARP verifies the fully opaque frame is black.

The containment path retains and inverts each node's complete serialized
`AbsoluteTransformation`, matching `CTriggerRestore::ProcessAttr` at
`0x0036c170` and `obbox::test_obb` at `0x003d45a4`. This is significant for
Level 2 trigger 40027: its box is tilted beneath the opening rooftop slope.
Applying a conventional inverse quaternion to Irrlicht's transposed scene
quaternion tilted that box through the walkable roof and falsely restored the
player. The no-teleport Level 2 route and a core regression now pin a valid
surface point above that volume.

`Player::CanEnableTriggerRestore` at `0x0033ff14` calls the `IsDead` vtable
entry and rejects a dead player while the trigger is idle. Once `CLevel` has
entered the death-screen path, its early return also prevents an already
active `CTriggerRestore` from advancing. `LevelRestoreRuntime` therefore does
not activate for a dead player and freezes an active opaque restore frame
while `LevelDeathRuntime` advances the independent confirmation fade.

Restore-volume enable gates are also command-driven. The preserved
`CCinematicThread::EnableTriggerRestore` at `0x003719d8` accepts only a Basic
thread, reads `^ID^TriggerRestore` and `enable`, resolves the object through
`CLevel::FindTriggerRestoreById` at `0x0037dc94`, treats a stale ID as a
successful no-op, and writes the enable byte at `CTriggerRestore+0xc8`.
`LevelRestoreRuntime` now keeps that state per authored ID and excludes a
disabled volume from containment/update. The Level 3 restore-gate autoplay
probe runs cinematic 30944, observes restore 31106 become disabled, enters a
point inside 31106 but outside every overlapping restore, and verifies that
health remains unchanged. `CTriggerRestore::ProcessAttr`'s additional
`^Link^Cinematic`, `FallAfterRestore`, and `UseLastCheckPoint` fields are
retained directly from the pack; notably, shipped restore 31106 has no linked
cinematic, so no synthetic 31109 handoff is inferred.

## Rhino phase tasks and synchronized throw QTE

Level two's Rhino path is reconstructed from the shipped task tables and ARM
behavior implementations rather than inferred from captures. `_GLOBAL__I_CBoss`
at `0x00329cd4` starts each phase with two task-3 melee actions and task 11;
`CBehaviorDush` states 78-85 supply `idle_charge_run`, `rush`,
`run_bash_attack`, the 1200 cm/s charge, 45-degree/s turn, and five-second
failed struggle. `CBoss::ParseLocalAiMessage` clamps the first threshold to
66 percent and starts phase one below 67 percent, then clamps the second to 33
percent and starts phase two at or below 33 percent. The ordered check prevents
one large hit from skipping a phase.

Phase one and two insert task 12, `CBehaviorThrow` at `0x003c5dec`-`0x003c6748`.
Its recovered states 86-91 use `idle_to_grab_ready`, `idle_to_grab`,
`grab_to_hold`, `struggleing`, `hold_to_throw`, and `grab_to_failed`, with the
250 cm range assigned by `CBoss::ResetBehavior` at `0x0032afdc`. The same reset
resolves the authored `R_Hand_Dummy`; `CatchPlayer` at `0x003c6164` advances
the boss pose and moves Rhino so that hand offset reaches the target. State 88
at `0x003c5f64` parents Spider-Man to that animated node with an identity
relative matrix translated by `(0, 0, -0.5 * player height)`. The portable
runtime evaluates that BDAE hierarchy each frame, preserves the opposite boss
facing, and restores the parent hand's absolute position when the QTE ends.

`QuickTimeActionConfigDatabase` decodes all 28 shipped `QTE_ACTIONS.bin`
records using `QTEActionConfigFile::ReadBasic` at `0x002fc50c`.
`QuickTimeActionRuntime` follows `QTEActionManager::DoAction`/`Update` at
`0x00389e1c`/`0x0038a0e0`: action 9 synchronizes `idle_to_grab` with
`grab_to_hold`, action 6 runs `struggleing` with BCONFIG entry 11 (eight taps
in 4000 ms), action 7 counters Rhino for 100 at frame 21, and action 8 throws
Spider-Man for 100 at frame 50. The looping main action also carries four
cumulative 20-damage keys at frames 20, 40, 60, and 80; these use the action
timeline rather than the one-second looping animation clock.

The application blocks ordinary controls during the synchronized action,
renders the recovered player and Rhino clips, displays button progress, and
routes the existing QTE input without host automation. Deterministic core
tests cover both outcomes, health clamps, attack frames, hand alignment, and
parent/detach matrices. Separate level-two autoplay probes force the phase
boundary through a process-local diagnostic damage hook and prove both the
eight-tap success and no-input failure paths. Their frame logs include Rhino
phase/cycle/task fields and QTE state, action time, button time, required and
completed tap counts, and progress.

## Native moving rooms and cinematic gates

Moving-room behavior is reconstructed from the original ARM handlers and
shipped scene graph, not inferred from captures. `CCinematicThread::ActiveRoom`
at `0x00371d54` and `CCinematicThread::DeactiveRoom` at `0x00371d28` read
`^ID^Geometry`, resolve it with `CLevel::GetRoomFromID`, and write one or zero
to `CRoom+0x90`. That field is the movement gate rather than room visibility.
`CCinematicThread::RevertRoomsPosition` at `0x00371d80` resolves the same ID
and calls `CRoom::RevertPosition`; all three handlers fail when the ID cannot
be resolved.

`CRoom::ProcessMovingAttributes` at `0x0036d49c` reads the authored
`Line_Speed`, `Active`, and `^Link^WayPoint` attributes. `CRoom::InitMovingProperty`
at `0x0036d5dc` stores the first linked waypoint's position as the room-path
origin and selects that waypoint's next link as its first target.
`CRoom::Move` at `0x0036d8c0` advances in centimeters per millisecond, updates
the physics velocity in centimeters per second, tests arrival and passage
relative to the previous reference position, and preserves its recovered
corner-overshoot branch. `CRoom::RevertPosition` at `0x0036d798` passes a
literal zero position to `CRoom::SetPosition` and reinitializes the path; it
does not restore a guessed or cached spawn transform.

The portable loader retains these fields from every shipped room Geometry
node. The application advances the recovered state on the scaled game clock,
translates the room's BDAE vertices and bounds in the D3D11 renderer, and
applies the same transform to room-owned collision triangles before rebuilding
the spatial grid. `rooms.csv` records the gate, links, current target,
position, and velocity every sampled frame, while `events.csv` records gate
and target transitions.

The Level 7 process probe dispatches the actual timestamp-zero `ActiveRoom`
commands from cinematic 61175 for Geometry IDs 61205, 61204, 60039, 60030,
and 60009. The four rooms with shipped waypoint links move at their authored
3 cm/ms and report 3000 cm/s velocity; the unlinked room remains stationary.
Core tests also exercise the shipped Deactive/Revert command sites and
collision translation. A D3D11 WARP test invokes the same 61175 command for
room 61204 and requires a material pixel change after its recovered motion.

The train sequence in that cinematic is separately reconstructed through its
native object path. `CCinematicThread::CutTrain` at `0x00370efc` calls
`CTrain::Cut` at `0x00321a0c` on the thread-bound train, detaching the selected
carriage from its predecessor and making it the head of the remaining
next-car chain. `CCinematicThread::FollowWayPoint` at `0x00370288` resolves
`^ID^WayPoint` and dispatches virtual `CM_FollowWayPoint`; the `CTrain`
override at `0x003213b8` ignores `$RunType` and calls
`CWayPointMover::SetDestination` at `0x003269c8` directly.

Shipped `Train` nodes now enter the typed mesh/object loader with their
`Line_Speed`, `Active`, waypoint, predecessor, successor, lifetime, transport,
and player-damage attributes. `CWayPointMover::NextDestination` at
`0x0032699c` supplies path traversal, while `CTrain::SetDirection` at
`0x00321e94` establishes the model's native negative-X forward convention.
The Level 7 probe executes cinematic 61175's actual Cut/Follow/Enable commands
for train 60564 and verifies its 1 cm/ms transform toward waypoint 328.
`objects.csv` records the train chain, target, speed, direction, quaternion,
and velocity. WARP tests independently prove that `train.bdae` is rendered
and that the shipped command sequence moves its pixels rather than only
changing diagnostic state.

The chronological `level7-opening-progression.usmauto` route now continues
from the opening handoff through both collapsed-street web crossings, both
street combat gates, the complete Room 4 facade fight and timed hazard seam,
the native edge-wall transfer, the Room 15 rooftop fight and hostage rescue,
and CheckPoint 269. It then crosses `jump_wall4` as an airborne vault, uses
WebGrabPoint 196 to catch Slide 198, jumps from that moving slide to
WebGrabPoint 201, avoids Room 5's instant-kill tentacle AreaDamage 224, and
lands through CheckPoint 156. The 162-step route contains no teleport or
direct-health command.

That route exposed a state-handoff defect rather than a missing slide trigger.
The original motion-28 web release stores velocity in the normal physics body;
changing from native state 19 to falling state 15 changes the animation/state
predicate but does not replace that velocity. The portable player now retains
the accumulated release velocity, gravity, and controllable horizontal
acceleration across the same transition. This makes the shipped 50 cm
horizontal `CSlider::Update` catch tube reachable without enlarging it.
`events.csv` records accepted slide candidates with the slide ID, projected
point, player state, and active flag. The autoplay language also supplies
`move_to_until_state x y z state timeout`, which emits normal camera-relative
movement toward an authored point until the required player state occurs; it
does not write the player transform.

## Native boss factory and Robot Phantom task graph

`CLevel::LoadNextObject` at `0x003853fc` routes all eight shipped boss game
types through the `CBoss` constructor branch at `0x0038640c`: Sandman, Rhino,
Electro, Venom, Venom-on-train, Robot Phantom, Green Goblin, and Dr. Octopus.
Robot Phantom is the one exception to the `Boss_*` naming convention and
selects its runtime type from the authored `Enemy_Type` field rather than
`Boss_Type`. The portable factory now preserves that native distinction and
loads the later-level actors as enemies instead of leaving their nodes as
unowned scenery.

Robot Phantom's task graph is reconstructed from the four 28-byte records
created by `CBoss::_GLOBAL__I_CBoss` at `0x00329cd4`, not inferred from the
Level 8 scene. Their task IDs are exactly 3, 24, 5, and 24; the task-3 and
task-5 records carry the native 500 cm range consumed by
`CBoss::ParseAiTaskInfo` at `0x0032c0c8`, which inserts movement before melee
and range. `CBoss::InitAiTask` at `0x0032b66c` selects all four records for
Robot phase zero. The behavior IDs resolve through their original virtual
methods: `CBehaviorMeleeAttack::GetBehaviorId` at `0x003b95bc` returns
`0x12f`, `CBehaviorRangeAttack::GetBehaviorId` at `0x003a9734` returns
`0x134`, and `CBehaviorConceal::GetBehaviorId` at `0x003aa5f8` returns
`0x141`.

The melee task uses the shipped `rush_attack_ready`, `rush_attack1`,
`rush_attack2`, and `rush_attack2_to_idle` chain. Special-action records 172
through 175 place attack 60 at 40 percent of `rush_attack1` (40 damage, 300
cm) and attack 61 at 80 percent of `rush_attack2` (70 damage, 300 cm). The
conceal task follows behavior states 131-133 recovered from
`CBehaviorConceal::BehaviorUpdate`/`StateEnter` at
`0x003aa7e0`/`0x003aab6c`: finish `conceal_ready`, hide the actual scene node
for 500 ms, calculate the point behind the target from the target's real
facing and both collision radii, use the native segment-collision fallback,
reappear facing the target, and run `conceal_attack` followed by
`conceal_attack_to_idle`. Its two authored keys are attack 65 at 60 percent
(40 damage) and attack 66 at 90 percent (60 damage), both with 300 cm reach.

Enemy type 15's sole range-map index is 17. The native lookup table at data
address `0x00510818` resolves it to weapon type 30, while
`RANGE_ATTACK_07` supplies a 1000 ms action window and 40 damage.
`CBehaviorRangeAttack::DynamicLoadMeshAndAnim` at `0x003c17fc` constructs a
`CBoomerang` with 500 cm/s speed and a 200 ms collision pause. The portable
weapon retains all six states assigned by `CBoomerang::SetState` at
`0x0035aefc` and advanced by `CBoomerang::Update` at `0x0035b7f8`: outbound
with a 1500 ms lifetime, 500 ms target pause, 200 ms collision pause,
homing return, half-speed hand return, and hidden/ready. Range behavior waits
for the weapon to reach state 5 before `throw_wait_to_idle`, matching
`CBehaviorRangeAttack::UpdateAttack_DoAttack` at `0x003c1e64`.

Both `CBoomerang` constructors at `0x0035b3fc` and `0x0035b604` load the
shipped `phantom_unit_weapons.bdae`. The ARM instruction sequences at
`0x0035b560` and `0x0035b720` then load `r1` from data address `0x004e538a`,
whose exact string is `weapons`; `r2` is 1 and `r3` is 0 before the call to
`IAnimatedObject::SetAnim` at `0x00311150`. Thus the native call is the named
`weapons` clip with looping enabled and mode zero, not animation index one.
The packaged clip starts at 33 ms and lasts 200 ms. The constructors also
load `0x42480000` (50.0 cm) into the flyable-physics call at `0x003d8650`, so
the portable swept player test uses the recovered 50 cm weapon sphere rather
than the molotov's separate 20 cm radius. The D3D11 renderer evaluates this
named clip on the exact packaged mesh for every live boomerang; a WARP
readback regression toggles that mesh and requires a material pixel change.

The deterministic Level 8 probe starts the authored visibility/health
cinematic 40526 for object 40524, then proves the complete
melee-conceal-range-conceal order without host input. `enemies.csv` records
the explicit Phantom task and sequence index, `enemy-projectiles.csv`
records every boomerang transform/velocity/state, and `events.csv` records
spawn, player contact, static contact, and return. Core tests additionally
assert the exact map, weapon type, attack IDs, damage values, 500 ms hidden
boundary, behind-target transform, packaged `weapons` clip, 50 cm projectile
collision radius, and returned weapon state.

## Native Electro task graph and effect objects

Electro is likewise reconstructed from the native boss path rather than from
visual trial and error. `CBoss::InitAiTask` at `0x0032b66c` selects the six
type-7 records with task IDs 5, 13, 20, 13, 22, and 13: range, weak, rotate,
weak, Electro dash, weak. Level 3 object 30418 is the shipped type-6 cinematic
double and therefore never receives that graph; object 31094 is its type-7
runtime boss. Range repeats three, four, or five times by phase with a 200 ms
inter-attack wait. Weak lasts 4000 ms because native parsing halves the
authored 8000 ms. Rotate uses 2000 ms ready plus 10000 ms active durations and
54, 90, or 108 degrees/second. Dash uses four, five, or six passes at 2500
cm/s, with the final pass returning to `CBoss::ResetBehavior`'s stored origin.
`CBoss::ParseLocalAiMessage` at `0x0032dea8` clamps phase transitions to 66 and
33 percent health.

The range weapon is the native Thunderclap pool returned by
`CLevel::GetThunderclapPool` at `0x003837f0`: weapon type `0x12`, five pooled
objects, 50 damage, 400 cm radius, three-second convergence, and three launches.
`CSummonObjManage::Launch` at `0x003670dc` seeds `srand48(0)`, so the first
angle is exactly 334 degrees and subsequent objects are 120 degrees apart.
`CSummonObjManage::Update` at `0x00366b1c` converges them at 400/3 cm/s and
switches state below 60 cm. `CSummonObject::SetState`/`Update` at
`0x00365f54`/`0x00364ff4` establish the exact visible sequence: `electro_wave`
`wave` (833 ms), `electro_beam` `keep` (633 ms), restarted `wave` (833 ms),
then the native 250 ms fade.

Rotate owns exactly three `CElectricPostWithEffect` objects. Its constructor at
`0x003c2bfc` loads `electro_wave.bdae`, allocates those three posts, and assigns
additive material type `0x0d`. `ResetElectricPost` at `0x003c2848` places every
post at boss Z plus collision height times 0.4, begins with the boss quaternion,
and advances each following quaternion by exactly 2.094395 radians around Z.
`CElectricPost` at `0x00354e94` loads `electro_beam.bdae` and loops animation
index one, the packaged 633 ms `keep` clip. Its shipped `dummy_start1` and
`dummy_end1` authored nodes define a 1429.7869 cm local -Y ray. The renderer
maps that actual local axis into each recovered direction and forces the native
additive blend; it does not replace the BDAE with a generated primitive.
Collision uses the same authored node length and decoded mesh half-width in a
portable segment/capsule equivalent to the animated-OBB-versus-unit test in
`CElectricPost::Update` at `0x00355558`.

Weak and dash bursts also retain their native paired effect models.
`CBoss::ResetBehavior` at `0x0032afdc` configures type-7 Weak with
`electro_wave.bdae`, `electro_wave_billboard.bdae`, scale 3.0, radius 400, and
70 damage. Message `0x65` reaches `CBehaviorWeak::onMessage` at `0x003cb34c`
at 30 percent of `scream`; `CBehaviorWeak::ThrowEffect` at `0x003cb1bc` places
both packaged models and `effect_lighting_splash` at the boss top. Electro dash
uses the same model pair at scale 1.0 through
`CBehaviorElectroDush::ThrowEffectRoundPlayer` at `0x003b2b74`, at the closest
point crossed by the rush. The portable effect lifetime is fixed by the longer
packaged non-looping `wave` clip (`electro_wave_billboard`, exactly 1000 ms),
and diagnostics record origin, scale, and elapsed time.

Core tests cover the task order, exact clips and durations, deterministic
Thunderclap ring, convergence, three damage events, weak release at 30 percent
of `scream`, phase-zero rotation, three 120-degree ElectricPosts, 50-damage
post contacts, 2500 cm/s dash, and health clamps. WARP readbacks separately
require Thunderclap, Weak burst, and ElectricPost pixels to brighten without
any darkening, guarding the `0x0d` blend path. The process-local
`level3-electro-rotate-probe.usmauto` traverses range -> weak -> rotate and
records each post center, direction, animation time, damage, and active state
in `enemy-projectiles.csv`; it sends no host input and requests no captures.

## BTEX/PVRTC textures

Level and entity textures use an eight-byte `BTEXpvr` wrapper followed by a
52-byte PVR v2 header and PVRTC4 data. This matches the preserved
`loadPVRTexture` routine at original address `0x003dc120` (Ghidra
`0x003ec120`). `assets::BtexTexture` validates the header and all mip bounds,
then uses the official MIT-licensed PowerVR decoder to produce RGBA8 pixels
suitable for a D3D11 shader-resource view. RGB-only PVR flags force opaque
alpha, matching the original GL compressed-RGB upload.
