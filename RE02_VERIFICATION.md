# OpenAndroidUSM RE02 — Source and verification report

**September 22, 2026 · Development continuation, not near-final testing**

This continues the delivered RE01 native C++ port of the Android/Xperia Play
release of *Ultimate Spider-Man: Total Mayhem*. It is source-level reverse
engineering from the supplied library and assets. No emulator, original-ARM
runtime bridge, invented replacement gameplay, or newly verified Windows game
executable is included.

## Implemented in this continuation

**Original QTE clocks and state order.** Replaced accumulated simulation-time
and `>=` timeout shortcuts with the original absolute timer, float32 arithmetic,
strict `>` boundary and recovered pause calculation. Mash idle and sprite
updates use their separate real-time input. Input/timeout ordering, compound
QTE sequencing, result sounds, control release and later cinematic dispatch are
represented explicitly. The original normal-success handoff occurs on the
ninth Draw call; it is no longer a made-up elapsed-time delay.

**Actual first-level drag and both authored outcomes.** The live source cinematic
is 20004, with a command at 2000 ms using config 6, success 20006 and failure
20010. Config 6 is a drag, not an A tap. The port now follows the original
four-direction path-selection and release rules, including signed coordinates,
tie ordering and the different endpoint tolerances. Its samples come from the
original interface sprite metadata. A plus the left stick supplies virtual
touch coordinates as an explicit PC adaptation; the recovered gameplay rules,
not a forced-success shortcut, determine the outcome. Autoplay also supplies
authored points. Five other packed QTE cinematics were not falsely counted as
live first-level coverage: their rooms are absent from the main scene's links.

**Hostage rescue ordering and sounds.** The hostage object now observes the
previous QTE-manager update. Cutting starts at the original remaining-action
check rather than at QTE entry. Interrupted rescue no longer grants completion
rewards or overwrites the interrupting player state. Original tap/success/fail
sound events are routed separately from spatial hostage sounds. The authored
ButtonHeight sentinel and Ultimate-state restriction follow the recovered
branches. Sound events and decoding were tested; actual speaker output was not.

**XInput transport cancellation.** Disconnecting while holding RB clears host
input rather than fabricating an R1 release that could trigger rescue or switch
requests. A real RB release still follows the recovered R1 route. This is a PC
device policy, not an assertion about Android device-disconnect behavior.
Combat's existing two-update press window remains distinct from QTE input.

## Tests actually executed

| Build/run | Result | CTest elapsed time |
| --- | --- | --- |
| GCC 14.2, native Linux x86-64 Release | **17/17 passed** | 31.16 s |
| Clang 17, complete portable suite with AddressSanitizer, UndefinedBehaviorSanitizer and leak detection | **17/17 passed**, no reported sanitizer errors | 118.62 s |
| Fresh RE01 copy, clean-applied code patch, fresh Release configure/build/test | **17/17 passed** | 33.19 s |

These runs cover the existing native core suite, ten QTE groups, and six input
groups. They include 200,000 clock-oracle operations; all 65,536 signed-coordinate
values across the four drag rules; 100,000 sprite timing steps; and 100,000
controller-transition frames. Exact per-test logs/check counts are preserved.
The original four QTE audio entries were decoded with the production decoder.

The asset-level fixture advances the actual first-level cinematic scheduler to
its StartQTE command and completes both its real success and failure handoffs.
Other cinematic commands are observed, not executed against a full rendered
world. The hostage fixture positions a diagnostic player at the authored
hostage. Neither fixture demonstrates normal play from the title screen.

A final separate patch-application check and source hashes are included.
Source, tests and build scripts match the clean-build tree byte-for-byte;
later documentation is checked separately. Intermediate failed test iterations
remain in the logs rather than being relabeled as successful runs.

## Original reference and unchanged material

Original library: `game/original/libspiderman.so`, 7,713,000 bytes.

```text
SHA256 f35d959d54d43d3d4cce07d1afac4cbe52438997a3bf4d5304c50610cabc2679
```

All **599 used original game-data/library files**, totaling **170,642,144 bytes**,
were freshly compared with their members in the 17 uploaded split volumes and
remain unchanged. This is not a full verification of the roughly 95 GB expanded
archive. Forty-eight original functions have retained symbol/address/byte
fingerprints. Supplied Ghidra addresses are ELF addresses plus 0x10000; the
new evidence records ELF instruction addresses explicitly.

`tools/verify_re02_reference.py` passes against the supplied library and rejects
a deliberately modified temporary copy. This establishes reference identity,
not behavioral equivalence. The tests compare reconstructed code against
instruction-shaped rules and original asset facts; **the original ARM program
was not executed** for differential validation.

## Remaining barriers to near-final testing

**Windows is not verified.** The Windows-only application wiring was edited but
not compiled here. D3D11 presentation, audible XAudio2, physical XInput, normal
first-level play and a newly built Windows executable remain unverified. Linux
CMake now excludes Windows-only targets by default so the full portable build
works; it is not a graphical Linux port. No old executable is represented as
containing RE02.

**Original game integration is still incomplete.** QTE visual composition and
failure fading are not fully reconstructed in the renderer. Shared QTE/input
ownership among cinematic, hostage, wall and enemy paths; the actual PC pause
menu lifecycle; remaining hostage animation/audio/ambient behavior; and the
uploaded Sandman work remain open. Clock pause API tests do not prove working
in-game pause. Reconnection still uses the inherited held-button policy.

Further original inspection located the interactive-button and switch-counter
consumers. Their order-dependent behavior is documented, not replaced by an
invented nearest-switch action. Full consumer integration remains outstanding.

## Deliverables and application

- `OpenAndroidUSM_RE02_Source.zip`: complete cumulative source through RE02.
- `OpenAndroidUSM_RE02_Overlay.zip`: changed/new files relative to delivered
  **RE01**, plus the evidence and actual verification records.
- This report and `docs/RE02_EVIDENCE.md`: scope, original addresses, exact
  interpretations and remaining limitations.

The overlay applies to **RE01**, not directly to the unmodified original upload.
Its `RE02.patch` is an alternative to copying replacement files, not a second
step after overlaying them. The cumulative source includes all preceding
source changes. Retain the original `game/` and supplied dependency material;
neither package redistributes original game binaries/assets, build caches or a
game executable. Existing Windows build interfaces are documented but not
claimed to have been executed.

**RE02 preserves the completed work. It is not a near-final acceptance build.**
