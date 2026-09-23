# OpenAndroidUSM — RE05 continuation

**Development source checkpoint. No newly built Windows executable is included.**
No user-side acceptance testing is requested at this stage.

RE05 reconstructs QTE Begin/End control-call ordering: a rejected request still
performs the native control-disable/key-reset call; accepted compound children
do not repeat it; result feedback restores controls synchronously before the
manager state write. QTE completion resets slow motion only above a 1.0
denominator. Cinematic interface controls use the same reset boundary.

The PC gameplay keypad publication is now separate from direct QTE/rescue/UI
flags and physical XInput state. Your drag controls are unchanged: **move the
left stick in the general requested direction**, with diagonals accepted and
no A hold/touch tracing. **A** remains tap/mash.

## Packages and application

`OpenAndroidUSM_RE05_Source.zip` contains cumulative source through RE05.
`OpenAndroidUSM_RE05_Overlay.zip` applies over the **exact delivered RE04 source**,
not the original unmodified upload. Both have an `OpenAndroidUSM/` root folder.
Keep the supplied original game and dependency material from the 17 volumes.
Neither package duplicates original game data, library, SDK/toolchain, compiled
objects or bulk decompiler output.

`RE05.patch` is the alternative code/test/build-rule/fingerprint patch. From a
clean RE04 root, run `git apply --check RE05.patch` followed by `git apply
RE05.patch`. **Do not apply the patch after extracting the overlay.** The patch
alone does not install the separate readme, evidence, reports and test logs.
Historical RE01–RE04 manifests and reports describe their respective snapshots,
not the current tree.

`RE05_PAYLOAD_MANIFEST.json` hashes every current packaged payload file except
itself. The external `OpenAndroidUSM_RE05_Package_Check.json` records archive
hashes, CRC checks and overlay-to-cumulative-source equality.

## Evidence and rebuild record

Read `docs/RE05_EVIDENCE.md` for source addresses and the distinction between
recovered native rules and PC input publication. `RE05_VERIFICATION.md` records
measured results and limits. Actual command/test/error records are retained in
`RE05_verification/`.

Portable Linux builds use the supplied source dependencies offline:

```sh
bash RE05_verification/build-linux.sh release
bash RE05_verification/build-linux.sh sanitize
```

The helper expects `game/data`, `game/original`, and the originally supplied
cached dependency sources in `build/windows-msvc/_deps`. It does not fetch a
replacement original game or install a proprietary SDK. The recorded clean
patch build uses a fresh RE04 source extraction and fresh objects, sharing only
unchanged original assets and dependency sources via directory links.

Reference fingerprint verification uses the existing standard-library helper:

```sh
python tools/verify_re02_reference.py --manifest docs/references/re05-original-fingerprints.json
```

The existing Windows CMake/Visual Studio path remains, but the Windows app,
D3D11, audible XAudio2 and physical controller execution are **not verified by
the portable suite**. Full pause integration and normal complete first-level
play still remain unfinished; this delivery is not a near-final testing build.
