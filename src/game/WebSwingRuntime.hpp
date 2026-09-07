#pragma once

#include "assets/IrrScene.hpp"
#include "core/Result.hpp"
#include "game/LevelOneBootstrap.hpp"

#include <cstdint>

namespace usm::game {

struct WebSwingRelease {
    assets::Vector3 velocityCentimetersPerSecond;
    bool hasTargetWaypoint{};
    assets::Vector3 targetWaypointPosition;
    std::int32_t targetSlideId{-1};
};

// Renderer-independent swing constraint reconstructed around the authored
// fields consumed by Player::StartWebSwing (0x00348090) and motion types 27/28
// in Player::UpdateMCSpeed (0x00346f50).
class WebSwingRuntime final {
public:
    [[nodiscard]] Result start(
        const LevelWebGrabPointAsset& point,
        const assets::Vector3& playerPosition,
        const assets::Vector3& playerVelocityCentimetersPerSecond = {})
        noexcept;
    void update(std::uint32_t elapsedMilliseconds) noexcept;
    [[nodiscard]] WebSwingRelease release() noexcept;

    [[nodiscard]] bool active() const noexcept { return point_ != nullptr; }
    [[nodiscard]] const LevelWebGrabPointAsset* point() const noexcept {
        return point_;
    }
    [[nodiscard]] const assets::Vector3& position() const noexcept {
        return position_;
    }
    [[nodiscard]] const assets::Vector3& velocityCentimetersPerSecond()
        const noexcept {
        return velocityCentimetersPerSecond_;
    }
    [[nodiscard]] const assets::Vector3& ropeDirection() const noexcept {
        return ropeDirection_;
    }
    [[nodiscard]] const assets::Vector3& travelDirection() const noexcept {
        return travelDirection_;
    }
    [[nodiscard]] bool exitAngleReached() const noexcept {
        return exitAngleReached_;
    }

private:
    [[nodiscard]] float finishTimeMilliseconds() const noexcept;
    void refreshDesiredPose(assets::Vector3& desiredPosition) noexcept;

    const LevelWebGrabPointAsset* point_{};
    assets::Vector3 position_;
    assets::Vector3 velocityCentimetersPerSecond_;
    assets::Vector3 ropeDirection_{0.0F, 0.0F, -1.0F};
    assets::Vector3 travelDirection_{1.0F, 0.0F, 0.0F};
    assets::Vector3 planeNormal_{0.0F, 1.0F, 0.0F};
    assets::Vector3 horizontalOffset_;
    float angleRadians_{};
    float targetAngleRadians_{};
    float catchUpTimeMilliseconds_{};
    bool exitAngleReached_{};
};

} // namespace usm::game
