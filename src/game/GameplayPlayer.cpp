#include "game/GameplayPlayer.hpp"

#include "assets/ColladaSkinning.hpp"
#include "game/LevelCollision.hpp"
#include "game/PlayerPhysicsConstants.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace usm::game {
namespace {

constexpr float kMaximumRunSpeedCentimetersPerSecond = 700.0F;
// Player::UpdateMove (0x00350f42-0x00350f94) caps the horizontal velocity
// calculated for a WebGrabPoint's linked WayPoint at this shipped constant.
constexpr float kMaximumForcedWebExitSpeedCentimetersPerSecond = 3000.0F;
// Player::UpdateMCSpeed (0x00346f50), motion 13, scales each wall-axis
// joystick component by 0.35 centimeters per millisecond.
constexpr float kWallClimbSpeedCentimetersPerSecond = 350.0F;
// `consts` image address 0x004c6a78, read by Player::SetNextStateId
// (0x003491d0) when entering k_state_jump_fall_idle.
constexpr float kSustainedFallSpeedCentimetersPerSecond = -1200.0F;
// DPhysicsConst::_GLOBAL__I_EPSILON (0x003d7338) initializes
// DEFAULT_GRAVITY.z to 0xc4750000, or -980 cm/s^2. The -1200 value above is
// the state-15 fall velocity installed after the release clip, not gravity.
constexpr float kDefaultGravityCentimetersPerSecondSquared = -980.0F;
// MC_CONST::MAX_SWING_HORIZONTAL_SPEED_CM_PER_SECOND at 0x004c6a84. Motion
// 28 obtains this through GetMCSpeedByJoyStick (0x00341e44) and replaces the
// previous frame's steering contribution instead of integrating acceleration.
constexpr float kMaximumSwingHorizontalSpeedCentimetersPerSecond = 210.0F;
// Player::_GLOBAL__I_WEB_GRAB_MAX_WEB_LENGTH (0x00346d20) initializes
// WEB_SWING_IDLE_ACC_DECREASE to this value. UpdateMCSpeed
// (0x00347f5c-0x00347fd0) raises it to elapsedSeconds and accumulates the
// decaying launch vector into motion 28's horizontal velocity.
constexpr float kWebSwingIdleAccelerationDecrease = 7.044234E-05F;
// Player::SetNextStateId (0x003491d0), motion 400, seeds Player+0x154
// with 200 ms. Player::UpdateSlide (0x0034c498) rejects the Cross/A jump
// while that timer remains positive.
constexpr std::uint32_t kSliderJumpLockoutMilliseconds = 200;
// Player::UpdateMCSpeed (0x00346f50), motions 24/25, installs this velocity
// when slide_to_jump finishes and the slider-fall phase begins.
constexpr float kSliderFallSpeedCentimetersPerSecond = -1200.0F;
// Player::UpdateSlide (0x0034c498) adds this lateral velocity only during
// motion 24 when the stick is held as Cross/A is pressed.
constexpr float kSliderJumpSideSpeedCentimetersPerSecond = 490.0F;
constexpr std::int32_t kNeutralSliderFallAnimationId = 138;
constexpr std::int32_t kLeftSliderFallAnimationId = 139;
constexpr std::int32_t kRightSliderFallAnimationId = 140;
constexpr std::int32_t kNeutralSliderJumpAnimationId = 141;
constexpr std::int32_t kLeftSliderJumpAnimationId = 142;
constexpr std::int32_t kRightSliderJumpAnimationId = 143;
// MC_STATE transition records store the controller action in field zero and
// the native UpdateKeyTrigger predicate in field one. Button 0 is the
// virtual movement key, button 6 is Square;
// predicate 101 is an ordinary press and 150 is the normal-suit press path.
constexpr std::int16_t kMovementButton = 0;
constexpr std::int16_t kPunchButton = 6;
constexpr std::int16_t kWebButton = 5;
constexpr std::int16_t kJumpButton = 8;
constexpr std::int16_t kPressedTransition = 101;
constexpr std::int16_t kReleasedTransition = 102;
constexpr std::int16_t kNormalSuitPressedTransition = 150;
constexpr std::int16_t kHeldTransition = 103;
constexpr std::int16_t kAirborneBindableTargetTransition = 110;
// Player::CheckFrame (0x003403fc) converts MC_STATE's authored 30-Hz event
// frame to the engine's fixed 20-Hz animation frame with integer (frame * 2)
// / 3 before testing whether the runtime frame crossed it. The runtime frame
// itself is rounded by FrameFixedTimelineController::getCurrentClipFrame
// (0x003906c8) as int(time / 50 + 0.5), rather than floored. Consequently
// authored frame 7 first becomes runtime frame 4 at 175 ms, not 200 ms.
constexpr std::uint32_t kRuntimeAnimationFrameMilliseconds = 50;

constexpr std::uint32_t runtimeAnimationFrame(
    std::uint64_t milliseconds) noexcept {
    return static_cast<std::uint32_t>(
        (milliseconds + kRuntimeAnimationFrameMilliseconds / 2U) /
        kRuntimeAnimationFrameMilliseconds);
}

constexpr std::uint32_t combatEventMilliseconds(
    std::int16_t authoredFrame) noexcept {
    const std::uint32_t runtimeFrame =
        static_cast<std::uint32_t>(authoredFrame * 2 / 3);
    return runtimeFrame == 0
        ? 0
        : runtimeFrame * kRuntimeAnimationFrameMilliseconds -
              kRuntimeAnimationFrameMilliseconds / 2U;
}
constexpr std::uint64_t kComboTimeoutMilliseconds = 2000;
// TGameSetting::Reset (0x003e268c) selects difficulty 1 and zeroes all
// upgrade indices for a new profile. The shipped MC_CONST arrays are at
// 0x004c6ba8, 0x004c6b20, 0x004c6b60, and 0x004c6b90 respectively.
constexpr std::uint32_t kDefaultDifficulty = 1;
constexpr std::uint32_t kDefaultUpgradeLevel = 0;
constexpr std::array<float, 4> kAttackPowerHardLevel{
    1.2F, 1.0F, 0.93F, 0.85F};
constexpr std::array<float, 4> kAttackPowerUpgrade{
    1.0F, 1.16F, 1.32F, 1.5F};
constexpr std::array<float, 4> kMagicPowerUpgrade{
    1.0F, 1.16F, 1.32F, 1.5F};
constexpr std::array<float, 4> kHardLevelUpgradeRatio{
    1.0F, 1.0F, 0.6F, 0.2F};

float comboUpgradeRate(const std::array<float, 4>& values) noexcept {
    const float first = values.front();
    const float selected = values[kDefaultUpgradeLevel];
    return first + (selected - first) *
                       kHardLevelUpgradeRatio[kDefaultDifficulty];
}

float length2D(float x, float y) noexcept { return std::sqrt(x * x + y * y); }

std::int32_t movementVirtualKey(const PlayerMotionInput& input) noexcept {
    // GameEventKeyWrap::VisualKeyPressed (0x002f8b0c) converts the analog
    // vector into one of the same W/S/A/D keys used by keyboard input. It
    // releases and presses event zero whenever that dominant direction
    // changes, so an edge is not limited to neutral -> non-neutral.
    if (length2D(input.right, input.forward) <= 1e-4F) {
        return -1;
    }
    if (std::abs(input.right) < std::abs(input.forward)) {
        return input.forward >= 0.0F ? 0x57 : 0x53;
    }
    return input.right >= 0.0F ? 0x44 : 0x41;
}

float length3D(const assets::Vector3& value) noexcept {
    return std::sqrt(value.x * value.x + value.y * value.y +
                     value.z * value.z);
}

bool calculateMovement(const PlayerMotionInput& input, const CameraPose& camera,
                       const assets::Vector3& fallbackFacing,
                       assets::Vector3& movement,
                       float& inputMagnitude) noexcept {
    inputMagnitude = length2D(input.right, input.forward);
    if (inputMagnitude <= 1e-4F) {
        movement = {};
        return false;
    }
    inputMagnitude = std::min(inputMagnitude, 1.0F);
    const float inverseInputLength =
        1.0F / length2D(input.right, input.forward);
    const float rightInput = input.right * inverseInputLength;
    const float forwardInput = input.forward * inverseInputLength;

    float cameraForwardX = camera.target.x - camera.position.x;
    float cameraForwardY = camera.target.y - camera.position.y;
    const float cameraForwardLength =
        length2D(cameraForwardX, cameraForwardY);
    if (cameraForwardLength > std::numeric_limits<float>::epsilon()) {
        cameraForwardX /= cameraForwardLength;
        cameraForwardY /= cameraForwardLength;
    } else {
        cameraForwardX = fallbackFacing.x;
        cameraForwardY = fallbackFacing.y;
    }
    const float cameraRightX = cameraForwardY;
    const float cameraRightY = -cameraForwardX;
    movement = {
        cameraRightX * rightInput + cameraForwardX * forwardInput,
        cameraRightY * rightInput + cameraForwardY * forwardInput, 0.0F};
    const float movementLength = length2D(movement.x, movement.y);
    if (movementLength > std::numeric_limits<float>::epsilon()) {
        movement.x /= movementLength;
        movement.y /= movementLength;
    }
    return true;
}

bool isWebBindingMotion(std::int16_t motionType) noexcept {
    // Player::IsInWebBinding (0x00340f60).
    return motionType == 103 ||
           (motionType >= 110 && motionType <= 115) ||
           (motionType >= 124 && motionType <= 130);
}

bool isWebAttackMotion(std::int16_t motionType) noexcept {
    return motionType == 116 || motionType == 118 || motionType == 119 ||
           motionType == 123 || isWebBindingMotion(motionType);
}

bool bindsEnemyOnHit(std::int16_t motionType) noexcept {
    // CEnemy::ParseLocalAiMessage (0x00331eb0) maps the pellet/bind attack
    // types 0x7b, 0x7c, and 0x6e to CBehaviorTiedUp message 0x69. That
    // message enters native tied state 35.
    return motionType == 110 || motionType == 123 || motionType == 124;
}

std::int16_t enemyHitTypeForState(
    const PlayerStateDefinition& state) noexcept {
    // Player::CheckAttackTarget (0x0034fca0) does not always send the raw
    // player motion type. These are the exact branches written to
    // AIHitTargetInfo+0 before CEnemy::ProcessHitInfo receives it.
    if (state.stateClass == 6) {
        // CheckAttackTarget 0x0035018c-0x003501a8 first normalizes the
        // Spider-Sense family to 0x79, then preserves red blink motion 0x1fd
        // and converts black blink motion 0x1fe to wheel type 0x89.
        if (state.motionType == 509) {
            return 509;
        }
        if (state.motionType == 510) {
            return 137;
        }
        return 121;
    }
    switch (state.motionType) {
    case 101:
    case 108:
        return 100;
    case 104:
    case 107:
    case 111:
    case 127:
    case 142:
        return 121;
    case 103:
        return 114;
    case 116:
        return 136;
    case 141:
        return 106;
    case 138:
    case 139:
    case 140:
        return 137;
    default:
        return state.motionType;
    }
}

const assets::ColladaAnimationClip* stateClip(
    const assets::ColladaAnimationFile* animationBank,
    const PlayerStateDefinition* state) noexcept {
    if (animationBank == nullptr || state == nullptr ||
        state->primaryAnimationId < 0 ||
        static_cast<std::size_t>(state->primaryAnimationId) >=
            animationBank->clips().size()) {
        return nullptr;
    }
    return &animationBank->clips()[
        static_cast<std::size_t>(state->primaryAnimationId)];
}

const assets::ColladaAnimationClip* clipById(
    const assets::ColladaAnimationFile* animationBank,
    std::int32_t animationId) noexcept {
    if (animationBank == nullptr || animationId < 0 ||
        static_cast<std::size_t>(animationId) >=
            animationBank->clips().size()) {
        return nullptr;
    }
    return &animationBank->clips()[static_cast<std::size_t>(animationId)];
}

float parseFloat(std::string_view text, float fallback) noexcept {
    const std::string storage(text);
    char* end = nullptr;
    const float value = std::strtof(storage.c_str(), &end);
    return end == storage.c_str() ? fallback : value;
}

bool parseBoolean(std::string_view text, bool fallback) noexcept {
    if (text == "true" || text == "1") {
        return true;
    }
    if (text == "false" || text == "0") {
        return false;
    }
    return fallback;
}

assets::Vector3 parseVector3(std::string_view text,
                             assets::Vector3 fallback) noexcept {
    std::string storage(text);
    std::replace(storage.begin(), storage.end(), ',', ' ');
    const char* cursor = storage.c_str();
    char* end = nullptr;
    assets::Vector3 result;
    for (float* component : {&result.x, &result.y, &result.z}) {
        *component = std::strtof(cursor, &end);
        if (end == cursor) {
            return fallback;
        }
        cursor = end;
    }
    return result;
}

assets::Quaternion parseQuaternion(std::string_view text,
                                   assets::Quaternion fallback) noexcept {
    std::string storage(text);
    std::replace(storage.begin(), storage.end(), ',', ' ');
    const char* cursor = storage.c_str();
    char* end = nullptr;
    assets::Quaternion result;
    for (float* component : {&result.x, &result.y, &result.z, &result.w}) {
        *component = std::strtof(cursor, &end);
        if (end == cursor) {
            return fallback;
        }
        cursor = end;
    }
    return result;
}

assets::Quaternion slerp(assets::Quaternion start,
                         assets::Quaternion end, float factor) noexcept {
    float dot = start.x * end.x + start.y * end.y + start.z * end.z +
                start.w * end.w;
    if (dot < 0.0F) {
        end.x = -end.x;
        end.y = -end.y;
        end.z = -end.z;
        end.w = -end.w;
        dot = -dot;
    }
    dot = std::clamp(dot, -1.0F, 1.0F);
    float startWeight = 1.0F - factor;
    float endWeight = factor;
    if (dot < 0.9995F) {
        const float angle = std::acos(dot);
        const float inverseSine = 1.0F / std::sin(angle);
        startWeight = std::sin((1.0F - factor) * angle) * inverseSine;
        endWeight = std::sin(factor * angle) * inverseSine;
    }
    assets::Quaternion result{start.x * startWeight + end.x * endWeight,
                              start.y * startWeight + end.y * endWeight,
                              start.z * startWeight + end.z * endWeight,
                              start.w * startWeight + end.w * endWeight};
    const float length =
        std::sqrt(result.x * result.x + result.y * result.y +
                  result.z * result.z + result.w * result.w);
    if (length > std::numeric_limits<float>::epsilon()) {
        result.x /= length;
        result.y /= length;
        result.z /= length;
        result.w /= length;
    }
    return result;
}

const CinematicCommand* nextMoveObjectCommand(
    const CinematicThread& thread, const CinematicCommand& command) noexcept {
    bool currentFound = false;
    for (const CinematicCommand& candidate : thread.commands) {
        if (&candidate == &command) {
            currentFound = true;
            continue;
        }
        if (currentFound && candidate.name == "MoveObject") {
            return &candidate;
        }
    }
    return nullptr;
}

std::array<float, 16> scriptedWorldMatrix(
    const assets::Vector3& position, assets::Quaternion rotation,
    const assets::Vector3& scale) noexcept {
    const float length =
        std::sqrt(rotation.x * rotation.x + rotation.y * rotation.y +
                  rotation.z * rotation.z + rotation.w * rotation.w);
    if (length > std::numeric_limits<float>::epsilon()) {
        rotation.x /= length;
        rotation.y /= length;
        rotation.z /= length;
        rotation.w /= length;
    }
    const float xx = rotation.x * rotation.x;
    const float yy = rotation.y * rotation.y;
    const float zz = rotation.z * rotation.z;
    const float xy = rotation.x * rotation.y;
    const float xz = rotation.x * rotation.z;
    const float yz = rotation.y * rotation.z;
    const float wx = rotation.w * rotation.x;
    const float wy = rotation.w * rotation.y;
    const float wz = rotation.w * rotation.z;
    return {(1.0F - 2.0F * (yy + zz)) * scale.x,
            (2.0F * (xy - wz)) * scale.x,
            (2.0F * (xz + wy)) * scale.x,
            0.0F,
            (2.0F * (xy + wz)) * scale.y,
            (1.0F - 2.0F * (xx + zz)) * scale.y,
            (2.0F * (yz - wx)) * scale.y,
            0.0F,
            (2.0F * (xz - wy)) * scale.z,
            (2.0F * (yz + wx)) * scale.z,
            (1.0F - 2.0F * (xx + yy)) * scale.z,
            0.0F,
            position.x,
            position.y,
            position.z,
            1.0F};
}

} // namespace

Result GameplayPlayer::initialize(const LevelPlayerAsset& asset,
                                  const LevelCollision* collision,
                                  const PlayerStateConfigDatabase* states,
                                  std::span<const LevelWebGrabPointAsset>
                                      webGrabPoints,
                                  std::span<const LevelSlideAsset> slides,
                                  std::span<const LevelWayPointAsset>
                                      waypoints,
                                  const ButtonConfigDatabase* buttons,
                                  const PlayerHitEffectConfigDatabase*
                                      hitEffects,
                                  std::span<const PlayerHitEffectAsset>
                                      hitEffectAssets) {
    if (asset.animationBank.findClip("idle_stand") == nullptr ||
        asset.animationBank.findClip("run") == nullptr) {
        return Result::failure(
            "Player animation bank is missing movement clips");
    }
    position_ = asset.position;
    wallWeb_ = {};
    buttonDatabase_ = buttons;
    wallWebActionPressed_ = false;
    wallWebTargetAlive_ = true;
    movementVirtualKey_ = -1;
    objectId_ = asset.objectId;
    cinematicDriven_ = false;
    cinematicAnimationLoops_ = true;
    cinematicAnimationSpeed_ = 1.0F;
    cinematicMotion_ = {};
    quickTimeActionDriven_ = false;
    quickTimeActionLoops_ = false;
    quickTimeActionDetachPosition_ = {};
    quickTimeActionHasDetachPosition_ = false;
    activeScriptedState_ = nullptr;
    scriptedStateLoops_ = false;
    renderPosition_ = position_;
    jumpAnchorHeight_ = position_.z;
    worldTransform_ = asset.worldTransform;
    scale_.x = length2D(worldTransform_[0], worldTransform_[1]);
    scale_.y = length2D(worldTransform_[4], worldTransform_[5]);
    scale_.z = std::sqrt(worldTransform_[8] * worldTransform_[8] +
                         worldTransform_[9] * worldTransform_[9] +
                         worldTransform_[10] * worldTransform_[10]);
    // UpdateRotation (0x003450bc) exposes the player's face direction as the
    // negative local-Y basis, while local X is its lateral axis.
    const float faceLength = length2D(worldTransform_[4], worldTransform_[5]);
    if (faceLength > std::numeric_limits<float>::epsilon()) {
        facing_ = {-worldTransform_[4] / faceLength,
                   -worldTransform_[5] / faceLength, 0.0F};
    }
    activeAnimation_ = "idle_stand";
    animationTimeMilliseconds_ = 0;
    collision_ = collision;
    mesh_ = &asset.mesh;
    animationBank_ = &asset.animationBank;
    animationDisplacement_ = &asset.animationDisplacement;
    stateDatabase_ = states;
    hitEffectDatabase_ = hitEffects;
    hitEffectAssets_ = hitEffectAssets;
    activeHitEffects_.clear();
    attackPhysicsVelocity_ = {};
    pendingHitEffectSpawnCount_ = 0;
    pendingCombatEffectCount_ = 0;
    pendingVoxStopEventCount_ = 0;
    nextUltimatePulseMilliseconds_ = 0;
    ultimatePhaseElapsedMilliseconds_ = 0;
    ultimatePhaseRemainingMilliseconds_ = 0;
    ultimateOutEffectEmitted_ = false;
    ultimateSplashEmitted_ = false;
    initialPunchState_ = nullptr;
    farPunchState_ = nullptr;
    groundWebShotState_ = nullptr;
    groundWebDragDownState_ = nullptr;
    groundWebFlyKickState_ = nullptr;
    airKnockdownState_ = nullptr;
    airWebBindGroundState_ = nullptr;
    airDoubleWebBindState_ = nullptr;
    airPunchState_ = nullptr;
    airTargetKickState_ = nullptr;
    ultimatePrepareState_ = nullptr;
    senseAvoidStates_.fill(nullptr);
    senseAttackStates_.fill(nullptr);
    senseBlinkRedState_ = nullptr;
    senseBlinkBlackState_ = nullptr;
    senseAvoidVariantCursor_ = 0;
    jumpStartState_ = nullptr;
    jumpFallState_ = nullptr;
    shortWebJumpState_ = nullptr;
    sustainedFallState_ = nullptr;
    jumpLandState_ = nullptr;
    swingThrowState_ = nullptr;
    swingHangState_ = nullptr;
    swingIdleState_ = nullptr;
    sliderLandState_ = nullptr;
    sliderMoveState_ = nullptr;
    sliderJumpUpState_ = nullptr;
    sliderJumpFallState_ = nullptr;
    wallIdleState_ = nullptr;
    wallMoveState_ = nullptr;
    wallAttachState_ = nullptr;
    wallExitState_ = nullptr;
    wallJumpUpState_ = nullptr;
    wallJumpDownState_ = nullptr;
    wallJumpLeftState_ = nullptr;
    wallJumpRightState_ = nullptr;
    hurtLightState_ = nullptr;
    hurtHeavyState_ = nullptr;
    deadOverState_ = nullptr;
    activeLocomotionState_ = nullptr;
    locomotionState_ = LocomotionState::Grounded;
    verticalVelocityCentimetersPerSecond_ = 0.0F;
    swingReleaseVelocity_ = {};
    swingReleaseSteeringVelocity_ = {};
    swingReleaseDecayVelocity_ = {};
    sustainedFallCarriesSwingVelocity_ = false;
    swingReleaseHasTarget_ = false;
    swingReleaseTarget_ = {};
    wallNormal_ = {};
    wallStateStartPosition_ = {};
    lastWallInput_ = {};
    webGrabPointRuntime_.bind(webGrabPoints, collision);
    webSwingRuntime_ = {};
    slideRuntime_.bind(slides, waypoints);
    slideJumpVelocity_ = {};
    slideJumpSideVelocity_ = {};
    slideJumpUpAnimationId_ = kNeutralSliderJumpAnimationId;
    slideJumpFallAnimationId_ = kNeutralSliderFallAnimationId;
    slideMoveElapsedMilliseconds_ = 0;
    selectedWebGrabPoint_ = nullptr;
    swingUsesLeftHand_ = false;
    webReleaseRequested_ = false;
    activeAttackState_ = nullptr;
    activeAttackTarget_.reset();
    activeAttackAirborne_ = false;
    queuedAttackState_ = nullptr;
    queuedAttackTarget_.reset();
    queuedAttackAirborne_ = false;
    nextAttackLinkAnimationIndex_ = 0;
    attackTimelineMilliseconds_ = 0;
    inputFrameAdvanceMilliseconds_ = 0;
    inputFramePrepared_ = false;
    attackEnteredDuringPreparedInputFrame_ = false;
    ultimateActive_ = false;
    locomotionRootTranslation_ = {};
    nextAttackImpactFrameIndex_ = 0;
    activeAttackContactAccepted_ = false;
    pendingMeleeImpactCount_ = 0;
    pendingWebPelletLaunchCount_ = 0;
    pendingEnemyNotifyEventCount_ = 0;
    pendingAttackSoundTriggerCount_ = 0;
    pendingCombatEffectCount_ = 0;
    pendingVoxStopEventCount_ = 0;
    hurtReactionRemainingMilliseconds_ = 0;
    enteredStateCount_ = 0;
    if (states != nullptr) {
        jumpStartState_ = states->findState("k_state_jump_start");
        jumpFallState_ = states->findState("k_state_jump_fall");
        shortWebJumpState_ =
            states->findState("k_state_jump_web_jump");
        sustainedFallState_ = states->findState("k_state_jump_fall_idle");
        jumpLandState_ = states->findState("k_state_jump_land");
        swingThrowState_ = states->findState("k_state_swing_web_throw");
        swingHangState_ = states->findState("k_state_swing_hang");
        swingIdleState_ = states->findState("k_state_swing_idle");
        sliderMoveState_ =
            states->findState("k_state_trigger_slider_move");
        sliderLandState_ =
            states->findState("k_state_trigger_slider_land");
        sliderJumpUpState_ =
            states->findState("k_state_slider_jumpup");
        sliderJumpFallState_ =
            states->findState("k_state_slider_jumpfall");
        wallIdleState_ = states->findState("k_state_idle_onwall");
        wallMoveState_ = states->findState("k_state_move_onwall");
        wallAttachState_ = states->findState("k_state_move_climb_wall");
        wallExitState_ = states->findState("k_state_move_exit_wall");
        wallJumpUpState_ = states->findState("k_state_move_jump_wall_up");
        wallJumpDownState_ = states->findState("k_state_move_jump_wall_down");
        wallJumpLeftState_ = states->findState("k_state_move_jump_wall_left");
        wallJumpRightState_ =
            states->findState("k_state_move_jump_wall_right");
        hurtLightState_ = states->findState("k_state_hurt_light");
        hurtHeavyState_ = states->findState("k_state_hurt_heavy");
        deadOverState_ = states->findState("k_state_dead_over");
        if (stateClip(animationBank_, jumpStartState_) == nullptr ||
            stateClip(animationBank_, jumpFallState_) == nullptr ||
            shortWebJumpState_ == nullptr ||
            shortWebJumpState_->motionType != 22 ||
            stateClip(animationBank_, shortWebJumpState_) == nullptr ||
            stateClip(animationBank_, sustainedFallState_) == nullptr ||
            stateClip(animationBank_, jumpLandState_) == nullptr) {
            return Result::failure(
                "Player jump states have invalid animation IDs");
        }
        if (!webGrabPoints.empty() &&
            (swingThrowState_ == nullptr || swingHangState_ == nullptr ||
             swingIdleState_ == nullptr ||
             clipById(animationBank_, 98) == nullptr ||
             clipById(animationBank_, 99) == nullptr ||
             clipById(animationBank_, 168) == nullptr ||
             clipById(animationBank_, 169) == nullptr ||
             clipById(animationBank_, 170) == nullptr ||
             clipById(animationBank_, 173) == nullptr)) {
            return Result::failure(
                "Player swing states have invalid animation IDs");
        }
        if (!slides.empty() &&
            (stateClip(animationBank_, sliderLandState_) == nullptr ||
             stateClip(animationBank_, sliderMoveState_) == nullptr ||
             sliderJumpUpState_ == nullptr ||
             sliderJumpUpState_->motionType != 24 ||
             stateClip(animationBank_, sliderJumpUpState_) == nullptr ||
             sliderJumpFallState_ == nullptr ||
             sliderJumpFallState_->motionType != 25 ||
             clipById(animationBank_, kNeutralSliderFallAnimationId) ==
                 nullptr ||
             clipById(animationBank_, kLeftSliderFallAnimationId) == nullptr ||
             clipById(animationBank_, kRightSliderFallAnimationId) == nullptr ||
             clipById(animationBank_, kLeftSliderJumpAnimationId) == nullptr ||
             clipById(animationBank_, kRightSliderJumpAnimationId) == nullptr)) {
            return Result::failure(
                "Player slider states have invalid animation IDs");
        }
        if (wallIdleState_ == nullptr || wallMoveState_ == nullptr ||
            wallAttachState_ == nullptr || wallExitState_ == nullptr ||
            wallJumpUpState_ == nullptr || wallJumpDownState_ == nullptr ||
            wallJumpLeftState_ == nullptr || wallJumpRightState_ == nullptr ||
            stateClip(animationBank_, wallIdleState_) == nullptr ||
            clipById(animationBank_, 128) == nullptr ||
            clipById(animationBank_, 198) == nullptr ||
            stateClip(animationBank_, wallJumpUpState_) == nullptr ||
            stateClip(animationBank_, wallJumpDownState_) == nullptr ||
            stateClip(animationBank_, wallJumpLeftState_) == nullptr ||
            stateClip(animationBank_, wallJumpRightState_) == nullptr) {
            return Result::failure(
                "Player wall states have invalid animation IDs");
        }
        if (stateClip(animationBank_, hurtLightState_) == nullptr ||
            stateClip(animationBank_, hurtHeavyState_) == nullptr) {
            return Result::failure(
                "Player hurt states have invalid animation IDs");
        }
        if (deadOverState_ == nullptr || deadOverState_->id != 0x80 ||
            deadOverState_->stateClass != 8) {
            return Result::failure(
                "Player dead-over state has invalid authored data");
        }
        initialPunchState_ =
            states->findState("k_state_idle_to_punch_right");
        farPunchState_ = states->findState("k_state_idle_to_far_attack");
        groundWebShotState_ =
            states->findState("k_state_attack_web_shoot");
        groundWebDragDownState_ =
            states->findState("k_state_web_drag_down");
        groundWebFlyKickState_ =
            states->findState("k_state_in_air_fly_kicking");
        airKnockdownState_ =
            states->findState("k_state_air_knockdown");
        airWebBindGroundState_ =
            states->findState("k_state_air_web_bind_ground");
        airDoubleWebBindState_ =
            states->findState("k_state_air_double_web_bind");
        airPunchState_ = states->findState("k_state_in_air_fast_punch");
        airTargetKickState_ =
            states->findState("k_state_in_air_diagonal_kick");
        ultimatePrepareState_ =
            states->findState("k_state_ultimate_prepare");
        constexpr std::array<std::string_view, 4> senseAvoidNames{
            "k_state_sense_avoid_front", "k_state_sense_avoid_back",
            "k_state_sense_avoid_left", "k_state_sense_avoid_right"};
        constexpr std::array<std::string_view, 4> senseAttackNames{
            "k_state_sense_attack_front", "k_state_sense_attack_back",
            "k_state_sense_attack_left", "k_state_sense_attack_right"};
        for (std::size_t index = 0; index < senseAvoidStates_.size(); ++index) {
            senseAvoidStates_[index] = states->findState(senseAvoidNames[index]);
            senseAttackStates_[index] = states->findState(senseAttackNames[index]);
        }
        senseBlinkRedState_ =
            states->findState("k_state_sense_blink_red");
        senseBlinkBlackState_ =
            states->findState("k_state_sense_blink_black");
        constexpr std::array<std::string_view, 6> normalGroundComboStates{
            "k_state_idle_to_punch_right",
            "k_state_punch_right_to_punch_left",
            "k_state_punch_left_to_kick_right",
            "k_state_kick_right_to_fast_kick",
            "k_state_kick_right_to_fast_kick_2",
            "k_state_kick_left_double_kick",
        };
        for (const std::string_view stateName : normalGroundComboStates) {
            const PlayerStateDefinition* state = states->findState(stateName);
            if (state == nullptr || state->stateClass != 4 ||
                stateClip(animationBank_, state) == nullptr) {
                return Result::failure(
                    "Player ground-combo state has invalid animation data");
            }
        }
        if (farPunchState_ == nullptr || farPunchState_->stateClass != 4 ||
            stateClip(animationBank_, farPunchState_) == nullptr) {
            return Result::failure(
                "Player far-attack state has invalid animation data");
        }
        for (const PlayerStateDefinition* webState :
             {groundWebShotState_, groundWebDragDownState_,
              groundWebFlyKickState_, airKnockdownState_,
              airWebBindGroundState_,
              airDoubleWebBindState_}) {
            if (webState == nullptr || webState->stateClass != 4 ||
                stateClip(animationBank_, webState) == nullptr) {
                return Result::failure(
                    "Player ground-web state has invalid animation data");
            }
        }
        for (const PlayerStateDefinition* combatState :
             {airPunchState_, airTargetKickState_, ultimatePrepareState_,
              senseAvoidStates_[0], senseAvoidStates_[1],
              senseAvoidStates_[2], senseAvoidStates_[3],
              senseAttackStates_[0], senseAttackStates_[1],
              senseAttackStates_[2], senseAttackStates_[3]}) {
            if (combatState == nullptr ||
                stateClip(animationBank_, combatState) == nullptr) {
                return Result::failure(
                    "Player extended-combat state has invalid animation data");
            }
        }
        for (const PlayerStateDefinition* blinkState :
             {senseBlinkRedState_, senseBlinkBlackState_}) {
            if (blinkState == nullptr || blinkState->stateClass != 6 ||
                stateClip(animationBank_, blinkState) == nullptr) {
                return Result::failure(
                    "Player blink-strike state has invalid animation data");
            }
        }
    }
    maximumHealth_ = std::max(asset.health, 1.0F);
    health_ = maximumHealth_;
    skillPoints_ = 0;
    normalComboCount_ = 0;
    ultimateComboCount_ = 0;
    normalComboDamage_ = 0.0F;
    ultimateComboDamage_ = 0.0F;
    maximumComboCount_ = 0;
    completedComboHitCount_ = 0;
    comboTouchedMilliseconds_ = 0;
    comboScore_ = 0;
    forceComboFinish_ = false;
    if (collision_ != nullptr) {
        assets::Vector3 grounded;
        (void)collision_->resolveGroundMotion(position_, position_, grounded,
                                              100.0F, 500.0F,
                                              LevelPhysicsFlags::JumpWall,
                                              LevelCollisionDepenetration::
                                                  TowardAuthoredNormal);
        position_ = grounded;
        renderPosition_ = position_;
        jumpAnchorHeight_ = position_.z;
        updateWorldTransform(facing_);
    }
    return Result::success();
}

