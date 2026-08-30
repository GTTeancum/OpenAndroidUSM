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

`tools/ghidra/ExportSelectedDisassembly.java` complements the selected
decompilation exporter with address-stamped ARM instruction listings and the
adjacent literal pool. It is used when the decompiler elides constants or ABI
arguments that are material to a reviewed reconstruction.

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
cinematic 1265, and end-level cinematic 1267. `LevelPlayerAsset` loads those
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
control plane, blends the polygon edges by reciprocal distance, distributes
those weights to their endpoints, and normalizes the blended direction. It
then follows `CGameCamera::Update` (original `0x002e327c`) by placing the
camera at `target - direction * distance` and adding the preserved 120-unit
vertical target offset. The initial area 283 pose is regression-tested from
Spider-Man's serialized level-one start position.

## Native gameplay and combat

`GameplayPlayer` implements renderer-independent ground movement, collision,
camera-relative controller input, named punch clips, authored impact timing,
and health. `LevelEnemyRuntime` owns the mutable state of all 34 level-one
enemies. Cinematic `DisableAI`, `EnableAI`, `SetVisible`, `SetAnim`, and
`MoveObject` commands feed that state directly; enabled enemies acquire the
player using their scene-authored awareness radius and chase at the authored
line speed.

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
`KillObject` and `IfObjectDestroyed` share the same native death transition
for enemy-backed objects: health reaches zero, AI stops, and the authored
one-shot death animation and behavior sound are selected. This covers the
enemy destruction gates used throughout the level-one encounter chain.

Enemy melee timing is not guessed. The typed
`EnemySpecialActionConfigDatabase` follows
`EnemyAttributeFile::ReadAnimSpeciaActionInfo` at `0x0033b9d8` and decodes all
230 records in `EnemysSpecialAnimConfigs.bin`. Knife thugs attach attack ID 6
to 45% and 75% of `idle_knife_at_idle`; bat thugs attach attack ID 7 to 47% of
`idle_at1_idle`. Those IDs resolve through `EnemysAttackConfigs.bin` to the
authored damage, hit-box reach, and angular sector. The native runtime checks
each crossed looping key frame, queues a named `EnemyPlayerHit`, and applies it
to player health. Core regressions exercise both knife impact frames and
verify their recovered 25-point damage.

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
key-frame cues even when an attack misses, rotates hurt variants
deterministically, and emits the corresponding death cue. Hurt and death
animations are one-shot states; D3D11 clamps them at the final authored pose
instead of wrapping. `EnemyBehaviorSoundBank` predecodes all 32 sounds used
by level-one enemy types 0, 1, 3, 4, 5, and 16 before gameplay. WARP captures
cover a mid-hurt pose and the
final prone death pose.

Player combat audio is likewise state-driven. `PlayerStateConfigDatabase`
follows `StateFile::ReadBasicState` (`0x0033d294`) and
`StateFile::ReadSoundConfig` (`0x0033d5f8`) to decode all 131 `MC_STATE.bin`
states and 38 `MC_SOUND.bin` configurations. The recovered
`k_state_idle_to_punch_right` record fires
`k_mc_sfx_swoosh_punch_lag` on entry and
`k_mc_sfx_punch_impact` at authored frame 9; those configurations select Vox
IDs 60/61 and 58/59 respectively. `k_state_hurt_light` selects the three
authored player-hurt variants on entry. `PlayerStateSoundBank` predecodes the
seven required clips, rotates variants deterministically, and dispatches them
to XAudio2 from the native gameplay state rather than filename heuristics.

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
This includes the three end-cinematic pipe hides and the animated web-wall
open/close sequence. `Physics` currently records the exact handoff performed
by `CDestroyableObject::SetPhysics` (`0x003061cc`); rigid-body integration is a
separate gameplay slice.

The D3D11 backend uploads a dynamic mesh per instance but shares immutable
texture views by archetype, avoiding the original per-instance memory
explosion. Editor-only, unreferenced image slots remain index-stable and
untextured helper/shadow batches are omitted. A WARP capture verifies the
textured street furniture and props without white fallback geometry.

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

Geometry-library coordinates are object-local. The adjacent `SVisualScene`
library begins at `SCollada` offsets `0x6c/0x70`; each visual scene owns
0x50-byte `SNode` records. A node supplies position, quaternion rotation,
scale, child nodes, and typed eight-byte instance records. Instance type 3 is
an `SInstanceGeometry`, whose local `#id` selects an `SGeometry`. This layout
matches the preserved `CColladaDatabase::constructNode` at Ghidra image
address `0x00419e80`. `ColladaMeshFile::sceneGeometries` now evaluates that
hierarchy, transforms positions and normals, and preserves a separate raw
geometry library. Room 1 resolves to 152 world-space geometry instances.

## Cinematic command files

The `.cff` resources are UTF-16 XML fragments, not opaque bytecode. Each
`cinematicThread` targets the player, a scene object, or global level state and
contains timestamped named commands with typed attributes. The native
`game::CinematicScript` parser preserves those names and IDs. Level 1's start
chain is cinematic 1264 (start 1265 and disable trigger 1263), the 38-command
1265 intro, then cleanup cinematic 1266. The intro explicitly names its DAE
camera/character animations, 15 sound events, message strings, and visible
rooms.

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

