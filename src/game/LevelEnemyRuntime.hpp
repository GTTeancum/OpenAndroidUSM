#pragma once

#include "core/Result.hpp"
#include "game/CinematicScript.hpp"
#include "game/LevelOneBootstrap.hpp"
#include "game/NativeRandomizer.hpp"
#include "game/QuickTimeActionRuntime.hpp"
#include "game/WallWebRuntime.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace usm::game {

class LevelCollision;

enum class EnemyBehaviorState {
    Disabled,
    Idle,
    Chasing,
    AttackRange,
    Hurt,
    TiedUp,
    Dead,
};

enum class SandmanBossTaskState {
    None,
    GroundAttack,
    GroundAttackRecovery,
    Jump,
};

// CBehaviorDush uses the contiguous authored behavior states 78-85.  Rhino's
// phase-zero CBoss task table first pushes task 3 twice (melee, each preceded
// by task 1 movement) and then task 11 (dash).  Keep the portable state
// explicit so deterministic traces can prove that sequence rather than
// reducing it to the generic thug behavior enum.
enum class RhinoBossTaskState {
    None,
    Approach,
    Melee,
    DashReady,
    DashRush,
    DashSuccess,
    DashSkid,
    DashFailed,
    DashFailedStruggle,
    ThrowApproach,
    ThrowReady,
    ThrowRush,
    ThrowCatch,
    ThrowStruggle,
    ThrowSuccess,
    ThrowRelease,
    ThrowMiss,
};

// CBoss::_GLOBAL__I_CBoss (0x00329cd4) gives Robot Phantom the repeating
// authored FIFO task sequence 3, 24, 5, 24. CBoss::InitAiTask
// (0x0032b66c) assigns behavior IDs 0x12f (melee), 0x141 (conceal), and
// 0x134 (range); ParseAiTaskInfo (0x0032c0c8) inserts the 500 cm approach
// before the melee and range tasks. Keep every native transition explicit so
// traces can distinguish the recovered graph from generic thug behavior.
enum class RobotPhantomTaskState {
    None,
    ApproachMelee,
    RushReady,
    RushFirst,
    RushSecond,
    RushRecovery,
    ConcealReady,
    ConcealHidden,
    ConcealAttack,
    ConcealRecovery,
    ApproachRange,
    ThrowReady,
    Throw,
    ThrowWait,
    ThrowRecovery,
};

// CBoss::InitAiTask (0x0032b66c) builds Electro's six-entry FIFO as
// range, weak, rotate, weak, Electro dash, weak. The behavior-specific
// native states are 92-94 (CBehaviorWeak), 102-104 (CBehaviorRotate), and
// 122-125 (CBehaviorElectroDush). Preserve them separately so diagnostics
// prove the recovered graph rather than reporting a generic boss attack.
enum class ElectroBossTaskState {
    None,
    RangeAttack,
    WeakStart,
    Weak,
    WeakEnd,
    RotateReady,
    Rotate,
    RotateEnd,
    DashReady,
    DashRush,
    DashLand,
    DashEnd,
};

struct EnemyCinematicMotionState {
    assets::Vector3 startPosition;
    assets::Vector3 endPosition;
    assets::Quaternion startRotation;
    assets::Quaternion endRotation;
    std::uint32_t elapsedMilliseconds{};
    std::uint32_t durationMilliseconds{};
    bool active{};
};

