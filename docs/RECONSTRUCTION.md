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
bytes. A material selects an effect, whose diffuse-image payload indexes the
image library. The image's source path is the archive-facing texture name;
for example, Room 1 maps `alphatest` to `level01_alphatest.tga` and
`Material__54` to `041_building.tga`.

The original
`CCommonGLMaterialRenderer_ALPHA_TEST_NONTRANSPARENT::onSetMaterial` at Ghidra
image address `0x00397864` enables alpha testing with `GL_GREATER` and a 0.5
reference. The D3D11 material path preserves that behavior with an HLSL
`clip` shader variant for the recovered `alphatest` materials.

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

## Vox sound events

The preserved `VoxSoundFile::LoadRecordFromFile` and `ReadBasicRecord`
functions (Ghidra `0x003da650` and `0x003da570`) load `/VoxSound.bin` or
`/VoxSounds.bin` into 0x30-byte event records. Those record-table assets are
not present in the supplied archive. `audio::SoundEventCatalog` therefore
indexes the 515 supplied Ogg files by event stem, rejects the five ambiguous
stems, and records aliases only when asset evidence is exact. The Level 1 CFF
typo `SPIDY` maps to the shipped `SPIDEY` filename; missing event aliases stay
unresolved and test-visible instead of being replaced with guessed sounds.

## BTEX/PVRTC textures

Level and entity textures use an eight-byte `BTEXpvr` wrapper followed by a
52-byte PVR v2 header and PVRTC4 data. This matches the preserved
`loadPVRTexture` routine at original address `0x003dc120` (Ghidra
`0x003ec120`). `assets::BtexTexture` validates the header and all mip bounds,
then uses the official MIT-licensed PowerVR decoder to produce RGBA8 pixels
suitable for a D3D11 shader-resource view. RGB-only PVR flags force opaque
alpha, matching the original GL compressed-RGB upload.
