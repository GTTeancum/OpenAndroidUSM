#include "game/LevelCollision.hpp"

#include "game/LevelObjectRuntime.hpp"
#include "game/PlayerPhysicsConstants.hpp"

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
constexpr float kGroundSupportRadius = kPlayerCollisionRadiusCentimeters;

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

Vector3 closestPointOnTriangle(const Vector3& point, const Vector3& first,
                               const Vector3& second,
                               const Vector3& third) noexcept {
    // processSphereTriangle (0x003d23cc) first projects onto the authored
    // face and otherwise tests all three edges. This Voronoi-region form
    // returns the same closest face/edge/vertex point without choosing a
    // dominant projection axis.
    const Vector3 firstEdge = subtract(second, first);
    const Vector3 secondEdge = subtract(third, first);
    const Vector3 firstOffset = subtract(point, first);
    const float firstDot = dot(firstEdge, firstOffset);
    const float secondDot = dot(secondEdge, firstOffset);
    if (firstDot <= 0.0F && secondDot <= 0.0F) {
        return first;
    }

    const Vector3 secondOffset = subtract(point, second);
    const float thirdDot = dot(firstEdge, secondOffset);
    const float fourthDot = dot(secondEdge, secondOffset);
    if (thirdDot >= 0.0F && fourthDot <= thirdDot) {
        return second;
    }

    const float firstRegion = firstDot * fourthDot - thirdDot * secondDot;
    if (firstRegion <= 0.0F && firstDot >= 0.0F && thirdDot <= 0.0F) {
        const float factor = firstDot / (firstDot - thirdDot);
        return {first.x + factor * firstEdge.x,
                first.y + factor * firstEdge.y,
                first.z + factor * firstEdge.z};
    }

    const Vector3 thirdOffset = subtract(point, third);
    const float fifthDot = dot(firstEdge, thirdOffset);
    const float sixthDot = dot(secondEdge, thirdOffset);
    if (sixthDot >= 0.0F && fifthDot <= sixthDot) {
        return third;
    }

    const float secondRegion = fifthDot * secondDot - firstDot * sixthDot;
    if (secondRegion <= 0.0F && secondDot >= 0.0F && sixthDot <= 0.0F) {
        const float factor = secondDot / (secondDot - sixthDot);
        return {first.x + factor * secondEdge.x,
                first.y + factor * secondEdge.y,
                first.z + factor * secondEdge.z};
    }

    const float thirdRegion =
        thirdDot * sixthDot - fifthDot * fourthDot;
    if (thirdRegion <= 0.0F && (fourthDot - thirdDot) >= 0.0F &&
        (fifthDot - sixthDot) >= 0.0F) {
        const Vector3 edge = subtract(third, second);
        const float factor = (fourthDot - thirdDot) /
                             ((fourthDot - thirdDot) +
                              (fifthDot - sixthDot));
        return {second.x + factor * edge.x,
                second.y + factor * edge.y,
                second.z + factor * edge.z};
    }

    const float denominator =
        1.0F / (firstRegion + secondRegion + thirdRegion);
    const float secondWeight = secondRegion * denominator;
    // The final face-region barycentrics are v=vb/(va+vb+vc) and
    // w=vc/(va+vb+vc). firstRegion is vc; thirdRegion is va. Using va for w
    // displaced interior contacts toward the BC edge and did not match
    // processSphereTriangle's projected face contact (0x003d23cc).
    const float thirdWeight = firstRegion * denominator;
    return {first.x + firstEdge.x * secondWeight +
                secondEdge.x * thirdWeight,
            first.y + firstEdge.y * secondWeight +
                secondEdge.y * thirdWeight,
            first.z + firstEdge.z * secondWeight +
                secondEdge.z * thirdWeight};
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
    staticTriangleCount_ = 0;
    roomPositions_.clear();
    grid_.clear();
    broadTriangles_.clear();
    dynamicObjectSnapshots_.clear();
    roomPositions_.reserve(rooms.size());
    for (std::size_t roomIndex = 0; roomIndex < rooms.size(); ++roomIndex) {
        const LevelRoomAsset& room = rooms[roomIndex];
        roomPositions_.push_back(room.position);
        append(room.collision.sceneGeometries(),
               static_cast<std::int32_t>(roomIndex + 1), room.position);
    }
    if (triangles_.empty()) {
        return Result::failure("Level collision contains no triangles");
    }
    staticTriangleCount_ = triangles_.size();
    rebuildGrid();
    return Result::success();
}