struct LevelEnemyState {
    const LevelEnemyAsset* asset{};
    assets::Vector3 position;
    assets::Vector3 facing{1.0F, 0.0F, 0.0F};
    // Unit::UpdateRenderOffset (0x00324d70) keeps the skinned pelvis aligned
    // with the physics root advanced by Unit::UpdateDisplacement. This is a
    // render-only local offset; position remains the collision capsule base.
    assets::Vector3 animationRenderOffset;
    std::array<float, 16> worldTransform{};
    std::string activeAnimation;
    std::uint32_t animationTimeMilliseconds{};
    double animationFractionalMilliseconds{};
    float animationSpeed{1.0F};
    bool animationLoops{true};
    bool animationReversed{};
    float collisionRadius{};
    float collisionHeight{};
    bool canBeTiedUp{};
    bool canBeDraggedTo{};
    bool allowsHorizontalHitForce{};
    bool allowsVerticalHitForce{};
    bool allowsLaunchHitType{};
    bool canBeCounterHit{};
    float verticalVelocity{};
    // CBehaviorHurt::StartMove applies AIHitTargetInfo's two force values
    // along the source-to-target axis and world Z. Preserve them separately
    // from ordinary chase motion so the authored hurt-state graph can carry
    // launches and knockback through its animation transitions.
    assets::Vector3 hurtVelocity;
    std::int16_t hurtStateId{-1};
    std::int16_t lastPlayerHitType{100};
    bool hurtStartedGrounded{};
    bool grounded{};
    // CEnemy::CheckWall (0x003305d8) stores the supporting plane normal.
    // Position remains the physics capsule base; the wall render anchor is
    // shifted by one radius toward the surface in UpdateForce (0x00331d78).
    bool onWall{};
    bool wallAttached{};
    bool wallWebCaptured{}; // CEnemy Unit state 14, messages 0x130/0x131.
    assets::Vector3 wallNormal;
    assets::Vector3 wallMoveTarget;
    assets::Vector3 wallAttackDirection;
    std::int32_t wallBehaviorState{}; // CBehaviorMoveOnWall states 96-99.
    std::uint32_t wallIdleMilliseconds{1000};
    // A small number of shipped actors are authored outside every static and
    // navigation support triangle. Native visibility-gated physics leaves
    // them at their scene transform until activated; retain that transform
    // rather than integrating them irrecoverably out of the world.
    bool supportInitialized{};
    bool anchoredWithoutSupport{};
    float unsupportedSupportHeight{};
    float health{};
    float maximumHealth{};
    bool visible{};
    bool aiEnabled{};
    // CCinematicThread::DisableAI/EnableAI directly deactivate/reactivate the
    // native PhysicsEntity independently of scene-node visibility.
    bool physicsActive{};
    bool playerDetected{};
    EnemyBehaviorState behavior{EnemyBehaviorState::Disabled};
    EnemyCinematicMotionState cinematicMotion;
    // CCinematicThread::ThrowingSomething/StopAction send CBoss local
    // messages 0x59/0x5e. Preserve the externally driven pick-up action and
    // its related object separately from the ordinary AI task queue.
    bool cinematicActionActive{};
    std::int32_t cinematicActionObjectId{-1};
    std::uint32_t rangeAttackCooldownMilliseconds{};
    std::uint32_t meleeAttackCooldownMilliseconds{};
    bool meleeAttackActive{};
    // CBehaviorMeleeAttack::StateEnter (0x003baef8) retains the complete
    // BehaviorAnimInfo list selected for an attack. The hammer rush list is
    // [idle_attack_hammer_rush_ready, attack_hammer_rush_to_idle], so its
    // warning/charge clip must finish before the damaging clip begins.
    std::vector<std::string> meleeAttackAnimationSequence;
    std::size_t meleeAttackAnimationSequenceIndex{};
    std::int16_t selectedMeleeAttackId{-1};
    // Action type 2 in EnemysSpecialAnimConfigs registers the native
    // Spider-Sense attack at its authored key-frame percentage.  This is
    // intentionally distinct from merely entering the melee animation.
    bool meleeSenseActive{};
    // CAIEntityManager owns a bounded list distinct from the behavior state.
    // Normal difficulty permits one registered melee attacker at a time.
    bool meleeAttackRegistered{};
    // RegisterEntityForMeleeAttack (0x00375710) assigns every entry a native
    // random [5000, 15000) ms lease. With the default one-attacker cap the
    // manager does not age this value, but consuming and retaining it keeps
    // the original global RNG call order visible to diagnostics.
    float meleeRegistrationTimerMilliseconds{};
    // CBehaviorTiedUp states 35-37 start from a 4000 ms authored timer,
    // multiplied by the player's web-duration upgrade rate.
    std::int16_t tiedUpStateId{-1};
    std::uint32_t tiedUpRemainingMilliseconds{};
    SandmanBossTaskState sandmanTask{SandmanBossTaskState::None};
    assets::Vector3 sandmanJumpStart;
    assets::Vector3 sandmanJumpTarget;
    std::uint32_t sandmanJumpElapsedMilliseconds{};
    std::uint32_t sandmanJumpDurationMilliseconds{};
    RhinoBossTaskState rhinoTask{RhinoBossTaskState::None};
    std::uint32_t rhinoTaskElapsedMilliseconds{};
    std::uint32_t rhinoMeleeAttacksRemaining{};
    assets::Vector3 rhinoDashDirection;
    std::uint32_t rhinoPhase{};
    std::uint32_t rhinoSequenceCycle{};
    RobotPhantomTaskState robotPhantomTask{
        RobotPhantomTaskState::None};
    std::uint32_t robotPhantomTaskElapsedMilliseconds{};
    // Index into the exact native task order: melee, conceal, range, conceal.
    std::uint32_t robotPhantomSequenceIndex{};
    ElectroBossTaskState electroTask{ElectroBossTaskState::None};
    std::uint32_t electroTaskElapsedMilliseconds{};
    // Index into the exact native task order: range, weak, rotate, weak,
    // Electro dash, weak.
    std::uint32_t electroSequenceIndex{};
    std::uint32_t electroPhase{};
    std::uint32_t electroRangeAttacksRemaining{};
    std::uint32_t electroRangeWaitMilliseconds{};
    std::uint32_t electroDashesRemaining{};
    assets::Vector3 electroHomePosition;
    assets::Vector3 electroDashStart;
    assets::Vector3 electroDashTarget;
    assets::Vector3 electroDashDirection;
    bool electroRangeReleased{};
    bool electroWeakReleased{};
    bool electroDashEffectThrown{};
    bool electroDashHitPlayer{};
};

