# OpenAndroidUSM RE04 — Verification

September 22, 2026. **Development continuation; not near-final acceptance testing.**

## Changes in this checkpoint

Cinematic and hostage QTEs now share one level-owned manager instead of advancing
separate progress, clock, sound and outcome systems. Hostage interruption/retry
and feedback use that same state. The native Begin guard is applied before config
lookup; source and outcome cinematic IDs independently honor their **-1** sentinel.

The QTE update now consumes the native per-update QTE/jump/rescue flags on the
appropriate branches, without issuing physical key releases or erasing unrelated
input and the existing two-update keypad history. This prevents a consumed QTE
press from also reaching later QTE-action or UI handlers in the same update.

Success/failure handoff is now synchronous. It clears the current pause/IGM press,
removes the optional source, finds/adds the optional outcome, and advances the
**entire cinematic manager by the original fixed 50 ms** before committing the
QTE's handled state. This preserves the native ordering even for a nested request
that observes the old success/failure-display state. Missing outcome lookup is a
no-op rather than an invented fallback.

The RE03 controller-native adaptation remains unchanged: **left-stick direction
alone for drag QTEs**, accepting diagonals; no A hold, touch tracing or touch
release. **A remains tap/mash.** Its thresholds are explicit PC input policy, not
claimed original Android constants.

## Final runs actually completed

| Build/run | Result | CTest elapsed time |
| --- | --- | --- |
| GCC 14.2 Linux Release, updated development tree | **23/23 passed** | 43.40 s |
| Clang 17 Linux, AddressSanitizer + UndefinedBehaviorSanitizer + leak detection, O1 | **23/23 passed** | 149.03 s |
| Fresh exact RE03 extraction, clean RE04 patch, fresh GCC Release build | **23/23 passed** | 45.35 s |

The fresh clean build reused the unchanged original data and dependency source
inputs through directory links, **not previous source files, compiled objects or
build outputs**. All **324 code/build/agent-rule/reference files** selected by the
recorded comparison match the development tree byte-for-byte, rechecked after
the tests. The tested code patch and baseline archive hashes are recorded in
`RE04_verification/clean-patch-apply.json`.

New test groups: **shared_manager (1,281 checks)**, **consumption (852,076)** and
**handoff (69)**. They include native-state guards, overlapping requests,
independent -1 source/outcome cases, failure feedback, interruption/retry,
checkpoint binding, every raw 16-bit button mask, whole-manager 50 ms stepping,
pre-commit state visibility, pause-press clearing, and dispatch failure handling.
The raw input states are supplied by fixtures, not physical-controller captures.

The two first-level fixtures load the original **20004 / config 6** command and
its real **20006 success / 20010 failure** scripts. Both run through the portable
synchronous handoff and immediate 50 ms scheduler update. Script commands are
observed, not executed against a complete world. Existing controller checks still
include **262,144 raw axis cases**, held/disconnect guards and **100,000 deterministic
transition frames / 1,200,000 checks**. Original clock, sprite, audio-event, rescue,
core and other input regressions also passed. No sanitizer errors were reported
in the successful complete run.

## Failed and earlier development runs

Untouched RE03 passed **20/20** here. The initial shared-manager change passed
those same 20 tests. An intermediate **21/22** run exposed a test-fixture error:
the retry helper expected an empty hostage sound queue before draining a genuine
failure StopLoop event. The fixture was corrected to assert and consume that
expected event; production behavior was not changed to satisfy the bad premise.
An intermediate 22/22 run preceded the final handoff group. Failed and successful
logs are preserved rather than replaced by only the final pass.

The optional analysis-package install and Windows cross-toolchain acquisition
attempts failed. The existing LLVM disassembler/readelf and supplied reference
were sufficient for this reconstruction. No Windows SDK/toolchain was acquired,
no donor game code was imported, and no Windows build success is claimed.

## Reference and original-material integrity

All **599 original game-data/reference files**, totaling **170,642,144 bytes**,
were freshly read from their members in the supplied 17-volume archive and
compared byte-for-byte with the used copies. ZIP member CRC checking succeeded
for those reads. This is **not** a full extraction or CRC validation of the roughly
95 GB expanded original project upload.

Original ARM library SHA-256:

```text
f35d959d54d43d3d4cce07d1afac4cbe52438997a3bf4d5304c50610cabc2679
```

**17 function fingerprints** accompany the interpreted evidence. The verifier
passes on the original and rejects a deliberately modified temporary copy.
Reference identity and transcribed-rule tests **do not establish equivalence to
executing ARM**; no original ARM game execution or differential run occurred.

The final payload manifest and external package check cover the packaged source,
ZIP CRC/hashes and exact reconstruction of the cumulative source by applying the
RE04 overlay to the delivered RE03 tree. No original game data, library, SDK,
build caches or bulk original decompiler output is redistributed.

## Remaining gaps and verification boundaries

**No newly built Windows executable is included.** Application/D3D11 integration
was edited and reviewed but not compiled here. GPU output, original success
explosion/full QTE composition, audible audio, physical XInputGetState operation,
and normal complete first-level gameplay remain unverified.

Native Begin's pre-guard control-disable/key-reset side effects and complete
control-release ordering remain unfinished. So do the pause/menu integration,
full level update/input lifetime, wall/enemy QTE ownership, the exact hostage
sound IsPlaying integration, remaining hostage behavior, and the uploaded Sandman
work. Merging the cinematic/hostage manager does not resolve all those systems.

This is a source checkpoint with portable regression evidence, **not a playable
Windows acceptance build or a claim of completed one-to-one gameplay parity**.

## Artifacts

`OpenAndroidUSM_RE04_Source.zip` is cumulative source through RE04.
`OpenAndroidUSM_RE04_Overlay.zip` applies over the exact delivered **RE03** source.
`RE04.patch` is an alternative for code/test/build-rule/fingerprint changes; do not
apply it again after extracting the overlay. Readmes, reports and logs are
additional overlay contents. See `RE04_README.md`, `docs/RE04_EVIDENCE.md` and
`RE04_verification/` for the instructions, interpreted evidence and actual records.
