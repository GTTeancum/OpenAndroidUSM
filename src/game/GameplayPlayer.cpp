#include "game/GameplayPlayer.hpp"

#include "game/LevelCollision.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <cstdlib>

namespace usm::game {
namespace {

constexpr float kMaximumRunSpeedCentimetersPerSecond = 700.0F;
// Player::UpdateMCSpeed (0x00346f50), motion 13, scales each wall-axis
// joystick component by 0.35 centimeters per millisecond.
constexpr float kWallClimbSpeedCentimetersPerSecond = 350.0F;
constexpr float kPlayerCollisionRadiusCentimeters = 50.0F;
constexpr float kPlayerCollisionHalfHeightCentimeters = 70.0F;
// `consts` image address 0x004c6a78, read by Player::SetNextStateId
// (0x003491d0) when entering k_state_jump_fall_idle.
constexpr float kSustainedFallSpeedCentimetersPerSecond = -1200.0F;
// MC_STATE transition records store the controller action in field zero and
// the native UpdateKeyTrigger predicate in field one. Button 6 is Square;
// predicate 101 is an ordinary press and 150 is the normal-suit press path.
constexpr std::int16_t kPunchButton = 6;
constexpr std::int16_t kPressedTransition = 101;
constexpr std::int16_t kNormalSuitPressedTransition = 150;
constexpr std::uint32_t kAuthoredAnimationFramesPerSecond = 30;

float length2D(float x, float y) noexcept { return std::sqrt(x * x + y * y); }

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
                                      waypoints) {
    if (asset.animationBank.findClip("idle_stand") == nullptr ||
        asset.animationBank.findClip("run") == nullptr) {
        return Result::failure(
            "Player animation bank is missing movement clips");
    }
    position_ = asset.position;
    objectId_ = asset.objectId;
    cinematicDriven_ = false;
    cinematicAnimationLoops_ = true;
    cinematicAnimationSpeed_ = 1.0F;
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
    animationBank_ = &asset.animationBank;
    stateDatabase_ = states;
    initialPunchState_ = nullptr;
    jumpStartState_ = nullptr;
    jumpFallState_ = nullptr;
    sustainedFallState_ = nullptr;
    jumpLandState_ = nullptr;
    swingThrowState_ = nullptr;
    swingHangState_ = nullptr;
    swingIdleState_ = nullptr;
    sliderLandState_ = nullptr;
    sliderMoveState_ = nullptr;
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
    activeLocomotionState_ = nullptr;
    locomotionState_ = LocomotionState::Grounded;
    verticalVelocityCentimetersPerSecond_ = 0.0F;
    swingReleaseVelocity_ = {};
    swingReleaseHasTarget_ = false;
    swingReleaseTarget_ = {};
    wallNormal_ = {};
    wallStateStartPosition_ = {};
    lastWallInput_ = {};
    webGrabPointRuntime_.bind(webGrabPoints, collision);
    webSwingRuntime_ = {};
    slideRuntime_.bind(slides, waypoints);
    selectedWebGrabPoint_ = nullptr;
    swingUsesLeftHand_ = false;
    webReleaseRequested_ = false;
    activeAttackState_ = nullptr;
    nextAttackImpactFrameIndex_ = 0;
    attackFrameSoundEmitted_ = false;
    pendingMeleeImpactCount_ = 0;
    pendingAttackFrameSoundCount_ = 0;
    hurtReactionRemainingMilliseconds_ = 0;
    enteredStateCount_ = 0;
    if (states != nullptr) {
        jumpStartState_ = states->findState("k_state_jump_start");
        jumpFallState_ = states->findState("k_state_jump_fall");
        sustainedFallState_ = states->findState("k_state_jump_fall_idle");
        jumpLandState_ = states->findState("k_state_jump_land");
        swingThrowState_ = states->findState("k_state_swing_web_throw");
        swingHangState_ = states->findState("k_state_swing_hang");
        swingIdleState_ = states->findState("k_state_swing_idle");
        sliderMoveState_ =
            states->findState("k_state_trigger_slider_move");
        sliderLandState_ =
            states->findState("k_state_trigger_slider_land");
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
        if (stateClip(animationBank_, jumpStartState_) == nullptr ||
            stateClip(animationBank_, jumpFallState_) == nullptr ||
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
             stateClip(animationBank_, sliderMoveState_) == nullptr)) {
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
        initialPunchState_ =
            states->findState("k_state_idle_to_punch_right");
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
    }
    maximumHealth_ = std::max(asset.health, 1.0F);
    health_ = maximumHealth_;
    skillPoints_ = 0;
    if (collision_ != nullptr) {
        assets::Vector3 grounded;
        (void)collision_->resolveGroundMotion(position_, position_, grounded,
                                              100.0F, 500.0F);
        position_ = grounded;
        renderPosition_ = position_;
        jumpAnchorHeight_ = position_.z;
        updateWorldTransform(facing_);
    }
    return Result::success();
}

bool GameplayPlayer::requestPunch() noexcept {
    if (locomotionState_ != LocomotionState::Grounded || dead() ||
        hurtReactionRemainingMilliseconds_ != 0) {
        return false;
    }
    if (activeAttackState_ == nullptr) {
        return initialPunchState_ != nullptr &&
               enterAttackState(*initialPunchState_);
    }
    if (!attackInputWindowOpen()) {
        return false;
    }
    const PlayerStateDefinition* transition = punchTransition();
    return transition != nullptr && enterAttackState(*transition);
}

bool GameplayPlayer::requestJump() noexcept {
    if (activeAttackState_ != nullptr || dead() ||
        hurtReactionRemainingMilliseconds_ != 0) {
        return false;
    }
    if (onWall()) {
        const PlayerStateDefinition* jumpState = wallJumpUpState_;
        if (std::abs(lastWallInput_.right) >
            std::abs(lastWallInput_.forward)) {
            jumpState = lastWallInput_.right < 0.0F
                            ? wallJumpLeftState_
                            : wallJumpRightState_;
        } else if (lastWallInput_.forward < -0.25F) {
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
    if (locomotionState_ != LocomotionState::Grounded ||
        jumpStartState_ == nullptr) {
        return false;
    }
    jumpAnchorHeight_ = position_.z;
    enterLocomotionState(LocomotionState::JumpStart);
    return true;
}

bool GameplayPlayer::requestWeb() noexcept {
    if (!airborne() || locomotionState_ == LocomotionState::WebThrow ||
        locomotionState_ == LocomotionState::SwingHang ||
        locomotionState_ == LocomotionState::SwingRelease || dead() ||
        swingThrowState_ == nullptr) {
        return false;
    }
    assets::Vector3 visualPosition = position_;
    visualPosition.z = animatedFootHeight();
    selectedWebGrabPoint_ =
        webGrabPointRuntime_.search(visualPosition, facing_);
    if (selectedWebGrabPoint_ == nullptr) {
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
    std::uint32_t minimumReactionMilliseconds) noexcept {
    if (damage <= 0.0F || dead()) {
        return false;
    }
    health_ = std::max(0.0F, health_ - damage);
    const PlayerStateDefinition* hurtState =
        damageType == 1 ? hurtHeavyState_ : hurtLightState_;
    const assets::ColladaAnimationClip* hurtClip =
        stateClip(animationBank_, hurtState);
    if (!dead() && !cinematicDriven_ &&
        locomotionState_ == LocomotionState::Grounded &&
        hurtClip != nullptr) {
        cancelAttack();
        setAnimation(hurtClip->name);
        hurtReactionRemainingMilliseconds_ = std::max(
            minimumReactionMilliseconds, hurtClip->durationMilliseconds());
    }
    return true;
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

void GameplayPlayer::restoreAt(const assets::Vector3& position,
                               const assets::Vector3& facing) noexcept {
    position_ = position;
    renderPosition_ = position;
    jumpAnchorHeight_ = position.z;
    const float facingLength = length2D(facing.x, facing.y);
    if (facingLength > std::numeric_limits<float>::epsilon()) {
        facing_ = {facing.x / facingLength, facing.y / facingLength, 0.0F};
    }
    activeLocomotionState_ = nullptr;
    locomotionState_ = LocomotionState::Grounded;
    verticalVelocityCentimetersPerSecond_ = 0.0F;
    swingReleaseVelocity_ = {};
    swingReleaseHasTarget_ = false;
    selectedWebGrabPoint_ = nullptr;
    wallNormal_ = {};
    lastWallInput_ = {};
    webSwingRuntime_ = {};
    webReleaseRequested_ = false;
    cancelAttack();
    hurtReactionRemainingMilliseconds_ = 0;
    setAnimation("idle_stand");
    updateWorldTransform(facing_);
}

Result GameplayPlayer::applyCinematicCommand(
    const CinematicThread& thread, const CinematicCommand& command) {
    if (thread.objectId != objectId_) {
        return Result::success();
    }
    if (command.name == "DisableAI") {
        cinematicDriven_ = true;
        cancelAttack();
        return Result::success();
    }
    if (command.name == "EnableAI") {
        cinematicDriven_ = false;
        cinematicAnimationSpeed_ = 1.0F;
        locomotionState_ = LocomotionState::Grounded;
        renderPosition_ = position_;
        return Result::success();
    }
    if (command.name == "SetAnim") {
        const CinematicAttribute* animation = command.findAttribute("$Anim");
        if (animation == nullptr || animationBank_ == nullptr) {
            return Result::failure("Player SetAnim has no animation name");
        }
        const assets::ColladaAnimationClip* clip =
            animationBank_->findClip(animation->value);
        if (clip == nullptr) {
            return Result::failure("Player SetAnim references a missing clip");
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
    if (dead()) {
        return;
    }
    if (cinematicDriven_) {
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
        if (hurtReactionRemainingMilliseconds_ == 0) {
            setAnimation("idle_stand");
        }
        return;
    }
    if (activeAttackState_ != nullptr) {
        updateAttack(elapsedMilliseconds);
        return;
    }
    if ((locomotionState_ == LocomotionState::JumpFall ||
         locomotionState_ == LocomotionState::SwingRelease) &&
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
        if (collision_->resolveGroundMotion(position_, desired, resolved)) {
            position_ = resolved;
        } else {
            collision_->resolveAirMotion(position_, desired, position_);
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
    locomotionState_ = state;
    switch (state) {
    case LocomotionState::Grounded:
        activeLocomotionState_ = nullptr;
        selectedWebGrabPoint_ = nullptr;
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
            stateClip(animationBank_, activeLocomotionState_);
        if (clip != nullptr) {
            setAnimation(clip->name);
        }
        if (state == LocomotionState::JumpStart ||
            state == LocomotionState::JumpLand ||
            state == LocomotionState::SliderMove ||
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
        collision_->resolveAirMotion(position_, desired, position_);
    } else {
        position_ = desired;
    }
    renderPosition_.x = position_.x;
    renderPosition_.y = position_.y;
    facing_ = movement;
}

void GameplayPlayer::enterSwingHang() noexcept {
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
    swingReleaseHasTarget_ = released.hasTargetWaypoint;
    swingReleaseTarget_ = released.targetWaypointPosition;
    activeLocomotionState_ = swingIdleState_;
    locomotionState_ = LocomotionState::SwingRelease;
    const assets::ColladaAnimationClip* clip =
        clipById(animationBank_, swingUsesLeftHand_ ? 170 : 173);
    if (clip != nullptr) {
        setAnimation(clip->name);
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
        enterSwingHang();
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
        return;
    }

    if (locomotionState_ != LocomotionState::SwingRelease) {
        return;
    }
    const float elapsedSeconds =
        static_cast<float>(elapsedMilliseconds) / 1000.0F;
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
        const float travelSpeed = std::max(
            length3D(swingReleaseVelocity_),
            kMaximumRunSpeedCentimetersPerSecond);
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
        float inputMagnitude{};
        assets::Vector3 movement;
        if (calculateMovement(input, camera, facing_, movement,
                              inputMagnitude)) {
            constexpr float kReleaseSteeringAcceleration = 700.0F;
            swingReleaseVelocity_.x += movement.x * inputMagnitude *
                                       kReleaseSteeringAcceleration *
                                       elapsedSeconds;
            swingReleaseVelocity_.y += movement.y * inputMagnitude *
                                       kReleaseSteeringAcceleration *
                                       elapsedSeconds;
        }
    }
    if (!swingReleaseHasTarget_) {
        swingReleaseVelocity_.z +=
            kSustainedFallSpeedCentimetersPerSecond * elapsedSeconds;
    }
    const float previousHeight = position_.z;
    assets::Vector3 desired{
        position_.x + swingReleaseVelocity_.x * elapsedSeconds,
        position_.y + swingReleaseVelocity_.y * elapsedSeconds,
        position_.z + swingReleaseVelocity_.z * elapsedSeconds,
    };
    if (collision_ != nullptr) {
        collision_->resolveAirMotion(position_, desired, desired);
    }
    if (swingReleaseHasTarget_ &&
        length3D({swingReleaseTarget_.x - position_.x,
                  swingReleaseTarget_.y - position_.y,
                  swingReleaseTarget_.z - position_.z}) <=
            length3D(swingReleaseVelocity_) * elapsedSeconds) {
        desired = swingReleaseTarget_;
        swingReleaseHasTarget_ = false;
    }
    position_ = desired;
    renderPosition_ = position_;
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
    animationTimeMilliseconds_ += elapsedMilliseconds;
    const assets::ColladaAnimationClip* clip =
        animationBank_ == nullptr
            ? nullptr
            : animationBank_->findClip(activeAnimation_);
    if (clip != nullptr && animationTimeMilliseconds_ >=
                               clip->durationMilliseconds() &&
        swingReleaseVelocity_.z <= 0.0F) {
        selectedWebGrabPoint_ = nullptr;
        enterLocomotionState(LocomotionState::SustainedFall);
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
    const SlideCatch caught = slideRuntime_.findCatch(visualPosition);
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
    if (exit.electricShock) {
        (void)applyDamage(100.0F);
    }
    jumpAnchorHeight_ = position_.z;
    enterLocomotionState(LocomotionState::SustainedFall);
    if (!exit.useGravity) {
        verticalVelocityCentimetersPerSecond_ =
            exit.velocityCentimetersPerSecond.z;
    }
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
    const assets::Vector3 start{
        candidate.x, candidate.y,
        candidate.z + kPlayerCollisionHalfHeightCentimeters};
    const assets::Vector3 end{
        start.x - wallNormal_.x * 125.0F,
        start.y - wallNormal_.y * 125.0F, start.z};
    LevelWallContact contact;
    if (!collision_->climbableWallContact(start, end, contact)) {
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
    position_ = {contact.position.x +
                     wallNormal_.x * kPlayerCollisionRadiusCentimeters,
                 contact.position.y +
                     wallNormal_.y * kPlayerCollisionRadiusCentimeters,
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
        const PlayerStateDefinition* completedState = activeLocomotionState_;
        if (!finishRootMotionState()) {
            return;
        }
        if (reacquireWallAt(position_)) {
            enterLocomotionState(LocomotionState::WallIdle);
        } else if (completedState == wallJumpUpState_) {
            wallStateStartPosition_ = position_;
            enterLocomotionState(LocomotionState::WallExit);
            if (const auto* clip = clipById(animationBank_, 198)) {
                setAnimation(clip->name);
            }
        } else {
            jumpAnchorHeight_ = position_.z;
            enterLocomotionState(LocomotionState::SustainedFall);
        }
        return;
    }
    if (locomotionState_ == LocomotionState::WallExit) {
        if (!finishRootMotionState()) {
            return;
        }
        assets::Vector3 grounded = position_;
        if (collision_ != nullptr &&
            collision_->resolveGroundMotion(position_, position_, grounded,
                                             300.0F, 500.0F)) {
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
    const assets::Vector3 localX{-facing_.y, facing_.x, 0.0F};
    const float travel = kWallClimbSpeedCentimetersPerSecond *
                         (static_cast<float>(elapsedMilliseconds) / 1000.0F);
    assets::Vector3 candidate{
        position_.x + localX.x * right * travel,
        position_.y + localX.y * right * travel,
        position_.z + upward * travel};
    if (!reacquireWallAt(candidate)) {
        if (upward > 0.1F) {
            wallStateStartPosition_ = position_;
            enterLocomotionState(LocomotionState::WallExit);
            if (const auto* clip = clipById(animationBank_, 198)) {
                setAnimation(clip->name);
            }
        } else if (upward < -0.1F) {
            jumpAnchorHeight_ = position_.z;
            enterLocomotionState(LocomotionState::SustainedFall);
        } else {
            enterLocomotionState(LocomotionState::WallIdle);
        }
        return;
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
    return collision_->groundHeight(reference, 20.0F, 5000.0F, height);
}

void GameplayPlayer::updateJump(const PlayerMotionInput& input,
                                const CameraPose& camera,
                                std::uint32_t elapsedMilliseconds) noexcept {
    updateAirHorizontalMotion(input, camera, elapsedMilliseconds);
    std::uint32_t remaining = elapsedMilliseconds;
    while (remaining > 0 &&
           locomotionState_ != LocomotionState::Grounded) {
        if (locomotionState_ == LocomotionState::SustainedFall) {
            const float previousHeight = position_.z;
            float landingHeight{};
            const bool hasLanding =
                findLandingHeight(previousHeight, landingHeight);
            position_.z += verticalVelocityCentimetersPerSecond_ *
                           (static_cast<float>(remaining) / 1000.0F);
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
    activeAttackState_ = &state;
    nextAttackImpactFrameIndex_ = 0;
    attackFrameSoundEmitted_ = false;
    activeAnimation_ = clip->name;
    animationTimeMilliseconds_ = 0;
    queueEnteredState(state.name);
    return true;
}

void GameplayPlayer::cancelAttack() noexcept {
    activeAttackState_ = nullptr;
    nextAttackImpactFrameIndex_ = 0;
    attackFrameSoundEmitted_ = false;
    pendingMeleeImpactCount_ = 0;
    pendingAttackFrameSoundCount_ = 0;
}

void GameplayPlayer::updateAttack(
    std::uint32_t elapsedMilliseconds) noexcept {
    std::uint32_t remaining = elapsedMilliseconds;
    while (activeAttackState_ != nullptr && remaining != 0) {
        const assets::ColladaAnimationClip* clip =
            stateClip(animationBank_, activeAttackState_);
        if (clip == nullptr || clip->durationMilliseconds() == 0) {
            cancelAttack();
            setAnimation("idle_stand");
            return;
        }
        const std::uint32_t duration = clip->durationMilliseconds();
        const std::uint32_t previous = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(animationTimeMilliseconds_, duration));
        const std::uint32_t step =
            std::min(remaining, duration - previous);
        const std::uint32_t current = previous + step;
        animationTimeMilliseconds_ = current;
        remaining -= step;
        queueAttackFrameEvents(previous, current);
        if (current < duration) {
            return;
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
        if (nextState == nullptr || !enterAttackState(*nextState)) {
            activeAttackState_ = nullptr;
            nextAttackImpactFrameIndex_ = 0;
            attackFrameSoundEmitted_ = false;
            setAnimation("idle_stand");
            return;
        }
    }
}

void GameplayPlayer::queueAttackFrameEvents(
    std::uint32_t previousMilliseconds,
    std::uint32_t currentMilliseconds) noexcept {
    if (activeAttackState_ == nullptr) {
        return;
    }
    const auto& hitFrames = activeAttackState_->auxiliaryIdLists[0];
    while (nextAttackImpactFrameIndex_ < hitFrames.size()) {
        const std::int16_t frame = hitFrames[nextAttackImpactFrameIndex_];
        if (frame < 0) {
            ++nextAttackImpactFrameIndex_;
            continue;
        }
        const std::uint32_t threshold =
            static_cast<std::uint32_t>(frame) * 1000U /
            kAuthoredAnimationFramesPerSecond;
        if (threshold > currentMilliseconds) {
            break;
        }
        if (threshold >= previousMilliseconds &&
            pendingMeleeImpactCount_ < pendingMeleeImpacts_.size()) {
            pendingMeleeImpacts_[pendingMeleeImpactCount_++] = {
                activeAttackState_->id,
                activeAttackState_->name,
                activeAttackState_->motionParameters[0],
                activeAttackState_->motionParameters[1],
                activeAttackState_->motionParameters[2],
                activeAttackState_->motionParameters[3],
            };
        }
        ++nextAttackImpactFrameIndex_;
    }

    if (!attackFrameSoundEmitted_ &&
        activeAttackState_->soundTriggerFrame >= 0) {
        const std::uint32_t threshold =
            static_cast<std::uint32_t>(
                activeAttackState_->soundTriggerFrame) *
            1000U / kAuthoredAnimationFramesPerSecond;
        if (threshold >= previousMilliseconds &&
            threshold <= currentMilliseconds) {
            if (pendingAttackFrameSoundCount_ <
                pendingAttackFrameSounds_.size()) {
                pendingAttackFrameSounds_[pendingAttackFrameSoundCount_++] =
                    activeAttackState_->name;
            }
            attackFrameSoundEmitted_ = true;
        }
    }
}

const PlayerStateDefinition* GameplayPlayer::punchTransition() const noexcept {
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
        if (button != kPunchButton ||
            (predicate != kPressedTransition &&
             predicate != kNormalSuitPressedTransition) ||
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
    const std::uint32_t frame = static_cast<std::uint32_t>(
        animationTimeMilliseconds_ * kAuthoredAnimationFramesPerSecond /
        1000U);
    const std::int16_t firstFrame =
        activeAttackState_->auxiliaryParameters[0];
    const std::int16_t lastFrame =
        activeAttackState_->auxiliaryParameters[1];
    return (firstFrame < 0 || frame >= static_cast<std::uint32_t>(firstFrame)) &&
           (lastFrame < 0 || frame <= static_cast<std::uint32_t>(lastFrame));
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

std::string_view GameplayPlayer::consumeAttackFrameSound() noexcept {
    if (pendingAttackFrameSoundCount_ == 0) {
        return {};
    }
    const std::string_view stateName = pendingAttackFrameSounds_[0];
    for (std::size_t index = 1; index < pendingAttackFrameSoundCount_; ++index) {
        pendingAttackFrameSounds_[index - 1] =
            pendingAttackFrameSounds_[index];
    }
    --pendingAttackFrameSoundCount_;
    return stateName;
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

std::uint32_t GameplayPlayer::animationTimeMilliseconds() const noexcept {
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(
        animationTimeMilliseconds_,
        std::numeric_limits<std::uint32_t>::max()));
}

bool GameplayPlayer::airborne() const noexcept {
    return locomotionState_ == LocomotionState::JumpStart ||
           locomotionState_ == LocomotionState::JumpFall ||
           locomotionState_ == LocomotionState::SustainedFall ||
           locomotionState_ == LocomotionState::WebThrow ||
           locomotionState_ == LocomotionState::SwingHang ||
           locomotionState_ == LocomotionState::SwingRelease;
}

bool GameplayPlayer::onWall() const noexcept {
    return locomotionState_ == LocomotionState::WallAttach ||
           locomotionState_ == LocomotionState::WallIdle ||
           locomotionState_ == LocomotionState::WallMove ||
           locomotionState_ == LocomotionState::WallExit ||
           locomotionState_ == LocomotionState::WallJump;
}

std::uint16_t GameplayPlayer::activeStateId() const noexcept {
    if (activeAttackState_ != nullptr) {
        return activeAttackState_->id;
    }
    return activeLocomotionState_ == nullptr ? 0U
                                             : activeLocomotionState_->id;
}

std::string_view GameplayPlayer::activeStateName() const noexcept {
    if (activeAttackState_ != nullptr) {
        return activeAttackState_->name;
    }
    return activeLocomotionState_ == nullptr ? std::string_view{"k_state_idle"}
                                             : activeLocomotionState_->name;
}

bool GameplayPlayer::punchTransitionReadyAfterImpact() const noexcept {
    if (activeAttackState_ == nullptr) {
        return true;
    }
    return attackInputWindowOpen() && punchTransition() != nullptr &&
           nextAttackImpactFrameIndex_ >=
               activeAttackState_->auxiliaryIdLists[0].size();
}

float GameplayPlayer::animatedFootHeight() const noexcept {
    if (locomotionState_ == LocomotionState::JumpStart ||
        locomotionState_ == LocomotionState::JumpFall) {
        return jumpAnchorHeight_ + currentRootHeight();
    }
    return position_.z;
}

bool GameplayPlayer::webLineActive() const noexcept {
    return selectedWebGrabPoint_ != nullptr &&
           (locomotionState_ == LocomotionState::WebThrow ||
            locomotionState_ == LocomotionState::SwingHang);
}

assets::Vector3 GameplayPlayer::webLineAnchor() const noexcept {
    return selectedWebGrabPoint_ == nullptr
               ? assets::Vector3{}
               : selectedWebGrabPoint_->position;
}

assets::Vector3 GameplayPlayer::webLineAttachPosition() const noexcept {
    assets::Vector3 attach = renderPosition_;
    // The original CobWeb endpoint follows the selected animated hand node.
    // The portable gameplay API stays skeleton-independent and exposes a
    // shoulder-height root endpoint for the renderer's line pass.
    attach.z += 100.0F;
    return attach;
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
