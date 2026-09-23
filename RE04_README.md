# OpenAndroidUSM — RE04 continuation

**Development source checkpoint, not a near-final test build.** No newly built
Windows EXE is included. Continue using the original supplied project material;
this delivery does not replace the Android reference or game assets.

RE04 connects cinematic and hostage interactions to one level-owned QTE manager,
reconstructs its request guard and per-update button consumption, and makes
success/failure cinematic handoffs synchronous with the original 50 ms manager
step and pre-commit state ordering. The RE03 controller adaptation is retained:
**move the left stick in the requested general direction** for drag QTEs;
**A** remains tap/mash. There is no touch-tracing requirement.

## Packages

`OpenAndroidUSM_RE04_Source.zip` contains cumulative source through RE04.
`OpenAndroidUSM_RE04_Overlay.zip` contains changed/new files to apply over the
**exact delivered RE03 source**, not the unmodified initial upload. Both archives
contain an `OpenAndroidUSM/` root directory. Keep the matching original `game/`
data and dependency material from the 17 uploaded archive volumes.

`RE04.patch` is an alternative for the code/test/build-rule/fingerprint changes:
apply it once from a clean RE03 source root with `git apply --check RE04.patch`
and `git apply RE04.patch`. **Do not apply it after extracting the overlay.**
The patch does not install the additional reports, readmes or verification logs;
those are supplied by the source/overlay archive. Historical RE01–RE03 manifests,
patches and reports describe their own checkpoints, not the latest tree.

`RE04_PAYLOAD_MANIFEST.json` records the current packaged source payload, excluding
itself. The externally delivered `OpenAndroidUSM_RE04_Package_Check.json` records
final archive hashes, CRC checks and overlay-to-cumulative reconstruction checks.

## Evidence and validation

Read `RE04_VERIFICATION.md` for measured results and limits, and
`docs/RE04_EVIDENCE.md` for the original ELF addresses and their interpretations.
`RE04_verification/` retains actual logs, failed development-fixture evidence,
reference identity checks, clean patch/build records and original-data hashes.

The native portable suite is built offline from the already supplied dependency
source copies. The recorded helper is `RE04_verification/build-linux.sh` with
`release`, `sanitize` or another build-directory suffix as its argument. It
requires the original cached dependencies in `build/windows-msvc/_deps` and the
original data/reference at `game/data` and `game/original`; it does not install
SDKs or acquire missing proprietary material. A clean source rebuild reused
these unchanged inputs, but no previous source or compiled object outputs.

Existing Windows CMake/Visual Studio project paths remain. Their application
code, rendering and XInputGetState execution are **not verified by the Linux core
suite**. No new user-side acceptance testing is requested for this checkpoint.
