#pragma once

#include "assets/IrrScene.hpp"
#include "core/Result.hpp"
#include "game/CinematicCamera.hpp"
#include "game/CinematicScript.hpp"
#include "game/LevelOneBootstrap.hpp"
#include "game/LevelSlideRuntime.hpp"
#include "game/NativeRandomizer.hpp"
#include "game/PlayerStateConfig.hpp"
#include "game/WebGrabPointRuntime.hpp"
#include "game/WebSwingRuntime.hpp"
#include "game/WallWebRuntime.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <span>
#include <vector>

namespace usm::game {

class LevelCollision;

struct PlayerMotionInput {
    float right{};
    float forward{};
};

enum class PlayerButtonPhase {
    Pressed,
    Held,
    Released,
};

enum class PlayerInputAction {
    None,
    Jump,
    Punch,
    Web,
};

struct PlayerMeleeImpact {
    std::uint16_t stateId{};
    std::string_view stateName;
    assets::Vector3 attackPosition;
    assets::Vector3 attackDirection{1.0F, 0.0F, 0.0F};
    float damage{};
    float maximumReach{};
    float minimumAngleDegrees{};
    float maximumAngleDegrees{};
    // Web pellets and binding states retain Player+0x594's acquired Unit
    // rather than resolving solely through a local cylinder test.
    std::int32_t targetedEnemyObjectId{-1};
    bool targetedDelivery{};
    bool webAttack{};
    // Motions 110, 123, and 124 are converted by CEnemy::
    // ParseLocalAiMessage into CBehaviorTiedUp message 0x69.
    bool bindsEnemy{};
    bool wallAttack{};
    assets::Vector3 wallNormal;
    // Ultimate wheel/explosion motions test every unit in their authored
    // 360-degree sector instead of stopping after the first contact.
    bool radialAttack{};
    bool ultimateAttack{};
    // State class 6 is the native Spider-Sense response family. Its contact
    // path tests the whole authored sector, guarantees the registered
    // attacker, and resets Spider-Sense slow motion at the hit frame.
    bool senseAttack{};
    // Motion 0x6d keeps the normal hit record for Player+0x594 but sends a
    // separate type-0x79, 200/500-force record to every other sector target.
    bool airKickDownSplit{};
    // Player::CheckAttackTarget (0x0034fca0) copies the active MC_STATE's
    // hit type and both authored force fields into AIHitTargetInfo.  They
    // drive the enemy hurt graph; damage alone is not a complete hit.
    std::int16_t hitType{100};
    float horizontalForce{};
    float verticalForce{};
    // Player::SendHitMessage decides AddWebPower eligibility synchronously
    // while this source state is still current. Application consumes the
    // portable impact after update(), so preserve that contact-time decision.
    bool powerRestoreBlocked{};
};

struct PlayerWebPelletLaunch {
    std::uint16_t stateId{};
    std::string_view stateName;
    assets::Vector3 origin;
    assets::Vector3 targetPosition;
    float damage{};
    std::int32_t targetedEnemyObjectId{-1};
};

struct PlayerEnemyNotifyEvent {
    std::uint16_t stateId{};
    std::string_view stateName;
    std::int32_t targetedEnemyObjectId{-1};
    std::int16_t hitType{};
    std::int16_t behaviorMessage{-1};
    std::int16_t behaviorState{-1};
};

struct PlayerHitEffectState {
    std::int16_t effectId{-1};
    std::uint32_t elapsedMilliseconds{};
    // CAnimObjEffect::IsAlive can use the selected animation duration while
    // Update fades against the separately supplied effect duration.
    std::uint32_t lifetimeMilliseconds{};
    std::uint32_t fadeDurationMilliseconds{};
    std::string_view spawnAnimation;
    std::uint32_t spawnAnimationMilliseconds{};
    std::array<float, 16> spawnPlayerWorldTransform{};
    // Player::AddHitEffect copies PhysicsEntity velocity into live
    // (non-snapshot) CAnimObjEffect instances at their exact spawn frame.
    assets::Vector3 capturedPhysicsVelocity;
    assets::Vector3 driftOffset;
    bool followsPlayerBone{};
    std::string_view boneNameOverride;
    float uniformScale{1.0F};
    bool fadeWithLifetime{true};
    // CAnimObjEffect::Init selects custom material 0x1d when true and 0x1e
    // otherwise. Application::Init registers those IDs as
    // ADDITIVE_MODULATE_NONTRANSPARENT and
    // TRANSPARENT_ALPHA_CHANNEL_WITH_VERTEX_ALPHA respectively.
    bool additiveModulateMaterial{};
};

struct PlayerHitEffectSpawnEvent {
    std::int16_t effectId{-1};
    std::string_view effectName;
    std::string_view boneName;
    std::uint32_t lifetimeMilliseconds{};
    std::uint32_t fadeDurationMilliseconds{};
    float uniformScale{1.0F};
    bool followsPlayerBone{};
    bool additiveModulateMaterial{};
    assets::Vector3 capturedPhysicsVelocity;
};

struct PlayerCombatEffectEvent {
    std::string_view effectType;
    assets::Vector3 origin;
    std::int16_t voxSoundId{-1};
};

struct PlayerVoxStopEvent {
    std::uint16_t stateId{};
    std::string_view stateName;
    std::int16_t voxSoundId{-1};
};

struct PlayerAttackSoundTrigger {
    std::int16_t soundConfigId{-1};
    std::size_t emitterIndex{};
    std::string_view stateName;
};

struct PlayerAttackTarget {
    assets::Vector3 position;
    float collisionRadius{};
    std::int32_t objectId{-1};
    bool airborne{};
    float collisionHeight{};
    bool canBeTiedUp{};
    bool canBeDraggedTo{};
    bool onWall{};
    bool canEnterWallWeb{};
    // Player::SetNextStateId motion 0x6c aims at the target's live
    // Bip01_Head node rather than its physics-entity base.
    std::optional<assets::Vector3> headPosition;
    // CEnemy::IsNearAttackKeyFrame (0x00334dd0) gates the red/black
    // Spider-Sense blink strike separately from ordinary directional sense.
    bool nearAttackKeyFrame{};
    std::int32_t senseReactionType{1};
    bool canBeCounterHit{true};
    std::int16_t enemySubType{-1};
};

// Portable, renderer-independent reconstruction of the normal ground movement
// path in Player::GetJoyStickDirToGameDir (0x00343a64), Player::moveForward
// (0x00341524), and Player::UpdateMCSpeed (0x00346f50).
class GameplayPlayer final {
public:
    [[nodiscard]] Result initialize(const LevelPlayerAsset& asset,
                                    const LevelCollision* collision = nullptr,
                                    const PlayerStateConfigDatabase* states =
                                        nullptr,
                                    std::span<const LevelWebGrabPointAsset>
                                        webGrabPoints = {},
                                    std::span<const LevelSlideAsset> slides = {},
                                    std::span<const LevelWayPointAsset>
                                        waypoints = {},
                                    const ButtonConfigDatabase* buttons = nullptr,
                                    const PlayerHitEffectConfigDatabase*
                                        hitEffects = nullptr,
                                    std::span<const PlayerHitEffectAsset>
                                        hitEffectAssets = {},
                                    NativeRandomizer* nativeRandomizer =
                                        nullptr);
    void setWallWebInput(bool pressed, bool targetAlive) noexcept {
        wallWebActionPressed_ = pressed;
        wallWebTargetAlive_ = targetAlive;
    }
    [[nodiscard]] const WallWebRuntime& wallWeb() const noexcept { return wallWeb_; }
    [[nodiscard]] std::vector<WallWebEvent> consumeWallWebEvents() {
        return wallWeb_.consumeEvents();
    }
    [[nodiscard]] bool requestPunch(
        const std::optional<PlayerAttackTarget>& target = std::nullopt,
        const std::optional<assets::Vector3>& directionalInput =
            std::nullopt,
        PlayerButtonPhase phase = PlayerButtonPhase::Pressed) noexcept;
    [[nodiscard]] bool requestJump() noexcept;
    [[nodiscard]] bool requestJump(const PlayerMotionInput& input,
                                   const CameraPose& camera,
                                   PlayerButtonPhase phase =
                                       PlayerButtonPhase::Pressed) noexcept;
    [[nodiscard]] bool requestWeb(
        const std::optional<PlayerAttackTarget>& target = std::nullopt,
        const std::optional<assets::Vector3>& directionalInput =
            std::nullopt,
        PlayerButtonPhase phase = PlayerButtonPhase::Pressed,
        const CameraPose* camera = nullptr) noexcept;
    [[nodiscard]] bool requestUltimate() noexcept;
    [[nodiscard]] bool requestSpiderSense(
        const PlayerAttackTarget& attacker) noexcept;
    // Player::UpdateSpiderSense shows its warning only when both
    // CheckCanDoAction(k_state_sense_avoid_front) and
    // CanEnableSpiderSense succeed.
    [[nodiscard]] bool canDisplaySpiderSense() const noexcept;
    // Player::UpdateKeyTrigger (0x0034d0a4) scans the current state's
    // serialized transition rows and lets later qualifying rows replace the
    // pending state. Use this before dispatching simultaneous face buttons;
    // a single global button order cannot reproduce the authored tables.
    [[nodiscard]] PlayerInputAction preferredInputAction(
        const std::optional<PlayerButtonPhase>& jump,
        const std::optional<PlayerButtonPhase>& punch,
        const std::optional<PlayerButtonPhase>& web,
        const std::optional<PlayerAttackTarget>& punchTarget =
            std::nullopt) const noexcept;
    void setWebGrabViewContext(
        const CameraPose& camera, float aspectRatio,
        std::span<const bool> roomVisibility) noexcept {
        webGrabPointRuntime_.setViewContext(camera, aspectRatio,
                                            roomVisibility);
    }
    [[nodiscard]] std::vector<WebGrabCandidateDiagnostics>
    webGrabCandidateDiagnostics() const;
    [[nodiscard]] std::int32_t availableWebGrabPointObjectId() const noexcept;
    [[nodiscard]] bool releaseWeb() noexcept;
    [[nodiscard]] bool applyDamage(
        float damage, std::int32_t damageType = 0,
        std::uint32_t minimumReactionMilliseconds = 0,
        std::int32_t nativeHitType = 100) noexcept;
    void addHealth(float health) noexcept;
    void addSkillPoints(std::int32_t points) noexcept;
    // Player::AddCombo/UpdateComboState (0x00340584/0x003458d0) keep the
    // score separate from skill points. Damage is the target's actual health
    // delta recovered from Player::SendHitMessage (0x00345fe8).
    void addCombo(float actualDamage, bool ultimateActive,
                  std::uint64_t timeMilliseconds,
                  std::optional<bool> powerRestoreAllowedAtContact =
                      std::nullopt) noexcept;
    void updateComboState(std::uint64_t timeMilliseconds) noexcept;
    void setComboScore(std::int32_t score) noexcept;
    void restoreAt(const assets::Vector3& position,
                   const assets::Vector3& facing) noexcept;
    void restartAt(const assets::Vector3& position,
                   const assets::Vector3& facing) noexcept;
    void loadCheckPointAt(const assets::Vector3& position,
                          const assets::Vector3& facing) noexcept;
    void applySupportingBodyMotion(
        const assets::Vector3& delta) noexcept;
    [[nodiscard]] Result applyCinematicCommand(
        const CinematicThread& thread, const CinematicCommand& command);
    [[nodiscard]] std::optional<bool> slideEnabled(
        std::int32_t objectId) const noexcept {
        return slideRuntime_.enabled(objectId);
    }
    void update(const PlayerMotionInput& input, const CameraPose& camera,
                std::uint32_t elapsedMilliseconds) noexcept;
    // Native Player::PreUpdate advances the animated object and
    // UpdateStateFrame (0x00341fa4) snapshots its new frame before
    // UpdateKeyTrigger (0x0034d0a4) examines controller edges. The portable
    // application dispatches input before update(), so expose the pending
    // game delta to transition predicates without advancing a newly entered
    // attack or locomotion state on the same tick.
    void prepareInputFrame(std::uint32_t elapsedMilliseconds) noexcept {
        inputFrameAdvanceMilliseconds_ = elapsedMilliseconds;
        inputFramePrepared_ = true;
        attackEnteredDuringPreparedInputFrame_ = false;
        locomotionEnteredDuringPreparedInputFrame_ = false;
    }
    // Native Player retains a live Unit* at +0x594 throughout linked attack
    // states. Refresh the portable value from the enemy runtime each frame so
    // homing attacks and web anchors follow knockback instead of a press-time
    // position snapshot.
    [[nodiscard]] std::int32_t trackedAttackTargetObjectId() const noexcept;
    void refreshTrackedAttackTarget(
        const std::optional<PlayerAttackTarget>& target) noexcept;
    [[nodiscard]] std::optional<assets::Vector3> attackDirection(
        const PlayerMotionInput& input,
        const CameraPose& camera) const noexcept;
    [[nodiscard]] std::optional<PlayerMeleeImpact>
    consumeMeleeImpact() noexcept;
    void notifyMeleeImpactAccepted(std::uint16_t stateId) noexcept;
    [[nodiscard]] std::optional<PlayerWebPelletLaunch>
    consumeWebPelletLaunch() noexcept;
    [[nodiscard]] std::optional<PlayerEnemyNotifyEvent>
    consumeEnemyNotifyEvent() noexcept;
    [[nodiscard]] std::span<const PlayerHitEffectState> hitEffects() const
        noexcept {
        return activeHitEffects_;
    }
    [[nodiscard]] std::optional<PlayerHitEffectSpawnEvent>
    consumeHitEffectSpawn() noexcept;
    [[nodiscard]] std::optional<PlayerCombatEffectEvent>
    consumeCombatEffect() noexcept;
    [[nodiscard]] std::optional<PlayerVoxStopEvent>
    consumeVoxStopEvent() noexcept;
    [[nodiscard]] std::optional<PlayerAttackSoundTrigger>
    consumeAttackSoundTrigger() noexcept;
    [[nodiscard]] std::string_view consumeEnteredState() noexcept;
    void setQuickTimeActionPose(std::string_view animation,
                                std::uint32_t animationMilliseconds,
                                bool loop,
                                const std::array<float, 16>* worldTransform =
                                    nullptr,
                                const assets::Vector3* facing =
                                    nullptr,
                                const assets::Vector3* detachPosition =
                                    nullptr) noexcept;
    void clearQuickTimeActionPose() noexcept;
    [[nodiscard]] Result enterScriptedState(
        std::uint16_t stateId, bool loop,
        const assets::Vector3* facing = nullptr);
    void clearScriptedState() noexcept;

