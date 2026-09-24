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

Last handoff refresh: **2026-09-24**

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
- **2026-09-24 priority correction from the user:** gameplay **1:1** is the
  active priority. The unresolved hostage camera-mode calls are polish and must
  **not** block gameplay reconstruction. Prioritize combat, boss behavior,
  movement/physics, QTE ownership, input lifetime, damage/state transitions,
  and normal-flow gameplay parity before camera/presentation polish.

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

### Latest `main` observed at this takeover

Repository head inspected in the new chat before this refresh:

`00fc5f4bff15015a2799f86c1e9cb59c1aa31da7`  
**Record ultimate-state boundary validation [skip ci]**

That commit is documentation-only. Its parent,
`75df3a591abed97f97f997a90956c81667b2adad`, remains the latest validated
source/tooling/CI change. This handoff-refresh commit is expected to become the
newer `main` head without changing runtime source.


### Current source state before this handoff file was added

Latest code/documentation head inspected before creating this handoff:

`b1ca948502b42a88f523bf080f4a1d3695a622aa`  
**Document Windows native validation [skip ci]**

The handoff-file commit itself is expected to be newer. Therefore a new chat
must always inspect the actual latest `main` instead of treating the SHA above
as a permanent head pin.

### Current continuation code/CI head

Validated source/tooling/CI head before this handoff refresh:

`b4a692f7f081a0a626dd8c85148a20290f4cf648`  
**Use native Sandman melee startup timing**

This is the squash merge of PR #14 and is a **gameplay-parity change**.

The retained native path now applied to Sandman's task-3 ground attack is:

- CBoss task/state 3 is owned by `CBehaviorMeleeAttack`;
- `CBehaviorMeleeAttack::StateEnter` at
  `0x003baef8-0x003bb478` retains the selected `EnemyAttackInfo`,
  points at the target on entry, and passes
  `clipLength / EnemyAttackInfo+8` to `SetAnimWithSpeed`;
- states 9/10 call `NeedTurning` only while that startup timer is active and
  `EnemyAttackInfo+0xc` requests turning;
- authored successor clips remain governed by the +0x38/+0x6c and +0x46
  continuation path fixed in PR #11.

Before PR #14, the Sandman-specific loop retained the selected attack after
PR #13 but still ran the phase-selected opening clip at raw speed 1.0 and
forced Sandman to face Spider-Man on **every** boss update, including authored
successor/recovery clips. PR #14 now uses the loaded selected attack's startup
duration and turn flag directly, faces the target on melee entry, limits
continuous turning to the native startup window, and leaves successor clips at
their existing authored speed. No timing constants or later-phase attack IDs
were guessed.

The real-asset smoke now derives the phase-zero attack wall-clock boundary from
the loaded `EnemyAttackInfo` instead of assuming raw Collada duration and
checks the configured startup-turn behavior.

Validation is green:

- PR Linux run `36026983859`: GCC Release **12/12**, Clang ASan+UBSan
  **12/12**;
- PR Windows run `36026983852`: **14/14**;
- post-merge Linux run `36027569749`: GCC **12/12**, Clang ASan+UBSan
  **12/12**;
- post-merge Windows run `36027569748`: **14/14**.

Both Windows runs reached the expected missing-game-data startup boundary and
the classified no-default-endpoint XAudio2 result `0x80070490`.

The unresolved Sandman **jump interpolation and sand-hand behavior remain
open** and were not changed. The user's active mandate remains gameplay 1:1
first; hostage camera behavior is deferred polish and must not block gameplay
work. Future chats must inspect actual latest `main`, because documentation
commits after this source SHA may be newer.

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

PR #5 then isolated and directly tested the other retained mechanism already
used by the Sandman WIP:

- `IBehaviorBase::SpecialAnimActionCheck` at `0x003a8c60` resolves
  `AIAnimSpecialActionInfo+0x38` as an authored animation and stores it at
  `IBehaviorBase+0x6c`;
- `CBehaviorMeleeAttack::UpdateAttackMelee_DoAttack` at `0x003b9e44`
  follows that successor only when `EnemyAttackInfo+0x46` permits it;
- the final string in `EnemysSpecialAnimConfigs.bin` is therefore retained as
  `nextAnimationName`, not treated as an effect asset.

`specialAnimationSuccessor` in
`EnemySpecialActionConfig.cpp/.hpp` now owns that already-present lookup rule,
and `LevelEnemyRuntime` calls the helper with unchanged behavior.
`tests/EnemySpecialActionTests.cpp` uses synthetic records only and verifies
the +0x38 string, action-type-zero attack filtering, oversized attack-ID
rejection, and the +0x46 permit/deny gate. This validates the successor
**mechanism**; it does not prove the complete authored Sandman chain, the
unresolved jump arc, or sand-hand behavior.

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
| Enemy special-action parser/successor | `src/game/EnemySpecialActionConfig.cpp/.hpp` |
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
| Enemy special-animation successor parity | `tests/EnemySpecialActionTests.cpp` |

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

## 8. Current remaining blockers — gameplay first; do not invent solutions

The user explicitly changed priority on **2026-09-24**: gameplay 1:1 comes
before camera/presentation polish. Do not allow the hostage camera-mode evidence
gap to block gameplay work.

Current open items, grouped by priority:

1. **Level 1 gameplay parity — Sandman boss fight is the clearest remaining
   first-level gameplay gap.**  
   The deterministic normal Level 1 route already has a 296/296 uninterrupted
   no-diagnostic-teleport closure in the retained project evidence, so simple
   traversal is not the blocker. Sandman's retained 66%/33% phase clamp,
   phase-dependent initial ground attack, 500 cm jump landing target, authored
   special-animation successor mechanism, selected melee-attack ownership, and
   native startup timing/turn semantics are now covered through PR #14. The
   **complete native boss task/combo sequence, airborne jump trajectory, and
   sand-hand behavior remain incomplete**. The legacy linear-XY +
   `sin(pi*t)*400` jump interpolation is not verified native behavior and
   must not be tuned by feel.

