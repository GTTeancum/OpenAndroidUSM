# OpenAndroidUSM RE03 — Verification

September 22, 2026. **Development continuation; not near-final acceptance testing.**

## What changed

Drag QTEs now accept a fresh left-thumbstick movement in the general requested
direction. **No A hold, virtual touch, path tracing, or touch release is required.**
Diagonal movement is accepted; held carry-in, held sequence input, device loss,
invalid axes and reconnection cannot manufacture a fresh gesture. A retains
its separate tap/mash function. Prompts and diagnostic autoplay use the same
controller-native policy.

The original QTE manager retains its clocks, compound sequencing, sound/control
release, and authored success/failure cinematic handoffs. Direction recognition
is an explicit user-authorized PC adaptation; its 0.55 engage / 0.25 neutral
thresholds and +/-60-degree cone are not falsely presented as Android constants.

Further native reconstruction adds result-sprite output from the original
interface atlas, completed-mash ring frames, captured result position and
**per-draw failure fading**. Frame geometry is shared with the D3D11 integration
and tested independently on the CPU. This does not complete the distinct native
success explosion or verify GPU composition.

## Final runs actually executed

| Build/run | Result | CTest time |
| --- | --- | --- |
| Fresh RE02 extraction, clean code-patch application, fresh GCC 14.2 Linux Release build | **20/20 passed** | 102.70 s |
| Clang 17 Linux, full portable suite with AddressSanitizer, UndefinedBehaviorSanitizer and leak detection | **20/20 passed** | 301.42 s |

The final controller-boundary group passes **789,461 checks**. It includes all
**65,536 raw signed axis values for each of four directions** (262,144 cases),
the actual production deadzone/normalization path, neutral/engage boundaries,
held-input rejection, diagonal angular coverage, compound children, disconnect,
invalid inputs and original deadline ordering. These are supplied raw states,
not measurements from a physical controller or execution of XInputGetState.

The first-level integration fixture loads the actual linked StartQTE command
(cinematic 20004, config 6) and reaches its real **20006 success** and **20010
failure** scheduler paths. Success uses raw diagonal stick input with no A;
wrong-direction plus A cannot pass. Other script commands are observed rather
than executed against a complete rendered world.

The feedback group passes **6,326 checks**: native six-frame failure metadata,
integer alpha decrement, repeated/catch-up snapshots, nine success draws,
completed-mash frames, the special photo result, original UVs and coordinates
at five viewport sizes, and explicit invalid-data rejection. Existing native
core, clock, sprite, gesture, hostage, audio and input regressions also run,
including **100,000 deterministic input-transition frames / 1,200,000 checks**.
No sanitizer errors were reported in the successful final run.

The restored RE02 baseline passed 17/17. An intermediate 20/20 Release run
preceded addition of the final exhaustive raw-axis sweep; it is retained as
an intermediate run, not substituted for the two final runs above. An early
feedback fixture failed because it selected the wrong photo config ID. Original
asset inspection corrected the fixture to config 19 and preserved the native
already-ended animation behavior; failed development logs remain included.

## Reference and source integrity

All **599 original game-data/reference files**, totaling **170,642,144 bytes**,
were freshly compared byte-for-byte against their members in the supplied
17-volume archive and remain unchanged. This is not validation of the roughly
95 GB complete expanded upload. The initial extraction log contains 598 files;
the final comparison also includes the original 325-byte import manifest.

Original ARM library SHA256:

```text
f35d959d54d43d3d4cce07d1afac4cbe52438997a3bf4d5304c50610cabc2679
```

Nine function fingerprint records accompany the interpreted Draw,
SetState, BeginQTE and sprite-output evidence. The reference verifier passes
and rejects a deliberately modified temporary copy. Reference identity and
transcribed-rule tests **do not prove behavioral equivalence to executing ARM**;
the original ARM game was not run here.

The code-only RE03 patch clean-applies to the exact delivered RE02 source ZIP
(SHA256 `4dc42a09e63ed25f8ff1c37ed409eb19999ffb0711373ad29be7a879887178f7`). All **298
code/test/build-rule/agent-rule files** match the clean-build tree byte-for-byte
and were rechecked after the final tests. Additional readme/evidence/verification
files are supplied separately in the overlay. The package payload manifest
records paths, sizes and hashes; an external package check records ZIP CRC
validation and final archive hashes without a self-referential checksum.

## What remains unverified / unfinished

**No newly built Windows executable is included.** Windows application/D3D11
wiring is edited but was not compiled here. Cross-toolchain acquisition failed
because of network/DNS access; the failed attempt is recorded. GPU rendering,
material/compositing parity, audible XAudio2, physical XInput, and normal
first-level gameplay remain unverified. Linux builds are native portable
runtime/test executables, not a graphical Linux port.

The original success explosion, full QTE visual composition, shared QTE/input
ownership across cinematic/hostage/wall/enemy paths, pause integration, remaining
hostage behavior, and uploaded Sandman work are still outstanding. The tests
must not be presented as near-final or full first-level acceptance.

## Preserved files

`OpenAndroidUSM_RE03_Source.zip` is complete cumulative source through RE03.
`OpenAndroidUSM_RE03_Overlay.zip` applies over **RE02**, not the unmodified
original upload. `RE03.patch` is a code-only alternative to replacing the same
source files, not a second step after applying the overlay. Keep the original
supplied game and dependency material. No original assets/libraries, build
caches, bulk decompiler output, or game executable are redistributed.

See `RE03_README.md`, `docs/RE03_EVIDENCE.md` and `RE03_verification/` for the
control policy, interpreted evidence, commands and actual test records.
