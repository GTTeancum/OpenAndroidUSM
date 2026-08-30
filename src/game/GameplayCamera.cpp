#include "game/GameplayCamera.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace usm::game {
namespace {

using assets::Vector3;

Vector3 add(const Vector3& left, const Vector3& right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

Vector3 subtract(const Vector3& left, const Vector3& right) noexcept {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

Vector3 scale(const Vector3& value, float factor) noexcept {
    return {value.x * factor, value.y * factor, value.z * factor};
}

float dot(const Vector3& left, const Vector3& right) noexcept {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

Vector3 cross(const Vector3& left, const Vector3& right) noexcept {
    return {left.y * right.z - left.z * right.y,
            left.z * right.x - left.x * right.z,
            left.x * right.y - left.y * right.x};
}

float lengthSquared(const Vector3& value) noexcept { return dot(value, value); }

float distance(const Vector3& left, const Vector3& right) noexcept {
    return std::sqrt(lengthSquared(subtract(left, right)));
}

Vector3 normalize(const Vector3& value) noexcept {
    const float length = std::sqrt(lengthSquared(value));
    if (length <= std::numeric_limits<float>::epsilon()) {
        return {};
    }
    return scale(value, 1.0F / length);
}

Vector3 closestPointOnSegment(const Vector3& point, const Vector3& start,
                              const Vector3& end) noexcept {
    const Vector3 edge = subtract(end, start);
    const float edgeLengthSquared = lengthSquared(edge);
    if (edgeLengthSquared <= std::numeric_limits<float>::epsilon()) {
        return start;
    }
    const float factor = std::clamp(
        dot(subtract(point, start), edge) / edgeLengthSquared, 0.0F, 1.0F);
    return add(start, scale(edge, factor));
}

bool pointInTriangle(const Vector3& point, const Vector3& first,
                     const Vector3& second, const Vector3& third) noexcept {
    const Vector3 edge0 = subtract(third, first);
    const Vector3 edge1 = subtract(second, first);
    const Vector3 relative = subtract(point, first);
    const float dot00 = dot(edge0, edge0);
    const float dot01 = dot(edge0, edge1);
    const float dot02 = dot(edge0, relative);
    const float dot11 = dot(edge1, edge1);
    const float dot12 = dot(edge1, relative);
    const float denominator = dot00 * dot11 - dot01 * dot01;
    if (std::abs(denominator) <= std::numeric_limits<float>::epsilon()) {
        return false;
    }
    const float inverse = 1.0F / denominator;
    const float u = (dot11 * dot02 - dot01 * dot12) * inverse;
    const float v = (dot00 * dot12 - dot01 * dot02) * inverse;
    return u >= 0.0F && v >= 0.0F && u + v <= 1.0F;
}

Vector3 projectOnControlPlane(const CameraArea& area,
                              const Vector3& point) noexcept {
    const Vector3& first = area.controlPoints[0].position;
    const Vector3 planeNormal = normalize(cross(
        subtract(area.controlPoints[1].position, first),
        subtract(area.controlPoints[2].position, first)));
    return subtract(point,
                    scale(planeNormal, dot(subtract(point, first), planeNormal)));
}

std::array<float, 4> controlPointWeights(const CameraArea& area,
                                         const Vector3& projected) noexcept {
    std::array<Vector3, 4> closest{};
    std::array<float, 4> edgeDistances{};
    std::size_t nearestEdge = 0;
    for (std::size_t edge = 0; edge < closest.size(); ++edge) {
        const std::size_t next = (edge + 1) % closest.size();
        closest[edge] = closestPointOnSegment(
            projected, area.controlPoints[edge].position,
            area.controlPoints[next].position);
        edgeDistances[edge] = distance(projected, closest[edge]);
        if (edgeDistances[edge] < edgeDistances[nearestEdge]) {
            nearestEdge = edge;
        }
    }

    const bool inside = pointInTriangle(
                            projected, area.controlPoints[0].position,
                            area.controlPoints[1].position,
                            area.controlPoints[2].position) ||
                        pointInTriangle(
                            projected, area.controlPoints[0].position,
                            area.controlPoints[2].position,
                            area.controlPoints[3].position);
    std::array<float, 4> edgeWeights{};
    if (inside &&
        std::all_of(edgeDistances.begin(), edgeDistances.end(),
                    [](float value) {
                        return value > std::numeric_limits<float>::epsilon();
                    })) {
        float reciprocalSum = 0.0F;
        for (float value : edgeDistances) {
            reciprocalSum += 1.0F / value;
        }
        for (std::size_t edge = 0; edge < edgeWeights.size(); ++edge) {
            edgeWeights[edge] = (1.0F / edgeDistances[edge]) / reciprocalSum;
        }
    } else {
        edgeWeights[nearestEdge] = 1.0F;
    }

    std::array<float, 4> result{};
    for (std::size_t edge = 0; edge < edgeWeights.size(); ++edge) {
        const std::size_t next = (edge + 1) % edgeWeights.size();
        const float toCurrent =
            distance(closest[edge], area.controlPoints[edge].position);
        const float toNext =
            distance(closest[edge], area.controlPoints[next].position);
        const float total = toCurrent + toNext;
        const float currentFactor =
            total <= std::numeric_limits<float>::epsilon()
                ? 1.0F
                : toNext / total;
        result[edge] += edgeWeights[edge] * currentFactor;
        result[next] += edgeWeights[edge] * (1.0F - currentFactor);
    }
    return result;
}

bool containsPlayer(const CameraArea& area, const Vector3& player) noexcept {
    if (area.disabled) {
        return false;
    }
    const Vector3 projected = projectOnControlPlane(area, player);
    if (distance(player, projected) > area.height + 1e-3F) {
        return false;
    }
    return pointInTriangle(projected, area.controlPoints[0].position,
                           area.controlPoints[1].position,
                           area.controlPoints[2].position) ||
           pointInTriangle(projected, area.controlPoints[0].position,
                           area.controlPoints[2].position,
                           area.controlPoints[3].position);
}

} // namespace

Result GameplayCamera::bind(std::span<const CameraArea> areas,
                            std::int32_t initialAreaId) {
    areas_.assign(areas.begin(), areas.end());
    transitionDurationMilliseconds_ = 0;
    transitionElapsedMilliseconds_ = 0;
    transitionProgress_ = 1.0F;
    const auto match = std::find_if(
        areas_.begin(), areas_.end(), [initialAreaId](const CameraArea& area) {
            return area.objectId == initialAreaId;
        });
    if (match == areas_.end()) {
        currentArea_ = nullptr;
        return Result::failure("Initial gameplay camera area was not found");
    }
    const Vector3 firstEdge = subtract(match->controlPoints[1].position,
                                       match->controlPoints[0].position);
    const Vector3 secondEdge = subtract(match->controlPoints[2].position,
                                        match->controlPoints[0].position);
    if (lengthSquared(cross(firstEdge, secondEdge)) <=
        std::numeric_limits<float>::epsilon()) {
        currentArea_ = nullptr;
        return Result::failure("Gameplay camera control plane is degenerate");
    }
    currentArea_ = &*match;
    return Result::success();
}

bool GameplayCamera::setAreaEnabled(std::int32_t areaId,
                                    bool enabled) noexcept {
    const auto match = std::find_if(
        areas_.begin(), areas_.end(), [areaId](const CameraArea& area) {
            return area.objectId == areaId;
        });
    if (match == areas_.end()) {
        return false;
    }
    match->disabled = !enabled;
    return true;
}

bool GameplayCamera::isAreaEnabled(std::int32_t areaId) const noexcept {
    const auto match = std::find_if(
        areas_.begin(), areas_.end(), [areaId](const CameraArea& area) {
            return area.objectId == areaId;
        });
    return match != areas_.end() && !match->disabled;
}

bool GameplayCamera::setCurrentArea(std::int32_t areaId) noexcept {
    const auto match = std::find_if(
        areas_.begin(), areas_.end(), [areaId](const CameraArea& area) {
            return area.objectId == areaId;
        });
    if (match == areas_.end()) {
        return false;
    }
    currentArea_ = &*match;
    transitionDurationMilliseconds_ = 0;
    transitionElapsedMilliseconds_ = 0;
    transitionProgress_ = 1.0F;
    return true;
}

bool GameplayCamera::relocateToContainingArea(
    const assets::Vector3& playerPosition) noexcept {
    auto match = std::find_if(
        areas_.begin(), areas_.end(),
        [&playerPosition](const CameraArea& area) {
            return containsPlayer(area, playerPosition);
        });
    if (match == areas_.end()) {
        float closestDistance = std::numeric_limits<float>::max();
        for (auto candidate = areas_.begin(); candidate != areas_.end();
             ++candidate) {
            if (candidate->disabled) {
                continue;
            }
            const Vector3 projected =
                projectOnControlPlane(*candidate, playerPosition);
            float candidateDistance = distance(playerPosition, projected);
            if (!pointInTriangle(projected,
                                 candidate->controlPoints[0].position,
                                 candidate->controlPoints[1].position,
                                 candidate->controlPoints[2].position) &&
                !pointInTriangle(projected,
                                 candidate->controlPoints[0].position,
                                 candidate->controlPoints[2].position,
                                 candidate->controlPoints[3].position)) {
                float edgeDistance = std::numeric_limits<float>::max();
                for (std::size_t edge = 0; edge < 4; ++edge) {
                    edgeDistance = std::min(
                        edgeDistance,
                        distance(projected,
                                 closestPointOnSegment(
                                     projected,
                                     candidate->controlPoints[edge].position,
                                     candidate->controlPoints[(edge + 1) % 4]
                                         .position)));
                }
                candidateDistance = std::hypot(candidateDistance,
                                               edgeDistance);
            }
            if (candidateDistance < closestDistance) {
                closestDistance = candidateDistance;
                match = candidate;
            }
        }
        if (match == areas_.end()) {
            return false;
        }
    }
    currentArea_ = &*match;
    transitionDurationMilliseconds_ = 0;
    transitionElapsedMilliseconds_ = 0;
    transitionProgress_ = 1.0F;
    return true;
}

const std::array<bool, 16>& GameplayCamera::mustInvisibleRooms() const
    noexcept {
    static constexpr std::array<bool, 16> kNoRooms{};
    return currentArea_ == nullptr ? kNoRooms
                                   : currentArea_->mustInvisibleRooms;
}

const std::array<bool, 16>& GameplayCamera::mustVisibleRooms() const noexcept {
    static constexpr std::array<bool, 16> kNoRooms{};
    return currentArea_ == nullptr ? kNoRooms : currentArea_->mustVisibleRooms;
}

bool GameplayCamera::updateArea(
    const assets::Vector3& playerPosition,
    std::uint32_t elapsedMilliseconds) noexcept {
    if (currentArea_ == nullptr || containsPlayer(*currentArea_, playerPosition)) {
        advanceTransition(elapsedMilliseconds);
        return false;
    }
    for (std::size_t index = 0; index < currentArea_->nextAreaIds.size();
         ++index) {
        const std::int32_t neighborId = currentArea_->nextAreaIds[index];
        const auto neighbor = std::find_if(
            areas_.begin(), areas_.end(), [neighborId](const CameraArea& area) {
                return area.objectId == neighborId;
            });
        if (neighbor == areas_.end() ||
            !containsPlayer(*neighbor, playerPosition)) {
            continue;
        }
        transitionStartPose_ = sample(playerPosition);
        transitionDurationMilliseconds_ =
            currentArea_->switchTimeUnits[index] * 50U;
        transitionElapsedMilliseconds_ = 0;
        transitionProgress_ =
            transitionDurationMilliseconds_ == 0 ? 1.0F : 0.0F;
        currentArea_ = &*neighbor;
        advanceTransition(elapsedMilliseconds);
        return true;
    }
    advanceTransition(elapsedMilliseconds);
    return false;
}

void GameplayCamera::advanceTransition(
    std::uint32_t elapsedMilliseconds) noexcept {
    if (transitionProgress_ >= 1.0F ||
        transitionDurationMilliseconds_ == 0) {
        transitionProgress_ = 1.0F;
        return;
    }
    transitionElapsedMilliseconds_ = std::min(
        transitionDurationMilliseconds_,
        transitionElapsedMilliseconds_ +
            std::min(elapsedMilliseconds,
                     transitionDurationMilliseconds_ -
                         transitionElapsedMilliseconds_));
    const float time =
        static_cast<float>(transitionElapsedMilliseconds_) /
        static_cast<float>(transitionDurationMilliseconds_);
    // CameraAreaSwitcher::UpdateTimer (0x002f5894) accelerates for the first
    // half and decelerates for the second half with 4 / duration^2.
    transitionProgress_ = time <= 0.5F
                              ? 2.0F * time * time
                              : 1.0F - 2.0F * (1.0F - time) * (1.0F - time);
}

CameraPose GameplayCamera::sample(
    const assets::Vector3& playerPosition) const noexcept {
    CameraPose pose;
    if (currentArea_ == nullptr) {
        return pose;
    }
    const Vector3 projected =
        projectOnControlPlane(*currentArea_, playerPosition);
    const auto weights = controlPointWeights(*currentArea_, projected);

    Vector3 direction{};
    Vector3 target = projected;
    float cameraDistance = 0.0F;
    float targetHeightOffset = 0.0F;
    for (std::size_t index = 0; index < weights.size(); ++index) {
        const CameraControlPoint& control =
            currentArea_->controlPoints[index];
        direction = add(direction, scale(control.direction, weights[index]));
        target = add(target, scale(control.targetOffset, weights[index]));
        cameraDistance += control.distance * weights[index];
        targetHeightOffset += control.targetHeightOffset * weights[index];
    }
    direction = normalize(direction);
    target.z += 120.0F + targetHeightOffset;

    pose.target = target;
    pose.position = subtract(target, scale(direction, cameraDistance));
    pose.up = {0.0F, 0.0F, 1.0F};
    pose.verticalFieldOfViewDegrees = 45.0F;
    pose.nearPlane = 1.0F;
    pose.farPlane = 10000.0F + currentArea_->farPlaneOffset;
    if (transitionProgress_ < 1.0F) {
        const float progress = transitionProgress_;
        const float remaining = 1.0F - progress;
        const Vector3 startDirection = normalize(subtract(
            transitionStartPose_.target, transitionStartPose_.position));
        const float startDistance = distance(transitionStartPose_.target,
                                             transitionStartPose_.position);
        const Vector3 finalDirection = direction;
        const Vector3 blendedDirection = normalize(add(
            scale(startDirection, remaining), scale(finalDirection, progress)));
        pose.target = add(scale(transitionStartPose_.target, remaining),
                          scale(pose.target, progress));
        const float blendedDistance =
            startDistance * remaining + cameraDistance * progress;
        pose.position =
            subtract(pose.target, scale(blendedDirection, blendedDistance));
    }
    return pose;
}

} // namespace usm::game