2. **Remaining gameplay QTE ownership/integration.**  
   Shared cinematic/hostage manager behavior is substantially recovered, but
   broader wall/enemy QTE ownership and lifetime integration are not a blanket
   1:1 claim. Continue only from direct executable/data evidence.

3. **Full pause/menu and gameplay input-lifetime parity.**  
   The exact Start/IGM one-shot clear boundary and CKeyPad press/release
   lifetime have direct coverage, but the complete pause/menu lifecycle is not
   yet a 1:1 gameplay claim.

4. **Persistent Audible emitter/gameplay audio behavior** outside the scoped
   transient hostage path.

5. **Original QTE failure-cue suppression mode** (manager flag around +0x88 in
   prior evidence).

6. **Original ARM differential execution** remains unperformed and would be
   useful for hard gameplay cases when the verified reference is available.

Lower-priority polish/host items — keep them open, but do not block gameplay:

7. **Hostage rescue camera-mode calls.**  
   Exact `CGameCamera::SetMode` arguments remain unresolved. The user has
   explicitly classified this as polish work; leave it unresolved until
   gameplay parity is substantially farther along.

8. **QTE success explosion and full native visual composition.**  
   The reconstructed result-sprite path now has real Windows WARP readback
   coverage, but the native success explosion itself remains unresolved.

9. **Audible Windows XAudio2 verification** on a host with a real/default audio
   endpoint.

10. **Physical XInput verification.**

Do not turn any open item into “complete” based only on compilation, unit tests,
or inferred behavior. Conversely, do not let a presentation-only gap prevent
progress on directly evidenced gameplay work.

---

## 9. Recommended next work from the current state

The active mandate is **gameplay 1:1 first**.

A new chat should:

1. Read this file and `AGENTS.md`, inspect latest `main`, and preserve all
   newer user/agent changes.
2. Treat the hostage camera calls as deferred polish, **not** the next blocker.
3. Continue Level 1 gameplay parity first. The highest-value current target is
   Sandman's actual boss behavior:
   - preserve PR #11-#14's authored-successor, selected-attack, startup-timing,
     and startup-turn semantics;
   - recover the complete authored Sandman task/combo sequence where direct
     executable/data evidence exists;
   - recover/replace the legacy jump interpolation only from direct native
     evidence;
   - reconstruct sand-hand behavior only from direct native evidence.
4. If a particular Sandman sub-behavior lacks enough evidence, move to the next
   directly evidenced **gameplay** gap (QTE ownership/lifetime, pause/input
   lifetime, combat/physics/state behavior) rather than spending the turn on
   camera or cosmetic polish.
5. Keep the source-of-truth discipline: no guessed constants, hand-tuned boss
   arcs, invented animation names, or “looks right” task transitions.
6. Keep gameplay reconstruction distinct from host adaptation and document that
   distinction.
7. Validate source changes through the hosted Linux GCC + Clang sanitizer gates
   and Windows MSVC gate before merge; validate the exact squash commit again
   after merge.
8. Update **this file at the end of every turn** with:
   - new head/commits;
   - exact files changed;
   - tests/builds actually run and results;
   - new evidence or corrected interpretations;
   - remaining gameplay gaps;
   - the next exact continuation point.
9. **Post the freshly updated Markdown file to the user as a downloadable
   attachment before ending the turn.** The posted copy must contain that
   turn's new updates; do not post a stale/pre-turn version.

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
reference-recovery test, Sandman parity test, and enemy special-animation
successor parity test under GCC Release and Clang ASan+UBSan; it does not
require or synthesize copyrighted game data.

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
- Post-merge push validation for that exact source SHA:
  - Linux run `35940028777` completed successfully;
  - Windows run `35940028816` also completed successfully with the same
    13/13 asset-independent selection and documented audio-endpoint
    classification.
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

### 2026-09-24 — authored special-animation successor validation

- Confirmed `main` began this turn at
  `45d5bef530c7356a6309a4bddf4faf18b1a4ee63`, a documentation-only handoff
  refresh; the latest gameplay/tooling source was PR #4's Sandman jump-target
  merge.
- Closed the prior handoff's pending status: PR #4 post-merge Windows run
  `35940028816` completed successfully.
- Reconfirmed the strict chronological gameplay blocker remains the hostage
  `CHostage::Update` camera-mode path. No hostage camera behavior was guessed
  or changed.
- Audited cold-storage commit
  `177c9d952b3828bde2fa8ded910d1824e3121aaf` and the current source around
  the authored ground-attack successor path.
- Retained native evidence used this turn:
  - `IBehaviorBase::SpecialAnimActionCheck` at `0x003a8c60` resolves
    `AIAnimSpecialActionInfo+0x38` and stores it at
    `IBehaviorBase+0x6c`;
  - `CBehaviorMeleeAttack::UpdateAttackMelee_DoAttack` at `0x003b9e44`
    follows that stored successor only when `EnemyAttackInfo+0x46` permits
    special-animation continuation.
- Added `specialAnimationSuccessor` to
  `src/game/EnemySpecialActionConfig.cpp/.hpp` and refactored
  `src/game/LevelEnemyRuntime.cpp` to call it. This is a behavior-preserving
  extraction of logic that was already present; it does not add a new Sandman
  transition or infer an unresolved animation.
- Added `tests/EnemySpecialActionTests.cpp` with synthetic attack and
  special-action records only. Coverage verifies:
  - the final special-action string is retained as `nextAnimationName`;
  - sound-map IDs remain separate from that successor string;
  - only action-type-zero non-negative attack events participate;
  - an attack ID above signed 16-bit range is not truncated into a lookup;
  - `EnemyAttackInfo+0x46` permits or blocks the successor as retained.