struct EnemyPlayerHit {
    std::int32_t sourceObjectId{-1};
    std::int16_t attackId{-1};
    float damage{};
    std::int32_t hitType{100};
};

struct PlayerMeleeHitResult {
    std::int32_t objectId{-1};
    float actualDamage{};
    // CEnemy::ProcessHitInfo creates the contact splash before the hurt
    // behavior replaces the enemy's animation. Preserve that pre-reaction
    // Bip01_Spine1 position through the portable dispatch boundary.
    assets::Vector3 hitEffectOrigin;
};

// CBullet type zero, created by Player::ShootWebPellet. The native physics
// entity is a 30 cm sphere launched 50 cm ahead of the hand at 1500 cm/s.
struct PlayerWebPelletState {
    std::int32_t targetedEnemyObjectId{-1};
    assets::Vector3 position;
    assets::Vector3 velocity;
    float damage{};
    float traveledCentimeters{};
    bool active{};
};

enum class PlayerWebPelletEventKind {
    Spawned,
    EnemyContact,
    StaticContact,
    Expired,
};

struct PlayerWebPelletEvent {
    PlayerWebPelletEventKind kind{PlayerWebPelletEventKind::Spawned};
    std::int32_t targetedEnemyObjectId{-1};
    std::int32_t hitEnemyObjectId{-1};
    assets::Vector3 position;
    float requestedDamage{};
    float actualDamage{};
    assets::Vector3 hitEffectOrigin;
    assets::Vector3 webSplashOrigin;
};

struct EnemySeparationEvent {
    std::int32_t firstObjectId{-1};
    std::int32_t secondObjectId{-1};
    float distanceBefore{};
    float distanceAfter{};
    float requiredDistance{};
};

struct EnemySoundCue {
    std::int32_t sourceObjectId{-1};
    std::int32_t voxSoundId{-1};
};

struct EnemyCameraShakeCue {
    float maximumOffset{};
    std::uint32_t frameCount{};
    assets::Vector3 axisRates;
};

// Portable counterpart of CGunLine. The original advances a short tracer at
// 1500 cm/s, checks each swept segment against the player's Unit AABB, and
// retires it after two seconds. It has no room-mesh collision body.
struct EnemyGunLineState {
    std::int32_t sourceObjectId{-1};
    assets::Vector3 position;
    assets::Vector3 direction;
    float damage{};
    std::uint32_t ageMilliseconds{};
    bool active{};
};

enum class EnemyMolotovPhase {
    Flying,
    ExplodeReady,
};

// CThrowObject subtype 1, allocated by weapon type 5. Positions and velocity
// remain in the original centimeter units used by the level collision mesh.
struct EnemyMolotovState {
    std::int32_t sourceObjectId{-1};
    std::int32_t roomId{-1};
    assets::Vector3 position;
    assets::Vector3 velocity;
    assets::Vector3 facing{1.0F, 0.0F, 0.0F};
    float gravityCentimetersPerSecondSquared{};
    float damage{};
    std::uint32_t phaseElapsedMilliseconds{};
    EnemyMolotovPhase phase{EnemyMolotovPhase::Flying};
    bool stoppedByPlayer{};
    bool active{};
};

// CBoomerang's native states are assigned by SetState (0x0035aefc) and
// advanced by Update (0x0035b7f8). The numeric ordering is intentionally
// retained because CBehaviorRangeAttack tests IsReady/WillReturnHand through
// the weapon vtable while waiting for states 4 and 5.
enum class EnemyBoomerangPhase : std::int32_t {
    Outbound = 0,
    TargetPause = 1,
    CollisionPause = 2,
    Returning = 3,
    HandReturn = 4,
    Ready = 5,
};

struct EnemyBoomerangState {
    std::int32_t sourceObjectId{-1};
    std::int32_t roomId{-1};
    assets::Vector3 position;
    assets::Vector3 velocity;
    assets::Vector3 targetPosition;
    assets::Vector3 facing{1.0F, 0.0F, 0.0F};
    float speedCentimetersPerSecond{500.0F};
    float damage{};
    std::uint32_t phaseElapsedMilliseconds{};
    EnemyBoomerangPhase phase{EnemyBoomerangPhase::Ready};
    bool hitPlayer{};
    bool active{};
};

enum class EnemyThunderclapPhase : std::int32_t {
    Converging = 2,
    Impact = 3,
    Release = 4,
    Fading = 5,
    Ready = 6,
};

// Weapon type 0x12 is CSummonObjManage's Thunderclap pool. Native Launch
// (0x003670dc) creates three summons on a 400 cm ring around the captured
// player position, and Update (0x00366b1c) converges them over three seconds.
struct EnemyThunderclapState {
    std::int32_t sourceObjectId{-1};
    std::int32_t roomId{-1};
    assets::Vector3 targetPosition;
    assets::Vector3 position;
    assets::Vector3 velocity;
    float damage{};
    std::uint32_t phaseElapsedMilliseconds{};
    EnemyThunderclapPhase phase{EnemyThunderclapPhase::Ready};
    bool hitPlayer{};
    bool active{};
};

