# OpenAndroidUSM

Source-level C++ reconstruction of the Android/Xperia Play release of
*Ultimate Spider-Man: Total Mayhem*.

The project does not include the original game executable or assets. A legally
obtained copy must be imported locally. Generated build products are intended
to require the user's original data.

## Current target

- Windows 10/11 x64
- Native C++20
- Direct3D 11 renderer with HLSL shaders
- XAudio2 audio
- XInput/GameInput-compatible controller abstraction
- CMake and Visual Studio 2022

Direct3D 11 is the initial backend because it offers a modern programmable
pipeline while keeping bring-up and compatibility work smaller than Direct3D
12. The renderer-facing API is backend-independent so another backend can be
added later.

## Repository policy

Original binaries, assets, reverse-engineering databases, and generated
decompiler exports stay untracked. Reconstructed source, manually reviewed
types, tools, tests, and documentation are tracked.

## Configure and build

```powershell
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

## Import local reference material

```powershell
powershell -ExecutionPolicy Bypass -File tools/import_game.ps1 `
  -Archive "$env:USERPROFILE\Downloads\spiderman.7z"
```

The importer extracts the reference ARM library and game data under ignored
`game/` directories and records hashes for reproducibility.

## Deterministic autoplay diagnostics

The Windows executable can drive the real level-one application loop from a
text scenario without keyboard, mouse, controller, or window automation:

```powershell
.\build\windows-msvc\Debug\OpenAndroidUSM.exe `
  --autoplay .\tests\autoplay\opening-baseline.usmauto `
  --output .\analysis\generated\opening-baseline
```

The output directory contains `frames.csv`, `enemies.csv`, `events.csv`,
`player-states.csv`, geometry-level `collision-surfaces.csv`, face-level
`collision-triangles.csv`, a `summary.txt` pass/fail record, and timestamped
BMP readbacks. Frame samples retain the complete concurrent cinematic set. The
enemy trace distinguishes visibility, AI, physics activity, and timed cinematic
motion, including each interpolation clock. Audio is traced
without playback by default; pass `--autoplay-audio` when audible output is
useful. The tracked scenarios cover the opening, concurrent first-encounter
presentation, encounters, non-aliasing enemy animation, and authored
wall/jump-wall/ledge traversal. They
are reconstruction probes, not a claim that the campaign is ready for manual
playtesting. Frame telemetry includes the active player state ID/name and
deterministic combo readiness; melee-impact events retain each state's authored
damage, reach, and angular limits.