- Registered the new test in CMake and both fail-closed hosted selectors.
- During branch construction, two initial workflow text replacements treated
  the regex suffix `$'` as a JavaScript replacement token and duplicated
  workflow tails. Those malformed branch-only commits triggered failed
  pre-PR Actions runs. They were detected before the PR, both workflows were
  restored from `main`, and the final PR diff contained only the intended
  one-line selector changes. No malformed workflow commit was merged.
- PR #5 validation:
  - Linux run `35975912462`: GCC Release **12/12 passed** and Clang
    ASan+UBSan **12/12 passed**; the new successor test passed in both and no
    sanitizer diagnostic was reported.
  - Windows run `35975912479`: **14/14 completed with 0 failures**;
    `OpenAndroidUSM.EnemySpecialActionTests`,
    `OpenAndroidUSM.SandmanPhaseTests`,
    `OpenAndroidUSM.D3D11BackendTests`, and
    `OpenAndroidUSM.ReferenceRecoveryTests` passed;
    `OpenAndroidUSM.AudioBackendTests` used the documented no-default-endpoint
    skip; startup reached the expected missing-data boundary; XAudio2 reached
    classified `0x80070490`.
  - MSVC again emitted the pre-existing unrelated C4100 warning for the unused
    `player` parameter in `LevelHostageRuntime.cpp`; this work did not touch
    that file.
- PR #5 was squash-merged as
  `a56cf9f8eedb337e1dc6e33683c213c5603b3ace`.
- Post-merge validation for that exact squash commit also completed green:
  - Linux run `35976412987`: GCC **12/12** and Clang ASan+UBSan **12/12**;
  - Windows run `35976412983`: **14/14 with 0 failures**, with the same
    documented AudioBackend skip, missing-data D3D11 startup boundary, and
    classified XAudio2 `0x80070490`.
- This turn validates successor plumbing only. The complete authored Sandman
  combo chain, current unverified jump arc, and sand-hand behavior remain open.
- Exact next strict continuation remains: obtain verified original
  `libspiderman.so` / raw `CHostage::Update` instructions and reconstruct
  the hostage camera-mode calls from direct evidence. If that material remains
  unavailable, continue only another already-retained evidence-backed slice.

### 2026-09-24 — retained hostage QTE state-gate validation

- Confirmed `main` began this turn at
  `31ff10270470b16e34b1a1ff373bd26adb7a73cc`, the documentation-only handoff
  refresh after PR #5. The latest gameplay/tooling source before this turn was
  `a56cf9f8eedb337e1dc6e33683c213c5603b3ace`.
- Reconfirmed the strict chronological blocker is still the original
  `CHostage::Update` camera-mode calls. No camera behavior was guessed or
  changed.
- Audited the explicitly unresolved QTE failure-cue suppression flag at
  manager+`0x88`. RE05 states that it remains unresolved, so this turn made no
  attempt to implement or infer it.
- Re-audited retained hostage evidence around `CHostage::Update`:
  - ELF `0x00328444-0x0032847e`: the transient untie-sound path requires
    hostage state 2, Player rescue state 28, and **exactly 7** remaining QTE
    actions before querying/playing `SFX_QTE_UNTIE`;
  - ELF `0x003283ba` onward: hostage result observation accepts both
    `SuccessDisplay` and `SuccessHandled` as success, and both
    `FailureDisplay` and `FailureHandled` as failure.
- Added pure helpers in `src/game/LevelHostageRuntime.cpp/.hpp`:
  - `hostageUntieSoundEligible`;
  - `hostageQteOutcomeForManagerState`.
  `LevelHostageRuntime` now calls those helpers with unchanged ordering and
  semantics.
- Extended the already asset-independent
  `OpenAndroidUSM.HostageIntegration.dispatch` group in
  `tests/HostageIntegrationTests.cpp` to pin:
  - exact phase 2;
  - exact Player state 28;
  - exact remaining count 7 (rejecting 6 and 8);
  - success/failure display and handled states;
  - non-terminal manager states remaining `Running`.
- No CMake or CI selector change was required because the existing hosted
  `HostageIntegration.dispatch` target already runs without original game
  data.
- PR #6 validation:
  - Linux run `35978882848`: GCC Release **12/12 passed** and Clang
    ASan+UBSan **12/12 passed**; the expanded hostage dispatch test passed in
    both and no sanitizer failure was reported.
  - Windows run `35978882937`: **14/14 completed with 0 failures**;
    `OpenAndroidUSM.HostageIntegration.dispatch` passed;
    `OpenAndroidUSM.D3D11BackendTests` and
    `OpenAndroidUSM.ReferenceRecoveryTests` passed;
    `OpenAndroidUSM.AudioBackendTests` used the documented no-default-endpoint
    skip; D3D11 startup reached the expected missing-data boundary; XAudio2
    reached classified `0x80070490`.
  - MSVC still reports the pre-existing unrelated C4100 warning for the unused
    `player` parameter in `LevelHostageRuntime.cpp`; line numbering moved
    because of the new helpers, but the warning is not caused by the new
    behavior and was not hidden.
- PR #6 was squash-merged as
  `a3b5731510b9c9742fb87cbe9e9eb27137d28f29`.
- Post-merge validation for that exact squash commit completed green:
  - Linux run `35979398362`: GCC **12/12** and Clang ASan+UBSan **12/12**;
  - Windows run `35979398369`: **14/14 with 0 failures**, with the same
    documented AudioBackend skip, missing-data D3D11 startup boundary, and
    classified XAudio2 `0x80070490`.
- This turn increases public/hosted proof around the hostage rescue path but
  does **not** reduce the missing-camera evidence requirement.
- Exact strict next continuation remains recovery of the verified
  `libspiderman.so` / raw `CHostage::Update` instruction evidence for the
  camera-mode calls. If that material remains unavailable, the next safe
  retained-evidence fallback identified this turn is the rescue-eligibility
  slice already documented by RE02:
  - constructor default `ButtonHeight=85` at `0x00328c20`;
  - positive-only `ProcessUserAttr` adjustment at `0x00328724`;
  - Player ultimate-state IDs **107..113** at `0x0033002c`.

