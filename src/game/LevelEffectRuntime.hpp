#pragma once

#include "core/Result.hpp"
#include "game/CinematicScript.hpp"
#include "game/EffectPreset.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace usm::game {

struct EffectParticleState {
    assets::Vector3 position;
    float width{};
    float height{};
    float rotationDegrees{};
    std::uint32_t color{0xffffffffU};
    std::int32_t frameId{-1};
    bool additive{};
    std::int32_t roomId{-1};
};

// Portable one-shot effect state created by CCinematicThread::PlayEffect
// (0x003712f8). It exposes renderer-neutral billboard particles only.
class LevelEffectRuntime final {
public:
    LevelEffectRuntime();
    ~LevelEffectRuntime();
    LevelEffectRuntime(const LevelEffectRuntime&) = delete;
    LevelEffectRuntime& operator=(const LevelEffectRuntime&) = delete;

    [[nodiscard]] Result initialize(const EffectPresetDatabase& presets);
    [[nodiscard]] Result applyCinematicCommand(
        const CinematicCommand& command);
    [[nodiscard]] Result addPersistentEffect(
        std::string_view effectType, const assets::Vector3& origin,
        std::int32_t roomId, bool visible = true);
    void update(std::uint32_t elapsedMilliseconds) noexcept;

    [[nodiscard]] std::span<const EffectParticleState> particles() const
        noexcept {
        return renderParticles_;
    }

private:
    struct PendingEmitter;
    struct PersistentEmitter;
    struct Particle;

    void spawnEmitter(const PendingEmitter& emitter) noexcept;
    void spawnParticle(const EffectEmitterPreset& preset,
                       const assets::Vector3& origin,
                       std::int32_t roomId) noexcept;
    [[nodiscard]] float randomUnit() noexcept;
    [[nodiscard]] float randomRange(float minimum, float maximum) noexcept;

    const EffectPresetDatabase* presets_{};
    std::vector<PendingEmitter> pendingEmitters_;
    std::vector<PersistentEmitter> persistentEmitters_;
    std::vector<Particle> particles_;
    std::vector<EffectParticleState> renderParticles_;
    std::uint32_t randomState_{0x6d2b79f5U};
};

} // namespace usm::game
