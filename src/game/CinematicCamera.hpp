#pragma once

#include "assets/ColladaAnimation.hpp"
#include "assets/IrrScene.hpp"
#include "core/Result.hpp"

#include <cstdint>

namespace usm::game {

struct CameraPose {
    assets::Vector3 position;
    assets::Vector3 target;
    // Level 1 uses the Collada Z_UP convention recovered by the original
    // CCameraSceneNode constructor.
    assets::Vector3 up{0.0F, 0.0F, 1.0F};
    // CGameCamera::ResetCamera (0x002f2c70) sets the shared gameplay camera
    // to 0.5277265 radians, a 100-unit near plane, and a 10000-unit far
    // plane. PlayDAECamera replaces these values with the BDAE/CFF camera.
    float verticalFieldOfViewDegrees{30.236501F};
    float nearPlane{100.0F};
    float farPlane{10000.0F};
};

// CGameCamera::IsPointInScreen (0x002f22fc): point inside all six
// camera-frustum planes. The original AABB precheck is redundant here.
[[nodiscard]] bool isPointInScreen(const CameraPose& camera,
    float aspectRatio, const assets::Vector3& point) noexcept;

// Binds the named camera and target-node channels used by PlayDAECamera.
class CinematicCamera final {
public:
    [[nodiscard]] Result bind(
        const assets::ColladaAnimationFile& animation,
        float farPlaneOverride = 0.0F,
        std::int32_t clipId = 0);
    [[nodiscard]] CameraPose sample(
        std::uint32_t timestampMilliseconds) const noexcept;
    [[nodiscard]] bool valid() const noexcept {
        return positionTrack_ != nullptr && targetTrack_ != nullptr;
    }
    [[nodiscard]] std::int32_t clipId() const noexcept { return clipId_; }
    [[nodiscard]] std::uint32_t clipStartMilliseconds() const noexcept {
        return clipStartMilliseconds_;
    }
    [[nodiscard]] std::uint32_t clipEndMilliseconds() const noexcept {
        return clipEndMilliseconds_;
    }
    [[nodiscard]] std::uint32_t clipDurationMilliseconds() const noexcept {
        return clipEndMilliseconds_ - clipStartMilliseconds_;
    }

private:
    const assets::ColladaAnimationTrack* positionTrack_{};
    const assets::ColladaAnimationTrack* targetTrack_{};
    CameraPose defaults_;
    std::int32_t clipId_{-1};
    std::uint32_t clipStartMilliseconds_{};
    std::uint32_t clipEndMilliseconds_{};
};

} // namespace usm::game
