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
