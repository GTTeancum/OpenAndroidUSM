#include "game/GameplayPlayer.hpp"

#include "game/LevelCollision.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace usm::game {
namespace {

constexpr float kMaximumRunSpeedCentimetersPerSecond = 700.0F;
constexpr std::uint32_t kPunchImpactMilliseconds = 180;

float length2D(float x, float y) noexcept { return std::sqrt(x * x + y * y); }

} // namespace

Result GameplayPlayer::initialize(const LevelPlayerAsset& asset,
                                  const LevelCollision* collision) {
    if (asset.animationBank.findClip("idle_stand") == nullptr ||
        asset.animationBank.findClip("run") == nullptr ||
        asset.animationBank.findClip("idle_to_punch_right") == nullptr ||
        asset.animationBank.findClip("punch_right_to_idle") == nullptr) {
        return Result::failure(
            "Player animation bank is missing movement or punch clips");
    }
    position_ = asset.position;
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
    attackState_ = AttackState::None;
    punchImpactPending_ = false;
    punchImpactEmitted_ = false;
    if (collision_ != nullptr) {
        assets::Vector3 grounded;
        (void)collision_->resolveGroundMotion(position_, position_, grounded,
                                              100.0F, 500.0F);
        position_ = grounded;
        worldTransform_[12] = position_.x;
        worldTransform_[13] = position_.y;
        worldTransform_[14] = position_.z;
    }
    return Result::success();
}

bool GameplayPlayer::requestPunch() noexcept {
    if (attackState_ != AttackState::None) {
        return false;
    }
    attackState_ = AttackState::PunchRight;
    punchImpactPending_ = false;
    punchImpactEmitted_ = false;
    setAnimation("idle_to_punch_right");
    return true;
}

void GameplayPlayer::update(const PlayerMotionInput& input,
                            const CameraPose& camera,
                            std::uint32_t elapsedMilliseconds) noexcept {
    if (attackState_ != AttackState::None) {
        animationTimeMilliseconds_ += elapsedMilliseconds;
        if (attackState_ == AttackState::PunchRight) {
            if (!punchImpactEmitted_ &&
                animationTimeMilliseconds_ >= kPunchImpactMilliseconds) {
                punchImpactPending_ = true;
                punchImpactEmitted_ = true;
            }
            // The authored clip spans 333 ms in spiderman_anim.bdae.
            if (animationTimeMilliseconds_ >= 333) {
                const std::uint64_t carry = animationTimeMilliseconds_ - 333;
                attackState_ = AttackState::Recover;
                setAnimation("punch_right_to_idle");
                animationTimeMilliseconds_ = carry;
            }
        }
        // The authored recovery clip spans 466 ms.
        if (attackState_ == AttackState::Recover &&
            animationTimeMilliseconds_ >= 466) {
            attackState_ = AttackState::None;
            setAnimation("idle_stand");
        }
        return;
    }
    float inputMagnitude = length2D(input.right, input.forward);
    if (inputMagnitude <= 1e-4F || elapsedMilliseconds == 0) {
        setAnimation("idle_stand");
        animationTimeMilliseconds_ += elapsedMilliseconds;
        return;
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
        cameraForwardX = facing_.x;
        cameraForwardY = facing_.y;
    }
    const float cameraRightX = cameraForwardY;
    const float cameraRightY = -cameraForwardX;
    assets::Vector3 movement{
        cameraRightX * rightInput + cameraForwardX * forwardInput,
        cameraRightY * rightInput + cameraForwardY * forwardInput, 0.0F};
    const float movementLength = length2D(movement.x, movement.y);
    if (movementLength > std::numeric_limits<float>::epsilon()) {
        movement.x /= movementLength;
        movement.y /= movementLength;
    }

    // The original constant at image address 0x004c6a7c is 700 cm/s and
    // GetMCSpeedByJoyStick divides it by 1000 for millisecond timesteps.
    const float distance = kMaximumRunSpeedCentimetersPerSecond *
                           inputMagnitude *
                           (static_cast<float>(elapsedMilliseconds) / 1000.0F);
    assets::Vector3 desired = position_;
    desired.x += movement.x * distance;
    desired.y += movement.y * distance;
    if (collision_ != nullptr) {
        assets::Vector3 resolved;
        (void)collision_->resolveGroundMotion(position_, desired, resolved);
        position_ = resolved;
    } else {
        position_ = desired;
    }
    facing_ = movement;
    updateWorldTransform(facing_);
    setAnimation("run");
    animationTimeMilliseconds_ += elapsedMilliseconds;
}

bool GameplayPlayer::consumePunchImpact() noexcept {
    const bool pending = punchImpactPending_;
    punchImpactPending_ = false;
    return pending;
}

std::uint32_t GameplayPlayer::animationTimeMilliseconds() const noexcept {
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(
        animationTimeMilliseconds_,
        std::numeric_limits<std::uint32_t>::max()));
}

void GameplayPlayer::setAnimation(std::string_view animation) noexcept {
    if (activeAnimation_ == animation) {
        return;
    }
    activeAnimation_ = animation;
    animationTimeMilliseconds_ = 0;
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
        position_.x,
        position_.y,
        position_.z,
        1.0F,
    };
}

} // namespace usm::game
