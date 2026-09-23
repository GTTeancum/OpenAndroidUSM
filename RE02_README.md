# OpenAndroidUSM RE02 — Native source continuation

September 22, 2026. Cumulative over delivered **RE01**, which was based on the
uploaded commit `59903b8037945c24ad81a5f947d69f01dc4de5b7`.

**Not ready for near-final testing. No new Windows game executable is included.**
This preserves completed reverse-engineering/source work; it is not a request
for user acceptance testing or a claim of finished first-level gameplay.

## Changes

Reconstructs the original absolute QTE timeout and float32 pause calculation;
real-time mash and original result-sprite timing; all four authored drag-path
rules, compound sequencing, and draw-driven outcome timing. Removes the
incorrect A-tap shortcut for the actual first-level drag. A plus left-stick
coordinates are an explicit PC input translation; only the recovered manager
judges success. Autoplay sends original path points.

Corrects hostage object/QTE update ordering, cutting-loop start, interruption
handling, original tap/success/failure sound events, and the ButtonHeight
sentinel. Device disconnection now cancels PC state instead of issuing an R1
release that could rescue/switch. Combat's existing two-update press window
remains separate from QTE taps.

See `docs/RE02_EVIDENCE.md` for original addresses and remaining differences;
`RE02_verification/` contains the real build/test logs, reference fingerprints,
asset witnesses, unchanged-original-data verification, and source/patch checks.

## Files and applying the source

`OpenAndroidUSM_RE02_Source.zip` is the complete cumulative source, including
RE01. It contains neither original game material nor vendor/build caches.
Continue using the original uploaded `game/` and dependency material.

`OpenAndroidUSM_RE02_Overlay.zip` contains changed/new files relative to **RE01**.
Extract its contents into that project's root, preserving unrelated local edits.
Do not apply the RE01-relative overlay directly to an unmodified original
upload. The cumulative source is the alternative for that case. The included
`RE02.patch` is an alternative to overlaying replacement files, not an
additional step afterward.

The exact delivered-source manifest and clean-application check are included.
No original `.so`, APK, game texture/audio, generated decompiler dump, or game
executable is in either archive.

## Reference check

From the project root, with the original supplied library in place:

```sh
python tools/verify_re02_reference.py
```

This uses the standard-library-only ELF parser from RE01. It fails on a
different reference. It does not execute ARM and is not proof of gameplay
parity by itself.

## Build interfaces

The existing Windows MSVC interfaces are retained:

```powershell
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
ctest --test-dir build/windows-msvc -C Release -R "^OpenAndroidUSM\.(CoreTests$|InputParity\.|QteParity\.)" --output-on-failure
```

**These Windows commands were not executed for RE02.** The newly edited
Windows application wiring is uncompiled here. The prior uploaded executable
must not be mistaken for a build containing these source changes.

On Linux, CMake now excludes the Windows-only D3D11/application/render-test
targets by default. `USM_BUILD_WINDOWS_APP` remains on by default on Windows;
trying to enable it on Linux fails explicitly. Native Linux core builds and
tests are recorded in `RE02_verification/commands.txt`, with dependency sources
restored from the supplied archive. This is a portable core/test build, not a
Linux graphical port.

## Outstanding acceptance work

Windows compilation, normal first-level play, rendering/audio and physical
XInput are unverified. Original QTE UI composition, shared manager arbitration
among cinematic/hostage/wall/enemy QTEs, full input/pause lifetimes, some hostage
animation/audio/ambient behavior, and the uploaded Sandman work remain open.
The native test results must not be substituted for those acceptance checks.
