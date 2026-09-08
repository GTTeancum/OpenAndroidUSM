# Level 1 first-encounter combat parity audit

This gate covers the first controllable fight in chronological player flow.
Passing it does not claim that later combat encounters are complete.

On a new Level 1 profile, this fight occurs before either special skill is
available. `Player::CanEnableUltimate` (`0x00345e98`) checks profile bit 0 and
`Player::CanEnableSpiderSense` (`0x00341c98`) checks profile bit 1 while
`CLevel+0x44` is zero (Level 1). The shipped commands set those bits later:
cinematic 974 unlocks Spider-Sense at 4000 ms and cinematic 969 unlocks
Ultimate at 0 ms. Normal gameplay now honors both gates. Isolated component
scenarios replay `CCinematicThread::OnUnlock` (`0x0037240c`) through the
autoplay harness without consuming a gameplay tick; the opening-flow lock
scenario deliberately does not.

## Web-power economy

The shared white HUD bar is live combat state rather than decoration.
`Player::Player` (`0x0034e4d4`) initializes current power at `Player+0x6f4`
and maximum power at `Player+0x700`; `CLevel::InitAfterRoomInit`
(`0x00382bdc`) fills the new Level 1 player to the shipped 1000-point maximum.
`Player::SetNextStateId` (`0x0034b84e`--`0x0034b86c`) asks
`Player::GetSpellMagic` (`0x00345c48`) for every entered state and deducts the
result through `Player::AddWebPower` (`0x00345d74`). At the default difficulty
and upgrade level, the reachable red-suit costs are 40 for a web pellet, 80
for web-combo/web-zip states, 160 for strike-land, 70 for a Spider-Sense evade,
200 total for a Spider-Sense counter or blink strike, and the full 1000 for
Ultimate.

`Player::CheckCanDoAction` (`0x00345e20`) rejects a requested transition when
the current meter cannot afford it. `Player::CanEnableUltimate`
(`0x00345e98`) requires the full meter but does allow Ultimate to replace an
ordinary grounded attack immediately. `Player::DoNormalSenseAction`
(`0x0034f7ee`--`0x0034f8b4`) charges the extra 130 points that distinguishes a
counter from the common 70-point class-six entry cost.

Passive recovery is also native state. `Player::PreUpdate` (`0x0034dfd8`)
calls `Player::UpdatePowerRestore` (`0x00345f3c`) before the state update and
adds 20 points per second for a new default profile. Positive restoration is
blocked while the active state itself has a spell cost and during Ultimate
states 107--113 or state 116. Successful hits use the actual target-health
loss in `Player::SendHitMessage` (`0x003460de`--`0x00346132`): their recovery
factor is 0.8 at or below 250 power and falls linearly to 0.25 at full power.
That permission is captured at the impact frame because native
`SendHitMessage` calls `NoPowerRestoreState` synchronously, before the state
runner can cross a linked-animation boundary; deferred application dispatch
must not accidentally test the following state instead.
The renderer now receives the measured current/maximum ratio, and autoplay
records both a `web_power` frame column and every meter transition. The
combined effects gate proves the 40-point pellet cost, native recovery to
1000, and only then the full-meter Ultimate transition.

## Enemy health and player damage

Room 1 objects 394, 395, and 397 each serialize `Health=500.000000` in the
shipped scene. `Player::SendHitMessage` (`0x00345fe8`) multiplies attack damage
by `GetHardLevelRate(0)` and `GetUpgradeRate(0)`. A new profile selects
difficulty 1 and attack upgrade 0. The native constant tables give both of
those selections a factor of 1.0, so this encounter has no hidden damage
reduction or reconstruction-added health multiplier.

The default ground chain is:

| State | Registered impacts | Damage per impact | State total |
| --- | ---: | ---: | ---: |
| 74, right punch | 1 | 35 | 35 |
| 75, left punch | 1 | 35 | 35 |
| 79, right kick | 1 | 55 | 55 |
| 90, fast kick | 4 | 21.25 | 85 |
| 91, fast kick 2 | 4 | 21.25 | 85 |
| 78, double kick | 2 authored; both contact a grounded type-0 thug | 40 | 80 delivered |