### 2026-09-24 — retained Player ultimate-state boundary validation

- Confirmed `main` began this turn at
  `e53c448be29f94216ce76c3cde29affe1decc63f`, the documentation-only handoff
  refresh after PR #6. The latest gameplay/tooling source before this turn was
  `a3b5731510b9c9742fb87cbe9e9eb27137d28f29`.
- Reconfirmed the strict chronological blocker remains the original
  `CHostage::Update` camera-mode calls. No camera behavior was guessed or
  changed.
- Audited the prior handoff's retained rescue-eligibility fallback and corrected
  one stale implication: `HostageAttributes.hpp` already contained the exact
  `hostageButtonHeight` rule, and the hosted asset-independent
  `OpenAndroidUSM.QteParity.attributes` group already tested negative, zero,
  positive, and NaN cases before this turn. No duplicate ButtonHeight work was
  needed.
- The remaining uncovered retained fact was
  `Player::IsUltimate(-1)`, ELF `0x0033002c`: inclusive state IDs
  **107..113**.
- Added pure constexpr helper `isPlayerUltimateStateId` in
  `src/game/GameplayPlayer.hpp`; `GameplayPlayer::isUltimateState()` now
  delegates to it without changing runtime behavior.
- Extended `tests/QteParityTests.cpp`'s already-hosted `attributes` group to
  verify:
  - state 106 is not ultimate;
  - every state 107 through 113 is ultimate;
  - state 114 is not ultimate;
  - state 0 and the maximum uint16 state are not ultimate.
- No CMake or workflow selector change was required because
  `OpenAndroidUSM.QteParity.attributes` was already part of both hosted
  fail-closed selectors.
- PR #7 validation:
  - Linux run `35982045064`: GCC Release **12/12 passed** and Clang
    ASan+UBSan **12/12 passed**; `OpenAndroidUSM.QteParity.attributes`
    passed in both and no sanitizer diagnostic was reported.
  - Windows run `35982045284`: **14/14 completed with 0 failures**;
    `OpenAndroidUSM.QteParity.attributes`,
    `OpenAndroidUSM.D3D11BackendTests`, and
    `OpenAndroidUSM.ReferenceRecoveryTests` passed;
    `OpenAndroidUSM.AudioBackendTests` used the documented no-default-endpoint
    skip; D3D11 startup reached the expected missing-data boundary; XAudio2
    reached classified `0x80070490`.
  - MSVC again emitted the pre-existing unrelated C4100 warning for the unused
    `player` parameter in `LevelHostageRuntime.cpp`; this turn did not touch
    that file.
- PR #7 was squash-merged as
  `75df3a591abed97f97f997a90956c81667b2adad`.
- Post-merge validation for that exact squash commit completed green:
  - Linux run `35982563131`: GCC **12/12** and Clang ASan+UBSan **12/12**;
  - Windows run `35982563136`: **14/14 with 0 failures**, with
    `OpenAndroidUSM.QteParity.attributes` passing plus the same documented
    AudioBackend skip, missing-data D3D11 startup boundary, and classified
    XAudio2 `0x80070490`.
- A broader retained-document audit found no equally strong exact provenance
  for expanding the currently implemented grounded/airborne rescue exclusion.
  Do not refactor or extend that condition merely to create more work.
- The hostage eligibility fallback is therefore exhausted from the retained
  GitHub evidence currently available. Exact strict continuation returns to
  recovering the verified `libspiderman.so` / raw `CHostage::Update`
  instructions for the missing camera-mode calls. If that reference remains
  unavailable, continue only host/platform validation or another blocker with
  already-retained direct evidence.

### 2026-09-24 — new-chat takeover audit

- Took over from the user-supplied canonical handoff, then inspected actual
  GitHub `main` and found it one continuation newer than that uploaded copy.
- Read `AGENTS.md` and preserved the project's strict source-reconstruction,
  chronological-flow, no-guessing, and no-Work/local-machine rules.
- Confirmed repository head before this refresh was
  `00fc5f4bff15015a2799f86c1e9cb59c1aa31da7`, a documentation-only handoff
  commit. The latest validated source/tooling/CI head is PR #7's squash merge
  `75df3a591abed97f97f997a90956c81667b2adad`.
- Confirmed PR #7 already exhausted the previously identified hostage
  rescue-eligibility fallback: the existing hosted suite already covered the
  retained `ButtonHeight` rule, and PR #7 added direct hosted coverage for the
  inclusive Player ultimate-state IDs **107..113**.
- Searched current conversation and ChatGPT Library sources again for the exact
  verified `libspiderman.so` identity and for
  `OpenAndroidUSM.zip.001` through `.017`. Results contained only handoff/
  verification documents; the original binary/split volumes still were not
  accessible.
- Re-audited the retained first-hostage RE06 evidence against
  `LevelHostageRuntime` and `HostageIntegrationTests`. The exact repeated
  control calls, untie-sound ordering, success/failure/interruption handling,
  and repeated rescue-end stop behavior are already represented in the current
  runtime/tests. No speculative camera behavior was added.
- No gameplay, host, build, test, or workflow source was changed in this turn;
  no CI run was required for this documentation-only takeover refresh.
- The strict chronological blocker therefore remains unchanged: obtain the
  verified original ARM reference or equivalent raw `CHostage::Update`
  instruction evidence, then reconstruct only the directly evidenced
  `CGameCamera::SetMode` calls. Until that evidence is available, continue only
  another directly retained evidence-backed validation slice or host/platform
  validation; do not infer the missing camera modes.

### 2026-09-24 — hosted QTE IGM press-clear boundary validation

- Confirmed `main` began this turn at
  `2a23a9ba22558f137050418902850ebf56521a7c`, the documentation-only
  takeover handoff refresh. The latest validated source/tooling/CI head before
  this turn was PR #7's `75df3a591abed97f97f997a90956c81667b2adad`.
