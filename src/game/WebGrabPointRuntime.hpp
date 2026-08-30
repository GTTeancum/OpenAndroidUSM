#pragma once

#include "assets/IrrScene.hpp"
#include "game/LevelOneBootstrap.hpp"

#include <cstdint>
#include <span>

namespace usm::game {

class LevelCollision;

// Portable reconstruction of Player::GetBestWebGrabPoint (0x00344424),
// GetClosestWebGrabPoint (0x00344648), and SearchWebGrabPoint (0x0034486c).
// It owns no level data and has no renderer or platform dependencies.
class WebGrabPointRuntime final {
public:
    void bind(std::span<const LevelWebGrabPointAsset> points,
              const LevelCollision* collision = nullptr) noexcept;

    [[nodiscard]] const LevelWebGrabPointAsset* findBest(
        const assets::Vector3& playerPosition,
        const assets::Vector3& playerFacing,
        std::int32_t currentPointId = -1) const noexcept;
    [[nodiscard]] const LevelWebGrabPointAsset* findClosestVisible(
        const assets::Vector3& playerPosition,
        std::int32_t currentPointId = -1) const noexcept;
    [[nodiscard]] const LevelWebGrabPointAsset* search(
        const assets::Vector3& playerPosition,
        const assets::Vector3& playerFacing,
        std::int32_t currentPointId = -1) const noexcept;

private:
    [[nodiscard]] bool hasLineOfSight(
        const assets::Vector3& playerPosition,
        const LevelWebGrabPointAsset& point) const noexcept;

    std::span<const LevelWebGrabPointAsset> points_;
    const LevelCollision* collision_{};
};

} // namespace usm::game