The first complete six-press chain leaves a grounded thug at 125 health. The
next right punch, left punch, and right kick remove that remainder:
`500 -> 465 -> 430 -> 375 -> 290 -> 205 -> 125 -> 90 -> 55 -> 0`.
This is not a tuned exception. `Player::ResetObject` (`0x0034e258`) loads the
185 cm player height from image address `0x0056ec94`, and
`createEnemyPhysics` (`0x003d8980`) gives this enemy a 40 cm half-height.
`createEnemyPhysics` also stores local center `{0,0,radius}` at shape offset
`+0x08`; `Physics::testPieCollision` (`0x003d5434`, center load at
`0x003d5462`) transforms that point through `PhysicsEntity::localToWorld`
(`0x003ce64c`) before applying the vertical extents at each animated `Bip01`
attack origin. Both state-78 origins therefore overlap the grounded cylinder.
The retained autoplay scenario
`first-encounter-health-damage-audit.usmauto` enters the authored encounter,
uses production input/targeting/hit dispatch, asserts all three starting
health values, and pins every value in this ledger. It also verifies that
post-death attack contacts cannot create additional damage, impact audio, or
target contact effects.

The serialized health and damage numbers are correct, but the “too many hits”
symptom exposed a separate gameplay failure. The reconstruction omitted
`Player::SendHitMessage`'s per-dispatch `0.85` vertical-force scale and its
airborne collision path could move a thug through the shopping-center shell.
There were two concrete errors: an overhead surface could be selected as
floor, and the storefront wall was approximated from its longest XY edge
instead of its authored triangle plane. Ground-recovery root motion also
bypassed collision. Those errors could leave a living thug below the street
and make it impossible to hit. Root displacement now writes through the
collision solver as the native physics body does, and the production-input
normal-flow gate verifies all three enemies stay reachable, are defeated,
and allow the encounter to advance.

Web and aerial continuations have separate source-authored ledgers; serialized
damage fields are not assumed to be delivered hits:

| Input path | Delivered damage | Fresh-thug result |
| --- | --- | ---: |
| Ground pellet, bind, and release (`58 -> 60 -> 62`) | pellet 20; state 62 entry hit 0 | 480 |
| Ground pellet, bind, and directional throw (`58 -> 60 -> 64`) | 20 + 150 | 330 |
| Ground pellet into back/540 throw (`58 -> 60 -> 65 -> 66 -> 67`) | 20 + 100 + 220 | 160 |
| Punch into web throw (`74 -> 92 -> 93 -> 94 -> 66 -> 67`) | 35 + 35 + 150 + 220 | 60 |
| Punch, jump-release launcher, then ground drag-down (`74 -> 97`, then `63`) | 35 + 60 + 70 | 335 |
| Punch, jump-release launcher, then airborne target kick (`74 -> 97`, then `86`) | 35 + 60 + 50 | 355 |
| Punch, jump-release launcher, then air web grab (`74 -> 97`, then `101 -> 102`) | 35 + 60 + 0 + 0 | 405 |
| Air knockdown into 720 web throw (`99 -> 100 -> 103`) | state 99 misses grounded cylinder; 280 | 220 |
| Air knockdown into kick-down/heavy kick (`99 -> 100 -> 104 -> 105`) | state 99 misses grounded cylinder; 100 + 80 | 320 |

`Player::IsInWebBinding` (`0x00340f60`) and `UpdateAttackParam`
(`0x00340fa0`) prove that binding motions do not use the generic hit-frame
path. In particular, state 62's serialized 20 is not a second pellet hit.
`Player::SetNextStateId` (`0x003491d0`) sends its entry hit with the damage of
the state being left, which is zero in the normal `60 -> 62` route.
`Player::UpdateAttacks` (`0x00351204`) delivers motion 127 at its attach and
primary-finish events, motion 129 at primary finish, and motion 130 at its
sound-trigger frame. The retained target receives those hits; a nearer
bystander cannot steal them.

State 64 is reached by native virtual movement event 0, produced by a fresh
WASD/left-stick direction while state 60 is active. State 63 is selected only
when the retained target is more than 160 cm above the player. States 101 and
102 retain and render their target web line but intentionally deal zero
damage. State 99 serializes a 100-damage sector, but its animated `Bip01`
origin remains above the first-encounter grounded cylinder and therefore does
not deliver that damage; the later state-103 or state-104 contact does. These
are separate authored outcomes, not missing generic hit events.

## Enemy offense and player hurt reactions

The first-room knife uses attack 6 (`ATTACK_HIT_LIGHT`, native hit type 100):
two 25-point contacts at 45 and 75 percent of its 1667 ms clip. The bat's
state-11 table contains two equally ranked attacks. Native
`CBehaviorMeleeAttack::StateEnter` resolves that exact tie with a 50-percent
random branch: attack 7 is a 35-point standing contact at 47 percent of its
1934 ms clip, while attack 11 is a 50-point jumping contact at 70 percent of
its 1500 ms clip. The runtime now uses the shipped Irrlicht generator and the
native `random(0,100) <= 49` tie branch rather than alternating the winner.
Focused core and normal-flow autoplay coverage observe both authored outcomes.