// CBehaviorRotate owns three CElectricPostWithEffect instances. Native
// ResetElectricPost (0x003c2848) places them at 40% of the boss collision
// height and separates their local -Y rays by exactly 2.094395 radians.
struct EnemyElectricPostState {
    std::int32_t sourceObjectId{-1};
    std::int32_t roomId{-1};
    assets::Vector3 position;
    assets::Vector3 facing{1.0F, 0.0F, 0.0F};
    float damage{};
    std::uint32_t animationTimeMilliseconds{};
    bool hitPlayer{};
    bool active{};
};

// EffectManager::ThrowAnimEffect calls made by Electro weak/dash behaviors
// pair electro_wave.bdae with electro_wave_billboard.bdae at one origin.
struct EnemyElectroBurstState {
    std::int32_t sourceObjectId{-1};
    std::int32_t roomId{-1};
    assets::Vector3 position;
    float scale{1.0F};
    std::uint32_t elapsedMilliseconds{};
    bool active{};
};

enum class EnemyLandingAnimatedEffectKind : std::uint8_t {
    Shockwave,
    CrashWall,
};

struct EnemyLandingAnimatedEffectState {
    EnemyLandingAnimatedEffectKind kind{
        EnemyLandingAnimatedEffectKind::Shockwave};
    std::int32_t sourceObjectId{-1};
    std::int32_t roomId{-1};
    assets::Vector3 position;
    float scale{1.0F};
    std::uint32_t lifetimeMilliseconds{};
    std::uint32_t elapsedMilliseconds{};
    bool additiveModulateMaterial{};
    bool active{};
};

struct EnemyLandingAnimatedEffectSpawnEvent {
    EnemyLandingAnimatedEffectKind kind{
        EnemyLandingAnimatedEffectKind::Shockwave};
    std::int32_t sourceObjectId{-1};
    std::int32_t roomId{-1};
    assets::Vector3 position;
    float scale{1.0F};
    std::uint32_t lifetimeMilliseconds{};
    bool additiveModulateMaterial{};
};

enum class EnemyProjectileEventKind {
    Spawned,
    PlayerContact,
    Grounded,
    Exploded,
    StaticContact,
    Returned,
};

struct EnemyProjectileEvent {
    EnemyProjectileEventKind kind{EnemyProjectileEventKind::Spawned};
    std::int32_t sourceObjectId{-1};
    assets::Vector3 position;
    assets::Vector3 velocity;
    float gravityCentimetersPerSecondSquared{};
};

struct EnemyEffectCue {
    std::int32_t sourceObjectId{-1};
    std::int32_t roomId{-1};
    assets::Vector3 position;
    std::string effectType;
};

