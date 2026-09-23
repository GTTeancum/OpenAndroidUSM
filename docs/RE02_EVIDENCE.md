# RE02 — Original QTE, rescue, and PC input reconstruction

Reference date: September 22, 2026. All addresses in this document are **ELF
instruction addresses**, with the Thumb bit removed. The supplied Ghidra
exports are rebased by **+0x10000**. This document supersedes the old isolated
A-tap cinematic shortcut described in the uploaded reconstruction notes.

## Scope and reference

This continues the delivered RE01 native C++ source. It does not execute the
original ARM code, introduce an emulator, call a binary compatibility bridge,
or reconstruct gameplay from screenshots. The reference is the supplied
`game/original/libspiderman.so`, 7,713,000 bytes:

`f35d959d54d43d3d4cce07d1afac4cbe52438997a3bf4d5304c50610cabc2679`

`docs/references/re02-original-fingerprints.json` pins 48 retained functions by
symbol name, ELF address, length, and byte hash. Run
`python tools/verify_re02_reference.py` against the supplied local library.
A deliberately altered temporary copy was rejected. Fingerprint verification
establishes the reference identity, **not gameplay equivalence**.

A fresh comparison read the 599 `game/data` and `game/original` members from
all 17 uploaded split ZIP volumes and compared their hashes with the working
files. All 170,642,144 bytes matched. This does not claim verification of the
unused roughly 95 GB expanded archive. No original binary, game asset, or bulk
decompiler output is redistributed in RE02.

## 1. Timeout, update, and pause clocks

`CQTEManager::IsOutTime` at `0x0037a4b8` compares
`((float(timerNow) - start) - pauseAdjustment) > duration`. This is not the
uploaded accumulated-simulation `>=` test. `BeginNowQTE`, `0x0037aa08`, records
the absolute timer. `SetPause`, `0x0037a4fc`, preserves its unusual native
arithmetic: the pause marker is `now - (now - start)`; resume overwrites the
adjustment with `now - marker`. It does not accumulate conventional pause
lengths. RE02 retains each float32 operation, strict boundaries, and the
non-modular behavior at the 32-bit timer wrap.

`Application::GetRealTs`, `0x003ce5d4`, is a separate unslowed update duration.
Mash idle uses it, not scaled world time. The supplied animation code also uses
it. `irr::os::Timer::getTime` / `tick`, `0x0042aea0` / `0x0042b054`, return the
cached timer value shared by inner catch-up steps. The host loop now provides
one absolute sample per outer tick, alongside separate real and simulation
steps. It retains the inherited fixed-step/catch-up policy.

`CLevel::PauseTimer` / `ResumeTimer`, `0x0036f42c` / `0x0036f3e4`, call the QTE
pause API in the original. The reconstructed clock's pause API is tested, but
**the PC Start/pause-menu lifecycle is not reconstructed by this change**.
An accurate clock method alone is not a claim of working pause integration.

## 2. Authored drag gesture, not an A-tap shortcut

The linked first-level cinematic is **20004**. Its command at **2000 ms** uses
config **6**, success **20006**, and failure **20010**. The previous isolated
smoke fixture used synthetic source ID **20005**, which is not this cinematic.
The new integration fixture uses the actual linked asset and both actual
outcomes. `RE02_verification/asset-witness.json` records original member hashes
and the CFF attributes read directly from the supplied archive.

Config 6 is interaction type 1, value 14, origin `(360,101)`, duration 3900 ms.
Its path is interface animation 5: fourteen authored samples ending at
`(360,251)`. `CalculateEndPos`, `0x0037a3f8`, adds the sprite offsets to the
configuration in float32, truncates to an integer, and retains the signed
16-bit coordinate. No replacement curve is generated.

`UpdateDragState`, `0x0037acd4`, selects the closest sample using the deciding
axis only: X for values 11/12 and Y for 13/14. Strict `<` makes an equidistant
position retain the earlier sample. It snaps both coordinates to that sample;
the final sample succeeds. The release event in `onEvent`, `0x0037abf0`, uses a
different rule: both axes must be within ten pixels, inclusive, of the stored
endpoint. RE02 preserves the distinction rather than substituting a distance
circle or a success percentage.