`CBehaviorMeleeAttack::UpdateAttackMelee` (`0x003ba244`) retains its active
attack substate until `UpdateAttackMelee_DoAttack` (`0x003b9e44`) observes the
animation task ending. It does not return to chase merely because an earlier
contact moved Spider-Man outside the distance used to enter the attack. The
portable runtime now preserves that ownership. This is essential for knife
attack 6: its first 25-point contact can create separation, but its second
25-point contact at 75 percent remains scheduled by the still-active clip.

The original AI manager permits one ordinary melee engager on difficulty one
(`0x003744a4`). On unregister, `0x00375560` samples a half-open 1000--1999 ms
global gate; `0x00375654` blocks the final free slot while that float remains
positive; and registration at `0x00375710` consumes a 5000--14999 ms entry
lease. The reconstruction now retains those calls and the exact
`irr::os::Randomizer::rand` recurrence at `0x0043ae48`. The former fixed
1000 ms delay and last-attacker round-robin behavior were reconstruction-only
and have been removed.

The same application-owned stream now covers the other random consumers in
this chronological combat slice. `Player::DoNormalSenseAction`
(`0x0034f868`--`0x0034f87e`) samples `random(100)` and gives its first evade
variant values 0--50 inclusive. Enemy animation-list mode 2, behavior-state
voices, and special-action voices sample their exact vector counts at
`0x003a8648`--`0x003a866a`, `0x003a8a60`--`0x003a8b50`, and
`0x003a8d84`--`0x003a8da0`. Player type-0 sound ranges use the same generator
inside `VoxSoundManager::Play2DRandom`/`Play3DRandom` at `0x003dada0` and
`0x003dafa8`. Deterministic alternation in those paths was not native and is
removed; core tests pin the resulting variants and generator states.

`Player::OnHit` (`0x0034d790`) maps hit type 100 to state 44
(`k_state_hurt_light`) and hit type 101 to state 45
(`k_state_hurt_heavy`). The corresponding shipped root tracks move about
16.84 cm and 200.43 cm backward respectively. Both state selection and root
motion are now retained; forcing bat contacts through the light reaction was
a reconstruction bug that made the exchange timing and spacing incorrect.

The shipped attack rows for IDs 6, 7, and 11 all contain zero horizontal
force, zero vertical force, and zero post-hit protection time. Their special
animation records also have empty effect names. Native
`IBehaviorBase::NotifyEntityAttack` (`0x003a8100`) sends a null hit-effect
pointer for these ordinary attacks, so the player should not receive an
invented generic impact mesh. The authored presentation is the thug swoosh,
the light/heavy player hurt animation, and state-enter hurt audio. Action type
2 registers Spider-Sense at 2 percent of each attack clip, after the attack
volume confirms the player is in range; merely entering the wind-up no longer
makes a premature counter available.

Accepted Spider-Sense does not make the player generically invulnerable.
`Player::GetSenseReactState` (`0x00340518`) reports 5 for class-six motions
below 505 and 6 for the counter/blink family. At each melee attack action,
`CBehaviorMeleeAttack::onMessage` (`0x003ba670`) checks that value and skips
its own `CheckAttack`/damage dispatch when the result is nonzero. The portable
runtime now suppresses the opening knife/bat contact at that enemy-behavior
boundary. `Player::IsCanBeHit` (`0x003413dc`) contains no class-six immunity,
so independent damage sources remain capable of reaching `Player::OnHit`.

When LB is accepted, `Player::UpdateSpiderSense` calls
`CTargetHelper::popAttack` (`0x0034fc40`--`0x0034fc48`) before entering the
response. `CTargetHelper::popAttack` (`0x00353d98`) copies the selected
`AISenseInfo` and removes that attacker from the pending list. The portable
warning now has the same one-shot lifetime: the knife animation may continue,
but another LB edge cannot consume the same warning or replace the response
already playing.

The warning itself is also native gameplay state, not merely the tutorial
textbox. `Player::SpawnPlayer` (`0x003455fe`--`0x0034564a`) creates a second
`Hint`, assigns `hintbb.bsprite` animation 0, links it directly to
`Bip01_Head`, and places it 50 cm above that bone. `Player::UpdateSpiderSense`
(`0x0034fba0`--`0x0034fbea`) shows this cue at size 50 only while state 34
passes `CheckCanDoAction` and a target-helper warning can be consumed;
`ClearSpiderSense` hides it otherwise. The runtime cue is distinct from
cinematic 974's authored tutorial Hint, so a cinematic `SetVisible` command
cannot erase a live attack warning. Core, D3D11, and autoplay coverage prove
its independent visibility and same-frame dismissal after an accepted LB
edge.