bool GameplayPlayer::requestPunch(
    const std::optional<PlayerAttackTarget>& target,
    const std::optional<assets::Vector3>& directionalInput) noexcept {
    lastActionRejectionReason_ = {};
    if (dead()) {
        lastActionRejectionReason_ = "dead";
        return false;
    }
    if (hurtReactionRemainingMilliseconds_ != 0) {
        lastActionRejectionReason_ = "hurt_reaction";
        return false;
    }
    if (onWall()) {
        if (activeAttackState_ != nullptr || wallWeb_.active() || stateDatabase_ == nullptr ||
            (locomotionState_ != LocomotionState::WallIdle &&
             locomotionState_ != LocomotionState::WallMove)) {
            lastActionRejectionReason_ = "wall_state_busy";
            return false;
        }
        // Idle state 1 routes X through state 68/GetOnWallSpecialState
        // (0x00342de0); moving state 5 chooses the same four states from
        // the current stick direction. No ground look-at rotation occurs.
        // This build's vector3d::crossProduct (0x0034063c) is
        // left-handed: normal.crossProduct(up) = {-normal.y,normal.x,0}.
        const assets::Vector3 right{-wallNormal_.y, wallNormal_.x, 0.0F};
        assets::Vector3 direction{0.0F, 0.0F, 1.0F};
        if (directionalInput) {
            direction = *directionalInput;
        } else if (target) {
            direction = {target->position.x - position_.x,
                         target->position.y - position_.y,
                         target->position.z - position_.z};
        }
        const float side = direction.x * right.x + direction.y * right.y;
        std::size_t pattern = 0;
        if (std::abs(side) > 0.0F && std::abs(side) >= std::abs(direction.z)) {
            pattern = side < 0.0F ? 2U : 3U;
        } else if (direction.z < 0.0F) {
            pattern = 1;
        }
        constexpr std::array<std::string_view, 4> states{
            "k_state_onwall_attack_up", "k_state_onwall_attack_down",
            "k_state_onwall_attack_left", "k_state_onwall_attack_right"};
        const auto* requested = stateDatabase_->findState(states[pattern]);
        wallAttackDirection_ = pattern == 0 ? assets::Vector3{0.0F, 0.0F, 1.0F}
            : pattern == 1 ? assets::Vector3{0.0F, 0.0F, -1.0F}
            : pattern == 2 ? assets::Vector3{-right.x, -right.y, 0.0F} : right;
        activeAttackTarget_ = target;
        activeAttackAirborne_ = false;
        if (requested == nullptr || !enterAttackState(*requested)) {
            activeAttackTarget_.reset();
            lastActionRejectionReason_ = "missing_wall_attack_animation";
            return false;
        }
        return true;
    }
    const bool beganAirborne = airborne();
    const bool continuingAttack = activeAttackState_ != nullptr;
    // Ordinary 0x65/0x96 button predicates in UpdateKeyTrigger
    // (0x0034d0a4) write only Player+0x4d8. They do not run the target-search
    // branch, so an attack already in progress retains Player+0x594 even if
    // another actor has become the best candidate on this input frame.
    const std::optional<PlayerAttackTarget>& selectedTarget =
        continuingAttack ? activeAttackTarget_ : target;
    const PlayerStateDefinition* requested = nullptr;
    if (activeAttackState_ != nullptr) {
        // UpdateKeyTrigger applies the same Square transition lookup to
        // class-four ground and air states. States 81 and 82 therefore form
        // the authored five-hit aerial punch sequence before state 83's
        // heavy finisher.
        if (!attackInputWindowOpen()) {
            lastActionRejectionReason_ = "attack_window_closed";
            return false;
        }
        requested = punchTransition();
    } else if (beganAirborne) {
        // Jump states 13-15 contain two Square transitions. Predicate 109 is
        // the CEnemy::IsInAir target-special branch; otherwise the ordinary
        // press enters state 81.
        requested = selectedTarget.has_value() && selectedTarget->airborne
                        ? airTargetKickState_
                        : airPunchState_;
    } else if (locomotionState_ == LocomotionState::Grounded) {
        requested = initialPunchState_;
    } else {
        lastActionRejectionReason_ = "attack_unavailable";
        return false;
    }
    if (requested == nullptr) {
        lastActionRejectionReason_ = "missing_transition";
        return false;
    }

    if (selectedTarget.has_value()) {
        const float x = selectedTarget->position.x - position_.x;
        const float y = selectedTarget->position.y - position_.y;
        const float length = std::hypot(x, y);
        if (length > std::numeric_limits<float>::epsilon()) {
            // SetNextStateId (0x003491d0) stores the acquired target and calls
            // Unit::SetLookAt before it selects the attack animation.
            facing_ = {x / length, y / length, 0.0F};
        }
    } else if (directionalInput.has_value()) {
        const float length = std::hypot(directionalInput->x,
                                        directionalInput->y);
        if (length > std::numeric_limits<float>::epsilon()) {
            // RotatePlayerByFixedDir (0x003415e8) precedes directional target
            // search so an untargeted attack still follows the stick.
            facing_ = {directionalInput->x / length,
                       directionalInput->y / length, 0.0F};
        }
    }
    // NeedDashToTarget is reached from UpdateKeyTrigger's target-search
    // predicate for the opening attack. A direct 0x65/0x96 combo transition
    // does not re-run it merely because knockback moved the retained victim.
    if (!beganAirborne && !continuingAttack) {
        requested = attackStateForTarget(*requested, selectedTarget);
    }
    const bool requestedAirborne = beganAirborne ||
        requested->motionType == 102 || requested->motionType == 103 ||
        requested->motionType == 104 || requested->motionType == 105 ||
        requested->motionType == 106 || requested->motionType == 108 ||
        requested->motionType == 109 || requested->motionType == 111 ||
        requested->motionType == 112 || requested->motionType == 114;
    if (activeAttackState_ != nullptr) {
        queueAttackTransition(*requested, selectedTarget, requestedAirborne);
        return true;
    }
    activeAttackTarget_ = selectedTarget;
    activeAttackAirborne_ = requestedAirborne;
    if (requested == nullptr || !enterAttackState(*requested)) {
        activeAttackTarget_.reset();
        lastActionRejectionReason_ = "missing_animation";
        return false;
    }
    return true;
}

std::optional<assets::Vector3> GameplayPlayer::attackDirection(
    const PlayerMotionInput& input, const CameraPose& camera) const noexcept {
    if (onWall()) {
        if (std::hypot(input.right, input.forward) <= 0.0001F) {
            return std::nullopt;
        }
        return assets::Vector3{-wallNormal_.y * input.right,
                              wallNormal_.x * input.right, input.forward};
    }
    assets::Vector3 direction;
    float magnitude{};
    if (!calculateMovement(input, camera, facing_, direction, magnitude)) {
        return std::nullopt;
    }
    return direction;
}

bool GameplayPlayer::requestJump() noexcept {
    return requestJump({}, {}, PlayerButtonPhase::Pressed);
}

bool GameplayPlayer::requestJump(const PlayerMotionInput& input,
                                 const CameraPose& camera,
                                 PlayerButtonPhase phase) noexcept {
    if (dead() || hurtReactionRemainingMilliseconds_ != 0 || wallWeb_.active()) {
        return false;
    }
    if (activeAttackState_ != nullptr) {
        if (!attackInputWindowOpen()) {
            return false;
        }
        if (phase == PlayerButtonPhase::Pressed &&
            activeAttackTarget_.has_value() &&
            activeAttackTarget_->airborne &&
            activeAttackTarget_->canBeTiedUp) {
            // UpdateKeyTrigger predicate 0x6e queries the retained CEnemy,
            // requires its airborne special-state capability and CanBeTiedUp,
            // then accepts a fresh Cross/A press. State 97 uses this exact
            // target-special route to state 106.
            const std::array<std::int16_t, 1> airborneTarget{
                kAirborneBindableTargetTransition};
            if (const PlayerStateDefinition* transition =
                    transitionForButton(kJumpButton, airborneTarget);
                transition != nullptr) {
                queueAttackTransition(*transition, activeAttackTarget_, true);
                return true;
            }
        }
        const std::array<std::int16_t, 1> predicates{
            phase == PlayerButtonPhase::Released
                ? kReleasedTransition
                : kHeldTransition};
        const PlayerStateDefinition* transition =
            transitionForButton(kJumpButton, predicates);
        if (transition == nullptr) {
            return false;
        }
        queueAttackTransition(*transition, activeAttackTarget_, true);
        return true;
    }
    // UpdateKeyTrigger polls hold/release predicates only as transitions from
    // the current state. A held or released A/Cross must not create a new
    // locomotion jump after an attack has already returned to idle.
    if (phase != PlayerButtonPhase::Pressed) {
        return false;
    }
    if (locomotionState_ == LocomotionState::SliderMove) {
        // Player::UpdateSlide (0x0034c498) performs this gate before copying
        // the PhysicsEntity velocity to Player+0x454 and entering state 20
        // with SetNextStateId(..., 2).
        if (slideMoveElapsedMilliseconds_ <
                kSliderJumpLockoutMilliseconds ||
            sliderJumpUpState_ == nullptr ||
            stateClip(animationBank_, sliderJumpUpState_) == nullptr) {
            return false;
        }
        slideJumpSideVelocity_ = {};
        slideJumpUpAnimationId_ = kNeutralSliderJumpAnimationId;
        slideJumpFallAnimationId_ = kNeutralSliderFallAnimationId;

        assets::Vector3 movement;
        float inputMagnitude{};
        const bool directional =
            calculateMovement(input, camera, facing_, movement,
                              inputMagnitude) &&
            length2D(facing_.x, facing_.y) > 0.001F;
        if (directional) {
            // UpdateSlide forms the lateral axis as face-direction cross +Z,
            // chooses its sign from the camera-relative stick direction, and
            // stores that 490 cm/s vector at Player+0x460. UpdateMCSpeed adds
            // it only while motion 24 is active.
            assets::Vector3 side{facing_.y, -facing_.x, 0.0F};
            const float sideLength = length2D(side.x, side.y);
            side.x /= sideLength;
            side.y /= sideLength;
            const float sideSign =
                movement.x * side.x + movement.y * side.y >= 0.0F
                    ? 1.0F
                    : -1.0F;
            slideJumpSideVelocity_ = {
                side.x * kSliderJumpSideSpeedCentimetersPerSecond * sideSign,
                side.y * kSliderJumpSideSpeedCentimetersPerSecond * sideSign,
                0.0F};
            if (sideSign > 0.0F) {
                slideJumpUpAnimationId_ = kLeftSliderJumpAnimationId;
                slideJumpFallAnimationId_ = kLeftSliderFallAnimationId;
            } else {
                slideJumpUpAnimationId_ = kRightSliderJumpAnimationId;
                slideJumpFallAnimationId_ = kRightSliderFallAnimationId;
            }
        }

        const auto* jumpClip =
            clipById(animationBank_, slideJumpUpAnimationId_);
        if (jumpClip == nullptr) {
            return false;
        }
        // The native only installs CSlider+0x21c's animation-length + 2000 ms
        // re-catch cooldown for a directional jump. A neutral jump deliberately
        // remains eligible to catch the same rope again in state 21.
        const SlideExit exit = directional
            ? slideRuntime_.jumpFinish(jumpClip->durationMilliseconds() + 2000U)
            : slideRuntime_.finish();
        slideJumpVelocity_ = exit.velocityCentimetersPerSecond;
        locomotionRootTranslation_ = {};
        renderPosition_ = position_;
        jumpAnchorHeight_ = position_.z;
        verticalVelocityCentimetersPerSecond_ = 0.0F;
        enterLocomotionState(LocomotionState::SliderJumpUp);
        return true;
    }
    if (onWall()) {
        // k_state_move_onwall's authored transition table routes button 8
        // through predicates 1..4 to the up/down/left/right wall-jump states.
        // Player::UpdateKeyTrigger (0x0034d0a4) evaluates those predicates
        // against the current controller sample.  Using lastWallInput_ here
        // made a released stick, or an autoplay jump supplied on this frame,
        // select a stale direction.
        const PlayerStateDefinition* jumpState = wallJumpUpState_;
        if (std::abs(input.right) > std::abs(input.forward)) {
            jumpState = input.right < 0.0F
                            ? wallJumpLeftState_
                            : wallJumpRightState_;
        } else if (input.forward < -0.25F) {
            jumpState = wallJumpDownState_;
        }
        if (jumpState == nullptr || stateClip(animationBank_, jumpState) == nullptr) {
            return false;
        }
        wallStateStartPosition_ = position_;
        activeLocomotionState_ = jumpState;
        locomotionState_ = LocomotionState::WallJump;
        setAnimation(stateClip(animationBank_, jumpState)->name);
        queueEnteredState(jumpState->name);
        updateWorldTransform(facing_);
        return true;
    }
    if ((locomotionState_ == LocomotionState::JumpStart ||
         locomotionState_ == LocomotionState::JumpFall ||
         locomotionState_ == LocomotionState::SustainedFall) &&
        shortWebJumpState_ != nullptr) {
        locomotionRootTranslation_ = {};
        enterLocomotionState(LocomotionState::ShortWebJump);
        return true;
    }
    if (locomotionState_ != LocomotionState::Grounded ||
        jumpStartState_ == nullptr) {
        return false;
    }
    jumpAnchorHeight_ = position_.z;
    enterLocomotionState(LocomotionState::JumpStart);
    return true;
}

bool GameplayPlayer::requestWeb(
    const std::optional<PlayerAttackTarget>& target,
    const std::optional<assets::Vector3>& directionalInput,
    PlayerButtonPhase phase, const CameraPose* camera) noexcept {
    lastActionRejectionReason_ = {};
    if (camera != nullptr) {
        // Every native web-line allocation immediately follows
        // CGameCamera::GetLookDirection and stores its negation through
        // CTexLineSceneNode::setOrientation (SetNextStateId call sites at
        // 0x0034a51a..0x0034aa3a; StartWebSwing at 0x00348592..0x003485c6).
        webLineOrientation_ = {
            camera->position.x - camera->target.x,
            camera->position.y - camera->target.y,
            camera->position.z - camera->target.z,
        };
        const float orientationLength = length3D(webLineOrientation_);
        if (orientationLength > std::numeric_limits<float>::epsilon()) {
            webLineOrientation_.x /= orientationLength;
            webLineOrientation_.y /= orientationLength;
            webLineOrientation_.z /= orientationLength;
        }
    }
    if (dead()) {
        lastActionRejectionReason_ = "dead";
        return false;
    }
    if (onWall()) {
        if (phase != PlayerButtonPhase::Pressed) {
            lastActionRejectionReason_ = "wall_web_requires_press";
            return false;
        }
        if (wallWeb_.active() || activeAttackState_ != nullptr ||
            hurtReactionRemainingMilliseconds_ != 0 ||
            (locomotionState_ != LocomotionState::WallIdle &&
             locomotionState_ != LocomotionState::WallMove)) {
            lastActionRejectionReason_ = "wall_state_busy";
            return false;
        }
        const auto* state = stateDatabase_ == nullptr ? nullptr :
            stateDatabase_->findState("k_state_onwall_web_qte_attack");
        if (state == nullptr || animationBank_ == nullptr || buttonDatabase_ == nullptr) {
            lastActionRejectionReason_ = "missing_wall_web_configuration";
            return false;
        }
        if (!target || !target->onWall || !target->canEnterWallWeb) {
            lastActionRejectionReason_ = "no_eligible_visible_wall_target";
            return false;
        }
        const assets::Vector3 direction{target->position.x - position_.x,
            target->position.y - position_.y, target->position.z - position_.z};
        const float distanceSquared = direction.x * direction.x +
            direction.y * direction.y + direction.z * direction.z;
        const float reach = state->motionParameters[1];
        if (distanceSquared > reach * reach) {
            lastActionRejectionReason_ = "wall_web_target_out_of_range";
            return false;
        }
        if (!wallWeb_.begin(WallWebRuntime::directionAngle(direction, wallNormal_),
                target->objectId, *state, *animationBank_, *buttonDatabase_)) {
            lastActionRejectionReason_ = "missing_wall_web_animation";
            return false;
        }
        activeAttackTarget_ = target;
        activeLocomotionState_ = state;
        locomotionState_ = LocomotionState::WallIdle;
        wallWebTargetAlive_ = true;
        wallWebActionPressed_ = false;
        setAnimation(wallWeb_.animation());
        queueEnteredState(state->name);
        return true;
    }
    if (locomotionState_ == LocomotionState::Grounded &&
        hurtReactionRemainingMilliseconds_ != 0) {
        lastActionRejectionReason_ = "hurt_reaction";
        return false;
    }
    if (locomotionState_ == LocomotionState::Grounded && !dead() &&
        hurtReactionRemainingMilliseconds_ == 0) {
        const PlayerStateDefinition* requested = nullptr;
        if (activeAttackState_ != nullptr) {
            if (!attackInputWindowOpen()) {
                lastActionRejectionReason_ = "attack_window_closed";
                return false;
            }
            requested = webTransition(phase);
        } else if (target.has_value() && target->airborne) {
            // Player::GetGroundWebSpecialState (0x00343f48) keeps the normal
            // web pellet for a grounded target. Its CEnemy::IsInAir virtual
            // redirects an airborne enemy to state 84 when its base is no
            // more than 160 cm above Spider-Man, or state 63 when higher.
            requested = target->position.z - position_.z <= 160.0F
                            ? groundWebFlyKickState_
                            : groundWebDragDownState_;
        } else {
            if (phase != PlayerButtonPhase::Pressed) {
                lastActionRejectionReason_ = "web_transition_unavailable";
                return false;
            }
            requested = groundWebShotState_;
        }
        if (requested == nullptr) {
            lastActionRejectionReason_ = "missing_transition";
            return false;
        }
        const std::optional<PlayerAttackTarget>& selectedTarget =
            activeAttackState_ != nullptr ? activeAttackTarget_ : target;
        if (selectedTarget.has_value()) {
            const float x = selectedTarget->position.x - position_.x;
            const float y = selectedTarget->position.y - position_.y;
            const float length = std::hypot(x, y);
            if (length > std::numeric_limits<float>::epsilon()) {
                facing_ = {x / length, y / length, 0.0F};
            }
        } else if (directionalInput.has_value()) {
            const float length = std::hypot(directionalInput->x,
                                            directionalInput->y);
            if (length > std::numeric_limits<float>::epsilon()) {
                facing_ = {directionalInput->x / length,
                           directionalInput->y / length, 0.0F};
            }
        }
        const bool requestedAirborne = requested == groundWebFlyKickState_;
        if (activeAttackState_ != nullptr) {
            // UpdateKeyTrigger leaves Player+0x594 pointing at the acquired
            // Unit while it buffers a ground-chain web transition. A target
            // need not be reacquired on the exact input frame; losing that
            // pointer here turns state 95 into an untargeted sector kick.
            queueAttackTransition(*requested, activeAttackTarget_,
                                  requestedAirborne);
            return true;
        }
        activeAttackTarget_ = target;
        activeAttackAirborne_ = requestedAirborne;
        if (!enterAttackState(*requested)) {
            activeAttackTarget_.reset();
            activeAttackAirborne_ = false;
            lastActionRejectionReason_ = "missing_animation";
            return false;
        }
        return true;
    }
    if (activeAttackState_ != nullptr) {
        if (!airborne() || !attackInputWindowOpen()) {
            lastActionRejectionReason_ = !airborne()
                ? "not_airborne"
                : "attack_window_closed";
            return false;
        }
        const PlayerStateDefinition* requested = webTransition(phase);
        if (requested == nullptr) {
            lastActionRejectionReason_ = "missing_transition";
            return false;
        }
        // As with the ground chain, ordinary Web predicates do not execute
        // UpdateKeyTrigger's target-search branch. Preserve Player+0x594.
        queueAttackTransition(*requested, activeAttackTarget_, true);
        return true;
    }
    if (!airborne() || locomotionState_ == LocomotionState::WebThrow ||
        locomotionState_ == LocomotionState::SwingHang || dead() ||
        swingThrowState_ == nullptr) {
        lastActionRejectionReason_ = dead() ? "dead" : "traversal_unavailable";
        return false;
    }
    if (phase != PlayerButtonPhase::Pressed) {
        lastActionRejectionReason_ = "web_transition_unavailable";
        return false;
    }
    assets::Vector3 visualPosition = position_;
    visualPosition.z = animatedFootHeight();
    // Player::UpdateMove (0x0035081c), including its state-19 branch at
    // 0x00350a8c, calls TryGrabPoint when either web button is pressed. Native
    // GetBestWebGrabPoint (0x00344424) excludes Player+0x5bc, the point that
    // launched the current release, while allowing an authored chain to a new
    // point before the release animation falls back to state 15.
    const std::int32_t currentPointId =
        selectedWebGrabPoint_ == nullptr ? -1 : selectedWebGrabPoint_->objectId;
    selectedWebGrabPoint_ = webGrabPointRuntime_.search(
        visualPosition, facing_, currentPointId);
    if (selectedWebGrabPoint_ == nullptr) {
        // UpdateMove (0x0035081c) calls TryGrabPoint while the current air
        // locomotion state still owns the input. UpdateKeyTrigger's pending
        // air-combat transition is therefore only used when no valid grab
        // point was accepted. This prevents nearby rooftop enemies from
        // stealing the authored traversal anchor during Level 2's chase.
        if (target.has_value()) {
            const float targetX = target->position.x - position_.x;
            const float targetY = target->position.y - position_.y;
            const float targetZ = target->position.z - position_.z;
            const float distanceSquared =
                targetX * targetX + targetY * targetY + targetZ * targetZ;
            // Player::GetAirWebSpecialState (0x00343de4) gives
            // CTargetHelper's nearest non-airborne list priority over the
            // ordinary capability queries. Its exact 40,000 cm^2 threshold
            // selects state 99 at close range and state 84 otherwise. Only
            // the fallback target then uses CanBeTiedUp/CanBeDragTo for
            // states 101/115.
            const PlayerStateDefinition* requested = !target->airborne
                ? (distanceSquared <= 40000.0F ? airKnockdownState_
                                               : groundWebFlyKickState_)
                : target->canBeTiedUp
                      ? airWebBindGroundState_
                      : target->canBeDraggedTo ? airDoubleWebBindState_
                                               : nullptr;
            if (requested != nullptr) {
                const float x = target->position.x - position_.x;
                const float y = target->position.y - position_.y;
                const float length = std::hypot(x, y);
                if (length > std::numeric_limits<float>::epsilon()) {
                    facing_ = {x / length, y / length, 0.0F};
                }
                activeAttackTarget_ = target;
                activeAttackAirborne_ = true;
                if (enterAttackState(*requested)) {
                    return true;
                }
                activeAttackTarget_.reset();
                activeAttackAirborne_ = false;
                return false;
            }
        }
        lastActionRejectionReason_ = "no_web_grab_point";
        return false;
    }
    const assets::Vector3 toPoint{
        selectedWebGrabPoint_->position.x - visualPosition.x,
        selectedWebGrabPoint_->position.y - visualPosition.y,
        selectedWebGrabPoint_->position.z - visualPosition.z,
    };
    // TryGrabPoint (0x00344990) selects hand 1 when
    // dot(cross(toPoint, globalUp), pointDirection) is negative.
    const float side =
        toPoint.y * selectedWebGrabPoint_->direction.x -
        toPoint.x * selectedWebGrabPoint_->direction.y;
    swingUsesLeftHand_ = side < 0.0F;
    position_ = visualPosition;
    renderPosition_ = visualPosition;
    jumpAnchorHeight_ = visualPosition.z;
    activeLocomotionState_ = swingThrowState_;
    locomotionState_ = LocomotionState::WebThrow;
    webReleaseRequested_ = false;
    const assets::ColladaAnimationClip* clip =
        clipById(animationBank_, swingUsesLeftHand_ ? 98 : 99);
    if (clip == nullptr) {
        selectedWebGrabPoint_ = nullptr;
        return false;
    }
    setAnimation(clip->name);
    queueEnteredState(swingThrowState_->name);
    updateWorldTransform(facing_);
    return true;
}