The bootstrap also loads `lvl01_sky.bdae`. The preserved
`CSkyBoxObject::Update` at
`0x0031b6f4` replaces the sky object's position with the active camera position
every frame; the D3D11 sky path implements the equivalent translation-free
view matrix. This keeps the 8.8k-unit skyline dome centered around the moving
cinematic camera instead of exposing the clear color outside Room 1.

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

All eight level-one cinematic `Play3D` commands resolve their thread object to
the live player, enemy, or static-object position. Enemy behavior cues use the
same path with their source enemy ID. The listener follows the active intro,
gameplay, or cinematic camera; active voices are re-panned as it moves. The
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
case is retained even though level one's fire loop is event 146.

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
captures verify stationary, ribbon, and HUD pixels.

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
`jump_to_fall`, and state 16 (`k_state_jump_land`, motion 23) selects
`fall_to_idle`.

The 600 ms jump arc is authored on the `Dummy_center-node` translation track:
the first clip rises roughly 353 cm and the second returns to ground. The
portable player samples that root height for landing tests while retaining
the scene/physics anchor used by `CGameCamera::Update` at `0x002f327c`; D3D11
applies the track once during skinning, avoiding double displacement. If the
falling arc finds no authored floor, state 15 continues with the recovered
`-1200 cm/s` value at image address `0x004c6a78`. Airborne horizontal motion
uses the existing recovered 700 cm/s controller-relative movement and the
level collision wall resolver.

XInput A follows the original Cross route into `requestJump`. SoundConfig 8
(`k_mc_sfx_swoosh_jump`, six variants) plays on state 13 entry and
SoundConfig 11 (`k_mc_sfx_land`) plays on state 16 entry through the existing
predecoded XAudio2 state-sound path. Deterministic core regressions cover the
state IDs, authored clips, root-height arc, midair rejection, landing, and
sound cues; a WARP regression renders the midpoint pose in the first gameplay
camera area.

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

## Authored slider traversal

`LevelOneBootstrap` also materializes both level-one `Slide` objects as typed
`LevelSlideAsset` graphs. `CSlider::ProcessUserAttr` at `0x0031ce6c` supplies
the enabled, electric-shock, and entry-waypoint fields; `CSlider::Init` at
`0x0031e7cc` follows the first waypoint link. The resulting graphs preserve
Slide1038's 429-to-430 roof run and Slide1039's 445-to-446 exit run.

`LevelSlideRuntime` reconstructs the renderer-independent segment work from
`CSlider::Update` at `0x0031d920`, including the recovered 640000 cm² catch
radius, nearest-segment projection, velocity preservation, linked-segment
switching, and final waypoint gravity/electric metadata. Airborne player
states can now enter state 23 (`k_state_trigger_slider_land`), advance through
the authored `fall_to_slide` clip, and loop state 22
(`k_state_trigger_slider_move`) with its looping Vox 77 slide sound. Forced
web releases retain their linked waypoint destination, allowing grab point
443 to feed waypoint 445 and the second authored slide. Deterministic tests
cover graph extraction, proximity rejection, projected catches, segment
switching, terminal metadata, player state transitions, and audio dispatch.
The cinematic `StartSlide` command follows its preserved implementation at
`0x003701c0`: it validates that both named waypoint endpoints exist, while the
independent `CSlider` update performs the actual airborne proximity catch.

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

## In-level Collada cinematics

The intro's source-level Collada camera/actor path now covers the three other
level-one `PlayDAECamera` streams: before-boss cinematic 1254, ending 1238,
and game-over 1267. Bootstrap resolves the authored camera and every
`PlayDAEAnim` target back to its named scene node, loads ten cutscene actor
animations, preserves delayed starts (Sandman at 15,700 ms and Rhino at
28,650 ms), and extends playback until both the CFF commands and all Collada
tracks finish. The far-plane override and the `level end`, `game end`, and
successor fields remain typed on each `LevelCinematicAsset`; 1254 therefore
continues into authored cinematic 1256 after playback.

D3D11 uploads these actors as hidden dynamic meshes. During playback it
updates only the active cinematic set, replaces matching persistent player or
enemy meshes, samples the authored animated camera, and restores gameplay
actors afterward. Gameplay controls and enemy simulation are suspended while
a full Collada cinematic owns the scene. A WARP regression renders cinematic
1254 before and after the delayed Sandman entrance.

## Localized cinematic and tutorial UI

`LocalizedStringTable` parses the original newline-delimited key maps and
offset-indexed UTF-16LE language payloads from `xlsStrings.pack`. The native
bootstrap loads all 18 level-one dialogue strings and 17 tutorial strings in
English without embedding copyrighted game text in the repository.
`CinematicUiRuntime` follows `CCinematicThread::OnTutorial`, `ShowMessage`,
and `InterfaceControlCmd` at `0x003710d4`, `0x003725f8`, and `0x003711e0`:
timed captions expire independently, persistent tutorials dismiss through the
controller, black-screen tutorials dim gameplay, and cinematic black bands
follow the authored interface state. Xperia touch/button markup is translated
to XInput A/X/B labels at runtime.

