#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "core/Result.hpp"
#include "game/CinematicScript.hpp"
#include "game/EffectPreset.hpp"
#include "game/NativeRandomizer.hpp"

namespace usm::game {

struct EffectParticleState {
    assets::Vector3 position;
    assets::Vector3 previousPosition;
    assets::Vector3 emitterDirection;
    float width{};
    float height{};
    float rotationDegrees{};
    std::uint32_t color{0xffffffffU};
    std::int32_t frameId{-1};
    bool additive{};
    bool directionalRotation{};
    bool projectDirection{};
    bool hasSpinAffector{};
    std::int32_t roomId{-1};
    // Native transparent sorting is per particle-system scene node, not per
    // particle. Preserve the owning node identity and absolute position.
    std::uint64_t emitterId{};
    assets::Vector3 emitterPosition;
};

struct PersistentEffectCheckPointState {
    std::int32_t sourceObjectId{-1};
    std::uint32_t emissionAccumulatorMilliseconds{};
    std::uint32_t activeElapsedMilliseconds{};
    std::uint32_t restartElapsedMilliseconds{};
    std::uint32_t startDelayElapsedMilliseconds{};
    std::uint32_t suspendedElapsedMilliseconds{};
    std::int32_t selectedSystemLifetimeMilliseconds{-1};
    std::int32_t selectedRestartMilliseconds{-1};
    bool firstUpdate{true};
    bool emissionComplete{};
    bool rotationAffectorInitialized{};
    bool attractionAffectorInitialized{};
    bool visible{true};
};

// ResetLevel clears live emitters and particles before CLevel::Load restores
// saved object visibility (0x003832d0, 0x00388450). Persistent emitters are
// portable renderer state, so their mutable values are captured explicitly.
struct LevelEffectCheckPointState {
    std::vector<PersistentEffectCheckPointState> persistentEffects;
};

// Portable one-shot effect state created by CCinematicThread::PlayEffect
// (0x003712f8). It exposes renderer-neutral billboard particles only.
class LevelEffectRuntime final {
public:
    LevelEffectRuntime();
    ~LevelEffectRuntime();
    LevelEffectRuntime(const LevelEffectRuntime&) = delete;
    LevelEffectRuntime& operator=(const LevelEffectRuntime&) = delete;

    [[nodiscard]] Result initialize(
        const EffectPresetDatabase& presets,
        NativeRandomizer* nativeRandomizer = nullptr);
    [[nodiscard]] Result applyCinematicCommand(const CinematicCommand& command);
    [[nodiscard]] Result playEffect(std::string_view effectType,
                                    const assets::Vector3& origin,
                                    std::int32_t roomId = -1);
    [[nodiscard]] Result addPersistentEffect(std::string_view effectType,
                                             const assets::Vector3& origin,
                                             std::int32_t roomId,
                                             bool visible = true,
                                             std::int32_t sourceObjectId = -1);
    [[nodiscard]] Result setPersistentEffectVisible(std::int32_t sourceObjectId,
                                                    bool visible) noexcept;
    [[nodiscard]] LevelEffectCheckPointState saveCheckPointState() const;
    [[nodiscard]] Result loadCheckPointState(
        const LevelEffectCheckPointState& state);
    void update(std::uint32_t elapsedMilliseconds,
                std::span<const bool> roomVisibility = {}) noexcept;

    [[nodiscard]] std::span<const EffectParticleState> particles()
        const noexcept {
        return renderParticles_;
    }

private:
    struct EmitterRuntimeState;
    struct PendingEmitter;
    struct PersistentEmitter;
    struct Particle;

    void initializeEmitter(EmitterRuntimeState& emitter,
                           const EffectEmitterPreset& preset,
                           const assets::Vector3& origin, std::int32_t roomId,
                           bool restartAfterClone) noexcept;
    void selectRandomLifetimes(EmitterRuntimeState& emitter) noexcept;
    void restartEmitter(EmitterRuntimeState& emitter) noexcept;
    void updateEmitter(EmitterRuntimeState& emitter,
                       std::uint32_t elapsedMilliseconds) noexcept;
    void updateEmitterParticles(EmitterRuntimeState& emitter,
                                std::uint32_t elapsedMilliseconds) noexcept;
    void spawnParticle(EmitterRuntimeState& emitter) noexcept;
    void eraseEmitterParticles(std::uint64_t emitterId) noexcept;
    [[nodiscard]] std::int32_t signedModulo(std::int32_t divisor) noexcept;
    [[nodiscard]] bool emitterHasParticles(
        std::uint64_t emitterId) const noexcept;
    void rebuildRenderParticles() noexcept;

    const EffectPresetDatabase* presets_{};
    std::vector<PendingEmitter> pendingEmitters_;
    std::vector<PersistentEmitter> persistentEmitters_;
    std::vector<Particle> particles_;
    std::vector<EffectParticleState> renderParticles_;
    NativeRandomizer ownedNativeRandomizer_;
    NativeRandomizer* nativeRandomizer_{&ownedNativeRandomizer_};
    std::uint64_t nextEmitterId_{1};
};

}  // namespace usm::game
