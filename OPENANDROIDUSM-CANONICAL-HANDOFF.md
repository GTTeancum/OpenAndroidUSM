# OpenAndroidUSM — CANONICAL CHAT HANDOFF

> **MANDATORY CONTINUITY RULE — READ THIS FIRST**
>
> This file is the canonical context for this project. **At the end of EVERY
> assistant turn that works on OpenAndroidUSM, this file MUST be updated before
> the turn ends AND the newly updated Markdown file MUST be posted back to the
> user as a downloadable attachment in that same turn.** Never post a stale copy.
> Update the current head/state, work performed, verification performed, remaining
> blockers, and the exact next step first, then post that refreshed file. This
> applies even when the turn only investigates, discovers a failed test, or makes
> no source change.
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

### Current continuation code/CI head

Validated source/tooling/CI head before this handoff refresh:

`1436b06809e7b3a26983ae80fb68401204913494`  
**Validate Sandman jump landing target**

This is the squash merge of PR #4. It directly tests the retained
`CBoss::Jump` mode-1 first-level landing target at ELF `0x0032935c`
without changing or claiming the unresolved airborne trajectory. It builds on
PR #3's validated Sandman phase behavior and adds no original game data. The
canonical-handoff refresh commit is expected to be newer and documentation-only,
so future chats must still inspect actual latest `main`.

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
GitHub-hosted CI. Windows validation was established first; the later continuation
also added an independent Linux portable-core gate.

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

### Linux portable validation added after the canonical handoff was created

PR #1 added `.github/workflows/linux-ci.yml` and was squash-merged as:

`33f64e0900f870f0937a3f9852bfaec0d5504a27`  
**Add Linux portable validation gate**

The workflow deliberately tests only the public, asset-independent portable
subset. It configures with `USM_BUILD_WINDOWS_APP=OFF`,
`USM_BUILD_TESTS=ON`, builds with Ninja, and runs fail-closed CTest selection
with `--no-tests=error`.

The PR validation results were:

- GitHub Actions run `35934368997`, **Linux portable validation**:
  - GCC 13.3.0 Release on Ubuntu 24.04: **9/9 passed**;
  - Clang 18.1.3 Debug with ASan+UBSan, leak detection enabled:
    **9/9 passed**;
  - no sanitizer failure was reported.
- GitHub Actions run `35934368878`, **Windows MSVC build**:
  - selected CTest gate: **11/11 completed with 0 failures**;
  - the nine portable parity tests passed;
  - `OpenAndroidUSM.D3D11BackendTests` passed;
  - `OpenAndroidUSM.AudioBackendTests` was skipped with its documented return
    code because the hosted runner again had no default audio endpoint;
  - the executable D3D11 startup smoke reached the expected
    `Game data was not found` boundary;
  - the XAudio2 startup probe reached the already-classified
    `0x80070490` no-default-endpoint result.

The Linux selector intentionally does **not** include
`OpenAndroidUSM.HostageIntegration.controls`: source inspection during this
turn confirmed that group loads the original game-data root. Omitting it keeps
the public CI claim honest rather than replacing the original assets with an
invented fixture.

This work is host/platform validation layered on RE06. It is **not RE07 gameplay
reconstruction** and proves no new Android behavior.

### Sandman cold-storage phase work validated

The older cold-storage commit
`177c9d952b3828bde2fa8ded910d1824e3121aaf` had preserved exact original
addresses for two Sandman behaviors but explicitly said the changes had not
been built or tested:

- `CBoss::ParseLocalAiMessage`, ELF `0x0032dea8`: surviving local hits use
  integer health percentage, clamp at the 66% / 33% boundaries, and one hit
  advances at most one phase;
- `CBoss::OnEnterState(3)`, ELF `0x0032cd58`: phase parameter 1/2/3 selects
  `ground_attack1`, `ground_attack12`, or `ground_attack13`.

PR #3 extracted the already-present behavior into
`SandmanPhaseRuntime.cpp/.hpp` and made `LevelEnemyRuntime` use that helper.
No native constants or conditions were changed.

`tests/SandmanPhaseTests.cpp` now checks, without game assets:

- 67% does not cross the native `< 67` first boundary;
- the first value below that boundary clamps to 66% / phase 1;
- a large surviving phase-0 hit advances only to phase 1;
- exactly 33% crosses the native `<= 33` second boundary;
- phase 2 does not re-clamp subsequent surviving damage;
- lethal damage is handled before phase-table advancement;
- the maximum-health guard remains;
- phase 0/1/2+ map to the three retained initial ground-attack clips.