    [[nodiscard]] const assets::Vector3& position() const noexcept {
        return position_;
    }
    [[nodiscard]] const assets::Vector3& renderPosition() const noexcept {
        return renderPosition_;
    }
    [[nodiscard]] const assets::Vector3& attackRootTranslation() const noexcept {
        return attackRootTranslation_;
    }
    [[nodiscard]] const assets::Vector3& facing() const noexcept {
        return facing_;
    }
    [[nodiscard]] const std::array<float, 16>& worldTransform() const noexcept {
        return worldTransform_;
    }
    [[nodiscard]] std::string_view activeAnimation() const noexcept {
        return activeAnimation_;
    }
    [[nodiscard]] std::uint32_t animationTimeMilliseconds() const noexcept;
    [[nodiscard]] std::uint32_t attackTimelineMilliseconds() const noexcept;
    [[nodiscard]] std::uint32_t activeAnimationDurationMilliseconds() const
        noexcept;
    [[nodiscard]] float health() const noexcept { return health_; }
    [[nodiscard]] float maximumHealth() const noexcept { return maximumHealth_; }
    [[nodiscard]] float webPower() const noexcept { return webPower_; }
    [[nodiscard]] float maximumWebPower() const noexcept {
        return maximumWebPower_;
    }
    [[nodiscard]] std::int32_t skillPoints() const noexcept {
        return skillPoints_;
    }
    [[nodiscard]] std::int32_t comboScore() const noexcept {
        return comboScore_;
    }
    [[nodiscard]] std::int32_t pendingComboCount() const noexcept {
        return normalComboCount_ + ultimateComboCount_;
    }
    [[nodiscard]] std::int32_t maximumComboCount() const noexcept {
        return maximumComboCount_;
    }
    [[nodiscard]] std::int32_t completedComboHitCount() const noexcept {
        return completedComboHitCount_;
    }
    [[nodiscard]] bool dead() const noexcept { return health_ <= 0.0F; }
    [[nodiscard]] bool cinematicDriven() const noexcept {
        return cinematicDriven_;
    }
    [[nodiscard]] bool cinematicMotionActive() const noexcept {
        return cinematicMotion_.active;
    }
    [[nodiscard]] std::uint32_t cinematicMotionElapsedMilliseconds()
        const noexcept {
        return cinematicMotion_.elapsedMilliseconds;
    }
    [[nodiscard]] std::uint32_t cinematicMotionDurationMilliseconds()
        const noexcept {
        return cinematicMotion_.durationMilliseconds;
    }
    [[nodiscard]] bool quickTimeActionDriven() const noexcept {
        return quickTimeActionDriven_;
    }
    [[nodiscard]] bool scriptedStateDriven() const noexcept {
        return activeScriptedState_ != nullptr;
    }
    [[nodiscard]] bool scriptedStateAnimationFinished() const noexcept;
    [[nodiscard]] bool airborne() const noexcept;
    [[nodiscard]] bool onWall() const noexcept;
    [[nodiscard]] SlideCatch slideCatchCandidate() const noexcept;
    [[nodiscard]] bool slideActive() const noexcept {
        return slideRuntime_.active();
    }
    [[nodiscard]] std::uint16_t activeStateId() const noexcept;
    [[nodiscard]] std::string_view activeStateName() const noexcept;
    [[nodiscard]] std::int32_t senseReactState() const noexcept;
    [[nodiscard]] bool punchTransitionReadyAfterImpact() const noexcept;
    [[nodiscard]] bool punchAttackTransitionReady() const noexcept;
    [[nodiscard]] bool jumpAttackTransitionReady() const noexcept;
    [[nodiscard]] bool jumpReleaseAttackTransitionReady() const noexcept;
    [[nodiscard]] bool webAttackTransitionReady() const noexcept;
    [[nodiscard]] bool webHeldAttackTransitionReady() const noexcept;
    [[nodiscard]] bool combatTransitionReady() const noexcept;
    [[nodiscard]] float animatedFootHeight() const noexcept;
    [[nodiscard]] bool webLineActive() const noexcept;
    [[nodiscard]] std::size_t webLineCount() const noexcept;
    [[nodiscard]] bool canEnableTriggerRestore() const noexcept;
    [[nodiscard]] assets::Vector3 webLineAnchor() const noexcept;
    [[nodiscard]] assets::Vector3 webLineAttachPosition(
        std::size_t lineIndex = 0) const noexcept;
    [[nodiscard]] const assets::Vector3& webLineOrientation() const noexcept {
        return webLineOrientation_;
    }
    [[nodiscard]] std::int32_t webLineTargetObjectId() const noexcept;
    [[nodiscard]] std::string_view lastActionRejectionReason() const noexcept {
        return lastActionRejectionReason_;
    }
    [[nodiscard]] std::uint32_t hurtReactionRemainingMilliseconds() const
        noexcept {
        return hurtReactionRemainingMilliseconds_;
    }

private:
    struct CinematicMotionState {
        assets::Vector3 startPosition;
        assets::Vector3 endPosition;
        assets::Quaternion startRotation;
        assets::Quaternion endRotation;
        std::uint32_t elapsedMilliseconds{};
        std::uint32_t durationMilliseconds{};
        bool active{};
    };

