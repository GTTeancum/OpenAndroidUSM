#pragma once

#include "core/Result.hpp"
#include "game/CinematicCamera.hpp"
#include "game/CinematicScript.hpp"

#include <cstdint>
#include <vector>

namespace usm::game {

struct CinematicCameraKeyframe {
    std::uint32_t timestampMilliseconds{};
    CameraPose pose;
    bool curvedInterpolation{};
};

// Portable reconstruction of CCinematic::initCameraCurve (0x0036f2dc) and
// CCinematic::updateCameraThread (0x0036e9ac). ChangeCamera commands author a
// target, direction, and signed distance; the runtime materializes camera
// positions and interpolates the target and position tracks independently.
class CinematicCameraTrack final {
public:
    [[nodiscard]] Result load(const CinematicScript& script);
    [[nodiscard]] CameraPose sample(
        std::uint32_t timestampMilliseconds) const noexcept;

    [[nodiscard]] bool valid() const noexcept { return !keyframes_.empty(); }
    [[nodiscard]] const std::vector<CinematicCameraKeyframe>& keyframes() const
        noexcept {
        return keyframes_;
    }

private:
    std::vector<CinematicCameraKeyframe> keyframes_;
};

} // namespace usm::game