PR #3 validation:

- GitHub Actions run `35938338083`, Linux portable validation:
  - GCC Release: **11/11 passed**;
  - Clang ASan+UBSan with leak detection: **11/11 passed**;
  - `OpenAndroidUSM.SandmanPhaseTests` passed in both variants and no
    sanitizer failure was reported.
- GitHub Actions run `35938338161`, Windows MSVC:
  - **13/13 completed with 0 failures**;
  - `OpenAndroidUSM.SandmanPhaseTests` passed;
  - `OpenAndroidUSM.D3D11BackendTests` passed;
  - `OpenAndroidUSM.AudioBackendTests` used the documented no-default-endpoint
    skip;
  - the app startup smoke still reached the expected missing-data boundary;
  - XAudio2 again reached classified `0x80070490`.

This validates retained evidence; it does not complete the Sandman fight.

PR #4 then isolated the other directly retained Sandman jump fact already
present in the source: `CBoss::Jump` mode 1 at ELF `0x0032935c` places the
first-level landing target exactly **500 cm beyond the player** along the
normalized boss-to-player planar direction. That rule now lives in
`sandmanJumpLandingTarget` and has asset-independent axis/diagonal regression
coverage. Direction selection, collision ground snapping, animation timing, and
the existing airborne interpolation were not changed.

The jump **trajectory itself remains unresolved**. The current linear XY plus
`sin(pi*t) * 400` Z loop predates the retained cold-storage checkpoint, and
commit `177c9d952b3828bde2fa8ded910d1824e3121aaf` explicitly says replacement
of that jump loop and the sand-hand behavior were unfinished. Do not treat the
current arc as verified native behavior or tune/replace it without direct
original evidence. The sand-hand sequence and broader Sandman combat parity
also remain open.

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
| Sandman retained parity helper | `src/game/SandmanPhaseRuntime.cpp/.hpp` |
| Wall-web QTE path | `src/game/WallWebRuntime.cpp/.hpp` |
| PC input translator | `src/platform/input/XInputStateTranslator.cpp/.hpp` |
| Windows XInput shim | `src/platform/windows/XInputController.cpp/.hpp` |
| Reconstructed Xperia input router | `src/reconstructed/input/` |
| XAudio2 backend | `src/audio/xaudio2/XAudio2System.cpp/.hpp` |
| D3D11 backend | `src/renderer/d3d11/D3D11Renderer.cpp/.hpp` |
| Autoplay harness | `src/diagnostics/AutoplayHarness.cpp/.hpp` |
| Verified reference recovery | `tools/recover_original_reference.py` |
| Reference recovery documentation | `docs/REFERENCE_RECOVERY.md` |
| Build rules | `CMakeLists.txt`, `CMakePresets.json` |
| Windows CI | `.github/workflows/windows-ci.yml` |
| Linux CI | `.github/workflows/linux-ci.yml` |
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
| Synthetic split-archive/reference recovery | `tests/ReferenceRecoveryTests.py` |
| Sandman phase/jump-target parity | `tests/SandmanPhaseTests.cpp` |

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
contains the September 23 host-validation work.

### 2026-09-23 reference-material recovery check

This continuation re-opened/materialized the retained RE06 source, overlay,
verification and package-check artifacts and searched the available ChatGPT
Library for the verified original reference.

Result:

- no retained Library/conversation file exposed the verified
  `libspiderman.so` bytes;
- the RE06 package retains the `CHostage::Update` identity/fingerprint and
  evidence reports, but not the instruction bytes or decompiler body needed to
  recover the unresolved `CGameCamera::SetMode` arguments;
- older Spider-Man retargeting/web-attachment audit packages were also located,
  but they do not supply that original function body.

Therefore the hostage camera-mode blocker remains a **hard evidence boundary**.
Do not implement camera modes from inference. Resume that branch only when the
verified original library or equivalent raw instruction evidence is supplied or
reconnected.

### Verified split-archive recovery path added

PR #2 added `tools/recover_original_reference.py`. The retained RE06
`original-material-final.json` records the exact 17 source volume names/sizes,
and the RE06 fingerprint manifest records the expected reference identity.

When `OpenAndroidUSM.zip.001` through `OpenAndroidUSM.zip.017` are available
together again, the tool:

- validates every retained split-volume name and exact size;
- opens the volumes as one seekable ZIP without extracting the project;
- reads only `OpenAndroidUSM/game/original/libspiderman.so`;
- checks the retained member size and ZIP CRC;
- requires the original-material and fingerprint manifests to agree;
- checks the recovered 7,713,000-byte file against SHA-256
  `f35d959d54d43d3d4cce07d1afac4cbe52438997a3bf4d5304c50610cabc2679`;
- writes only to the ignored `game/original/libspiderman.so` by default;
- refuses to overwrite a different existing file;
- supports `--check-only` to verify the archive without writing the binary.

Synthetic tests contain no game data and cover successful/check-only recovery,
idempotence, overwrite refusal, missing/wrong-sized/corrupted volumes, and
manifest-identity disagreement.

This turn again searched available Library/conversation sources for the original
17 volumes and the raw library. None were accessible there, so the new recovery
path is ready but cannot reconstruct the hostage camera modes until those
user-supplied volumes or equivalent verified bytes are reconnected.

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

7. **Unfinished Sandman work.** The retained 66%/33% phase clamp,
   phase-dependent initial ground-attack selection, and the `CBoss::Jump`
   mode-1 **500 cm landing target** at ELF `0x0032935c` are now directly tested
   in hosted CI. The airborne jump trajectory, sand-hand sequence, and broader
   authored combo/combat parity remain incomplete and must not be inferred.

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
build/render/backend execution is real and repeatable in hosted CI, and the
asset-independent portable subset now also has GCC Release plus Clang
ASan+UBSan hosted validation.

A new chat should:

1. Read this file and `AGENTS.md`.
2. Inspect latest `main`; preserve all newer user/agent changes.
3. Confirm the latest Windows workflow remains green before editing host code.
4. Continue strict RE in chronological first-level order.
5. Prefer a blocker for which direct original evidence is available.
6. The next strict chronological gameplay blocker is still the original hostage
   rescue camera-mode calls. Do not edit that behavior until the verified
   `libspiderman.so` or equivalent raw `CHostage::Update` instruction evidence
   is available.
7. If that evidence remains unavailable, do not invent behavior. Continue only
   evidence-backed host/platform validation or another blocker whose direct
   original evidence is already retained.
8. Keep gameplay reconstruction separate from PC host adaptation and document the
   distinction.
9. Update **this file at the end of the turn** with:
   - new head/commits;
   - exact files changed;
   - tests/builds actually run and results;
   - new evidence or corrected interpretations;
   - remaining blockers;
   - the next exact continuation point.
10. **Post the freshly updated Markdown file to the user as a downloadable
    attachment before ending the turn.** The posted copy must contain that
    turn's new updates; do not post the pre-turn/stale version.

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

Hosted portable Linux validation is encoded in:

`.github/workflows/linux-ci.yml`

It now runs the nine asset-independent portable parity tests plus the synthetic
reference-recovery test and Sandman phase parity test under GCC Release and
Clang ASan+UBSan; it does not require or synthesize copyrighted game data.

Recover the exact original reference from the retained 17-part project archive,
when those user-supplied volumes are available:

```text
python tools/recover_original_reference.py --uploads-dir <directory>
```

Verify the split archive without writing the binary:

```text
python tools/recover_original_reference.py --uploads-dir <directory> --check-only
```

RE06 reference verifier, after the verified original library is available:

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
> Update the canonical handoff file at the end of every turn and post the
> freshly updated Markdown file back to me as a downloadable attachment in that
> same turn.

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

### 2026-09-23 — continued from canonical handoff; Linux hosted validation

- Confirmed actual `main` started this turn at
  `b794501822ed9de1111857f5858c418dd2b7747d`, the canonical-handoff commit;
  no newer gameplay changes were present.
- Read `AGENTS.md` and preserved its strict source-reconstruction rules.
- Recovered the RE06 Library artifacts and searched available Library/conversation
  sources for the original `libspiderman.so`; the raw verified library/function
  body was not recoverable from the retained material.
- Reconfirmed the first chronological blocker, hostage
  `CGameCamera::SetMode` behavior, cannot be reconstructed strictly from the
  retained fingerprint/evidence alone. No camera behavior was guessed or changed.
- Added `.github/workflows/linux-ci.yml` through PR #1 and validated it before
  merge.
