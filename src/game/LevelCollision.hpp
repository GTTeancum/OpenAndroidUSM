#pragma once

#include "assets/ColladaMesh.hpp"
#include "assets/IrrScene.hpp"
#include "core/Result.hpp"
#include "game/LevelCinematicRuntime.hpp"
#include "game/LevelOneBootstrap.hpp"

#include <cstddef>
#include <cstdint>
#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace usm::game {

struct LevelObjectState;

namespace LevelPhysicsFlags {
constexpr std::uint32_t Ground = 0x01U;
constexpr std::uint32_t Wall = 0x02U;
constexpr std::uint32_t DoubleSided = 0x04U;
constexpr std::uint32_t JumpWall = 0x10U;
constexpr std::uint32_t ClimbableWall = 0x20U;
constexpr std::uint32_t ClimbableEdge = 0x40U;
} // namespace LevelPhysicsFlags

namespace LevelCollisionConstants {
constexpr float MinimumGroundNormalZ = 0.70710677F;
} // namespace LevelCollisionConstants

enum class LevelCollisionDepenetration {
    PreserveCurrentSide,
    TowardAuthoredNormal,
};

struct LevelWallContact {
    assets::Vector3 position;
    assets::Vector3 normal;
    float segmentFraction{};
    std::uint32_t physicsFlags{};
    std::string_view geometryName;
    std::string_view materialName;
};

struct LevelSegmentHit {
    assets::Vector3 position;
    assets::Vector3 normal;
    float segmentFraction{};
    std::uint32_t physicsFlags{};
    std::int32_t roomId{-1};
    std::string_view geometryName;
    std::string_view materialName;
};

// Portable triangle collision extracted from each room's authored Collisions
// mesh. It is deliberately independent of Direct3D and platform input.
class LevelCollision final {
public:
    [[nodiscard]] Result build(std::span<const LevelRoomAsset> rooms);
    [[nodiscard]] Result build(
        std::span<const assets::ColladaGeometry> geometries);
    // CRoom::SetPosition (0x0036d6e4) translates the room's static physics
    // body by the same delta applied to its scene node. Keep the portable
    // triangle mesh synchronized with CRoom::Move/RevertPosition.
    [[nodiscard]] Result updateRoomPositions(
        std::span<const RoomMotionState> rooms);
    // CSlideCar constructs a transmission PhysicsEntity from the authored
    // child node named "bbox" and updates that entity with the car's absolute
    // scene transform. Keep that moving collider in the same portable query
    // world as room collision.
    [[nodiscard]] Result updateObjectColliders(
        std::span<const LevelObjectState> objects);

    [[nodiscard]] bool groundHeight(const assets::Vector3& reference,
                                    float maximumStepUp, float maximumDrop,
                                    float& height,
                                    std::uint32_t ignoredPhysicsFlags = 0U,
                                    std::int32_t* supportingObjectId = nullptr)
        const noexcept;
    [[nodiscard]] bool resolveGroundMotion(
        const assets::Vector3& start, const assets::Vector3& desired,
        assets::Vector3& resolved, float maximumStepUp = 75.0F,
        float maximumDrop = 150.0F,
        std::uint32_t ignoredPhysicsFlags = 0U,
        LevelCollisionDepenetration depenetration =
            LevelCollisionDepenetration::PreserveCurrentSide) const noexcept;
    void resolveAirMotion(const assets::Vector3& start,
                          const assets::Vector3& desired,
                          assets::Vector3& resolved,
                          std::uint32_t ignoredPhysicsFlags = 0U,
                          LevelCollisionDepenetration depenetration =
                              LevelCollisionDepenetration::PreserveCurrentSide)
        const noexcept;
    [[nodiscard]] bool segmentBlocked(
        const assets::Vector3& start,
        const assets::Vector3& end,
        std::uint32_t ignoredPhysicsFlags = 0U) const noexcept;
    [[nodiscard]] std::optional<LevelSegmentHit> segmentFirstHit(
        const assets::Vector3& start,
        const assets::Vector3& end,
        std::uint32_t ignoredPhysicsFlags = 0U) const noexcept;
    [[nodiscard]] bool climbableWallContact(
        const assets::Vector3& start, const assets::Vector3& end,
        LevelWallContact& contact) const noexcept;
    [[nodiscard]] bool climbableEdgeContact(
        const assets::Vector3& capsuleBase,
        LevelWallContact& contact) const noexcept;
    [[nodiscard]] bool jumpWallContact(
        const assets::Vector3& capsuleBase,
        LevelWallContact& contact) const noexcept;

    [[nodiscard]] std::size_t triangleCount() const noexcept {
        return triangles_.size();
    }

private:
    struct Triangle {
        assets::Vector3 localFirst;
        assets::Vector3 localSecond;
        assets::Vector3 localThird;
        assets::Vector3 first;
        assets::Vector3 second;
        assets::Vector3 third;
        assets::Vector3 normal;
        float minimumX{};
        float maximumX{};
        float minimumY{};
        float maximumY{};
        float minimumZ{};
        float maximumZ{};
        std::string geometryName;
        std::string materialName;
        std::uint32_t physicsFlags{};
        std::int32_t roomId{-1};
        std::int32_t objectId{-1};
    };

    struct DynamicObjectSnapshot {
        std::int32_t objectId{-1};
        std::array<float, 16> worldTransform{};

        bool operator==(const DynamicObjectSnapshot&) const = default;
    };

    void append(std::span<const assets::ColladaGeometry> geometries,
                std::int32_t roomId = -1,
                const assets::Vector3& roomPosition = {});
    void appendObjectBox(const LevelObjectState& object);
    void rebuildGrid();
    void resolveWalls(const assets::Vector3& start,
                      assets::Vector3& desired,
                      std::uint32_t ignoredPhysicsFlags,
                      LevelCollisionDepenetration depenetration) const noexcept;
    [[nodiscard]] bool horizontalSurfaceContact(
        const assets::Vector3& capsuleBase,
        std::uint32_t physicsFlags,
        LevelWallContact& contact) const noexcept;
    [[nodiscard]] static std::int64_t cellKey(std::int32_t x,
                                               std::int32_t y) noexcept;

    std::vector<Triangle> triangles_;
    std::size_t staticTriangleCount_{};
    std::vector<assets::Vector3> roomPositions_;
    std::vector<DynamicObjectSnapshot> dynamicObjectSnapshots_;
    std::unordered_map<std::int64_t, std::vector<std::uint32_t>> grid_;
    std::vector<std::uint32_t> broadTriangles_;
};

} // namespace usm::game