// Mutable native state for the authored enemy objects. Cinematic command names
// remain intact so recovered scripts can manipulate state without binary hooks.
class LevelEnemyRuntime final {
public:
    [[nodiscard]] Result initialize(
        const LevelOneBootstrap& level,
        NativeRandomizer* nativeRandomizer = nullptr);
    void advanceAnimations(
        std::uint32_t elapsedMilliseconds,
        const LevelCollision* collision = nullptr) noexcept;
    void updateGameplay(std::uint32_t elapsedMilliseconds,
                        const assets::Vector3& playerPosition,
                        const LevelCollision* collision = nullptr,
                        bool quickTimeActionPressed = false,
                        const assets::Vector3& playerFacing =
                            assets::Vector3{1.0F, 0.0F, 0.0F},
                        bool playerOnWall = false,
                        std::int32_t playerSenseReactState = 0,
                        std::optional<assets::Vector3>
                            playerRangeTargetPosition = std::nullopt) noexcept;
    [[nodiscard]] std::optional<std::int32_t> applyPlayerMeleeHit(
        const assets::Vector3& attackPosition,
        const assets::Vector3& attackDirection, float radius, float damage,
        float minimumForwardDot = 0.0F) noexcept;
    [[nodiscard]] std::optional<PlayerMeleeHitResult>
    applyPlayerMeleeHitDetailed(
        const assets::Vector3& attackPosition,
        const assets::Vector3& attackDirection, float radius, float damage,
        float minimumForwardDot = 0.0F, std::int16_t hitType = 100,
        const assets::Vector3* sourcePosition = nullptr,
        float horizontalForce = 0.0F,
        float verticalForce = 0.0F) noexcept;
    [[nodiscard]] std::vector<PlayerMeleeHitResult>
    applyPlayerSectorMeleeHits(
        const assets::Vector3& attackPosition,
        const assets::Vector3& attackDirection, float radius, float damage,
        float minimumForwardDot = 0.0F, std::int16_t hitType = 100,
        const assets::Vector3* sourcePosition = nullptr,
        float horizontalForce = 0.0F,
        float verticalForce = 0.0F) noexcept;
    [[nodiscard]] std::vector<PlayerMeleeHitResult>
    applyPlayerAirKickDownSectorMeleeHits(
        const assets::Vector3& attackPosition,
        const assets::Vector3& attackDirection, float radius, float damage,
        float minimumForwardDot, std::int32_t retainedTargetObjectId,
        const assets::Vector3* sourcePosition = nullptr,
        float retainedHorizontalForce = 0.0F,
        float retainedVerticalForce = 0.0F) noexcept;
    [[nodiscard]] std::vector<PlayerMeleeHitResult>
    applyPlayerRadialMeleeHits(
        const assets::Vector3& attackPosition, float radius,
        float damage, std::int16_t hitType = 100,
        float horizontalForce = 0.0F,
        float verticalForce = 0.0F) noexcept;
    // Player::CheckAttackTarget's state-class-6 path applies the authored
    // sector to every valid Unit and then guarantees the registered attacker
    // if the sector search did not already include it.
    [[nodiscard]] std::vector<PlayerMeleeHitResult>
    applyPlayerSenseMeleeHits(
        const assets::Vector3& attackPosition,
        const assets::Vector3& attackDirection, float radius, float damage,
        float minimumForwardDot, std::int32_t attackerObjectId,
        std::int16_t hitType = 121,
        float horizontalForce = 0.0F,
        float verticalForce = 0.0F) noexcept;
    [[nodiscard]] std::optional<PlayerMeleeHitResult>
    applyPlayerTargetedHitDetailed(std::int32_t objectId,
                                   float damage,
                                   std::int16_t hitType = 100,
                                   const assets::Vector3* sourcePosition = nullptr,
                                   float horizontalForce = 0.0F,
                                   float verticalForce = 0.0F) noexcept;
    [[nodiscard]] std::vector<PlayerMeleeHitResult> applyPlayerWallMeleeHits(
        const assets::Vector3& attackCenter, const assets::Vector3& wallNormal,
        const assets::Vector3& direction, float reach, float damage,
        float minimumAngleDegrees, float maximumAngleDegrees,
        std::int16_t hitType = 131,
        float horizontalForce = 0.0F,
        float verticalForce = 0.0F) noexcept;
    [[nodiscard]] std::optional<PlayerMeleeHitResult>
    applyPlayerWebBindingDetailed(std::int32_t objectId,
                                  float damage) noexcept;
    [[nodiscard]] bool applyPlayerAirKnockdownBinding(
        std::int32_t objectId) noexcept;
    [[nodiscard]] bool launchPlayerWebPellet(
        const assets::Vector3& origin,
        const assets::Vector3& targetPosition,
        std::int32_t targetedEnemyObjectId,
        float damage) noexcept;
    // Player::UpdateKeyTrigger (0x0034d0a4) first invokes
    // SearchTargetByEyeHorizon (0x00343b70). It walks CTargetHelper's sorted
    // lists in reverse and chooses the strictly best directional dot. Only
    // neutral input may fall back to SearchTargetByAttackRange (0x003430c8),
    // whose final raw three-dimensional range comparison is strict.
    [[nodiscard]] const LevelEnemyState* findPlayerAttackTarget(
        const assets::Vector3& playerPosition,
        const assets::Vector3& attackDirection, bool hasDirectionalInput,
        float maximumRange = 1000.0F,
        const LevelCollision* collision = nullptr,
        float minimumForwardDot = 0.5F) const noexcept;
    // CTargetHelper::update (0x003543e4) places CEnemy::IsInAir targets in
    // list/mask 2. GetAirWebSpecialState consults its nearest entry before the
    // directional/attack-range fallback and caps the helper census at 2000 cm.
    [[nodiscard]] const LevelEnemyState* findNearestAirbornePlayerTarget(
        const assets::Vector3& playerPosition,
        float maximumRange = 2000.0F) const noexcept;
    [[nodiscard]] const LevelEnemyState* findPlayerWallAttackTarget(
        const assets::Vector3& playerPosition) const noexcept;
    // Native CTargetHelper contains every enemy that has registered an
    // imminent attack. Normal difficulty exposes the one active melee
    // engager to the spider-sense input path.
    [[nodiscard]] const LevelEnemyState* findSpiderSenseAttacker(
        const assets::Vector3& playerPosition) const noexcept;
    // CTargetHelper::popAttack (0x00353d98) removes the selected warning as
    // soon as UpdateSpiderSense accepts it. This prevents one enemy attack
    // from being consumed repeatedly during its remaining animation.
    [[nodiscard]] bool consumeSpiderSenseAttacker(
        std::int32_t objectId) noexcept;
    [[nodiscard]] float spiderSenseSlowMotionDenominator(
        std::int32_t objectId) const noexcept;
    [[nodiscard]] std::int32_t spiderSenseReactionType(
        std::int32_t objectId) const noexcept;
    [[nodiscard]] bool isNearAttackKeyFrame(
        std::int32_t objectId) const noexcept;
    // CEnemy::IsInAir (0x0032883c) does not expose raw physics support. It
    // delegates to CAIBehaviorManager::IsCurActiveFloat (0x00373dec), which
    // recognizes only the authored airborne hurt/tied-up behavior states.
    [[nodiscard]] bool isInAir(std::int32_t objectId) const noexcept;
    [[nodiscard]] bool canEnterWallWeb(std::int32_t objectId) const noexcept;
    [[nodiscard]] bool applyWallWebEvent(const WallWebEvent& event);
    [[nodiscard]] std::optional<assets::Vector3> nodeWorldPosition(
        std::int32_t objectId, std::string_view nodeName,
        const assets::Vector3& localPoint = {}) const;
    [[nodiscard]] std::vector<EnemyPlayerHit> consumePlayerHits() noexcept;
    [[nodiscard]] std::vector<EnemySoundCue> consumeSoundCues() noexcept;
    [[nodiscard]] std::vector<EnemyCameraShakeCue>
    consumeCameraShakeCues() noexcept;
    [[nodiscard]] std::vector<EnemyProjectileEvent>
    consumeProjectileEvents() noexcept;
    [[nodiscard]] std::vector<EnemyEffectCue> consumeEffectCues() noexcept;
    [[nodiscard]] std::vector<EnemyLandingAnimatedEffectSpawnEvent>
    consumeLandingAnimatedEffectSpawnEvents() noexcept;
    [[nodiscard]] std::vector<PlayerWebPelletEvent>
    consumePlayerWebPelletEvents() noexcept;
    [[nodiscard]] std::vector<EnemySeparationEvent>
    consumeEnemySeparationEvents() noexcept;
    [[nodiscard]] bool destroy(std::int32_t objectId) noexcept;
    // Process-local diagnostic harness hook. Production activation still
    // arrives through CCinematicThread::EnableAI; this only avoids driving a
    // desktop window when a reconstructed boss task needs a focused probe.
    [[nodiscard]] bool setDiagnosticAiEnabled(
        std::int32_t objectId, bool enabled,
        bool forcePlayerDetected = true) noexcept;
    [[nodiscard]] bool setDiagnosticPhysicsActive(
        std::int32_t objectId, bool enabled) noexcept;
    [[nodiscard]] bool applyDiagnosticDamage(std::int32_t objectId,
                                             float damage) noexcept;
    [[nodiscard]] Result applyCinematicCommand(
        const LevelOneBootstrap& level, const CinematicThread& thread,
        const CinematicCommand& command);
    // CCinematicThread::UnUseDAEAnim (0x00370624) leaves the original
    // actor visible and reactivates its PhysicsEntity after PopAnimator.
    void endColladaAnimation(std::int32_t objectId) noexcept;
    void resetTransientForCheckPointLoad() noexcept;

