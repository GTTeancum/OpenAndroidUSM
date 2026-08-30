#pragma once

#include "assets/IrrScene.hpp"

namespace usm::audio {

struct SpatialSoundSource {
    assets::Vector3 position;
    float minimumDistance{};
    float maximumDistance{};
    bool distanceCullingEnabled{};
};

struct SpatialSoundMix {
    float distance{};
    float attenuation{1.0F};
    float leftGain{};
    float rightGain{};
    bool culled{};
};

// Deterministic equivalent of the native emitter range/culling setup followed
// by a constant-power mono-to-stereo pan for the Windows XAudio2 backend.
[[nodiscard]] SpatialSoundMix calculateSpatialSoundMix(
    const assets::Vector3& listenerPosition,
    const assets::Vector3& listenerRight,
    const SpatialSoundSource& source) noexcept;

} // namespace usm::audio