Result LevelCollision::build(
    std::span<const assets::ColladaGeometry> geometries) {
    triangles_.clear();
    staticTriangleCount_ = 0;
    roomPositions_.clear();
    grid_.clear();
    broadTriangles_.clear();
    dynamicObjectSnapshots_.clear();
    append(geometries);
    if (triangles_.empty()) {
        return Result::failure("Collision geometry contains no triangles");
    }
    staticTriangleCount_ = triangles_.size();
    rebuildGrid();
    return Result::success();
}

void LevelCollision::appendObjectBox(const LevelObjectState& object) {
    if (object.asset == nullptr || !object.asset->hasCollisionBounds) {
        return;
    }
    const assets::Vector3& minimum = object.asset->collisionLocalMinimum;
    const assets::Vector3& maximum = object.asset->collisionLocalMaximum;
    const auto transform = [&object](const assets::Vector3& local) {
        const auto& matrix = object.worldTransform;
        return assets::Vector3{
            local.x * matrix[0] + local.y * matrix[4] +
                local.z * matrix[8] + matrix[12],
            local.x * matrix[1] + local.y * matrix[5] +
                local.z * matrix[9] + matrix[13],
            local.x * matrix[2] + local.y * matrix[6] +
                local.z * matrix[10] + matrix[14]};
    };
    const std::array<assets::Vector3, 8> corners{{
        transform({minimum.x, minimum.y, minimum.z}),
        transform({maximum.x, minimum.y, minimum.z}),
        transform({minimum.x, maximum.y, minimum.z}),
        transform({maximum.x, maximum.y, minimum.z}),
        transform({minimum.x, minimum.y, maximum.z}),
        transform({maximum.x, minimum.y, maximum.z}),
        transform({minimum.x, maximum.y, maximum.z}),
        transform({maximum.x, maximum.y, maximum.z}),
    }};
    const auto appendTriangle = [this, &corners, &object](
                                    std::size_t firstIndex,
                                    std::size_t secondIndex,
                                    std::size_t thirdIndex,
                                    std::uint32_t forcedPhysicsFlags = 0U) {
        Triangle triangle;
        triangle.first = corners[firstIndex];
        triangle.second = corners[secondIndex];
        triangle.third = corners[thirdIndex];
        triangle.localFirst = triangle.first;
        triangle.localSecond = triangle.second;
        triangle.localThird = triangle.third;
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
        triangle.geometryName = object.asset->name + "/bbox";
        triangle.materialName = "transmission_physics";
        triangle.physicsFlags = forcedPhysicsFlags != 0U
                                    ? forcedPhysicsFlags
                                    : triangle.normal.z <=
                                              LevelCollisionConstants::
                                                  MinimumGroundNormalZ
                                          ? LevelPhysicsFlags::Wall
                                          : LevelPhysicsFlags::Ground;
        triangle.roomId = -1;
        triangle.objectId = object.asset->objectId;
        triangles_.push_back(std::move(triangle));
    };

    if (object.asset->kind == LevelObjectKind::Platform ||
        object.asset->kind == LevelObjectKind::ElectricPlatform) {
        // platform_phy.bdae is not a closed box: `_11` is the upward-wound
        // support plane at local minimum Z and the four tall side nodes are
        // named jump_wall_East/South/West/North. Airborne player states mask
        // JumpWall (0x10), allowing the jump arc onto the platform, while
        // ordinary ground motion remains fenced by those authored sides.
        appendTriangle(0, 3, 2, LevelPhysicsFlags::Ground);
        appendTriangle(0, 1, 3, LevelPhysicsFlags::Ground);
        appendTriangle(0, 4, 6, LevelPhysicsFlags::JumpWall);
        appendTriangle(0, 6, 2, LevelPhysicsFlags::JumpWall);
        appendTriangle(1, 3, 7, LevelPhysicsFlags::JumpWall);
        appendTriangle(1, 7, 5, LevelPhysicsFlags::JumpWall);
        appendTriangle(0, 1, 5, LevelPhysicsFlags::JumpWall);
        appendTriangle(0, 5, 4, LevelPhysicsFlags::JumpWall);
        appendTriangle(2, 6, 7, LevelPhysicsFlags::JumpWall);
        appendTriangle(2, 7, 3, LevelPhysicsFlags::JumpWall);
        return;
    }

    // Outward-wound faces for the transformed authored AABB.
    appendTriangle(0, 2, 3);
    appendTriangle(0, 3, 1);
    appendTriangle(4, 5, 7);
    appendTriangle(4, 7, 6);
    appendTriangle(0, 4, 6);
    appendTriangle(0, 6, 2);
    appendTriangle(1, 3, 7);
    appendTriangle(1, 7, 5);
    appendTriangle(0, 1, 5);
    appendTriangle(0, 5, 4);
    appendTriangle(2, 6, 7);
    appendTriangle(2, 7, 3);
}

