# OpenAndroidUSM RE06 — Verification

September 22, 2026. **Source development checkpoint, not near-final testing.**

## What changed

**An earlier reconstruction error is corrected.** The hostage untie sound was
previously treated as a continuous loop with a requested/playing latch. Following
the actual virtual call into `Audible::PlayAudio` and `VoxSoundManager::Play3D`
shows that this transient path plays a **one-shot**, not a loop. The original
hostage queries playback every eligible update, only while the remaining mash
action count is exactly **7**. RE06 removes the latch and repeats that live query.
Completed, culled, or failed requests cannot silently remain marked as playing.
Earlier reports' loop interpretation is explicitly superseded, not erased.

**The hostage's own control calls are now bound.** In addition to RE05's QTE
manager operations, rescue-start and active-rescue branches repeatedly disable
ordinary controls/reset gameplay input. Failure, interruption, and end-animation
branches restore controls at their observed positions. These calls retain the
separate QTE event and physical controller state. A queued activation-frame A
press is not erased by the gameplay-keypad reset.

Sound dispatch happens during the object-update branch, with existing QTE cues
drained before the following hostage sound. Explicit and repeated stop branches
are preserved. Checkpoint copies do not store playback guesses or call-scoped host
callbacks. The Windows binding queries actual XAudio2 voice state rather than a
synthetic clip timer, but **that Windows code has not been compiled or executed**.

The user-approved drag policy is unchanged: **fresh left-stick direction alone,
including diagonals; no A hold or touch tracing. A remains tap/mash.** QTE timings,
counters, compound sequencing and authored outcomes are not retuned.

## Completed final runs

| Build/run | Result | CTest elapsed time |
| --- | --- | --- |
| GCC 14.2 Linux, fresh Release build | **29/29 passed** | 53.62 s |
| Clang 17 Linux, AddressSanitizer + UndefinedBehaviorSanitizer, O1, leak detection | **29/29 passed** | 165.25 s |
| Separate exact RE05 extraction, clean RE06 patch, fresh GCC Release objects | **29/29 passed** | 56.10 s |

No sanitizer errors were reported in the successful final run. These durations
are test-run times, not gameplay performance measurements. The clean build shares
only unchanged original assets and dependency **source** through directory links;
it does not reuse the development source tree, objects, or compiled libraries.

The new groups pass **dispatch: 109**, **recovery: 164**, and **controls: 140**
checks. They cover live query-before-play, existing-playback suppression, natural
completion, distance culling, injected backend failures, shared sound-ID scope,
repeated explicit stops, count-7 eligibility and idle-decay re-entry, checkpoint
copying, repeated control resets, interruption, timeout, and hook forwarding.
These are injected backend states, not captured physical audio-device behavior.

The first-hostage fixture uses original object **30018**, player/config data and
animations. It reaches the authored release/reward/thank/idle sequence through the
bound sound/control path, checks no duplicate reward/thank dispatch, and preserves
the interrupting player state without rescue rewards on failure. Animations are
advanced in controlled fixture steps, not a natural rendered playthrough.

Existing first-level **20004/config 6 -> 20006 success / 20010 failure** QTE fixtures
still pass, along with **262,144 raw stick-axis cases**, exhaustive raw button-mask
checks, and **100,000 deterministic controller-transition frames**. Their script
commands are observed rather than executed against a complete rendered world.
No physical gamepad or XInputGetState call was exercised.

## Baseline and intermediate work

The untouched delivered RE05 passed **26/26** in 49.37 s before the changes.
The first expanded intermediate suite passed **29/29** in 51.71 s. A later targeted
three-group run passed after adding complete thank/idle and timeout control checks;
the full final runs above include those additions. Intermediate logs are retained
and are not substituted for the final clean and sanitizer evidence.

One local scene-audit helper initially assumed a single XML document element. The
original room files also contain XML fragments; the helper was corrected to wrap
those for metadata inspection. This did not alter original assets or gameplay
source. The final asset witness identifies the actual room-2 hostage node. The
negative-reference failures are intentional verification tests, not game failures.

## Reference, original material, and tested-source integrity

Original library: `game/original/libspiderman.so`, 7,713,000 bytes; SHA-256:

```text
f35d959d54d43d3d4cce07d1afac4cbe52438997a3bf4d5304c50610cabc2679
```

The verifier checks **45 function fingerprints** and the hostage's actual **three
Audible virtual slots**. It accepts the original reference, rejects a deliberately
modified temporary image, and rejects a deliberately incorrect virtual-target
manifest. All addresses and the one-shot interpretation are documented in
`docs/RE06_EVIDENCE.md`. **No original ARM execution or differential run occurred.**

After all final test runs, all **599 original game-data/reference files**, totaling
**170,642,144 bytes**, were freshly read from their supplied 17-volume ZIP members
and compared byte-for-byte with the used copies. CRC checks passed for those
member reads. This is not a full extraction/CRC validation of the approximately
95 GB expanded original project archive. The read-only comparison helper and its
complete selected-file receipt are included under `RE06_verification/`.

The code patch clean-applies to the exact RE05 source ZIP, SHA-256
`6133209ac787a47272c653a6a84ab6a2da859a78433a4c2874dee49f5bd585f8`.
All **333 selected code/build/agent-rule/reference/tool files** match the clean
patched tree byte-for-byte and were rechecked after testing. The exact selection
is recorded in `tested-source-files.json`; it excludes reports and build caches.
The patch changes/adds 13 files in this selection. These are source identity
checks, not evidence that Windows-only units were compiled by Linux tests.

The final package check separately validates ZIP CRCs, all payload hashes, and
exact reconstruction of the entire cumulative source by applying the overlay to
RE05. `RE06_PAYLOAD.json` excludes only itself; previous checkpoint manifests are
retained historical records. The external package check records archive hashes
without a self-referential checksum.

## Remaining limits

**No newly built Windows executable is included.** The Windows application and
new XAudio2 method are edited/reviewed but uncompiled. No Windows SDK/import-library
toolchain was established; an installed clang-cl frontend alone is not that
complete toolchain. The host-toolchain status is recorded. Linux outputs are
portable runtime/test executables, not a graphical Linux port.

GPU output, audible audio, physical XInput, and a normal complete first-level
playthrough remain unverified. Original rescue camera modes, full pause/menu and
input lifetimes, other hostage details, wall/enemy QTE ownership, persistent
Audible emitter behavior, original failure-cue suppression mode, success
explosion/full QTE composition, and the uploaded Sandman work remain unfinished.
The current transient dispatcher is not a complete original audio-manager port.

**This is not a near-final acceptance build or completed one-to-one port.** No
user-side acceptance testing is requested by this checkpoint.

## Preserved artifacts

`OpenAndroidUSM_RE06_Source.zip` is cumulative source through RE06.
`OpenAndroidUSM_RE06_Overlay.zip` applies over **RE05**, not the untouched upload.
`RE06.patch` is an alternative code/test/build-rule/reference-tool update; do not
apply it twice after merging the overlay. Readmes, evidence and logs are additional
overlay contents. Original game assets/library, SDK, build caches, bulk decompiler
output and game executables are not redistributed.
