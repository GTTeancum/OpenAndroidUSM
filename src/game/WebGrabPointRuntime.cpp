#include "game/WebGrabPointRuntime.hpp"

#include "game/LevelCollision.hpp"

#include <cmath>
#include <limits>

namespace usm::game {
namespace {

// DAT_00344638, compared against the normalized point direction by
// Player::GetBestWebGrabPoint (0x00344424).
constexpr float kMinimumFacingDot = 0.2F;
// Player::GetRadius (0x0033fdf8) returns 50 cm. The recovered visibility
// segment starts half a radius above the player's scene position.
constexpr float kPlayerRadiusCentimeters = 50.0F;

assets::Vector3 subtract(const assets::Vector3& left,
                         const assets::Vector3& right) noexcept {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

float lengthSquared(const assets::Vector3& value) noexcept {
    return value.x * value.x + value.y * value.y + value.z * value.z;
}

float dot(const assets::Vector3& left,
          const assets::Vector3& right) noexcept {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

} // namespace

void WebGrabPointRuntime::bind(
    std::span<const LevelWebGrabPointAsset> points,
    const LevelCollision* collision) noexcept {
    points_ = points;
    collision_ = collision;
}

const LevelWebGrabPointAsset* WebGrabPointRuntime::findBest(
    const assets::Vector3& playerPosition,
    const assets::Vector3& playerFacing,
    std::int32_t currentPointId) const noexcept {
    const float facingLengthSquared = lengthSquared(playerFacing);
    if (facingLengthSquared <= std::numeric_limits<float>::epsilon()) {
        return nullptr;
    }
    const float inverseFacingLength = 1.0F / std::sqrt(facingLengthSquared);
    const assets::Vector3 normalizedFacing{
        playerFacing.x * inverseFacingLength,
        playerFacing.y * inverseFacingLength,
        playerFacing.z * inverseFacingLength,
    };

    const LevelWebGrabPointAsset* best = nullptr;
    float bestInverseDistance = 0.0F;
    for (const LevelWebGrabPointAsset& point : points_) {
        if (point.objectId == currentPointId ||
            !hasLineOfSight(playerPosition, point)) {
            continue;
        }
        const assets::Vector3 toPoint = subtract(point.position,
                                                 playerPosition);
        const float distanceSquared = lengthSquared(toPoint);
        if (distanceSquared <= std::numeric_limits<float>::epsilon()) {
            continue;
        }
        const float inverseDistance = 1.0F / std::sqrt(distanceSquared);
        const assets::Vector3 normalizedDirection{
            toPoint.x * inverseDistance,
            toPoint.y * inverseDistance,
            toPoint.z * inverseDistance,
        };
        if (dot(normalizedFacing, normalizedDirection) <=
                kMinimumFacingDot ||
            inverseDistance <= bestInverseDistance) {
            continue;
        }
        best = &point;
        bestInverseDistance = inverseDistance;
    }
    return best;
}

const LevelWebGrabPointAsset* WebGrabPointRuntime::findClosestVisible(
    const assets::Vector3& playerPosition,
    std::int32_t currentPointId) const noexcept {
    const LevelWebGrabPointAsset* closest = nullptr;
    float closestDistanceSquared = std::numeric_limits<float>::max();
    for (const LevelWebGrabPointAsset& point : points_) {
        if (point.objectId == currentPointId ||
            !hasLineOfSight(playerPosition, point)) {
            continue;
        }
        const float candidateDistanceSquared =
            lengthSquared(subtract(point.position, playerPosition));
        if (candidateDistanceSquared >= closestDistanceSquared) {
            continue;
        }
        closest = &point;
        closestDistanceSquared = candidateDistanceSquared;
    }
    return closest;
}

const LevelWebGrabPointAsset* WebGrabPointRuntime::search(
    const assets::Vector3& playerPosition,
    const assets::Vector3& playerFacing,
    std::int32_t currentPointId) const noexcept {
    const LevelWebGrabPointAsset* best =
        findBest(playerPosition, playerFacing, currentPointId);
    if (best == nullptr || best->visibleLength <= 0.0F) {
        return best;
    }
    const float distanceSquared =
        lengthSquared(subtract(best->position, playerPosition));
    return distanceSquared <= best->visibleLength * best->visibleLength
               ? best
               : nullptr;
}

bool WebGrabPointRuntime::hasLineOfSight(
    const assets::Vector3& playerPosition,
    const LevelWebGrabPointAsset& point) const noexcept {
    if (collision_ == nullptr) {
        return true;
    }
    assets::Vector3 segmentStart = playerPosition;
    segmentStart.z += kPlayerRadiusCentimeters * 0.5F;
    return !collision_->segmentBlocked(segmentStart, point.position);
}

} // namespace usm::game
