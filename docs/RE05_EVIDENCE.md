# RE05 — QTE control calls and the PC keypad publication boundary

September 22, 2026. This records interpretation of the supplied reference, not
execution of the original ARM game. It is a continuation of RE04, not a new
engine, emulator, binary hook, or visual recreation.

## Reference identity and address convention

`game/original/libspiderman.so`: 7,713,000 bytes; SHA-256
`f35d959d54d43d3d4cce07d1afac4cbe52438997a3bf4d5304c50610cabc2679`.
Addresses below are **ELF instruction addresses**, with the Thumb bit cleared.
The supplied Ghidra exports use an additional **0x10000** rebase. Some inherited
source comments still quote that Ghidra address convention.

The new `docs/references/re05-original-fingerprints.json` retains the RE04
fingerprints and extends them to 29 functions. The existing standard-library
verifier accepts the original reference and rejects an intentionally modified
scratch copy. This proves identity, not behavioral equivalence. Local Thumb
instruction inspection used the installed LLVM 19 disassembler library and the
supplied decompiler exports; raw output and original binaries are not packaged.

## Original call order reconstructed

| Original function / address | Evidence and implemented consequence |
| --- | --- |
| `CQTEManager::BeginQTE`, `0x0037ab40` | At `0x37ab54`, calls `CLevel::EnableControls(false,true)` **before** testing manager state and before looking up the requested config. A refused overlapping request still performs this side effect. |
| Begin guard, `0x37ab40` function | States 0–3 refuse replacement; inactive -1 and states 4–6 admit it. The RE04 guard remains. Unknown config IDs are not looked up on refused paths. Unbound/malformed inputs remain explicit port diagnostics, not invented original error recovery. |
| Accepted Begin, `0x37abac` | Enables the separate QTE control only after `BeginNowQTE`. Compound children call `BeginNowQTE`, not the outer Begin; they must not repeat ordinary-control disable/key-reset or QTE-button reset. |
| `CQTEManager::SetState`, `0x0037a67c` | Same-state return is preserved. Success publishes cue 0x188; failure normally publishes 0x189. Both disable/reset the QTE control, capture result position, initialize result animation, call EndQTE, and **then** commit the new manager state. |
| SetState, `0x37a70e`, `0x37a760`, `0x37a798`, `0x37a7b6` | Respectively the two QTE-control disable calls, common EndQTE call, and final state write. The bound host sees the old state during control operations. The cue is queued before those operations; this does not establish audible sink timing. |
| `CQTEManager::EndQTE`, `0x0037a5b0` | Resets slow motion only when the denominator is **strictly greater than 1.0**, then calls EnableControls(true,false). An existing ratio-1 hold/ramp must not be erased. |
| `CLevel::EnableControls`, `0x0036fe94` | If parameter 2 is false, resets/enables the pause/IGM button; enables the normal interface; finally resets GameEventKeyWrap's keypad and movement. Repeated calls with the same enabled value still reset keys. Parameter 2 is pause-button preservation, **not** a whole-game pause operation. |
| `CLevelInterfaceNormal::EnableControls`, `0x00378730` | Delegates to ControlScheme::SetEnabled (`0x002ead8c`). The supplied level initialization identifies the normal interface at level+0x24, key wrapper at +0x5c, and IGM button at +0x220. |
| `ControlScheme::EnableQTEControl`, `0x002ea990` | Resets the QTE button before enabling/disabling it, including repeated requests. This is separate from ordinary controls and physical XInput state. |
| `GameEventKeyWrap::ResetAllKeys`, `0x002e8888` | Calls CKeyPadCustomer::resetKeys (`0x002e9b28`) and zeroes movement floats +0x10/+0x14. It does **not** clear the direct Xperia QTE/rescue/switch globals. |
| `CKeyPad::resetKeys`, `0x002e951c` | Clears both 16-slot keypad state arrays and the three last-key history bytes. The PC action facade now has a separate resettable publication, rather than erasing direct events or manufacturing hardware releases. |
| `GameEventKeyWrap::IsPressed/IsHold/IsReleased`, `0x002e8a88/0x002e8a48/0x002e8a54` | Player key-trigger queries reach the key wrapper/keypad independently of the direct QTE flags. The movement query also reads the wrapper's movement floats. |
| `CCinematicThread::InterfaceControlCmd`, `0x003611e0` | Calls EnableControls with parameter 2 false before the later interface flags. Its controls path now uses the same reset operation rather than just assigning a boolean. |
| `CCinematicThread::OnOffDaeMovieUI`, `0x0036043c` | Similarly calls EnableControls(!active,false), then updates HUD/band/skip fields; objective-arrow state is not changed by this operation. |