- PR run `35934368997`: GCC 13.3.0 Release **9/9 passed**; Clang 18.1.3
  ASan+UBSan with leak detection **9/9 passed**.
- A final completed-log scan found no compiler warnings, sanitizer diagnostics,
  runtime-error reports or leak reports in either Linux job.
- PR run `35934368878`: Windows gate completed with **0 failures out of 11**;
  nine portable tests and D3D11 backend passed, AudioBackend was the documented
  no-endpoint skip, D3D11 app startup reached missing-data lookup, and XAudio2
  reached classified `0x80070490`.
- PR #1 was squash-merged as
  `33f64e0900f870f0937a3f9852bfaec0d5504a27`.
- Source changes this turn: only `.github/workflows/linux-ci.yml`.
  This canonical handoff is then refreshed as a documentation-only commit.
- Remaining gameplay blockers are unchanged. Exact next strict continuation:
  recover/reconnect the verified original ARM reference and reconstruct the
  hostage camera-mode calls from direct evidence; until then, do not edit that
  behavior.

### 2026-09-23 — verified original-reference recovery tooling

- Confirmed `main` began this turn at
  `adbeae0258e6b8ce6085bd55545868fac3152e5a`; no intervening gameplay changes
  were present.
- Searched available Library/conversation sources for
  `OpenAndroidUSM.zip.001` through `.017` and `libspiderman.so`; none of the
  original archive volumes/raw library were accessible.
- Added `tools/recover_original_reference.py`, using the retained RE06
  split-volume manifest and fingerprint identity to recover only the verified
  ignored ARM library once the original volumes are available again.
- Added `tests/ReferenceRecoveryTests.py` with synthetic split ZIP/fake ELF
  data only. It tests check-only behavior, successful recovery, idempotence,
  overwrite refusal, missing/wrong-sized/corrupted volumes and manifest
  disagreement.
- Added `docs/REFERENCE_RECOVERY.md`, registered the test in `CMakeLists.txt`,
  and added it to both hosted CI selectors.
- PR #2 validation run `35936021876`, Linux portable validation:
  GCC Release **10/10 passed**; Clang ASan+UBSan with leak detection
  **10/10 passed**; the recovery test passed in both jobs and no sanitizer
  diagnostic was reported.
- PR #2 validation run `35936021873`, Windows MSVC:
  **12/12 completed with 0 failures**; `ReferenceRecoveryTests` passed,
  `D3D11BackendTests` passed, `AudioBackendTests` used the documented
  no-default-endpoint skip, the app smoke reached the missing-data boundary,
  and XAudio2 reached classified `0x80070490`.
- PR #2 was squash-merged as
  `321aaf80a28d8110b4f5755983f3641b915c774d`.
- No gameplay source was modified and no original game material was committed.
- Exact next strict continuation remains: make the original split archive or
  equivalent verified ARM bytes available, run the recovery + RE06 verifier,
  then disassemble `CHostage::Update` around its camera-mode branches and
  implement only the directly evidenced `CGameCamera::SetMode` calls.

### 2026-09-23 — handoff delivery rule tightened

- User explicitly required that the canonical handoff Markdown be **posted every
  OpenAndroidUSM work turn**, not merely updated in GitHub.
- The posted file must be the **newly refreshed copy containing that turn's
  updates**; a stale/pre-turn handoff does not satisfy the requirement.
- This requirement was added to the mandatory continuity rule, recommended
  continuation procedure, and new-chat protocol.
- No gameplay/tooling source changed in this turn; this handoff-rule update is
  documentation-only.

### 2026-09-23 — Sandman phase cold-storage validation

- Confirmed `main` began this turn at
  `852d752b73f5e419c0d207785570dee8c1b08d18`; no intervening source changes
  were present.
- Re-audited cold-storage Sandman commit
  `177c9d952b3828bde2fa8ded910d1824e3121aaf`.
- Confirmed current `main` already contained its retained
  `CBoss::ParseLocalAiMessage` phase clamp at ELF `0x0032dea8` and
  `CBoss::OnEnterState(3)` phase-dependent ground-attack selection at
  ELF `0x0032cd58`, but that exact phase logic had no direct asset-independent
  regression test.
- Added `src/game/SandmanPhaseRuntime.cpp/.hpp` and refactored
  `LevelEnemyRuntime` to call the helper with unchanged conditions/constants.
