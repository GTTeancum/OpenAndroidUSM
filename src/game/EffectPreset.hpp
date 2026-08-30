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

struct EffectEmitterPreset {
    std::string name;
    assets::Vector3 position;
    assets::Vector3 scale{1.0F, 1.0F, 1.0F};
    assets::Vector3 box;
    assets::Vector3 direction;
    assets::Vector3 gravity;
    std::int32_t systemMinimumLifetimeMilliseconds{};
    std::int32_t systemMaximumLifetimeMilliseconds{};
    std::int32_t startDelayMilliseconds{};
    std::int32_t minimumParticlesPerSecond{};
    std::int32_t maximumParticlesPerSecond{};
    float particleWidth{};
    float particleHeight{};
    float targetWidth{};
    float targetHeight{};
    std::int32_t sizeVariationPercent{};
    std::uint32_t minimumStartColor{0xffffffffU};
    std::uint32_t maximumStartColor{0xffffffffU};
    std::uint32_t fadeTargetColor{};
    std::int32_t minimumParticleLifetimeMilliseconds{};
    std::int32_t maximumParticleLifetimeMilliseconds{};
    std::int32_t speedVariationPercent{};
    std::int32_t initialRotationMinimumDegrees{};
    std::int32_t initialRotationMaximumDegrees{};
    std::int32_t fadeStartPercent{100};
    std::int32_t fadeEndPercent{100};
    std::int32_t sizeStartPercent{};
    std::int32_t sizeEndPercent{100};
    std::int32_t frameId{-1};
    bool additive{};
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
