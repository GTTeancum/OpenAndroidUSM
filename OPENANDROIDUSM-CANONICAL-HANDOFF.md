# OpenAndroidUSM — CANONICAL CHAT HANDOFF

> **MANDATORY CONTINUITY RULE — READ THIS FIRST**
>
> This file is the canonical context for this project. **At the end of EVERY
> assistant turn that works on OpenAndroidUSM, this file MUST be updated before
> the turn ends.** Update the current head/state, work performed, verification
> performed, remaining blockers, and the exact next step. This applies even when
> the turn only investigates, discovers a failed test, or makes no source change.
>
> When moving to a new chat, **this file is the only chat-context file that needs
> to be transferred.** The new chat must read this file first, then read the
> repository's `AGENTS.md`, inspect the latest `main`, and continue from the
> latest GitHub state. Do not depend on old chat history being available.
>
> If this file and stale historical reports disagree about the current host-side
> state, this file plus the latest GitHub history are authoritative. Historical
> RE reports remain authoritative for the exact checkpoint/evidence they record.

Last handoff refresh: **2026-09-23**

---

## 1. Project identity and user's non-negotiable rules

Repository: **`GTTeancum/OpenAndroidUSM`**  
Branch: **`main`**  
Public GitHub repository; the user has push/admin access through the connected
GitHub integration.

This is a **strict source-level native-PC reconstruction** of the Android/Xperia
Play release of *Ultimate Spider-Man: Total Mayhem*.

The user explicitly requires:

- **Do not use Work mode or the user's local machine for development.**
- Develop from GitHub and remote/cloud validation only unless the user explicitly
  changes that instruction later.
- Do **not** replace source reconstruction with ARM emulation, dynamic
  recompilation, binary hooks, a compatibility wrapper, or a remake.
- Do not invent, approximate, tune by feel, or redesign unresolved original game
  behavior. Recover behavior from the original executable and shipped data first.
- Preserve original addresses/source provenance for RE conclusions.
- Work chronologically through normal player flow and finish blocking parity work
  before jumping ahead.
- Original game binaries/assets/decompiler databases must not be committed to
  GitHub.
- Host-platform documentation (Microsoft API behavior, compiler behavior, etc.)
  may be researched separately, but it is not evidence for original Android game
  behavior.
- Stop for user review only when first-level gameplay is verified with no known
  graphical/audio issues; do not call the overall port complete at that point.

### Explicit PC-control policy mandated by the user

The RE03 adaptation remains binding:

- directional drag QTE = **fresh left-thumbstick motion in the requested general
  direction**;
- diagonals are accepted;
- **no A hold and no touch emulation/tracing** for drag QTEs;
- A remains the tap/mash QTE input;
- controller thresholds and gesture-lifecycle rules must be described as PC
  adaptation policy, not claimed as Android facts;
- original QTE timing, compound sequences, outcome sounds, control release and
  cinematic consequences must remain recovered/native.

---

## 2. Current authoritative Git state

### Current source state before this handoff file was added

Latest code/documentation head inspected before creating this handoff:

`b1ca948502b42a88f523bf080f4a1d3695a622aa`  
**Document Windows native validation [skip ci]**

The handoff-file commit itself is expected to be newer. Therefore a new chat
must always inspect the actual latest `main` instead of treating the SHA above
as a permanent head pin.

### Gameplay/reference checkpoint

RE06 gameplay/reference base:

`a772706bfcbf861052d6439bdf52ac9b70a1d6a0`  
**Add RE06 reconstruction checkpoint**

### Final post-RE06 Windows validation source

The final fail-closed Windows validation source before the documentation-only
commit:

`d280ed37933d7a4101021b93d9ae164962907a65`  
**Fail closed on unexpected XAudio2 startup errors**

The post-RE06 Windows validation is **host/platform validation layered on RE06**.
It is **not RE07 gameplay reconstruction**.

---

## 3. Historical reconstruction summary — RE01 through RE06

### Base / 0.3b

Base historical commit:

`59903b8037945c24ad81a5f947d69f01dc4de5b7`

The repository already contained substantial reconstructed C++ gameplay,
Direct3D 11 rendering, XAudio2 audio, XInput handling, asset decoders, combat,
cinematics and autoplay. It was and remains an incomplete preservation/native-PC
port, not a finished game.