    [[nodiscard]] std::span<const LevelEnemyState> states() const noexcept {
        return states_;
    }
    // CAIEntityManager melee-arbitration diagnostics reconstructed from
    // 0x003744a4, 0x00375560, 0x00375654, and 0x00375710.
    [[nodiscard]] std::int32_t meleeEngagerObjectId() const noexcept {
        return meleeEngagerObjectId_;
    }
    [[nodiscard]] float meleeEngagementCooldownMilliseconds() const noexcept {
        return meleeEngagementCooldownMilliseconds_;
    }
    [[nodiscard]] std::int32_t nativeRandomState() const noexcept {
        return nativeRandomizer_->state();
    }
    [[nodiscard]] std::span<const EnemyGunLineState> gunLines() const noexcept {
        return gunLines_;
    }
    [[nodiscard]] std::span<const EnemyMolotovState> molotovs() const noexcept {
        return molotovs_;
    }
    [[nodiscard]] std::span<const EnemyBoomerangState> boomerangs() const
        noexcept {
        return boomerangs_;
    }
    [[nodiscard]] std::span<const PlayerWebPelletState> playerWebPellets()
        const noexcept {
        return playerWebPellets_;
    }
    [[nodiscard]] std::span<const EnemyThunderclapState> thunderclaps() const
        noexcept {
        return thunderclaps_;
    }
    [[nodiscard]] std::span<const EnemyElectricPostState> electricPosts() const
        noexcept {
        return electricPosts_;
    }
    [[nodiscard]] std::span<const EnemyElectroBurstState> electroBursts() const
        noexcept {
        return electroBursts_;
    }
    [[nodiscard]] std::span<const EnemyLandingAnimatedEffectState>
    landingAnimatedEffects() const noexcept {
        return landingAnimatedEffects_;
    }
    [[nodiscard]] const LevelEnemyState* find(std::int32_t objectId) const
        noexcept;
    // CCinematicThread::IfEnemyDead (0x003700e0) ignores the serialized
    // IDEnemy on an object thread and tests that thread's bound enemy. Basic
    // and other thread types resolve the explicit attribute instead.
    [[nodiscard]] bool cinematicEnemyDead(
        const CinematicThread& thread,
        const CinematicCommand& command) const noexcept;
    [[nodiscard]] const LevelEnemyState* shownHealthBarEnemy() const noexcept;
    [[nodiscard]] const QuickTimeActionRuntime& rhinoQuickTimeAction() const
        noexcept {
        return rhinoQuickTimeAction_;
    }
    [[nodiscard]] std::optional<std::array<float, 16>>
    rhinoQuickTimePlayerWorldTransform() const;
    [[nodiscard]] std::optional<assets::Vector3>
    rhinoQuickTimePlayerFacing() const noexcept;
    [[nodiscard]] std::optional<assets::Vector3>
    rhinoQuickTimePlayerDetachPosition() const;

private:
    [[nodiscard]] LevelEnemyState* findMutable(std::int32_t objectId) noexcept;
    [[nodiscard]] static bool isInAir(
        const LevelEnemyState& enemy) noexcept;
    [[nodiscard]] assets::Vector3 playerHitEffectOrigin(
        const LevelEnemyState& enemy) const;
    [[nodiscard]] bool registerMeleeEngager(LevelEnemyState& enemy) noexcept;
    void unregisterMeleeEngager(std::int32_t objectId) noexcept;
    [[nodiscard]] float maximumAttackReach(
        const LevelEnemyState& enemy) const noexcept;
    void queueAuthoredAttackEvents(LevelEnemyState& enemy,
                                   std::uint32_t previousTimeMilliseconds,
                                   const assets::Vector3& playerPosition,
                                   const assets::Vector3&
                                       playerRangeTargetPosition,
                                   std::int32_t playerSenseReactState);
    void updateGunLines(std::uint32_t elapsedMilliseconds,
                        const assets::Vector3& playerPosition,
                        const LevelCollision* collision) noexcept;
    void updateMolotovs(std::uint32_t elapsedMilliseconds,
                        const assets::Vector3& playerPosition,
                        const LevelCollision* collision) noexcept;
    void updateBoomerangs(std::uint32_t elapsedMilliseconds,
                          const assets::Vector3& playerPosition,
                          const LevelCollision* collision) noexcept;
    void updatePlayerWebPellets(std::uint32_t elapsedMilliseconds,
                                const LevelCollision* collision) noexcept;
    void resolveEnemyContacts(const LevelCollision* collision) noexcept;
    void startGunLineAttack(LevelEnemyState& enemy);
    void startMolotovAttack(LevelEnemyState& enemy);
    void throwMolotov(LevelEnemyState& enemy,
                      const assets::Vector3& playerPosition,
                      std::uint32_t authoredEventTimeMilliseconds);
    void startMeleeAttack(LevelEnemyState& enemy,
                          const assets::Vector3& playerPosition);
    void startSandmanGroundAttack(LevelEnemyState& enemy);
    void startSandmanJump(LevelEnemyState& enemy,
                          const assets::Vector3& playerPosition,
                          const LevelCollision* collision);
    void updateSandmanBoss(LevelEnemyState& enemy,
                           std::uint32_t elapsedMilliseconds,
                           const assets::Vector3& playerPosition,
                           const LevelCollision* collision);
    void updateRhinoBoss(LevelEnemyState& enemy,
                         std::uint32_t elapsedMilliseconds,
                         const assets::Vector3& playerPosition,
                         const LevelCollision* collision,
                         bool quickTimeActionPressed);
    void updateRobotPhantomBoss(LevelEnemyState& enemy,
                                std::uint32_t elapsedMilliseconds,
                                const assets::Vector3& playerPosition,
                                const assets::Vector3& playerFacing,
                                const LevelCollision* collision);
    void enterRobotPhantomTask(LevelEnemyState& enemy,
                               RobotPhantomTaskState task);
    void advanceRobotPhantomSequence(LevelEnemyState& enemy);
    void throwRobotPhantomBoomerang(
        LevelEnemyState& enemy,
        const assets::Vector3& playerPosition);
    [[nodiscard]] assets::Vector3 robotPhantomHandPosition(
        const LevelEnemyState& enemy) const;
    void updateElectroBoss(LevelEnemyState& enemy,
                           std::uint32_t elapsedMilliseconds,
                           const assets::Vector3& playerPosition,
                           const LevelCollision* collision);
    void enterElectroTask(LevelEnemyState& enemy,
                          ElectroBossTaskState task);
    void advanceElectroSequence(
        LevelEnemyState& enemy,
        const assets::Vector3& playerPosition);
    void configureElectroDash(
        LevelEnemyState& enemy,
        const assets::Vector3& playerPosition);
    void launchElectroThunderclap(
        LevelEnemyState& enemy,
        const assets::Vector3& playerPosition);
    void updateThunderclaps(
        std::uint32_t elapsedMilliseconds,
        const assets::Vector3& playerPosition) noexcept;
    void activateElectroPosts(LevelEnemyState& enemy);
    void updateElectroPosts(LevelEnemyState& enemy,
                            std::uint32_t elapsedMilliseconds,
                            const assets::Vector3& playerPosition) noexcept;
    void removeElectroPosts(std::int32_t sourceObjectId) noexcept;
    void spawnElectroBurst(const LevelEnemyState& enemy,
                           const assets::Vector3& position, float scale);
    void updateElectroBursts(std::uint32_t elapsedMilliseconds) noexcept;
    void updateLandingAnimatedEffects(
        std::uint32_t elapsedMilliseconds) noexcept;
    [[nodiscard]] bool isElectroBoss(
        const LevelEnemyState& enemy) const noexcept;
    void enterRhinoTask(LevelEnemyState& enemy,
                        RhinoBossTaskState task);
    void applyRhinoDamage(LevelEnemyState& enemy, float damage) noexcept;
    void applyCombatDamage(LevelEnemyState& enemy, float damage,
                           std::int16_t hitType = 100,
                           const assets::Vector3* sourcePosition = nullptr,
                           float horizontalForce = 0.0F,
                           float verticalForce = 0.0F) noexcept;
    void enterHurtState(LevelEnemyState& enemy,
                        std::int16_t stateId) noexcept;
    [[nodiscard]] bool hasHurtStateAnimation(
        const LevelEnemyState& enemy, std::int16_t stateId) const noexcept;
    void cancelRhinoQuickTimeIfOwned(
        const LevelEnemyState& enemy) noexcept;
    void finishRhinoThrow(LevelEnemyState& enemy);
    [[nodiscard]] bool alignRhinoThrowCatch(
        LevelEnemyState& enemy,
        const assets::Vector3& playerPosition);
    [[nodiscard]] bool isGunLineEnemy(
        const LevelEnemyState& enemy) const noexcept;
    [[nodiscard]] bool isMolotovEnemy(
        const LevelEnemyState& enemy) const noexcept;
    void queueStateSound(LevelEnemyState& enemy,
                         std::string_view behaviorStateName);
    void selectStateAnimation(LevelEnemyState& enemy,
                              std::string_view behaviorStateName,
                              bool loop);
    void attachEnemyToWall(LevelEnemyState& enemy,
                           const LevelCollision& collision) noexcept;
    void updateWallEnemy(LevelEnemyState& enemy,
                          std::uint32_t elapsedMilliseconds,
                          const assets::Vector3& playerPosition,
                          const assets::Vector3& playerFacing,
                          bool playerOnWall, const LevelCollision* collision);
    void enterDeadState(LevelEnemyState& enemy);

