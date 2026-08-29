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
