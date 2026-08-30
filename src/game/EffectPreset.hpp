#pragma once

#include "assets/IrrScene.hpp"
#include "core/Result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace usm::game {

struct EffectColorAffector {
    std::uint32_t targetColor{};
    std::int32_t startPercent{100};
    std::int32_t endPercent{100};
};

struct EffectSizeAffector {
    float targetWidth{};
    float targetHeight{};
    std::int32_t variationPercent{};
    std::int32_t startPercent{};
    std::int32_t endPercent{100};
};

struct EffectEmitterPreset {
    std::string name;
    assets::Vector3 position;
    assets::Vector3 scale{1.0F, 1.0F, 1.0F};
    assets::Vector3 box;
    assets::Vector3 direction;
    assets::Vector3 gravity;
    assets::Vector3 rotationPivot;
    assets::Vector3 rotationSpeedDegreesPerSecond;
    std::int32_t systemMinimumLifetimeMilliseconds{};
    std::int32_t systemMaximumLifetimeMilliseconds{};
    std::int32_t startDelayMilliseconds{};
    std::int32_t minimumParticlesPerSecond{};
    std::int32_t maximumParticlesPerSecond{};
    float particleWidth{};
    float particleHeight{};
    std::int32_t sizeVariationPercent{};
    std::uint32_t minimumStartColor{0xffffffffU};
    std::uint32_t maximumStartColor{0xffffffffU};
    std::int32_t minimumParticleLifetimeMilliseconds{};
    std::int32_t maximumParticleLifetimeMilliseconds{};
    std::int32_t speedVariationPercent{};
    std::int32_t initialRotationMinimumDegrees{};
    std::int32_t initialRotationMaximumDegrees{};
    std::int32_t gravityStartPercent{};
    std::int32_t gravityEndPercent{100};
    std::int32_t spinMinimumDegrees{};
    std::int32_t spinMaximumDegrees{};
    std::int32_t spinStartPercent{};
    std::int32_t spinEndPercent{100};
    std::int32_t frameId{-1};
    bool hasGravity{};
    bool hasRotation{};
    bool hasSpin{};
    bool additive{};
    std::vector<EffectColorAffector> colorAffectors;
    std::vector<EffectSizeAffector> sizeAffectors;
};

struct EffectPreset {
    std::string name;
    std::vector<EffectEmitterPreset> emitters;
};

// Typed reconstruction of the preset records consumed by
// EffectManager::LoadEffectPresets (0x003925c4).
class EffectPresetDatabase final {
public:
    [[nodiscard]] Result load(std::span<const std::byte> bytes);
    [[nodiscard]] const EffectPreset* find(
        std::string_view name) const noexcept;
    [[nodiscard]] const std::vector<EffectPreset>& presets() const noexcept {
        return presets_;
    }

private:
    std::vector<EffectPreset> presets_;
};

} // namespace usm::game
