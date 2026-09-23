# RE03 — Controller-native drag QTEs and recovered result feedback

September 22, 2026. This continues the delivered RE02 source. Original gameplay
behavior is taken from the supplied ARM library and assets; **controller intent
recognition is an explicit PC adaptation requested by the user**, not a recovered
Android feature. No original ARM code is executed by this native C++ runtime.

## Controller policy, distinct from recovered gameplay

A drag is completed by a fresh left-stick movement in the general authored
direction. A is not required, held, released, or synthesized. No virtual touch
coordinates, sampled path traversal, mouse movement, or touch-up are produced.
Tap and mash inputs retain their separate A/Cross route.

`QteGesturePath::direction()` reads the direction from the original path's
start/end coordinates; no new per-level hardcoded direction table is introduced.
`QteGamepadAdapter` emits semantic `QteInput::dragDirection` only for a matching
movement. The manager's recovered state machine still owns compound sequencing,
timeouts, feedback/audio, control release, and cinematic requests.

The explicit PC tuning is an engage magnitude of 0.55 and neutral/rearm magnitude
of 0.25 **after the existing production radial deadzone**, with a +/-60-degree
acceptance cone. Diagonals therefore work. These three values are host-input
choices, not ARM constants. Wrong-direction input is ignored while the original
deadline continues; correcting the direction does not require a button or a
traced path. One accepted movement cannot clear multiple sequence children.
A held stick at QTE entry or reconnection must first return to neutral. A fresh
movement on entry can count when the preceding observed sample was neutral.
Disconnect/invalid axis data cannot manufacture that neutral observation.

Application prompts now say `Move left stick LEFT/RIGHT/UP/DOWN`. A presses are
suppressed in drag input, including invalid-axis cases, so they cannot fall
through to a compound tap child. The ordinary player movement gate already
blocks movement while this cinematic QTE is active; it was not replaced.
Autoplay uses the same production directional adapter, with a neutral sample
between gestures, instead of supplying a sequence of touch coordinates.

## Preserved original ordering

RE02's recovered clocks, strict timeout boundary, float32 behavior, and compound
manager remain. In native drag state 1, timeout is checked **before**
`UpdateDragState`, which may still complete on that update. The controller intent
is consumed in the same slot. This retains the native late-update failure-then-
success sound/state sequence rather than imposing a newly designed cutoff. A
missing/wrong gesture or disconnection does not extend the deadline. Normal
success still hands off on the ninth Draw, not after an invented delay.

The actual linked first-level fixture remains source cinematic **20004**,
StartQTE at **2000 ms**, config **6**, success **20006**, failure **20010**.
Config 6 is a downward drag at (360,101), ending at (360,251), with duration
3900 ms. It is not a tap. The integrated fixture supplies raw XInput state to
the production translator, Xperia router and new adapter: no A for success;
wrong-direction stick plus A for failure. Both authored scheduler handoffs are
checked. Other commands are observed, not a fully rendered world simulation.

The original coordinate-based path/release API remains for reverse-engineering
regression tests. It is no longer used for live gamepad drags or automatic drags.

## Further reconstruction: result sprites and failure fading

Original reference is `game/original/libspiderman.so`, 7,713,000 bytes, SHA256
`f35d959d54d43d3d4cce07d1afac4cbe52438997a3bf4d5304c50610cabc2679`.
The following are **ELF instruction addresses**, not the rebased Ghidra address
(which is ELF + 0x10000):

| Original function / range | Evidence applied |
| --- | --- |
| `CQTEManager::BeginQTE`, 0x0037ab40; reset at 0x0037ab5e | Reset signed failure alpha to 255 on an accepted new QTE, not each compound child. |
| `CQTEManager::BeginNowQTE`, 0x0037aa08 | Capture the authored/randomized origin for the prompt/ring position. |
| `CQTEManager::SetState`, 0x0037a67c | Capture result sprite position when entering result state, distinct from subsequent control-coordinate snapping. |
| `CQTEManager::Draw`, 0x0037a7c8; 0x0037a8ba–0x0037a8e4 | Failure decrement is integer `255 / GetAnimDuration(1)`, clamped at zero, **before** painting, once per Draw. |
| `CQTEManager::Draw`, 0x0037a896–0x0037a8a6 | An already-ended failure animation dispatches failure without painting/fading. |
| `CQTEManager::Draw`, 0x0037a7e4–0x0037a836 | Paint success result first; for a completed mash paint interface frames 85 and 93, in that order. |
| `CSprite::PaintAFrame`, 0x002d99f4 | Apply animation-frame offsets at unit instance scale, then animation flags, before frame-module drawing. |
| `CSpriteInstance::PaintAnimOnScreen`, 0x002d9e0c; `PaintAnim`, 0x002d9e98 | Use retained instance position/animation for result output. |

Nine function identities/byte fingerprints are retained in
`docs/references/re03-original-fingerprints.json`. Verify with:

```sh
python tools/verify_re02_reference.py --manifest docs/references/re03-original-fingerprints.json
```

The original `interface.bsprite` animation 1 has six frames (34 through 39),
durations `[1,1,2,2,2,2]`, total 10. Therefore the fade step is **25**, not 25.5:
painted alpha is 230, 205, ... 5, 0. Animation time and draw count remain separate.
Animation 7 has total duration 8; its retained success behavior is unchanged.
Photo config **19** has interaction value 16 and animation 29, a single frame
of duration 1. The recovered ended-at-tick-zero rule means its failure dispatches
without a made-up visible fade. It is tested as an asset-level special case,
not claimed as encountered in first-level normal play.

`feedbackFrame()` is a side-effect-free snapshot of the upcoming draw, including
the upcoming alpha decrement. Only `drawStep()` commits that decrement and
outcome dispatch. Preparing a frame during simulation catch-up cannot accelerate
fading or lose the ninth success presentation. Result animation coordinates
are not truncated back to 16 bits after adding their frame offsets.

`QteFeedbackGeometry` builds textured geometry from the actual interface atlas,
with independent tests of XY, UV, alpha, draw order, and five PC viewport sizes.
This host presentation uses the existing aspect-preserving 480x320 HUD mapping.
Unsupported transforms/invalid bounds fail explicitly rather than guessing.
The current result frames and completed-mash ring use supported zero transforms.
The D3D11 path consumes this same portable geometry and existing interface texture.

**Limits:** This does not complete original QTE visual composition. The success
explosion sprite uses its own scaled/material-flagged draw and is deliberately
not approximated; the active gesture UI remains the authorized controller text
and inherited timer bar. D3D11 integration and its compositing/blending have not
been compiled or visually verified in this Linux environment. Tested geometry
is not equivalent to a tested Windows screenshot.

## Verification scope and remaining work

See `RE03_VERIFICATION.md` and the saved logs for the actual final run results.
The tests are source/asset/transcribed-rule tests, **not differential execution
against the ARM program**. No normal title-to-first-level controller playthrough,
physical XInput, speaker audio, Windows executable, or graphical Linux game
has been verified here. Shared cinematic/hostage/wall/enemy QTE ownership, pause
integration, remaining hostage behavior, success explosion rendering, broader
visual composition, and the uploaded Sandman work remain outstanding.

The first exploratory feedback test used the wrong fixture ID for the photo
QTE; the asset census identified config 19 (not 13), and the corrected test also
checks the native ended-at-tick-zero behavior. The failed development logs remain
preserved rather than relabeled as passes.
