#pragma once

#include "assets/BresFile.hpp"
#include "core/Result.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace usm::assets {

enum class ColladaAnimationProperty {
    Unknown,
    Translation,
    Rotation,
};

struct ColladaAnimationSample {
    std::array<float, 4> value{};
    std::uint32_t componentCount{};
};

struct ColladaAnimationTrack {
    std::string id;
    std::string targetNode;
    ColladaAnimationProperty property{ColladaAnimationProperty::Unknown};
    std::uint32_t componentCount{};
    std::vector<std::uint32_t> timestampsMilliseconds;
    std::vector<float> values;

    [[nodiscard]] ColladaAnimationSample sample(
        std::uint32_t timestampMilliseconds) const noexcept;
};

// Typed view of the SAnimation/SSource data used by CAnimationTrackEx.
class ColladaAnimationFile final {
public:
    [[nodiscard]] Result load(std::span<const std::byte> bytes);
    [[nodiscard]] const std::vector<ColladaAnimationTrack>& tracks() const
        noexcept {
        return tracks_;
    }
    [[nodiscard]] std::uint32_t durationMilliseconds() const noexcept;

private:
    BresFile resource_;
    std::vector<ColladaAnimationTrack> tracks_;
};

} // namespace usm::assets
