#pragma once

#include "assets/BresFile.hpp"
#include "core/Result.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <optional>
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

struct ColladaCamera {
    std::string id;
    std::string targetNode;
    bool orthographic{};
    float verticalFieldOfViewDegrees{45.0F};
    float aspectRatio{1.5F};
    float nearPlane{1.0F};
    float farPlane{1000.0F};
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
    [[nodiscard]] const std::optional<ColladaCamera>& camera() const noexcept {
        return camera_;
    }

private:
    BresFile resource_;
    std::vector<ColladaAnimationTrack> tracks_;
    std::optional<ColladaCamera> camera_;
};

} // namespace usm::assets
