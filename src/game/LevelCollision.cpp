#include "game/LevelCollision.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace usm::game {
namespace {

using assets::ColladaGeometry;
using assets::ColladaMeshBuffer;
using assets::ColladaPrimitive;
using assets::Vector3;

constexpr float kGridCellSize = 500.0F;
// Player::GetRadius (0x0033fdf8) returns the preserved 50 cm `consts` value
// at image address 0x0056ec90.
constexpr float kGroundSupportRadius = 50.0F;
constexpr float kPlayerCollisionHeight = 140.0F;

Vector3 subtract(const Vector3& left, const Vector3& right) noexcept {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

Vector3 cross(const Vector3& left, const Vector3& right) noexcept {
    return {left.y * right.z - left.z * right.y,
            left.z * right.x - left.x * right.z,
            left.x * right.y - left.y * right.x};
}

float length(const Vector3& value) noexcept {
    return std::sqrt(value.x * value.x + value.y * value.y +
                     value.z * value.z);
}

float dot(const Vector3& left, const Vector3& right) noexcept {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

float pointSegmentDistanceSquared(float pointX, float pointY,
                                  const Vector3& start,
                                  const Vector3& end) noexcept {
    const float edgeX = end.x - start.x;
    const float edgeY = end.y - start.y;
    const float edgeLengthSquared = edgeX * edgeX + edgeY * edgeY;
    const float factor =
        edgeLengthSquared <= std::numeric_limits<float>::epsilon()
            ? 0.0F
            : std::clamp(((pointX - start.x) * edgeX +
                          (pointY - start.y) * edgeY) /
                             edgeLengthSquared,
                         0.0F, 1.0F);
    const float differenceX = pointX - (start.x + edgeX * factor);
    const float differenceY = pointY - (start.y + edgeY * factor);
    return differenceX * differenceX + differenceY * differenceY;
}

std::int32_t cellCoordinate(float value) noexcept {
    return static_cast<std::int32_t>(std::floor(value / kGridCellSize));
}

} // namespace

Result LevelCollision::build(std::span<const LevelRoomAsset> rooms) {
    triangles_.clear();
    grid_.clear();
    broadTriangles_.clear();
    for (const LevelRoomAsset& room : rooms) {
        append(room.collision.sceneGeometries());
    }
    if (triangles_.empty()) {
        return Result::failure("Level collision contains no triangles");
    }
    rebuildGrid();
    return Result::success();
}

Result LevelCollision::build(
    std::span<const assets::ColladaGeometry> geometries) {
    triangles_.clear();
    grid_.clear();
    broadTriangles_.clear();
    append(geometries);
    if (triangles_.empty()) {
        return Result::failure("Collision geometry contains no triangles");
    }
    rebuildGrid();
    return Result::success();
}

void LevelCollision::append(std::span<const ColladaGeometry> geometries) {
    auto appendTriangle = [this](const ColladaGeometry& geometry,
                                 const ColladaMeshBuffer& buffer,
                                 std::uint16_t firstIndex,
                                 std::uint16_t secondIndex,
                                 std::uint16_t thirdIndex) {
        if (firstIndex >= geometry.vertices.size() ||
            secondIndex >= geometry.vertices.size() ||
            thirdIndex >= geometry.vertices.size()) {
            return;
        }
        Triangle triangle;
        triangle.first = geometry.vertices[firstIndex].position;
        triangle.second = geometry.vertices[secondIndex].position;
        triangle.third = geometry.vertices[thirdIndex].position;
        const auto finite = [](const Vector3& value) {
            return std::isfinite(value.x) && std::isfinite(value.y) &&
                   std::isfinite(value.z);
        };
        if (!finite(triangle.first) || !finite(triangle.second) ||
            !finite(triangle.third)) {
            return;
        }
        triangle.normal = cross(subtract(triangle.second, triangle.first),
                                subtract(triangle.third, triangle.first));
        const float normalLength = length(triangle.normal);
        if (normalLength <= std::numeric_limits<float>::epsilon()) {
            return;
        }
        triangle.normal.x /= normalLength;
        triangle.normal.y /= normalLength;
        triangle.normal.z /= normalLength;
        triangle.minimumX = std::min(
            {triangle.first.x, triangle.second.x, triangle.third.x});
        triangle.maximumX = std::max(
            {triangle.first.x, triangle.second.x, triangle.third.x});
        triangle.minimumY = std::min(
            {triangle.first.y, triangle.second.y, triangle.third.y});
        triangle.maximumY = std::max(
            {triangle.first.y, triangle.second.y, triangle.third.y});
        triangle.minimumZ = std::min(
            {triangle.first.z, triangle.second.z, triangle.third.z});
        triangle.maximumZ = std::max(
            {triangle.first.z, triangle.second.z, triangle.third.z});
        triangle.geometryName = geometry.name;
        triangle.materialName = buffer.materialName;
        // PhysicsTriangleMeshShape::addSceneNodeInternal (0x003d9e94)
        // derives the native triangle flags from collision-node prefixes.
        // constructMesh (0x003d95d8) applies the regular wall flag only to
        // faces below its 0.70710677 ground-normal threshold.
        const bool vertical =
            std::abs(triangle.normal.z) <
            LevelCollisionConstants::MinimumGroundNormalZ;
        if (geometry.name.starts_with("wall")) {
            triangle.physicsFlags = vertical
                                        ? LevelPhysicsFlags::ClimbableWall
                                        : LevelPhysicsFlags::Ground;
        } else if (geometry.name.starts_with("jump_wall")) {
            triangle.physicsFlags = LevelPhysicsFlags::JumpWall;
        } else if (geometry.name.starts_with("edge_wall")) {
            triangle.physicsFlags = LevelPhysicsFlags::ClimbableEdge;
        } else {
            triangle.physicsFlags = vertical ? LevelPhysicsFlags::Wall
                                             : LevelPhysicsFlags::Ground;
        }
        triangles_.push_back(triangle);
    };

    for (const ColladaGeometry& geometry : geometries) {
        for (const ColladaMeshBuffer& buffer : geometry.meshBuffers) {
            if (buffer.primitive == ColladaPrimitive::Triangles) {
                for (std::size_t index = 0; index + 2 < buffer.indices.size();
                     index += 3) {
                    appendTriangle(geometry, buffer, buffer.indices[index],
                                   buffer.indices[index + 1],
                                   buffer.indices[index + 2]);
                }
            } else if (buffer.primitive == ColladaPrimitive::TriangleStrip) {
                for (std::size_t index = 2; index < buffer.indices.size();
                     ++index) {
                    const bool odd = (index & 1U) != 0;
                    appendTriangle(geometry, buffer,
                                   buffer.indices[index - (odd ? 0 : 2)],
                                   buffer.indices[index - 1],
                                   buffer.indices[index - (odd ? 2 : 0)]);
                }
            }
        }
    }
}

void LevelCollision::rebuildGrid() {
    for (std::uint32_t index = 0; index < triangles_.size(); ++index) {
        const Triangle& triangle = triangles_[index];
        const std::int32_t minimumCellX = cellCoordinate(triangle.minimumX);
        const std::int32_t maximumCellX = cellCoordinate(triangle.maximumX);
        const std::int32_t minimumCellY = cellCoordinate(triangle.minimumY);
        const std::int32_t maximumCellY = cellCoordinate(triangle.maximumY);
        const std::int64_t cellWidth =
            static_cast<std::int64_t>(maximumCellX) - minimumCellX + 1;
        const std::int64_t cellHeight =
            static_cast<std::int64_t>(maximumCellY) - minimumCellY + 1;
        if (cellWidth <= 0 || cellHeight <= 0 ||
            cellWidth * cellHeight > 4096) {
            broadTriangles_.push_back(index);
            continue;
        }
        for (std::int64_t x = minimumCellX; x <= maximumCellX; ++x) {
            for (std::int64_t y = minimumCellY; y <= maximumCellY; ++y) {
                grid_[cellKey(static_cast<std::int32_t>(x),
                              static_cast<std::int32_t>(y))]
                    .push_back(index);
            }
        }
    }
}

std::int64_t LevelCollision::cellKey(std::int32_t x,
                                     std::int32_t y) noexcept {
    return static_cast<std::int64_t>(
        (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32U) |
        static_cast<std::uint32_t>(y));
}

bool LevelCollision::groundHeight(const Vector3& reference,
                                  float maximumStepUp, float maximumDrop,
                                  float& height,
                                  std::uint32_t ignoredPhysicsFlags)
    const noexcept {
    bool found = false;
    float best = -std::numeric_limits<float>::infinity();
    const auto considerTriangle = [&](std::uint32_t triangleIndex) {
        const Triangle& triangle = triangles_[triangleIndex];
        if ((triangle.physicsFlags & ignoredPhysicsFlags) != 0U ||
            std::abs(triangle.normal.z) <
                LevelCollisionConstants::MinimumGroundNormalZ ||
            reference.x < triangle.minimumX - kGroundSupportRadius ||
            reference.x > triangle.maximumX + kGroundSupportRadius ||
            reference.y < triangle.minimumY - kGroundSupportRadius ||
            reference.y > triangle.maximumY + kGroundSupportRadius) {
            return;
        }
        const float denominator =
            (triangle.second.y - triangle.third.y) *
                (triangle.first.x - triangle.third.x) +
            (triangle.third.x - triangle.second.x) *
                (triangle.first.y - triangle.third.y);
        if (std::abs(denominator) <= std::numeric_limits<float>::epsilon()) {
            return;
        }
        const float firstWeight =
            ((triangle.second.y - triangle.third.y) *
                 (reference.x - triangle.third.x) +
             (triangle.third.x - triangle.second.x) *
                 (reference.y - triangle.third.y)) /
            denominator;
        const float secondWeight =
            ((triangle.third.y - triangle.first.y) *
                 (reference.x - triangle.third.x) +
             (triangle.first.x - triangle.third.x) *
                 (reference.y - triangle.third.y)) /
            denominator;
        const float thirdWeight = 1.0F - firstWeight - secondWeight;
        if (firstWeight < -1e-4F || secondWeight < -1e-4F ||
            thirdWeight < -1e-4F) {
            const float distanceSquared = std::min(
                {pointSegmentDistanceSquared(reference.x, reference.y,
                                             triangle.first, triangle.second),
                 pointSegmentDistanceSquared(reference.x, reference.y,
                                             triangle.second, triangle.third),
                 pointSegmentDistanceSquared(reference.x, reference.y,
                                             triangle.third, triangle.first)});
            if (distanceSquared >
                kGroundSupportRadius * kGroundSupportRadius) {
                return;
            }
        }
        const float candidate =
            triangle.first.z -
            (triangle.normal.x * (reference.x - triangle.first.x) +
             triangle.normal.y * (reference.y - triangle.first.y)) /
                triangle.normal.z;
        if (candidate > reference.z + maximumStepUp ||
            candidate < reference.z - maximumDrop || candidate <= best) {
            return;
        }
        best = candidate;
        found = true;
    };
    const std::int32_t centerCellX = cellCoordinate(reference.x);
    const std::int32_t centerCellY = cellCoordinate(reference.y);
    for (std::int32_t offsetX = -1; offsetX <= 1; ++offsetX) {
        for (std::int32_t offsetY = -1; offsetY <= 1; ++offsetY) {
            const auto cell = grid_.find(
                cellKey(centerCellX + offsetX, centerCellY + offsetY));
            if (cell == grid_.end()) {
                continue;
            }
            for (std::uint32_t triangleIndex : cell->second) {
                considerTriangle(triangleIndex);
            }
        }
    }
    for (std::uint32_t triangleIndex : broadTriangles_) {
        considerTriangle(triangleIndex);
    }
    if (found) {
        height = best;
    }
    return found;
}

bool LevelCollision::resolveGroundMotion(const Vector3& start,
                                         const Vector3& desired,
                                         Vector3& resolved,
                                         float maximumStepUp,
                                         float maximumDrop,
                                         std::uint32_t ignoredPhysicsFlags)
    const noexcept {
    Vector3 wallResolved = desired;
    if (std::abs(desired.x - start.x) > 1e-4F ||
        std::abs(desired.y - start.y) > 1e-4F) {
        resolveWalls(start, wallResolved, ignoredPhysicsFlags);
    }
    float height = 0.0F;
    if (groundHeight(wallResolved, maximumStepUp, maximumDrop, height,
                     ignoredPhysicsFlags)) {
        resolved = {wallResolved.x, wallResolved.y, height};
        return true;
    }
    Vector3 slide = {wallResolved.x, start.y, start.z};
    if (groundHeight(slide, maximumStepUp, maximumDrop, height,
                     ignoredPhysicsFlags)) {
        resolved = {slide.x, slide.y, height};
        return true;
    }
    slide = {start.x, wallResolved.y, start.z};
    if (groundHeight(slide, maximumStepUp, maximumDrop, height,
                     ignoredPhysicsFlags)) {
        resolved = {slide.x, slide.y, height};
        return true;
    }
    resolved = start;
    if (groundHeight(start, maximumStepUp, maximumDrop, height,
                     ignoredPhysicsFlags)) {
        resolved.z = height;
    }
    return false;
}

void LevelCollision::resolveAirMotion(const Vector3& start,
                                      const Vector3& desired,
                                      Vector3& resolved,
                                      std::uint32_t ignoredPhysicsFlags)
    const noexcept {
    resolved = desired;
    if (std::abs(desired.x - start.x) > 1e-4F ||
        std::abs(desired.y - start.y) > 1e-4F) {
        resolveWalls(start, resolved, ignoredPhysicsFlags);
    }
}

bool LevelCollision::segmentBlocked(const Vector3& start,
                                    const Vector3& end) const noexcept {
    const Vector3 direction = subtract(end, start);
    constexpr float kIntersectionEpsilon = 1e-5F;
    for (const Triangle& triangle : triangles_) {
        if (std::max(start.x, end.x) < triangle.minimumX ||
            std::min(start.x, end.x) > triangle.maximumX ||
            std::max(start.y, end.y) < triangle.minimumY ||
            std::min(start.y, end.y) > triangle.maximumY ||
            std::max(start.z, end.z) < triangle.minimumZ ||
            std::min(start.z, end.z) > triangle.maximumZ) {
            continue;
        }
        const Vector3 firstEdge = subtract(triangle.second, triangle.first);
        const Vector3 secondEdge = subtract(triangle.third, triangle.first);
        const Vector3 determinantCross = cross(direction, secondEdge);
        const float determinant = dot(firstEdge, determinantCross);
        if (std::abs(determinant) <= kIntersectionEpsilon) {
            continue;
        }
        const float inverseDeterminant = 1.0F / determinant;
        const Vector3 fromFirst = subtract(start, triangle.first);
        const float firstWeight =
            dot(fromFirst, determinantCross) * inverseDeterminant;
        if (firstWeight < 0.0F || firstWeight > 1.0F) {
            continue;
        }
        const Vector3 secondCross = cross(fromFirst, firstEdge);
        const float secondWeight =
            dot(direction, secondCross) * inverseDeterminant;
        if (secondWeight < 0.0F ||
            firstWeight + secondWeight > 1.0F) {
            continue;
        }
        const float segmentTime =
            dot(secondEdge, secondCross) * inverseDeterminant;
        // Endpoints are omitted so standing on collision geometry or attaching
        // to a point placed directly on it does not self-occlude.
        if (segmentTime > kIntersectionEpsilon &&
            segmentTime < 1.0F - kIntersectionEpsilon) {
            return true;
        }
    }
    return false;
}

bool LevelCollision::climbableWallContact(
    const Vector3& start, const Vector3& end,
    LevelWallContact& contact) const noexcept {
    // Player::CheckClimbableWall (0x0034863c) obtains both the contact point
    // and manifold normal from a forward segment/contact query. The portable
    // collision mesh has no Bullet manifold, so retain the nearest vertical
    // triangle intersection explicitly.
    const Vector3 direction = subtract(end, start);
    constexpr float kIntersectionEpsilon = 1e-5F;
    bool found = false;
    float nearestFraction = 1.0F + kIntersectionEpsilon;
    for (const Triangle& triangle : triangles_) {
        if (triangle.physicsFlags != LevelPhysicsFlags::ClimbableWall ||
            std::abs(triangle.normal.z) >=
                LevelCollisionConstants::MinimumGroundNormalZ ||
            std::max(start.x, end.x) < triangle.minimumX ||
            std::min(start.x, end.x) > triangle.maximumX ||
            std::max(start.y, end.y) < triangle.minimumY ||
            std::min(start.y, end.y) > triangle.maximumY ||
            std::max(start.z, end.z) < triangle.minimumZ ||
            std::min(start.z, end.z) > triangle.maximumZ) {
            continue;
        }
        const Vector3 firstEdge = subtract(triangle.second, triangle.first);
        const Vector3 secondEdge = subtract(triangle.third, triangle.first);
        const Vector3 determinantCross = cross(direction, secondEdge);
        const float determinant = dot(firstEdge, determinantCross);
        if (std::abs(determinant) <= kIntersectionEpsilon) {
            continue;
        }
        const float inverseDeterminant = 1.0F / determinant;
        const Vector3 fromFirst = subtract(start, triangle.first);
        const float firstWeight =
            dot(fromFirst, determinantCross) * inverseDeterminant;
        if (firstWeight < 0.0F || firstWeight > 1.0F) {
            continue;
        }
        const Vector3 secondCross = cross(fromFirst, firstEdge);
        const float secondWeight =
            dot(direction, secondCross) * inverseDeterminant;
        if (secondWeight < 0.0F || firstWeight + secondWeight > 1.0F) {
            continue;
        }
        const float fraction =
            dot(secondEdge, secondCross) * inverseDeterminant;
        if (fraction < -kIntersectionEpsilon ||
            fraction > 1.0F + kIntersectionEpsilon ||
            fraction >= nearestFraction) {
            continue;
        }
        nearestFraction = std::clamp(fraction, 0.0F, 1.0F);
        contact.position = {start.x + direction.x * nearestFraction,
                            start.y + direction.y * nearestFraction,
                            start.z + direction.z * nearestFraction};
        contact.normal = triangle.normal;
        if (dot(contact.normal, direction) > 0.0F) {
            contact.normal.x = -contact.normal.x;
            contact.normal.y = -contact.normal.y;
            contact.normal.z = -contact.normal.z;
        }
        contact.segmentFraction = nearestFraction;
        contact.physicsFlags = triangle.physicsFlags;
        contact.geometryName = triangle.geometryName;
        contact.materialName = triangle.materialName;
        found = true;
    }
    return found;
}

bool LevelCollision::climbableEdgeContact(
    const Vector3& capsuleBase, LevelWallContact& contact) const noexcept {
    return horizontalSurfaceContact(capsuleBase,
                                    LevelPhysicsFlags::ClimbableEdge,
                                    contact);
}

bool LevelCollision::jumpWallContact(
    const Vector3& capsuleBase, LevelWallContact& contact) const noexcept {
    return horizontalSurfaceContact(capsuleBase, LevelPhysicsFlags::JumpWall,
                                    contact);
}

bool LevelCollision::horizontalSurfaceContact(
    const Vector3& capsuleBase, std::uint32_t physicsFlags,
    LevelWallContact& contact) const noexcept {
    // CheckClimbableWall(2) (0x0034863c) consumes the player's active 0x40
    // manifold contact. Jump-wall slabs use the same native manifold path
    // with flag 0x10. Model both with the 50 cm support radius and 140 cm
    // player height used by the portable body solver.
    bool found = false;
    float nearestVerticalDistance = std::numeric_limits<float>::infinity();
    for (const Triangle& triangle : triangles_) {
        if (triangle.physicsFlags != physicsFlags ||
            std::abs(triangle.normal.z) <
                LevelCollisionConstants::MinimumGroundNormalZ ||
            capsuleBase.x < triangle.minimumX - kGroundSupportRadius ||
            capsuleBase.x > triangle.maximumX + kGroundSupportRadius ||
            capsuleBase.y < triangle.minimumY - kGroundSupportRadius ||
            capsuleBase.y > triangle.maximumY + kGroundSupportRadius) {
            continue;
        }
        const float denominator =
            (triangle.second.y - triangle.third.y) *
                (triangle.first.x - triangle.third.x) +
            (triangle.third.x - triangle.second.x) *
                (triangle.first.y - triangle.third.y);
        if (std::abs(denominator) <= std::numeric_limits<float>::epsilon()) {
            continue;
        }
        const float firstWeight =
            ((triangle.second.y - triangle.third.y) *
                 (capsuleBase.x - triangle.third.x) +
             (triangle.third.x - triangle.second.x) *
                 (capsuleBase.y - triangle.third.y)) /
            denominator;
        const float secondWeight =
            ((triangle.third.y - triangle.first.y) *
                 (capsuleBase.x - triangle.third.x) +
             (triangle.first.x - triangle.third.x) *
                 (capsuleBase.y - triangle.third.y)) /
            denominator;
        const float thirdWeight = 1.0F - firstWeight - secondWeight;
        if (firstWeight < -1e-4F || secondWeight < -1e-4F ||
            thirdWeight < -1e-4F) {
            const float distanceSquared = std::min(
                {pointSegmentDistanceSquared(capsuleBase.x, capsuleBase.y,
                                             triangle.first,
                                             triangle.second),
                 pointSegmentDistanceSquared(capsuleBase.x, capsuleBase.y,
                                             triangle.second,
                                             triangle.third),
                 pointSegmentDistanceSquared(capsuleBase.x, capsuleBase.y,
                                             triangle.third,
                                             triangle.first)});
            if (distanceSquared >
                kGroundSupportRadius * kGroundSupportRadius) {
                continue;
            }
        }
        const float edgeHeight =
            triangle.first.z -
            (triangle.normal.x * (capsuleBase.x - triangle.first.x) +
             triangle.normal.y * (capsuleBase.y - triangle.first.y)) /
                triangle.normal.z;
        if (edgeHeight < capsuleBase.z - 1.0F ||
            edgeHeight > capsuleBase.z + kPlayerCollisionHeight + 1.0F) {
            continue;
        }
        const float verticalDistance = std::abs(edgeHeight - capsuleBase.z);
        if (verticalDistance >= nearestVerticalDistance) {
            continue;
        }
        nearestVerticalDistance = verticalDistance;
        contact.position = {capsuleBase.x, capsuleBase.y, edgeHeight};
        contact.normal = triangle.normal;
        if (contact.normal.z < 0.0F) {
            contact.normal.x = -contact.normal.x;
            contact.normal.y = -contact.normal.y;
            contact.normal.z = -contact.normal.z;
        }
        contact.segmentFraction = 0.0F;
        contact.physicsFlags = triangle.physicsFlags;
        contact.geometryName = triangle.geometryName;
        contact.materialName = triangle.materialName;
        found = true;
    }
    return found;
}

void LevelCollision::resolveWalls(const Vector3& start,
                                  Vector3& desired,
                                  std::uint32_t ignoredPhysicsFlags)
    const noexcept {
    std::vector<std::uint32_t> candidates = broadTriangles_;
    const std::int32_t centerCellX = cellCoordinate(desired.x);
    const std::int32_t centerCellY = cellCoordinate(desired.y);
    for (std::int32_t offsetX = -1; offsetX <= 1; ++offsetX) {
        for (std::int32_t offsetY = -1; offsetY <= 1; ++offsetY) {
            const auto cell = grid_.find(
                cellKey(centerCellX + offsetX, centerCellY + offsetY));
            if (cell != grid_.end()) {
                candidates.insert(candidates.end(), cell->second.begin(),
                                  cell->second.end());
            }
        }
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()),
                     candidates.end());

    for (int iteration = 0; iteration < 4; ++iteration) {
        bool corrected = false;
        for (std::uint32_t triangleIndex : candidates) {
            const Triangle& triangle = triangles_[triangleIndex];
            if ((triangle.physicsFlags & ignoredPhysicsFlags) != 0U ||
                std::abs(triangle.normal.z) >=
                    LevelCollisionConstants::MinimumGroundNormalZ ||
                triangle.maximumZ < start.z ||
                triangle.minimumZ > start.z + kPlayerCollisionHeight) {
                continue;
            }
            const std::array<std::pair<const Vector3*, const Vector3*>, 3>
                edges{{{&triangle.first, &triangle.second},
                       {&triangle.second, &triangle.third},
                       {&triangle.third, &triangle.first}}};
            const auto longest = std::max_element(
                edges.begin(), edges.end(), [](const auto& left,
                                               const auto& right) {
                    const float leftX = left.second->x - left.first->x;
                    const float leftY = left.second->y - left.first->y;
                    const float rightX = right.second->x - right.first->x;
                    const float rightY = right.second->y - right.first->y;
                    return leftX * leftX + leftY * leftY <
                           rightX * rightX + rightY * rightY;
                });
            const float edgeX = longest->second->x - longest->first->x;
            const float edgeY = longest->second->y - longest->first->y;
            const float edgeLengthSquared = edgeX * edgeX + edgeY * edgeY;
            if (edgeLengthSquared <=
                std::numeric_limits<float>::epsilon()) {
                continue;
            }
            const float edgeLength = std::sqrt(edgeLengthSquared);
            const float normalX = -edgeY / edgeLength;
            const float normalY = edgeX / edgeLength;
            const float projection =
                ((desired.x - longest->first->x) * edgeX +
                 (desired.y - longest->first->y) * edgeY) /
                edgeLengthSquared;
            if (projection < 0.0F || projection > 1.0F) {
                continue;
            }
            const float closestX = longest->first->x + projection * edgeX;
            const float closestY = longest->first->y + projection * edgeY;
            const float desiredSide =
                (desired.x - closestX) * normalX +
                (desired.y - closestY) * normalY;
            const float startSide =
                (start.x - closestX) * normalX +
                (start.y - closestY) * normalY;
            if (startSide * desiredSide >= 0.0F &&
                std::abs(desiredSide) >= kGroundSupportRadius) {
                continue;
            }
            const float sideSign = startSide < 0.0F ? -1.0F : 1.0F;
            const float correction =
                sideSign * kGroundSupportRadius - desiredSide;
            desired.x += normalX * correction;
            desired.y += normalY * correction;
            corrected = true;
        }
        if (!corrected) {
            break;
        }
    }
}

} // namespace usm::game