bool GameplayPlayer::requestUltimate() noexcept {
    lastActionRejectionReason_ = {};
    // Player::CanEnableUltimate (0x00345e98) rejects air, wall, slide,
    // trigger-jump, hurt/dead, and states 107-113. The direct-level Windows
    // runtime uses the normal red-suit branch selected by DoUltimate
    // (0x0034def4).
    if (dead() || hurtReactionRemainingMilliseconds_ != 0) {
        lastActionRejectionReason_ = dead() ? "dead" : "hurt_reaction";
        return false;
    }
    if (locomotionState_ != LocomotionState::Grounded ||
        activeAttackState_ != nullptr || wallWeb_.active() ||
        ultimatePrepareState_ == nullptr) {
        lastActionRejectionReason_ = "ultimate_unavailable";
        return false;
    }
    activeAttackTarget_.reset();
    activeAttackAirborne_ = false;
    ultimateActive_ = true;
    if (!enterAttackState(*ultimatePrepareState_)) {
        ultimateActive_ = false;
        lastActionRejectionReason_ = "missing_ultimate_animation";
        return false;
    }
    return true;
}

bool GameplayPlayer::requestSpiderSense(
    const PlayerAttackTarget& attacker) noexcept {
    lastActionRejectionReason_ = {};
    if (dead() || hurtReactionRemainingMilliseconds_ != 0 ||
        wallWeb_.active() || stateDatabase_ == nullptr) {
        lastActionRejectionReason_ = "sense_unavailable";
        return false;
    }
    if (!canEnableSpiderSense()) {
        lastActionRejectionReason_ = "sense_transition_locked";
        return false;
    }
    const float x = attacker.position.x - position_.x;
    const float y = attacker.position.y - position_.y;
    const float z = attacker.position.z - position_.z;
    const float distance = std::sqrt(x * x + y * y + z * z);
    const float horizontal = std::hypot(x, y);
    assets::Vector3 direction = facing_;
    if (horizontal > std::numeric_limits<float>::epsilon()) {
        direction = {x / horizontal, y / horizontal, 0.0F};
    }
    const float forward = direction.x * facing_.x + direction.y * facing_.y;
    const float side = facing_.x * direction.y - facing_.y * direction.x;
    std::size_t quadrant = 0; // front
    if (forward <= -0.70710678F) {
        quadrant = 1; // back
    } else if (forward < 0.70710678F) {
        quadrant = side >= 0.0F ? 2U : 3U; // left/right
    }

    // CheckBlinkStrike (0x00342550) precedes the ordinary directional choice.
    // It admits a grounded/landing player while CEnemy::
    // IsNearAttackKeyFrame reports the authored -200/+49 ms attack window.
    // A normal red-suit profile then selects state 42/motion 0x1fd. The
    // black-suit state is retained and validated above for its later native
    // profile branch, but the current chronological build is red-suit only.
    const bool canBlinkStrike = attacker.nearAttackKeyFrame &&
        (locomotionState_ == LocomotionState::Grounded ||
         std::abs(verticalVelocityCentimetersPerSecond_) <=
             kPlayerCollisionRadiusCentimeters);
    const PlayerStateDefinition* requested = nullptr;
    if (canBlinkStrike) {
        requested = senseBlinkRedState_;
    } else {
        // DoNormalSenseAction (0x0034f630, 0x0034f80a-0x0034f8cc) does not
        // select an evade matching the attacker's quadrant. Reaction types
        // 1/6 either use the matching counter [38,40,41,39], or choose one
        // of the perpendicular evade pair with native random(100)'s 50
        // split. Types 2..5 index the fixed table [35,34,36,37]. Enemy
        // subtype 5 is forced to state 35 after either ordinary selection.
        const bool ordinaryReaction = attacker.senseReactionType == 1 ||
                                      attacker.senseReactionType == 6;
        if (ordinaryReaction) {
            const bool earlySenseState = activeAttackState_ != nullptr &&
                activeAttackState_->stateClass == 6 &&
                activeAttackState_->motionType < 0x1f9;
            const bool evade = distance > 200.0F ||
                !attacker.canBeCounterHit ||
                locomotionState_ != LocomotionState::Grounded ||
                earlySenseState || attacker.senseReactionType == 6;
            if (!evade) {
                requested = senseAttackStates_[quadrant];
            } else {
                const bool firstVariant =
                    (senseAvoidVariantCursor_++ & 1U) == 0;
                if (quadrant == 2 || quadrant == 3) {
                    requested = senseAvoidStates_[firstVariant ? 0 : 1];
                } else {
                    requested = senseAvoidStates_[firstVariant ? 2 : 3];
                }
            }
        } else if (attacker.senseReactionType >= 2 &&
                   attacker.senseReactionType <= 5) {
            constexpr std::array<std::size_t, 4> reactionToAvoid{
                1, 0, 2, 3};
            requested = senseAvoidStates_[reactionToAvoid[
                static_cast<std::size_t>(attacker.senseReactionType - 2)]];
        } else {
            requested = senseAvoidStates_[1];
        }
        if (attacker.enemySubType == 5) {
            // CEnemy::GetSubType (0x00327ce4) returns CEnemy+0x38c; subtype
            // 5 overrides every non-blink response after selection.
            requested = senseAvoidStates_[1];
        }
    }
    if (requested == nullptr) {
        lastActionRejectionReason_ = "missing_sense_state";
        return false;
    }
    if (activeAttackState_ != nullptr) {
        cancelAttack();
    }
    facing_ = direction;
    activeAttackTarget_ = attacker;
    activeAttackAirborne_ =
        locomotionState_ != LocomotionState::Grounded;
    ultimateActive_ = true;
    if (!enterAttackState(*requested)) {
        activeAttackTarget_.reset();
        activeAttackAirborne_ = false;
        ultimateActive_ = false;
        lastActionRejectionReason_ = "missing_sense_animation";
        return false;
    }
    return true;
}

std::vector<WebGrabCandidateDiagnostics>
GameplayPlayer::webGrabCandidateDiagnostics() const {
    assets::Vector3 visualPosition = position_;
    visualPosition.z = animatedFootHeight();
    const std::int32_t currentPointId =
        selectedWebGrabPoint_ == nullptr ? -1 : selectedWebGrabPoint_->objectId;
    return webGrabPointRuntime_.diagnose(visualPosition, facing_,
                                         currentPointId);
}

bool GameplayPlayer::releaseWeb() noexcept {
    if (locomotionState_ == LocomotionState::WebThrow) {
        webReleaseRequested_ = true;
        return true;
    }
    if (locomotionState_ != LocomotionState::SwingHang) {
        return false;
    }
    enterSwingRelease();
    return true;
}

bool GameplayPlayer::applyDamage(
    float damage, std::int32_t damageType,
    std::uint32_t minimumReactionMilliseconds,
    std::int32_t nativeHitType) noexcept {
    // Player::IsCanBeHit (0x003413dc) rejects ordinary-priority hits while
    // the current state id is in the inclusive 107..113 ultimate range. The
    // original OnHit returns before both health damage and state transitions;
    // allowing a thug hit here cancels state 109 before its authored splash.
    const bool ultimateInvulnerability =
        activeAttackState_ != nullptr && activeAttackState_->id >= 107 &&
        activeAttackState_->id <= 113;
    // Successful spider-sense states are the shipped evade/counter response
    // to a registered attack; the attacker is notified before the state is
    // entered and its pending hit must not land through that response.
    if (damage <= 0.0F || dead() || ultimateInvulnerability ||
        (activeAttackState_ != nullptr &&
         activeAttackState_->stateClass == 6)) {
        return false;
    }
    health_ = std::max(0.0F, health_ - damage);
    wallWeb_.cancel();
    // Player::OnHit (0x0034d790) sets Player+0x584 after accepting the hurt
    // transition. UpdateComboState consumes it as an immediate combo flush.
    if (pendingComboCount() > 0) {
        forceComboFinish_ = true;
    }
    if (dead()) {
        // Player::CheckDeath (0x0034cfcc) asks IsDead (vtable +0x140),
        // releases both web lines, and normally enters state 0x80. CLevel
        // then recognizes IsDeadOver (vtable +0x144) on the next update.
        enterDeadState();
        return true;
    }
    // Player::OnHit (0x0034d790) maps grounded hit type 100 to state 44 and
    // hit types 101/0x85/0x86 to state 45.  The opening knife therefore uses
    // the light reaction while both authored bat attacks use the substantially
    // longer, backward-moving heavy reaction.
    const bool heavyGroundHit = damageType == 1 || nativeHitType == 101 ||
                                nativeHitType == 0x85 || nativeHitType == 0x86;
    const PlayerStateDefinition* hurtState =
        heavyGroundHit ? hurtHeavyState_ : hurtLightState_;
    // Player::IsOnWall (0x003411cc) excludes motion 15 (roof exit).
    // onWall() also covers that locomotion path for portable traversal
    // updates, but it must not turn a roof exit into a wall-idle reaction.
    const bool wallHit = onWall() &&
        locomotionState_ != LocomotionState::WallExit;
    if (wallHit && stateDatabase_ != nullptr) {
        // Player::OnHit (0x0034d790): hit types >0x67 select state 50;
        // lighter hits select 51. Keep the wall's normal and attachment.
        hurtState = stateDatabase_->findState(nativeHitType > 0x67
            ? "k_state_hurt_onwall" : "k_state_hurt_onwall_light");
    }
    const assets::ColladaAnimationClip* hurtClip =
        stateClip(animationBank_, hurtState);
    if (!dead() && !cinematicDriven_ && !quickTimeActionDriven_ &&
        (locomotionState_ == LocomotionState::Grounded || wallHit) &&
        hurtClip != nullptr) {
        cancelAttack();
        activeLocomotionState_ = hurtState;
        queueEnteredState(hurtState->name);
        setAnimation(hurtClip->name);
        applyAttackRootMotion(animationPhysicalDisplacement(hurtState, 0),
                              animationRenderOffset(hurtState, 0));
        // UpdateHurt (0x0035062c) leaves the wall reaction when its clip
        // finishes. CEffectDamage's 1000 ms Player+0x708 immunity is not
        // a minimum animation length; LevelDamageRuntime tracks that
        // contact cooldown separately.
        hurtReactionRemainingMilliseconds_ = wallHit
            ? hurtClip->durationMilliseconds()
            : std::max(minimumReactionMilliseconds,
                       hurtClip->durationMilliseconds());
    }
    return true;
}

void GameplayPlayer::enterDeadState() noexcept {
    wallWeb_.cancel();
    if (activeLocomotionState_ == deadOverState_ && deadOverState_ != nullptr) {
        return;
    }
    cancelAttack();
    selectedWebGrabPoint_ = nullptr;
    webSwingRuntime_ = {};
    webReleaseRequested_ = false;
    cinematicMotion_ = {};
    activeLocomotionState_ = deadOverState_;
    locomotionState_ = LocomotionState::Grounded;
    locomotionRootTranslation_ = {};
    verticalVelocityCentimetersPerSecond_ = 0.0F;
    hurtReactionRemainingMilliseconds_ = 0;
    if (deadOverState_ != nullptr) {
        queueEnteredState(deadOverState_->name);
    }
}

void GameplayPlayer::addHealth(float health) noexcept {
    if (health > 0.0F && !dead()) {
        health_ = std::min(maximumHealth_, health_ + health);
    }
}

void GameplayPlayer::addSkillPoints(std::int32_t points) noexcept {
    if (points > 0) {
        skillPoints_ += points;
    }
}

void GameplayPlayer::addCombo(float actualDamage, bool ultimateActive,
                              std::uint64_t timeMilliseconds) noexcept {
    // Player::AddCombo (0x00340584). Player+0x4fd is the ultimate-mode flag:
    // DoUltimate (0x0034def4) sets it and ResetObject (0x0034e258) clears it.
    // It is not an airborne classifier.
    if (!(actualDamage > 0.0F) || !std::isfinite(actualDamage)) {
        return;
    }
    if (ultimateActive) {
        ultimateComboDamage_ += actualDamage;
        ++ultimateComboCount_;
    } else {
        normalComboDamage_ += actualDamage;
        ++normalComboCount_;
    }
    maximumComboCount_ =
        std::max(maximumComboCount_, pendingComboCount());
    comboTouchedMilliseconds_ = timeMilliseconds;
}

void GameplayPlayer::updateComboState(
    std::uint64_t timeMilliseconds) noexcept {
    // Player::UpdateComboState (0x003458d0) uses a strict two-second timeout
    // or Player+0x584, performs two integer truncations, then clears only the
    // pending buckets. Player::GetComboScore is 0x003414e4.
    const bool timedOut =
        timeMilliseconds > comboTouchedMilliseconds_ &&
        timeMilliseconds - comboTouchedMilliseconds_ >
            kComboTimeoutMilliseconds;
    if (!timedOut && !forceComboFinish_) {
        return;
    }

    const float hardAttackRate = kAttackPowerHardLevel[kDefaultDifficulty];
    const float attackUpgradeRate = comboUpgradeRate(kAttackPowerUpgrade);
    const float magicUpgradeRate = comboUpgradeRate(kMagicPowerUpgrade);
    const float normalMultiplier =
        static_cast<float>(normalComboCount_) * 0.01F * hardAttackRate *
            attackUpgradeRate +
        1.0F;
    comboScore_ = static_cast<std::int32_t>(
        static_cast<float>(comboScore_) +
        normalComboDamage_ * normalMultiplier);
    const float ultimateMultiplier =
        static_cast<float>(ultimateComboCount_) * 0.01F * hardAttackRate *
            magicUpgradeRate +
        1.0F;
    comboScore_ = static_cast<std::int32_t>(
        static_cast<float>(comboScore_) +
        ultimateComboDamage_ * ultimateMultiplier);
    completedComboHitCount_ += pendingComboCount();
    normalComboCount_ = 0;
    ultimateComboCount_ = 0;
    normalComboDamage_ = 0.0F;
    ultimateComboDamage_ = 0.0F;
    forceComboFinish_ = false;
}

void GameplayPlayer::setComboScore(std::int32_t score) noexcept {
    comboScore_ = score;
}

void GameplayPlayer::restoreAt(const assets::Vector3& position,
                               const assets::Vector3& facing) noexcept {
    wallWeb_.cancel();
    position_ = position;
    renderPosition_ = position;
    jumpAnchorHeight_ = position.z;
    const float facingLength = length2D(facing.x, facing.y);
    if (facingLength > std::numeric_limits<float>::epsilon()) {
        facing_ = {facing.x / facingLength, facing.y / facingLength, 0.0F};
    }
    activeLocomotionState_ = nullptr;
    locomotionState_ = LocomotionState::Grounded;
    locomotionRootTranslation_ = {};
    verticalVelocityCentimetersPerSecond_ = 0.0F;
    swingReleaseVelocity_ = {};
    swingReleaseSteeringVelocity_ = {};
    swingReleaseDecayVelocity_ = {};
    swingReleaseHasTarget_ = false;
    selectedWebGrabPoint_ = nullptr;
    wallNormal_ = {};
    lastWallInput_ = {};
    cinematicMotion_ = {};
    webSwingRuntime_ = {};
    webReleaseRequested_ = false;
    cancelAttack();
    activeHitEffects_.clear();
    pendingHitEffectSpawnCount_ = 0;
    hurtReactionRemainingMilliseconds_ = 0;
    activeScriptedState_ = nullptr;
    scriptedStateLoops_ = false;
    setAnimation("idle_stand");
    updateWorldTransform(facing_);
}

void GameplayPlayer::restartAt(const assets::Vector3& position,
                               const assets::Vector3& facing) noexcept {
    // CLevel::ResetLevel (0x003832d0) calls Player::ResetObject
    // (0x0034e258) before RestartAtCheckPoint places the player. The base
    // CGameObject reset restores authored health; the Player override clears
    // attack, web, QTE, hurt, target, and state data.
    health_ = maximumHealth_;
    cinematicDriven_ = false;
    cinematicAnimationLoops_ = true;
    cinematicAnimationSpeed_ = 1.0F;
    quickTimeActionDriven_ = false;
    quickTimeActionLoops_ = false;
    quickTimeActionDetachPosition_ = {};
    quickTimeActionHasDetachPosition_ = false;
    activeScriptedState_ = nullptr;
    scriptedStateLoops_ = false;
    pendingMeleeImpactCount_ = 0;
    pendingWebPelletLaunchCount_ = 0;
    pendingAttackSoundTriggerCount_ = 0;
    pendingCombatEffectCount_ = 0;
    pendingVoxStopEventCount_ = 0;
    activeHitEffects_.clear();
    pendingHitEffectSpawnCount_ = 0;
    enteredStateCount_ = 0;
    normalComboCount_ = 0;
    ultimateComboCount_ = 0;
    normalComboDamage_ = 0.0F;
    ultimateComboDamage_ = 0.0F;
    maximumComboCount_ = 0;
    completedComboHitCount_ = 0;
    comboTouchedMilliseconds_ = 0;
    comboScore_ = 0;
    forceComboFinish_ = false;
    restoreAt(position, facing);
}

void GameplayPlayer::loadCheckPointAt(
    const assets::Vector3& position,
    const assets::Vector3& facing) noexcept {
    // Player::Save/Load (0x0034078c/0x003407f0) wraps CGameObject::Save/Load
    // (0x0030f04c/0x0030f188), so checkpoint health and progression survive
    // ResetLevel while transient attack/death/QTE state does not.
    const float savedHealth = health_;
    const std::int32_t savedSkillPoints = skillPoints_;
    const std::int32_t savedComboScore = comboScore_;
    restartAt(position, facing);
    health_ = std::clamp(savedHealth, 0.0F, maximumHealth_);
    skillPoints_ = savedSkillPoints;
    comboScore_ = savedComboScore;
}

void GameplayPlayer::applySupportingBodyMotion(
    const assets::Vector3& delta) noexcept {
    // PhysicsEntity::preUpdate (0x003d799c) applies the transmission body's
    // linear/displacement/angular motion before the player controller runs.
    // Keep the render root and airborne reference coherent with that same
    // world-space carry.
    position_.x += delta.x;
    position_.y += delta.y;
    position_.z += delta.z;
    renderPosition_.x += delta.x;
    renderPosition_.y += delta.y;
    renderPosition_.z += delta.z;
    jumpAnchorHeight_ += delta.z;
    updateWorldTransform(facing_);
}

Result GameplayPlayer::applyCinematicCommand(
    const CinematicThread& thread, const CinematicCommand& command) {
    // Enable_Slide is command 0x69 in CCinematicThread::doCommand
    // (0x003727e4) and targets a level object by authored ID; it is not a
    // Player-thread command despite this runtime owning slide traversal.
    if (command.name == "Enable_Slide") {
        return slideRuntime_.applyCinematicCommand(command);
    }
    if (command.name == "Restore") {
        // CCinematicThread::OnRestore (0x0036fe0c) resolves the active player
        // through CLevel+0x60[current-player] and writes -1.0f directly to
        // Unit+0x64. Player::IsDead (0x00340084) reads that same health field;
        // Player::CheckDeath performs the state-0x80 transition on update.
        health_ = -1.0F;
        return Result::success();
    }
    // CCinematicThread::Init (0x00371de0) resolves every type-3 thread to
    // CLevel's active player and deliberately ignores the serialized object
    // ID.  Level 2 cinematic 467 retains the stale value 1141 (also an enemy
    // ID), but its MoveObject/AI commands still target Spider-Man natively.
    if (thread.type != 3) {
        return Result::success();
    }
    if (command.name == "DisableAI") {
        wallWeb_.cancel();
        cinematicDriven_ = true;
        cancelAttack();
        return Result::success();
    }
    if (command.name == "EnableAI") {
        cinematicDriven_ = false;
        cinematicAnimationSpeed_ = 1.0F;
        cinematicMotion_ = {};
        locomotionState_ = LocomotionState::Grounded;
        locomotionRootTranslation_ = {};
        renderPosition_ = position_;
        return Result::success();
    }
    if (command.name == "SetAnim") {
        const CinematicAttribute* animation = command.findAttribute("$Anim");
        if (animation == nullptr || animationBank_ == nullptr) {
            return Result::success();
        }
        const assets::ColladaAnimationClip* clip =
            animationBank_->findClip(animation->value);
        if (clip == nullptr) {
            // AnimationProxy::GetAnimIdByName (0x0038e9b0) returns -1 for an
            // unresolved name. IAnimatedObject::SetAnim (0x00310fec) then
            // leaves the current animation untouched. The shipped Level 3
            // arrival script deliberately contains two such stale names.
            return Result::success();
        }
        setAnimation(clip->name);
        if (const CinematicAttribute* loop = command.findAttribute("loop")) {
            cinematicAnimationLoops_ = parseBoolean(loop->value, true);
        }
        if (const CinematicAttribute* speed = command.findAttribute("speed")) {
            cinematicAnimationSpeed_ = parseFloat(speed->value, 1.0F);
        }
        return Result::success();
    }
    if (command.name == "MoveObject") {
        if (const CinematicAttribute* absolute =
                command.findAttribute("abspos")) {
            position_ = parseVector3(absolute->value, position_);
        } else if (const CinematicAttribute* local =
                       command.findAttribute("pos")) {
            position_ = parseVector3(local->value, position_);
        }
        assets::Quaternion rotation{};
        rotation.w = 1.0F;
        if (const CinematicAttribute* authoredRotation =
                command.findAttribute("rot")) {
            rotation = parseQuaternion(authoredRotation->value, rotation);
        }
        renderPosition_ = position_;
        jumpAnchorHeight_ = position_.z;
        worldTransform_ = scriptedWorldMatrix(position_, rotation, scale_);
        const float faceLength =
            std::hypot(worldTransform_[4], worldTransform_[5]);
        if (faceLength > std::numeric_limits<float>::epsilon()) {
            facing_ = {-worldTransform_[4] / faceLength,
                       -worldTransform_[5] / faceLength, 0.0F};
        }
        cinematicMotion_ = {};
        if (const CinematicCommand* next =
                nextMoveObjectCommand(thread, command);
            next != nullptr &&
            next->timestampMilliseconds >=
                command.timestampMilliseconds + 51U) {
            const CinematicAttribute* nextAbsolute =
                next->findAttribute("abspos");
            const CinematicAttribute* nextRotation =
                next->findAttribute("rot");
            if (nextAbsolute != nullptr && nextRotation != nullptr) {
                // CCinematicThread::MoveObject (0x00370494) stores this
                // key and the next authored key on the bound object thread.
                cinematicMotion_.startPosition = position_;
                cinematicMotion_.endPosition =
                    parseVector3(nextAbsolute->value, position_);
                cinematicMotion_.startRotation = rotation;
                cinematicMotion_.endRotation =
                    parseQuaternion(nextRotation->value, rotation);
                cinematicMotion_.durationMilliseconds =
                    next->timestampMilliseconds - command.timestampMilliseconds;
                cinematicMotion_.active = true;
            }
        }
        return Result::success();
    }
    if (command.name != "GetDamage") {
        return Result::success();
    }
    const CinematicAttribute* damage = command.findAttribute("DamageValue");
    if (damage == nullptr) {
        return Result::failure("Player GetDamage is missing DamageValue");
    }
    float value = 0.0F;
    const char* begin = damage->value.data();
    const char* end = begin + damage->value.size();
    const auto parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end || value < 0.0F) {
        return Result::failure("Player GetDamage has an invalid DamageValue");
    }
    (void)applyDamage(value);
    return Result::success();
}

void GameplayPlayer::update(const PlayerMotionInput& input,
                            const CameraPose& camera,
                            std::uint32_t elapsedMilliseconds) noexcept {
    // CGameObject::Update and UpdateStateFrame run before native input
    // predicates, but a state selected by that input starts at frame zero and
    // is not advanced until the next outer update. prepareInputFrame bridges
    // that phase ordering while preserving this single public update call.
    const bool deferNewAttackAdvance =
        inputFramePrepared_ && attackEnteredDuringPreparedInputFrame_;
    inputFramePrepared_ = false;
    attackEnteredDuringPreparedInputFrame_ = false;
    inputFrameAdvanceMilliseconds_ = 0;
    const std::int32_t nextMovementVirtualKey = movementVirtualKey(input);
    const bool movementPressed = nextMovementVirtualKey >= 0 &&
        nextMovementVirtualKey != movementVirtualKey_;
    movementVirtualKey_ = nextMovementVirtualKey;
    for (PlayerHitEffectState& effect : activeHitEffects_) {
        const float seconds =
            static_cast<float>(elapsedMilliseconds) / 1000.0F;
        effect.driftOffset.x += effect.capturedPhysicsVelocity.x * seconds;
        effect.driftOffset.y += effect.capturedPhysicsVelocity.y * seconds;
        effect.driftOffset.z += effect.capturedPhysicsVelocity.z * seconds;
        effect.elapsedMilliseconds = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(
                static_cast<std::uint64_t>(effect.elapsedMilliseconds) +
                    elapsedMilliseconds,
                effect.lifetimeMilliseconds));
    }
    std::erase_if(activeHitEffects_, [](const PlayerHitEffectState& effect) {
        return effect.elapsedMilliseconds >= effect.lifetimeMilliseconds;
    });
    slideRuntime_.advanceCooldown(elapsedMilliseconds);
    if (dead()) {
        enterDeadState();
        return;
    }
    if (quickTimeActionDriven_) {
        return;
    }
    if (activeScriptedState_ != nullptr) {
        const assets::ColladaAnimationClip* clip =
            stateClip(animationBank_, activeScriptedState_);
        animationTimeMilliseconds_ += elapsedMilliseconds;
        if (clip != nullptr && clip->durationMilliseconds() != 0) {
            if (scriptedStateLoops_) {
                animationTimeMilliseconds_ %= clip->durationMilliseconds();
            } else {
                animationTimeMilliseconds_ = std::min<std::uint64_t>(
                    animationTimeMilliseconds_,
                    clip->durationMilliseconds());
            }
        }
        return;
    }
    if (cinematicDriven_) {
        if (cinematicMotion_.active &&
            cinematicMotion_.durationMilliseconds != 0) {
            // CCinematicThread::DoExecChange (0x00371880) evaluates the
            // current interpolation clock before adding the frame delta.
            const float factor = std::clamp(
                static_cast<float>(cinematicMotion_.elapsedMilliseconds) /
                    static_cast<float>(
                        cinematicMotion_.durationMilliseconds),
                0.0F, 1.0F);
            position_ = {
                cinematicMotion_.startPosition.x +
                    (cinematicMotion_.endPosition.x -
                     cinematicMotion_.startPosition.x) * factor,
                cinematicMotion_.startPosition.y +
                    (cinematicMotion_.endPosition.y -
                     cinematicMotion_.startPosition.y) * factor,
                cinematicMotion_.startPosition.z +
                    (cinematicMotion_.endPosition.z -
                     cinematicMotion_.startPosition.z) * factor};
            const assets::Quaternion rotation = slerp(
                cinematicMotion_.startRotation,
                cinematicMotion_.endRotation, factor);
            renderPosition_ = position_;
            jumpAnchorHeight_ = position_.z;
            worldTransform_ = scriptedWorldMatrix(position_, rotation, scale_);
            const float faceLength =
                std::hypot(worldTransform_[4], worldTransform_[5]);
            if (faceLength > std::numeric_limits<float>::epsilon()) {
                facing_ = {-worldTransform_[4] / faceLength,
                           -worldTransform_[5] / faceLength, 0.0F};
            }
            const std::uint64_t next =
                static_cast<std::uint64_t>(
                    cinematicMotion_.elapsedMilliseconds) +
                elapsedMilliseconds;
            cinematicMotion_.elapsedMilliseconds =
                static_cast<std::uint32_t>(std::min<std::uint64_t>(
                    next, cinematicMotion_.durationMilliseconds));
            if (next >= cinematicMotion_.durationMilliseconds) {
                cinematicMotion_.active = false;
            }
        }
        const double advanced = static_cast<double>(elapsedMilliseconds) *
                                cinematicAnimationSpeed_;
        animationTimeMilliseconds_ += static_cast<std::uint64_t>(
            std::max(advanced, 0.0));
        if (!cinematicAnimationLoops_ && animationBank_ != nullptr) {
            const assets::ColladaAnimationClip* clip =
                animationBank_->findClip(activeAnimation_);
            if (clip != nullptr) {
                const std::uint32_t finalPoseTime =
                    clip->durationMilliseconds() == 0
                        ? 0
                        : clip->durationMilliseconds() - 1;
                animationTimeMilliseconds_ = std::min<std::uint64_t>(
                    animationTimeMilliseconds_, finalPoseTime);
            }
        }
        return;
    }
    if (hurtReactionRemainingMilliseconds_ != 0) {
        const std::uint32_t step = std::min(
            elapsedMilliseconds, hurtReactionRemainingMilliseconds_);
        animationTimeMilliseconds_ += step;
        hurtReactionRemainingMilliseconds_ -= step;
        if (activeLocomotionState_ != nullptr) {
            // Player::Update (0x00353494) applies Unit::UpdateDisplacement
            // after UpdateHurt. GetAnimOffseted (0x003400f0) does not mask
            // motion 205: wall_idle_to_hurt really moves down the wall.
            const auto* clip = stateClip(animationBank_, activeLocomotionState_);
            if (clip != nullptr && clip->durationMilliseconds() != 0) {
                const auto rootTime = std::min(animationTimeMilliseconds(),
                                              clip->durationMilliseconds() - 1);
                applyAttackRootMotion(
                    animationPhysicalDisplacement(activeLocomotionState_, rootTime),
                    animationRenderOffset(activeLocomotionState_, rootTime));
            }
        }
        if (hurtReactionRemainingMilliseconds_ == 0) {
            if (onWall()) {
                cancelAttack();
                enterLocomotionState(LocomotionState::WallIdle);
            } else {
                enterLocomotionState(LocomotionState::Grounded);
            }
        }
        return;
    }
    if (wallWeb_.active()) {
        wallWeb_.update(elapsedMilliseconds, wallWebActionPressed_, wallWebTargetAlive_);
        wallWebActionPressed_ = false;
        if (wallWeb_.active()) {
            setAnimation(wallWeb_.animation());
            animationTimeMilliseconds_ = wallWeb_.animationMilliseconds();
        } else {
            activeAttackTarget_.reset();
            enterLocomotionState(LocomotionState::WallIdle);
        }
        updateWorldTransform(facing_);
        return;
    }
    if (activeAttackState_ != nullptr) {
        if (movementPressed) {
            constexpr std::array<std::int16_t, 1> pressed{
                kPressedTransition};
            const PlayerStateDefinition* transition =
                transitionForButton(kMovementButton, pressed);
            assets::Vector3 movement;
            float inputMagnitude{};
            if (transition != nullptr &&
                calculateMovement(input, camera, facing_, movement,
                                  inputMagnitude)) {
                // UpdateKeyTrigger calls RotatePlayerByFixedDir before it
                // accepts event zero. RotatePlayerByFixedDir delegates to
                // Unit::SetFaceDir, whose source vector is the current
                // camera-relative joystick direction.
                facing_ = movement;
                queueAttackTransition(*transition, activeAttackTarget_,
                                      activeAttackAirborne_);
            }
        }
        updateAttack(deferNewAttackAdvance ? 0U : elapsedMilliseconds);
        return;
    }
    if ((locomotionState_ == LocomotionState::Grounded ||
         locomotionState_ == LocomotionState::JumpFall ||
         locomotionState_ == LocomotionState::SustainedFall ||
         locomotionState_ == LocomotionState::JumpLand ||
         locomotionState_ == LocomotionState::SwingRelease ||
         locomotionState_ == LocomotionState::SliderJumpFall) &&
        tryCatchSlide()) {
        updateSlideTraversal(elapsedMilliseconds);
        return;
    }
    if (locomotionState_ == LocomotionState::SliderLand ||
        locomotionState_ == LocomotionState::SliderMove) {
        updateSlideTraversal(elapsedMilliseconds);
        return;
    }
    if (onWall()) {
        updateWallTraversal(input, elapsedMilliseconds);
        return;
    }
    if (locomotionState_ == LocomotionState::WebThrow ||
        locomotionState_ == LocomotionState::SwingHang ||
        locomotionState_ == LocomotionState::SwingRelease) {
        updateWebTraversal(input, camera, elapsedMilliseconds);
        return;
    }
    if (locomotionState_ != LocomotionState::Grounded) {
        updateJump(input, camera, elapsedMilliseconds);
        return;
    }
    float inputMagnitude{};
    assets::Vector3 movement;
    if (!calculateMovement(input, camera, facing_, movement, inputMagnitude) ||
        elapsedMilliseconds == 0) {
        setAnimation("idle_stand");
        animationTimeMilliseconds_ += elapsedMilliseconds;
        return;
    }

    // The original constant at image address 0x004c6a7c is 700 cm/s and
    // GetMCSpeedByJoyStick divides it by 1000 for millisecond timesteps.
    const float distance = kMaximumRunSpeedCentimetersPerSecond *
                           inputMagnitude *
                           (static_cast<float>(elapsedMilliseconds) / 1000.0F);
    assets::Vector3 desired = position_;
    desired.x += movement.x * distance;
    desired.y += movement.y * distance;
    if (tryAttachWall(movement)) {
        updateWallTraversal(input, elapsedMilliseconds);
        return;
    }
    if (collision_ != nullptr) {
        assets::Vector3 resolved;
        if (collision_->resolveGroundMotion(
                position_, desired, resolved, 75.0F, 150.0F, 0U,
                LevelCollisionDepenetration::TowardAuthoredNormal)) {
            position_ = resolved;
        } else {
            collision_->resolveAirMotion(
                position_, desired, position_, 0U,
                LevelCollisionDepenetration::TowardAuthoredNormal);
            jumpAnchorHeight_ = position_.z;
            enterLocomotionState(LocomotionState::SustainedFall);
        }
    } else {
        position_ = desired;
    }
    renderPosition_ = position_;
    facing_ = movement;
    updateWorldTransform(facing_);
    if (locomotionState_ == LocomotionState::Grounded) {
        setAnimation("run");
    }
    animationTimeMilliseconds_ += elapsedMilliseconds;
}

