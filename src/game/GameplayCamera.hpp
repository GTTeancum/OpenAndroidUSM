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
    std::array<CameraControlPoint, 4> controlPoints;
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
    [[nodiscard]] std::int32_t currentAreaId() const noexcept {
        return currentArea_ == nullptr ? -1 : currentArea_->objectId;
    }

private:
    const CameraArea* currentArea_{};
};

} // namespace usm::game