### RE01 — input/rescue separation

RE01 reconstructed and tested the first-hostage input path:

- routed original R1-release rescue behavior to PC RB;
- separated A/Cross QTE input from X/Square punch input;
- preserved the native two-update combat press window;
- gave hostage mash decay its evidenced real-time delta;
- factored XInput translation into deterministic testable production code.

Historical RE01 source evidence and logs remain under:

- `RE01_README.md`
- `docs/RE01_RECONSTRUCTION_REPORT.md`
- `RE01_verification/`

### RE02 — QTE timing, drag behavior and result lifecycle

RE02 reconstructed:

- original absolute QTE timeout behavior and pause calculation;
- real-time mash behavior;
- result-sprite timing;
- authored drag-path rules;
- compound QTE sequencing;
- draw-driven outcome timing;
- correct hostage/QTE update ordering;
- disconnect cancellation instead of synthesizing an R1 release.

It also removed the incorrect “A tap succeeds the first-level drag” shortcut.

Historical evidence:

- `RE02_README.md`
- `docs/RE02_EVIDENCE.md`
- `RE02_VERIFICATION.md`
- `RE02_verification/`

### RE03 — final user-mandated controller-native drag policy + feedback geometry

RE03 changed the PC drag adaptation to the current user-approved policy:

- fresh left-stick direction only;
- no A hold;
- no touch tracing;
- neutral/re-engage semantics between gestures;
- diagonals accepted.

It also reconstructed result-sprite draw requests, completed-mash ring frames,
failure alpha behavior, and portable QTE feedback geometry. The success explosion
and full native QTE composition remained intentionally unresolved.

Historical evidence:

- `RE03_README.md`
- `docs/RE03_EVIDENCE.md`
- `RE03_VERIFICATION.md`
- `RE03_verification/`

### RE04 — shared level QTE manager and synchronous handoff

RE04 connected cinematic and hostage QTE paths to the same level-owned manager,
reconstructed:

- manager request guard behavior;
- per-update input consumption;
- shared cinematic/hostage manager ownership;
- synchronous success/failure cinematic handoff;
- original 50 ms manager-step/pre-commit ordering.

Historical evidence:

- `RE04_README.md`
- `docs/RE04_EVIDENCE.md`
- `RE04_VERIFICATION.md`
- `RE04_verification/`

### RE05 — control-call order and input publication boundary

RE05 reconstructed:

- BeginQTE control-disable/reset occurring before the state guard;
- accepted child compound behavior without repeating the outer Begin reset;
- synchronous result feedback/control restoration before final manager state
  commit;
- EndQTE slow-motion reset only when the denominator is strictly greater than 1;
- cinematic interface controls using the same reset boundary;
- separate PC gameplay-keypad publication from direct QTE/rescue/UI flags and
  physical XInput state.

Historical evidence:

- `RE05_README.md`
- `docs/RE05_EVIDENCE.md`
- `RE05_VERIFICATION.md`
- `RE05_verification/`

### RE06 — hostage sound correction + repeated native control calls

RE06 corrected an earlier interpretation: hostage sound ID `0x18b`
(`SFX_QTE_UNTIE`) is **not a continuous cutting loop** on this path.

Recovered behavior:

- during hostage state 2 / player state 28, query playback only while remaining
  mash action count is exactly **7**;
- call `IsPlaying(0x18b, true)`;
- if not playing, issue a transient spatial **one-shot**;
- do not latch a request as “playing”;
- completed/culled/failed requests can be retried while the native condition
  remains true;
- preserve explicit/repeated stop calls;
- restore the hostage object's own repeated
  `EnableControls(false,true)` / restore calls at their observed positions;
- preserve failure/interruption/success ordering and the shared QTE manager.

The first hostage uses object **30018** and the shared config 11 mash QTE.

RE06 reference:

- original `libspiderman.so` size: **7,713,000 bytes**
- SHA-256:
  `f35d959d54d43d3d4cce07d1afac4cbe52438997a3bf4d5304c50610cabc2679`