The accepted action also starts the missing native full-screen response.
`Player::DoNormalSenseAction` (`0x0034f650`) calls
`CLevel::StartInterfaceEffect(160, 0, -1)`. That function selects
`interface.bsprite` frame 13 and divides the starting alpha by
`ALPHA_HIT_TIME`; the executable constant at `0x0056ec64` is 800.0 ms.
`CLevel::Render2DInterface` (`0x00387a54`) paints the authored sprite before
the normal HUD, then fades it through `UpdateInterfaceEffect` (`0x0037d810`).
The same frame, alpha, ordering, and 800 ms fade are now rendered and emitted
to the autoplay event log.

## Contact, trail, and audio timing

- `GS_Loading::Update` selects `Application::SetTargetFPS(20)` at
  `0x002c05ce`. `Application::SetTargetFPS` (`0x003de618`) stores a 50 ms
  target interval. `Application::Update` (`0x003e1624`) compares consecutive
  real-time interval buckets, performs at most two **separate** fixed updates,
  draws once after them, and records the current bucket so older backlog is
  dropped. Interactive play now uses that same scheduler. It no longer runs
  combat at the display's 60 Hz cadence or combines a two-tick catch-up into
  one 100 ms state update, either of which changes input-edge lifetime,
  animation-frame crossings, and effect/contact ordering.
- `CKeyPad::keyPressed`/`update`/`wasKeyPressed`
  (`0x002f96e8`/`0x002f9434`/`0x002f964c`) expose a new physical press as
  keypad states 1 and 2 on two consecutive gameplay updates; state 3 is no
  longer considered pressed. `keyReleased`/`wasKeyReleased`
  (`0x002f9714`/`0x002f9664`) expose release for one update. The Windows
  input router now preserves that exact lifetime. `CLevel::Update`
  (`0x003820bc`) calls the level's `GameEventKeyWrap::update` virtual once at
  `0x00382242`, then calls each player object's update virtual once at
  `0x0038237c`; consequently both keypad states reach two distinct player
  updates rather than two input samples inside one gameplay tick. The former
  one-update edge shortened every real controller/keyboard combo opportunity
  relative to the original even though scripted autoplay taps still passed.
- `CKeyPadCustomer::isKeyDown(key, 2)` (`0x002f9788`) accepts only keypad
  states greater than 2. `Player::UpdateKeyTrigger` (`0x0034d0a4`) uses that
  call for predicate 103, so a new Cross/A press during a punch is not the
  held uppercut branch: states 1 and 2 remain press edges, and the held branch
  begins on the third update. Jump-start's state-level frame-2 input gate is
  now applied to punch, web, and second-jump requests as well as class-four
  attacks. The former path accepted all four actions too early.
- `Player::UpdateKeyTrigger` (`0x0034d0a4`) walks the complete serialized
  transition array and never stops at its first match. Each qualifying row
  replaces `Player+0x4d8`, so the final matching row wins. This is not a
  fixed face-button priority: idle state 0 selects Web over a simultaneous
  near Punch and Jump, but its later predicate-105 far-Punch row overrides
  Web; jump state 13 puts second Jump after Punch and Web; attack state 74
  puts Web after held Jump and Punch. The application now arbitrates a
  simultaneous input snapshot against that current authored row order before
  dispatch, and same-button transition lookup likewise retains the final
  match. State-38--41 counter continuations therefore recover their trailing
  far-Punch transition to state 87 instead of always taking state 74.
- `Player::UpdateAttacks` (`0x00351204`) returns immediately after
  `SetNextStateId` at `0x00353352`, and also after
  `SwitchToNextLinkAnim`. A combo state or recovery clip entered on the
  current tick therefore starts at animation time zero; unused time from the
  completed predecessor is not applied to it. The portable attack runner now
  preserves this tick boundary, including under deliberately oversized test
  deltas.
