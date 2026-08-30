#pragma once

#include "assets/ColladaMesh.hpp"
#include "assets/IrrScene.hpp"
#include "core/Result.hpp"
#include "game/LevelOneBootstrap.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace usm::game {

// Portable triangle collision extracted from each room's authored Collisions
// mesh. It is deliberately independent of Direct3D and platform input.
class LevelCollision final {
public:
    [[nodiscard]] Result build(std::span<const LevelRoomAsset> rooms);
    [[nodiscard]] Result build(
        std::span<const assets::ColladaGeometry> geometries);

    [[nodiscard]] bool groundHeight(const assets::Vector3& reference,
                                    float maximumStepUp, float maximumDrop,
                                    float& height) const noexcept;
    [[nodiscard]] bool resolveGroundMotion(
        const assets::Vector3& start, const assets::Vector3& desired,
        assets::Vector3& resolved, float maximumStepUp = 75.0F,
        float maximumDrop = 150.0F) const noexcept;
    void resolveAirMotion(const assets::Vector3& start,
                          const assets::Vector3& desired,
                          assets::Vector3& resolved) const noexcept;

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
    };

    void append(std::span<const assets::ColladaGeometry> geometries);
    void rebuildGrid();
    void resolveWalls(const assets::Vector3& start,
                      assets::Vector3& desired) const noexcept;
    [[nodiscard]] static std::int64_t cellKey(std::int32_t x,
                                               std::int32_t y) noexcept;

    std::vector<Triangle> triangles_;
    std::unordered_map<std::int64_t, std::vector<std::uint32_t>> grid_;
    std::vector<std::uint32_t> broadTriangles_;
};

} // namespace usm::game
