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
    float verticalFieldOfViewDegrees{45.0F};
    float nearPlane{1.0F};
    float farPlane{1000.0F};
};

// Binds the named camera and target-node channels used by PlayDAECamera.
class CinematicCamera final {
public:
    [[nodiscard]] Result bind(
        const assets::ColladaAnimationFile& animation,
        float farPlaneOverride = 0.0F);
    [[nodiscard]] CameraPose sample(
        std::uint32_t timestampMilliseconds) const noexcept;
    [[nodiscard]] bool valid() const noexcept {
        return positionTrack_ != nullptr && targetTrack_ != nullptr;
    }

private:
    const assets::ColladaAnimationTrack* positionTrack_{};
    const assets::ColladaAnimationTrack* targetTrack_{};
    CameraPose defaults_;
};

} // namespace usm::game