- The frame-zero rule is not attack-only. `Player::PreUpdate`
  (`0x0034dfd8`) advances `CGameObject` and calls `UpdateStateFrame` before
  `UpdateTriggers` invokes `UpdateKeyTrigger`; `Player::UpdateState`
  (`0x00353368`) runs afterward. A jump, wall action, or web-traversal state
  selected by that input can execute its state handler, but its newly selected
  animation has not received the current 50 ms advance. The portable player
  now defers locomotion animation and root displacement on that entry tick as
  well. This restores the first legal close-range air-Web sample instead of
  moving Spider-Man one tick beyond the native branch threshold.
- `CLevel::Update` (`0x003820bc`) updates Player before EffectManager.
  `Player::AddHitEffect` (`0x00348dc4`) therefore captures the current animated
  bone pose, then `EffectManager::Update`/`CAnimObjEffect::Update`
  (`0x00391b7c`/`0x00390a88`) age and drift the newly created effect by the full
  current tick. The portable effect clock now preserves that ordering instead
  of back-dating both the captured pose and age to the authored event
  threshold. As in the native pool, an effect that reaches zero during the
  update remains allocated through that render and is reclaimed on the next
  manager update.
- `Player::CheckFrame` (`0x003403fc`) converts authored frames through integer
  `(frame * 2) / 3`. `FrameFixedTimelineController::getCurrentClipFrame`
  (`0x003906c8`) rounds the 50 ms runtime frame with `time / 50 + 0.5`, so
  state 74's authored hit frame 7 first becomes runtime frame 4 at 175 ms.
- `Player::UpdateNormalEffect` (`0x00348f24`) scans every hit marker crossed
  by the current update, but a multi-hit state stores only the latest crossed
  marker and emits one corresponding effect. A single-hit state emits its
  complete auxiliary effect set once. The portable catch-up path now follows
  that distinction, so a slow frame cannot stack several delayed fast-kick
  trails into the same rendered frame.
- Both ordinary-trail call sites load `r3 = 1` immediately before
  `Player::AddHitEffect` (`0x00348fae`/`0x00348fec`, calls at
  `0x00348fb2`/`0x00348ff0`). The `0x00348f00` wrapper preserves that value,
  `Player::AddHitEffect` at `0x00348dc4` forwards it as the final
  `EffectManager::ThrowAnimEffect` argument, and `CAnimObjEffect::Init`
  (`0x00390bb8`) consequently assigns material type `0x1d`. The OpenGLES
  driver installs 27 built-ins at `0x00443d70`, and `Application::Init`
  (`0x003e1c78`) registers `ADDITIVE_MODULATE_NONTRANSPARENT` third, so
  ordinary punch/kick trails use its `GL_MODULATE` plus
  `GL_SRC_ALPHA,GL_ONE` renderer at `0x00396f68`. They do not use an ambient
  add/subtract combiner.
- `CAnimObjEffect::Init` (`0x00390bb8`) gives snapshot trails the player's
  orientation plus the authored bone position; it does not retain the bone's
  rotation. Live attachments retain the complete bone transform.
- `CEnemy::ProcessHitInfo` (`0x00330fe4`) creates
  `cartoon_hit_splash` for ordinary accepted hits and the big variant for
  heavy/special types. `Unit::AddPlayerHitEffect` (`0x00324234`) attaches this
  target feedback to `Bip01_Spine1`. Native code creates it before health is
  subtracted and before hurt behavior replaces the enemy animation. The
  runtime now carries that pre-reaction bone sample with the hit result;
  deferred application dispatch no longer samples the newly entered hurt
  pose and shifts the splash away from the actual contact.
- `CBullet::CheckCollisions` (`0x0035cba0`) does not reuse that cartoon
  splash for the ground web pellet. Its post-message path constructs the
  literal `web_splash` at `0x0035cae8`, attaches it to the struck Unit, and
  awards combo from the measured health delta. The portable pellet now
  carries the pre-reaction spine sample into application dispatch and emits
  exactly that preset. Its later zero-damage retained bind message remains a
  contact but creates no second effect, matching `CEnemy::ProcessHitInfo`'s
  incoming-damage gate.
- `CBehaviorHurt::BehaviorStart` (`0x003b8890`) rewrites hit type 105 to the
  ordinary type-100 reaction while its victim is still grounded, before hit
  force launches the body. Its state update (`0x003b89f8`) preserves the
  complete 49--70 graph, including wall-contact priority for states 54/62,
  the shared state-55 landing transition from 56/58, state 64's facing
  reversal, state 65's externally owned lifetime, and state 70's direct
  recovery. `StateEnter` (`0x003b8640`) emits `smoke_splash` on the three
  authored ground/wall impact states 55, 56, and 63.
