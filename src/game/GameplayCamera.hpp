#pragma once

#include "assets/IrrScene.hpp"
#include "core/Result.hpp"
#include "game/CinematicCamera.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace usm::game {

struct CameraControlPoint {
    std::int32_t objectId{-1};
    assets::Vector3 position;
    assets::Vector3 direction;
    float distance{};
    assets::Vector3 targetOffset;
    float targetHeightOffset{};
};

struct CameraArea {
    std::int32_t objectId{-1};
    std::array<std::int32_t, 4> nextAreaIds{{-1, -1, -1, -1}};
    // CameraAreaSwitcher::InitSwitchTargetDirDis (0x002f58f4) converts each
    // authored switchTime unit to 50 milliseconds.
    std::array<std::uint32_t, 4> switchTimeUnits{};
    std::array<CameraControlPoint, 4> controlPoints;
    bool inverseNormal{};
    float height{};
    float zFollowRate{};
    bool disabled{};
    float farPlaneOffset{};
};

// Native reconstruction of CCameraArea interpolation and the standard
// CGameCamera placement path. The original routines are preserved at image
// addresses 0x002f4ec4 and 0x002f327c.
class GameplayCamera final {
public:
    [[nodiscard]] Result bind(std::span<const CameraArea> areas,
                              std::int32_t initialAreaId);
    [[nodiscard]] CameraPose sample(
        const assets::Vector3& playerPosition) const noexcept;
    // Reconstructs CCameraArea::GetValidArea (0x002f3f3c): only declared
    // neighbors can become active, and only after the player leaves this quad.
    [[nodiscard]] bool updateArea(
        const assets::Vector3& playerPosition,
        std::uint32_t elapsedMilliseconds = 0) noexcept;
    [[nodiscard]] std::int32_t currentAreaId() const noexcept {
        return currentArea_ == nullptr ? -1 : currentArea_->objectId;
    }
    [[nodiscard]] std::uint32_t lastSwitchDurationMilliseconds() const noexcept {
        return transitionDurationMilliseconds_;
    }

private:
    void advanceTransition(std::uint32_t elapsedMilliseconds) noexcept;

    std::span<const CameraArea> areas_;
    const CameraArea* currentArea_{};
    CameraPose transitionStartPose_;
    std::uint32_t transitionDurationMilliseconds_{};
    std::uint32_t transitionElapsedMilliseconds_{};
    float transitionProgress_{1.0F};
};

} // namespace usm::game
