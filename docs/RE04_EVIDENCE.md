# RE04 — Shared QTE manager, input consumption and synchronous cinematic handoff

September 22, 2026. This is interpreted evidence from the supplied reference, not
an assertion that the reconstructed program has been differentially compared
against the running ARM game. No ARM game code was executed for this checkpoint.

## Reference identity and address convention

The sole game-code reference is the supplied `game/original/libspiderman.so`:
7,713,000 bytes; SHA-256
`f35d959d54d43d3d4cce07d1afac4cbe52438997a3bf4d5304c50610cabc2679`.
`docs/references/re04-original-fingerprints.json` records byte fingerprints for
17 defined dynamic function symbols. Addresses in this document are **ELF
instruction addresses**, with the Thumb bit cleared. The supplied Ghidra exports
use **ELF + 0x10000**, not those addresses unchanged.

Verification uses the existing standard-library-only parser:

```sh
python tools/verify_re02_reference.py \
  --manifest docs/references/re04-original-fingerprints.json
```

The verifier passed against the original and rejected a deliberately changed
**temporary copy**. Identity checking is not a behavioral-equivalence test. The
original file was not patched. Raw original instructions, bulk decompiler
output and original assets are not included in this source delivery.

## 1. Cinematic and hostage paths share the level manager

`CHostage::Update` at **0x00328068** loads the QTE manager from the level's
**+0x54** member. At **0x00328366–0x00328376** it calls `BeginQTE` with config
**11** and three **-1** cinematic parameters. It then enters its QTE phase;
there is no private per-hostage mash manager or ownership-priority check.
The subsequent hostage phase reads that manager's state at **+0x7c**.
States **3/5** mean success and **4/6** mean failure. The interruption path
calls `ForceFailQTE` at **0x0032840e**. The cutting-sound branch at
**0x0032844c–0x00328450** compares the signed remaining-action count to literal
**7** before checking the original sound's playing state.

The native cinematic `StartQTE` entry at **0x0036153c** also reaches the level
manager. RE03 instead had a separate hostage progress/clock/result subsystem.
RE04 binds `LevelHostageRuntime` to the same `QuickTimeEventRuntime` used by
cinematic commands. Its non-owning binding must outlive the hostage runtime and
checkpoint copies. Application initialization binds the manager first.

The normal application loop updates this manager once, consumes its sound and
control-release queues once, and uses its existing result-feedback renderer for
both callers. `observeQuickTime` reads manager state without advancing time or
consuming effects. The hostage object phase still observes a press/result on its
next object update, not by prematurely awarding rescue completion inside button
processing. Interruption forces the shared failure state without replacing the
interrupting player state or awarding rescue completion.

This does **not** introduce an invented “hostage wins” policy. Overlapping
requests, including the original behavior when an existing cinematic QTE is
observed by a hostage, are covered by fixtures. The cutting-loop host latch is
still not a full reconstruction of the original `IsPlaying` query; audible
voice lifetime and some later hostage behavior remain unresolved.

## 2. Begin guard and optional cinematic IDs

`CQTEManager::BeginQTE` at **0x0037ab40** uses an unsigned **state > 3** guard
before config lookup. Thus inactive **-1**, failure display **4**, and handled
states **5/6** accept a request; **0–3** refuse to replace the current QTE.
The guard is not conditioned on caller priority. `ForceFailQTE` at
**0x0037a9fc** requests state **4** unconditionally; the shared `SetState`
same-state guard prevents duplicate transitions.

RE04 puts the state guard before config lookup and keeps the original special
meaning of **-1**. Source removal and outcome selection are independent: a source
can be removed even with no outcome, and a missing source does not prohibit an
outcome. The port no longer tries to launch a cinematic with ID -1. It also does
not fabricate a fallback when the original `FindCinematic` would miss.

There is an important remaining boundary: native `BeginQTE` invokes
`CLevel::EnableControls(false, true)` **before even a rejected request**.
The full synchronous control-disable/reset/re-enable side-effect sequence is not
implemented in this checkpoint. The current active-QTE control gates and queued
release notifications must not be mistaken for complete control-scheme parity.
Strict authored-data validation also remains a port diagnostic policy; malformed
or missing config behavior is not claimed to match unchecked ARM operations.

## 3. Original per-update input consumption

`CQTEManager::Update` at **0x0037b240** clears the native globals after consuming
input, rather than fabricating a physical key release:

