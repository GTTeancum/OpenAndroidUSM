#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "assets/IrrScene.hpp"
#include "core/Result.hpp"

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

struct EffectAttractionAffector {
    assets::Vector3 point;
    float speed{};
    bool attract{true};
    bool affectX{true};
    bool affectY{true};
    bool affectZ{true};
};

enum class EffectAffectorKind : std::uint8_t {
    FadeOut,
    Gravity,
    Rotate,
    Spin,
    Attract,
    Size,
};

struct EffectAffectorReference {
    EffectAffectorKind kind{};
    std::size_t index{};
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
    std::int32_t restartMinimumMilliseconds{-1};
    std::int32_t restartMaximumMilliseconds{-1};
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
    std::int32_t maximumAngleDegreesXY{};
    std::int32_t maximumAngleDegreesYZ{};
    std::int32_t maximumAngleDegreesXZ{};
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
    bool globalParticles{};
    bool directionalRotation{};
    bool projectDirection{};
    std::vector<EffectColorAffector> colorAffectors;
    std::vector<EffectAttractionAffector> attractionAffectors;
    std::vector<EffectSizeAffector> sizeAffectors;
    std::vector<EffectAffectorReference> affectorOrder;
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

}  // namespace usm::game