- Reconfirmed the strict chronological gameplay blocker remains the original
  `CHostage::Update` `CGameCamera::SetMode` calls. The verified
  `libspiderman.so` / raw function body is still unavailable in current
  conversation/Library sources, so no camera behavior was guessed or changed.
- Audited two nearby unresolved areas before editing:
  - native `CLevel::PauseTimer/ResumeTimer` evidence proves QTE-clock pause
    calls, but the complete Start/pause-menu lifecycle is not retained strongly
    enough to wire the PC pause menu without inference;
  - RE03 explicitly records the success explosion as a scaled/material-flagged
    draw whose exact rendering remains unresolved, so no explosion visual was
    approximated.
- Found one exact retained input fact that was implemented but not directly
  pinned by the hosted asset-independent suite:
  `CQTEManager::SetState`, ELF `0x0037a7a0-0x0037a7b2`, clears the direct
  IGM/Start **press flag** before result handoff without turning the held
  physical Start button into a release or resetting CKeyPad history.
- Changed only `tests/InputParityTests.cpp`:
  - press Start through the production XInput translator/router;
  - verify both direct and keypad publications observe the press/hold;
  - call `consumePausePress()` and verify only the direct one-shot press clears;
  - verify held/release state and `pressedFramesRemaining` are preserved;
  - verify the separate gameplay-keypad press/hold/release/history is unchanged;
  - release physical Start and verify the real release still arrives.
- No CMake/workflow selector change was required because
  `OpenAndroidUSM.InputParity.routing` was already in both fail-closed hosted
  selectors.
- PR #8 head `bbf7b112dde5d248124eb4d2805fa90eac5ae6ed` validation:
  - Linux run `35998455233`: GCC Release **12/12 passed** and Clang
    ASan+UBSan **12/12 passed**; `OpenAndroidUSM.InputParity.routing` passed in
    both and no sanitizer diagnostic was reported.
  - Windows run `35998455221`: **14/14 passed**;
    `OpenAndroidUSM.InputParity.routing` and
    `OpenAndroidUSM.D3D11BackendTests` passed; startup reached the expected
    missing-game-data boundary; XAudio2 reached the classified no-default-
    endpoint `0x80070490` path.
  - MSVC emitted the pre-existing unrelated C4100 warning for the unused
    `player` parameter in `LevelHostageRuntime.cpp`; this turn did not touch
    that file.
- PR #8 was squash-merged as
  `87150ca95323b9ed81ca74305caa2e9a518990b8`.
- Post-merge validation for that exact squash commit completed green:
  - Linux run `35999083591`: GCC **12/12** and Clang ASan+UBSan **12/12**;
    `OpenAndroidUSM.InputParity.routing` passed in both;
  - Windows run `35999083606`: **14/14 passed**, including
    `OpenAndroidUSM.InputParity.routing` and D3D11 backend validation, with the
    same expected missing-data startup boundary and classified XAudio2
    `0x80070490` no-endpoint result.
- This turn strengthens hosted proof of an already-reconstructed input boundary;
  it does **not** complete the native pause menu, QTE success explosion, or
  hostage camera behavior.
- Exact strict continuation remains recovery of the verified original
  `libspiderman.so` / raw `CHostage::Update` instructions for the camera-mode
  calls. If that material remains unavailable, continue only another
  directly-retained evidence-backed validation slice or host/platform
  validation; do not infer the missing camera modes.

### 2026-09-24 — native R1 global identity correction

- Confirmed `main` began this turn at
  `9173a4d86a9ee7de8fef9b27d638c1378a64ed92`, the documentation-only
  handoff refresh after PR #8. The latest validated source/tooling/CI head
  before this turn was `87150ca95323b9ed81ca74305caa2e9a518990b8`.
- Reconfirmed the strict chronological gameplay blocker remains the unresolved
  `CHostage::Update` `CGameCamera::SetMode` calls. A title-only search of
  current conversation/Library sources for `OpenAndroidUSM.zip.001`, `.002`,
  `.017`, and `libspiderman.so` returned no original reference material, so
  no camera behavior was guessed or changed.
- Audited retained RE02 R1-release evidence. `appKeyReleased`, ELF
  `0x003cc2b8-0x003cc2ea`, writes three distinct globals:
  - hostage rescue request;
  - the boolean at `0x00566518`, consumed/cleared by
    `InteractiveButton::Update` at `0x0032f1ac`;
  - the word/counter at `0x0056651c`, seeded to **4** and read/arithmetic-
    shifted by `CSwitchObject::Update` at `0x00310828`.
- Confirmed current source already published three values but named the latter
  two ambiguously as `switchRequested` and `switchRequestValue`. This could
  encourage a future implementation to conflate two separate native systems.
- Renamed those fields, with no behavioral change:
  - `switchRequested` -> `interactiveButtonRequested`;
  - `switchRequestValue` -> `switchCounter`.
- Exact files changed in PR #9:
  - `src/reconstructed/input/GameplayInputState.hpp`;
  - `src/reconstructed/input/XperiaKeyRouter.cpp`;
  - `tests/InputParityTests.cpp`;
  - `tests/InputTransitionsTests.cpp`;
  - `tests/QteParityTests.cpp`;
  - `tests/QteControlTests.cpp`.
- The first PR head exposed one missed test reference: Linux run
  `36001762380` failed Clang compilation in `tests/QteControlTests.cpp` on
  stale `switchRequested` / `switchRequestValue` names. No runtime source
  failure was involved. The stale test references were corrected in
  `d34f46e155b8526463dc86734116e73d57f58386` and fresh CI was allowed to
  complete before merge.
- Corrected PR #9 validation:
  - Linux run `36001893797`: GCC Release **12/12 passed** and Clang
    ASan+UBSan **12/12 passed**; `OpenAndroidUSM.InputParity.routing`,
    `OpenAndroidUSM.InputParity.transitions`, and
    `OpenAndroidUSM.QteControl.keypad` passed in both, with no sanitizer
    diagnostic reported.
  - Windows run `36001893810`: **14/14 passed**, including the same input/QTE
    groups and `OpenAndroidUSM.D3D11BackendTests`; application startup reached
    the expected missing-game-data boundary and XAudio2 reached the classified
    no-default-endpoint `0x80070490` path.
  - MSVC emitted the pre-existing unrelated C4100 warning for the unused
    `player` parameter in `LevelHostageRuntime.cpp`; this turn did not touch
    that file.