void LevelCollision::appendSpiderWebWall(const LevelObjectState& object) {
    if (object.asset == nullptr || object.archetype == nullptr ||
        !object.asset->hasCollisionBounds) {
        return;
    }
    const auto transform = [&object](const assets::Vector3& local) {
        const auto& matrix = object.worldTransform;
        return assets::Vector3{
            local.x * matrix[0] + local.y * matrix[4] +
                local.z * matrix[8] + matrix[12],
            local.x * matrix[1] + local.y * matrix[5] +
                local.z * matrix[9] + matrix[13],
            local.x * matrix[2] + local.y * matrix[6] +
                local.z * matrix[10] + matrix[14]};
    };
    const auto appendTriangle = [this, &object, &transform](
                                    const ColladaGeometry& geometry,
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
        triangle.localFirst = geometry.vertices[firstIndex].position;
        triangle.localSecond = geometry.vertices[secondIndex].position;
        triangle.localThird = geometry.vertices[thirdIndex].position;
        triangle.first = transform(triangle.localFirst);
        triangle.second = transform(triangle.localSecond);
        triangle.third = transform(triangle.localThird);
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
        triangle.geometryName = object.asset->name + "/" + geometry.name;
        triangle.materialName = buffer.materialName;
        // CSpiderWebWall::Init (0x0031ee04) calls setFlagsAll(..., 6)
        // after creating the exact bbox triangle mesh. Native 0x06 is the
        // ordinary wall flag combined with two-sided collision.
        triangle.physicsFlags =
            LevelPhysicsFlags::Wall | LevelPhysicsFlags::DoubleSided;
        triangle.roomId = -1;
        triangle.objectId = object.asset->objectId;
        triangles_.push_back(std::move(triangle));
    };

    for (const ColladaGeometry& geometry :
         object.archetype->mesh.sceneGeometries()) {
        if (geometry.name != "bbox") {
            continue;
        }
        for (const ColladaMeshBuffer& buffer : geometry.meshBuffers) {
            if (buffer.primitive == ColladaPrimitive::Triangles) {
                for (std::size_t index = 0;
                     index + 2 < buffer.indices.size(); index += 3) {
                    appendTriangle(geometry, buffer, buffer.indices[index],
                                   buffer.indices[index + 1],
                                   buffer.indices[index + 2]);
                }
            } else if (buffer.primitive ==
                       ColladaPrimitive::TriangleStrip) {
                for (std::size_t index = 2; index < buffer.indices.size();
                     ++index) {
                    const bool odd = (index & 1U) != 0;
                    appendTriangle(
                        geometry, buffer,
                        buffer.indices[index - (odd ? 0 : 2)],
                        buffer.indices[index - 1],
                        buffer.indices[index - (odd ? 2 : 0)]);
                }
            }
        }
    }
}

