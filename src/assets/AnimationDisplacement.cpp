#include "assets/AnimationDisplacement.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace usm::assets {
namespace {

constexpr std::size_t kVectorByteSize = sizeof(float) * 3U;
constexpr double kAnimationFramesPerSecond = 30.0;

float readFloat(std::span<const std::byte> bytes, std::size_t offset) noexcept {
    std::uint32_t bits{};
    for (std::size_t index = 0; index < sizeof(bits); ++index) {
        bits |= static_cast<std::uint32_t>(
                    std::to_integer<unsigned char>(bytes[offset + index]))
                << (index * 8U);
    }
    return std::bit_cast<float>(bits);
}

Vector3 exportedVector(std::span<const std::byte> bytes,
                       std::size_t offset) noexcept {
    const float sourceX = readFloat(bytes, offset);
    const float sourceY = readFloat(bytes, offset + sizeof(float));
    const float sourceZ = readFloat(bytes, offset + sizeof(float) * 2U);
    // createDisplacementAnimation (0x003902f8) converts the exporter axes
    // while copying each twelve-byte record.
    return {-sourceY, sourceX, sourceZ};
}

bool finite(const Vector3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}

} // namespace

Result AnimationDisplacement::load(std::span<const std::byte> dummyBytes,
                                   std::span<const std::byte> pelvisBytes) {
    physicalFrames_.clear();
    renderOffsetFrames_.clear();
    if (dummyBytes.empty() || pelvisBytes.empty() ||
        dummyBytes.size() % kVectorByteSize != 0 ||
        pelvisBytes.size() != dummyBytes.size()) {
        return Result::failure(
            "Animation displacement streams have invalid sizes");
    }

    const std::size_t count = dummyBytes.size() / kVectorByteSize;
    physicalFrames_.reserve(count);
    renderOffsetFrames_.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t offset = index * kVectorByteSize;
        const Vector3 physical = exportedVector(dummyBytes, offset);
        Vector3 pelvis = exportedVector(pelvisBytes, offset);
        pelvis.z = 0.0F;
        const Vector3 renderOffset{pelvis.x - physical.x,
                                   pelvis.y - physical.y,
                                   pelvis.z - physical.z};
        if (!finite(physical) || !finite(renderOffset)) {
            physicalFrames_.clear();
            renderOffsetFrames_.clear();
            return Result::failure(
                "Animation displacement stream contains non-finite data");
        }
        physicalFrames_.push_back(physical);
        renderOffsetFrames_.push_back(renderOffset);
    }
    return Result::success();
}

Vector3 AnimationDisplacement::physicalAt(
    std::uint32_t timelineMilliseconds) const noexcept {
    return sample(physicalFrames_, timelineMilliseconds);
}

Vector3 AnimationDisplacement::renderOffsetAt(
    std::uint32_t timelineMilliseconds) const noexcept {
    return sample(renderOffsetFrames_, timelineMilliseconds);
}

Vector3 AnimationDisplacement::sample(
    std::span<const Vector3> frames,
    std::uint32_t timelineMilliseconds) noexcept {
    if (frames.empty()) {
        return {};
    }
    const double frame =
        static_cast<double>(timelineMilliseconds) *
        kAnimationFramesPerSecond / 1000.0;
    const std::size_t first = std::min(
        static_cast<std::size_t>(frame), frames.size() - 1U);
    const std::size_t second = std::min(first + 1U, frames.size() - 1U);
    const float alpha = static_cast<float>(
        std::clamp(frame - static_cast<double>(first), 0.0, 1.0));
    return {frames[first].x + (frames[second].x - frames[first].x) * alpha,
            frames[first].y + (frames[second].y - frames[first].y) * alpha,
            frames[first].z + (frames[second].z - frames[first].z) * alpha};
}

} // namespace usm::assets
