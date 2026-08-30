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

} // namespace

Result GameplayCamera::bind(std::span<const CameraArea> areas,
                            std::int32_t initialAreaId) {
    const auto match = std::find_if(
        areas.begin(), areas.end(), [initialAreaId](const CameraArea& area) {
            return area.objectId == initialAreaId;
        });
    if (match == areas.end()) {
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
    return pose;
}

} // namespace usm::game