- `Player::PlaySound`/`UpdateSound` (`0x0034905c`/`0x003429d4`) retain combat
  sounds with positive emitter frames. The first punch swoosh uses emitter
  frame 2 and first crosses its rounded runtime frame at 25 ms. Its impact
  range plays only when the 175 ms damage is accepted. Multi-emitter
  configurations pair frame and Vox entries by index.
- `Player::CleanSound`/`StopSound` (`0x00341f38`/`0x00341e10`) clean retained
  state sounds during transitions, including the ultimate-wheel loop.
- Ground Web pressed during state 74 retains the acquired Unit at
  `Player+0x594` through states 92, 93, and 95. State 95 motion `0x6f` checks
  and hits that same Unit; it is not a free sector attack that another thug
  can steal. Its accepted contact deals 80 damage, launches with the authored
  400/700 force pair, spawns effect 16 (`fx_in_air_diagonal_kick`) on frame 2,
  and dispatches the state-95 kick-impact configuration.
- The alternate aerial branch retains the live knocked-back target through
  states 84 and 85. The fly kick and linked headbutt each deal 120 damage and
  pair their accepted contacts with the authored kick-impact audio. Enemy
  dummy/pelvis displacement is loaded and applied to the collision body as
  well as the rendered skeleton, preventing a visible launched victim from
  separating from its gameplay target.
- Motions `0x6c`, `0x6d`, and `0x6f` copy their explicit velocity to the
  native character controller and leave the state immediately when
  `Unit::IsFalling` becomes false. The portable capsule now sweeps the road
  support plane and performs that same early handoff. This prevents state 104
  from carrying Spider-Man below the street and restores the buffered
  state-105 heavy kick, its 80 damage, effect 20, and critical-hit cue.
- Airborne target state 86 pursues `Bip01_Head` at the native 1400 cm/s,
  retries its contact probe until the accepted-contact latch is set, deals 50
  exactly once, and retains effect 26 for the travel duration plus effect 16
  and the air-diagonal-kick/kick-impact audio at accepted contact.
- State 64's motion-129 throw delivers 150 at primary-clip completion. State
  63's motion-127 attach is non-damaging and its primary completion delivers
  70 while the web line remains owned until state exit. States 101/102 retain
  the airborne target and web line but send only their authored zero-damage
  grab contact.
- Directional spider-sense counters are distinct authored attacks, not one
  generic response: states 38/39/40/41 each deal 300 and emit effects
  7/6/8/9 respectively. Core coverage pins all four quadrants; the live
  first-encounter gate also pins the front counter, enemy warning/attack,
  three-times slow motion, enemy-side hit suppression, and accepted target
  contact.
- `EnemyAttributeFile::ReadEnemyAttackInfo` (`0x0033c2c0`) stores the
  serialized Spider-Sense selector at `EnemyAttackInfo+0x48`, slow-motion
  denominator at `+0x50`, forced-sense gate at `+0x54`, and photo target at
  `+0x58`. `AISenseInfo` (`0x003a7a30`) and `Player::onMessage`
  (`0x0034ddb4`) repack those fields before `DoNormalSenseAction`; the
  opening knife and both bat attacks author selector 1, denominator 3, and
  `-1` for both optional IDs. `Unit::CanBeCounterHit` (`0x002fe8ec`) returns
  the archetype byte loaded from `EnemyAttributeInfo+0x4b`; both opening
  thug archetypes enable it.
- The native switch tables at `0x004cfd28` and `0x004cfd38` are
  `[38,40,41,39]` for close quadrant counters and `[35,34,36,37]` for
  reaction selectors 2--5. Airborne, distant, non-counterable, selector-6,
  and early chained-sense cases instead choose the perpendicular evade pair
  with the executable's 50-percent branch. A live airborne opening-knife
  test now proves that the warning chooses state 36, deals no enemy damage,
  preserves player health, and enters the authored three-times slow motion.
- `Player::CheckBlinkStrike` (`0x00342550`) precedes the ordinary directional
  response in `Player::DoNormalSenseAction` (`0x0034f630`). It accepts a live
  enemy only when `CEnemy::IsNearAttackKeyFrame` (`0x00334dd0`) finds an
  authored damage action from 200 ms before through 49 ms after its keyframe.
  For the 1667 ms opening knife clip, the first 45-percent contact therefore
  permits the red-suit blink strike at animation times 550--799 ms. State 42,
  motion `0x1fd`, then emits effect 25 on entry, effect 24 at frame 16, deals
  200 sector damage with preserved hit type `0x1fd` at frame 18, and emits
  `super_web_splash` plus VoxSound `0x54` at frame 20. Immediately after effect
  24 at frame 16, the same native branch (`0x00352f04`--`0x00352f18`) stops
  VoxSound `0x53`; the shipped table resolves it to `SFX_SUPER_WEB_ATTACK`.
  The corresponding live gate separately proves that exact stop and this
  narrow path without replacing the earlier directional-counter coverage.