Result LevelCollision::updateObjectColliders(
    std::span<const LevelObjectState> objects) {
    std::vector<DynamicObjectSnapshot> snapshots;
    for (const LevelObjectState& object : objects) {
        if (object.asset == nullptr ||
            (object.asset->kind != LevelObjectKind::SpiderWebWall &&
             object.asset->kind != LevelObjectKind::SlideCar &&
             object.asset->kind != LevelObjectKind::BrokenBridge &&
             object.asset->kind != LevelObjectKind::Platform &&
             object.asset->kind != LevelObjectKind::ElectricPlatform) ||
            !object.asset->hasCollisionBounds || !object.visible ||
            !object.physicsEnabled || !object.collisionEnabled) {
            continue;
        }
        snapshots.push_back(
            {object.asset->objectId, object.worldTransform});
    }
    if (snapshots == dynamicObjectSnapshots_) {
        return Result::success();
    }
    if (staticTriangleCount_ > triangles_.size()) {
        return Result::failure(
            "Dynamic collision static triangle boundary is invalid");
    }
    triangles_.resize(staticTriangleCount_);
    for (const LevelObjectState& object : objects) {
        if (object.asset == nullptr ||
            (object.asset->kind != LevelObjectKind::SpiderWebWall &&
             object.asset->kind != LevelObjectKind::SlideCar &&
             object.asset->kind != LevelObjectKind::BrokenBridge &&
             object.asset->kind != LevelObjectKind::Platform &&
             object.asset->kind != LevelObjectKind::ElectricPlatform) ||
            !object.asset->hasCollisionBounds || !object.visible ||
            !object.physicsEnabled || !object.collisionEnabled) {
            continue;
        }
        if (object.asset->kind == LevelObjectKind::SpiderWebWall) {
            appendSpiderWebWall(object);
        } else {
            appendObjectBox(object);
        }
    }
    dynamicObjectSnapshots_ = std::move(snapshots);
    rebuildGrid();
    return Result::success();
}