    enum class LocomotionState {
        Grounded,
        JumpStart,
        JumpFall,
        ShortWebJump,
        SustainedFall,
        JumpLand,
        WebThrow,
        SwingHang,
        SwingRelease,
        SliderLand,
        SliderMove,
        SliderJumpUp,
        SliderJumpFall,
        WallAttach,
        WallIdle,
        WallMove,
        WallExit,
        WallJump,
    };

    void setAnimation(std::string_view animation) noexcept;
    void queueEnteredState(std::string_view stateName) noexcept;
    [[nodiscard]] bool enterAttackState(
        const PlayerStateDefinition& state) noexcept;
    [[nodiscard]] const PlayerStateDefinition* attackStateForTarget(
        const PlayerStateDefinition& requested,
        const std::optional<PlayerAttackTarget>& target) const noexcept;
    [[nodiscard]] assets::Vector3 animationPhysicalDisplacement(
        const PlayerStateDefinition* state,
        std::uint32_t localMilliseconds) const noexcept;
    [[nodiscard]] assets::Vector3 animationRenderOffset(
        const PlayerStateDefinition* state,
        std::uint32_t localMilliseconds) const noexcept;
    [[nodiscard]] assets::Vector3 animationPhysicalDisplacement(
        const assets::ColladaAnimationClip* clip,
        std::uint32_t localMilliseconds) const noexcept;
    [[nodiscard]] assets::Vector3 animationRenderOffset(
        const assets::ColladaAnimationClip* clip,
        std::uint32_t localMilliseconds) const noexcept;
    [[nodiscard]] bool queueAttackTransition(
        const PlayerStateDefinition& state,
        const std::optional<PlayerAttackTarget>& target,
        bool airborne) noexcept;
    [[nodiscard]] bool enterQueuedAttackTransition() noexcept;
    [[nodiscard]] bool switchToNextAttackLinkAnimation() noexcept;
    void applyAttackRootMotion(
        const assets::Vector3& physicalDisplacement,
        const assets::Vector3& renderOffset) noexcept;
    void applyFlyKickHomingMotion(
        std::uint32_t elapsedMilliseconds) noexcept;
    void applyAirKnockdownApproachMotion(
        std::uint32_t elapsedMilliseconds,
        std::uint32_t animationDurationMilliseconds) noexcept;
    void applyAirAttackPursuitMotion(
        std::uint32_t elapsedMilliseconds) noexcept;
    void applyAirRootMotion(
        const assets::Vector3& physicalDisplacement,
        const assets::Vector3& renderOffset) noexcept;
    void cancelAttack() noexcept;
    void enterDeadState() noexcept;
    void updateAttack(std::uint32_t elapsedMilliseconds) noexcept;
    void queueAttackFrameEvents(std::uint32_t previousMilliseconds,
                                std::uint32_t currentMilliseconds) noexcept;
    void queueAttackSoundEvents(std::uint32_t previousMilliseconds,
                                std::uint32_t currentMilliseconds) noexcept;
    void queueHitEffect(std::int16_t effectId,
                        std::uint32_t elapsedMilliseconds = 0,
                        std::uint32_t lifetimeOverrideMilliseconds = 0,
                        std::string_view boneNameOverride = {},
                        float uniformScale = 1.0F,
                        bool additiveModulateMaterial = false) noexcept;
    void queueSpecialAttackEffects(std::uint32_t previousMilliseconds,
                                   std::uint32_t currentMilliseconds) noexcept;
    [[nodiscard]] const PlayerStateDefinition*
    punchTransition(
        PlayerButtonPhase phase = PlayerButtonPhase::Pressed,
        const std::optional<PlayerAttackTarget>& target =
            std::nullopt) const noexcept;
    [[nodiscard]] const PlayerStateDefinition*
    webTransition(PlayerButtonPhase phase =
                      PlayerButtonPhase::Pressed) const noexcept;
    [[nodiscard]] const PlayerStateDefinition*
    transitionForButton(std::int16_t button,
                        std::span<const std::int16_t> predicates) const noexcept;
    [[nodiscard]] bool attackInputWindowOpen() const noexcept;
    [[nodiscard]] bool locomotionInputWindowOpen() const noexcept;
    [[nodiscard]] bool canEnableSpiderSense() const noexcept;
    [[nodiscard]] float spellMagicForState(
        const PlayerStateDefinition& state) const noexcept;
    [[nodiscard]] bool canAffordState(
        const PlayerStateDefinition& state) const noexcept;
    [[nodiscard]] bool powerRestoreBlocked() const noexcept;
    void addWebPower(float amount) noexcept;
    void updateWebPowerRestore(std::uint32_t elapsedMilliseconds) noexcept;
    void enterLocomotionState(LocomotionState state) noexcept;
    void updateJump(const PlayerMotionInput& input, const CameraPose& camera,
                    std::uint32_t elapsedMilliseconds) noexcept;
    void updateAirHorizontalMotion(const PlayerMotionInput& input,
                                   const CameraPose& camera,
                                   std::uint32_t elapsedMilliseconds) noexcept;
    void updateWebTraversal(const PlayerMotionInput& input,
                            const CameraPose& camera,
                            std::uint32_t elapsedMilliseconds) noexcept;
    void enterSwingHang(const CameraPose& camera) noexcept;
    void enterSwingRelease() noexcept;
    [[nodiscard]] bool tryCatchSlide() noexcept;
    void updateSlideTraversal(std::uint32_t elapsedMilliseconds) noexcept;
    [[nodiscard]] bool tryAttachWall(
        const assets::Vector3& movement) noexcept;
    [[nodiscard]] bool tryAttachSwingReleaseWall(
        const assets::Vector3& start,
        const assets::Vector3& desired,
        const assets::Vector3& velocity) noexcept;
    void updateWallTraversal(const PlayerMotionInput& input,
                             std::uint32_t elapsedMilliseconds) noexcept;
    [[nodiscard]] assets::Vector3 wallRootTranslation(
        const PlayerStateDefinition* state,
        std::uint32_t localMilliseconds) const noexcept;
    [[nodiscard]] assets::Vector3 wallRootWorldDelta(
        const assets::Vector3& localDelta) const noexcept;
    [[nodiscard]] bool reacquireWallAt(
        const assets::Vector3& candidate) noexcept;
    [[nodiscard]] float currentRootHeight() const noexcept;
    [[nodiscard]] bool findLandingHeight(float referenceHeight,
                                         float& height) const noexcept;
    void updateWorldTransform(const assets::Vector3& facing) noexcept;