The original `appKeyPressed`, `0x003cc75c`, supplies a Cross flag for tap/mash.
The recovered manager does not use that flag to force state-1 drag success.
The PC adapter therefore uses **A as a virtual touch hold and the left stick
as positional displacement**. Both axes use the authored primary-axis span;
positive XInput Y is inverted to screen Y. That mapping is an explicit host
input policy, **not claimed original Xperia hardware behavior**. The native
manager remains the sole success/failure judge. Diagnostic autoplay sends the
same original samples; its `qte_tap` command is no longer a drag-success cheat.

The archive also contains five additional QTE-bearing CFFs for source IDs
20016, 20045, 20051, 20062, and 20081. Their rooms are not among the thirteen
links in `levelnew_01.irr`. They are not promoted into live first-level gameplay
or counted as normal-flow coverage simply because their files exist.

## 3. QTE order and result handoff

`CQTEManager::Update`, `0x0037b240`, is reconstructed as explicit states
0/1/2/3/4/5/6 for tap, drag, mash, success display, failure display, success
handled, and failure handled. Inactive is -1.

Tap/mash input runs **before** the timeout. A final late tap can produce tap,
success, and then failure sounds in that order. Drag timeout runs **before**
positional completion; reaching the final sample in that same update can
replace the failure with success. These are observed instruction-order quirks,
not changes chosen for game feel.

`BeginQTE`, `0x0037ab40`, ignores a new Begin in states 0 through 3. Compound
record 2 traverses its authored `[0,3,1]` children. `SetState`, `0x0037a67c`,
returns early when the state is unchanged: consecutive drag children retain
the previous result animation and release endpoint, even while nearest-sample
updates read the new child. The tests exercise this easily lost detail.
Random-position records use the shared existing native randomizer; an absent
shared randomizer is rejected instead of silently supplying a made-up seed.

`SetState(3/4)` emits sounds 0x188/0x189, restarts the original result animation,
and calls `EndQTE` (`0x0037a5b0`) to restore control and reset slow motion.
Outcome dispatch is later, via `SuccessHandle` / `FailHandle`,
`0x0037a5e4` / `0x0037a630`; the source cinematic is removed then.

`CSpriteInstance::UpdateSpriteAnim`, `0x002d9c10`, uses a strict residual
`> 50.0` loop and its own tick/frame advancement, not a conventional
accumulated-duration player. The original 50.0 constant is at `0x00505380`.
`SetAnim`, `SetAnimSafe`, `Restart`, and `IsAnimEnded` at
`0x002d9ce8`, `0x002d9d04`, `0x002d9cc4`, and `0x002d9d1c` preserve restart,
residual, same-animation, and last-frame rules.

`CQTEManager::Draw`, `0x0037a7c8`, changes state. The constructor sums animation
7's durations to eight, and Draw compares before incrementing its counter;
normal success therefore hands off on the **ninth Draw call**. Success Update
does not advance that result sprite. Failure advances its sprite and tests
animation completion. Photo result animation 29 has its separate one-frame
end behavior. The application calls `drawStep` once per presented frame,
not once per capped simulation substep.

This reconstructs **result timing**, not complete QTE rendering. The original
sprite composition, explosion scaling/alpha, control-button drawing, and
failure fading are not all wired to Direct3D. The inherited generic prompt/bar
still exists. No pixel-accurate UI or successful Windows rendering is claimed.

## 4. Hostage rescue ordering and original sound cues

`CHostage::Update`, `0x00328068`, runs as an object update before the shared QTE
manager later in `CLevel::Update`, `0x003720bc`. RE02 separates the two phases;
the hostage observes the **previous** manager result/action count. The
convenience fixture wrapper retains object-then-manager ordering.

