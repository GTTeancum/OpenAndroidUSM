# OpenAndroidUSM RE05 — Verification

September 22, 2026. **Development source checkpoint, not near-final testing.**

## Changes made

**QTE control calls now run synchronously in their original order.** Begin
performs control-disable/key-reset before its state guard; even a rejected
request has that side effect. Accepted Begin enables the QTE control after
initialization, whereas compound children do not repeat the outer reset.
Success/failure disables that control, initializes result feedback, calls EndQTE,
and only then commits manager state. The bound application no longer replays a
stale deferred control release over a later cinematic decision.

**QTE completion resets slow motion only above a 1.0 denominator.** A pending
ratio-1 hold/ramp remains intact. The pause-button-preservation parameter is kept
separate from whole-game pausing; repeated EnableControls calls still reset the
keypad. Authored InterfaceControl and Collada UI calls use the same reset path.

**Gameplay keypad publication is separate from direct QTE/rescue/UI events and
physical controller state.** Reset clears the game-facing keypad/movement copy,
not direct flags or physical XInput. A real held stick cannot be turned into an
invented neutral sample for the QTE adapter. The next PC poll republishes gameplay
movement, and genuine release/repress can create a new keypad press.

The user-approved RE03 policy is unchanged: **left-stick direction alone for drag
QTEs**, with diagonals accepted, no A hold or touch tracing. **A remains tap/mash.**
The existing keypad action facade remains a PC integration boundary, not a claim
that all original 16-slot keypad and native event lifetimes are reconstructed.

## Final runs actually completed

| Build/run | Result | CTest elapsed time |
| --- | --- | --- |
| GCC 14.2 Linux Release, final expanded suite | **26/26 passed** | 78.30 s |
| Clang 17 Linux, AddressSanitizer + UndefinedBehaviorSanitizer and leak detection, O1 | **26/26 passed** | 149.68 s |
| Fresh exact RE04 source extraction + clean RE05 patch + fresh GCC Release objects | **26/26 passed** | 53.19 s |

These times are test-run durations, not game performance measurements. No
sanitizer errors were reported in the successful final run. New group counts:
**lifecycle (607)**, **integration
(138)**, and **keypad
(13,434,880)** checks. The keypad group
exhausts **65,536 distinct raw button masks**; its larger check count is multiple
assertions per case, not that many independent gameplay sessions.

The final fixtures cover every native manager guard state, accepted/rejected
requests, pre-commit state visibility, same-value resets, compound children,
nested success handoff, checkpoint copies, the slow-motion boundary, and a late
final mash press which produces both native success and failure control effects.

The original first-level **20004/config 6 -> 20006 success / 20010 failure**
fixtures remain, using the real scripts and RE04's immediate whole-scheduler
50 ms handoff. Script commands are observed rather than executed against a
complete rendered world. The new raw-controller fixture also reaches those
QTE outcomes after gameplay-input reset, without touch tracing or A for success.

First-level hostage **30018** is exercised with original object/player/config
assets and the shared control host. An activation-frame A press survives the
keypad reset, eight distinct A presses reach success, and interruption keeps
the interrupting player state and does not award a rescue. This is portable
runtime integration, not a full rendered scene or complete hostage acceptance.
Existing **262,144 raw axis cases** and **100,000 deterministic controller
transition frames / 1,200,000 checks** continue to pass, along with native core,
clock, sprite, audio-event, input and rescue regressions. No physical controller
or XInputGetState call was exercised.

## Source, reference and asset integrity

The code-only patch clean-applies to the exact delivered RE04 archive (SHA-256
`e2f4507d2cc8993a4401ba0c4687ef5c58d235c558121de695af145a2965a16a`). All **330 selected code/build/agent-rule/
reference files** match the clean tested tree byte-for-byte and were rechecked
after all tests. Clean builds reuse only unchanged original asset and dependency
source inputs through directory links—not prior objects or compiled outputs.

All **599 original data/reference files**, totaling **170,642,144 bytes**, were
freshly byte-compared to their members of the supplied 17-volume archive **after
the test runs**. ZIP member CRC checks succeeded for those reads. This does not
validate or expand the roughly 95 GB complete original project upload.

Original ARM library SHA-256:

```text
f35d959d54d43d3d4cce07d1afac4cbe52438997a3bf4d5304c50610cabc2679
```

**29 original function fingerprints** accompany the interpreted evidence. The
verifier accepts the original and rejects a deliberately modified scratch copy.
Neither fingerprint matching nor source-level rule tests prove equivalence to
executing ARM. **The original ARM game was not executed.**

The packaged payload manifest and external package check cover source hashes,
ZIP CRCs and exact reconstruction of the cumulative source by applying the RE05
overlay to RE04. No original game assets/library, compiler/SDK, build caches or
bulk decompiler output are redistributed.

## Earlier/failed development runs retained

Untouched RE04 passed **23/23** in 46.11 s.
An initial incremental build invocation was interrupted by its command timeout;
the resumed build and existing 23 tests passed. The first new test compilation
failed because a fixture aggregate omitted CinematicCommand's leading fields;
that fixture construction was corrected. The next new run passed lifecycle and
integration but exposed a bad keypad-test expectation about the dormant stored
R1 switch payload. The test was corrected to distinguish that retained direct
global from the reset keypad publication; production flags were not erased to
make the test pass. Intermediate successful runs are retained but are not used
in place of the final expanded runs above.

Windows cross-toolchain acquisition failed with DNS resolution (curl exit 6).
No Windows toolchain was acquired and no Windows application build succeeded.
The actual failure log is included. No donor game code was imported.

## Remaining gaps / verification boundaries

**No newly built Windows executable is included.** The application binding,
player-input selection and diagnostic autoplay changes were reviewed but **not
compiled or executed on Windows**. Linux outputs are native portable test
executables, not a graphical Linux port. GPU output, audible audio, physical
XInput, full QTE composition/success explosion and normal complete first-level
gameplay remain unverified.

Full pause/menu integration and native update/input lifetime, wall/enemy QTE
ownership, the hostage cutting-loop IsPlaying query, remaining hostage details,
the original failure-cue suppression mode and uploaded Sandman work remain
unfinished. The new pause/QTE enabled fields represent scoped original control
calls; they do not establish a working pause menu or full mobile-widget parity.

**This is a source checkpoint with portable regression evidence, not a near-final
acceptance build or completed one-to-one gameplay reconstruction.**

## Artifacts

`OpenAndroidUSM_RE05_Source.zip` is cumulative source through RE05.
`OpenAndroidUSM_RE05_Overlay.zip` applies over **RE04**. `RE05.patch` is an
alternative code/test/build-rule/fingerprint update, not a second step after the
overlay. Read `RE05_README.md`, `docs/RE05_EVIDENCE.md`, and `RE05_verification/`
for the source interpretations, commands, limits and retained actual records.