| Path | Original evidence | Published input cleared by RE04 |
| --- | --- | --- |
| Tap, action present | 0x0037b27e–0x0037b286 | QTE press |
| Mash, every update | 0x0037b2c0–0x0037b2c4 | Rescue press |
| Mash, action present | 0x0037b2c6–0x0037b2d4 | QTE press and jump press |

`QuickTimeEventRuntime::update` returns an explicit `QteInputConsumption` mask.
The mask is based on the branch that processed the input, before a successful
compound child changes state. Application wiring clears the corresponding
published router/autoplay flags before later QTE-action and UI consumers.
Held physical buttons, switch data, unrelated actions and the existing keypad's
two-update press history remain separate and are not wiped.

`CLevel::Update` at **0x003720bc** supplies the manager-before-QTE-action ordering
used here. Its modal-tutorial branch still updates QTE paths while skipping much
of the world. This evidence does not authorize an invented whole-game pause
behavior; the full original level update ordering and pause menu remain open.

## 4. Outcome handoff is synchronous, with a 50 ms cinematic-manager step

`SuccessHandle` (**0x0037a5e4**) and `FailHandle` (**0x0037a630**) independently
remove the optional source, find/add the optional outcome, then call the
**whole cinematic manager's Update with float 50.0** (literal **0x42480000**).
It is not the next host frame's delta, a scaled gameplay delta or an update of
only the newly added outcome. The update is skipped when no outcome is found.

`SetState` (**0x0037a67c**) clears the pause/IGM press and calls the appropriate
handler at **0x0037a7a0–0x0037a7b2**; it stores the handled state at
**0x0037a7b6**, **after** that call. This matters if a cinematic command starts a
QTE during the immediate manager step. A start sees old success-display state 3
(and is refused) or old failure-display state 4 (and is accepted); the outer
state write still follows. RE04 preserves this ordering rather than “fixing” it
with an invented reentrancy/priority policy. Nested-request tests are synthetic
ordering fixtures, not a claim that the first level authors such a request.

`QteCinematicHandoff` is a portable synchronous bridge. Live update/draw passes
a callback; source release, removal, start and the whole-manager **50 ms** step
occur before the QTE state write. The application reuses its normal cinematic
command-dispatch closure for that step. The closure captures only function- or
frame-lifetime context and is reset on every new frame. This Windows wiring was
reviewed but **not compiled or executed here**.

Detached result/removal accessors remain for test and diagnostic observation.
They do not emulate synchronous world dispatch, and live application integration
does not use them. Callback exceptions are not supported; host dispatch failures
are returned as `Result` and cause the application to stop rather than silently
inventing success. The original pause-press clear is distinct from releasing a
held controller button or implementing the pause menu.

## 5. Controller adaptation retained

The user-authorized RE03 drag policy is unchanged: a **fresh left-thumbstick
movement** in the requested general direction; diagonals accepted; no A hold,
touch tracing or touch release. A remains tap/mash. The **0.55 engage / 0.25
neutral** thresholds after the inherited deadzone and **±60-degree** cone are PC
adaptation choices, not newly recovered Android constants. Carry-in, invalid
axes, disconnect and reconnect do not create fresh gestures.

## What the tests establish

The new shared-manager group covers accepted/rejected states, overlapping starts,
independent -1 IDs, one sound/control queue, feedback continuation after failure,
interruption, retry and checkpoint binding/clock behavior. The consumption group
covers all eight native states and all **65,536 raw button-bit masks**, including
preservation of held/keypad history and unrelated inputs. Existing production
raw-axis/controller tests and 100,000 deterministic transition frames remain.

Both original first-level outcome fixtures load cinematic **20004**, config
**6**, and its real **20006 success / 20010 failure** scripts. They now execute
the portable synchronous handoff and the immediate 50 ms scheduler step. Their
script commands are **observed**, not executed against a fully rendered world.
Success still uses the controller-native gesture, with no A shortcut.

See `RE04_VERIFICATION.md` and the actual logs for build/run results. Remaining
work includes Windows compilation/execution, GPU composition and success
explosion, audible sound, physical XInput, full control/pause integration,
wall/enemy QTE ownership, remaining hostage behavior, the uploaded Sandman work,
and normal first-level/end-to-end gameplay. This is not near-final acceptance.
