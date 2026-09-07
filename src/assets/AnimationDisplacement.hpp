#pragma once

#include "assets/ColladaMesh.hpp"
#include "core/Result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace usm::assets {

// Portable form of DisplacementAnimation's exported *_dummy.bin and
// *_pelvis.bin streams. The native runtime uses the dummy stream for physics
// and pelvis-minus-dummy for the render offset; both are sampled on the
// animation bank's fixed 30 Hz timeline.
class AnimationDisplacement final {
public:
    [[nodiscard]] Result load(std::span<const std::byte> dummyBytes,
                              std::span<const std::byte> pelvisBytes);

    [[nodiscard]] Vector3 physicalAt(
        std::uint32_t timelineMilliseconds) const noexcept;
    [[nodiscard]] Vector3 renderOffsetAt(
        std::uint32_t timelineMilliseconds) const noexcept;
    [[nodiscard]] std::size_t frameCount() const noexcept {
        return physicalFrames_.size();
    }

private:
    [[nodiscard]] static Vector3 sample(
        std::span<const Vector3> frames,
        std::uint32_t timelineMilliseconds) noexcept;

    std::vector<Vector3> physicalFrames_;
    std::vector<Vector3> renderOffsetFrames_;
};

} // namespace usm::assets
