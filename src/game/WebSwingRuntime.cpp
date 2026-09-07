#include "game/WebSwingRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace usm::game {
namespace {

constexpr assets::Vector3 kUp{0.0F, 0.0F, 1.0F};
constexpr assets::Vector3 kDown{0.0F, 0.0F, -1.0F};
constexpr float kDegreesToRadians = 0.01745329251994329577F;
// FinishPalstanceTime (0x003420f0) advances the authored swing in 50 ms
// increments. StartWebSwing stores 90 percent of that duration as the
// catch-up window used by UpdateMCSpeed.
constexpr float kNativeSwingStepSeconds = 0.05F;
constexpr float kNativeSwingAcceleration = 120.0F;
constexpr float kNativeSwingDamping = 60.0F;
constexpr float kSwingCatchUpScale = 0.9F;

assets::Vector3 subtract(const assets::Vector3& left,
                         const assets::Vector3& right) noexcept {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

assets::Vector3 scale(const assets::Vector3& value, float factor) noexcept {
    return {value.x * factor, value.y * factor, value.z * factor};
}

assets::Vector3 add(const assets::Vector3& left,
                    const assets::Vector3& right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
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

assets::Vector3 cross(const assets::Vector3& left,
                      const assets::Vector3& right) noexcept {
    return {left.y * right.z - left.z * right.y,
            left.z * right.x - left.x * right.z,
            left.x * right.y - left.y * right.x};
}

assets::Vector3 rotateAroundAxis(const assets::Vector3& value,
                                 assets::Vector3 axis,
                                 float angleRadians) noexcept {
    if (!normalize(axis)) {
        return value;
    }
    const float cosine = std::cos(angleRadians);
    const float sine = std::sin(angleRadians);
    return add(add(scale(value, cosine), scale(cross(axis, value), sine)),
               scale(axis, dot(axis, value) * (1.0F - cosine)));
}

float palstanceStep(float currentAngleRadians, float targetAngleRadians,
                    float elapsedSeconds,
                    float ropeLengthMeters) noexcept {
    if (ropeLengthMeters <= std::numeric_limits<float>::epsilon() ||
        elapsedSeconds <= 0.0F) {
        return 0.0F;
    }
    const float effectiveTarget =
        std::max(targetAngleRadians, currentAngleRadians);
    const float energy = std::max(
        ropeLengthMeters * kNativeSwingAcceleration *
            (std::cos(currentAngleRadians) -
             std::cos(effectiveTarget + 0.1F)),
        0.0F);
    const float angularTravel =
        (std::sqrt(energy) -
         std::sin(currentAngleRadians) * kNativeSwingDamping *
             elapsedSeconds) *
        elapsedSeconds / ropeLengthMeters;
    return std::isfinite(angularTravel) ? angularTravel : 0.0F;
}

} // namespace

Result WebSwingRuntime::start(
    const LevelWebGrabPointAsset& point,
    const assets::Vector3& playerPosition,
    const assets::Vector3&) noexcept {
    if (!std::isfinite(point.length) || point.length <= 0.0F) {
        return Result::failure("Web grab point has no positive rope length");
    }
    point_ = &point;
    position_ = playerPosition;
    velocityCentimetersPerSecond_ = {};
    exitAngleReached_ = false;

    const assets::Vector3 pointToPlayer =
        subtract(point.position, playerPosition);
    assets::Vector3 entryPlaneNormal = cross(pointToPlayer, kDown);
    assets::Vector3 authoredPlaneNormal = cross(point.direction, kDown);
    if (!normalize(entryPlaneNormal)) {
        entryPlaneNormal = {0.0F, 1.0F, 0.0F};
    }
    if (!normalize(authoredPlaneNormal)) {
        authoredPlaneNormal = entryPlaneNormal;
    }
    // StartWebSwing aligns CWebGrabPoint::GetDir's plane to the player's
    // entry side before projecting the rope into that authored plane.
    if (dot(authoredPlaneNormal, entryPlaneNormal) < 0.0F) {
        authoredPlaneNormal = scale(authoredPlaneNormal, -1.0F);
    }
    planeNormal_ = authoredPlaneNormal;

    assets::Vector3 projectedRope = subtract(
        pointToPlayer, scale(planeNormal_, dot(pointToPlayer, planeNormal_)));
    if (!normalize(projectedRope)) {
        projectedRope = kUp;
    }
    const float authoredVerticalAngle = std::clamp(
        point.verticalAngleDegrees * kDegreesToRadians, 0.0F,
        3.14159265358979323846F);
    const float entryAngle = std::min(
        std::acos(std::clamp(dot(projectedRope, kUp), -1.0F, 1.0F)),
        authoredVerticalAngle);
    targetAngleRadians_ = authoredVerticalAngle;
    angleRadians_ = -entryAngle;
    ropeDirection_ = rotateAroundAxis(kUp, planeNormal_, entryAngle);
    normalize(ropeDirection_);

    travelDirection_ = {point.direction.x, point.direction.y, 0.0F};
    if (!normalize(travelDirection_)) {
        travelDirection_ = cross(planeNormal_, kUp);
        if (!normalize(travelDirection_)) {
            travelDirection_ = {1.0F, 0.0F, 0.0F};
        }
    }

    assets::Vector3 rotationAxis = point.direction;
    if (!normalize(rotationAxis)) {
        rotationAxis = travelDirection_;
    }
    assets::Vector3 horizontallyRotatedRope = rotateAroundAxis(
        ropeDirection_, rotationAxis,
        point.horizontalAngleDegrees * kDegreesToRadians);
    normalize(horizontallyRotatedRope);
    const assets::Vector3 initialOrbit = subtract(
        point.position, scale(ropeDirection_, point.length));
    const assets::Vector3 horizontallyOffsetOrbit = subtract(
        point.position, scale(horizontallyRotatedRope, point.length));
    horizontalOffset_ = subtract(horizontallyOffsetOrbit, initialOrbit);
    // UpdateMCSpeed deliberately suppresses the vertical component of the
    // AngleH offset; vertical travel comes only from GetPalstance.
    horizontalOffset_.z = 0.0F;
    catchUpTimeMilliseconds_ =
        finishTimeMilliseconds() * kSwingCatchUpScale;
    return Result::success();
}

void WebSwingRuntime::update(std::uint32_t elapsedMilliseconds) noexcept {
    if (point_ == nullptr || elapsedMilliseconds == 0) {
        return;
    }
    const assets::Vector3 previousPosition = position_;
    float remainingMilliseconds = static_cast<float>(elapsedMilliseconds);
    while (remainingMilliseconds > 0.0F && !exitAngleReached_) {
        const float stepMilliseconds =
            std::min(remainingMilliseconds,
                     kNativeSwingStepSeconds * 1000.0F);
        const float stepSeconds = stepMilliseconds / 1000.0F;
        const float deltaAngle = palstanceStep(
            angleRadians_, targetAngleRadians_, stepSeconds,
            point_->length * 0.01F);
        angleRadians_ += deltaAngle;
        ropeDirection_ = rotateAroundAxis(
            ropeDirection_, planeNormal_, -deltaAngle);
        normalize(ropeDirection_);

        assets::Vector3 desiredPosition;
        refreshDesiredPose(desiredPosition);
        if (catchUpTimeMilliseconds_ > 0.0F) {
            const float blend = stepMilliseconds /
                (stepMilliseconds + catchUpTimeMilliseconds_);
            position_ = add(position_, scale(
                subtract(desiredPosition, position_), blend));
            catchUpTimeMilliseconds_ = std::max(
                catchUpTimeMilliseconds_ - stepMilliseconds, 0.0F);
        } else {
            position_ = desiredPosition;
        }
        if (angleRadians_ > targetAngleRadians_) {
            exitAngleReached_ = true;
        }
        remainingMilliseconds -= stepMilliseconds;
    }
    const float elapsedSeconds =
        static_cast<float>(elapsedMilliseconds) / 1000.0F;
    velocityCentimetersPerSecond_ = scale(
        subtract(position_, previousPosition), 1.0F / elapsedSeconds);
}

WebSwingRelease WebSwingRuntime::release() noexcept {
    WebSwingRelease released;
    if (point_ == nullptr) {
        return released;
    }
    // SetNextStateId motion 28 takes planeNormal x ropeDirection, flattens
    // and normalizes it, then assigns z=1 and normalizes again. The result is
    // a fixed 45-degree upward launch regardless of the release point.
    assets::Vector3 tangent = cross(planeNormal_, ropeDirection_);
    tangent.z = 0.0F;
    if (!normalize(tangent)) {
        tangent = travelDirection_;
    }
    tangent.z = 1.0F;
    normalize(tangent);
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

float WebSwingRuntime::finishTimeMilliseconds() const noexcept {
    if (point_ == nullptr || point_->length <= 0.0F) {
        return 0.0F;
    }
    float simulatedAngle = angleRadians_;
    float elapsedSeconds = 0.0F;
    // The shipped function has no practical long-running path, but retain a
    // finite guard for malformed reconstructed data.
    while (simulatedAngle < targetAngleRadians_ && elapsedSeconds < 60.0F) {
        simulatedAngle += palstanceStep(
            simulatedAngle, targetAngleRadians_, kNativeSwingStepSeconds,
            point_->length * 0.01F);
        if (simulatedAngle < targetAngleRadians_) {
            elapsedSeconds += kNativeSwingStepSeconds;
        }
    }
    return elapsedSeconds * 1000.0F;
}

void WebSwingRuntime::refreshDesiredPose(
    assets::Vector3& desiredPosition) noexcept {
    if (point_ == nullptr) {
        desiredPosition = position_;
        return;
    }
    desiredPosition = add(
        subtract(point_->position, scale(ropeDirection_, point_->length)),
        horizontalOffset_);
}

} // namespace usm::game
