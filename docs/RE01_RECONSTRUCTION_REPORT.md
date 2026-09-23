# OpenAndroidUSM RE01 — Input reconstruction checkpoint

Date: September 21, 2026

Base commit: `59903b8037945c24ad81a5f947d69f01dc4de5b7`

Target: the existing source-level native Windows PC port of the Android/Xperia Play release of *Ultimate Spider-Man: Total Mayhem*.

## Status

This is a tested source continuation, not a finished port or a newly verified playable Windows build. The supplied project already contains substantial reconstructed C++, Direct3D 11, XAudio2, and an XInput adapter. RE01 repairs a concrete first-level input path using the supplied original library and data.

No Windows executable is included. The portable production core and input tests were compiled and executed natively on Linux x86-64. The original ARM library was inspected but not executed. No emulator, binary hook, or original-library runtime bridge was introduced.

## Reference identity

The supplied `game/original/libspiderman.so` is 7,713,000 bytes, with SHA-256:

```text
f35d959d54d43d3d4cce07d1afac4cbe52438997a3bf4d5304c50610cabc2679
```

All 599 extracted files under `game/data` and `game/original` were independently SHA-256-compared with their entries in the uploaded split archive: 170,642,144 bytes in total, unchanged. This verifies those used files, not every generated capture in the roughly 95 GB expanded archive. All 17 split volumes and the ZIP central directory were readable; the entire archive was not expanded or CRC-tested.

The supplied Ghidra exports use addresses **0x10000 above the ELF image addresses**. The ARM Thumb-state bit is separate and must also be cleared when identifying the instruction address. The reference manifest retains ELF instruction addresses and function byte fingerprints for eight symbols. Existing source comments elsewhere may still use the imported Ghidra addresses.

Run the new standard-library-only reference verifier from the project root:

```powershell
python tools/verify_re01_reference.py
```

It fails on a different library instead of silently accepting these addresses. Fingerprint verification establishes reference identity, not gameplay equivalence.

## Native evidence and implemented changes

| Original evidence | Source reconstruction in RE01 |
| --- | --- |
| `appKeyReleased`, ELF `0x003cbd5c`; R1 branch `0x003cc2b8–0x003cc2ea`: gameplay key 103, scan 311, on release writes rescue=1, switch=1, switch value=4. | The existing RB-to-Xperia-R1 adapter no longer terminates at an ignored router case. R1 release raises separate rescue/switch events. The switch value is retained; no unverified switch target or consumer was invented. |
| `appKeyPressed`, ELF `0x003cc75c`: Cross key 23 / scan 304 down sets the QTE input flag. `CQTEManager::Update`, ELF `0x0037b240`, consumes it once. | QTE input has a single-update event state rather than reusing combat's two-update press window. Holding A no longer contributes a second QTE action in the tested active-QTE path. A down/up pair within one update retains one QTE tap. |
| `CKeyPad::update`, ELF `0x002e9434`, and `wasKeyPressed(int)`, ELF `0x002e964c`: the gameplay press window covers keypad states 1 and 2. | The existing combat/jump two-update press semantics remain unchanged. Only the distinct QTE event uses the one-update representation. |
| `CHostage::Update`, ELF `0x00328068`, starts rescue from the rescue flag and enters player states 27, 28, and 29 around the hostage QTE. | Rescue initiation and QTE progress are separate `HostageInput` fields. Punch no longer starts or advances rescue. The application, diagnostic autoplay, and displayed RB/A prompts use the same separation. |
| `CQTEManager::Update`, state 2, consumes Cross taps and restores one remaining action after 500 ms without a tap. It reads `Application::GetRealTs`, ELF `0x003ce5d4`. | The hostage path uses the existing shared `ButtonMashProgress` implementation and the shipped configuration, including interaction type 3 and eight actions. `HostageTimeStep` carries simulation and real elapsed time separately; mash idle uses real elapsed time. The slow-motion/frozen-simulation fixture checks that 499+1 real milliseconds still trigger decay. |

The Windows XInput polling call remains `XInputGetState`. Its existing button translation, ordering, stick deadzone, digital-direction threshold, and disconnect release policy were factored into portable production code so they can be tested directly. The Windows shim contains compile-time comparisons with the actual Windows SDK constants; this shim was not compiled here.

The stick calibration is an **inherited PC-adapter policy**, not newly asserted Android behavior. Renderer, audio backend, movement/combat algorithms, level assets, and Sandman logic were not changed by this checkpoint. A missing `<charconv>` include in the uploaded smoke test was also repaired so that the existing test compiles with the available compiler.

## Verification actually performed

The unchanged baseline core suite passed after the header-only portability fix. After all RE01 code changes, the final selected CTest run passed **7/7 tests**, in 44.08 seconds:

