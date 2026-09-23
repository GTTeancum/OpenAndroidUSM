# RE06 — Hostage sound dispatch and repeated native control calls

September 22, 2026. Native source continuation from the delivered RE05 archive.
This records interpretations of supplied ARM instructions and shipped data, not
execution of the original game or a claim of complete gameplay equivalence.

## Correction to the earlier cutting-loop interpretation

RE05 and earlier checkpoints called the hostage's sound a cutting **loop** and
latched `cuttingLoopRequested` after publishing a play request. That interpretation
was incomplete. Following the virtual call into its actual implementation shows
that this path requests a **non-looping, spatial one-shot**. The original hostage
queries playback again on every eligible update. RE06 supersedes the earlier loop
claim and removes that latch. Historical reports remain unchanged as records of
what those checkpoints contained; their loop interpretation is not current.

The condition itself was already recovered correctly: during hostage state 2,
with the player still in rescue state 28, the QTE's **remaining action count must
be exactly 7**. It is not `<=7`, not every mash input, not a timer based on estimated
clip duration, and not a permanent loop starting when the QTE begins.

## Reference and virtual-call resolution

Supplied library: `game/original/libspiderman.so`, 7,713,000 bytes, SHA-256
`f35d959d54d43d3d4cce07d1afac4cbe52438997a3bf4d5304c50610cabc2679`.
All addresses here are **ELF addresses with the Thumb bit cleared**. The supplied
Ghidra exports add 0x10000. Function pointers below retain their Thumb bit.

`docs/references/re06-original-fingerprints.json` extends the prior 29 function
fingerprints to 45 and also records the actual hostage Audible virtual slots.
`tools/verify_re06_reference.py` verifies both the whole image/function identities
and slot-to-symbol resolution. It does not execute ARM. Two negative checks reject
an altered temporary image and a manifest deliberately naming the wrong slot.

| Original evidence | Interpretation used by RE06 |
| --- | --- |
| `CHostage::CHostage`, ELF 0x328c20; Audible subobject at hostage +0x114 | The final Audible vptr is ELF 0x4b27b8. This resolves the indirect calls using the actual subclass table, not guessed virtual-method indices. |
| vptr +0x1c, +0x20, +0x24 | Stored pointers are respectively 0x3bbf2d (`PlayAudio(int,bool,bool)`), 0x3bbe51 (`StopAudio(int,int)`), and 0x3bbdd1 (`IsPlaying(int,bool)`). |
| `CHostage::Update`, ELF 0x328068; 0x328444–0x32847e | Repeats `EnableControls(false,true)`; tests remaining-count ==7; calls `IsPlaying(0x18b,true)`; only a false result calls `PlayAudio(0x18b,true,false)`. |
| `Audible::PlayAudio(int,bool,bool)`, ELF 0x3bbf2c | The final bool selects persistent vs transient behavior. On the hostage's false/transient branch, the incoming loop bool is not forwarded. At 0x3bbf6c–0x3bbf84 the function supplies a literal zero loop argument to `VoxSoundManager::Play3D`. A true value stored on the stack here is a different parameter, not the loop flag. |
| `VoxSoundManager::Play3D`, ELF 0x3caf74 | Preserves its incoming loop argument in r5 and passes it in r2 to `PlayEmitter` at 0x3cafa0. The hostage's transient call therefore remains non-looping. |
| `Audible::IsPlaying`, ELF 0x3bbdd0 | A matching configured persistent emitter has a separate branch. Otherwise the fallback reaches `VoxSoundManager::IsPlaying(soundId,sceneId)`. |
| `VoxSoundManager::IsPlaying`, ELF 0x3cac58 | Calls `nativeIsMediaPlaying` with the sound ID. The scene-object ID is not used by this fallback. |
| `Audible::StopAudio(int,int)`, ELF 0x3bbe50; manager `Stop`, ELF 0x3cace4 | The transient fallback stops by sound ID. The Android manager does not forward the supplied scene-object/fade arguments to `nativeStopSound`. Hostage calls use fade zero. |
| Constructor chain: `CHostage` -> `Unit` C2 at 0x315bac -> `CGameObject` C2 at 0x2ffd08 -> `Audible` C2 at 0x3bbc40 | At Audible construction, stored media/emitter IDs are -1 and the loop/persistent flags are zero. The first-hostage authored node has no sound/audio attribute. This is evidence for the scoped transient rescue path, not a universal reconstruction of persistent emitter configuration. |

The original table identifies 0x18b (395) as `SFX_QTE_UNTIE`. The unchanged audio
fixture decodes that supplied clip along with the QTE success/failure/click sounds.
Decoding a clip is not proof that Windows audio sounds or mixes correctly.

