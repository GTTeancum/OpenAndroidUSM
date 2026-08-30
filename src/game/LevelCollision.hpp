#pragma once

#include "assets/ColladaMesh.hpp"
#include "assets/IrrScene.hpp"
#include "core/Result.hpp"
#include "game/LevelOneBootstrap.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace usm::game {

namespace LevelPhysicsFlags {
constexpr std::uint32_t Ground = 0x01U;
constexpr std::uint32_t Wall = 0x02U;
constexpr std::uint32_t JumpWall = 0x10U;
constexpr std::uint32_t ClimbableWall = 0x20U;
constexpr std::uint32_t ClimbableEdge = 0x40U;
} // namespace LevelPhysicsFlags

namespace LevelCollisionConstants {
constexpr float MinimumGroundNormalZ = 0.70710677F;
} // namespace LevelCollisionConstants

struct LevelWallContact {
    assets::Vector3 position;
    assets::Vector3 normal;
    float segmentFraction{};
    std::uint32_t physicsFlags{};
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

    [[nodiscard]] bool groundHeight(const assets::Vector3& reference,
                                    float maximumStepUp, float maximumDrop,
                                    float& height,
                                    std::uint32_t ignoredPhysicsFlags = 0U)
        const noexcept;
    [[nodiscard]] bool resolveGroundMotion(
        const assets::Vector3& start, const assets::Vector3& desired,
        assets::Vector3& resolved, float maximumStepUp = 75.0F,
        float maximumDrop = 150.0F,
        std::uint32_t ignoredPhysicsFlags = 0U) const noexcept;
    void resolveAirMotion(const assets::Vector3& start,
                          const assets::Vector3& desired,
                          assets::Vector3& resolved,
                          std::uint32_t ignoredPhysicsFlags = 0U)
        const noexcept;
    [[nodiscard]] bool segmentBlocked(
        const assets::Vector3& start,
        const assets::Vector3& end) const noexcept;
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
    };

    void append(std::span<const assets::ColladaGeometry> geometries);
    void rebuildGrid();
    void resolveWalls(const assets::Vector3& start,
                      assets::Vector3& desired,
                      std::uint32_t ignoredPhysicsFlags) const noexcept;
    [[nodiscard]] bool horizontalSurfaceContact(
        const assets::Vector3& capsuleBase,
        std::uint32_t physicsFlags,
        LevelWallContact& contact) const noexcept;
    [[nodiscard]] static std::int64_t cellKey(std::int32_t x,
                                               std::int32_t y) noexcept;

    std::vector<Triangle> triangles_;
    std::unordered_map<std::int64_t, std::vector<std::uint32_t>> grid_;
    std::vector<std::uint32_t> broadTriangles_;
};

} // namespace usm::game
