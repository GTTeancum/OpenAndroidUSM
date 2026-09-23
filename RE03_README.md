# OpenAndroidUSM RE03 — Native source continuation

**Development checkpoint, not near-final acceptance testing. No new Windows
executable is included.** This is cumulative over delivered RE02 and preserves
the original native source reconstruction; it is not touch emulation or a remake.

## Controls

Drag QTEs now require **moving the left thumbstick in the requested general
direction**. No A hold, touch tracing, or release is required. Diagonal motion
is accepted. Return the stick toward center between successive gestures; a
held stick carried into a QTE or across controller reconnection does not count
as a new movement. Tap and mash QTEs still use A. Original timing, compound
sequences, sounds, control release, and outcomes remain in the recovered manager.

The new PC input thresholds are documented as controller policy, not Android
facts. Autoplay uses the same adapter. Additional reconstruction restores the
original result-sprite draw requests, completed-mash ring frames, and per-draw
failure alpha; the Windows renderer is wired to the tested portable geometry.
The distinct success explosion/compositing work remains unfinished.

## Preserved source

- `OpenAndroidUSM_RE03_Source.zip`: complete cumulative source, including RE01
  and RE02. Retain the original supplied `game/` and dependency material.
- `OpenAndroidUSM_RE03_Overlay.zip`: changed/new files **relative to RE02**.
  Extract into the existing RE02 project root, preserving unrelated local edits.
- `RE03.patch`: alternative code/AGENTS/CMake patch against RE02. Do not apply
  it after copying the same overlay replacement files. Evidence/readme/log
  files are supplied separately in the overlay; the patch is code-only.

Neither archive contains original game libraries/assets, build caches, bulk
Ghidra output, or a purported new game executable. Source manifests and real
build/test/reference/asset checks are included in `RE03_verification/`.

## Build interfaces (not a request for user testing)

Existing Windows interfaces are retained:

```powershell
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
ctest --test-dir build/windows-msvc -C Release -R "^OpenAndroidUSM\.(CoreTests$|InputParity\.|QteParity\.)" --output-on-failure
```

These Windows commands were not executed here. Obtaining a cross-toolchain was
blocked by network/DNS failures; that does not establish Windows compilation,
rendering, audible audio or physical XInput success. The old uploaded executable
does not contain RE03. Native Linux tests cover the portable runtime/adapter/
geometry, not a graphical Linux port.

`docs/RE03_EVIDENCE.md` separates original behavior, deliberate controller
adaptation, tested fixtures, and unresolved integration. `RE03_VERIFICATION.md`
records final results and limitations. Previous release notes/screenshots are
historical and are not verification of this source checkpoint.