The cutting loop 0x18b is not started merely by entering the QTE. The original
checks for exactly seven remaining actions in the object phase. Config 11 has
eight actions, so this is normally observed on the object update after the
first accepted tap. Idle decay still restores only one action per qualifying
update, even after a large real-time delta, and runs after success checking.

Interrupting the player during the rescue no longer automatically overwrites
the interrupting state or grants completion rewards. Forced failure queues the
original failure cue before the cutting-loop stop. Rescue-end transitions keep
the original independent state-mismatch/animation-complete tests and repeated
stop requests.

The separate QTE-manager sounds **0x18a (tap), 0x188 (success), 0x189 (failure)**
now use the existing application Play2D path; the hostage's cutting/thanks
spatial cues remain separate. All four relevant supplied sound entries were
decoded to PCM by the portable production decoder. This verifies decoding and
queued events, **not audible XAudio2 playback or matching acoustic timing**.

The original hostage constructor (`0x00328c20`) defaults ButtonHeight to 85.
`ProcessUserAttr`, `0x00328724`, subtracts 100 only for a positive authored
value. The shipped -1 therefore means default 85, not -101. The port now follows
that branch. `Player::IsUltimateState`, `0x0033002c`, checks IDs 107 through 113;
these also block the rescue prompt/initiation as in the inspected path.

Remaining hostage differences include the native ambient-help/random cadence,
room activation and full input ownership, animation-completion generality,
checkpoint Save/Load shape, and audio-manager `IsPlaying` feedback (the PC uses
a request latch). These have not been declared equivalent.

## 5. XInput device cancellation and switch consumers

A physical R1 release in `appKeyReleased`, branch `0x003cc2b8–0x003cc2ea`,
sets rescue, an interactive-button flag, and the switch counter to 4.
Disconnecting a PC controller is not that event. The old translator synthesized
key-up events on disconnect, which could now start a rescue. RE02 sends one
explicit transport-cancel notification, clears held/pending state across
contexts, and emits no native release action. This is a PC device-lifecycle
fix, not invented Android key-handler semantics.

Reconnection's treatment of physically held buttons is still the inherited
translator policy: a newly observed held button produces a down event. Full
cross-context arbitration and the exact lifetime of all native global flags
remain to be audited. The drag adapter's cancellation/fresh-edge tests do not
claim a physical reconnect interlock that the poller does not implement.

Further original inspection identifies real switch consumers, without adding
an invented nearest-object shortcut:

- `InteractiveButton::Update`, `0x0032f1ac`, consumes and clears the boolean at
  `0x00566518`, then performs its object's event path.
- `CSwitchObject::Update`, `0x00310828`, reads the word at `0x0056651c` and
  arithmetic-shifts it right. In-range, a nonzero pre-shift value also calls
  `doAction` (`0x003107a4`); the out-of-range branch still shifts it. The literal
  4 is therefore not evidence of a target-object index or single-use boolean.

These reader functions are fingerprinted and recorded for continuation, but
**their object interaction systems are not newly wired in RE02**. Native object
iteration order and activation matter; resetting or consuming this counter
once per host frame would not reproduce the inspected code.

## Verification interpretation

The supplied logs distinguish the baseline, intermediate failures, corrected
final release/sanitizer runs, and clean source rebuild. A test named “parity”
means it checks a recovered rule; it is not a side-by-side ARM execution test.
The clock and sprite stress oracles are independent instruction-shaped C++
transcriptions. No original ARM instruction was executed.

The first-level integration fixture advances the actual cinematic scheduler
to the actual StartQTE command and dispatches both authored outcomes. Other
cinematic commands are observed rather than executed against a complete world.
The hostage fixture positions the diagnostic player at asset 30018. Neither
fixture demonstrates normal title-to-level play, rendered combat, physical
XInput, or audible audio.

**The first-level graphical/audio review milestone and near-final testing
readiness have not been reached.** Windows app compilation/runtime, shared QTE
ownership, original QTE visual composition, pause/input lifecycle, and the
uploaded Sandman work remain open. No pre-existing executable is relabeled as
a new or verified RE02 build.