- `CHostage::Update`: ELF `0x00328068`
- hostage Audible subobject: hostage + `0x114`
- resolved Audible vptr: ELF `0x004b27b8`
- vtable +0x1c = `PlayAudio(int,bool,bool)`
- vtable +0x20 = `StopAudio(int,int)`
- vtable +0x24 = `IsPlaying(int,bool)`

RE06 validation actually completed:

- GCC 14.2 Linux fresh Release: **29/29 passed**
- Clang 17 ASan+UBSan, leak detection: **29/29 passed**
- exact RE05 extraction + clean RE06 patch + fresh GCC objects:
  **29/29 passed**
- all 599 selected original game-data/reference files were re-read and compared
  against the supplied archive members;
- RE06 verifier checks 45 function fingerprints plus the actual hostage Audible
  virtual slots.

Authoritative RE06 evidence:

- `RE06_README.md`
- `RE06_VERIFICATION.md`
- `docs/RE06_EVIDENCE.md`
- `docs/references/re06-original-fingerprints.json`
- `tools/verify_re06_reference.py`
- `RE06_verification/`

---

## 4. Post-RE06 work completed in this continuation

After pulling latest GitHub state, work continued entirely through GitHub and
GitHub-hosted Windows CI.

### Commit chain and what each step did

1. `3925aa30006ff0db6b7a47616498dcc84029cae5`  
   **Add remote Windows MSVC build gate**  
   Added a GitHub-hosted Windows build workflow.

2. `f5640284b8577ff233e2f90adf5bd9aa5e228393`  
   **Make Windows CTest gate fail closed**  
   Fixed an initial regex mistake where CTest matched zero tests while the step
   still returned success. Added `--no-tests=error`.

3. `d91cc610d6b1f599f19ae438ac6805ae041a9488`  
   **Expand asset-independent Windows parity gate**  
   Windows MSVC successfully ran asset-independent QTE/input/hostage tests.

4. `74e3ddc521f430b5712ab780deb3633fe97b6269`  
   **Exercise Windows D3D11 startup in CI**  
   Added a real executable startup smoke; the first harness invocation was wrong
   because the app is a GUI-subsystem process.

5. `a92cef3adc9ea4a7b4f24511af07d8321de9faf3`  
   **Wait for Windows GUI startup smoke process**  
   Corrected the smoke to use `Start-Process -Wait -PassThru`. It proved the
   built Windows executable initializes the real offscreen WARP renderer/shader
   pipeline and reaches the expected missing-game-data error.

6. `68de9406c93e4093f588ce813ddfa2d330174d3b`  
   **Exercise Windows XAudio2 startup in CI**

7. `3eefb2aac01e295b1d5afc44f57e1fd82d960177`  
   **Test live XAudio2 playback state on Windows**  
   Added `tests/XAudio2BackendTests.cpp`.

8. `954e9047b1d5099bc0d9595e08df866e7c265d6`  
   **Test D3D11 WARP render and readback on Windows**  
   Added a real asset-independent D3D11 runtime/readback test.

9. `f09903cdf17158c179dff888ba5216831439edcd`  
   **Skip XAudio2 runtime probe when CI has no audio endpoint**

10. `882f52054664b78cbf0d6595e08df866e7c265d6`  
    **Preserve XAudio2 HRESULT diagnostics**

11. `614e2a9969f76972c94d5c9f22be7c191971b59d`  
    **Initialize COM for XAudio2 backend**  
    Fixed a real Windows host defect: XAudio2 had been initialized without COM
    initialization on the application thread.

12. `d280ed37933d7a4101021b93d9ae164962907a65`  
    **Fail closed on unexpected XAudio2 startup errors**  
    The no-audio-device skip is restricted to the exact known result; other
    mastering-voice failures are errors.

13. `b1ca948502b42a88f523bf080f4a1d3695a622aa`  
    **Document Windows native validation [skip ci]**  
    Updated the main README and added the authoritative Windows validation
    document.

### Windows validation established

The complete Windows target now configures and links in GitHub-hosted
`windows-2022` CI, including:

- `OpenAndroidUSM.exe`
- Direct3D 11 renderer
- XAudio2 application/backend code
- XInput application code
- portable core and test binaries

Observed validated toolchain:

- Windows SDK 10.0.26100.0
- MSVC 19.44.35228/35229 family on the hosted runner

### Asset-independent Windows tests