- Effects 24/25 are not rigid-only meshes. Native channel type `0x0e` selects
  `CWeightEx` in `CColladaDatabase::getAnimationTrackEx` (`0x00418348`), and
  `ISceneNodeAnimator::forceBind` (`0x00429870`) binds `SChannel+0x0c` to the
  exact morph target slot. `CColladaMorphingMesh::morph` (`0x00420384`) then
  applies the serialized normalized blend to positions and normals before the
  node transform. Both the shrink and explode clips now use those authored
  weights; the Object01 `weights` channel is proven to select slot 1 rather
  than a name-derived slot 0.
- `Player::CanEnableSpiderSense` (`0x00341c98`) gates chained sense input at
  the current class-6 state's inclusive `StateBasic+0x30` frame (the parsed
  `soundTriggerFrame`: 3 for evades, 7/8 for directional counters, and 6 for
  blink strikes). It also rejects state 73, ultimate states 107--113,
  web-whirlwind motion 116, wall-attack motion 131, and air-bounce states
  117/118. These are now native state-data predicates rather than an
  unrestricted attack cancellation.

## Player target selection

`CLevel::GetTargetedDestroyableList` (`0x0037df54`) returns every destroyable
whose authored `IsAttack` byte is enabled. Both native player searches include
that list; scenery is not merely checked after a free-form enemy attack.

`Player::SearchTargetByAttackRange` (`0x003430c8`) walks visible, live
destroyables in authored order, retains the strictly nearest object inside the
raw three-dimensional range, obtains the enemy candidate from
`CTargetHelper::getNearestTarget(mask=3)`, and keeps the enemy only when it is
strictly nearer. Equal distance therefore selects the destroyable.
`Player::SearchTargetByEyeHorizon` (`0x00343b70`) appends the same objects after
the helper's airborne/non-airborne enemy lists, traverses the combined list
backwards, applies the 0.5 horizontal-facing threshold and the target-radius
range allowance, rejects `Unit::IsBlockedByWorld` (`0x00324670`) occlusion,
and replaces a candidate only for a strictly better facing dot. Equal facing
therefore also retains the destroyable. The runtime now preserves those
filters, ordering, ties, world rays, and target IDs through the attack update;
web lines aimed at targetable scenery also follow the retained object rather
than snapping back to a stale launch point.

## Shopping-center doorway

The shipped Room 1 `geometry01.bdae` already contains the broken shopping-
center doorway. Its geometry has no morph target, and the Room 1 scene has no
mesh-bearing door node or intact alternate door mesh. Crash cinematic 1212
has exactly 48 commands and moves the bus object 1210, plays its damage
animation, and emits the impact presentation. Its only visibility changes are
for Sandman and hint object 1216; no command swaps, hides, destroys, or changes
a door. Therefore the source assets do **not** contain an intact-to-broken door
transition at the bus impact. Implementing one would be an invention rather
than parity with this shipped build.

## Opening-thug authored appearances

The opening knife, bat, and gun appearances use authored regions of the
shared 512-by-512 `thug.tga` atlas. This is not interchangeable with assigning
an invented random tint. The indexed UVs select the lower-right atlas tile for
the bat mesh and the upper-right tile for the gun mesh. The knife mesh carries
the same lower-right UV range as the bat but its first diffuse texture record
adds `U=-0.498`, selecting the lower-left tile.

`CMaterial::prepareMaterial` (`0x0041c7e2`--`0x0041c84e`) reads translation,
rotation, and scale from each 0x1c-byte texture record and calls
`CMatrix4<float>::buildTextureTransform` at `0x00419260`. The BDAE parser and
D3D11 vertex path now retain that full row-vector affine transform. Material
animation remains distinct: `CTextureTransformEx::applyValueEx` at
`0x0041a5bc` replaces the prepared matrix with its animated SData transform,
rather than incrementally adding an offset to the static matrix.