| Test | Observed result |
| --- | --- |
| `OpenAndroidUSM.CoreTests` | Passed; the existing monolithic native core regression suite ran with the supplied game data. |
| `OpenAndroidUSM.InputParity.buttons` | Passed; all 4,096 combinations of the 12 mapped buttons, press/hold/release ordering, unmapped bits, disconnect, and reconnect. |
| `OpenAndroidUSM.InputParity.routing` | Passed; R1 release and value 4, wrong-scan/context rejection, distinct punch/rescue/QTE inputs, held A, same-update down/up, and the inherited disconnect policy. |
| `OpenAndroidUSM.InputParity.sticks` | Passed; 225 axis-boundary pairs, finite/unit-bounded normalization, deadzone, and strict digital-direction thresholds. |
| `OpenAndroidUSM.InputParity.hostage` | Passed; the actual first-level asset 30018, rejected X/A rescue initiation, RB-release initiation, player states 27/28/29, one count for held A, 500 ms decay, eight distinct taps, authored release/thanks/freed animation sequence, ten reward orbs, failure/reset/retry, and independent real-time idle decay. |
| `OpenAndroidUSM.InputParity.autoplay` | Passed; the existing `rescue_hostage` goal emits the same separated rescue and QTE actions instead of punch. |
| `OpenAndroidUSM.InputParity.transitions` | Passed; 100,000 deterministic connected/disconnected button-state frames through the production translator and router. |

The same 100,000-frame transition test also passed with Clang AddressSanitizer and UndefinedBehaviorSanitizer enabled, with no reported sanitizer error. **That sanitizer run covers the adapter/router stress test, not the entire game or hostage runtime.**

The hostage fixture places the diagnostic player at the authored hostage location and directly advances the existing runtime and animations. It does not demonstrate playing from the title screen to that location, rendering it, hearing it, or operating a physical controller. The tests check these reconstructed paths; they are not a side-by-side execution comparison with the original ARM game.

## Known limitations retained explicitly

1. **No new Windows build/run or physical XInput test.** Direct3D presentation, audible audio, real controller operation, and the full normal first-level player flow were not verified here. The available environment lacked the Windows SDK/runtime; a cross-toolchain download attempt did not succeed. No old executable is relabeled as a new build.
2. **QTE timeout is not fully reconstructed.** Direct disassembly of `CQTEManager::IsOutTime`, ELF `0x0037a4b8`, shows an absolute `irr::os::Timer::getTime` comparison with start/pause offsets and a strict `>` duration test. The inherited hostage timeout still accumulates simulation time and uses `>=`. RE01 fixes the separately evidenced real-time *mash-idle* clock; it does not substitute an unverified clock for the timeout. The 4001 ms failure fixture is not proof of the 4000 ms native boundary. Cinematic QTE timeout/caller timing also needs the same clock audit.
3. **Hostage cutting-loop sound timing remains inherited.** The uploaded reconstruction starts its loop when entering the QTE. The native hostage update tests for seven remaining actions before starting it. The existing sound-cue regression does not establish this start-frame parity. This is a concrete remaining audio issue, not a verified match.
4. **The native switch request has no verified consumer in this change.** Its value 4 is exposed but not given invented meaning or behavior.
5. **Disconnect policy is inherited, not native Android evidence.** A disconnected controller emits releases for previously held buttons. With R1 now routed, disconnecting while holding RB can raise the rescue request. The test records that behavior rather than hiding it. Host cancellation policy remains to be resolved explicitly.
6. **Full input arbitration/lifetime parity is not established.** These tests cover the stated active gameplay/QTE paths. Cross-context input ownership, activation-boundary flag lifetime, simultaneous-action arbitration, and other menus/modes have not all been compared against the original.
7. **Sandman remains the uploaded WIP.** Its unfinished task sequence/sand-hand behavior and the existing untested phase/combo work were not replaced or declared correct. The project's first-level graphics/audio review milestone has not been reached.

## Apply and build

The overlay contains replacement source files relative to the existing project root. Extract it into the uploaded `OpenAndroidUSM` checkout. It is based on the exact commit above; preserve any additional local edits before applying it. The included unified patch is an alternative to the replacement files, not an additional step after overlaying them.

The complete source archive includes the inherited tracked source plus RE01. Neither archive contains original game binaries/assets, Ghidra databases, generated decompiler dumps, vendor build caches, or a game executable. Continue using the supplied `game/` material.

Use the existing Windows build commands from the project root:

```powershell
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
ctest --test-dir build/windows-msvc -C Release -R "^OpenAndroidUSM\.(CoreTests$|InputParity\.)" --output-on-failure
.\build\windows-msvc\Release\OpenAndroidUSM.exe
```

These are the existing project build/run interfaces, updated only to include the new test selection. Their Windows execution was not verified in this session. Building and running them locally is the remaining platform check.

The `RE01_verification` directory in the downloads contains the actual baseline/final logs, reference fingerprints, original-material integrity record, source-change manifest, and clean-base patch-application check. `RE01.patch` can be independently checked with `git apply --check` against the unmodified base commit.
