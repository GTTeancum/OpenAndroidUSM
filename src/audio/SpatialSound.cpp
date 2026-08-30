#include "audio/SpatialSound.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace usm::audio {
namespace {

assets::Vector3 subtract(const assets::Vector3& first,
                         const assets::Vector3& second) noexcept {
    return {first.x - second.x, first.y - second.y, first.z - second.z};
}

float length(const assets::Vector3& value) noexcept {
    return std::sqrt(value.x * value.x + value.y * value.y +
                     value.z * value.z);
}

assets::Vector3 normalize(const assets::Vector3& value) noexcept {
    const float magnitude = length(value);
    return magnitude > 0.0001F
               ? assets::Vector3{value.x / magnitude, value.y / magnitude,
                                 value.z / magnitude}
               : assets::Vector3{};
}

float dot(const assets::Vector3& first,
          const assets::Vector3& second) noexcept {
    return first.x * second.x + first.y * second.y + first.z * second.z;
}

} // namespace

SpatialSoundMix calculateSpatialSoundMix(
    const assets::Vector3& listenerPosition,
    const assets::Vector3& listenerRight,
    const SpatialSoundSource& source) noexcept {
    const assets::Vector3 offset = subtract(source.position, listenerPosition);
    SpatialSoundMix mix;
    mix.distance = length(offset);
    mix.culled = source.distanceCullingEnabled &&
                 source.maximumDistance > 0.0F &&
                 mix.distance > source.maximumDistance;
    if (source.maximumDistance > source.minimumDistance) {
        mix.attenuation = std::clamp(
            (source.maximumDistance - mix.distance) /
                (source.maximumDistance - source.minimumDistance),
            0.0F, 1.0F);
    }
    const float pan =
        mix.distance > 0.0001F
            ? std::clamp(dot(normalize(offset), listenerRight), -1.0F, 1.0F)
            : 0.0F;
    const float angle =
        (pan + 1.0F) * std::numbers::pi_v<float> * 0.25F;
    mix.leftGain = std::cos(angle) * mix.attenuation;
    mix.rightGain = std::sin(angle) * mix.attenuation;
    return mix;
}

} // namespace usm::audio