## Repeated control calls, not just the QTE manager's outer reset

RE05 recovered the manager's own synchronous control operations. `CHostage::Update`
also issues calls independently, and they were missing from the bound PC object
path. Every same-value call matters because `EnableControls` resets gameplay input.

| Hostage branch | Recovered order now issued by the runtime |
| --- | --- |
| Tied -> rescue-start | Enter player state 27, then disable ordinary controls while preserving pause. |
| Rescue-start, player still 27 | Repeat disable/reset on each update. If the animation finishes, enter state 28 and call BeginQTE; that outer call performs its own additional reset. |
| Interrupted rescue-start | Restore controls, reset hostage. Preserve the original independent animation-finished check rather than turning it into an `else if`. |
| Active QTE, player still 28 | Repeat disable/reset before the remaining-count/playback check. |
| QTE success | Stop untie sound before entering player state 29. Do not insert an extra hostage control-restore call in this branch. |
| QTE failure | Stop untie sound, request idle, restore controls, reset hostage. |
| Interrupted active QTE | ForceFailQTE, restore controls, stop untie sound, reset hostage without replacing the interrupting player state. |
| Rescue-end | Repeated stop on each update. An interrupted player restores controls/reset; the independent animation-finished branch requests idle and restores controls before release progression. |

The original also calls `CGameCamera::SetMode` on several of these branches.
**Those camera-mode calls are not reconstructed by RE06.** Their omission is a
remaining parity gap, not an implied completion of the whole native call sequence.

## Production changes and PC boundaries

`HostageSound.hpp/.cpp` introduces a portable, synchronous sound-command dispatcher.
`PlayOnceIfStopped` queries the supplied backend at the exact object-update point,
then issues a one-shot only when not playing. A failed or distance-culled request
is not latched as playback. A completed sound can be retriggered while count==7;
at other counts there is no new play query. The native explicit stop branches
remain, including repeated rescue-end stops.

`HostageUpdateHooks` is call-scoped, not stored in checkpoint copies. It forwards
the direct hostage control calls to the same `LevelCinematicRuntime::enableControls`
used by RE05. Empty hooks deliberately support detached source fixtures: they
publish conditional commands rather than pretending a sound is playing. They do
not imply a fully bound application.

`Application.cpp` binds these hooks to its real host audio/control services and
removes its old post-object looping-sound drain. Prior QTE audio cues are drained
before dispatching the next hostage sound, preserving the observed failure-cue
before untie-stop order. This does **not** establish sample-accurate timing for all
native sound/control calls; the manager still has its existing audio cue bridge.

`XAudio2System::isNamedPlaying` checks matching active voices, callback completion,
and current `BuffersQueued` using `IXAudio2SourceVoice::GetState`. It does not use
the gameplay clock or a request flag. This method and the Windows application
binding were edited/reviewed but **not compiled or executed in this checkpoint**.
The portable fixture injects playback/culling/failure status; it is not a fake
Windows-header build or an audible test of XAudio2.

Host-API documentation, separate from the original-game evidence:
- Microsoft, `IXAudio2SourceVoice::GetState`: https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2sourcevoice-getstate
- Microsoft, `XAUDIO2_VOICE_STATE`: https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/ns-xaudio2-xaudio2_voice_state

Muted diagnostic autoplay reports no active host voice and can therefore log
another eligible attempt. It does not synthesize playback duration. Missing
required sound bindings and backend errors return explicit port errors, not
invented successful playback or Android error-handling behavior. Persistent or
attached Audible emitters outside this rescue path remain outside this module.

The RE03 XInput adaptation is unchanged: fresh left-stick direction for drag
QTEs (diagonals accepted), no A hold/touch tracing, and separate A tap/mash.
Original QTE timers, counters, result handling and authored outcomes are not tuned.

## Test scope

The three new Release-active groups are `dispatch`, `recovery`, and `controls`.
They check query-before-play, live suppression, natural completion, culling,
backend failures, shared sound-ID scope, explicit stops, checkpoint state copying,
count-7 eligibility, idle-decay re-entry, repeated control resets, activation-frame
A survival, success/failure/interruption, and composed-entry hook forwarding.

The bound first-hostage fixture uses object 30018 in the original room-2 scene,
original player/config/animation data, and the shared QTE/control runtime. It
advances animations in controlled steps to verify release/reward/thank/idle
transitions and absence of duplicate reward/thank dispatch. This is **not** a
rendered playthrough, original-ARM differential execution, physical gamepad
capture, or evidence that the final game is ready for acceptance testing.

See `RE06_VERIFICATION.md` and `RE06_verification/` for completed runs, source
identity, the final original-material comparison, and packaging results.