- PR #9 was squash-merged as
  `468d4577836936273973321eddfb145e7f06dd3d`.
- Post-merge validation for that exact squash commit completed green:
  - Linux run `36002471443`: GCC **12/12** and Clang ASan+UBSan **12/12**;
  - Windows run `36002471439`: **14/14 passed**, including routing,
    transitions, QTE keypad and D3D11 backend validation, with the same expected
    missing-data startup boundary and classified XAudio2 `0x80070490` result.
- This turn intentionally did **not** wire `InteractiveButton` or
  `CSwitchObject` consumers. RE02 explicitly warns that native object
  iteration/order and activation control the boolean clear and repeated
  arithmetic shift; consuming the counter once per host frame would be wrong.
  The exact lifetime/consumer integration therefore remains part of broader
  input/object parity, not a completed feature.
- Exact strict continuation remains recovery of the verified original
  `libspiderman.so` / raw `CHostage::Update` instructions for the hostage
  camera-mode calls. If that material remains unavailable, continue only
  another directly retained evidence-backed validation/source slice or
  host-platform validation; do not infer missing native behavior.

### 2026-09-24 — D3D11 WARP QTE feedback validation

- Confirmed `main` began this turn at
  `4059dd19a06f2b963d6953fdd1cc8973c4ac7901`, the documentation-only
  handoff refresh after PR #9. The latest validated source/tooling/CI head
  before this turn was `468d4577836936273973321eddfb145e7f06dd3d`.
- Reconfirmed the strict chronological gameplay blocker remains the unresolved
  hostage `CHostage::Update` `CGameCamera::SetMode` calls. The verified
  original ARM reference/raw instruction body is still unavailable, so no
  camera behavior was guessed or changed.
- Audited other first-level gaps before editing. Hostage ambient-help/random
  cadence still lacks enough retained instruction detail to implement strictly;
  the RE03 success-explosion sprite is also explicitly unresolved because its
  scaled/material-flagged draw has not been recovered completely. Neither was
  approximated.
- Chose an explicit RE03 host-validation gap instead: RE03 already proved the
  portable `QteFeedbackGeometry` XY/UV/alpha/order rules but stated that the
  D3D11 integration/compositing had not been compiled or visually verified.
- Added public `D3D11Renderer::uploadHud(const LevelHudAsset&)`, which is only
  a standalone entry to the pre-existing production `uploadHudTexture` path.
  It does not add a test-only renderer friend/hook or alter the full-level
  uploader.
- Expanded `tests/D3D11BackendTests.cpp` using **synthetic data only**:
  - constructs a minimal real-format `.bsprite` binary and loads it through
    `SpriteAtlas::load`;
  - constructs ATCA DDS payloads and loads them through
    `DdsAtcTexture::load`;
  - constructs the unrelated required TGA slot through `TgaTexture::load`;
  - stages the HUD independently on a 480x320 offscreen WARP device;
  - renders frame 85 from a red atlas block and frame 93 from a green block and
    verifies those pixels by readback;
  - overlaps frames 85 then 93 and verifies green wins, proving the retained
    completed-mash draw order reaches the GPU;
  - renders frame 85 with alpha 128 and verifies the production native-like
    `SRC_ALPHA/INV_SRC_ALPHA` blend result over the existing clear color.
- Exact files changed in PR #10:
  - `src/renderer/d3d11/D3D11Renderer.hpp`;
  - `src/renderer/d3d11/D3D11Renderer.cpp`;
  - `tests/D3D11BackendTests.cpp`.
- PR #10 head `06d03c13efb09d18108adc986a3f41c6399bf62c` validation:
  - Linux run `36009995451`: GCC Release **12/12 passed** and Clang
    ASan+UBSan **12/12 passed**; no sanitizer failure was reported.
  - Windows run `36009995687`: **14/14 passed**;
    `OpenAndroidUSM.D3D11BackendTests` passed the new WARP QTE feedback
    readback checks; the application startup smoke reached the expected
    missing-game-data boundary; XAudio2 reached the classified no-default-
    endpoint `0x80070490` path.
  - MSVC again emitted the pre-existing unrelated C4100 warning for the unused
    `player` parameter in `LevelHostageRuntime.cpp`; this work did not touch
    that file.
- PR #10 was squash-merged as
  `9a3189a80fd8b399f6e8e7f297797892227a9f26`.
- Post-merge validation for that exact squash commit completed green:
  - Linux run `36010512831`: GCC **12/12** and Clang ASan+UBSan **12/12**;
  - Windows run `36010512787`: **14/14 passed** with
    `OpenAndroidUSM.D3D11BackendTests` passing again, plus the same expected
    missing-data startup boundary and classified XAudio2 `0x80070490` result.
- This closes the specific RE03 statement that the already-reconstructed QTE
  feedback geometry had no tested Windows D3D11 integration/compositing path.
  It is **not** an original-game screenshot comparison and does not prove the
  unresolved success explosion or full native QTE UI composition.
- Exact strict continuation remains recovery of the verified original
  `libspiderman.so` / raw `CHostage::Update` instructions for the missing
  hostage camera-mode calls. If that material remains unavailable, continue
  only another directly retained evidence-backed source/validation slice or
  host-platform validation; do not infer missing native behavior.

### 2026-09-24 — fresh Linux rebuild after PR #10

- User explicitly requested a Linux build before continuing further.
- Confirmed `main` at the start of this turn was the documentation-only
  handoff commit `586789ccd6a41c0013b1efd0e6154012ee7d6308`; its source
  parent remains PR #10's validated squash commit
  `9a3189a80fd8b399f6e8e7f297797892227a9f26`.