void GameplayPlayer::enterLocomotionState(LocomotionState state) noexcept {
    sustainedFallCarriesSwingVelocity_ = false;
    locomotionState_ = state;
    switch (state) {
    case LocomotionState::Grounded:
        activeLocomotionState_ = nullptr;
        selectedWebGrabPoint_ = nullptr;
        locomotionRootTranslation_ = {};
        verticalVelocityCentimetersPerSecond_ = 0.0F;
        renderPosition_ = position_;
        setAnimation("idle_stand");
        break;
    case LocomotionState::JumpStart:
        activeLocomotionState_ = jumpStartState_;
        break;
    case LocomotionState::JumpFall:
        activeLocomotionState_ = jumpFallState_;
        break;
    case LocomotionState::ShortWebJump:
        activeLocomotionState_ = shortWebJumpState_;
        locomotionRootTranslation_ = {};
        break;
    case LocomotionState::SustainedFall:
        activeLocomotionState_ = sustainedFallState_;
        verticalVelocityCentimetersPerSecond_ =
            kSustainedFallSpeedCentimetersPerSecond;
        renderPosition_ = position_;
        break;
    case LocomotionState::JumpLand:
        activeLocomotionState_ = jumpLandState_;
        verticalVelocityCentimetersPerSecond_ = 0.0F;
        renderPosition_ = position_;
        break;
    case LocomotionState::WebThrow:
    case LocomotionState::SwingHang:
    case LocomotionState::SwingRelease:
        break;
    case LocomotionState::SliderLand:
        activeLocomotionState_ = sliderLandState_;
        selectedWebGrabPoint_ = nullptr;
        break;
    case LocomotionState::SliderMove:
        activeLocomotionState_ = sliderMoveState_;
        selectedWebGrabPoint_ = nullptr;
        slideMoveElapsedMilliseconds_ = 0;
        break;
    case LocomotionState::SliderJumpUp:
        activeLocomotionState_ = sliderJumpUpState_;
        selectedWebGrabPoint_ = nullptr;
        locomotionRootTranslation_ = {};
        break;
    case LocomotionState::SliderJumpFall:
        activeLocomotionState_ = sliderJumpFallState_;
        selectedWebGrabPoint_ = nullptr;
        locomotionRootTranslation_ = {};
        renderPosition_ = position_;
        break;
    case LocomotionState::WallAttach:
        activeLocomotionState_ = wallAttachState_;
        break;
    case LocomotionState::WallIdle:
        activeLocomotionState_ = wallIdleState_;
        break;
    case LocomotionState::WallMove:
        activeLocomotionState_ = wallMoveState_;
        break;
    case LocomotionState::WallExit:
        activeLocomotionState_ = wallExitState_;
        break;
    case LocomotionState::WallJump:
        break;
    }
    if (activeLocomotionState_ != nullptr) {
        const assets::ColladaAnimationClip* clip =
            state == LocomotionState::SliderJumpUp
                ? clipById(animationBank_, slideJumpUpAnimationId_)
                : state == LocomotionState::SliderJumpFall
                    ? clipById(animationBank_, slideJumpFallAnimationId_)
                    : stateClip(animationBank_, activeLocomotionState_);
        if (clip != nullptr) {
            setAnimation(clip->name);
        }
        if (state == LocomotionState::JumpStart ||
            state == LocomotionState::ShortWebJump ||
            state == LocomotionState::JumpLand ||
            state == LocomotionState::SliderMove ||
            state == LocomotionState::SliderJumpUp ||
            state == LocomotionState::SliderJumpFall ||
            state == LocomotionState::WallAttach ||
            state == LocomotionState::WallIdle ||
            state == LocomotionState::WallExit) {
            queueEnteredState(activeLocomotionState_->name);
        }
    }
    updateWorldTransform(facing_);
}

void GameplayPlayer::updateAirHorizontalMotion(
    const PlayerMotionInput& input, const CameraPose& camera,
    std::uint32_t elapsedMilliseconds) noexcept {
    float inputMagnitude{};
    assets::Vector3 movement;
    if (elapsedMilliseconds == 0 ||
        !calculateMovement(input, camera, facing_, movement, inputMagnitude)) {
        return;
    }
    const float distance = kMaximumRunSpeedCentimetersPerSecond *
                           inputMagnitude *
                           (static_cast<float>(elapsedMilliseconds) / 1000.0F);
    assets::Vector3 desired = position_;
    desired.x += movement.x * distance;
    desired.y += movement.y * distance;
    if (collision_ != nullptr) {
        collision_->resolveAirMotion(
            position_, desired, position_, LevelPhysicsFlags::JumpWall,
            LevelCollisionDepenetration::TowardAuthoredNormal);
    } else {
        position_ = desired;
    }
    renderPosition_.x = position_.x;
    renderPosition_.y = position_.y;
    facing_ = movement;
}

void GameplayPlayer::enterSwingHang(const CameraPose& camera) noexcept {
    if (selectedWebGrabPoint_ == nullptr || swingHangState_ == nullptr) {
        enterLocomotionState(LocomotionState::SustainedFall);
        return;
    }
    const assets::Vector3 incomingVelocity{
        facing_.x * kMaximumRunSpeedCentimetersPerSecond,
        facing_.y * kMaximumRunSpeedCentimetersPerSecond, 0.0F,
    };
    if (!webSwingRuntime_.start(*selectedWebGrabPoint_, position_,
                                incomingVelocity)) {
        selectedWebGrabPoint_ = nullptr;
        enterLocomotionState(LocomotionState::SustainedFall);
        return;
    }
    webLineOrientation_ = {
        camera.position.x - camera.target.x,
        camera.position.y - camera.target.y,
        camera.position.z - camera.target.z,
    };
    const float orientationLength = length3D(webLineOrientation_);
    if (orientationLength > std::numeric_limits<float>::epsilon()) {
        webLineOrientation_.x /= orientationLength;
        webLineOrientation_.y /= orientationLength;
        webLineOrientation_.z /= orientationLength;
    }
    position_ = webSwingRuntime_.position();
    renderPosition_ = position_;
    activeLocomotionState_ = swingHangState_;
    locomotionState_ = LocomotionState::SwingHang;
    const assets::ColladaAnimationClip* clip =
        clipById(animationBank_, swingUsesLeftHand_ ? 168 : 169);
    if (clip != nullptr) {
        setAnimation(clip->name);
    }
    queueEnteredState(swingHangState_->name);
    updateWorldTransform(facing_);
    if (webReleaseRequested_) {
        enterSwingRelease();
    }
}

void GameplayPlayer::enterSwingRelease() noexcept {
    if (locomotionState_ != LocomotionState::SwingHang ||
        swingIdleState_ == nullptr) {
        return;
    }
    const WebSwingRelease released = webSwingRuntime_.release();
    swingReleaseVelocity_ = released.velocityCentimetersPerSecond;
    swingReleaseSteeringVelocity_ = {};
    swingReleaseDecayVelocity_ = released.velocityCentimetersPerSecond;
    // SetNextStateId motion 28 stores the undoubled launch Z at Player+0x43c.
    // UpdateMCSpeed preserves physics Z separately, so only the horizontal
    // decay components affect the portable trajectory.
    swingReleaseDecayVelocity_.z *= 0.5F;
    swingReleaseHasTarget_ = released.hasTargetWaypoint;
    swingReleaseTarget_ = released.targetWaypointPosition;
    activeLocomotionState_ = swingIdleState_;
    locomotionState_ = LocomotionState::SwingRelease;
    const assets::ColladaAnimationClip* clip =
        clipById(animationBank_, swingUsesLeftHand_ ? 170 : 173);
    if (clip != nullptr) {
        setAnimation(clip->name);
    }
    locomotionRootTranslation_ = {};
    if (swingReleaseHasTarget_) {
        // Player::UpdateMove (0x00350dd2-0x00350f94) divides the remaining
        // point-to-WayPoint displacement by the release animation length and
        // installs that result as the motion-28 physics velocity. The original
        // also subtracts animation root displacement; this runtime renders the
        // clip in place, so the entire world-space delta belongs here.
        assets::Vector3 animationDisplacement;
        if (clip != nullptr && animationDisplacement_ != nullptr &&
            animationDisplacement_->frameCount() != 0) {
            const assets::Vector3 start =
                animationDisplacement_->physicalAt(clip->startMilliseconds);
            const assets::Vector3 end = animationDisplacement_->physicalAt(
                clip->startMilliseconds + clip->durationMilliseconds());
            animationDisplacement = {end.x - start.x, end.y - start.y,
                                     end.z - start.z};
        }
        const assets::Vector3 worldAnimationDisplacement{
            facing_.x * animationDisplacement.x -
                facing_.y * animationDisplacement.y,
            facing_.y * animationDisplacement.x +
                facing_.x * animationDisplacement.y,
            animationDisplacement.z,
        };
        const assets::Vector3 toTarget{
            swingReleaseTarget_.x - position_.x -
                worldAnimationDisplacement.x,
            swingReleaseTarget_.y - position_.y -
                worldAnimationDisplacement.y,
            swingReleaseTarget_.z - position_.z -
                worldAnimationDisplacement.z,
        };
        const float distance = length3D(toTarget);
        const std::uint32_t durationMilliseconds =
            clip == nullptr ? 1U
                            : std::max(clip->durationMilliseconds(), 1U);
        if (distance > std::numeric_limits<float>::epsilon()) {
            const float requiredSpeed =
                distance * 1000.0F /
                static_cast<float>(durationMilliseconds);
            const float travelSpeed = std::min(
                requiredSpeed,
                kMaximumForcedWebExitSpeedCentimetersPerSecond);
            const float velocityScale = travelSpeed / distance;
            swingReleaseVelocity_ = {
                toTarget.x * velocityScale,
                toTarget.y * velocityScale,
                toTarget.z * velocityScale,
            };
        }
    }
    queueEnteredState(swingIdleState_->name);
    webReleaseRequested_ = false;
}

void GameplayPlayer::updateWebTraversal(
    const PlayerMotionInput& input, const CameraPose& camera,
    std::uint32_t elapsedMilliseconds) noexcept {
    if (locomotionState_ == LocomotionState::WebThrow) {
        const assets::ColladaAnimationClip* clip =
            animationBank_ == nullptr
                ? nullptr
                : animationBank_->findClip(activeAnimation_);
        if (clip == nullptr) {
            selectedWebGrabPoint_ = nullptr;
            enterLocomotionState(LocomotionState::SustainedFall);
            return;
        }
        const std::uint32_t duration = clip->durationMilliseconds();
        const std::uint32_t current = animationTimeMilliseconds();
        const std::uint32_t step = std::min(
            elapsedMilliseconds, duration > current ? duration - current : 0U);
        animationTimeMilliseconds_ += step;
        if (animationTimeMilliseconds_ < duration) {
            return;
        }
        const std::uint32_t carry = elapsedMilliseconds - step;
        enterSwingHang(camera);
        if (locomotionState_ == LocomotionState::SwingHang && carry > 0) {
            webSwingRuntime_.update(carry);
            position_ = webSwingRuntime_.position();
            renderPosition_ = position_;
            updateWorldTransform(facing_);
        }
        return;
    }

    if (locomotionState_ == LocomotionState::SwingHang) {
        if (webReleaseRequested_) {
            enterSwingRelease();
            if (locomotionState_ != LocomotionState::SwingHang) {
                updateWebTraversal(input, camera, elapsedMilliseconds);
            }
            return;
        }
        webSwingRuntime_.update(elapsedMilliseconds);
        position_ = webSwingRuntime_.position();
        renderPosition_ = position_;
        const assets::Vector3 velocity =
            webSwingRuntime_.velocityCentimetersPerSecond();
        const float horizontalLength =
            std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
        if (horizontalLength > std::numeric_limits<float>::epsilon()) {
            facing_ = {velocity.x / horizontalLength,
                       velocity.y / horizontalLength, 0.0F};
        }
        const assets::ColladaAnimationClip* clip =
            animationBank_ == nullptr
                ? nullptr
                : animationBank_->findClip(activeAnimation_);
        if (clip != nullptr && clip->durationMilliseconds() > 0) {
            animationTimeMilliseconds_ =
                (animationTimeMilliseconds_ + elapsedMilliseconds) %
                clip->durationMilliseconds();
        }
        updateWorldTransform(facing_);
        // UpdateMove releases motion 27 once the signed rope angle exceeds
        // CWebGrabPoint::AngleV. Manual release remains available earlier.
        if (webSwingRuntime_.exitAngleReached()) {
            enterSwingRelease();
        }
        return;
    }

    if (locomotionState_ != LocomotionState::SwingRelease) {
        return;
    }
    const float elapsedSeconds =
        static_cast<float>(elapsedMilliseconds) / 1000.0F;
    const assets::ColladaAnimationClip* releaseClip =
        animationBank_ == nullptr
            ? nullptr
            : animationBank_->findClip(activeAnimation_);
    const std::uint64_t nextAnimationTime64 =
        releaseClip == nullptr
            ? static_cast<std::uint64_t>(animationTimeMilliseconds_) +
                  elapsedMilliseconds
            : std::min<std::uint64_t>(
                  static_cast<std::uint64_t>(animationTimeMilliseconds_) +
                      elapsedMilliseconds,
                  releaseClip->durationMilliseconds());
    const std::uint32_t nextAnimationTime =
        static_cast<std::uint32_t>(nextAnimationTime64);
    assets::Vector3 rootDisplacement;
    assets::Vector3 renderOffset;
    if (releaseClip != nullptr && animationDisplacement_ != nullptr &&
        animationDisplacement_->frameCount() != 0) {
        const assets::Vector3 rootStart = animationDisplacement_->physicalAt(
            releaseClip->startMilliseconds);
        const assets::Vector3 rootCurrent =
            animationDisplacement_->physicalAt(
                releaseClip->startMilliseconds + nextAnimationTime);
        rootDisplacement = {rootCurrent.x - rootStart.x,
                            rootCurrent.y - rootStart.y,
                            rootCurrent.z - rootStart.z};
        renderOffset = animationDisplacement_->renderOffsetAt(
            releaseClip->startMilliseconds + nextAnimationTime);
    }
    const assets::Vector3 rootDelta{
        rootDisplacement.x - locomotionRootTranslation_.x,
        rootDisplacement.y - locomotionRootTranslation_.y,
        rootDisplacement.z - locomotionRootTranslation_.z,
    };
    const assets::Vector3 worldRootDelta{
        facing_.x * rootDelta.x - facing_.y * rootDelta.y,
        facing_.y * rootDelta.x + facing_.x * rootDelta.y,
        rootDelta.z,
    };
    if (swingReleaseHasTarget_) {
        // Motion type 28 consumes CWebGrabPoint's linked WayPoint as a forced
        // release destination. This is the authored bridge from point 443 to
        // WayPoint 445, where CSlider's proximity test catches the player.
        const assets::Vector3 toTarget{
            swingReleaseTarget_.x - position_.x,
            swingReleaseTarget_.y - position_.y,
            swingReleaseTarget_.z - position_.z,
        };
        const float targetDistance = length3D(toTarget);
        const float travelSpeed = length3D(swingReleaseVelocity_);
        if (targetDistance > std::numeric_limits<float>::epsilon()) {
            const float inverseDistance = 1.0F / targetDistance;
            swingReleaseVelocity_ = {
                toTarget.x * inverseDistance * travelSpeed,
                toTarget.y * inverseDistance * travelSpeed,
                toTarget.z * inverseDistance * travelSpeed,
            };
        }
    } else if (selectedWebGrabPoint_ != nullptr &&
        !selectedWebGrabPoint_->cannotControl &&
        !selectedWebGrabPoint_->hasTargetWaypoint) {
        // UpdateMCSpeed motion 28 (0x00347e66-0x00347fd0) removes the prior
        // stick contribution, installs the current MAX_SWING contribution,
        // then accumulates the exponentially decaying launch vector.
        swingReleaseVelocity_.x -= swingReleaseSteeringVelocity_.x;
        swingReleaseVelocity_.y -= swingReleaseSteeringVelocity_.y;
        swingReleaseSteeringVelocity_ = {};
        float inputMagnitude{};
        assets::Vector3 movement;
        if (calculateMovement(input, camera, facing_, movement,
                              inputMagnitude)) {
            swingReleaseSteeringVelocity_ = {
                movement.x * inputMagnitude *
                    kMaximumSwingHorizontalSpeedCentimetersPerSecond,
                movement.y * inputMagnitude *
                    kMaximumSwingHorizontalSpeedCentimetersPerSecond,
                0.0F,
            };
        }
        swingReleaseVelocity_.x += swingReleaseSteeringVelocity_.x;
        swingReleaseVelocity_.y += swingReleaseSteeringVelocity_.y;
        const float releaseDecay = std::pow(
            kWebSwingIdleAccelerationDecrease, elapsedSeconds);
        swingReleaseDecayVelocity_.x *= releaseDecay;
        swingReleaseDecayVelocity_.y *= releaseDecay;
        swingReleaseVelocity_.x += swingReleaseDecayVelocity_.x;
        swingReleaseVelocity_.y += swingReleaseDecayVelocity_.y;
    }
    if (!swingReleaseHasTarget_) {
        swingReleaseVelocity_.z +=
            kDefaultGravityCentimetersPerSecondSquared * elapsedSeconds;
    }
    const float previousHeight = position_.z;
    assets::Vector3 desired{
        position_.x + swingReleaseVelocity_.x * elapsedSeconds +
            worldRootDelta.x,
        position_.y + swingReleaseVelocity_.y * elapsedSeconds +
            worldRootDelta.y,
        position_.z + swingReleaseVelocity_.z * elapsedSeconds +
            worldRootDelta.z,
    };
    const float intendedTravelDistance = length3D(
        {desired.x - position_.x, desired.y - position_.y,
         desired.z - position_.z});
    if (collision_ != nullptr) {
        collision_->resolveAirMotion(
            position_, desired, desired, 0U,
            LevelCollisionDepenetration::TowardAuthoredNormal);
    }
    if (tryAttachSwingReleaseWall(position_, desired,
                                  swingReleaseVelocity_)) {
        return;
    }
    if (swingReleaseHasTarget_ &&
        length3D({swingReleaseTarget_.x - position_.x,
                  swingReleaseTarget_.y - position_.y,
                  swingReleaseTarget_.z - position_.z}) <=
            intendedTravelDistance) {
        desired = swingReleaseTarget_;
        swingReleaseHasTarget_ = false;
    }
    position_ = desired;
    locomotionRootTranslation_ = rootDisplacement;
    const assets::Vector3 worldRenderOffset{
        facing_.x * renderOffset.x - facing_.y * renderOffset.y,
        facing_.y * renderOffset.x + facing_.x * renderOffset.y,
        renderOffset.z,
    };
    renderPosition_ = {position_.x + worldRenderOffset.x,
                       position_.y + worldRenderOffset.y,
                       position_.z + worldRenderOffset.z};
    if (tryCatchSlide()) {
        return;
    }
    float landingHeight{};
    const bool hasLanding = findLandingHeight(previousHeight, landingHeight);
    if (hasLanding && swingReleaseVelocity_.z <= 0.0F &&
        previousHeight >= landingHeight - 1.0F &&
        desired.z <= landingHeight) {
        position_ = {desired.x, desired.y, landingHeight};
        renderPosition_ = position_;
        jumpAnchorHeight_ = landingHeight;
        selectedWebGrabPoint_ = nullptr;
        enterLocomotionState(LocomotionState::JumpLand);
        return;
    }
    const float horizontalLength = std::sqrt(
        swingReleaseVelocity_.x * swingReleaseVelocity_.x +
        swingReleaseVelocity_.y * swingReleaseVelocity_.y);
    if (horizontalLength > std::numeric_limits<float>::epsilon()) {
        facing_ = {swingReleaseVelocity_.x / horizontalLength,
                   swingReleaseVelocity_.y / horizontalLength, 0.0F};
    }
    animationTimeMilliseconds_ = nextAnimationTime;
    if (releaseClip != nullptr && !swingReleaseHasTarget_ &&
        animationTimeMilliseconds_ >= releaseClip->durationMilliseconds() &&
        swingReleaseVelocity_.z <= 0.0F) {
        selectedWebGrabPoint_ = nullptr;
        enterLocomotionState(LocomotionState::SustainedFall);
        // Motion 19 -> 15 changes the animation/state predicate, not the
        // physics body. Retain the motion-28 launch velocity so the next
        // frame follows the native ballistic path into nearby sliders.
        sustainedFallCarriesSwingVelocity_ = true;
        return;
    }
    updateWorldTransform(facing_);
}

bool GameplayPlayer::tryCatchSlide() noexcept {
    if (sliderLandState_ == nullptr || sliderMoveState_ == nullptr ||
        slideRuntime_.active()) {
        return false;
    }
    assets::Vector3 visualPosition = position_;
    visualPosition.z = animatedFootHeight();
    // Player::IsCatchedBySliderStates (0x0033ffb4) admits native states
    // 0/4/14/15/16/19/21. IsDownFalling (0x0033ffe8) is the same set without
    // grounded 0/4. Only states 19 and 21 bypass CSlider's terminal-200 cm
    // rejection.
    const bool downFalling = locomotionState_ != LocomotionState::Grounded;
    const bool allowTerminalCatch =
        locomotionState_ == LocomotionState::SwingRelease ||
        locomotionState_ == LocomotionState::SliderJumpFall;
    const SlideCatch caught = slideRuntime_.findCatch(
        visualPosition, downFalling, allowTerminalCatch);
    if (caught.slide == nullptr) {
        return false;
    }
    float incomingSpeed = kMaximumRunSpeedCentimetersPerSecond;
    if (locomotionState_ == LocomotionState::SwingRelease) {
        incomingSpeed = length3D(swingReleaseVelocity_);
    }
    if (!slideRuntime_.start(caught, incomingSpeed)) {
        return false;
    }
    position_ = slideRuntime_.position();
    renderPosition_ = position_;
    jumpAnchorHeight_ = position_.z;
    const assets::Vector3 direction = slideRuntime_.direction();
    const float horizontalLength = length2D(direction.x, direction.y);
    if (horizontalLength > std::numeric_limits<float>::epsilon()) {
        facing_ = {direction.x / horizontalLength,
                   direction.y / horizontalLength, 0.0F};
    }
    swingReleaseHasTarget_ = false;
    enterLocomotionState(LocomotionState::SliderLand);
    return true;
}

SlideCatch GameplayPlayer::slideCatchCandidate() const noexcept {
    assets::Vector3 visualPosition = position_;
    visualPosition.z = animatedFootHeight();
    const bool downFalling = locomotionState_ != LocomotionState::Grounded;
    const bool allowTerminalCatch =
        locomotionState_ == LocomotionState::SwingRelease ||
        locomotionState_ == LocomotionState::SliderJumpFall;
    return slideRuntime_.findCatch(visualPosition, downFalling,
                                   allowTerminalCatch);
}