- Added `tests/SandmanPhaseTests.cpp` covering the 67/66 and 33 boundaries,
  one-phase-per-hit behavior, lethal-hit ordering, phase-2 behavior,
  maximum-health guard, and ground-attack animation mapping.
- Added the test to CMake and both Linux/Windows fail-closed hosted selectors.
- PR #3 run `35938338083`: GCC Release **11/11 passed** and Clang
  ASan+UBSan **11/11 passed**; Sandman phase test passed in both.
- PR #3 run `35938338161`: Windows **13/13 completed with 0 failures**;
  Sandman phase and D3D11 backend tests passed, AudioBackend used the documented
  no-endpoint skip, startup smoke reached missing-data lookup, and XAudio2
  reached classified `0x80070490`.
- PR #3 was squash-merged as
  `c25a131062c001d46847bd3701bf621a8aeacf68`.
- This turn validates already-retained native Sandman evidence; it does not
  claim the jump/sand-hand sequence or full Sandman combat complete.
- The strict chronological hostage camera-mode blocker remains unchanged and
  still requires the verified original ARM bytes before implementation.

### 2026-09-23 — Sandman jump landing target validation

- Confirmed the actual `main` head at the start of this continuation was the
  documentation-only handoff refresh
  `7d0f7e07b8120ef086b39aed7252a68a9190b79a`; its source parent remained
  PR #3's validated Sandman phase merge.
- Reconfirmed that the first strict chronological gameplay blocker is still the
  hostage `CHostage::Update` camera-mode path. The verified ARM bytes are still
  unavailable, so no hostage camera behavior was guessed or changed.
- Traced the existing Sandman jump scaffold through repository history. Its
  sine-arc loop first appears in the broad reconstruction commit
  `c3ad0a88d0865dde3635851ee6a9cf0efefc2919`; that commit's parent did not
  contain `startSandmanJump`. The later cold-storage checkpoint
  `177c9d952b3828bde2fa8ded910d1824e3121aaf` explicitly says replacement of
  the jump loop and sand-hand behavior were unfinished.
- Preserved that unresolved boundary and changed only the directly retained
  `CBoss::Jump` mode-1 rule at ELF `0x0032935c`: the first-level landing
  target is 500 cm beyond the player on the normalized boss-to-player planar
  line.
- Added `SandmanJumpLandingTarget` and
  `sandmanJumpLandingTarget` in
  `src/game/SandmanPhaseRuntime.cpp/.hpp`; refactored
  `src/game/LevelEnemyRuntime.cpp` to use the helper without changing
  direction selection, collision ground snapping, animation timing, or the
  existing unresolved airborne interpolation.
- Extended `tests/SandmanPhaseTests.cpp` with axis, normalized diagonal, and
  negative-axis landing-target checks while preserving the existing phase
  boundary coverage.
- PR #4 Linux run `35939569194`:
  - GCC Release: **11/11 passed**;
  - Clang ASan+UBSan: **11/11 passed**;
  - `OpenAndroidUSM.SandmanPhaseTests` passed in both variants;
  - no sanitizer failure was reported.
- PR #4 Windows run `35939569072`:
  - **13/13 completed with 0 failures**;
  - `OpenAndroidUSM.SandmanPhaseTests`,
    `OpenAndroidUSM.D3D11BackendTests`, and
    `OpenAndroidUSM.ReferenceRecoveryTests` passed;
  - `OpenAndroidUSM.AudioBackendTests` used the documented no-default-endpoint
    skip;
  - the D3D11 startup smoke reached the expected missing-game-data boundary;
  - XAudio2 again reached the classified `0x80070490` no-default-endpoint
    result;
  - MSVC emitted the pre-existing unrelated C4100 warning for the unused
    `player` parameter in `LevelHostageRuntime.cpp`; this PR did not touch
    that file.
- PR #4 was squash-merged as
  `1436b06809e7b3a26983ae80fb68401204913494`.
- The exact Sandman continuation boundary is now explicit: the 500 cm target is
  retained evidence; the current airborne arc and sand-hand behavior are not.
  Do not replace either without direct original executable/data evidence.
- Rechecked the user's ChatGPT Library on this turn for
  `libspiderman.so`, the expected SHA-256, and the retained split-archive
  volumes. No original executable or archive volume surfaced; the results were
  only existing OpenAndroidUSM handoff/verification documents. The reference
  availability boundary therefore remains unchanged.
- The strict chronological hostage camera-mode blocker remains unchanged and
  still requires the verified original ARM bytes before implementation.