The current CI gate selects:

- `OpenAndroidUSM.QteParity.clock`
- `OpenAndroidUSM.QteParity.attributes`
- `OpenAndroidUSM.QteControl.keypad`
- `OpenAndroidUSM.HostageIntegration.dispatch`
- `OpenAndroidUSM.InputParity.buttons`
- `OpenAndroidUSM.InputParity.routing`
- `OpenAndroidUSM.InputParity.sticks`
- `OpenAndroidUSM.InputParity.autoplay`
- `OpenAndroidUSM.InputParity.transitions`
- `OpenAndroidUSM.AudioBackendTests`
- `OpenAndroidUSM.D3D11BackendTests`

The earlier nine non-backend tests passed on Windows/MSVC. The current final
gate also contains the D3D11 and XAudio2 backend tests.

### D3D11 runtime proof

The D3D11 backend test is real runtime validation, not compile-only:

- creates a WARP D3D11 device;
- creates 64x32 offscreen color/depth targets;
- compiles/creates the shader pipeline;
- renders an empty frame;
- reads pixels back from the GPU resource;
- verifies the expected uniform clear result.

The built `OpenAndroidUSM.exe` is also started in autoplay mode in CI. It
initializes the real offscreen WARP path and then intentionally fails at game
data discovery because copyrighted original data is not in GitHub.

### XAudio2 defect found and fixed

The initial real XAudio2 startup probe failed at
`IXAudio2::CreateMasteringVoice` with:

`0x800401F0` = `CO_E_NOTINITIALIZED`

That exposed a real host-backend defect. `XAudio2System` now:

- calls `CoInitializeEx(..., COINIT_MULTITHREADED)`;
- retains the matching successful COM initialization reference;
- destroys voices/engine state before COM teardown;
- pairs successful initialization with `CoUninitialize`;
- links `ole32` where required.

After the fix, hosted CI advances to:

`0x80070490`

for `CreateMasteringVoice`, which is the runner's no-default-audio-endpoint
case. The skip is intentionally restricted to that exact failure.

On a Windows machine/runner with a valid default audio endpoint,
`OpenAndroidUSM.AudioBackendTests` continues into synthetic PCM source-voice
tests for:

- live `isNamedPlaying`;
- natural one-shot completion;
- looping playback;
- named stop behavior.

The current GitHub-hosted runner therefore verifies COM/XAudio2 initialization
progress but **does not prove audible output** because it lacks a default audio
endpoint.

### Important CI runs retained as evidence

- `35912628711` — initial Windows build; exposed zero-test regex false-positive
- `35913024892` — fail-closed single transition test passed
- `35913326799` — expanded nine asset-independent tests passed
- `35914026210` — first GUI smoke harness failed due PowerShell invocation method
- `35914587640` — corrected Windows app/D3D11 startup smoke passed
- `35917834889` — first XAudio2 startup probe failure
- `35918040721` — live XAudio2 test development failure
- `35918317329` — D3D11 readback/audio-stage development failure
- `35918455410` — no-audio-endpoint handling iteration passed
- `35918883727` — captured pre-fix XAudio2 `0x800401F0`
- `35919488698` — COM fix passed; advanced to no-device `0x80070490`
- `35919999966` — final fail-closed XAudio2 classification gate passed

Authoritative host-validation writeup:

`docs/WINDOWS_NATIVE_VALIDATION_2026-09-23.md`

**Important:** old RE06 reports say Windows code was uncompiled. That statement
was true for the RE06 checkpoint but is superseded for the current tree by the
post-RE06 Windows validation above.

---

## 5. Where the editable source files are

Use **GitHub `GTTeancum/OpenAndroidUSM`, branch `main`** as the current source
of truth. Do not start from the older source ZIP unless reconstructing history.

Primary edit locations:

| Area | Path |
| --- | --- |
| App/runtime integration | `src/app/` |
| Main Windows application integration | `src/app/Application.cpp` |
| Core gameplay reconstruction | `src/game/` |
| Hostage runtime | `src/game/LevelHostageRuntime.cpp/.hpp` |
| Hostage sound bridge | `src/game/HostageSound.cpp/.hpp` |
| QTE manager/runtime | `src/game/QuickTimeEventRuntime.cpp/.hpp` |
| QTE action runtime | `src/game/QuickTimeActionRuntime.cpp/.hpp` |
| QTE feedback/gesture/sprite code | `src/game/Qte*.cpp/.hpp` |
| Cinematic control host | `src/game/LevelCinematicRuntime.cpp/.hpp` |
| Player/combat | `src/game/GameplayPlayer.cpp/.hpp` |
| Enemy/combat/boss logic | `src/game/LevelEnemyRuntime.cpp/.hpp` |
| Wall-web QTE path | `src/game/WallWebRuntime.cpp/.hpp` |
| PC input translator | `src/platform/input/XInputStateTranslator.cpp/.hpp` |
| Windows XInput shim | `src/platform/windows/XInputController.cpp/.hpp` |
| Reconstructed Xperia input router | `src/reconstructed/input/` |
| XAudio2 backend | `src/audio/xaudio2/XAudio2System.cpp/.hpp` |
| D3D11 backend | `src/renderer/d3d11/D3D11Renderer.cpp/.hpp` |
| Autoplay harness | `src/diagnostics/AutoplayHarness.cpp/.hpp` |
| Build rules | `CMakeLists.txt`, `CMakePresets.json` |
| Windows CI | `.github/workflows/windows-ci.yml` |
| Repository agent rules | `AGENTS.md` |

---

## 6. Where tests and verification files are

Primary tests:

| Test area | Path |
| --- | --- |
| Large core regression suite | `tests/SmokeTests.cpp` |
| QTE parity | `tests/QteParityTests.cpp` |
| QTE/control ordering | `tests/QteControlTests.cpp` |
| Hostage sound/control integration | `tests/HostageIntegrationTests.cpp` |
| Input parity | `tests/InputParityTests.cpp` |
| 100k transition stress | `tests/InputTransitionsTests.cpp` |
| Windows XAudio2 backend | `tests/XAudio2BackendTests.cpp` |
| Asset-independent D3D11 backend | `tests/D3D11BackendTests.cpp` |
| Full D3D11 render regressions requiring game data | `tests/D3D11RenderTests.cpp` |
| Autoplay scenarios | `tests/autoplay/` |
| Autoplay runner unit tests | `tests/AutoplaySuiteRunnerTests.ps1` |
| Autoplay comparison unit tests | `tests/AutoplayComparisonTests.ps1` |

RE06 retained verification:

`RE06_verification/`

Key files there include:

- `release-*.log`
- `sanitize-*.log`
- `clean-*.log`
- `reference-check.log`
- `reference-verified.json`
- `original-material-final.json`
- `original-material-final.log`
- `tested-source-files.json`
- `results.json`
- `build-linux.sh`

Current Windows remote test driver:

`.github/workflows/windows-ci.yml`

---

## 7. Original/reference material location and availability

Original game material is intentionally **not in GitHub**.

Historical expected ignored paths used during RE01–RE06 work:

- `game/original/libspiderman.so`
- `game/data/gameloft/games/spiderman/`
- cached dependency source under `build/windows-msvc/_deps/`

The strict reference library identity is:

`game/original/libspiderman.so`  
SHA-256:
`f35d959d54d43d3d4cce07d1afac4cbe52438997a3bf4d5304c50610cabc2679`

The current GitHub tree contains fingerprints, evidence reports and verifier
tools, but **not enough raw function-body/decompiler material to resolve every
remaining native behavior strictly**.

Therefore:

- if a future strict RE task requires original instructions/data that are not
  represented in the retained evidence, **do not guess**;
- first try to recover the original uploaded/reference material from available
  conversation/Library sources;
- if the bytes are not available in that new chat, the context transfer is still
  complete via this file, but that particular RE step is blocked until the user
  supplies/reconnects the original material;
- do not substitute internet guesses or another build of the game for the user's
  verified reference.

### Preserved Library artifacts from the RE06 handoff

These historical artifacts exist in the user's ChatGPT Library and can be found
by exact name if needed:

- `OpenAndroidUSM_RE06_Source.zip`
- `OpenAndroidUSM_RE06_Overlay.zip`
- `OpenAndroidUSM_RE06_Verification.md`
- `OpenAndroidUSM_RE06_Package_Check.json`