- Attempted to obtain a scratch checkout for a container-local build, but this
  runtime cannot clone/download the GitHub repository directly because outbound
  GitHub/DNS access is blocked and the repository's workflows publish no source
  or build artifact. Per the project rule, the user's local machine was not
  used.
- Re-ran the actual GitHub-hosted Ubuntu Linux build jobs for the exact current
  source commit `9a3189a80fd8b399f6e8e7f297797892227a9f26` instead. The
  workflow run is `36010512831`, final run attempt **3**.
- Fresh GNU Release build/job `107691489569`:
  - compiler: **GNU C/C++ 13.3.0**;
  - configure: Ninja, `CMAKE_BUILD_TYPE=Release`,
    `USM_BUILD_WINDOWS_APP=OFF`, `USM_BUILD_TESTS=ON`;
  - build completed all **133 Ninja steps** successfully;
  - fail-closed asset-independent CTest selection passed **12/12** in 0.72 s.
- Fresh Clang sanitizer build/job `107692721661`:
  - compiler: **Clang C/C++ 18.1.3**;
  - configure: Debug + `-fsanitize=address,undefined` and
    `-fno-omit-frame-pointer`;
  - `ASAN_OPTIONS=detect_leaks=1`;
  - `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1`;
  - build completed successfully;
  - fail-closed asset-independent CTest selection passed **12/12** in 1.82 s;
  - no sanitizer, UB, or leak failure was reported.
- No gameplay, renderer, host, test, build-rule, or workflow source changed in
  this turn. This was a fresh build/validation checkpoint only.
- A follow-up audit again found that wall/enemy QTE ownership and the remaining
  hostage details are not safe to change from the retained evidence alone.
  `WallWebRuntime` is a separate recovered Player wall-web QTE path, and the
  existing RE04/RE05 documents explicitly leave broader ownership/lifetime
  integration open. No shared-manager behavior was guessed.
- The strict chronological blocker remains recovery of the verified original
  `libspiderman.so` / raw `CHostage::Update` instructions for the missing
  hostage camera-mode calls. If that reference remains unavailable, continue
  only another directly retained evidence-backed source/validation slice or
  host-platform validation.

### 2026-09-24 — gameplay-first priority + Sandman authored-chain correction

- User explicitly corrected project priority: the unresolved hostage camera is
  **polish**, not a gameplay blocker. The active goal is gameplay **1:1**.
  This handoff has been rewritten so future chats do not stop gameplay work on
  the camera evidence gap.
- Re-audited the retained Level 1 state. The existing project evidence already
  closes the uninterrupted normal Level 1 route at **296/296** steps without a
  diagnostic teleport, but that traversal closure is not the same as 1:1 boss
  combat. Sandman remains a meaningful first-level gameplay gap.
- Audited the cold-storage Sandman WIP and found a reconstruction-only rule in
  `LevelEnemyRuntime::updateSandmanBoss`: after resolving an authored special-
  animation successor, the WIP classified the successor as attack/recovery by
  scanning whether its own attack events had positive damage. Direct retained
  native evidence instead gates continuation on `EnemyAttackInfo+0x46` and the
  authored +0x38/+0x6c successor; damage is not part of that decision.
- Changed `src/game/LevelEnemyRuntime.cpp` so:
  - a valid authored successor keeps Sandman's GroundAttack/melee ownership
    active regardless of successor damage, allowing its own special-action and
    sound records to execute;
  - the authored successor chain ends only when no valid next animation remains,
    at which point control returns to the existing post-chain Sandman task
    boundary;
  - the unverified jump interpolation and unfinished sand-hand behavior are
    untouched.
- Extended `tests/EnemySpecialActionTests.cpp` with attack 71, a synthetic
  **zero-damage** attack whose +0x46 continuation flag is true, and verified
  `specialAnimationSuccessor` still follows its authored next clip. This pins
  that native continuation is independent of damage.
- PR #11 head `92650a29ccb17480f6a4786a0719d44c11c02b69` validation:
  - Linux run `36020284531`: GCC Release **12/12 passed**; Clang ASan+UBSan
    **12/12 passed**, including SandmanPhaseTests and EnemySpecialActionTests;
  - Windows run `36020284809`: **14/14 passed**, including Sandman,
    EnemySpecialAction and D3D11 backend tests; startup reached the expected
    missing-data boundary; XAudio2 reached classified `0x80070490`;
  - the pre-existing unrelated C4100 warning in `LevelHostageRuntime.cpp`
    remains visible.
- PR #11 was squash-merged as
  `79e82078ff58dba5787f8fa1a335f183eed27012`.
- Exact post-merge validation completed green:
  - Linux run `36020954445`: GCC **12/12** and Clang ASan+UBSan **12/12**;
  - Windows run `36020954253`: **14/14**, with the same expected startup and
    XAudio2 classifications.
- This is a real gameplay-parity correction, not host/presentation validation.
  It does **not** claim Sandman complete: the full task sequence, native jump
  trajectory, and sand-hand behavior still require direct evidence.
- Next priority: continue gameplay 1:1, preferably Sandman boss behavior where
  evidence permits. If a Sandman sub-path is not evidenced, move to another
  directly evidenced gameplay gap rather than camera polish.

### 2026-09-24 — gameplay-first Sandman melee ownership continuation

- Continued under the user's explicit priority: **gameplay 1:1 first**; hostage
  camera behavior is deferred polish and was not investigated further.
- Found the already-created real-asset regression branch for PR #11's authored
  successor correction. Its smoke expectation now keeps
  `ground_attack1_to_idle` in GroundAttack/melee ownership until that
  zero-damage authored successor completes, then enters the existing recovery /
  jump boundary.
- PR #12 changed only `tests/SmokeTests.cpp` and was squash-merged as
  `eff13b5624dd3f7f8408b3fc50501c39597307fb`.