The D3D11 backend rasterizes the selected UTF-16 text with the system Segoe UI
font into an RGBA texture, then composites captions, tutorial panels,
letterbox bands, and the authored QTE countdown through the existing
alpha-blended screen-space shader. The game-facing state contains no DirectX
or Windows handles. A WARP regression verifies that the native text, bands,
and QTE prompt materially alter the rendered frame.

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
authored emitter's delay, lifetime, box, direction, size/color variation,
gravity, fade, size curve, spin, pivot rotation, sprite frame, and material
type, and exposes only renderer-neutral billboard state. The deterministic
generator keeps test and capture results stable without importing Irrlicht
particle objects.

The affector math follows the preserved implementations rather than treating
the XML values as generic forces. `CFpsParticleGravityAffector::affect` at
`0x0039d7d4` captures the incoming velocity and linearly reaches its target
over the configured lifetime interval. `CFpsParticleSpinAffector::affect` at
`0x0039e9c4` selects a total billboard angle for that interval.
`CFpsParticleRotationAffector::affect` at `0x0039df08` rotates particle
positions about its pivot using degrees per second. Emitters without a gravity
affector retain their velocity instead of being damped toward zero.

The room loader also instantiates all 23 authored `CEffect` nodes across level
one. Their `SysMinLifeTime = SysMaxLifeTime = -1` emitters run continuously at
the preset particle rate, retain one-based room ownership for frustum/room
visibility, and cover the five persistent fire/smoke presets used by the
scene. Presets may contain several `FadeOut` color affectors. These are kept as
separate time intervals and evaluated in chronological order, matching
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

Player death now starts the authored cinematic referenced by
`^EndGame^Cinematic` (1267 in level one). Its Collada camera and four actors,
sound/dialogue commands, and 55-second transport command run through the same
portable cinematic path as in-level sequences. The level-ending cinematic
1238 likewise retains its delayed Rhino entrance and final pose until
`CLevel::End`/`GameEnd` would leave the original level state; the Windows
first-level target presents that last frame and exits cleanly. WARP
regressions render both terminal sequences with their object-visibility
commands and camera-area masks applied.

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
one-shot damage event.

## Authored damage volumes and hurt reactions

`CEffectDamage::ProcessUserAttr` at `0x00368ba4` builds an oriented box from
the node's absolute transform and `Sizes`, defaults damage values at or below
0.1 to 30, and retains the authored enable and damage-type fields. Level one
uses four enabled type-0 volumes: ID 703 in Room 1 and IDs 754, 836, and 837
along the Room 8 fire walls. They have no renderable mesh; their presentation
is the player's light-hurt response.

`LevelDamageRuntime` reproduces `CEffectDamage::Update` at `0x00368a80` with
renderer-independent oriented containment and the native 1000 ms minimum
reaction window, preventing a player who remains inside a volume from taking
damage every display frame. A hit subtracts the authored 30 health, selects
`k_state_hurt_light` for type 0 (`k_state_hurt_heavy` for type 1), plays that
state's recovered VoxSound entry, and temporarily blocks ordinary movement
and attacks while the corresponding packaged animation advances. Core tests
cover all four real records, containment, cooldown, health, and animation
timing; WARP verifies that the hurt pose changes the rendered player frame.

## Fall restore and black-screen transition

The 13 linked rooms contain 11 `TriggerRestore` volumes, each paired with a
`RestorePoint`. `CTriggerRestore::ProcessAttr` at `0x0036c170` constructs an
oriented fall volume from the absolute transform and `Sizes`; the first-level
pairs carry 200 damage except trigger 1176, which carries 50. The restore
point returns the player to its authored absolute position and facing, resets
ordinary locomotion, and lets the nearest camera-area update select the new
room view.

The normal state-2 path in `CTriggerRestore::Update` at `0x0036bf50` and
`Draw2D` at `0x0036bb84` is preserved exactly: black alpha advances with
`elapsed * 256 / 1280`, the player is damaged and restored once at 1280 ms,
the opaque frame is held through 1792 ms, and then controls and alpha reset.
`LevelRestoreRuntime` owns that timing and containment independently of D3D11;
the application suppresses ordinary input and additional hazard hits during
the transition. The renderer adds the resulting alpha as a final full-screen
color quad. Deterministic tests cover every shipped link and exact transition
boundaries, while WARP verifies the fully opaque frame is black.

## BTEX/PVRTC textures

Level and entity textures use an eight-byte `BTEXpvr` wrapper followed by a
52-byte PVR v2 header and PVRTC4 data. This matches the preserved
`loadPVRTexture` routine at original address `0x003dc120` (Ghidra
`0x003ec120`). `assets::BtexTexture` validates the header and all mip bounds,
then uses the official MIT-licensed PowerVR decoder to produce RGBA8 pixels
suitable for a D3D11 shader-resource view. RGB-only PVR flags force opaque
alpha, matching the original GL compressed-RGB upload.