The asset census logs both raw indexed UV bounds and all six affine matrix
components. Core tests pin the bat identity matrix and the knife's authored
`U=-0.498` matrix. The first-encounter presentation gate consequently renders
the two knife instances and one bat instance with their distinct shipped
atlas regions. The first encounter has no additional spawn randomizer. A
complete reference
census for the executable's `random`, `rand`, `lrand48`, `Rand`, and `NRand`
entry points finds no call from `CEnemy::ProcessUserAttr`, `CEnemy::Init`, or
material preparation. The only `CEnemy` calls to the global two-argument
`random` routine occur in state choice, movement/attack positioning, and voice
selection after construction. Likewise, the native texture, diffuse-color,
ambient-color, and vertex-color mutation helpers have no enemy-construction
caller.

`CEnemy::ProcessUserAttr` (`0x00332870`) instead passes each node's serialized
`MeshFile` directly to `IAnimatedObject` and never replaces its texture. The
shared thug animation bank carries no texture-transform channel, so it cannot
change the atlas choice after load. Core tests now pin objects 394/397 to the
knife transform and object 395 to the bat transform, including the absence of
animated UV tracks. Randomizing these three would contradict this shipped
Android executable and Level 1 scene.

## Automated acceptance

The coherent native-cadence Release run in
`analysis/generated/combat-sense-cue-suite/suite-manifest.json`
completed all 34 selected scenarios with 34 passes and zero failures. The run
used one executable hash for every scenario and removed 23 generated BMP
captures after their assertions. The focused post-fix run in
`analysis/generated/autoplay-main-combat-current/suite-manifest.json` uses one
new executable hash and passes all 11 selected opening-combat regressions,
including every directional Spider-Sense branch, launcher and kick-down hurt
graphs, enemy offense, the health ledger, and the full combo/effects sequence.
The current retained gates are:

- `OpenAndroidUSM.CoreTests.exe`
- `OpenAndroidUSM.RenderTests.exe`
- `first-encounter-health-damage-audit.usmauto` (53/53)
- `first-encounter-full-combo-effects.usmauto` (46/46)
- `first-encounter-simultaneous-input-order.usmauto` (20/20)
- `first-encounter-alternate-combat-effects.usmauto` (82/82)
- `combat-effects-parity-gate.usmauto` (47/47)
- `first-encounter-normal-progression.usmauto` (14/14)
- `first-encounter-ground-web-bind-parity.usmauto` (22/22)
- `first-encounter-ground-web-throw-parity.usmauto` (32/32)
- `first-encounter-punch-web-throw-parity.usmauto` (30/30)
- `first-encounter-air-web-knockdown-parity.usmauto` (27/27)
- `first-encounter-air-web-throw-parity.usmauto` (26/26)
- `first-encounter-air-kick-down-combo-parity.usmauto` (30/30)
- `first-encounter-jump-release-parity.usmauto` (37/37)
- `first-encounter-launcher-parity.usmauto` (23/23)
- `first-encounter-crowd-separation.usmauto` (11/11)
- `first-encounter-spider-sense-counter.usmauto` (25/25)
- `first-encounter-spider-sense-back-counter.usmauto` (18/18)
- `first-encounter-spider-sense-left-counter.usmauto` (18/18)
- `first-encounter-spider-sense-right-counter.usmauto` (18/18)
- `first-encounter-spider-sense-air-evade.usmauto` (17/17)
- `first-encounter-spider-sense-blink-strike.usmauto` (25/25)
- `first-encounter-skill-lock-parity.usmauto` (15/15)
- `first-encounter-ground-web-directional-throw-parity.usmauto` (23/23)
- `first-encounter-ground-web-drag-down-parity.usmauto` (32/32)
- `first-encounter-air-target-kick-parity.usmauto` (32/32)
- `first-encounter-air-web-grab-parity.usmauto` (33/33)
- `first-encounter-melee-miss-parity.usmauto` (18/18)
- `first-encounter-enemy-interruption-parity.usmauto` (18/18)
- `first-encounter-enemy-offense-parity.usmauto` (42/42)
- `first-encounter-web-pellet-render.usmauto` (12/12)
- `first-encounter-far-combo-parity.usmauto` (20/20)
- `first-encounter-manual-combat.usmauto` (19/19)
- `first-encounter-presentation.usmauto` (10/10)
- `player-button-combat-probe.usmauto` (34/34)

These gates establish the audited source facts and catch their regressions.
They are not a blanket claim of parity for later-level enemy types,
black-suit-only transitions, or the full campaign. Within the chronological
first encounter, the normal-suit attack graph, target retention, miss path,
enemy interruption, crowd separation, enemy offense, player hurt reactions,
damage, trails, target splashes, web projectiles/lines, and audio now have
source-to-runtime acceptance coverage. Opening knife and bat types have no
behavior-slot-8 block state, so adding a block response would be invention.