- PR #12 validation and exact post-merge validation were fully green:
  - Linux PR `36022194081`: **12/12 GCC + 12/12 Clang ASan+UBSan**;
  - Windows PR `36022194375`: **14/14**;
  - Linux post-merge `36023506999`: **12/12 + 12/12**;
  - Windows post-merge `36023507012`: **14/14**.
- Auditing the Sandman task-3 path then exposed a real gameplay-state defect:
  `startSandmanGroundAttack` entered native melee ownership with
  `meleeAttackActive=true` but did not retain the selected
  `EnemyAttackInfo`. Consequently the special-action action-type-2 /
  Spider-Sense path could not consult the same attack record the native
  `CBehaviorMeleeAttack` keeps at behavior+0x90.
- Added `specialAnimationAttackId` in
  `EnemySpecialActionConfig.cpp/.hpp`. It returns the first valid
  action-type-zero authored attack ID for an enemy type + animation and does
  not hard-code Sandman IDs.
- `startSandmanGroundAttack` now:
  - resolves and retains the selected attack for the phase-selected initial
    clip;
  - resets stale melee-sense state/queue;
  - clears the generic melee animation-list cursor so boss-task ownership stays
    distinct;
  - keeps the existing phase-specific clip from
    `CBoss::OnEnterState(3)` at `0x0032cd58`.
- When Sandman's authored successor chain actually terminates, the runtime now
  clears selected attack + sense state together with melee ownership.
- Synthetic `EnemySpecialActionTests` pin typed attack-ID resolution.
  The real-asset smoke additionally pins the shipped phase-zero mapping
  `ground_attack1 -> attack 69`; retained reconstruction evidence records
  attack 69 as **75 damage / 400 cm reach**.
- PR #13 was squash-merged as
  `7bdce2e89bec3b208b189e5e3f2ddcdf80cbb89f`.
- PR #13 validation:
  - Linux `36024382423`: **12/12 GCC + 12/12 Clang ASan+UBSan**;
  - Windows `36024382691`: **14/14**.
- Exact PR #13 post-merge validation:
  - Linux `36024952034`: **12/12 GCC + 12/12 Clang ASan+UBSan**;
  - Windows `36024952048`: **14/14**.
- The next Sandman candidate is **not** to tune the jump. A possible native-
  backed follow-up is task-3 melee timing/turn semantics now that the selected
  attack is retained, but only if Sandman-specific evidence confirms the boss
  task uses the same `CBehaviorMeleeAttack::StateEnter` timing path. Do not
  infer that relationship solely from generic melee behavior.
- A separate audit identified another concrete gameplay-parity debt elsewhere:
  `CAreaDamage` random wait selection currently uses a reconstruction-only
  stable object-ID hash even though retained source comments identify an
  original `random()` call at `0x003029d6`. This is a real gameplay timing
  discrepancy and is a good follow-up **once the exact native float/range
  wrapper semantics are established**; do not replace it with a guessed RNG
  formula.

### 2026-09-24 — Sandman native melee startup timing

- Continued under the gameplay-first mandate; hostage camera work was not
  revisited.
- Re-audited the now-retained Sandman selected attack from PR #13 against the
  generic native `CBehaviorMeleeAttack` path already documented in the
  repository.
- Found another boss-specific reconstruction shortcut:
  `startSandmanGroundAttack` always used animation speed **1.0**, while
  `updateSandmanBoss` forced Sandman to face the live player on every update.
  That bypassed the retained `EnemyAttackInfo+8/+0xc` startup rules and also
  homed authored successor/recovery clips continuously.
- PR #14 changed `src/game/LevelEnemyRuntime.cpp/.hpp` so the phase-selected
  task-3 opening attack:
  - resolves the already-retained selected attack;
  - faces the target once on melee entry;
  - uses `clip.duration / startupMilliseconds` exactly like
    `CBehaviorMeleeAttack::StateEnter` when the loaded startup value is
    positive;
  - applies target turning during the initial startup clip only when the
    loaded `turnTowardTargetDuringStartup` flag is set;
  - increments the boss-local attack-stage marker when an authored successor
    begins so successor/recovery clips are not continuously homed.
- No Sandman startup value or phase attack ID was hard-coded. Phase-specific
  selected attacks remain driven by the shipped special-action + attack config
  tables.
- Updated the real-asset `tests/SmokeTests.cpp` Sandman chain:
  - expected animation speed is derived from the loaded attack and clip;
  - the phase-zero wall-clock boundary is derived from that speed rather than
    the old hard-coded/raw-clip 666+1 ms assumption;
  - startup turning is asserted conditionally from the shipped +0xc/startup
    fields.
- PR #14 head `6802305cb64fdb421b8e6faf041bf965eab5f2d5`:
  - Linux `36026983859`: GCC **12/12**, Clang ASan+UBSan **12/12**;
  - Windows `36026983852`: **14/14**.
- PR #14 was squash-merged as
  `b4a692f7f081a0a626dd8c85148a20290f4cf648`.
- Exact post-merge validation:
  - Linux `36027569749`: GCC **12/12**, Clang ASan+UBSan **12/12**;
  - Windows `36027569748`: **14/14**.
- `docs/RECONSTRUCTION.md` was updated afterward in documentation-only commit
  `9d053890f9a720d4c288d1a9c675f06761a1b7dd`.
- Sandman's unverified airborne interpolation and unfinished sand-hand behavior
  remain explicitly untouched.
- Follow-up gameplay audit notes:
  - every accepted combat hit currently resets the portable Sandman task to
    `None`; retained phase evidence confirms the 66/33 clamp and phase-specific
    next task-3 clip, but this turn did **not** claim enough evidence to change
    the exact native phase/hurt queue handoff;
  - `CAreaDamage` still contains a known reconstruction-only object-ID hash
    for random wait selection despite an original `random()` call retained at
    `0x003029d6`. Do not change it until the exact native numeric/range
    conversion for that call is established.
- Next work should continue directly evidenced gameplay behavior. Do not spend
  the next turn on hostage-camera polish.
