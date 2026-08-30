#include "game/WebSwingRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace usm::game {
namespace {

constexpr assets::Vector3 kUp{0.0F, 0.0F, 1.0F};
// GetPalstance (0x00341ff8) integrates pendulum acceleration with the
// recovered 120 constant in the original 0.1-scaled level timestep. The
// equivalent native seconds-based acceleration is 1200 cm/s^2.
constexpr float kSwingGravityCentimetersPerSecondSquared = 1200.0F;
constexpr float kAngularDampingPerSecond = 0.18F;
constexpr float kMaximumSimulationStepSeconds = 1.0F / 120.0F;

assets::Vector3 subtract(const assets::Vector3& left,
                         const assets::Vector3& right) noexcept {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

assets::Vector3 scale(const assets::Vector3& value, float factor) noexcept {
    return {value.x * factor, value.y * factor, value.z * factor};
}

float dot(const assets::Vector3& left,
          const assets::Vector3& right) noexcept {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

float lengthSquared(const assets::Vector3& value) noexcept {
    return dot(value, value);
}

bool normalize(assets::Vector3& value) noexcept {
    const float squared = lengthSquared(value);
    if (squared <= std::numeric_limits<float>::epsilon()) {
        return false;
    }
    const float inverseLength = 1.0F / std::sqrt(squared);
    value = scale(value, inverseLength);
    return true;
}

} // namespace

Result WebSwingRuntime::start(
    const LevelWebGrabPointAsset& point,
    const assets::Vector3& playerPosition,
    const assets::Vector3& playerVelocityCentimetersPerSecond) noexcept {
    if (!std::isfinite(point.length) || point.length <= 0.0F) {
        return Result::failure("Web grab point has no positive rope length");
    }
    point_ = &point;
    travelDirection_ = {point.direction.x, point.direction.y, 0.0F};
    if (!normalize(travelDirection_)) {
        travelDirection_ = subtract(playerPosition, point.position);
        travelDirection_.z = 0.0F;
        if (!normalize(travelDirection_)) {
            travelDirection_ = {1.0F, 0.0F, 0.0F};
        }
    }

    assets::Vector3 initialRope = subtract(playerPosition, point.position);
    // StartWebSwing projects the current point-to-player vector onto the
    // vertical plane selected by CWebGrabPoint::GetDir.
    const float horizontal = dot(initialRope, travelDirection_);
    initialRope = {travelDirection_.x * horizontal,
                   travelDirection_.y * horizontal, initialRope.z};
    if (!normalize(initialRope)) {
        initialRope = {0.0F, 0.0F, -1.0F};
    }
    ropeDirection_ = initialRope;
    angleRadians_ = std::atan2(dot(ropeDirection_, travelDirection_),
                               -dot(ropeDirection_, kUp));
    const assets::Vector3 tangent{
        travelDirection_.x * std::cos(angleRadians_),
        travelDirection_.y * std::cos(angleRadians_),
        std::sin(angleRadians_),
    };
    angularVelocityRadiansPerSecond_ =
        dot(playerVelocityCentimetersPerSecond, tangent) / point.length;
    refreshPose();
    return Result::success();
}

void WebSwingRuntime::update(std::uint32_t elapsedMilliseconds) noexcept {
    if (point_ == nullptr || elapsedMilliseconds == 0) {
        return;
    }
    float remainingSeconds =
        static_cast<float>(elapsedMilliseconds) / 1000.0F;
    while (remainingSeconds > 0.0F) {
        const float step =
            std::min(remainingSeconds, kMaximumSimulationStepSeconds);
        const float angularAcceleration =
            -(kSwingGravityCentimetersPerSecondSquared / point_->length) *
                std::sin(angleRadians_) -
            kAngularDampingPerSecond * angularVelocityRadiansPerSecond_;
        angularVelocityRadiansPerSecond_ += angularAcceleration * step;
        angleRadians_ += angularVelocityRadiansPerSecond_ * step;
        remainingSeconds -= step;
    }
    refreshPose();
}

WebSwingRelease WebSwingRuntime::release() noexcept {
    WebSwingRelease released;
    if (point_ == nullptr) {
        return released;
    }
    assets::Vector3 tangent{
        travelDirection_.x * std::cos(angleRadians_),
        travelDirection_.y * std::cos(angleRadians_),
        std::sin(angleRadians_),
    };
    if (angularVelocityRadiansPerSecond_ < 0.0F) {
        tangent = scale(tangent, -1.0F);
    }
    const float exitSpeedCentimetersPerSecond = point_->exitSpeed * 1000.0F;
    released.velocityCentimetersPerSecond =
        scale(tangent, exitSpeedCentimetersPerSecond);
    // Motion type 28 doubles the vertical exit component before applying the
    // recovered 1000 ms-to-seconds scale at SetNextStateId+0x5b0.
    released.velocityCentimetersPerSecond.z *= 2.0F;
    released.hasTargetWaypoint = point_->hasTargetWaypoint;
    released.targetWaypointPosition = point_->targetWaypointPosition;
    released.targetSlideId = point_->targetSlideId;
    point_ = nullptr;
    return released;
}

void WebSwingRuntime::refreshPose() noexcept {
    if (point_ == nullptr) {
        return;
    }
    const float sine = std::sin(angleRadians_);
    const float cosine = std::cos(angleRadians_);
    ropeDirection_ = {
        travelDirection_.x * sine,
        travelDirection_.y * sine,
        -cosine,
    };
    position_ = {
        point_->position.x + ropeDirection_.x * point_->length,
        point_->position.y + ropeDirection_.y * point_->length,
        point_->position.z + ropeDirection_.z * point_->length,
    };
    const assets::Vector3 tangent{
        travelDirection_.x * cosine,
        travelDirection_.y * cosine,
        sine,
    };
    velocityCentimetersPerSecond_ = scale(
        tangent, angularVelocityRadiansPerSecond_ * point_->length);
}

} // namespace usm::game
