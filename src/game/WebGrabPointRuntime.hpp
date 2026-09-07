#pragma once

#include "assets/IrrScene.hpp"
#include "game/LevelOneBootstrap.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace usm::game {

class LevelCollision;

struct WebGrabCandidateDiagnostics {
    std::int32_t objectId{-1};
    float distance{};
    float facingDot{};
    float visibleLength{};
    bool currentPoint{};
    bool lineOfSight{};
    bool facingAccepted{};
    bool withinVisibleLength{};
    bool withinBestSearchRadius{};
    bool withinHintSearchRadius{};
    bool roomVisible{true};
    bool onScreen{true};
    std::int32_t blockingRoomId{-1};
    std::uint32_t blockingPhysicsFlags{};
    float blockingFraction{};
    assets::Vector3 blockingPosition;
    std::string_view blockingGeometry;
    std::string_view blockingMaterial;
};

// Portable reconstruction of Player::GetBestWebGrabPoint (0x00344424),
// GetClosestWebGrabPoint (0x00344648), and SearchWebGrabPoint (0x0034486c).
// It owns no level data and has no renderer or platform dependencies.
class WebGrabPointRuntime final {
public:
    void bind(std::span<const LevelWebGrabPointAsset> points,
              const LevelCollision* collision = nullptr) noexcept;
    void setViewContext(const CameraPose& camera, float aspectRatio,
                        std::span<const bool> roomVisibility) noexcept;

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
    [[nodiscard]] std::vector<WebGrabCandidateDiagnostics> diagnose(
        const assets::Vector3& playerPosition,
        const assets::Vector3& playerFacing,
        std::int32_t currentPointId = -1) const;

private:
    [[nodiscard]] bool inNativeCandidateSet(
        const assets::Vector3& playerPosition,
        const LevelWebGrabPointAsset& point,
        float searchRadius) const noexcept;
    [[nodiscard]] bool hasLineOfSight(
        const assets::Vector3& playerPosition,
        const LevelWebGrabPointAsset& point) const noexcept;

    std::span<const LevelWebGrabPointAsset> points_;
    const LevelCollision* collision_{};
    CameraPose camera_;
    float aspectRatio_{};
    std::span<const bool> roomVisibility_;
    bool hasViewContext_{};
};

} // namespace usm::game