void GameplayPlayer::updateSlideTraversal(
    std::uint32_t elapsedMilliseconds) noexcept {
    if (locomotionState_ == LocomotionState::SliderLand) {
        const assets::ColladaAnimationClip* clip =
            stateClip(animationBank_, sliderLandState_);
        if (clip == nullptr) {
            (void)slideRuntime_.finish();
            enterLocomotionState(LocomotionState::SustainedFall);
            return;
        }
        const std::uint32_t duration = clip->durationMilliseconds();
        const std::uint32_t current = animationTimeMilliseconds();
        const std::uint32_t step = std::min(
            elapsedMilliseconds, duration > current ? duration - current : 0U);
        animationTimeMilliseconds_ += step;
        if (animationTimeMilliseconds_ < duration) {
            updateWorldTransform(facing_);
            return;
        }
        const std::uint32_t carry = elapsedMilliseconds - step;
        enterLocomotionState(LocomotionState::SliderMove);
        if (carry == 0) {
            return;
        }
        elapsedMilliseconds = carry;
    }

    if (locomotionState_ != LocomotionState::SliderMove) {
        return;
    }
    slideMoveElapsedMilliseconds_ = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(
            static_cast<std::uint64_t>(slideMoveElapsedMilliseconds_) +
                elapsedMilliseconds,
            std::numeric_limits<std::uint32_t>::max()));
    slideRuntime_.update(elapsedMilliseconds);
    position_ = slideRuntime_.position();
    renderPosition_ = position_;
    const assets::Vector3 direction = slideRuntime_.direction();
    const float horizontalLength = length2D(direction.x, direction.y);
    if (horizontalLength > std::numeric_limits<float>::epsilon()) {
        facing_ = {direction.x / horizontalLength,
                   direction.y / horizontalLength, 0.0F};
    }
    const assets::ColladaAnimationClip* clip =
        stateClip(animationBank_, sliderMoveState_);
    if (clip != nullptr && clip->durationMilliseconds() > 0) {
        animationTimeMilliseconds_ =
            (animationTimeMilliseconds_ + elapsedMilliseconds) %
            clip->durationMilliseconds();
    }
    updateWorldTransform(facing_);
    if (slideRuntime_.active()) {
        return;
    }
    const SlideExit exit = slideRuntime_.finish();
    swingReleaseVelocity_ = exit.velocityCentimetersPerSecond;
    if (!exit.useGravity) {
        swingReleaseVelocity_.z = 0.0F;
    }
    if (exit.electricShock) {
        (void)applyDamage(100.0F);
    }
    if (!exit.useGravity && collision_ != nullptr) {
        assets::Vector3 supportedPosition;
        if (collision_->resolveGroundMotion(
                position_, position_, supportedPosition, 75.0F, 150.0F,
                LevelPhysicsFlags::JumpWall,
                LevelCollisionDepenetration::TowardAuthoredNormal)) {
            position_ = supportedPosition;
            renderPosition_ = position_;
        }
    }
    jumpAnchorHeight_ = position_.z;
    enterLocomotionState(LocomotionState::SustainedFall);
    // CSlider::Update (0x0031d920) copies the terminal segment's horizontal
    // body velocity into Player+0x478/0x47c before changing to fall state 13.
    // Preserve that physical-body handoff: several shipped slides end just
    // outside the receiving roof and rely on this momentum to cross the lip.
    sustainedFallCarriesSwingVelocity_ = true;
}

bool GameplayPlayer::tryAttachWall(
    const assets::Vector3& movement) noexcept {
    if (collision_ == nullptr || wallAttachState_ == nullptr) {
        return false;
    }
    const assets::Vector3 start{
        position_.x, position_.y,
        position_.z + kPlayerCollisionHalfHeightCentimeters};
    const assets::Vector3 end{
        start.x + movement.x * 120.0F,
        start.y + movement.y * 120.0F, start.z};
    LevelWallContact contact;
    if (!collision_->climbableWallContact(start, end, contact) ||
        movement.x * contact.normal.x + movement.y * contact.normal.y >
            -0.2F) {
        return false;
    }
    wallNormal_ = contact.normal;
    wallNormal_.z = 0.0F;
    const float normalLength = length2D(wallNormal_.x, wallNormal_.y);
    if (normalLength <= std::numeric_limits<float>::epsilon()) {
        return false;
    }
    wallNormal_.x /= normalLength;
    wallNormal_.y /= normalLength;
    // run_to_wall_climb carries 37 cm toward the wall. Begin one root-motion
    // span outside the native 50 cm player cylinder so the final idle anchor
    // sits exactly at that radius.
    position_.x = contact.position.x +
                  wallNormal_.x *
                      (kPlayerCollisionRadiusCentimeters + 37.0F);
    position_.y = contact.position.y +
                  wallNormal_.y *
                      (kPlayerCollisionRadiusCentimeters + 37.0F);
    wallStateStartPosition_ = position_;
    facing_ = {-wallNormal_.x, -wallNormal_.y, 0.0F};
    renderPosition_ = position_;
    enterLocomotionState(LocomotionState::WallAttach);
    const assets::ColladaAnimationClip* attachClip =
        clipById(animationBank_, 128);
    if (attachClip == nullptr) {
        enterLocomotionState(LocomotionState::Grounded);
        return false;
    }
    setAnimation(attachClip->name);
    return true;
}

bool GameplayPlayer::tryAttachSwingReleaseWall(
    const assets::Vector3& start,
    const assets::Vector3& desired,
    const assets::Vector3& velocity) noexcept {
    if (collision_ == nullptr || wallIdleState_ == nullptr) {
        return false;
    }
    const float horizontalLength = length2D(velocity.x, velocity.y);
    if (horizontalLength <= std::numeric_limits<float>::epsilon()) {
        return false;
    }
    const assets::Vector3 direction{
        velocity.x / horizontalLength,
        velocity.y / horizontalLength,
        0.0F,
    };
    const assets::Vector3 probeStart{
        start.x, start.y,
        start.z + kPlayerCollisionHalfHeightCentimeters};
    // Bullet reports the climbable-wall manifold when the player's 50 cm
    // cylinder reaches it.  The portable triangle query is a point segment,
    // so extend its end by that radius to recover the same contact.
    const assets::Vector3 probeEnd{
        desired.x + direction.x *
            (kPlayerCollisionRadiusCentimeters + 5.0F),
        desired.y + direction.y *
            (kPlayerCollisionRadiusCentimeters + 5.0F),
        desired.z + kPlayerCollisionHalfHeightCentimeters};
    LevelWallContact contact;
    if (!collision_->climbableWallContact(probeStart, probeEnd, contact) ||
        direction.x * contact.normal.x +
                direction.y * contact.normal.y >
            -0.2F) {
        return false;
    }
    wallNormal_ = contact.normal;
    wallNormal_.z = 0.0F;
    const float normalLength = length2D(wallNormal_.x, wallNormal_.y);
    if (normalLength <= std::numeric_limits<float>::epsilon()) {
        return false;
    }
    wallNormal_.x /= normalLength;
    wallNormal_.y /= normalLength;
    position_ = {
        contact.position.x +
            wallNormal_.x * kPlayerCollisionRadiusCentimeters,
        contact.position.y +
            wallNormal_.y * kPlayerCollisionRadiusCentimeters,
        contact.position.z - kPlayerCollisionHalfHeightCentimeters,
    };
    wallStateStartPosition_ = position_;
    facing_ = {-wallNormal_.x, -wallNormal_.y, 0.0F};
    renderPosition_ = position_;
    selectedWebGrabPoint_ = nullptr;
    // Player::UpdateMove (0x0035081c, state 19 at 0x00350a8c) calls
    // CheckClimbableWall(this, 4) and selects state 1 immediately.  It does
    // not use the grounded run-to-wall attach state 6.
    enterLocomotionState(LocomotionState::WallIdle);
    return true;
}

assets::Vector3 GameplayPlayer::wallRootTranslation(
    const PlayerStateDefinition* state,
    std::uint32_t localMilliseconds) const noexcept {
    const assets::ColladaAnimationClip* clip = stateClip(animationBank_, state);
    if (clip == nullptr && animationBank_ != nullptr) {
        clip = animationBank_->findClip(activeAnimation_);
    }
    if (clip == nullptr || animationBank_ == nullptr) {
        return {};
    }
    const std::uint32_t timestamp =
        clip->startMilliseconds +
        std::min(localMilliseconds, clip->durationMilliseconds());
    assets::Vector3 translation;
    for (const assets::ColladaAnimationTrack& track :
         animationBank_->tracks()) {
        if (track.targetNode != "Dummy_center-node") {
            continue;
        }
        const assets::ColladaAnimationSample sample = track.sample(timestamp);
        switch (track.property) {
        case assets::ColladaAnimationProperty::Translation:
            translation = {sample.value[0], sample.value[1], sample.value[2]};
            break;
        case assets::ColladaAnimationProperty::TranslationX:
            translation.x = sample.value[0];
            break;
        case assets::ColladaAnimationProperty::TranslationY:
            translation.y = sample.value[0];
            break;
        case assets::ColladaAnimationProperty::TranslationZ:
            translation.z = sample.value[0];
            break;
        default: break;
        }
    }
    return translation;
}

assets::Vector3 GameplayPlayer::wallRootWorldDelta(
    const assets::Vector3& localDelta) const noexcept {
    // UpdateRotation exposes local X as (-facing.y, facing.x), while local Y
    // points away from the wall. Preserve that same basis for authored root
    // translation in attach, exit, and directional wall-jump clips.
    const assets::Vector3 localX{-facing_.y, facing_.x, 0.0F};
    return {localX.x * localDelta.x + wallNormal_.x * localDelta.y,
            localX.y * localDelta.x + wallNormal_.y * localDelta.y,
            localDelta.z};
}

bool GameplayPlayer::reacquireWallAt(
    const assets::Vector3& candidate) noexcept {
    if (collision_ == nullptr) {
        return false;
    }
    LevelWallContact contact;
    bool foundContact = false;
    // CheckClimbableWall (0x0034863c) consumes Bullet's contact manifold for
    // the complete 50 x 185 cm player capsule. A single ray through its
    // centre misses the next authored wall segment when wall_jump_up crosses
    // a jump_wall slab or a directional jump ends with the capsule overlapping
    // the side edge of the next panel. Probe the capsule's height and radius,
    // preserving the same full-body contact opportunity.
    constexpr std::array<float, 3> kContactHeights{
        kPlayerCollisionHalfHeightCentimeters,
        kPlayerCollisionHalfHeightCentimeters * 2.0F - 1.0F,
        1.0F,
    };
    constexpr std::array<float, 3> kContactSideOffsets{
        0.0F,
        kPlayerCollisionRadiusCentimeters - 1.0F,
        1.0F - kPlayerCollisionRadiusCentimeters,
    };
    const assets::Vector3 wallTangent{-wallNormal_.y, wallNormal_.x, 0.0F};
    for (const float height : kContactHeights) {
        for (const float sideOffset : kContactSideOffsets) {
            const assets::Vector3 start{
                candidate.x + wallTangent.x * sideOffset,
                candidate.y + wallTangent.y * sideOffset,
                candidate.z + height};
            const assets::Vector3 end{
                start.x - wallNormal_.x * 125.0F,
                start.y - wallNormal_.y * 125.0F, start.z};
            if (collision_->climbableWallContact(start, end, contact)) {
                foundContact = true;
                break;
            }
        }
        if (foundContact) {
            break;
        }
    }
    if (!foundContact) {
        return false;
    }
    wallNormal_ = contact.normal;
    wallNormal_.z = 0.0F;
    const float normalLength = length2D(wallNormal_.x, wallNormal_.y);
    if (normalLength <= std::numeric_limits<float>::epsilon()) {
        return false;
    }
    wallNormal_.x /= normalLength;
    wallNormal_.y /= normalLength;
    const float normalCorrection =
        (contact.position.x - candidate.x) * wallNormal_.x +
        (contact.position.y - candidate.y) * wallNormal_.y +
        kPlayerCollisionRadiusCentimeters;
    position_ = {candidate.x + wallNormal_.x * normalCorrection,
                 candidate.y + wallNormal_.y * normalCorrection,
                 candidate.z};
    renderPosition_ = position_;
    facing_ = {-wallNormal_.x, -wallNormal_.y, 0.0F};
    return true;
}

void GameplayPlayer::updateWallTraversal(
    const PlayerMotionInput& input,
    std::uint32_t elapsedMilliseconds) noexcept {
    lastWallInput_ = input;
    const auto finishRootMotionState = [this, elapsedMilliseconds]() {
        const assets::ColladaAnimationClip* clip =
            stateClip(animationBank_, activeLocomotionState_);
        if (clip == nullptr && animationBank_ != nullptr) {
            clip = animationBank_->findClip(activeAnimation_);
        }
        if (clip == nullptr) {
            return true;
        }
        animationTimeMilliseconds_ = std::min<std::uint64_t>(
            animationTimeMilliseconds_ + elapsedMilliseconds,
            clip->durationMilliseconds());
        const assets::Vector3 first =
            wallRootTranslation(activeLocomotionState_, 0);
        const assets::Vector3 last = wallRootTranslation(
            activeLocomotionState_, animationTimeMilliseconds());
        const assets::Vector3 worldDelta = wallRootWorldDelta(
            {last.x - first.x, last.y - first.y, last.z - first.z});
        position_ = {wallStateStartPosition_.x + worldDelta.x,
                     wallStateStartPosition_.y + worldDelta.y,
                     wallStateStartPosition_.z + worldDelta.z};
        renderPosition_ = position_;
        updateWorldTransform(facing_);
        return animationTimeMilliseconds_ >= clip->durationMilliseconds();
    };

    if (locomotionState_ == LocomotionState::WallAttach) {
        if (!finishRootMotionState()) {
            return;
        }
        if (reacquireWallAt(position_)) {
            enterLocomotionState(LocomotionState::WallIdle);
        } else {
            jumpAnchorHeight_ = position_.z;
            enterLocomotionState(LocomotionState::SustainedFall);
        }
        return;
    }
    if (locomotionState_ == LocomotionState::WallJump) {
        if (!finishRootMotionState()) {
            return;
        }
        if (reacquireWallAt(position_)) {
            enterLocomotionState(LocomotionState::WallIdle);
        } else {
            // State 7 is selected by the authored edge_wall contact while
            // climbing.  A wall jump which finds no regular 0x20 surface is
            // an ordinary airborne exit, not an implicit roof vault.
            jumpAnchorHeight_ = position_.z;
            enterLocomotionState(LocomotionState::SustainedFall);
        }
        return;
    }
    if (locomotionState_ == LocomotionState::WallExit) {
        if (!finishRootMotionState()) {
            return;
        }
        // wall_climb_to_roof_idle brings Dummy_center to the authored edge.
        // SetNextStateId's motion-15 transition then transfers the native
        // 50 cm player cylinder to the far side before ordinary ground
        // collision resumes.  Without that capsule transfer, the portable
        // center remained on wall05's near face and could never enter the
        // hostage roof even though the rendered climb had completed.
        position_.x -= wallNormal_.x *
                       (kPlayerCollisionRadiusCentimeters + 1.0F);
        position_.y -= wallNormal_.y *
                       (kPlayerCollisionRadiusCentimeters + 1.0F);
        assets::Vector3 grounded = position_;
        if (collision_ != nullptr &&
            collision_->resolveGroundMotion(position_, position_, grounded,
                                             300.0F, 500.0F, 0U,
                                             LevelCollisionDepenetration::
                                                 TowardAuthoredNormal)) {
            position_ = grounded;
            renderPosition_ = grounded;
            jumpAnchorHeight_ = grounded.z;
            enterLocomotionState(LocomotionState::Grounded);
        } else {
            jumpAnchorHeight_ = position_.z;
            enterLocomotionState(LocomotionState::SustainedFall);
        }
        return;
    }

    const float magnitude =
        std::min(length2D(input.right, input.forward), 1.0F);
    if (magnitude <= 1e-4F || elapsedMilliseconds == 0) {
        if (locomotionState_ != LocomotionState::WallIdle) {
            enterLocomotionState(LocomotionState::WallIdle);
        } else {
            const assets::ColladaAnimationClip* clip =
                stateClip(animationBank_, wallIdleState_);
            animationTimeMilliseconds_ += elapsedMilliseconds;
            if (clip != nullptr && clip->durationMilliseconds() > 0) {
                animationTimeMilliseconds_ %= clip->durationMilliseconds();
            }
        }
        updateWorldTransform(facing_);
        return;
    }

    const float inverseMagnitude =
        1.0F / length2D(input.right, input.forward);
    const float right = input.right * inverseMagnitude * magnitude;
    const float upward = input.forward * inverseMagnitude * magnitude;
    // Player::moveSideway (0x00340684) uses Player+0x3dc, the wall
    // normal, with the original vector class's left-handed crossProduct.
    // This is the same right-hand direction as GetOnWallMoveDir; using
    // the facing vector here reverses lateral controller movement.
    const assets::Vector3 localX{-wallNormal_.y, wallNormal_.x, 0.0F};
    const float travel = kWallClimbSpeedCentimetersPerSecond *
                         (static_cast<float>(elapsedMilliseconds) / 1000.0F);
    assets::Vector3 candidate{
        position_.x + localX.x * right * travel,
        position_.y + localX.y * right * travel,
        position_.z + upward * travel};
    LevelWallContact edgeContact;
    if (upward > 0.1F && collision_ != nullptr &&
        collision_->climbableEdgeContact(candidate, edgeContact)) {
        wallStateStartPosition_ = position_;
        enterLocomotionState(LocomotionState::WallExit);
        if (const auto* clip = clipById(animationBank_, 198)) {
            setAnimation(clip->name);
        }
        return;
    }
    LevelWallContact jumpWallContact;
    if (std::abs(upward) > 0.1F && collision_ != nullptr &&
        collision_->jumpWallContact(candidate, jumpWallContact)) {
        candidate.z = position_.z;
    }

    // The native state-5 path applies world-up/sideways body velocity and
    // leaves Bullet's 50 cm cylinder in contact with the current wall. On a
    // sloped climbable face, the collision solver therefore moves the body
    // along the face normal after every step. Reconcile that manifold here;
    // assigning the candidate directly lets the portable point controller
    // climb vertically away from the authored face and skip adjacent camera
    // areas. A missing contact is retained so side-edge behavior continues
    // through the dedicated jump/edge tests above.
    if (!reacquireWallAt(candidate)) {
        position_ = candidate;
        renderPosition_ = candidate;
    }

    activeLocomotionState_ = wallMoveState_;
    locomotionState_ = LocomotionState::WallMove;
    std::int32_t animationId = upward < 0.0F ? 191 : 199;
    if (right < -0.1F) {
        animationId = upward < 0.0F ? 194 : 195;
    } else if (right > 0.1F) {
        animationId = upward < 0.0F ? 196 : 197;
    }
    if (const auto* clip = clipById(animationBank_, animationId)) {
        if (activeAnimation_ != clip->name) {
            setAnimation(clip->name);
        } else if (clip->durationMilliseconds() > 0) {
            animationTimeMilliseconds_ =
                (animationTimeMilliseconds_ + elapsedMilliseconds) %
                clip->durationMilliseconds();
        }
    }
    updateWorldTransform(facing_);
}

float GameplayPlayer::currentRootHeight() const noexcept {
    const assets::ColladaAnimationClip* clip =
        stateClip(animationBank_, activeLocomotionState_);
    if (clip == nullptr || animationBank_ == nullptr) {
        return 0.0F;
    }
    const std::uint32_t localTime = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(animationTimeMilliseconds_,
                                clip->durationMilliseconds()));
    for (const assets::ColladaAnimationTrack& track :
         animationBank_->tracks()) {
        if (track.targetNode == "Dummy_center-node" &&
            track.property ==
                assets::ColladaAnimationProperty::Translation) {
            return track.sample(clip->startMilliseconds + localTime).value[2];
        }
    }
    return 0.0F;
}

bool GameplayPlayer::findLandingHeight(float referenceHeight,
                                       float& height) const noexcept {
    if (collision_ == nullptr) {
        height = jumpAnchorHeight_;
        return true;
    }
    // CheckLanding (0x00342504) accepts either a ground contact or a floor
    // within the configured fall distance. Query from the previous foot
    // position so a 100 ms frame cannot tunnel through a platform.
    assets::Vector3 reference = position_;
    reference.z = referenceHeight;
    return collision_->groundHeight(reference, 20.0F, 5000.0F, height,
                                    LevelPhysicsFlags::JumpWall);
}

void GameplayPlayer::updateJump(const PlayerMotionInput& input,
                                const CameraPose& camera,
                                std::uint32_t elapsedMilliseconds) noexcept {
    if (locomotionState_ != LocomotionState::SliderJumpUp &&
        locomotionState_ != LocomotionState::SliderJumpFall &&
        !(locomotionState_ == LocomotionState::SustainedFall &&
          sustainedFallCarriesSwingVelocity_)) {
        updateAirHorizontalMotion(input, camera, elapsedMilliseconds);
    }
    std::uint32_t remaining = elapsedMilliseconds;
    while (remaining > 0 &&
           locomotionState_ != LocomotionState::Grounded) {
        if (locomotionState_ == LocomotionState::SliderJumpUp) {
            const assets::ColladaAnimationClip* clip =
                clipById(animationBank_, slideJumpUpAnimationId_);
            if (clip == nullptr || clip->durationMilliseconds() == 0) {
                verticalVelocityCentimetersPerSecond_ =
                    kSliderFallSpeedCentimetersPerSecond;
                enterLocomotionState(LocomotionState::SliderJumpFall);
                continue;
            }
            const std::uint32_t duration = clip->durationMilliseconds();
            const std::uint32_t current = animationTimeMilliseconds();
            const std::uint32_t step = std::min(
                remaining, duration > current ? duration - current : 0U);
            const float previousHeight = position_.z;
            const float seconds = static_cast<float>(step) / 1000.0F;
            const assets::Vector3 inheritedDestination{
                position_.x +
                    (slideJumpVelocity_.x + slideJumpSideVelocity_.x) * seconds,
                position_.y +
                    (slideJumpVelocity_.y + slideJumpSideVelocity_.y) * seconds,
                position_.z + slideJumpVelocity_.z * seconds};
            if (collision_ != nullptr) {
                collision_->resolveAirMotion(
                    position_, inheritedDestination, position_,
                    LevelPhysicsFlags::JumpWall,
                    LevelCollisionDepenetration::TowardAuthoredNormal);
            } else {
                position_ = inheritedDestination;
            }
            animationTimeMilliseconds_ += step;
            remaining -= step;
            const std::uint32_t rootTime = animationTimeMilliseconds();
            assets::Vector3 physicalDisplacement;
            assets::Vector3 renderOffset;
            if (animationDisplacement_ != nullptr) {
                const std::uint32_t localTime =
                    std::min(rootTime, clip->durationMilliseconds());
                const assets::Vector3 start =
                    animationDisplacement_->physicalAt(clip->startMilliseconds);
                const assets::Vector3 sampled = animationDisplacement_->physicalAt(
                    clip->startMilliseconds + localTime);
                physicalDisplacement = {sampled.x - start.x,
                                        sampled.y - start.y,
                                        sampled.z - start.z};
                renderOffset = animationDisplacement_->renderOffsetAt(
                    clip->startMilliseconds + localTime);
            }
            applyAirRootMotion(physicalDisplacement, renderOffset);

            float landingHeight{};
            if (collision_ != nullptr &&
                findLandingHeight(previousHeight, landingHeight) &&
                previousHeight >= landingHeight - 1.0F &&
                position_.z <= landingHeight + 1.0F) {
                position_.z = landingHeight;
                jumpAnchorHeight_ = landingHeight;
                enterLocomotionState(LocomotionState::JumpLand);
                continue;
            }
            if (animationTimeMilliseconds_ >= duration) {
                renderPosition_ = position_;
                jumpAnchorHeight_ = position_.z;
                verticalVelocityCentimetersPerSecond_ =
                    kSliderFallSpeedCentimetersPerSecond;
                enterLocomotionState(LocomotionState::SliderJumpFall);
            }
            continue;
        }

        if (locomotionState_ == LocomotionState::SliderJumpFall) {
            const float previousHeight = position_.z;
            const float seconds = static_cast<float>(remaining) / 1000.0F;
            const assets::Vector3 desired{
                position_.x + slideJumpVelocity_.x * seconds,
                position_.y + slideJumpVelocity_.y * seconds,
                position_.z + verticalVelocityCentimetersPerSecond_ *
                                  seconds};
            if (collision_ != nullptr) {
                collision_->resolveAirMotion(
                    position_, desired, position_,
                    LevelPhysicsFlags::JumpWall,
                    LevelCollisionDepenetration::TowardAuthoredNormal);
            } else {
                position_ = desired;
            }
            const assets::ColladaAnimationClip* clip =
                clipById(animationBank_, slideJumpFallAnimationId_);
            if (clip != nullptr && clip->durationMilliseconds() != 0) {
                animationTimeMilliseconds_ =
                    (animationTimeMilliseconds_ + remaining) %
                    clip->durationMilliseconds();
            } else {
                animationTimeMilliseconds_ += remaining;
            }
            remaining = 0;
            float landingHeight{};
            if (collision_ != nullptr &&
                findLandingHeight(previousHeight, landingHeight) &&
                previousHeight >= landingHeight - 1.0F &&
                position_.z <= landingHeight + 1.0F) {
                position_.z = landingHeight;
                jumpAnchorHeight_ = landingHeight;
                enterLocomotionState(LocomotionState::JumpLand);
            } else {
                renderPosition_ = position_;
                updateWorldTransform(facing_);
            }
            continue;
        }

        if (locomotionState_ == LocomotionState::SustainedFall) {
            const float previousHeight = position_.z;
            float landingHeight{};
            const bool hasLanding =
                findLandingHeight(previousHeight, landingHeight);
            const float seconds = static_cast<float>(remaining) / 1000.0F;
            if (sustainedFallCarriesSwingVelocity_) {
                swingReleaseVelocity_.z +=
                    kDefaultGravityCentimetersPerSecondSquared * seconds;
                const assets::Vector3 desired{
                    position_.x + swingReleaseVelocity_.x * seconds,
                    position_.y + swingReleaseVelocity_.y * seconds,
                    position_.z + swingReleaseVelocity_.z * seconds};
                if (collision_ != nullptr) {
                    collision_->resolveAirMotion(
                        position_, desired, position_,
                        LevelPhysicsFlags::JumpWall,
                        LevelCollisionDepenetration::TowardAuthoredNormal);
                } else {
                    position_ = desired;
                }
                renderPosition_ = position_;
                if (tryCatchSlide()) {
                    return;
                }
                const float horizontalSpeed = std::hypot(
                    swingReleaseVelocity_.x, swingReleaseVelocity_.y);
                if (horizontalSpeed >
                    std::numeric_limits<float>::epsilon()) {
                    facing_ = {
                        swingReleaseVelocity_.x / horizontalSpeed,
                        swingReleaseVelocity_.y / horizontalSpeed, 0.0F};
                }
            } else {
                position_.z += verticalVelocityCentimetersPerSecond_ *
                               seconds;
            }
            animationTimeMilliseconds_ += remaining;
            remaining = 0;
            if (hasLanding && previousHeight >= landingHeight - 1.0F &&
                position_.z <= landingHeight) {
                position_.z = landingHeight;
                jumpAnchorHeight_ = landingHeight;
                enterLocomotionState(LocomotionState::JumpLand);
            } else {
                renderPosition_ = position_;
                updateWorldTransform(facing_);
            }
            continue;
        }

        if (locomotionState_ == LocomotionState::ShortWebJump) {
            const assets::ColladaAnimationClip* clip =
                stateClip(animationBank_, shortWebJumpState_);
            if (clip == nullptr || clip->durationMilliseconds() == 0) {
                enterLocomotionState(LocomotionState::SustainedFall);
                continue;
            }
            const std::uint32_t duration = clip->durationMilliseconds();
            const std::uint32_t current = animationTimeMilliseconds();
            const std::uint32_t step = std::min(
                remaining, duration > current ? duration - current : 0U);
            const float previousHeight = position_.z;
            animationTimeMilliseconds_ += step;
            remaining -= step;
            const std::uint32_t rootTime = animationTimeMilliseconds();
            applyAirRootMotion(
                animationPhysicalDisplacement(shortWebJumpState_, rootTime),
                animationRenderOffset(shortWebJumpState_, rootTime));

            float landingHeight{};
            const bool hasLanding =
                findLandingHeight(previousHeight, landingHeight);
            if (hasLanding && previousHeight >= landingHeight - 1.0F &&
                position_.z <= landingHeight + 1.0F) {
                position_.z = landingHeight;
                jumpAnchorHeight_ = landingHeight;
                enterLocomotionState(LocomotionState::JumpLand);
                continue;
            }
            if (animationTimeMilliseconds_ >= duration) {
                renderPosition_ = position_;
                jumpAnchorHeight_ = position_.z;
                enterLocomotionState(LocomotionState::SustainedFall);
            }
            continue;
        }

        const assets::ColladaAnimationClip* clip =
            stateClip(animationBank_, activeLocomotionState_);
        if (clip == nullptr) {
            enterLocomotionState(LocomotionState::Grounded);
            break;
        }
        const std::uint32_t duration = clip->durationMilliseconds();
        const std::uint32_t current = animationTimeMilliseconds();
        const std::uint32_t step =
            std::min(remaining, duration > current ? duration - current : 0U);
        const float previousHeight =
            (locomotionState_ == LocomotionState::JumpStart ||
             locomotionState_ == LocomotionState::JumpFall)
                ? jumpAnchorHeight_ + currentRootHeight()
                : position_.z;
        animationTimeMilliseconds_ += step;
        remaining -= step;

        if (locomotionState_ == LocomotionState::JumpStart ||
            locomotionState_ == LocomotionState::JumpFall) {
            const float animatedHeight =
                jumpAnchorHeight_ + currentRootHeight();
            // Player::Update (0x00353494) samples GetAnimOffseted and passes
            // its dummy stream to Unit::UpdateDisplacement (0x00324df0),
            // which drives the PhysicsEntity as well as accumulating the
            // compensating render offset. The collision capsule therefore
            // follows the authored jump arc; keeping only the skinned mesh in
            // the air leaves elevated gameplay triggers unreachable.
            position_.z = animatedHeight;
            renderPosition_.z = jumpAnchorHeight_;
            if (locomotionState_ == LocomotionState::JumpFall) {
                float landingHeight{};
                const bool hasLanding =
                    findLandingHeight(previousHeight, landingHeight);
                if (hasLanding && previousHeight >= landingHeight - 1.0F &&
                    animatedHeight <= landingHeight + 1.0F) {
                    position_.z = landingHeight;
                    jumpAnchorHeight_ = landingHeight;
                    enterLocomotionState(LocomotionState::JumpLand);
                    continue;
                }
            }
            updateWorldTransform(facing_);
        }

        if (animationTimeMilliseconds_ < duration) {
            break;
        }
        if (locomotionState_ == LocomotionState::JumpStart) {
            enterLocomotionState(LocomotionState::JumpFall);
        } else if (locomotionState_ == LocomotionState::JumpFall) {
            float landingHeight{};
            const float animatedHeight =
                jumpAnchorHeight_ + currentRootHeight();
            if (findLandingHeight(animatedHeight, landingHeight) &&
                animatedHeight <= landingHeight + 1.0F) {
                position_.z = landingHeight;
                jumpAnchorHeight_ = landingHeight;
                enterLocomotionState(LocomotionState::JumpLand);
            } else {
                renderPosition_ = position_;
                enterLocomotionState(LocomotionState::SustainedFall);
            }
        } else if (locomotionState_ == LocomotionState::JumpLand) {
            enterLocomotionState(LocomotionState::Grounded);
        }
    }
}