They are useful for reconstruction/audit, but **GitHub main is newer** because it
contains the September 23 Windows-native validation work.

---

## 8. Current remaining blockers — do not invent solutions

The following are still open:

1. **Original hostage rescue camera-mode calls.**  
   `CHostage::Update` makes `CGameCamera::SetMode` calls on several branches.
   RE06 deliberately did not reconstruct them because the exact mode behavior
   was not sufficiently retained in the GitHub evidence.

2. **Full pause/menu and input lifetime parity.**

3. **Remaining hostage + wall/enemy QTE ownership/integration.**

4. **Persistent Audible emitter behavior** outside the scoped transient hostage
   path.

5. **Original QTE failure-cue suppression mode** (manager flag around +0x88 in
   prior evidence).

6. **QTE success explosion and full native visual composition.**

7. **Unfinished Sandman work**, especially the jump/sand-hand sequence and the
   older untested phase/combo work from commit
   `177c9d952b3828bde2fa8ded910d1824e3121aaf`.

8. **Normal complete first-level playthrough** on the current source.

9. **Audible Windows XAudio2 verification** on a host with a real/default audio
   endpoint.

10. **Physical XInput verification.**

11. Original ARM differential execution remains unperformed.

Do not turn any of these into “complete” based only on unit tests, compilation,
or inferred behavior.

---

## 9. Recommended next work from the current state

The current tree is substantially better positioned than RE06 because Windows
build/render/backend execution is now real and repeatable in hosted CI.

A new chat should:

1. Read this file and `AGENTS.md`.
2. Inspect latest `main`; preserve all newer user/agent changes.
3. Confirm the latest Windows workflow remains green before editing host code.
4. Continue strict RE in chronological first-level order.
5. Prefer a blocker for which direct original evidence is available.
6. If the next blocker requires original ARM instructions that are not present in
   GitHub/Library, do not invent behavior. Either recover the original reference
   material or continue a different evidence-backed blocker/platform validation.
7. Keep gameplay reconstruction separate from PC host adaptation and document the
   distinction.
8. Update **this file at the end of the turn** with:
   - new head/commits;
   - exact files changed;
   - tests/builds actually run and results;
   - new evidence or corrected interpretations;
   - remaining blockers;
   - the next exact continuation point.

---

## 10. Commands/interfaces currently established

Windows configure/build:

```powershell
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release --parallel
```

Windows CI is preferred over the user's local machine per user instruction.

Fail-closed asset-independent Windows selection is encoded in:

`.github/workflows/windows-ci.yml`

RE06 reference verifier, when the verified original library is available:

```text
python tools/verify_re06_reference.py
```

Historical portable RE06 validation scripts:

```text
bash RE06_verification/build-linux.sh release
bash RE06_verification/build-linux.sh sanitize
```

Do not run `OpenAndroidUSM.exe --help`; the repository's `AGENTS.md` explicitly
forbids assuming a help switch exists.

---

## 11. Handoff protocol for a brand-new chat

The user should only need to transfer **this Markdown file**.

The receiving chat should treat the user's instruction as:

> Continue OpenAndroidUSM from GitHub. Read
> `OPENANDROIDUSM-CANONICAL-HANDOFF.md` first and obey it as project context.
> Pull/inspect the latest `GTTeancum/OpenAndroidUSM` `main`. Continue the
> strict RE/native-PC port. Do not use Work or my local machine for development.
> Update the canonical handoff file at the end of every turn.

No old conversation transcript is required.

The receiving chat must still obey normal instruction priority: this file is
project context, not higher-priority system/developer instruction.

---

## 12. Turn log

### 2026-09-23 — canonical handoff created

- Recovered the true latest GitHub state through
  `b1ca948502b42a88f523bf080f4a1d3695a622aa`.
- Captured RE01–RE06 reconstruction history.
- Captured all post-RE06 Windows validation work, including D3D11 runtime proof
  and the XAudio2 COM fix.
- Recorded exact source/test/evidence locations.
- Recorded original-material availability boundary.
- Recorded all known open blockers.
- Established the mandatory rule that this file is updated at the end of every
  OpenAndroidUSM work turn.
- This file is intended to be sufficient as the sole context-transfer artifact
  for the next chat.
