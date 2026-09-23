# OpenAndroidUSM RE06 — Source development checkpoint

Native C++ PC reconstruction of the supplied Android game, cumulative through
RE06 over the exact delivered RE05 source. **Not near-final testing.**

## What this adds

The earlier hostage cutting-loop interpretation is corrected: the original
transient Audible path plays an untie one-shot and queries whether it is still
playing on each eligible object update (remaining actions ==7). RE06 removes the
request-is-playing latch, binds synchronous sound dispatch and live host playback
queries, and restores the hostage's own repeated control-disable/reset and
control-restoration calls. See `docs/RE06_EVIDENCE.md` for exact addresses, the
actual virtual-call chain, scoped implementation limits, and the explicit
correction to the old reports.

Left-stick-only directional drag QTEs remain unchanged. Diagonals count; no A
hold or touch tracing. A remains tap/mash. Gameplay rules are not retuned.

## Applying this checkpoint

- `OpenAndroidUSM_RE06_Source.zip` is the complete cumulative source checkpoint.
- `OpenAndroidUSM_RE06_Overlay.zip` contains only changed/new files for the exact
  delivered **RE05** source (SHA-256 below), including new evidence and logs.
- Both ZIPs contain the `OpenAndroidUSM/` directory. Extract into its parent, or
  merge that directory's contents into the existing project. Avoid nesting an
  extra project directory.
- `RE06.patch` is an alternative **code/test/build-rule/reference-tool** update
  from RE05. Do not apply it a second time after extracting the overlay. Reports,
  readmes and logs are additional overlay contents, not all part of that patch.
- Preserve the original game/dependency uploads. Original assets, ARM library,
  compiler/SDK, decompiler dumps, build caches and game executables are excluded.
  These source ZIPs alone are not a self-contained runnable game distribution.

RE05 baseline source SHA-256:
`6133209ac787a47272c653a6a84ab6a2da859a78433a4c2874dee49f5bd585f8`.

## Reference and build checks

From the project root, with the original supplied library in its existing place:

```text
python tools/verify_re06_reference.py
```

This checks 45 function fingerprints and the actual hostage Audible virtual slots.
It does not execute ARM or prove whole-game equivalence.

`RE06_verification/build-linux.sh release` and `... sanitize` retain the exact
portable build/test commands used here. They expect the original offline
dependency source directories under `build/windows-msvc/_deps` and original data
under `game/data/gameloft/games/spiderman`. The `clean` run was performed in a
separate RE05-plus-patch extraction with fresh objects. The existing Windows
project/build presets remain; **no Windows build success or new EXE is included**.

Read `RE06_VERIFICATION.md` for actual final results and limits. The external
`OpenAndroidUSM_RE06_Package_Check.json` records final archive identities and exact
RE05-plus-overlay reconstruction. `RE06_PAYLOAD.json` covers cumulative payload
paths/sizes/hashes except itself. Earlier payload manifests are historical records
of their own checkpoints, not manifests for the RE06 tree.

## Remaining blockers

Windows app/audio/renderer compilation and execution; physical XInput; a normal
complete first-level playthrough; original rescue camera modes; full pause/menu
and input lifetime; remaining hostage and wall/enemy QTE integration; persistent
emitter behavior; failure-cue suppression mode; QTE success explosion/full
composition; and the uploaded Sandman work. This checkpoint is not a request for
user-side acceptance testing and does not claim these gaps are resolved.
