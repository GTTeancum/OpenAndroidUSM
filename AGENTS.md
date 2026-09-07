# OpenAndroidUSM agent instructions

- This is a source-level C++ reconstruction. Do not substitute ARM emulation,
  dynamic recompilation, binary hooks, or a compatibility-wrapper executable
  for reconstructed source.
- Never commit or redistribute original game binaries, assets, Ghidra/IDA
  databases, or bulk generated decompiler output. These belong only in ignored
  local directories.
- Preserve original ELF names and source-unit associations when available.
  Mark inferred names and replace temporary offset names once semantics are
  established.
- Keep the game-facing renderer API independent of Direct3D. Direct3D 11 is
  the initial Windows backend; reconstructed gameplay code must not depend on
  D3D types.
- Prefer deterministic, non-interactive tests and headless analysis scripts.
- Do not invoke the built OpenAndroidUSM executable with `--help`; it has no
  help switch. Verify supported command-line arguments from source and invoke
  only arguments that are implemented there.
- Work through the game chronologically in normal player-flow order. Finish
  the current playable sequence and its blocking reconstruction issues before
  moving to later areas or levels; only investigate later code when it is a
  required dependency for the current sequence.
- Resolve game behavior by reverse engineering the original executable and
  shipped data. Do not invent, approximate, tune by feel, or independently
  design an answer when native behavior is unresolved; obtain direct code or
  data evidence first, record its address/source, and implement that result.
- Record original addresses for reconstructed functions until an automated
  source map supersedes them.
- The current stopping milestone is verified first-level gameplay with no
  known graphical or audio issues. At that point, stop changing the project
  and request user review without marking the durable goal complete.