bool GameplayPlayer::enterAttackState(
    const PlayerStateDefinition& state) noexcept {
    const assets::ColladaAnimationClip* clip =
        stateClip(animationBank_, &state);
    if (clip == nullptr) {
        return false;
    }
    // Every displacement clip owns its own center-node track. The previous
    // clip's final displacement has already been committed to position_, so a
    // combo transition must begin from a fresh local origin instead of
    // subtracting the preceding clip's root and snapping the player back.
    attackRootTranslation_ = {};
    attackVisualRootTranslation_ = {};
    renderPosition_ = position_;
    // SetNextStateId does not replace Player+0x4a8 until the end of the
    // function.  Its entry-time motion 0x73/0x7e hit therefore copies damage
    // from the state being left, not the state being entered.
    const float previousStateDamage = activeAttackState_ == nullptr
        ? 0.0F
        : activeAttackState_->motionParameters[0];
    applyAttackRootMotion(animationPhysicalDisplacement(&state, 0),
                          animationRenderOffset(&state, 0));
    activeAttackState_ = &state;
    queuedAttackState_ = nullptr;
    queuedAttackTarget_.reset();
    queuedAttackAirborne_ = false;
    nextAttackLinkAnimationIndex_ = 0;
    attackTimelineMilliseconds_ = 0;
    nextAttackImpactFrameIndex_ = 0;
    activeAttackContactAccepted_ = false;
    specialWebImpactPhase_ = 0;
    combatWebLinesReleased_ = false;
    activeAnimation_ = clip->name;
    animationTimeMilliseconds_ = 0;
    if (inputFramePrepared_) {
        attackEnteredDuringPreparedInputFrame_ = true;
    }
    attackPhysicsVelocity_ = {};
    nextUltimatePulseMilliseconds_ = 0;
    ultimatePhaseElapsedMilliseconds_ = 0;
    ultimatePhaseRemainingMilliseconds_ = 0;
    ultimateOutEffectEmitted_ = false;
    ultimateSplashEmitted_ = false;
    queueEnteredState(state.name);

    // Player::SetNextStateId (0x003491d0) constructs the normal-suit
    // ultimate's mesh effects on state entry. These are not present in the
    // MC_STATE auxiliary lists used by ordinary punches and kicks.
    if (state.motionType == 135) {
        if (state.id == 107) {
            queueHitEffect(24);
        } else if (state.id == 110) {
            queueHitEffect(27);
            queueHitEffect(28);
            queueHitEffect(29);
            queueHitEffect(28, 0, 800, "Bip01_Spine1");
        }
    } else if (state.motionType == 136) {
        constexpr std::uint32_t wheelLifetimeMilliseconds = 1200;
        ultimatePhaseRemainingMilliseconds_ = wheelLifetimeMilliseconds;
        queueHitEffect(22, 0, wheelLifetimeMilliseconds, "Bip01", 1.5F,
                       true);
        queueHitEffect(22, 0, wheelLifetimeMilliseconds, "Bip01_Head", 0.7F,
                       true);
        queueHitEffect(22, 0, wheelLifetimeMilliseconds, "Bip01_Spine2",
                       1.0F, true);
        queueHitEffect(22, 0, wheelLifetimeMilliseconds, "Bip01_R_Foot",
                       0.7F, true);
        queueHitEffect(23);
        nextUltimatePulseMilliseconds_ = 300;
    } else if (state.motionType == 137 && state.id == 109) {
        std::erase_if(activeHitEffects_, [](const PlayerHitEffectState& effect) {
            return effect.effectId == 22 || effect.effectId == 23;
        });
        queueHitEffect(25, 0, 0, {}, 1.0F, true);
    } else if (state.motionType == 509) {
        // SetNextStateId (0x003491d0, 0x0034b1b8-0x0034b1d4) gives the
        // normal-suit blink strike the same inward mesh (effect 25) used by
        // the red ultimate's explode phase.
        queueHitEffect(25, 0, 0, {}, 1.0F, true);
    } else if (state.motionType == 510) {
        // The black-suit branch selects effect 30 at the same native site.
        queueHitEffect(30, 0, 0, {}, 1.0F, true);
    }
    if (state.motionType == 108 && activeAttackTarget_.has_value()) {
        // The diagonal aerial kick creates fx_web_whirlwind for the dash
        // travel time plus 600 ms (Player::SetNextStateId, 0x0034af66-
        // 0x0034b0b8). The same native branch aims at Bip01_Head, installs
        // its normalized direction at 1400 cm/s on both Player and the
        // PhysicsEntity, and derives the trail lifetime from that distance.
        // It snapshots Bip01 at entry and the retained callback is cleanup-
        // only.
        const assets::Vector3 target =
            activeAttackTarget_->headPosition.value_or(assets::Vector3{
                activeAttackTarget_->position.x,
                activeAttackTarget_->position.y,
                activeAttackTarget_->position.z +
                    activeAttackTarget_->collisionHeight});
        const float dx = target.x - position_.x;
        const float dy = target.y - position_.y;
        const float dz = target.z - position_.z;
        const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        constexpr float kDiagonalKickSpeedCentimetersPerSecond = 1400.0F;
        if (distance > std::numeric_limits<float>::epsilon()) {
            attackPhysicsVelocity_ = {
                dx / distance * kDiagonalKickSpeedCentimetersPerSecond,
                dy / distance * kDiagonalKickSpeedCentimetersPerSecond,
                dz / distance * kDiagonalKickSpeedCentimetersPerSecond};
        }
        const auto travelMilliseconds = static_cast<std::uint32_t>(
            distance / kDiagonalKickSpeedCentimetersPerSecond * 1000.0F);
        queueHitEffect(26, 0, travelMilliseconds + 600U);
    }
    const bool webPellet = state.motionType == 123;
    const bool immediateWebBindingHit =
        (state.motionType == 115 || state.motionType == 126) &&
        activeAttackTarget_.has_value();
    if (webPellet &&
        pendingWebPelletLaunchCount_ < pendingWebPelletLaunches_.size()) {
        assets::Vector3 origin = webLineAttachPosition();
        assets::Vector3 targetPosition{
            origin.x + facing_.x * 5000.0F,
            origin.y + facing_.y * 5000.0F,
            origin.z + facing_.z * 5000.0F};
        if (activeAttackTarget_) {
            targetPosition = activeAttackTarget_->position;
            targetPosition.z += activeAttackTarget_->collisionHeight * 0.5F;
        }
        pendingWebPelletLaunches_[pendingWebPelletLaunchCount_++] = {
            state.id,
            state.name,
            origin,
            targetPosition,
            state.motionParameters[0],
            activeAttackTarget_ ? activeAttackTarget_->objectId : -1,
        };
    }
    if (state.motionType == 112 && activeAttackTarget_.has_value() &&
        pendingEnemyNotifyEventCount_ < pendingEnemyNotifyEvents_.size()) {
        // Player::SetNextStateId motion 0x70 sends local AI hit type 0x71
        // through SendNotifyMessage(..., 3, 1) immediately on entry. Enemy::
        // ParseLocalAiMessage converts it to tied-up message 0x6a, and
        // CBehaviorTiedUp enters authored state 0x24 (TIED_LIE).
        pendingEnemyNotifyEvents_[pendingEnemyNotifyEventCount_++] = {
            state.id, state.name, activeAttackTarget_->objectId,
            0x71, 0x6a, 0x24};
    }
    if (state.motionType == 109 && activeAttackTarget_.has_value()) {
        // Player::SetNextStateId (0x003491d0, motion 0x6d) sends the
        // retained Unit notify hit 0xa0, then snapshots a pursuit velocity
        // toward that Unit minus 30 cm along the player's facing. The
        // normalized result is installed at exactly 840.00006 cm/s and
        // Player::UpdateAttacks preserves it throughout the aerial kick.
        const PlayerAttackTarget& target = *activeAttackTarget_;
        assets::Vector3 direction{
            target.position.x - position_.x - facing_.x * 30.0F,
            target.position.y - position_.y - facing_.y * 30.0F,
            target.position.z - position_.z - facing_.z * 30.0F};
        const float distance = length3D(direction);
        if (distance > std::numeric_limits<float>::epsilon()) {
            constexpr float kAirBindKickSpeedCentimetersPerSecond =
                840.00006F;
            attackPhysicsVelocity_ = {
                direction.x / distance *
                    kAirBindKickSpeedCentimetersPerSecond,
                direction.y / distance *
                    kAirBindKickSpeedCentimetersPerSecond,
                direction.z / distance *
                    kAirBindKickSpeedCentimetersPerSecond};
        }
        if (pendingEnemyNotifyEventCount_ <
            pendingEnemyNotifyEvents_.size()) {
            pendingEnemyNotifyEvents_[pendingEnemyNotifyEventCount_++] = {
                state.id, state.name, target.objectId, 0xa0, -1, -1};
        }
    }
    if (immediateWebBindingHit &&
        pendingMeleeImpactCount_ < pendingMeleeImpacts_.size()) {
        // SetNextStateId (0x003491d0) sends binding-state hit messages on
        // state entry and retains the acquired Unit at Player+0x594. Motion
        // 123 is queued separately as the native CBullet flight above.
        pendingMeleeImpacts_[pendingMeleeImpactCount_++] = {
            state.id,
            state.name,
            position_,
            facing_,
            previousStateDamage,
            state.motionParameters[1],
            state.motionParameters[2],
            state.motionParameters[3],
            activeAttackTarget_ ? activeAttackTarget_->objectId : -1,
            activeAttackTarget_.has_value(),
            true,
            bindsEnemyOnHit(state.motionType),
        };
        auto& impact = pendingMeleeImpacts_[pendingMeleeImpactCount_ - 1];
        impact.hitType = enemyHitTypeForState(state);
        impact.horizontalForce = state.timingParameters[0];
        impact.verticalForce = state.timingParameters[1];
    }
    return true;
}

void GameplayPlayer::queueAttackTransition(
    const PlayerStateDefinition& state,
    const std::optional<PlayerAttackTarget>& target,
    bool airborne) noexcept {
    // Player::UpdateKeyTrigger (0x0034d0a4) writes the requested state to
    // Player+0x4d8. UpdateAttacks does not enter it until the current linked
    // animation finishes. Retaining the request here prevents a buffered
    // combo input from truncating the current contact and follow-through.
    queuedAttackState_ = &state;
    queuedAttackTarget_ = target;
    queuedAttackAirborne_ = airborne;

    // Once SwitchToNextLinkAnim has selected the first linked/recovery clip,
    // Player::UpdateAttacks does not wait for that clip to finish.  At
    // 0x00353332-0x0035334e it sees Player+0x4d0 >= 0 and immediately consumes
    // the state buffered at Player+0x4d8.  nextAttackLinkAnimationIndex_ is
    // zero during the primary clip and becomes one when our first link is
    // selected, so it is the portable equivalent of that native condition.
    if (nextAttackLinkAnimationIndex_ != 0) {
        (void)enterQueuedAttackTransition();
    }
}

bool GameplayPlayer::enterQueuedAttackTransition() noexcept {
    if (queuedAttackState_ == nullptr) {
        return false;
    }
    const PlayerStateDefinition* state = queuedAttackState_;
    const std::optional<PlayerAttackTarget> target = queuedAttackTarget_;
    const bool airborne = queuedAttackAirborne_;
    queuedAttackState_ = nullptr;
    queuedAttackTarget_.reset();
    queuedAttackAirborne_ = false;
    activeAttackTarget_ = target;
    activeAttackAirborne_ = airborne;
    return enterAttackState(*state);
}

bool GameplayPlayer::switchToNextAttackLinkAnimation() noexcept {
    if (activeAttackState_ == nullptr || animationBank_ == nullptr) {
        return false;
    }
    const auto& links = activeAttackState_->animationIds;
    while (nextAttackLinkAnimationIndex_ < links.size()) {
        const std::int16_t animationId =
            links[nextAttackLinkAnimationIndex_++];
        const assets::ColladaAnimationClip* clip =
            clipById(animationBank_, animationId);
        if (clip == nullptr || clip->durationMilliseconds() == 0) {
            continue;
        }
        // Player::SwitchToNextLinkAnim (0x00340370) accumulates the finished
        // animation's frame count for CheckFrame/UpdateKeyTrigger, resets the
        // local animation frame, and selects the next authored link. Unit's
        // displacement accumulator is likewise local to the newly selected
        // clip; its prior endpoint has already been committed to position_.
        activeAnimation_ = clip->name;
        animationTimeMilliseconds_ = 0;
        attackRootTranslation_ = {};
        attackVisualRootTranslation_ = {};
        renderPosition_ = position_;
        applyAttackRootMotion(animationPhysicalDisplacement(clip, 0),
                              animationRenderOffset(clip, 0));
        return true;
    }
    return false;
}

const PlayerStateDefinition* GameplayPlayer::attackStateForTarget(
    const PlayerStateDefinition& requested,
    const std::optional<PlayerAttackTarget>& target) const noexcept {
    if (!target.has_value() || farPunchState_ == nullptr ||
        (requested.motionType != 100 && requested.motionType != 121)) {
        return &requested;
    }
    const float x = target->position.x - position_.x;
    const float y = target->position.y - position_.y;
    const float z = target->position.z - position_.z;
    const float distanceSquared = x * x + y * y + z * z;
    const float directReach =
        requested.motionParameters[1] + target->collisionRadius;
    // UpdateKeyTrigger (0x0034d0a4, 0x0034d620-0x0034d650) applies
    // CheckHeightToTargetClose (0x003402e4) after NeedDashToTarget.  The
    // latter redirects an ordinary type-100/121 attack only when the target
    // is beyond direct reach and inside 1000 cm; the former additionally
    // requires the two root heights to differ by no more than Spider-Man's
    // recovered 50 cm Unit radius.
    if (distanceSquared > directReach * directReach &&
        distanceSquared < 1000.0F * 1000.0F &&
        std::abs(z) <= kPlayerCollisionRadiusCentimeters) {
        return farPunchState_;
    }
    return &requested;
}

assets::Vector3 GameplayPlayer::animationPhysicalDisplacement(
    const PlayerStateDefinition* state,
    std::uint32_t localMilliseconds) const noexcept {
    const assets::ColladaAnimationClip* clip = stateClip(animationBank_, state);
    if (clip == nullptr || animationDisplacement_ == nullptr ||
        animationDisplacement_->frameCount() == 0) {
        return wallRootTranslation(state, localMilliseconds);
    }
    return animationPhysicalDisplacement(clip, localMilliseconds);
}

assets::Vector3 GameplayPlayer::animationPhysicalDisplacement(
    const assets::ColladaAnimationClip* clip,
    std::uint32_t localMilliseconds) const noexcept {
    if (clip == nullptr || animationDisplacement_ == nullptr ||
        animationDisplacement_->frameCount() == 0) {
        return {};
    }
    const std::uint32_t localTime =
        std::min(localMilliseconds, clip->durationMilliseconds());
    const assets::Vector3 start =
        animationDisplacement_->physicalAt(clip->startMilliseconds);
    const assets::Vector3 current = animationDisplacement_->physicalAt(
        clip->startMilliseconds + localTime);
    return {current.x - start.x, current.y - start.y, current.z - start.z};
}

assets::Vector3 GameplayPlayer::animationRenderOffset(
    const PlayerStateDefinition* state,
    std::uint32_t localMilliseconds) const noexcept {
    const assets::ColladaAnimationClip* clip = stateClip(animationBank_, state);
    return animationRenderOffset(clip, localMilliseconds);
}

assets::Vector3 GameplayPlayer::animationRenderOffset(
    const assets::ColladaAnimationClip* clip,
    std::uint32_t localMilliseconds) const noexcept {
    if (clip == nullptr || animationDisplacement_ == nullptr ||
        animationDisplacement_->frameCount() == 0) {
        return {};
    }
    const std::uint32_t localTime =
        std::min(localMilliseconds, clip->durationMilliseconds());
    return animationDisplacement_->renderOffsetAt(
        clip->startMilliseconds + localTime);
}

void GameplayPlayer::applyAttackRootMotion(
    const assets::Vector3& physicalDisplacement,
    const assets::Vector3& renderOffset) noexcept {
    const assets::Vector3 localDelta{
        physicalDisplacement.x - attackRootTranslation_.x,
        physicalDisplacement.y - attackRootTranslation_.y,
        physicalDisplacement.z - attackRootTranslation_.z};
    // Unit::UpdateDisplacement (0x00324df0) rotates the exported dummy
    // displacement directly by the unit's face direction. This is the
    // ordinary XY rotation below; it is distinct from the mesh's local
    // Dummy_center basis used by wall-state animation tracks.
    const assets::Vector3 worldDelta{
        facing_.x * localDelta.x - facing_.y * localDelta.y,
        facing_.y * localDelta.x + facing_.x * localDelta.y,
        localDelta.z};
    assets::Vector3 desired{position_.x + worldDelta.x,
                            position_.y + worldDelta.y,
                            position_.z + worldDelta.z};
    const float previousPhysicalHeight = position_.z;
    if (activeAttackTarget_.has_value() && !onWall()) {
        const PlayerAttackTarget& target = *activeAttackTarget_;
        const bool verticalOverlap =
            position_.z + kPlayerCollisionHalfHeightCentimeters * 2.0F >=
                target.position.z &&
            target.position.z + target.collisionHeight >= position_.z;
        const float segmentX = desired.x - position_.x;
        const float segmentY = desired.y - position_.y;
        const float relativeX = position_.x - target.position.x;
        const float relativeY = position_.y - target.position.y;
        const float minimumDistance =
            kPlayerCollisionRadiusCentimeters + target.collisionRadius;
        const float a = segmentX * segmentX + segmentY * segmentY;
        const float b = 2.0F *
                        (relativeX * segmentX + relativeY * segmentY);
        const float c = relativeX * relativeX + relativeY * relativeY -
                        minimumDistance * minimumDistance;
        // Both actors are active PhysicsEntity cylinders in the native
        // dash. Stop the animation-driven player capsule at the first unit
        // contact instead of allowing the fixed root track to cross its
        // acquired target before the authored impact frame.
        const float towardTarget =
            segmentX * -relativeX + segmentY * -relativeY;
        if (verticalOverlap && c <= 0.01F && towardTarget > 0.0F) {
            desired.x = position_.x;
            desired.y = position_.y;
        } else if (verticalOverlap && c > 0.0F &&
                   a > std::numeric_limits<float>::epsilon()) {
            const float discriminant = b * b - 4.0F * a * c;
            if (discriminant >= 0.0F) {
                const float contact =
                    (-b - std::sqrt(discriminant)) / (2.0F * a);
                if (contact >= 0.0F && contact <= 1.0F) {
                    desired.x = position_.x + segmentX * contact;
                    desired.y = position_.y + segmentY * contact;
                    desired.z = position_.z + worldDelta.z * contact;
                }
            }
        }
    }
    if (onWall()) {
        // Wall attacks retain the normal-constrained capsule. Ground
        // projection here would pull a climbing player onto a nearby floor.
        const float alongNormal = worldDelta.x * wallNormal_.x +
            worldDelta.y * wallNormal_.y + worldDelta.z * wallNormal_.z;
        desired.x -= wallNormal_.x * alongNormal;
        desired.y -= wallNormal_.y * alongNormal;
        desired.z -= wallNormal_.z * alongNormal;
        position_ = desired;
    } else if (collision_ != nullptr && activeAttackAirborne_) {
        collision_->resolveAirMotion(
            position_, desired, position_, LevelPhysicsFlags::JumpWall,
            LevelCollisionDepenetration::TowardAuthoredNormal);
        if (worldDelta.z < 0.0F) {
            float supportHeight{};
            const float sweptDistance =
                std::max(previousPhysicalHeight - position_.z, 0.0F);
            if (collision_->groundHeight(
                    position_, sweptDistance + 5.0F, 5.0F,
                    supportHeight, LevelPhysicsFlags::JumpWall) &&
                supportHeight <= previousPhysicalHeight + 1.0F &&
                position_.z <= supportHeight) {
                // The native player PhysicsEntity is a capsule and cannot
                // cross the street while an airborne attack's Dummy root
                // descends. The portable point sweep can miss a coplanar
                // floor, so retain the same support contact before the state
                // hands control back to jump/fall locomotion.
                position_.z = supportHeight;
            }
        }
    } else if (collision_ != nullptr) {
        assets::Vector3 resolved;
        if (collision_->resolveGroundMotion(position_, desired, resolved,
                                            75.0F, 150.0F,
                                            LevelPhysicsFlags::JumpWall,
                                            LevelCollisionDepenetration::
                                                TowardAuthoredNormal)) {
            position_ = resolved;
        } else {
            position_ = desired;
        }
    } else {
        position_ = desired;
    }
    attackRootTranslation_ = physicalDisplacement;
    // Unit::UpdateRenderOffset (0x00324d70) applies the same face-direction
    // rotation to the pelvis-minus-dummy stream before anchoring the scene.
    const assets::Vector3 worldRoot{
        facing_.x * renderOffset.x - facing_.y * renderOffset.y,
        facing_.y * renderOffset.x + facing_.x * renderOffset.y,
        renderOffset.z};
    // Unit::SetPosition (0x0032305c) adds the render displacement stored at
    // +0x220 to the physics position before updating the scene node.
    renderPosition_ = {position_.x + worldRoot.x,
                       position_.y + worldRoot.y,
                       position_.z + worldRoot.z};
    attackVisualRootTranslation_ = renderOffset;
    updateWorldTransform(facing_);
}

void GameplayPlayer::applyFlyKickHomingMotion(
    std::uint32_t elapsedMilliseconds) noexcept {
    if (activeAttackState_ == nullptr ||
        activeAttackState_->motionType != 114 ||
        activeAttackState_->animationIds.empty() ||
        animationBank_ == nullptr || !activeAttackTarget_.has_value()) {
        return;
    }
    const assets::ColladaAnimationClip* attackClip =
        clipById(animationBank_, activeAttackState_->animationIds.front());
    if (attackClip == nullptr || activeAnimation_ != attackClip->name) {
        return;
    }

    const PlayerAttackTarget& target = *activeAttackTarget_;
    assets::Vector3 direction{target.position.x - position_.x,
                              target.position.y - position_.y,
                              target.position.z - position_.z};
    const float distance = std::sqrt(direction.x * direction.x +
                                     direction.y * direction.y +
                                     direction.z * direction.z);
    if (distance <= std::numeric_limits<float>::epsilon()) {
        return;
    }
    direction.x /= distance;
    direction.y /= distance;
    direction.z /= distance;

    // Player::UpdateAttacks motion 0x72 (0x00351204) writes the normalized
    // live target vector times 700 * 3 to PhysicsEntity velocity throughout
    // the first linked (in_air_fly_kicking) clip. Active character cylinders
    // stop at contact in native physics, so clamp the portable sweep to the
    // same combined radius rather than overshooting the target in a 25 ms
    // deterministic step.
    constexpr float kFlyKickSpeedCentimetersPerSecond = 2100.0F;
    const float maximumTravel =
        kFlyKickSpeedCentimetersPerSecond *
        static_cast<float>(elapsedMilliseconds) / 1000.0F;
    const float contactDistance =
        kPlayerCollisionRadiusCentimeters + target.collisionRadius;
    const float travel =
        std::min(maximumTravel, std::max(distance - contactDistance, 0.0F));
    if (travel <= 0.0F) {
        return;
    }
    const assets::Vector3 desired{position_.x + direction.x * travel,
                                  position_.y + direction.y * travel,
                                  position_.z + direction.z * travel};
    if (collision_ != nullptr) {
        collision_->resolveAirMotion(
            position_, desired, position_, LevelPhysicsFlags::JumpWall,
            LevelCollisionDepenetration::TowardAuthoredNormal);
    } else {
        position_ = desired;
    }
}

void GameplayPlayer::applyAirKnockdownApproachMotion(
    std::uint32_t elapsedMilliseconds,
    std::uint32_t animationDurationMilliseconds) noexcept {
    if (activeAttackState_ == nullptr ||
        activeAttackState_->motionType != 107 ||
        animationBank_ == nullptr || !activeAttackTarget_.has_value() ||
        elapsedMilliseconds == 0 || animationDurationMilliseconds == 0) {
        return;
    }
    const assets::ColladaAnimationClip* primaryClip =
        clipById(animationBank_, activeAttackState_->primaryAnimationId);
    if (primaryClip == nullptr || activeAnimation_ == primaryClip->name) {
        return;
    }

    // Player::UpdateAttacks (0x00351204) special-cases state 99 after its
    // primary contact clip. Every update of the linked animation aims the
    // player PhysicsEntity at the retained target plus DEFAULT_UP times half
    // the player's authored 185 cm height plus 300 cm, then divides that
    // live delta by the complete linked-animation duration. This is an
    // easing approach, not an authored dash/root-motion approximation.
    const PlayerAttackTarget& target = *activeAttackTarget_;
    const assets::Vector3 destination{
        target.position.x,
        target.position.y,
        target.position.z + kPlayerCollisionHalfHeightCentimeters + 300.0F};
    const float stepFraction =
        static_cast<float>(elapsedMilliseconds) /
        static_cast<float>(animationDurationMilliseconds);
    const assets::Vector3 desired{
        position_.x + (destination.x - position_.x) * stepFraction,
        position_.y + (destination.y - position_.y) * stepFraction,
        position_.z + (destination.z - position_.z) * stepFraction};
    if (collision_ != nullptr) {
        collision_->resolveAirMotion(
            position_, desired, position_, LevelPhysicsFlags::JumpWall,
            LevelCollisionDepenetration::TowardAuthoredNormal);
    } else {
        position_ = desired;
    }
}

void GameplayPlayer::applyAirAttackPursuitMotion(
    std::uint32_t elapsedMilliseconds) noexcept {
    if (activeAttackState_ == nullptr ||
        (activeAttackState_->motionType != 108 &&
         activeAttackState_->motionType != 109) ||
        elapsedMilliseconds == 0) {
        return;
    }
    const float seconds =
        static_cast<float>(elapsedMilliseconds) / 1000.0F;
    assets::Vector3 desired{
        position_.x + attackPhysicsVelocity_.x * seconds,
        position_.y + attackPhysicsVelocity_.y * seconds,
        position_.z + attackPhysicsVelocity_.z * seconds};
    const assets::Vector3 start = position_;
    if (activeAttackTarget_.has_value()) {
        const PlayerAttackTarget& target = *activeAttackTarget_;
        const bool verticalOverlap =
            start.z + kPlayerCollisionHalfHeightCentimeters * 2.0F >=
                target.position.z &&
            target.position.z + target.collisionHeight >= start.z;
        const float relativeX = start.x - target.position.x;
        const float relativeY = start.y - target.position.y;
        const float minimumDistance =
            kPlayerCollisionRadiusCentimeters + target.collisionRadius;
        const float towardTarget =
            (desired.x - start.x) * -relativeX +
            (desired.y - start.y) * -relativeY;
        if (verticalOverlap &&
            relativeX * relativeX + relativeY * relativeY <=
                minimumDistance * minimumDistance + 0.01F &&
            towardTarget > 0.0F) {
            // The native player and enemy are active character cylinders.
            // Once their vertical spans overlap, the pursuit velocity cannot
            // push the player capsule through the retained victim.
            desired.x = start.x;
            desired.y = start.y;
        }
    }
    if (collision_ != nullptr) {
        collision_->resolveAirMotion(
            start, desired, position_, LevelPhysicsFlags::JumpWall,
            LevelCollisionDepenetration::TowardAuthoredNormal);
        if (desired.z < start.z) {
            float supportHeight{};
            const float fallDistance =
                std::max(start.z - position_.z, 0.0F);
            if (collision_->groundHeight(
                    position_, fallDistance + 5.0F, 5.0F, supportHeight,
                    LevelPhysicsFlags::JumpWall) &&
                supportHeight <= start.z + 1.0F &&
                position_.z <= supportHeight) {
                // Physics::processSphereTriangle prevents the native capsule
                // from crossing its support plane.  resolveAirMotion only
                // handles walls, so retain the swept floor contact here.
                position_.z = supportHeight;
                attackPhysicsVelocity_ = {};
            }
        }
    } else {
        position_ = desired;
    }
}

