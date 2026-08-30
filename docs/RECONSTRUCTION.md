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

The intro's `MustBeVisible` command names Rooms 1 through 5. The native level
bootstrap loads those five authored room scenes, their `geometry01` through
`geometry05` BDAEs, and every referenced texture as separate D3D11 resources.
It also loads `lvl01_sky.bdae`. The preserved `CSkyBoxObject::Update` at
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
layer and `spiderman_rim.tga` as its secondary layer. The base layer is active
in D3D11; the recovered secondary binding is retained for the later equivalent
rim-light shader rather than discarded or guessed.

## Vox sound events

The preserved `VoxSoundFile::LoadRecordFromFile` and `ReadBasicRecord`
functions (Ghidra `0x003da650` and `0x003da570`) load `/VoxSound.bin` or
`/VoxSounds.bin` into 0x30-byte event records. Those record-table assets are
not present in the supplied archive. `audio::SoundEventCatalog` therefore
indexes the 515 supplied Ogg files by event stem, rejects the five ambiguous
stems, and records aliases only when asset evidence is exact. The Level 1 CFF
typo `SPIDY` maps to the shipped `SPIDEY` filename; missing event aliases stay
unresolved and test-visible instead of being replaced with guessed sounds.

`audio::CinematicSoundBank` scans the typed CFF before playback and decodes
each unique resolvable event once, keeping Vorbis work off its scheduled frame.
The application advances `CinematicPlayer` from the same monotonic clock as
the camera and dispatches 2D/loop flags to XAudio2. The level-one intro has 19
unique event names: 17 resolve to supplied Ogg files, while
`SFX_THUG_KNIFE_HURT_1` and `SFX_VERTICAL_IMPACT` remain explicit evidence
gaps because the absent VoxSound table is the only authoritative alias map.

## BTEX/PVRTC textures

Level and entity textures use an eight-byte `BTEXpvr` wrapper followed by a
52-byte PVR v2 header and PVRTC4 data. This matches the preserved
`loadPVRTexture` routine at original address `0x003dc120` (Ghidra
`0x003ec120`). `assets::BtexTexture` validates the header and all mip bounds,
then uses the official MIT-licensed PowerVR decoder to produce RGBA8 pixels
suitable for a D3D11 shader-resource view. RGB-only PVR flags force opaque
alpha, matching the original GL compressed-RGB upload.
