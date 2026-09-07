#include "game/WebLineGeometry.hpp"

#include <cmath>
#include <limits>

namespace usm::game {
namespace {

assets::Vector3 add(const assets::Vector3& left,
                    const assets::Vector3& right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

assets::Vector3 subtract(const assets::Vector3& left,
                         const assets::Vector3& right) noexcept {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

assets::Vector3 scale(const assets::Vector3& value, float factor) noexcept {
    return {value.x * factor, value.y * factor, value.z * factor};
}

float length(const assets::Vector3& value) noexcept {
    return std::sqrt(value.x * value.x + value.y * value.y +
                     value.z * value.z);
}

bool normalize(assets::Vector3& value) noexcept {
    const float magnitude = length(value);
    if (!std::isfinite(magnitude) ||
        magnitude <= std::numeric_limits<float>::epsilon()) {
        return false;
    }
    value = scale(value, 1.0F / magnitude);
    return true;
}

assets::Vector3 cross(const assets::Vector3& left,
                      const assets::Vector3& right) noexcept {
    return {left.y * right.z - left.z * right.y,
            left.z * right.x - left.x * right.z,
            left.x * right.y - left.y * right.x};
}

bool finite(const assets::Vector3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

} // namespace

Result buildWebLineGeometry(const assets::Vector3& start,
                            const assets::Vector3& end,
                            const assets::Vector3& orientation,
                            WebLineGeometry& output) noexcept {
    output = {};
    if (!finite(start) || !finite(end) || !finite(orientation)) {
        return Result::failure("Web line inputs are not finite");
    }

    assets::Vector3 direction = subtract(end, start);
    const float lineLength = length(direction);
    if (!normalize(direction)) {
        return Result::failure("Web line endpoints coincide");
    }

    assets::Vector3 normalizedOrientation = orientation;
    if (!normalize(normalizedOrientation)) {
        return Result::failure("Web line orientation has no direction");
    }

    // setLineSegment forms direction x orientation at 0x0039b000-0x0039b034,
    // normalizes it, scales by the width, and places the two sides at +/- 1/2.
    assets::Vector3 widthAxis = cross(direction, normalizedOrientation);
    if (!normalize(widthAxis)) {
        // Irrlicht's normalize leaves a zero vector unchanged, producing a
        // collapsed (but otherwise valid) ribbon when view and line coincide.
        widthAxis = {};
    }
    const assets::Vector3 halfWidth =
        scale(widthAxis, kWebLineWidth * 0.5F);

    const float repeatCount = lineLength / kWebLineSegmentLength;
    const std::uint32_t completeSegmentCount =
        static_cast<std::uint32_t>(repeatCount);
    const bool capped =
        completeSegmentCount >= kWebLineMaximumSegments;
    const std::uint32_t activeSegmentCount =
        capped ? kWebLineMaximumSegments : completeSegmentCount + 1U;
    const std::uint32_t pairCount = activeSegmentCount + 1U;

    output.vertices.reserve(pairCount * 2U);
    output.indices.reserve(activeSegmentCount * 6U);
    for (std::uint32_t pair = 0; pair < pairCount; ++pair) {
        float distance;
        float textureU;
        if (capped) {
            distance = lineLength * static_cast<float>(pair) /
                       static_cast<float>(kWebLineMaximumSegments);
            textureU = static_cast<float>(pair);
        } else if (pair + 1U == pairCount) {
            distance = lineLength;
            textureU = repeatCount;
        } else {
            distance = kWebLineSegmentLength * static_cast<float>(pair);
            textureU = static_cast<float>(pair);
        }
        const assets::Vector3 center = add(start, scale(direction, distance));
        output.vertices.push_back(
            {subtract(center, halfWidth), textureU, kWebLineTextureBottom});
        output.vertices.push_back(
            {add(center, halfWidth), textureU, kWebLineTextureTop});
    }
    for (std::uint32_t segment = 0; segment < activeSegmentCount; ++segment) {
        const std::uint16_t first =
            static_cast<std::uint16_t>(segment * 2U);
        output.indices.insert(output.indices.end(),
                              {first,
                               static_cast<std::uint16_t>(first + 1U),
                               static_cast<std::uint16_t>(first + 2U),
                               static_cast<std::uint16_t>(first + 2U),
                               static_cast<std::uint16_t>(first + 1U),
                               static_cast<std::uint16_t>(first + 3U)});
    }
    return Result::success();
}

} // namespace usm::game