    std::vector<LevelEnemyState> states_;
    std::vector<EnemyPlayerHit> pendingPlayerHits_;
    std::vector<EnemySoundCue> pendingSoundCues_;
    std::vector<EnemyCameraShakeCue> pendingCameraShakeCues_;
    std::vector<EnemyGunLineState> gunLines_;
    std::vector<EnemyMolotovState> molotovs_;
    std::vector<EnemyBoomerangState> boomerangs_;
    std::vector<PlayerWebPelletState> playerWebPellets_;
    std::vector<EnemyThunderclapState> thunderclaps_;
    std::vector<EnemyElectricPostState> electricPosts_;
    std::vector<EnemyElectroBurstState> electroBursts_;
    std::vector<EnemyLandingAnimatedEffectState> landingAnimatedEffects_;
    std::vector<EnemyLandingAnimatedEffectSpawnEvent>
        pendingLandingAnimatedEffectSpawnEvents_;
    std::vector<EnemyProjectileEvent> pendingProjectileEvents_;
    std::vector<EnemyEffectCue> pendingEffectCues_;
    std::vector<PlayerWebPelletEvent> pendingPlayerWebPelletEvents_;
    std::vector<EnemySeparationEvent> pendingEnemySeparationEvents_;
    const LevelOneBootstrap* level_{};
    std::optional<std::int32_t> shownHealthBarObjectId_;
    std::int32_t meleeEngagerObjectId_{-1};
    float meleeEngagementCooldownMilliseconds_{};
    // BehaviorStateFile stores these two mutable indices directly in each
    // shared s_behavior_stateInfo (+0x10/+0x14). They are state-global, not
    // per-enemy cursors.
    std::vector<std::int32_t> behaviorStateListCursors_;
    std::vector<std::int32_t> behaviorStateAnimationCursors_;
    NativeRandomizer ownedNativeRandomizer_;
    NativeRandomizer* nativeRandomizer_{&ownedNativeRandomizer_};
    QuickTimeActionRuntime rhinoQuickTimeAction_;
    std::int32_t rhinoQuickTimeEnemyId_{-1};
};

} // namespace usm::game