The original failure-cue suppression flag at manager+0x88 remains an unresolved
integration detail; RE05 does not pretend to implement that additional mode.

## Source implementation

`QteControlHost.hpp` defines three synchronous operations. The production
`LevelCinematicRuntime` implements them. The level's QTE manager stores a
non-owning pointer to that stable host, supplied at both application bind sites.
The pointer must outlive the manager and any value-copy checkpoint snapshots.
It is not a pointer to a per-frame local closure.

Begin's control operation is outside the state guard. Feedback invokes the host
inside SetState, before its final state write. A live bound manager does not also
queue a deferred control release. Detached diagnostic fixtures retain the old
observable release flag; the application must not replay it over a later
cinematic's control decision. RE04's synchronous whole-cinematic-manager 50 ms
handoff remains unchanged, including nested-request pre-commit visibility.

LevelCinematicRuntime also records pause-button/QTE-button enabled state and
reset-call counts. Counts are diagnostics, not reconstructed game timers. These
fields represent the native UI control calls; they do not reconstruct all mobile
widget registration, the pause menu, or the full Windows UI/input path.

## Explicit PC adapter boundary

The existing per-action keypad facade and direct Xperia-event view were sharing
one `GameplayInputState`. RE05 separates their publications in XperiaKeyRouter:

- Physical translator events still feed the direct QTE/UI/rescue view.
- Gameplay reads a distinct keypad view with the inherited two-update press
  history. Reset clears that view and the current game-facing movement sample.
- Reset does not touch the translator's physical buttons/stick, direct QTE flag,
  rescue/switch events, pause press, or manufacture any key-up/key-down event.
- The normalized stick is republished for gameplay on the next PC poll. The QTE
  adapter continues reading physical stick samples independently, so a gameplay
  reset cannot supply a fake neutral sample or defeat its held-input guard.
- A held physical button is not reissued as a fresh press after a reset. A real
  release/repress can publish it again. Disconnect retains RE02's cancellation
  path, without producing an R1-release rescue.

This is the **PC integration of a recovered reset boundary**, not a claim that
all 16 original keypad slots, disabled-key timers, event-dispatch ordering, or
native global lifetimes have been fully reconstructed. Those broader input
issues remain open. The user-approved RE03 directional QTE adapter, thresholds,
neutral/engage policy and diagonal cone are unchanged. A is still tap/mash, with
no A hold or touch coordinates for drags.

The application also discards its diagnostic autoplay keypad snapshot when a
synchronous reset has occurred since publication, preserving separate QTE and
rescue intent. That Windows-only application branch is reviewed but **not
compiled or executed** by the portable tests.

## New test coverage

`QteControlTests.cpp` has three independently selected groups. Its checks remain
active in Release builds (they do not rely on disabled C assert statements).

**lifecycle** observes old-state visibility at every host callback, all native
manager guard states, refused invalid IDs, pause-button preservation, outer vs
compound-child reset counts, result animation assignment, cue publication,
strict slow-motion reset, repeated failure no-op, and a last mash press arriving
past deadline. That last case preserves both success and failure side effects
rather than coalescing them into one host reset.

**integration** exercises authored interface commands and the ratio-1 boundary,
repeated same-value control requests, a refused nested Begin during SuccessHandle,
checkpoint value copying, and the original first-level 20004/config6 drag with
its 20006/20010 outcomes through raw XInput normalization. It also exercises
first-level hostage 30018 against the original object/player/config assets: an
A press on the activation update survives the keypad reset, eight distinct A
presses reach success, and interruption preserves the interrupting player state
without rewarding a rescue. World rendering and a full level playthrough are
not part of these fixtures.

**keypad** exhausts all 65,536 raw 16-bit button masks, checking that reset leaves
all direct event fields and physical input unchanged while clearing only the
gameplay publication; subsequent holds, genuine release/repress, movement
republishing and disconnect are checked separately. The test compares members,
not struct padding. The inactive stored switch payload may differ between the
reset keypad view and retained direct globals; the test explicitly respects
that rather than clearing a real native payload to satisfy a bad expectation.

Read `RE05_VERIFICATION.md` and `RE05_verification/` for actual build results,
failed development runs, source equality, reference and asset integrity.

## Still unfinished

No new Windows executable, Windows build, GPU composition, audible audio,
physical XInputGetState execution, original ARM differential execution, or
complete normal first-level gameplay is established here. Full pause/menu and
level update/input lifetime, wall/enemy QTE ownership, hostage cutting-loop
IsPlaying integration, remaining hostage behavior, the success explosion/full
QTE composition and uploaded Sandman work still require reconstruction or
verification. This checkpoint closes the scoped synchronous control-call gap;
it is not a completed native port or near-final acceptance build.
