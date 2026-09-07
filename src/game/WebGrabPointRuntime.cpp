#include "game/WebGrabPointRuntime.hpp"

#include "game/LevelCollision.hpp"

#include <cmath>
#include <limits>
#include <optional>

namespace usm::game {
namespace {

// DAT_00344638, compared against the normalized point direction by
// Player::GetBestWebGrabPoint (0x00344424).
constexpr float kMinimumFacingDot = 0.2F;
// Player::GetRadius (0x0033fdf8) returns 50 cm. The recovered visibility
// segment starts half a radius above the player's scene position.
constexpr float kPlayerRadiusCentimeters = 50.0F;
// SearchWebGrabPoint (0x0034486c) passes these literal radii to
// GetBestWebGrabPoint and GetClosestWebGrabPoint respectively.
constexpr float kBestSearchRadiusCentimeters = 3000.0F;
constexpr float kHintSearchRadiusCentimeters = 6000.0F;
// Player::GetBestWebGrabPoint passes -33000 to CLevel::SegmentCollision.
// Physics::processCollision interprets it as an ignored-surface mask, which
// excludes the authored jump/climb/edge helper faces (0x10/0x20/0x40) while
// retaining ordinary ground and wall occlusion.
constexpr std::uint32_t kWebGrabIgnoredPhysicsFlags = 0xffff7f18U;

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
    roomVisibility_ = {};
    hasViewContext_ = false;
}

void WebGrabPointRuntime::setViewContext(
    const CameraPose& camera, float aspectRatio,
    std::span<const bool> roomVisibility) noexcept {
    camera_ = camera;
    aspectRatio_ = aspectRatio;
    roomVisibility_ = roomVisibility;
    hasViewContext_ = aspectRatio > 0.0F;
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
            !inNativeCandidateSet(playerPosition, point,
                                  kBestSearchRadiusCentimeters) ||
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
            !inNativeCandidateSet(playerPosition, point,
                                  kHintSearchRadiusCentimeters) ||
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

std::vector<WebGrabCandidateDiagnostics> WebGrabPointRuntime::diagnose(
    const assets::Vector3& playerPosition,
    const assets::Vector3& playerFacing,
    std::int32_t currentPointId) const {
    std::vector<WebGrabCandidateDiagnostics> result;
    result.reserve(points_.size());
    const float facingLengthSquared = lengthSquared(playerFacing);
    const float inverseFacingLength =
        facingLengthSquared <= std::numeric_limits<float>::epsilon()
            ? 0.0F
            : 1.0F / std::sqrt(facingLengthSquared);
    for (const LevelWebGrabPointAsset& point : points_) {
        WebGrabCandidateDiagnostics candidate;
        candidate.objectId = point.objectId;
        candidate.visibleLength = point.visibleLength;
        candidate.currentPoint = point.objectId == currentPointId;
        assets::Vector3 segmentStart = playerPosition;
        segmentStart.z += kPlayerRadiusCentimeters * 0.5F;
        const std::optional<LevelSegmentHit> blockingHit =
            collision_ == nullptr
                ? std::nullopt
                : collision_->segmentFirstHit(
                      segmentStart, point.position,
                      kWebGrabIgnoredPhysicsFlags);
        candidate.lineOfSight = !blockingHit.has_value();
        if (blockingHit) {
            candidate.blockingRoomId = blockingHit->roomId;
            candidate.blockingPhysicsFlags = blockingHit->physicsFlags;
            candidate.blockingFraction = blockingHit->segmentFraction;
            candidate.blockingPosition = blockingHit->position;
            candidate.blockingGeometry = blockingHit->geometryName;
            candidate.blockingMaterial = blockingHit->materialName;
        }
        const assets::Vector3 toPoint = subtract(point.position,
                                                 playerPosition);
        const float distanceSquared = lengthSquared(toPoint);
        candidate.distance = std::sqrt(std::max(0.0F, distanceSquared));
        if (inverseFacingLength > 0.0F &&
            candidate.distance > std::numeric_limits<float>::epsilon()) {
            candidate.facingDot =
                dot(playerFacing, toPoint) * inverseFacingLength /
                candidate.distance;
        }
        candidate.facingAccepted = candidate.facingDot > kMinimumFacingDot;
        candidate.withinVisibleLength =
            point.visibleLength <= 0.0F ||
            distanceSquared <= point.visibleLength * point.visibleLength;
        candidate.withinBestSearchRadius =
            distanceSquared < kBestSearchRadiusCentimeters *
                                  kBestSearchRadiusCentimeters;
        candidate.withinHintSearchRadius =
            distanceSquared < kHintSearchRadiusCentimeters *
                                  kHintSearchRadiusCentimeters;
        if (hasViewContext_) {
            candidate.onScreen =
                isPointInScreen(camera_, aspectRatio_, point.position);
            if (!roomVisibility_.empty()) {
                candidate.roomVisible =
                    point.roomId >= 1 &&
                    static_cast<std::size_t>(point.roomId) <=
                        roomVisibility_.size() &&
                    roomVisibility_[static_cast<std::size_t>(point.roomId - 1)];
            }
        }
        result.push_back(candidate);
    }
    return result;
}

bool WebGrabPointRuntime::inNativeCandidateSet(
    const assets::Vector3& playerPosition,
    const LevelWebGrabPointAsset& point,
    float searchRadius) const noexcept {
    if (lengthSquared(subtract(point.position, playerPosition)) >=
        searchRadius * searchRadius) {
        return false;
    }
    if (!hasViewContext_) {
        return true;
    }
    if (!roomVisibility_.empty() &&
        (point.roomId < 1 ||
         static_cast<std::size_t>(point.roomId) > roomVisibility_.size() ||
         !roomVisibility_[static_cast<std::size_t>(point.roomId - 1)])) {
        return false;
    }
    return isPointInScreen(camera_, aspectRatio_, point.position);
}

bool WebGrabPointRuntime::hasLineOfSight(
    const assets::Vector3& playerPosition,
    const LevelWebGrabPointAsset& point) const noexcept {
    if (collision_ == nullptr) {
        return true;
    }
    assets::Vector3 segmentStart = playerPosition;
    segmentStart.z += kPlayerRadiusCentimeters * 0.5F;
    return !collision_->segmentBlocked(segmentStart, point.position,
                                       kWebGrabIgnoredPhysicsFlags);
}

} // namespace usm::game
