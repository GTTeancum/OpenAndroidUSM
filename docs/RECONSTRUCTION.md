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
and health. `LevelEnemyRuntime` owns the mutable state of all 28 level-one
thugs. Cinematic `DisableAI`, `EnableAI`, `SetVisible`, `SetAnim`, and
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
each crossed looping key frame, queues a named `EnemyMeleeHit`, and applies it
to player health. Core regressions exercise both knife impact frames and
verify their recovered 25-point damage.

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
instead of wrapping. `EnemyBehaviorSoundBank` predecodes the nine level-one
thug sounds before gameplay. WARP captures cover a mid-hurt pose and the
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
separate D3D11 resources. The linked scenes contribute 27 triggers, 28 melee
enemies, and 43 cinematic objects. Forty-two cinematic scripts are present;
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
the camera and dispatches 2D/loop flags to XAudio2. All 19 unique level-one
intro event names now resolve through the recovered table and preload before
playback. The same deduplicated path scans all 42 runnable encounter scripts,
preloads their 60 unique sound events with no unresolved aliases, and tags
XAudio2 voices by Vox event name. Authored `Stop`/`Stop2D` commands can
therefore stop every matching active voice instead of leaking looping sounds
across cinematic boundaries.

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

## BTEX/PVRTC textures

Level and entity textures use an eight-byte `BTEXpvr` wrapper followed by a
52-byte PVR v2 header and PVRTC4 data. This matches the preserved
`loadPVRTexture` routine at original address `0x003dc120` (Ghidra
`0x003ec120`). `assets::BtexTexture` validates the header and all mip bounds,
then uses the official MIT-licensed PowerVR decoder to produce RGBA8 pixels
suitable for a D3D11 shader-resource view. RGB-only PVR flags force opaque
alpha, matching the original GL compressed-RGB upload.