void LevelCollision::append(std::span<const ColladaGeometry> geometries,
                            std::int32_t roomId,
                            const assets::Vector3& roomPosition) {
    auto appendTriangle = [this, roomId, &roomPosition](
                              const ColladaGeometry& geometry,
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
        triangle.localFirst = geometry.vertices[firstIndex].position;
        triangle.localSecond = geometry.vertices[secondIndex].position;
        triangle.localThird = geometry.vertices[thirdIndex].position;
        triangle.first = {triangle.localFirst.x + roomPosition.x,
                          triangle.localFirst.y + roomPosition.y,
                          triangle.localFirst.z + roomPosition.z};
        triangle.second = {triangle.localSecond.x + roomPosition.x,
                           triangle.localSecond.y + roomPosition.y,
                           triangle.localSecond.z + roomPosition.z};
        triangle.third = {triangle.localThird.x + roomPosition.x,
                          triangle.localThird.y + roomPosition.y,
                          triangle.localThird.z + roomPosition.z};
        triangle.roomId = roomId;
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
        // constructMesh (0x003d95d8) uses the SIGNED dot product with +Z.
        // Downward-facing ceilings are not ground, even when horizontal.
        const bool vertical =
            triangle.normal.z <=
            LevelCollisionConstants::MinimumGroundNormalZ;
        if (geometry.name.starts_with("wall")) {
            triangle.physicsFlags = vertical
                                        ? LevelPhysicsFlags::ClimbableWall
                                        : LevelPhysicsFlags::Ground;
        } else if (geometry.name.starts_with("jump_wall")) {
            triangle.physicsFlags = LevelPhysicsFlags::JumpWall;
        } else if (geometry.name.starts_with("edge_wall")) {
            triangle.physicsFlags = LevelPhysicsFlags::ClimbableEdge;
        } else if (geometry.name.starts_with("double")) {
            // NODE_NAME_PREFIX_DOUBLE_SIDE at 0x0056f53c -> "double".
            triangle.physicsFlags = (vertical ? LevelPhysicsFlags::Wall
                                              : LevelPhysicsFlags::Ground) |
                                    LevelPhysicsFlags::DoubleSided;
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
    grid_.clear();
    broadTriangles_.clear();
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

Result LevelCollision::updateRoomPositions(
    std::span<const RoomMotionState> rooms) {
    if (rooms.size() != roomPositions_.size()) {
        return Result::failure("Collision room runtime count is invalid");
    }
    bool changed = false;
    for (std::size_t index = 0; index < rooms.size(); ++index) {
        if (rooms[index].roomId != static_cast<std::int32_t>(index + 1)) {
            return Result::failure("Collision room runtime order is invalid");
        }
        const assets::Vector3& position = rooms[index].position;
        assets::Vector3& previous = roomPositions_[index];
        if (position.x == previous.x && position.y == previous.y &&
            position.z == previous.z) {
            continue;
        }
        previous = position;
        changed = true;
    }
    if (!changed) {
        return Result::success();
    }
    for (Triangle& triangle : triangles_) {
        if (triangle.roomId < 1 ||
            triangle.roomId > static_cast<std::int32_t>(rooms.size())) {
            continue;
        }
        const assets::Vector3& position =
            rooms[static_cast<std::size_t>(triangle.roomId - 1)].position;
        triangle.first = {triangle.localFirst.x + position.x,
                          triangle.localFirst.y + position.y,
                          triangle.localFirst.z + position.z};
        triangle.second = {triangle.localSecond.x + position.x,
                           triangle.localSecond.y + position.y,
                           triangle.localSecond.z + position.z};
        triangle.third = {triangle.localThird.x + position.x,
                          triangle.localThird.y + position.y,
                          triangle.localThird.z + position.z};
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
    }
    rebuildGrid();
    return Result::success();
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
                                  std::uint32_t ignoredPhysicsFlags,
                                  std::int32_t* supportingObjectId)
    const noexcept {
    bool found = false;
    float best = -std::numeric_limits<float>::infinity();
    std::int32_t bestObjectId = -1;
    const auto considerTriangle = [&](std::uint32_t triangleIndex) {
        const Triangle& triangle = triangles_[triangleIndex];
        const float supportNormal =
            (triangle.physicsFlags & LevelPhysicsFlags::DoubleSided) != 0U
                ? std::abs(triangle.normal.z) : triangle.normal.z;
        if ((triangle.physicsFlags & ignoredPhysicsFlags) != 0U ||
            supportNormal <=
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
        bestObjectId = triangle.objectId;
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
        if (supportingObjectId != nullptr) {
            *supportingObjectId = bestObjectId;
        }
    } else if (supportingObjectId != nullptr) {
        *supportingObjectId = -1;
    }
    return found;
}

bool LevelCollision::resolveGroundMotion(const Vector3& start,
                                         const Vector3& desired,
                                         Vector3& resolved,
                                         float maximumStepUp,
                                         float maximumDrop,
                                         std::uint32_t ignoredPhysicsFlags,
                                         LevelCollisionDepenetration depenetration)
    const noexcept {
    Vector3 wallResolved = desired;
    if (std::abs(desired.x - start.x) > 1e-4F ||
        std::abs(desired.y - start.y) > 1e-4F) {
        resolveWalls(start, wallResolved, ignoredPhysicsFlags, depenetration);
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
                                      std::uint32_t ignoredPhysicsFlags,
                                      LevelCollisionDepenetration depenetration)
    const noexcept {
    // Most native callers update a PhysicsEntity in place. Preserve the
    // sweep origin before writing the output so an in-place portable call
    // cannot turn a crossing test into a zero-length, same-side test.
    const Vector3 sweepStart = start;
    resolved = desired;
    if (std::abs(desired.x - sweepStart.x) > 1e-4F ||
        std::abs(desired.y - sweepStart.y) > 1e-4F) {
        resolveWalls(sweepStart, resolved, ignoredPhysicsFlags,
                     depenetration);
    }
}

bool LevelCollision::segmentBlocked(const Vector3& start,
                                    const Vector3& end,
                                    std::uint32_t ignoredPhysicsFlags) const
    noexcept {
    return segmentFirstHit(start, end, ignoredPhysicsFlags).has_value();
}

std::optional<LevelSegmentHit> LevelCollision::segmentFirstHit(
    const Vector3& start, const Vector3& end,
    std::uint32_t ignoredPhysicsFlags) const noexcept {
    const Vector3 direction = subtract(end, start);
    constexpr float kIntersectionEpsilon = 1e-5F;
    const Triangle* closestTriangle = nullptr;
    float closestTime = 1.0F;
    for (const Triangle& triangle : triangles_) {
        if ((triangle.physicsFlags & ignoredPhysicsFlags) != 0U) {
            continue;
        }
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
        // PhysicsTriangleMeshShape::constructMesh (0x003d95d8) preserves
        // authored winding. Ordinary mesh triangles are one-sided; only the
        // NODE_NAME_PREFIX_DOUBLE_SIDE path enables the reverse face. This
        // matters for room transition hulls: a web-grab ray leaving through
        // their back face must not be occluded by that hull.
        const bool doubleSided =
            (triangle.physicsFlags & LevelPhysicsFlags::DoubleSided) != 0U;
        if ((!doubleSided && determinant <= kIntersectionEpsilon) ||
            (doubleSided &&
             std::abs(determinant) <= kIntersectionEpsilon)) {
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
            segmentTime < 1.0F - kIntersectionEpsilon &&
            segmentTime < closestTime) {
            closestTriangle = &triangle;
            closestTime = segmentTime;
        }
    }
    if (closestTriangle == nullptr) {
        return std::nullopt;
    }
    return LevelSegmentHit{
        {start.x + direction.x * closestTime,
         start.y + direction.y * closestTime,
         start.z + direction.z * closestTime},
        closestTriangle->normal,
        closestTime,
        closestTriangle->physicsFlags,
        closestTriangle->roomId,
        closestTriangle->objectId,
        closestTriangle->geometryName,
        closestTriangle->materialName};
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

bool LevelCollision::ordinaryWallContact(
    const Vector3& capsuleBase, LevelWallContact& contact) const noexcept {
    // PhysicsTriangleMeshShape::constructMesh (0x003d95d8) assigns flag 2
    // to ordinary non-ground faces. The dynamic Spider-Man body is created
    // as a 50 cm lower sphere plus a cylinder (createSpridemanPhysics,
    // 0x003d8a34); PhysicsEntity::getContextContactPoint (0x003485f0)
    // exposes the persistent flag-2 manifold point consumed by
    // Player::SetNextStateId at 0x0034ac3e-0x0034acc8. Recover the same
    // lower-sphere contact from the portable authored triangle world.
    const Vector3 sphereCenter{
        capsuleBase.x, capsuleBase.y,
        capsuleBase.z + kGroundSupportRadius};
    constexpr float kContactTolerance = 0.01F;
    const float maximumDistance = kGroundSupportRadius + kContactTolerance;
    const float maximumDistanceSquared = maximumDistance * maximumDistance;
    float nearestDistanceSquared = std::numeric_limits<float>::infinity();
    bool found = false;
    for (const Triangle& triangle : triangles_) {
        if ((triangle.physicsFlags & LevelPhysicsFlags::Wall) == 0U ||
            triangle.normal.z >=
                LevelCollisionConstants::MinimumGroundNormalZ ||
            sphereCenter.x + maximumDistance < triangle.minimumX ||
            sphereCenter.x - maximumDistance > triangle.maximumX ||
            sphereCenter.y + maximumDistance < triangle.minimumY ||
            sphereCenter.y - maximumDistance > triangle.maximumY ||
            sphereCenter.z + maximumDistance < triangle.minimumZ ||
            sphereCenter.z - maximumDistance > triangle.maximumZ) {
            continue;
        }
        const bool doubleSided =
            (triangle.physicsFlags & LevelPhysicsFlags::DoubleSided) != 0U;
        const float authoredSide =
            dot(subtract(sphereCenter, triangle.first), triangle.normal);
        if (!doubleSided && authoredSide < -kContactTolerance) {
            continue;
        }
        const Vector3 closest = closestPointOnTriangle(
            sphereCenter, triangle.first, triangle.second, triangle.third);
        const Vector3 separation = subtract(sphereCenter, closest);
        const float distanceSquared = dot(separation, separation);
        if (distanceSquared > maximumDistanceSquared ||
            distanceSquared >= nearestDistanceSquared) {
            continue;
        }
        nearestDistanceSquared = distanceSquared;
        contact.position = closest;
        contact.normal = triangle.normal;
        if (dot(contact.normal, separation) < 0.0F) {
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

bool LevelCollision::horizontalSurfaceContact(
    const Vector3& capsuleBase, std::uint32_t physicsFlags,
    LevelWallContact& contact) const noexcept {
    // CheckClimbableWall(2) (0x0034863c) consumes the player's active 0x40
    // manifold contact. Jump-wall slabs use the same native manifold path
    // with flag 0x10. Model both with the 50 cm support radius and 185 cm
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
            edgeHeight > capsuleBase.z +
                             kPlayerCollisionHeightCentimeters + 1.0F) {
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
                                  std::uint32_t ignoredPhysicsFlags,
                                  LevelCollisionDepenetration depenetration)
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
                triangle.normal.z >=
                    LevelCollisionConstants::MinimumGroundNormalZ) {
                continue;
            }

            const Vector3 startSphereCenter{
                start.x, start.y, start.z + kGroundSupportRadius};
            const float startPlaneSide =
                dot(subtract(startSphereCenter, triangle.first),
                    triangle.normal);
            const bool doubleSided =
                (triangle.physicsFlags & LevelPhysicsFlags::DoubleSided) != 0U;
            // PhysicsTriangleMeshShape::constructMesh (0x003d95d8) retains
            // winding and processSphereTriangle (0x003d23cc) distinguishes
            // ordinary faces from the explicit DoubleSide flag. A body
            // already behind an ordinary face may leave through that back;
            // treating all wall triangles as two-sided creates authored
            // one-way faces as invisible walls.
            if (!doubleSided && startPlaneSide < -1e-4F) {
                continue;
            }

            // createSpridemanPhysics (0x003d8a34) installs a lower sphere
            // centered 50 cm above the Unit origin with a 50 cm radius.
            // CapsuleTriangleMeshCollisionAlgorithm::processCollision
            // (0x003ce6e4) delegates that sphere to processSphereTriangle
            // (0x003d23cc), which resolves against the closest point on the
            // authored face or any of its edges. TManifoldPoint::refresh
            // (0x003d3a1c) gives a dynamic hero the full static-mesh
            // penetration vector. Reproduce that rounded contact: treating
            // the separate 185 cm gameplay height as a flat collision prism
            // traps the native sphere on tiny rooftop lips.
            Vector3 sphereCenter{desired.x, desired.y,
                                 desired.z + kGroundSupportRadius};
            if (sphereCenter.x + kGroundSupportRadius >= triangle.minimumX &&
                sphereCenter.x - kGroundSupportRadius <= triangle.maximumX &&
                sphereCenter.y + kGroundSupportRadius >= triangle.minimumY &&
                sphereCenter.y - kGroundSupportRadius <= triangle.maximumY &&
                sphereCenter.z + kGroundSupportRadius >= triangle.minimumZ &&
                sphereCenter.z - kGroundSupportRadius <= triangle.maximumZ) {
                const Vector3 closest = closestPointOnTriangle(
                    sphereCenter, triangle.first, triangle.second,
                    triangle.third);
                Vector3 separation = subtract(sphereCenter, closest);
                const float separationSquared = dot(separation, separation);
                const float radiusSquared =
                    kGroundSupportRadius * kGroundSupportRadius;
                if (separationSquared < radiusSquared) {
                    const float desiredSide =
                        dot(subtract(sphereCenter, triangle.first),
                            triangle.normal);
                    const float separationLength =
                        std::sqrt(separationSquared);
                    float penetration =
                        kGroundSupportRadius - separationLength;
                    if (!doubleSided && desiredSide < 0.0F &&
                        triangle.maximumZ >= sphereCenter.z) {
                        // A front-side sweep that penetrates a tall face must
                        // remain on its authored-normal side. The native
                        // persistent manifold catches the crossing before the
                        // sphere center changes sides; restore that continuous
                        // result for portable fixed steps.
                        separation = triangle.normal;
                        penetration = kGroundSupportRadius - desiredSide;
                    } else if (depenetration ==
                            LevelCollisionDepenetration::TowardAuthoredNormal &&
                        startPlaneSide < 0.0F &&
                        std::abs(startPlaneSide) < kGroundSupportRadius &&
                        desiredSide > startPlaneSide) {
                        separation = triangle.normal;
                    } else if (separationLength > 1e-5F) {
                        separation.x /= separationLength;
                        separation.y /= separationLength;
                        separation.z /= separationLength;
                    } else {
                        const float side =
                            desiredSide < 0.0F ? -1.0F : 1.0F;
                        separation = {triangle.normal.x * side,
                                      triangle.normal.y * side,
                                      triangle.normal.z * side};
                    }
                    desired.x += separation.x * penetration;
                    desired.y += separation.y * penetration;
                    desired.z += separation.z * penetration;
                    corrected = true;
                    continue;
                }
            }
            const float horizontalNormalLength =
                std::hypot(triangle.normal.x, triangle.normal.y);
            if (horizontalNormalLength <=
                std::numeric_limits<float>::epsilon()) {
                continue;
            }
            // PhysicsTriangleMeshShape::addSceneNodeInternal (0x003d9e94)
            // preserves the authored collision-mesh plane and winding. Use
            // that plane directly. Approximating it from the longest XY edge
            // is incorrect for a vertical triangle whose third vertex is
            // offset at the top: in Room 1 that shifted the storefront shell
            // by roughly 20 cm and let an airborne thug cross the road edge.
            const float normalX = triangle.normal.x / horizontalNormalLength;
            const float normalY = triangle.normal.y / horizontalNormalLength;
            const float tangentX = -normalY;
            const float tangentY = normalX;
            const auto tangentProjection = [&](const Vector3& point) {
                return (point.x - triangle.first.x) * tangentX +
                       (point.y - triangle.first.y) * tangentY;
            };
            const float desiredProjection =
                (desired.x - triangle.first.x) * tangentX +
                (desired.y - triangle.first.y) * tangentY;
            const float secondProjection = tangentProjection(triangle.second);
            const float thirdProjection = tangentProjection(triangle.third);
            const float minimumProjection =
                std::min({0.0F, secondProjection, thirdProjection});
            const float maximumProjection =
                std::max({0.0F, secondProjection, thirdProjection});
            if (desiredProjection < minimumProjection ||
                desiredProjection > maximumProjection) {
                continue;
            }
            const float desiredSide =
                (desired.x - triangle.first.x) * normalX +
                (desired.y - triangle.first.y) * normalY;
            const float startSide =
                (start.x - triangle.first.x) * normalX +
                (start.y - triangle.first.y) * normalY;
            // A vertical face ending below both lower-sphere centers is a
            // ledge, not a full-height barrier. Intermediate native manifold
            // contacts push the sphere upward around that top edge; a
            // continuous flat-plane fallback would erase that rounded
            // response when a portable timestep ends beyond the face.
            const float startSphereCenterZ =
                start.z + kGroundSupportRadius;
            const float desiredSphereCenterZ =
                desired.z + kGroundSupportRadius;
            if (triangle.maximumZ <
                std::min(startSphereCenterZ, desiredSphereCenterZ)) {
                continue;
            }
            const float startSphereMinimumZ = start.z;
            const float startSphereMaximumZ =
                start.z + 2.0F * kGroundSupportRadius;
            const float desiredSphereMinimumZ = desired.z;
            const float desiredSphereMaximumZ =
                desired.z + 2.0F * kGroundSupportRadius;
            if (std::max(startSphereMaximumZ, desiredSphereMaximumZ) <
                    triangle.minimumZ ||
                std::min(startSphereMinimumZ, desiredSphereMinimumZ) >
                    triangle.maximumZ) {
                continue;
            }
            if (startSide * desiredSide >= 0.0F &&
                std::abs(desiredSide) >= kGroundSupportRadius) {
                continue;
            }
            float sideSign = startSide < 0.0F ? -1.0F : 1.0F;
            // An authored spawn may begin inside the radius margin. Let
            // intentional motion toward the face's authored-normal side
            // depenetrate there instead of preserving the embedded side.
            // Once the capsule is clear, normal two-sided collision keeps it
            // from crossing the wall in either direction.
            if (depenetration ==
                    LevelCollisionDepenetration::TowardAuthoredNormal &&
                startSide < 0.0F &&
                std::abs(startSide) < kGroundSupportRadius &&
                desiredSide > startSide) {
                sideSign = 1.0F;
            }
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
