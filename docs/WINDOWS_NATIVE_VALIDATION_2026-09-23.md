# Windows native validation — September 23, 2026

This document records host/platform validation layered on top of the RE06
gameplay reconstruction. It is **not RE07 gameplay reconstruction** and does
not claim new original-game behavior.

Gameplay/reference base: `a772706bfcbf861052d6439bdf52ac9b70a1d6a0`
(RE06).

Final fail-closed validation source before this documentation-only update:
`d280ed37933d7a4101021b93d9ae164962907a65`.

## Windows build now established

GitHub-hosted `windows-2022` runners configure the existing
`windows-msvc-release` preset and build the complete Windows target, including:

- `OpenAndroidUSM.exe`;
- the Direct3D 11 renderer;
- XAudio2 application code;
- XInput application code;
- the portable core and selected Windows/native regression binaries.

The validated toolchain selected Windows SDK 10.0.26100.0 and MSVC
19.44.35228.0. This closes RE06's earlier **uncompiled Windows source** gap.
It does not by itself prove gameplay, GPU parity, audible output, or physical
controller behavior.

The final CI gate is intentionally fail-closed: its CTest selection uses
`--no-tests=error`, so a bad regular expression cannot report success after
running zero tests.

## Asset-independent Windows execution

The Windows gate executes these asset-independent paths:

- QTE clock and attribute rules;
- QTE keypad/reset isolation;
- hostage sound dispatcher behavior;
- XInput button translation, routing, stick normalization, autoplay routing,
  and deterministic transition stress;
- the Windows D3D11 backend;
- the Windows XAudio2 backend when a default audio endpoint exists.

The D3D11 backend test is an actual runtime test, not a compile-only check. It
creates a 64x32 WARP device and offscreen color/depth targets, builds the shader
pipeline, renders an empty frame, reads pixels back from the GPU resource, and
checks the expected uniform clear value.

A separate process smoke starts the built `OpenAndroidUSM.exe` in autoplay
mode. It initializes the real offscreen WARP renderer and shader pipeline,
then reaches the expected game-data lookup failure because original assets are
deliberately absent from the repository and CI environment.

## XAudio2 COM defect found and corrected

The first Windows runtime probe reached
`IXAudio2::CreateMasteringVoice` and failed with HRESULT `0x800401F0`
(`CO_E_NOTINITIALIZED`). This was a real host-backend defect: Windows COM had
not been initialized on the application thread before XAudio2 setup.

`XAudio2System` now initializes COM with `CoInitializeEx(...,
COINIT_MULTITHREADED)`, retains one matching initialization reference, releases
all XAudio2 voices/engine state first, and pairs that successful COM
initialization with `CoUninitialize` at destruction. The application and the
backend test link `ole32` explicitly.

Microsoft's XAudio2 initialization guidance requires COM initialization before
creating the XAudio2 engine:

- https://learn.microsoft.com/windows/win32/xaudio2/how-to--initialize-xaudio2
- https://learn.microsoft.com/windows/win32/com/the-com-library

After that correction, the hosted runner advances to
`CreateMasteringVoice` and returns HRESULT `0x80070490`. Microsoft's
`CreateMasteringVoice` documentation identifies that result as the
no-default-audio-device case when the default device is requested:

- https://learn.microsoft.com/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2-createmasteringvoice

The CI skip is restricted to that exact `0x80070490` result. Any other
mastering-voice failure remains a test failure. On a Windows host that has a
default audio endpoint, `OpenAndroidUSM.AudioBackendTests` proceeds to create
real source voices from synthetic PCM and checks live `isNamedPlaying`,
natural one-shot completion, looping playback, and named stop behavior.

The GitHub runner therefore proves the COM repair and XAudio2 initialization
path, but **does not prove audible playback or the live voice-state assertions**
because it has no default audio endpoint.

## Verification boundary

This validation does **not** add or substitute any original-game behavior.
Original Android assets and `libspiderman.so` remain excluded from GitHub.
Consequently hosted CI cannot currently perform:

- a normal complete first-level playthrough;
- original ARM differential execution;
- audible XAudio2 verification;
- physical XInput verification;
- strict reconstruction of native behavior whose reviewed function body/data
  is not retained in the repository.

In particular, the following RE06 blockers remain open and must not be filled
by guesswork:

- original hostage rescue camera-mode calls;
- full pause/menu and input lifetime;
- remaining hostage and wall/enemy QTE integration;
- persistent Audible emitter behavior;
- the QTE failure-cue suppression mode;
- the QTE success explosion/full native composition;
- the unfinished Sandman jump/sand-hand sequence.

The historical repository confirms that generated Ghidra projects,
decompilation, disassembly, and binary-derived databases were intentionally
local-only. The current GitHub history retains export tooling, fingerprints,
native addresses, and reviewed reports, but not enough function-body evidence
to resolve the open items above strictly.

## CI records

The Windows validation was developed incrementally so failed probes are retained
as evidence rather than rewritten:

- run `35914587640`: full MSVC build, selected input/QTE tests, and real
  D3D11 application startup passed;
- run `35918883727`: preserved the pre-fix XAudio2 HRESULT
  `0x800401F0`;
- run `35919488698`: COM-corrected build passed, D3D11 WARP
  render/readback passed, and the audio probe advanced to the runner's
  no-default-device HRESULT `0x80070490`;
- run `35919999966`: final gate restricts the audio skip to that exact
  no-device result.

These are host/platform validation records. RE06's Linux Release,
sanitizer, clean-patch, reference-fingerprint, and original-material integrity
records remain the gameplay-reconstruction evidence for that checkpoint.