void GameplayPlayer::applyAirRootMotion(
    const assets::Vector3& physicalDisplacement,
    const assets::Vector3& renderOffset) noexcept {
    const assets::Vector3 localDelta{
        physicalDisplacement.x - locomotionRootTranslation_.x,
        physicalDisplacement.y - locomotionRootTranslation_.y,
        physicalDisplacement.z - locomotionRootTranslation_.z};
    const assets::Vector3 worldDelta{
        facing_.x * localDelta.x - facing_.y * localDelta.y,
        facing_.y * localDelta.x + facing_.x * localDelta.y,
        localDelta.z};
    const assets::Vector3 desired{position_.x + worldDelta.x,
                                  position_.y + worldDelta.y,
                                  position_.z + worldDelta.z};
    if (collision_ != nullptr) {
        collision_->resolveAirMotion(
            position_, desired, position_, LevelPhysicsFlags::JumpWall,
            LevelCollisionDepenetration::TowardAuthoredNormal);
    } else {
        position_ = desired;
    }
    locomotionRootTranslation_ = physicalDisplacement;

    const assets::Vector3 worldRoot{
        facing_.x * renderOffset.x - facing_.y * renderOffset.y,
        facing_.y * renderOffset.x + facing_.x * renderOffset.y,
        renderOffset.z};
    renderPosition_ = {position_.x + worldRoot.x,
                       position_.y + worldRoot.y,
                       position_.z + worldRoot.z};
    updateWorldTransform(facing_);
}

void GameplayPlayer::cancelAttack() noexcept {
    activeAttackState_ = nullptr;
    activeAttackTarget_.reset();
    activeAttackAirborne_ = false;
    queuedAttackState_ = nullptr;
    queuedAttackTarget_.reset();
    queuedAttackAirborne_ = false;
    nextAttackLinkAnimationIndex_ = 0;
    attackTimelineMilliseconds_ = 0;
    ultimateActive_ = false;
    attackRootTranslation_ = {};
    attackVisualRootTranslation_ = {};
    attackPhysicsVelocity_ = {};
    renderPosition_ = position_;
    updateWorldTransform(facing_);
    nextAttackImpactFrameIndex_ = 0;
    activeAttackContactAccepted_ = false;
    specialWebImpactPhase_ = 0;
    combatWebLinesReleased_ = false;
    pendingMeleeImpactCount_ = 0;
    pendingAttackSoundTriggerCount_ = 0;
}

void GameplayPlayer::updateAttack(
    std::uint32_t elapsedMilliseconds) noexcept {
    std::uint32_t remaining = elapsedMilliseconds;
    while (activeAttackState_ != nullptr && remaining != 0) {
        const assets::ColladaAnimationClip* clip =
            animationBank_ == nullptr
                ? nullptr
                : animationBank_->findClip(activeAnimation_);
        if (clip == nullptr || clip->durationMilliseconds() == 0) {
            cancelAttack();
            setAnimation("idle_stand");
            return;
        }
        const std::uint32_t duration = clip->durationMilliseconds();
        const std::uint32_t previousLocal = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(animationTimeMilliseconds_, duration));
        std::uint32_t step =
            std::min(remaining, duration - previousLocal);
        if (activeAttackState_->motionType == 136 &&
            ultimatePhaseRemainingMilliseconds_ != 0) {
            step = std::min(step, ultimatePhaseRemainingMilliseconds_);
        }
        const std::uint32_t currentLocal = previousLocal + step;
        const std::uint32_t previousTimeline = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(attackTimelineMilliseconds_,
                                    std::numeric_limits<std::uint32_t>::max()));
        animationTimeMilliseconds_ = currentLocal;
        attackTimelineMilliseconds_ += step;
        const std::uint32_t currentTimeline = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(attackTimelineMilliseconds_,
                                    std::numeric_limits<std::uint32_t>::max()));
        remaining -= step;
        if (activeAttackState_->motionType == 136) {
            ultimatePhaseElapsedMilliseconds_ += step;
            ultimatePhaseRemainingMilliseconds_ -= std::min(
                ultimatePhaseRemainingMilliseconds_, step);
        }
        const std::uint32_t rootTime =
            currentLocal == 0 ? 0 : std::min(currentLocal, duration - 1);
        assets::Vector3 physicalDisplacement =
            animationPhysicalDisplacement(clip, rootTime);
        const assets::Vector3 renderOffset =
            animationRenderOffset(clip, rootTime);
        // Player::GetAnimOffseted (0x003400f0) returns mask 7 for motions
        // 0x6c and 0x6d. Unit::UpdateDisplacement (0x00324df0) consumes
        // those bits by clearing physical X/Y/Z while UpdateRenderOffset
        // still preserves the authored visual pose. These aerial binding
        // transitions move only through the explicit velocity installed by
        // SetNextStateId; applying their dummy track to physics makes the
        // finishing kick climb away from its tied target.
        if (activeAttackState_->motionType == 108 ||
            activeAttackState_->motionType == 109) {
            physicalDisplacement = {};
        }
        // Player::UpdateAttacks (0x00351204) calls UpdateAttackParam and
        // CheckAttackTarget at 0x0035121c-0x0035122a before it writes any of
        // the motion-specific velocities to PhysicsEntity. Unit applies
        // those velocities later in Player::Update through
        // Unit::UpdateDisplacement. Preserve that phase boundary: contacts
        // and their bone-attached effects observe the pre-displacement pose
        // for this tick.
        queueAttackFrameEvents(previousTimeline, currentTimeline);

        const assets::Vector3 beforeHoming = position_;
        applyFlyKickHomingMotion(step);
        applyAirKnockdownApproachMotion(step, duration);
        applyAirAttackPursuitMotion(step);
        if (step != 0) {
            const float inverseSeconds = 1000.0F / static_cast<float>(step);
            const assets::Vector3 homingDelta{
                position_.x - beforeHoming.x,
                position_.y - beforeHoming.y,
                position_.z - beforeHoming.z};
            const float homingDistanceSquared =
                homingDelta.x * homingDelta.x +
                homingDelta.y * homingDelta.y +
                homingDelta.z * homingDelta.z;
            if (homingDistanceSquared >
                std::numeric_limits<float>::epsilon()) {
                attackPhysicsVelocity_ = {
                    homingDelta.x * inverseSeconds,
                    homingDelta.y * inverseSeconds,
                    homingDelta.z * inverseSeconds};
            } else {
                const assets::Vector3 localDelta{
                    physicalDisplacement.x - attackRootTranslation_.x,
                    physicalDisplacement.y - attackRootTranslation_.y,
                    physicalDisplacement.z - attackRootTranslation_.z};
                attackPhysicsVelocity_ = {
                    (facing_.x * localDelta.x -
                     facing_.y * localDelta.y) * inverseSeconds,
                    (facing_.y * localDelta.x +
                     facing_.x * localDelta.y) * inverseSeconds,
                    localDelta.z * inverseSeconds};
            }
        }
        applyAttackRootMotion(physicalDisplacement, renderOffset);
        const bool ultimateWheelFinished =
            activeAttackState_->motionType == 136 &&
            ultimatePhaseRemainingMilliseconds_ == 0;
        bool fallingAttackStopped = false;
        if (collision_ != nullptr &&
            (activeAttackState_->motionType == 108 ||
             activeAttackState_->motionType == 109 ||
             activeAttackState_->motionType == 111) &&
            attackPhysicsVelocity_.z <= 0.0F) {
            float supportHeight{};
            fallingAttackStopped = collision_->groundHeight(
                position_, 1.0F, 1.0F, supportHeight,
                LevelPhysicsFlags::JumpWall) &&
                std::abs(position_.z - supportHeight) <= 1.0F;
        }
        // UpdateAttacks checks Unit::IsFalling for motions 0x6c, 0x6d, and
        // 0x6f before waiting for animation completion. Ground contact must
        // consume a buffered transition (or the state's normal successor)
        // immediately; otherwise state 104 keeps its downward velocity and
        // carries both the player and its state-105 finisher below the road.
        if (currentLocal < duration && !ultimateWheelFinished &&
            !fallingAttackStopped) {
            return;
        }

        // UpdateKeyTrigger stores a valid buffered state at Player+0x4d8.
        // UpdateAttacks consumes it when the current animation finishes,
        // before the no-input recovery link is selected.
        if (!ultimateWheelFinished && queuedAttackState_ != nullptr) {
            if (!enterQueuedAttackTransition()) {
                cancelAttack();
                setAnimation("idle_stand");
                return;
            }
            continue;
        }

        // Player::SwitchToNextLinkAnim (0x00340370) is a generic attack-state
        // path, not an ultimate-only special case. Ordinary punches, kicks,
        // aerial finishers, and web binds all use it for authored recovery or
        // contact clips. The wheel alone repeats its sole link until its
        // independent 1200 ms phase timer expires.
        if (!ultimateWheelFinished &&
            switchToNextAttackLinkAnimation()) {
            continue;
        }
        if (!ultimateWheelFinished && activeAttackState_->motionType == 136 &&
            !activeAttackState_->animationIds.empty()) {
            nextAttackLinkAnimationIndex_ = 0;
            if (switchToNextAttackLinkAnimation()) {
                continue;
            }
        }

        const std::int16_t nextStateId = activeAttackState_->nextStateId;
        const PlayerStateDefinition* nextState = nullptr;
        if (stateDatabase_ != nullptr && nextStateId > 0 &&
            static_cast<std::size_t>(nextStateId) <
                stateDatabase_->states().size()) {
            const PlayerStateDefinition& candidate =
                stateDatabase_->states()[static_cast<std::size_t>(nextStateId)];
            if (candidate.id == static_cast<std::uint16_t>(nextStateId) &&
                candidate.stateClass == 4) {
                nextState = &candidate;
            }
        }
        if (nextState == nullptr && jumpFallState_ != nullptr &&
            sustainedFallState_ != nullptr &&
            (nextStateId == static_cast<std::int16_t>(jumpFallState_->id) ||
             nextStateId ==
                 static_cast<std::int16_t>(sustainedFallState_->id))) {
            activeAttackState_ = nullptr;
            activeAttackTarget_.reset();
            activeAttackAirborne_ = false;
            ultimateActive_ = false;
            attackRootTranslation_ = {};
            attackVisualRootTranslation_ = {};
            nextAttackImpactFrameIndex_ = 0;
            locomotionRootTranslation_ = {};
            if (nextStateId == static_cast<std::int16_t>(jumpFallState_->id)) {
                enterLocomotionState(LocomotionState::JumpFall);
                jumpAnchorHeight_ = position_.z - currentRootHeight();
                renderPosition_.z = jumpAnchorHeight_;
                updateWorldTransform(facing_);
            } else {
                jumpAnchorHeight_ = position_.z;
                enterLocomotionState(LocomotionState::SustainedFall);
            }
            continue;
        }
        if (nextState == nullptr || !enterAttackState(*nextState)) {
            activeAttackState_ = nullptr;
            activeAttackTarget_.reset();
            activeAttackAirborne_ = false;
            ultimateActive_ = false;
            attackRootTranslation_ = {};
            attackVisualRootTranslation_ = {};
            renderPosition_ = position_;
            nextAttackImpactFrameIndex_ = 0;
            if (onWall()) {
                enterLocomotionState(LocomotionState::WallIdle);
            } else {
                setAnimation("idle_stand");
            }
            updateWorldTransform(facing_);
            return;
        }
    }
}

std::int32_t GameplayPlayer::trackedAttackTargetObjectId() const noexcept {
    if (queuedAttackTarget_.has_value()) {
        return queuedAttackTarget_->objectId;
    }
    return activeAttackTarget_.has_value() ? activeAttackTarget_->objectId
                                           : -1;
}

void GameplayPlayer::refreshTrackedAttackTarget(
    const std::optional<PlayerAttackTarget>& target) noexcept {
    const auto refresh = [&target](std::optional<PlayerAttackTarget>& tracked) {
        if (!tracked.has_value()) {
            return;
        }
        if (!target.has_value() || target->objectId != tracked->objectId) {
            tracked.reset();
            return;
        }
        tracked = target;
    };
    refresh(activeAttackTarget_);
    refresh(queuedAttackTarget_);
}

void GameplayPlayer::queueAttackFrameEvents(
    std::uint32_t previousMilliseconds,
    std::uint32_t currentMilliseconds) noexcept {
    if (activeAttackState_ == nullptr) {
        return;
    }
    queueAttackSoundEvents(previousMilliseconds, currentMilliseconds);
    queueSpecialAttackEffects(previousMilliseconds, currentMilliseconds);

    const auto animatedBip01Origin = [this]() noexcept {
        // Unit::CheckAttackByPos (0x00325118, 0x003251b0-0x00325212)
        // starts with Unit::GetPosition plus half the player's height, then
        // replaces that center with the current animated `Bip01` absolute
        // position whenever that node exists. The old portable sector used
        // the physics feet, which made aerial paired attacks miss vertically
        // even while their rendered bodies overlapped.
        assets::Vector3 origin{
            position_.x, position_.y,
            position_.z + kPlayerCollisionHalfHeightCentimeters};
        const auto* clip = animationBank_ == nullptr
            ? nullptr
            : animationBank_->findClip(activeAnimation_);
        std::array<float, 16> pelvis{};
        if (mesh_ != nullptr && clip != nullptr &&
            assets::evaluateColladaSceneNodeTransform(
                *mesh_, *animationBank_,
                clip->startMilliseconds + animationTimeMilliseconds(),
                "Bip01", pelvis)) {
            const assets::Vector3 local{
                pelvis[12], pelvis[13], pelvis[14]};
            origin = {
                worldTransform_[0] * local.x + worldTransform_[4] * local.y +
                    worldTransform_[8] * local.z + worldTransform_[12],
                worldTransform_[1] * local.x + worldTransform_[5] * local.y +
                    worldTransform_[9] * local.z + worldTransform_[13],
                worldTransform_[2] * local.x + worldTransform_[6] * local.y +
                    worldTransform_[10] * local.z + worldTransform_[14]};
        }
        return origin;
    };

    const auto queueRetainedWebImpact =
        [this, &animatedBip01Origin](
            float damage, std::int16_t hitType) noexcept {
            if (!activeAttackTarget_.has_value() ||
                pendingMeleeImpactCount_ >= pendingMeleeImpacts_.size()) {
                return;
            }
            pendingMeleeImpacts_[pendingMeleeImpactCount_++] = {
                activeAttackState_->id,
                activeAttackState_->name,
                animatedBip01Origin(),
                facing_,
                damage,
                activeAttackState_->motionParameters[1],
                activeAttackState_->motionParameters[2],
                activeAttackState_->motionParameters[3],
                activeAttackTarget_->objectId,
                true,
                true,
                false,
            };
            auto& impact = pendingMeleeImpacts_[pendingMeleeImpactCount_ - 1];
            impact.hitType = hitType;
        };

    // UpdateAttackParam deliberately excludes web-binding motions 0x7c..0x82
    // from the generic MC_STATE impact list. UpdateAttacks supplies the three
    // damaging retained-target paths itself:
    //  * motion 0x7f sends a zero-damage attach hit at authored frame 7 and a
    //    type-100 damage hit when the primary drag-down clip finishes;
    //  * motion 0x81 damages when its primary throw clip finishes;
    //  * motion 0x82 damages at StateBasic+0x30 (soundTriggerFrame).
    // This is why state 62's serialized 20 is not a second pellet hit and why
    // throw damage must not occur as soon as the animation begins.
    if (activeAttackState_->motionType == 127) {
        const std::uint32_t attachThreshold = combatEventMilliseconds(7);
        if (specialWebImpactPhase_ == 0 &&
            attachThreshold >= previousMilliseconds &&
            attachThreshold <= currentMilliseconds) {
            queueRetainedWebImpact(0.0F, 127);
            specialWebImpactPhase_ = 1;
        }
        const auto* primary = stateClip(animationBank_, activeAttackState_);
        if (specialWebImpactPhase_ <= 1 && primary != nullptr &&
            currentMilliseconds >= primary->durationMilliseconds()) {
            queueRetainedWebImpact(activeAttackState_->motionParameters[0],
                                   100);
            specialWebImpactPhase_ = 2;
        }
    } else if (activeAttackState_->motionType == 129) {
        const auto* primary = stateClip(animationBank_, activeAttackState_);
        if (specialWebImpactPhase_ == 0 && primary != nullptr &&
            currentMilliseconds >= primary->durationMilliseconds()) {
            queueRetainedWebImpact(activeAttackState_->motionParameters[0],
                                   129);
            specialWebImpactPhase_ = 1;
        }
    } else if (activeAttackState_->motionType == 130 &&
               activeAttackState_->soundTriggerFrame >= 0) {
        const std::uint32_t threshold = combatEventMilliseconds(
            activeAttackState_->soundTriggerFrame);
        if (specialWebImpactPhase_ == 0 && threshold >= previousMilliseconds &&
            threshold <= currentMilliseconds) {
            queueRetainedWebImpact(activeAttackState_->motionParameters[0],
                                   130);
            specialWebImpactPhase_ = 1;
            // UpdateAttacks frees both retained CobWeb objects immediately
            // after the motion-0x82 hit dispatch.
            combatWebLinesReleased_ = true;
        }
    }

    const bool genericImpactSuppressed =
        activeAttackState_->motionType == 110 ||
        activeAttackState_->motionType == 112 ||
        activeAttackState_->motionType == 113 ||
        activeAttackState_->motionType == 115 ||
        (activeAttackState_->motionType >= 124 &&
         activeAttackState_->motionType <= 130);
    // UpdateAttackParam (0x00340fa0) returns immediately when Player+0x4f0
    // already records an accepted contact.  That early return precedes its
    // authored-frame scan.  In particular, motion 0x67/state 106 can meet
    // the retained airborne victim through the frame-3 retry before its
    // serialized frame-18 marker; the later marker must not deal the same
    // 80 damage again.  UpdateNormalEffect (0x00348f24) is called separately
    // and still advances the frame-indexed visual effect below.
    const bool repeatsAerialContact =
        activeAttackState_->motionType == 103 ||
        activeAttackState_->motionType == 108 ||
        activeAttackState_->motionType == 109 ||
        activeAttackState_->motionType == 111;
    const auto& hitFrames = activeAttackState_->auxiliaryIdLists[0];
    bool queuedGenericImpact = false;
    while (nextAttackImpactFrameIndex_ < hitFrames.size()) {
        const std::size_t hitFrameIndex = nextAttackImpactFrameIndex_;
        const std::int16_t frame = hitFrames[hitFrameIndex];
        if (frame < 0) {
            ++nextAttackImpactFrameIndex_;
            continue;
        }
        const std::uint32_t threshold = combatEventMilliseconds(frame);
        if (threshold > currentMilliseconds) {
            break;
        }
        if (!genericImpactSuppressed &&
            !(repeatsAerialContact && activeAttackContactAccepted_) &&
            threshold >= previousMilliseconds &&
            pendingMeleeImpactCount_ < pendingMeleeImpacts_.size()) {
            const bool multiHit = hitFrames.size() > 1;
            const float authoredDamage =
                activeAttackState_->motionParameters[0];
            const float contactDamage = multiHit
                ? authoredDamage / static_cast<float>(hitFrames.size())
                : authoredDamage;
            const bool bindsEnemy =
                bindsEnemyOnHit(activeAttackState_->motionType);
            const bool targetedWebBinding =
                bindsEnemy && activeAttackTarget_.has_value();
            // Motions 0x6f and 0x72 are not free sector sweeps. UpdateAttacks
            // (0x00351204, notably 0x00351970-0x00351b04 for 0x6f) calls
            // CheckAttackByPos against Player+0x594's retained Unit and sends
            // the hit to that same Unit. Without retained-target delivery the
            // diagonal kick can damage an unrelated thug who crosses the web
            // line while its intended victim receives nothing.
            const bool targetedFlyKick =
                (activeAttackState_->motionType == 111 ||
                 activeAttackState_->motionType == 114) &&
                activeAttackTarget_.has_value();
            const bool senseTarget =
                activeAttackState_->stateClass == 6 &&
                activeAttackTarget_.has_value();
            pendingMeleeImpacts_[pendingMeleeImpactCount_++] = {
                activeAttackState_->id,
                activeAttackState_->name,
                animatedBip01Origin(),
                facing_,
                contactDamage,
                activeAttackState_->motionParameters[1],
                activeAttackState_->motionParameters[2],
                activeAttackState_->motionParameters[3],
                targetedWebBinding || targetedFlyKick || senseTarget
                    ? activeAttackTarget_->objectId
                    : -1,
                targetedWebBinding || targetedFlyKick,
                isWebAttackMotion(activeAttackState_->motionType),
                bindsEnemy,
            };
            queuedGenericImpact = true;
            auto& queued = pendingMeleeImpacts_[pendingMeleeImpactCount_ - 1];
            queued.hitType = enemyHitTypeForState(*activeAttackState_);
            // CheckAttackTarget (0x0034fca0) divides a multi-hit state's
            // authored damage by its contact count. Its knockback fields are
            // copied only for a single-hit state or the final contact, so the
            // victim remains inside the preceding flurry instead of being
            // ejected by its first pulse.
            const bool finalContact =
                !multiHit || hitFrameIndex + 1 == hitFrames.size();
            queued.horizontalForce = finalContact
                ? activeAttackState_->timingParameters[0]
                : 0.0F;
            queued.verticalForce = finalContact
                ? activeAttackState_->timingParameters[1]
                : 0.0F;
            queued.radialAttack =
                activeAttackState_->motionType >= 136 &&
                activeAttackState_->motionType <= 140;
            queued.ultimateAttack = ultimateActive_;
            queued.senseAttack = activeAttackState_->stateClass == 6;
            if (activeAttackState_->motionType == 109 &&
                activeAttackTarget_.has_value()) {
                // Player::CheckAttackTarget (0x0034fca0, 0x003505ce-
                // 0x00350614) retains Player+0x594 while keeping sector
                // enumeration. The retained Unit receives the ordinary
                // motion-0x6d AIHitTargetInfo; every other Unit receives a
                // separate type-0x79 record with fixed 200/500 force.
                queued.targetedEnemyObjectId =
                    activeAttackTarget_->objectId;
                queued.airKickDownSplit = true;
            }
            if (activeAttackState_->motionType == 131) {
                auto& impact = pendingMeleeImpacts_[pendingMeleeImpactCount_ - 1];
                impact.wallAttack = true;
                impact.wallNormal = wallNormal_;
                impact.attackDirection = wallAttackDirection_;
                // CheckAttackTarget (0x0034fca0) supplies +Z * radius to
                // Unit::CheckAttackByPosAndAxis's animated Bip01 origin.
                impact.attackPosition.z += kPlayerCollisionRadiusCentimeters;
            }
        }
        if (threshold >= previousMilliseconds &&
            hitEffectDatabase_ != nullptr) {
            const auto& effectIds = activeAttackState_->auxiliaryIdLists[1];
            if (hitFrames.size() > 1 && !effectIds.empty()) {
                queueHitEffect(effectIds[std::min(hitFrameIndex,
                                                  effectIds.size() - 1)],
                               currentMilliseconds - threshold);
            } else {
                for (const std::int16_t effectId : effectIds) {
                    queueHitEffect(effectId,
                                   currentMilliseconds - threshold);
                }
            }
        }
        ++nextAttackImpactFrameIndex_;
    }

    // UpdateAttackParam (0x00340fa0) does not make the aerial pursuit family
    // a one-shot frame test. Its branch at 0x0034103c-0x00341076 admits
    // motions 0x67, 0x6c, 0x6d, and 0x6f; after the combined animation
    // counter passes frame 2 it requests CheckAttackTarget every update until
    // that function records an accepted contact at Player+0x4f0. This is
    // essential for state 104's downward kick: its first probe occurs while
    // Spider-Man is still above the paired target and later probes follow the
    // native 840 cm/s approach into contact.
    if (repeatsAerialContact &&
        !activeAttackContactAccepted_ && !queuedGenericImpact &&
        currentMilliseconds >= combatEventMilliseconds(3) &&
        pendingMeleeImpactCount_ < pendingMeleeImpacts_.size()) {
        pendingMeleeImpacts_[pendingMeleeImpactCount_++] = {
            activeAttackState_->id,
            activeAttackState_->name,
            animatedBip01Origin(),
            facing_,
            activeAttackState_->motionParameters[0],
            activeAttackState_->motionParameters[1],
            activeAttackState_->motionParameters[2],
            activeAttackState_->motionParameters[3],
            -1,
            false,
            false,
            false,
        };
        auto& retry = pendingMeleeImpacts_[pendingMeleeImpactCount_ - 1];
        retry.hitType = enemyHitTypeForState(*activeAttackState_);
        retry.horizontalForce = activeAttackState_->timingParameters[0];
        retry.verticalForce = activeAttackState_->timingParameters[1];
        if (activeAttackState_->motionType == 109 &&
            activeAttackTarget_.has_value()) {
            // Same native split as the authored-frame contact above. The
            // retry is still a sector query, not targeted-only delivery.
            retry.targetedEnemyObjectId = activeAttackTarget_->objectId;
            retry.airKickDownSplit = true;
        }
    }

    // Player::PlayerStateSFX(state, 1, target) is called by UpdateAttacks
    // only after a target accepts the hit. Application dispatches that list
    // beside the resulting health delta; it is neither a free-running frame
    // cue nor valid on a miss.
}

void GameplayPlayer::queueAttackSoundEvents(
    std::uint32_t previousMilliseconds,
    std::uint32_t currentMilliseconds) noexcept {
    if (activeAttackState_ == nullptr || stateDatabase_ == nullptr) {
        return;
    }
    // Player::PlaySound (0x0034905c) retains entry SoundConfigs that carry
    // animation emitter frames. Player::UpdateSound (0x003429d4) checks every
    // positive emitter through Player::CheckFrame, using the same 20 Hz frame
    // conversion as attack/effect events. Emitter -1/0 configurations are
    // handled immediately by PlayerStateSoundBank on state entry.
    for (const std::int16_t configId :
         activeAttackState_->enterSoundConfigIds) {
        const PlayerSoundConfig* config =
            stateDatabase_->findSoundConfig(configId);
        if (config == nullptr) {
            continue;
        }
        for (std::size_t emitterIndex = 0;
             emitterIndex < config->activeEmitterIds.size(); ++emitterIndex) {
            const std::int16_t authoredFrame =
                config->activeEmitterIds[emitterIndex];
            if (authoredFrame <= 0) {
                continue;
            }
            const std::uint32_t threshold =
                combatEventMilliseconds(authoredFrame);
            if (threshold <= previousMilliseconds ||
                threshold > currentMilliseconds ||
                pendingAttackSoundTriggerCount_ >=
                    pendingAttackSoundTriggers_.size()) {
                continue;
            }
            pendingAttackSoundTriggers_[pendingAttackSoundTriggerCount_++] = {
                configId, emitterIndex, activeAttackState_->name};
        }
    }
}

void GameplayPlayer::queueHitEffect(
    std::int16_t effectId, std::uint32_t elapsedMilliseconds,
    std::uint32_t lifetimeOverrideMilliseconds,
    std::string_view boneNameOverride, float uniformScale,
    bool subtractAmbientMaterial) noexcept {
    if (hitEffectDatabase_ == nullptr) {
        return;
    }
    const PlayerHitEffectDefinition* definition =
        hitEffectDatabase_->find(effectId);
    if (definition == nullptr) {
        return;
    }

    std::uint32_t fadeDuration = lifetimeOverrideMilliseconds;
    bool fadeWithLifetime = lifetimeOverrideMilliseconds != 0;
    if (fadeDuration == 0 && definition->lifetimeMilliseconds > 0.0F) {
        fadeDuration = static_cast<std::uint32_t>(
            std::max(1.0F, definition->lifetimeMilliseconds));
        fadeWithLifetime = true;
    }
    std::uint32_t lifetime = fadeDuration;
    // CAnimObjEffect::IsAlive (0x00390a38) checks the selected non-looping
    // mesh animation before the duration counter. This remains true when a
    // positive duration independently drives Update's alpha fade.
    if (definition->renderingParameter >= 0 &&
        static_cast<std::size_t>(effectId) < hitEffectAssets_.size()) {
        const auto& clips = hitEffectAssets_[static_cast<std::size_t>(effectId)]
                                .animation.clips();
        const std::size_t clipIndex =
            static_cast<std::size_t>(definition->renderingParameter);
        if (clipIndex < clips.size()) {
            lifetime = std::max<std::uint32_t>(
                clips[clipIndex].durationMilliseconds(), 1);
        }
    }
    if (lifetime == 0 || elapsedMilliseconds >= lifetime) {
        return;
    }
    activeHitEffects_.push_back({
        effectId,
        elapsedMilliseconds,
        lifetime,
        fadeDuration,
        activeAnimation_,
        static_cast<std::uint32_t>(animationTimeMilliseconds_ >=
                                           elapsedMilliseconds
                                       ? animationTimeMilliseconds_ -
                                             elapsedMilliseconds
                                       : 0),
        worldTransform_,
        definition->snapshotBoneTransform
            ? assets::Vector3{}
            : attackPhysicsVelocity_,
        {},
        !definition->snapshotBoneTransform,
        boneNameOverride,
        uniformScale,
        fadeWithLifetime,
        subtractAmbientMaterial,
    });
    if (pendingHitEffectSpawnCount_ < pendingHitEffectSpawns_.size()) {
        pendingHitEffectSpawns_[pendingHitEffectSpawnCount_++] = {
            effectId,
            definition->name,
            boneNameOverride.empty() ? std::string_view(definition->boneName)
                                     : boneNameOverride,
            lifetime,
            fadeDuration,
            uniformScale,
            !definition->snapshotBoneTransform,
            subtractAmbientMaterial,
            definition->snapshotBoneTransform
                ? assets::Vector3{}
                : attackPhysicsVelocity_,
        };
    }
}