    assets::Vector3 position_;
    assets::Vector3 renderPosition_;
    float jumpAnchorHeight_{};
    std::array<float, 16> worldTransform_{};
    assets::Vector3 scale_{1.0F, 1.0F, 1.0F};
    assets::Vector3 facing_{1.0F, 0.0F, 0.0F};
    std::string_view activeAnimation_{"idle_stand"};
    std::uint64_t animationTimeMilliseconds_{};
    const LevelCollision* collision_{};
    const assets::ColladaMeshFile* mesh_{};
    const assets::ColladaAnimationFile* animationBank_{};
    const assets::AnimationDisplacement* animationDisplacement_{};
    const PlayerStateConfigDatabase* stateDatabase_{};
    const PlayerHitEffectConfigDatabase* hitEffectDatabase_{};
    std::span<const PlayerHitEffectAsset> hitEffectAssets_;
    const PlayerStateDefinition* initialPunchState_{};
    const PlayerStateDefinition* farPunchState_{};
    const PlayerStateDefinition* groundWebShotState_{};
    const PlayerStateDefinition* groundWebDragDownState_{};
    const PlayerStateDefinition* groundWebFlyKickState_{};
    const PlayerStateDefinition* airKnockdownState_{};
    const PlayerStateDefinition* airWebBindGroundState_{};
    const PlayerStateDefinition* airDoubleWebBindState_{};
    const PlayerStateDefinition* airPunchState_{};
    const PlayerStateDefinition* airTargetKickState_{};
    const PlayerStateDefinition* ultimatePrepareState_{};
    std::array<const PlayerStateDefinition*, 4> senseAvoidStates_{};
    std::array<const PlayerStateDefinition*, 4> senseAttackStates_{};
    const PlayerStateDefinition* senseBlinkRedState_{};
    const PlayerStateDefinition* senseBlinkBlackState_{};
    NativeRandomizer ownedNativeRandomizer_;
    NativeRandomizer* nativeRandomizer_{&ownedNativeRandomizer_};
    const PlayerStateDefinition* jumpStartState_{};
    const PlayerStateDefinition* jumpFallState_{};
    const PlayerStateDefinition* shortWebJumpState_{};
    const PlayerStateDefinition* sustainedFallState_{};
    const PlayerStateDefinition* jumpLandState_{};
    const PlayerStateDefinition* swingThrowState_{};
    const PlayerStateDefinition* swingHangState_{};
    const PlayerStateDefinition* swingIdleState_{};
    const PlayerStateDefinition* sliderLandState_{};
    const PlayerStateDefinition* sliderMoveState_{};
    const PlayerStateDefinition* sliderJumpUpState_{};
    const PlayerStateDefinition* sliderJumpFallState_{};
    const PlayerStateDefinition* wallIdleState_{};
    const PlayerStateDefinition* wallMoveState_{};
    const PlayerStateDefinition* wallAttachState_{};
    const PlayerStateDefinition* wallExitState_{};
    const PlayerStateDefinition* wallJumpUpState_{};
    const PlayerStateDefinition* wallJumpDownState_{};
    const PlayerStateDefinition* wallJumpLeftState_{};
    const PlayerStateDefinition* wallJumpRightState_{};
    const PlayerStateDefinition* hurtLightState_{};
    const PlayerStateDefinition* hurtHeavyState_{};
    const PlayerStateDefinition* deadOverState_{};
    const PlayerStateDefinition* activeLocomotionState_{};
    LocomotionState locomotionState_{LocomotionState::Grounded};
    float verticalVelocityCentimetersPerSecond_{};
    assets::Vector3 swingReleaseVelocity_;
    assets::Vector3 swingReleaseSteeringVelocity_;
    assets::Vector3 swingReleaseDecayVelocity_;
    bool sustainedFallCarriesSwingVelocity_{};
    bool swingReleaseHasTarget_{};
    assets::Vector3 swingReleaseTarget_;
    assets::Vector3 wallNormal_;
    assets::Vector3 wallStateStartPosition_;
    PlayerMotionInput lastWallInput_;
    WebGrabPointRuntime webGrabPointRuntime_;
    WebSwingRuntime webSwingRuntime_;
    // Player::StartWebSwing (0x00348592-0x003485c6) stores the negated camera
    // look direction in CTexLineSceneNode exactly once when it allocates the
    // CobWeb. The ribbon therefore retains this orientation during the swing.
    assets::Vector3 webLineOrientation_{0.0F, 0.0F, -1.0F};
    WallWebRuntime wallWeb_;
    const ButtonConfigDatabase* buttonDatabase_{};
    // Dominant W/S/A/D direction synthesized by native
    // GameEventKeyWrap::VisualKeyPressed. A direction change is a fresh
    // press of virtual movement event zero, including during attacks.
    std::int32_t movementVirtualKey_{-1};
    bool wallWebActionPressed_{};
    bool wallWebTargetAlive_{true};
    LevelSlideRuntime slideRuntime_;
    assets::Vector3 slideJumpVelocity_;
    assets::Vector3 slideJumpSideVelocity_;
    std::int32_t slideJumpUpAnimationId_{141};
    std::int32_t slideJumpFallAnimationId_{138};
    std::uint32_t slideMoveElapsedMilliseconds_{};
    const LevelWebGrabPointAsset* selectedWebGrabPoint_{};
    bool swingUsesLeftHand_{};
    bool webReleaseRequested_{};
    const PlayerStateDefinition* activeAttackState_{};
    std::optional<PlayerAttackTarget> activeAttackTarget_;
    bool activeAttackAirborne_{};
    const PlayerStateDefinition* queuedAttackState_{};
    std::optional<PlayerAttackTarget> queuedAttackTarget_;
    bool queuedAttackAirborne_{};
    std::size_t nextAttackLinkAnimationIndex_{};
    std::uint64_t attackTimelineMilliseconds_{};
    std::uint32_t inputFrameAdvanceMilliseconds_{};
    bool inputFramePrepared_{};
    bool attackEnteredDuringPreparedInputFrame_{};
    bool locomotionEnteredDuringPreparedInputFrame_{};
    bool hitEffectUpdateInProgress_{};
    std::uint32_t hitEffectFrameAdvanceMilliseconds_{};
    bool ultimateActive_{};
    assets::Vector3 attackRootTranslation_;
    assets::Vector3 attackVisualRootTranslation_;
    assets::Vector3 attackPhysicsVelocity_;
    assets::Vector3 wallAttackDirection_;
    assets::Vector3 locomotionRootTranslation_;
    std::size_t nextAttackImpactFrameIndex_{};
    bool activeAttackContactAccepted_{};
    // UpdateAttacks has dedicated retained-target delivery for web motions
    // 0x7f/0x81/0x82.  These events do not use MC_STATE's ordinary impact
    // frame list, so keep their one-shot phase independently.
    std::uint8_t specialWebImpactPhase_{};
    bool combatWebLinesReleased_{};
    std::array<PlayerMeleeImpact, 8> pendingMeleeImpacts_{};
    std::size_t pendingMeleeImpactCount_{};
    std::array<PlayerWebPelletLaunch, 8> pendingWebPelletLaunches_{};
    std::size_t pendingWebPelletLaunchCount_{};
    std::array<PlayerEnemyNotifyEvent, 4> pendingEnemyNotifyEvents_{};
    std::size_t pendingEnemyNotifyEventCount_{};
    std::vector<PlayerHitEffectState> activeHitEffects_;
    std::array<PlayerHitEffectSpawnEvent, 16> pendingHitEffectSpawns_{};
    std::size_t pendingHitEffectSpawnCount_{};
    std::array<PlayerCombatEffectEvent, 4> pendingCombatEffects_{};
    std::size_t pendingCombatEffectCount_{};
    std::array<PlayerVoxStopEvent, 4> pendingVoxStopEvents_{};
    std::size_t pendingVoxStopEventCount_{};
    std::uint32_t nextUltimatePulseMilliseconds_{};
    std::uint32_t ultimatePhaseElapsedMilliseconds_{};
    std::uint32_t ultimatePhaseRemainingMilliseconds_{};
    bool ultimateOutEffectEmitted_{};
    bool ultimateSplashEmitted_{};
    std::array<PlayerAttackSoundTrigger, 16> pendingAttackSoundTriggers_{};
    std::size_t pendingAttackSoundTriggerCount_{};
    std::uint32_t hurtReactionRemainingMilliseconds_{};
    std::string_view lastActionRejectionReason_;
    std::array<std::string_view, 4> enteredStates_{};
    std::size_t enteredStateCount_{};
    float health_{1000.0F};
    float maximumHealth_{1000.0F};
    float webPower_{1000.0F};
    float maximumWebPower_{1000.0F};
    float webPowerRestoreDelayMilliseconds_{};
    std::int32_t skillPoints_{};
    std::int32_t normalComboCount_{};
    std::int32_t ultimateComboCount_{};
    float normalComboDamage_{};
    float ultimateComboDamage_{};
    std::int32_t maximumComboCount_{};
    std::int32_t completedComboHitCount_{};
    std::uint64_t comboTouchedMilliseconds_{};
    std::int32_t comboScore_{};
    bool forceComboFinish_{};
    std::int32_t objectId_{-1};
    bool cinematicDriven_{};
    bool cinematicAnimationLoops_{true};
    float cinematicAnimationSpeed_{1.0F};
    CinematicMotionState cinematicMotion_;
    bool quickTimeActionDriven_{};
    bool quickTimeActionLoops_{};
    assets::Vector3 quickTimeActionDetachPosition_{};
    bool quickTimeActionHasDetachPosition_{};
    const PlayerStateDefinition* activeScriptedState_{};
    bool scriptedStateLoops_{};
};

} // namespace usm::game