std::optional<PlayerHitEffectSpawnEvent>
GameplayPlayer::consumeHitEffectSpawn() noexcept {
    if (pendingHitEffectSpawnCount_ == 0) {
        return std::nullopt;
    }
    PlayerHitEffectSpawnEvent event = pendingHitEffectSpawns_[0];
    for (std::size_t index = 1; index < pendingHitEffectSpawnCount_; ++index) {
        pendingHitEffectSpawns_[index - 1] = pendingHitEffectSpawns_[index];
    }
    --pendingHitEffectSpawnCount_;
    return event;
}

void GameplayPlayer::queueSpecialAttackEffects(
    std::uint32_t /*previousMilliseconds*/,
    std::uint32_t currentMilliseconds) noexcept {
    if (activeAttackState_ == nullptr) {
        return;
    }
    if (activeAttackState_->motionType == 136) {
        while (nextUltimatePulseMilliseconds_ != 0 &&
               nextUltimatePulseMilliseconds_ < 1200 &&
               nextUltimatePulseMilliseconds_ <=
                   ultimatePhaseElapsedMilliseconds_) {
            queueHitEffect(23,
                           ultimatePhaseElapsedMilliseconds_ -
                               nextUltimatePulseMilliseconds_);
            nextUltimatePulseMilliseconds_ += 300;
        }
        return;
    }
    const bool redExplosion =
        (activeAttackState_->motionType == 137 &&
         activeAttackState_->id == 109) ||
        activeAttackState_->motionType == 509;
    const bool blackBlink = activeAttackState_->motionType == 510;
    if (!redExplosion && !blackBlink) {
        return;
    }

    constexpr std::uint32_t outFrameMilliseconds =
        combatEventMilliseconds(16);
    constexpr std::uint32_t splashFrameMilliseconds =
        combatEventMilliseconds(20);
    if (!ultimateOutEffectEmitted_ &&
        currentMilliseconds >= outFrameMilliseconds) {
        // UpdateAttacks (0x00351204, 0x00352f04-0x00353018) emits effect 24
        // for red motion 0x1fd and effect 29 for black motion 0x1fe two
        // authored frames before contact.
        queueHitEffect(blackBlink ? 29 : 24,
                       currentMilliseconds - outFrameMilliseconds);
        // Player::UpdateAttacks (0x00351204, 0x00352f04-0x00352f18 and
        // 0x00352fda-0x00353018) stops VoxSound 0x53 immediately after the
        // frame-16 outward mesh is spawned for red/black explode and blink
        // motions.  This precedes both the frame-18 hit and frame-20 splash.
        if (pendingVoxStopEventCount_ < pendingVoxStopEvents_.size()) {
            pendingVoxStopEvents_[pendingVoxStopEventCount_++] = {
                activeAttackState_->id, activeAttackState_->name, 0x53};
        }
        ultimateOutEffectEmitted_ = true;
    }
    if (!ultimateSplashEmitted_ &&
        currentMilliseconds >= splashFrameMilliseconds) {
        if (pendingCombatEffectCount_ < pendingCombatEffects_.size()) {
            constexpr float splashForwardOffset =
                kPlayerCollisionRadiusCentimeters * 0.7F;
            pendingCombatEffects_[pendingCombatEffectCount_++] = {
                blackBlink ? "super_web_splash_black"
                           : "super_web_splash",
                {position_.x + facing_.x * splashForwardOffset,
                 position_.y + facing_.y * splashForwardOffset,
                 position_.z + facing_.z * splashForwardOffset},
                0x54};
        }
        ultimateSplashEmitted_ = true;
    }
}

const PlayerStateDefinition* GameplayPlayer::punchTransition() const noexcept {
    constexpr std::array<std::int16_t, 2> predicates{
        kPressedTransition, kNormalSuitPressedTransition};
    return transitionForButton(kPunchButton, predicates);
}

const PlayerStateDefinition* GameplayPlayer::webTransition(
    PlayerButtonPhase phase) const noexcept {
    if (phase == PlayerButtonPhase::Held) {
        constexpr std::array<std::int16_t, 1> held{kHeldTransition};
        return transitionForButton(kWebButton, held);
    }
    constexpr std::array<std::int16_t, 2> predicates{
        kPressedTransition, kNormalSuitPressedTransition};
    return transitionForButton(kWebButton, predicates);
}

const PlayerStateDefinition* GameplayPlayer::transitionForButton(
    std::int16_t requestedButton,
    std::span<const std::int16_t> acceptedPredicates) const noexcept {
    if (activeAttackState_ == nullptr || stateDatabase_ == nullptr) {
        return nullptr;
    }
    const std::size_t transitionCount =
        std::min({activeAttackState_->transitionFields[0].size(),
                  activeAttackState_->transitionFields[1].size(),
                  activeAttackState_->transitionFields[2].size()});
    for (std::size_t index = 0; index < transitionCount; ++index) {
        const std::int16_t button =
            activeAttackState_->transitionFields[0][index];
        const std::int16_t predicate =
            activeAttackState_->transitionFields[1][index];
        const std::int16_t target =
            activeAttackState_->transitionFields[2][index];
        if (button != requestedButton ||
            std::find(acceptedPredicates.begin(), acceptedPredicates.end(),
                      predicate) == acceptedPredicates.end() ||
            target < 0 || static_cast<std::size_t>(target) >=
                              stateDatabase_->states().size()) {
            continue;
        }
        const PlayerStateDefinition& state =
            stateDatabase_->states()[static_cast<std::size_t>(target)];
        if (state.id == static_cast<std::uint16_t>(target)) {
            return &state;
        }
    }
    return nullptr;
}

bool GameplayPlayer::attackInputWindowOpen() const noexcept {
    if (activeAttackState_ == nullptr) {
        return false;
    }
    // UpdateKeyTrigger (0x0034d0a4) compares the runtime frame directly with
    // StateBasic+0x34/+0x38.  These input-window fields are already expressed
    // in fixed 20-Hz runtime frames; unlike impact/effect frames, CheckFrame's
    // 30-to-20 conversion is not applied to them.
    // Player::PreUpdate -> CGameObject::Update -> UpdateStateFrame samples
    // the current tick before UpdateKeyTrigger. During application input
    // dispatch, update() has not run yet, so include exactly that pending
    // game delta. A state entered by this input remains at native frame zero.
    const std::uint64_t sampledTimeline = attackTimelineMilliseconds_ +
        (inputFramePrepared_ && !attackEnteredDuringPreparedInputFrame_
             ? inputFrameAdvanceMilliseconds_
             : 0U);
    const std::uint64_t frame = runtimeAnimationFrame(sampledTimeline);
    const std::int16_t firstFrame =
        activeAttackState_->auxiliaryParameters[0];
    const std::int16_t lastFrame =
        activeAttackState_->auxiliaryParameters[1];
    return (firstFrame < 0 || frame >= static_cast<std::uint64_t>(firstFrame)) &&
           (lastFrame < 0 || frame <= static_cast<std::uint64_t>(lastFrame));
}

bool GameplayPlayer::canEnableSpiderSense() const noexcept {
    // Player::CanEnableSpiderSense (0x00341c98) first excludes the scripted
    // class-7 family, state 0x49, and the entire red/black ultimate range
    // 0x6b..0x71.  The remaining decision is made from the current state's
    // native motion, class, state ID, and combined animation frame.
    const PlayerStateDefinition* state = activeAttackState_ != nullptr
        ? activeAttackState_
        : activeScriptedState_ != nullptr
            ? activeScriptedState_
            : activeLocomotionState_;
    if (state == nullptr) {
        // Idle is native state 0/motion 0 and is explicitly enabled.
        return true;
    }
    if (state->stateClass == 7 || state->id == 0x49 ||
        (state->id >= 0x6b && state->id <= 0x71)) {
        return false;
    }

    const std::int16_t motion = state->motionType;
    if (motion == 0 || motion == 1 || motion == 11 ||
        (motion >= 17 && motion <= 21)) {
        return true;
    }

    const std::uint64_t pendingAdvance =
        inputFramePrepared_ && !attackEnteredDuringPreparedInputFrame_
            ? inputFrameAdvanceMilliseconds_
            : 0U;
    const std::uint64_t timeline = activeAttackState_ != nullptr
        ? attackTimelineMilliseconds_ + pendingAdvance
        : animationTimeMilliseconds_ + pendingAdvance;
    const std::uint64_t combinedFrame = runtimeAnimationFrame(timeline);
    if (state->stateClass == 6) {
        // StateBasic+0x30 is soundTriggerFrame. The native comparison is
        // inclusive and uses Player+0x4cc plus its linked-animation offset.
        return state->soundTriggerFrame >= 0 &&
               combinedFrame >=
                   static_cast<std::uint64_t>(state->soundTriggerFrame);
    }
    if (motion == 30) {
        return combinedFrame > 2;
    }
    if (state->stateClass == 4) {
        // The final native branch admits ordinary attack states except
        // web-whirlwind motion 0x74, wall-attack motion 0x83, and explicit
        // air-bounce states 0x75/0x76.
        return motion != 116 && motion != 131 && state->id != 117 &&
               state->id != 118;
    }
    return false;
}

bool GameplayPlayer::combatTransitionReady() const noexcept {
    return activeAttackState_ == nullptr || attackInputWindowOpen();
}

std::optional<PlayerMeleeImpact>
GameplayPlayer::consumeMeleeImpact() noexcept {
    if (pendingMeleeImpactCount_ == 0) {
        return std::nullopt;
    }
    PlayerMeleeImpact impact = pendingMeleeImpacts_[0];
    for (std::size_t index = 1; index < pendingMeleeImpactCount_; ++index) {
        pendingMeleeImpacts_[index - 1] = pendingMeleeImpacts_[index];
    }
    --pendingMeleeImpactCount_;
    return impact;
}

void GameplayPlayer::notifyMeleeImpactAccepted(
    std::uint16_t stateId) noexcept {
    if (activeAttackState_ != nullptr && activeAttackState_->id == stateId) {
        activeAttackContactAccepted_ = true;
    }
}

std::optional<PlayerWebPelletLaunch>
GameplayPlayer::consumeWebPelletLaunch() noexcept {
    if (pendingWebPelletLaunchCount_ == 0) {
        return std::nullopt;
    }
    PlayerWebPelletLaunch launch = pendingWebPelletLaunches_[0];
    for (std::size_t index = 1; index < pendingWebPelletLaunchCount_; ++index) {
        pendingWebPelletLaunches_[index - 1] =
            pendingWebPelletLaunches_[index];
    }
    --pendingWebPelletLaunchCount_;
    return launch;
}

std::optional<PlayerEnemyNotifyEvent>
GameplayPlayer::consumeEnemyNotifyEvent() noexcept {
    if (pendingEnemyNotifyEventCount_ == 0) {
        return std::nullopt;
    }
    const PlayerEnemyNotifyEvent event = pendingEnemyNotifyEvents_[0];
    for (std::size_t index = 1; index < pendingEnemyNotifyEventCount_;
         ++index) {
        pendingEnemyNotifyEvents_[index - 1] =
            pendingEnemyNotifyEvents_[index];
    }
    --pendingEnemyNotifyEventCount_;
    return event;
}

std::optional<PlayerCombatEffectEvent>
GameplayPlayer::consumeCombatEffect() noexcept {
    if (pendingCombatEffectCount_ == 0) {
        return std::nullopt;
    }
    const PlayerCombatEffectEvent event = pendingCombatEffects_[0];
    for (std::size_t index = 1; index < pendingCombatEffectCount_; ++index) {
        pendingCombatEffects_[index - 1] = pendingCombatEffects_[index];
    }
    --pendingCombatEffectCount_;
    return event;
}

std::optional<PlayerVoxStopEvent>
GameplayPlayer::consumeVoxStopEvent() noexcept {
    if (pendingVoxStopEventCount_ == 0) {
        return std::nullopt;
    }
    const PlayerVoxStopEvent event = pendingVoxStopEvents_[0];
    for (std::size_t index = 1; index < pendingVoxStopEventCount_; ++index) {
        pendingVoxStopEvents_[index - 1] = pendingVoxStopEvents_[index];
    }
    --pendingVoxStopEventCount_;
    return event;
}

std::optional<PlayerAttackSoundTrigger>
GameplayPlayer::consumeAttackSoundTrigger() noexcept {
    if (pendingAttackSoundTriggerCount_ == 0) {
        return std::nullopt;
    }
    const PlayerAttackSoundTrigger trigger = pendingAttackSoundTriggers_[0];
    for (std::size_t index = 1; index < pendingAttackSoundTriggerCount_;
         ++index) {
        pendingAttackSoundTriggers_[index - 1] =
            pendingAttackSoundTriggers_[index];
    }
    --pendingAttackSoundTriggerCount_;
    return trigger;
}

std::string_view GameplayPlayer::consumeEnteredState() noexcept {
    if (enteredStateCount_ == 0) {
        return {};
    }
    const std::string_view state = enteredStates_[0];
    for (std::size_t index = 1; index < enteredStateCount_; ++index) {
        enteredStates_[index - 1] = enteredStates_[index];
    }
    --enteredStateCount_;
    return state;
}

void GameplayPlayer::setQuickTimeActionPose(
    std::string_view animation, std::uint32_t animationMilliseconds,
    bool loop, const std::array<float, 16>* worldTransform,
    const assets::Vector3* facing,
    const assets::Vector3* detachPosition) noexcept {
    if (!quickTimeActionDriven_) {
        wallWeb_.cancel();
        cancelAttack();
        hurtReactionRemainingMilliseconds_ = 0;
        quickTimeActionDriven_ = true;
    }
    activeAnimation_ = animation;
    animationTimeMilliseconds_ = animationMilliseconds;
    quickTimeActionLoops_ = loop;
    if (worldTransform != nullptr) {
        worldTransform_ = *worldTransform;
    }
    if (facing != nullptr) {
        facing_ = *facing;
    }
    if (detachPosition != nullptr) {
        quickTimeActionDetachPosition_ = *detachPosition;
        quickTimeActionHasDetachPosition_ = true;
    }
}

void GameplayPlayer::clearQuickTimeActionPose() noexcept {
    if (!quickTimeActionDriven_) {
        return;
    }
    // The native state-90 detach keeps the hand-parented scene node's current
    // absolute position before physics is re-enabled. Preserve that visual
    // endpoint in the portable capsule-base representation instead of
    // snapping back to the pre-grab position.
    position_ = quickTimeActionHasDetachPosition_
                    ? quickTimeActionDetachPosition_
                    : assets::Vector3{worldTransform_[12],
                                      worldTransform_[13],
                                      worldTransform_[14]};
    quickTimeActionDriven_ = false;
    quickTimeActionLoops_ = false;
    quickTimeActionHasDetachPosition_ = false;
    activeLocomotionState_ = nullptr;
    locomotionState_ = LocomotionState::Grounded;
    locomotionRootTranslation_ = {};
    renderPosition_ = position_;
    jumpAnchorHeight_ = position_.z;
    setAnimation("idle_stand");
    updateWorldTransform(facing_);
}

Result GameplayPlayer::enterScriptedState(
    std::uint16_t stateId, bool loop,
    const assets::Vector3* facing) {
    if (stateDatabase_ == nullptr || animationBank_ == nullptr ||
        stateId >= stateDatabase_->states().size()) {
        return Result::failure("Player scripted state ID is invalid");
    }
    const PlayerStateDefinition& state =
        stateDatabase_->states()[stateId];
    const assets::ColladaAnimationClip* clip =
        stateClip(animationBank_, &state);
    if (state.id != stateId || clip == nullptr) {
        return Result::failure(
            "Player scripted state has no authored animation");
    }
    wallWeb_.cancel();
    cancelAttack();
    hurtReactionRemainingMilliseconds_ = 0;
    activeLocomotionState_ = nullptr;
    locomotionState_ = LocomotionState::Grounded;
    locomotionRootTranslation_ = {};
    selectedWebGrabPoint_ = nullptr;
    webSwingRuntime_ = {};
    activeScriptedState_ = &state;
    scriptedStateLoops_ = loop;
    activeAnimation_ = clip->name;
    animationTimeMilliseconds_ = 0;
    renderPosition_ = position_;
    if (facing != nullptr) {
        const float length = std::hypot(facing->x, facing->y);
        if (length > std::numeric_limits<float>::epsilon()) {
            facing_ = {facing->x / length, facing->y / length, 0.0F};
        }
    }
    updateWorldTransform(facing_);
    queueEnteredState(state.name);
    return Result::success();
}

void GameplayPlayer::clearScriptedState() noexcept {
    if (activeScriptedState_ == nullptr) {
        return;
    }
    activeScriptedState_ = nullptr;
    scriptedStateLoops_ = false;
    activeLocomotionState_ = nullptr;
    locomotionState_ = LocomotionState::Grounded;
    locomotionRootTranslation_ = {};
    renderPosition_ = position_;
    jumpAnchorHeight_ = position_.z;
    setAnimation("idle_stand");
    updateWorldTransform(facing_);
}

bool GameplayPlayer::scriptedStateAnimationFinished() const noexcept {
    if (activeScriptedState_ == nullptr || scriptedStateLoops_ ||
        animationBank_ == nullptr) {
        return false;
    }
    const assets::ColladaAnimationClip* clip =
        stateClip(animationBank_, activeScriptedState_);
    return clip != nullptr &&
           animationTimeMilliseconds_ >= clip->durationMilliseconds();
}

std::uint32_t GameplayPlayer::animationTimeMilliseconds() const noexcept {
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(
        animationTimeMilliseconds_,
        std::numeric_limits<std::uint32_t>::max()));
}

std::uint32_t GameplayPlayer::attackTimelineMilliseconds() const noexcept {
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(
        attackTimelineMilliseconds_,
        std::numeric_limits<std::uint32_t>::max()));
}

std::uint32_t GameplayPlayer::activeAnimationDurationMilliseconds() const
    noexcept {
    const assets::ColladaAnimationClip* clip = animationBank_ == nullptr
        ? nullptr
        : animationBank_->findClip(activeAnimation_);
    return clip == nullptr ? 0U : clip->durationMilliseconds();
}

bool GameplayPlayer::airborne() const noexcept {
    return activeAttackAirborne_ ||
           locomotionState_ == LocomotionState::JumpStart ||
           locomotionState_ == LocomotionState::JumpFall ||
           locomotionState_ == LocomotionState::ShortWebJump ||
           locomotionState_ == LocomotionState::SustainedFall ||
           locomotionState_ == LocomotionState::WebThrow ||
           locomotionState_ == LocomotionState::SwingHang ||
           locomotionState_ == LocomotionState::SwingRelease ||
           locomotionState_ == LocomotionState::SliderJumpUp ||
           locomotionState_ == LocomotionState::SliderJumpFall;
}

bool GameplayPlayer::onWall() const noexcept {
    return locomotionState_ == LocomotionState::WallAttach ||
           locomotionState_ == LocomotionState::WallIdle ||
           locomotionState_ == LocomotionState::WallMove ||
           locomotionState_ == LocomotionState::WallExit ||
           locomotionState_ == LocomotionState::WallJump;
}

std::uint16_t GameplayPlayer::activeStateId() const noexcept {
    if (dead()) {
        return deadOverState_ == nullptr ? 0x80U : deadOverState_->id;
    }
    if (activeAttackState_ != nullptr) {
        return activeAttackState_->id;
    }
    if (activeScriptedState_ != nullptr) {
        return activeScriptedState_->id;
    }
    return activeLocomotionState_ == nullptr ? 0U
                                             : activeLocomotionState_->id;
}

std::string_view GameplayPlayer::activeStateName() const noexcept {
    if (dead()) {
        return deadOverState_ == nullptr
                   ? std::string_view{"k_state_dead_over"}
                   : deadOverState_->name;
    }
    if (activeAttackState_ != nullptr) {
        return activeAttackState_->name;
    }
    if (activeScriptedState_ != nullptr) {
        return activeScriptedState_->name;
    }
    return activeLocomotionState_ == nullptr ? std::string_view{"k_state_idle"}
                                             : activeLocomotionState_->name;
}

bool GameplayPlayer::punchTransitionReadyAfterImpact() const noexcept {
    if (hurtReactionRemainingMilliseconds_ != 0 || dead()) {
        return false;
    }
    if (activeAttackState_ == nullptr) {
        return true;
    }
    return attackInputWindowOpen() && punchTransition() != nullptr &&
           nextAttackImpactFrameIndex_ >=
               activeAttackState_->auxiliaryIdLists[0].size();
}

bool GameplayPlayer::punchAttackTransitionReady() const noexcept {
    return hurtReactionRemainingMilliseconds_ == 0 && !dead() &&
           activeAttackState_ != nullptr && attackInputWindowOpen() &&
           punchTransition() != nullptr;
}

bool GameplayPlayer::jumpAttackTransitionReady() const noexcept {
    if (hurtReactionRemainingMilliseconds_ != 0 || dead() ||
        activeAttackState_ == nullptr || !attackInputWindowOpen()) {
        return false;
    }
    constexpr std::array<std::int16_t, 1> held{kHeldTransition};
    return transitionForButton(kJumpButton, held) != nullptr;
}

bool GameplayPlayer::jumpReleaseAttackTransitionReady() const noexcept {
    if (hurtReactionRemainingMilliseconds_ != 0 || dead() ||
        activeAttackState_ == nullptr || !attackInputWindowOpen()) {
        return false;
    }
    constexpr std::array<std::int16_t, 1> released{kReleasedTransition};
    return transitionForButton(kJumpButton, released) != nullptr;
}

bool GameplayPlayer::webAttackTransitionReady() const noexcept {
    return hurtReactionRemainingMilliseconds_ == 0 && !dead() &&
           activeAttackState_ != nullptr && attackInputWindowOpen() &&
           webTransition() != nullptr;
}

bool GameplayPlayer::webHeldAttackTransitionReady() const noexcept {
    return hurtReactionRemainingMilliseconds_ == 0 && !dead() &&
           activeAttackState_ != nullptr && attackInputWindowOpen() &&
           webTransition(PlayerButtonPhase::Held) != nullptr;
}

float GameplayPlayer::animatedFootHeight() const noexcept {
    if (locomotionState_ == LocomotionState::JumpStart ||
        locomotionState_ == LocomotionState::JumpFall) {
        return jumpAnchorHeight_ + currentRootHeight();
    }
    return position_.z;
}

bool GameplayPlayer::webLineActive() const noexcept {
    const bool traversalLine =
        selectedWebGrabPoint_ != nullptr &&
        (locomotionState_ == LocomotionState::WebThrow ||
         locomotionState_ == LocomotionState::SwingHang);
    const bool combatLine = !combatWebLinesReleased_ &&
                            activeAttackTarget_.has_value() &&
                            activeAttackState_ != nullptr &&
                            isWebBindingMotion(
                                activeAttackState_->motionType);
    return traversalLine || combatLine || wallWeb_.lineActive();
}

std::size_t GameplayPlayer::webLineCount() const noexcept {
    if (!webLineActive()) {
        return 0;
    }
    // Player::SetNextStateId (0x003491d0) allocates both Player+0x5d4 and
    // Player+0x5d8 for motions 0x70 and 0x7d, attaching FX_RH and FX_LH to
    // the same target head/position. Other combat and traversal paths own a
    // single CobWeb in the currently reconstructed flow.
    if (activeAttackState_ != nullptr &&
        (activeAttackState_->motionType == 112 ||
         activeAttackState_->id == 103 ||
         activeAttackState_->motionType == 125)) {
        return 2;
    }
    return 1;
}

bool GameplayPlayer::canEnableTriggerRestore() const noexcept {
    // Player::CanEnableTriggerRestore (0x0033ff14) rejects web throw/hang
    // motions 26/27, death class 8, and IsDead. Its separate +0x5dc restore
    // latch is owned by LevelRestoreRuntime's active transaction here.
    const auto* state = activeScriptedState_ != nullptr ? activeScriptedState_
        : activeAttackState_ != nullptr ? activeAttackState_ : activeLocomotionState_;
    return !dead() && (state == nullptr ||
        (state->motionType != 26 && state->motionType != 27 && state->stateClass != 8));
}

assets::Vector3 GameplayPlayer::webLineAnchor() const noexcept {
    if (selectedWebGrabPoint_ != nullptr) {
        return selectedWebGrabPoint_->position;
    }
    if (activeAttackTarget_.has_value()) {
        assets::Vector3 anchor = activeAttackTarget_->position;
        anchor.z += activeAttackTarget_->collisionHeight * 0.5F;
        return anchor;
    }
    return {};
}

assets::Vector3 GameplayPlayer::webLineAttachPosition(
    std::size_t lineIndex) const noexcept {
    assets::Vector3 attach = renderPosition_;
    attach.z += 100.0F;
    if (mesh_ == nullptr || animationBank_ == nullptr) {
        return attach;
    }
    const assets::ColladaAnimationClip* clip =
        animationBank_->findClip(activeAnimation_);
    if (clip == nullptr) {
        return attach;
    }
    const std::uint32_t localTime =
        std::min(animationTimeMilliseconds(), clip->durationMilliseconds());
    std::string_view handNode = "FX_RH";
    if (wallWeb_.active()) {
        handNode = wallWeb_.playerHandNode();
    } else if (lineIndex == 1 ||
               (selectedWebGrabPoint_ != nullptr && swingUsesLeftHand_)) {
        handNode = "FX_LH";
    }
    std::array<float, 16> handTransform{};
    const Result handResult = assets::evaluateColladaSceneNodeTransform(
        *mesh_, *animationBank_, clip->startMilliseconds + localTime,
        handNode, handTransform);
    if (!handResult) {
        return attach;
    }
    const assets::Vector3 localHand{handTransform[12], handTransform[13],
                                    handTransform[14]};
    // Native Player construction stores FX_LH/FX_RH at +0x5e8/+0x5ec and
    // CobWeb binds directly to those effect locators. Apply the same actor
    // matrix used for the skinned player mesh so each strand leaves its
    // authored animated hand locator.
    return {
        localHand.x * worldTransform_[0] +
            localHand.y * worldTransform_[4] +
            localHand.z * worldTransform_[8] + worldTransform_[12],
        localHand.x * worldTransform_[1] +
            localHand.y * worldTransform_[5] +
            localHand.z * worldTransform_[9] + worldTransform_[13],
        localHand.x * worldTransform_[2] +
            localHand.y * worldTransform_[6] +
            localHand.z * worldTransform_[10] + worldTransform_[14]};
}

std::int32_t GameplayPlayer::webLineTargetObjectId() const noexcept {
    if (wallWeb_.lineActive()) {
        return wallWeb_.targetObjectId();
    }
    return activeAttackTarget_.has_value() && activeAttackState_ != nullptr &&
                   isWebBindingMotion(activeAttackState_->motionType)
               ? activeAttackTarget_->objectId
               : -1;
}

void GameplayPlayer::setAnimation(std::string_view animation) noexcept {
    if (activeAnimation_ == animation) {
        return;
    }
    activeAnimation_ = animation;
    animationTimeMilliseconds_ = 0;
}

void GameplayPlayer::queueEnteredState(std::string_view stateName) noexcept {
    if (enteredStateCount_ < enteredStates_.size()) {
        enteredStates_[enteredStateCount_++] = stateName;
    }
}

void GameplayPlayer::updateWorldTransform(
    const assets::Vector3& facing) noexcept {
    worldTransform_ = {
        -facing.y * scale_.x,
        facing.x * scale_.x,
        0.0F,
        0.0F,
        -facing.x * scale_.y,
        -facing.y * scale_.y,
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        scale_.z,
        0.0F,
        renderPosition_.x,
        renderPosition_.y,
        renderPosition_.z,
        1.0F,
    };
}

} // namespace usm::game
