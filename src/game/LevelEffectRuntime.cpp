#include "game/LevelEffectRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace usm::game {
namespace {

assets::Vector3 parseVector3(std::string_view text, bool& valid) noexcept {
    std::string storage(text);
    std::replace(storage.begin(), storage.end(), ',', ' ');
    const char* cursor = storage.c_str();
    char* end = nullptr;
    assets::Vector3 result;
    valid = true;
    for (float* component : {&result.x, &result.y, &result.z}) {
        *component = std::strtof(cursor, &end);
        if (end == cursor || !std::isfinite(*component)) {
            valid = false;
            return {};
        }
        cursor = end;
    }
    return result;
}

std::uint8_t colorChannel(std::uint32_t argb, unsigned shift) noexcept {
    return static_cast<std::uint8_t>((argb >> shift) & 0xffU);
}

std::uint32_t interpolateColor(std::uint32_t from, std::uint32_t to,
                               float progress) noexcept {
    progress = std::clamp(progress, 0.0F, 1.0F);
    std::uint32_t result{};
    for (const unsigned shift : {0U, 8U, 16U, 24U}) {
        const float start = static_cast<float>(colorChannel(from, shift));
        const float end = static_cast<float>(colorChannel(to, shift));
        // SColor::getInterpolated (0x0039bebc) clamps then converts with
        // vcvt.u32.f32, which truncates rather than rounds each channel.
        result |= static_cast<std::uint32_t>(std::clamp(
                      start + (end - start) * progress, 0.0F, 255.0F))
                  << shift;
    }
    return result;
}

void rotateAroundX(assets::Vector3& position, float degrees,
                   const assets::Vector3& pivot) noexcept {
    constexpr float degreesToRadians = 0.017453292519943295F;
    const float angle = degrees * degreesToRadians;
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    const float y = position.y - pivot.y;
    const float z = position.z - pivot.z;
    position.y = y * cosine - z * sine + pivot.y;
    position.z = z * cosine + y * sine + pivot.z;
}

void rotateAroundY(assets::Vector3& position, float degrees,
                   const assets::Vector3& pivot) noexcept {
    constexpr float degreesToRadians = 0.017453292519943295F;
    const float angle = degrees * degreesToRadians;
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    const float x = position.x - pivot.x;
    const float z = position.z - pivot.z;
    position.x = x * cosine - z * sine + pivot.x;
    position.z = z * cosine + x * sine + pivot.z;
}

void rotateAroundZ(assets::Vector3& position, float degrees,
                   const assets::Vector3& pivot) noexcept {
    constexpr float degreesToRadians = 0.017453292519943295F;
    const float angle = degrees * degreesToRadians;
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    const float x = position.x - pivot.x;
    const float y = position.y - pivot.y;
    position.x = x * cosine - y * sine + pivot.x;
    position.y = y * cosine + x * sine + pivot.y;
}

}  // namespace

struct LevelEffectRuntime::EmitterRuntimeState {
    const EffectEmitterPreset* preset{};
    assets::Vector3 origin;
    std::uint64_t id{};
    std::uint32_t emissionAccumulatorMilliseconds{};
    std::uint32_t activeElapsedMilliseconds{};
    std::uint32_t restartElapsedMilliseconds{};
    std::uint32_t startDelayElapsedMilliseconds{};
    std::uint32_t suspendedElapsedMilliseconds{};
    std::int32_t selectedSystemLifetimeMilliseconds{-1};
    std::int32_t selectedRestartMilliseconds{-1};
    std::int32_t roomId{-1};
    bool firstUpdate{true};
    bool emissionComplete{};
    bool rotationAffectorInitialized{};
    bool attractionAffectorInitialized{};
};

struct LevelEffectRuntime::PendingEmitter {
    EmitterRuntimeState runtime;
};

struct LevelEffectRuntime::PersistentEmitter {
    EmitterRuntimeState runtime;
    std::int32_t sourceObjectId{-1};
    bool visible{true};
};

struct LevelEffectRuntime::Particle {
    struct ColorTarget {
        std::uint32_t startColor{0xffffffffU};
        bool initialized{};
    };

    struct SizeTarget {
        float startWidth{};
        float startHeight{};
        float deltaWidth{};
        float deltaHeight{};
        bool initialized{};
    };

    const EffectEmitterPreset* preset{};
    std::uint64_t emitterId{};
    assets::Vector3 position;
    assets::Vector3 previousPosition;
    assets::Vector3 velocity;
    assets::Vector3 gravityStartVelocity;
    assets::Vector3 rotationPivot;
    std::uint32_t ageMilliseconds{};
    std::uint32_t lifetimeMilliseconds{};
    float width{};
    float height{};
    std::vector<ColorTarget> colorTargets;
    std::vector<SizeTarget> sizeTargets;
    float rotationDegrees{};
    float spinBaseRotationDegrees{};
    float spinDeltaDegrees{};
    std::uint32_t color{0xffffffffU};
    bool gravityInitialized{};
    bool spinInitialized{};
    bool spawnedThisUpdate{true};
    bool visible{true};
    std::int32_t roomId{-1};
    assets::Vector3 emitterPosition;
};

LevelEffectRuntime::LevelEffectRuntime() = default;
LevelEffectRuntime::~LevelEffectRuntime() = default;

Result LevelEffectRuntime::initialize(const EffectPresetDatabase& presets,
                                      NativeRandomizer* nativeRandomizer) {
    if (presets.presets().empty()) {
        return Result::failure("Effect runtime has no presets");
    }
    presets_ = &presets;
    pendingEmitters_.clear();
    persistentEmitters_.clear();
    particles_.clear();
    renderParticles_.clear();
    if (nativeRandomizer == nullptr) {
        ownedNativeRandomizer_ = NativeRandomizer{};
        nativeRandomizer_ = &ownedNativeRandomizer_;
    } else {
        nativeRandomizer_ = nativeRandomizer;
    }
    nextEmitterId_ = 1;
    return Result::success();
}

Result LevelEffectRuntime::applyCinematicCommand(
    const CinematicCommand& command) {
    if (command.name != "PlayEffect") {
        return Result::success();
    }
    if (presets_ == nullptr) {
        return Result::failure("Effect runtime is not initialized");
    }
    const CinematicAttribute* type = command.findAttribute("$EffectType");
    const CinematicAttribute* position = command.findAttribute("abspos");
    bool validPosition = false;
    const assets::Vector3 origin =
        position == nullptr ? assets::Vector3{}
                            : parseVector3(position->value, validPosition);
    if (type == nullptr || type->value.empty() || position == nullptr ||
        !validPosition) {
        return Result::failure("PlayEffect has invalid type or position");
    }
    return playEffect(type->value, origin, -1);
}

Result LevelEffectRuntime::playEffect(std::string_view effectType,
                                      const assets::Vector3& origin,
                                      std::int32_t roomId) {
    if (presets_ == nullptr) {
        return Result::failure("Effect runtime is not initialized");
    }
    const EffectPreset* preset = presets_->find(effectType);
    if (preset == nullptr) {
        return Result::failure("Effect references an unknown preset");
    }
    for (const EffectEmitterPreset& emitter : preset->emitters) {
        PendingEmitter pending;
        initializeEmitter(pending.runtime, emitter, origin, roomId, true);
        pendingEmitters_.push_back(std::move(pending));
    }
    return Result::success();
}

Result LevelEffectRuntime::addPersistentEffect(std::string_view effectType,
                                               const assets::Vector3& origin,
                                               std::int32_t roomId,
                                               bool visible,
                                               std::int32_t sourceObjectId) {
    if (presets_ == nullptr) {
        return Result::failure("Effect runtime is not initialized");
    }
    const EffectPreset* preset = presets_->find(effectType);
    if (preset == nullptr) {
        return Result::failure(
            "Persistent effect references an unknown preset");
    }
    for (const EffectEmitterPreset& emitter : preset->emitters) {
        PersistentEmitter persistent;
        initializeEmitter(persistent.runtime, emitter, origin, roomId, false);
        persistent.sourceObjectId = sourceObjectId;
        persistent.visible = visible;
        persistentEmitters_.push_back(std::move(persistent));
    }
    return Result::success();
}

Result LevelEffectRuntime::setPersistentEffectVisible(
    std::int32_t sourceObjectId, bool visible) noexcept {
    bool found = false;
    for (PersistentEmitter& emitter : persistentEmitters_) {
        if (emitter.sourceObjectId != sourceObjectId) {
            continue;
        }
        emitter.visible = visible;
        for (Particle& particle : particles_) {
            if (particle.emitterId == emitter.runtime.id) {
                particle.visible = visible;
            }
        }
        found = true;
    }
    return found ? Result::success()
                 : Result::failure("Persistent effect source was not found");
}

LevelEffectCheckPointState LevelEffectRuntime::saveCheckPointState() const {
    LevelEffectCheckPointState result;
    result.persistentEffects.reserve(persistentEmitters_.size());
    for (const PersistentEmitter& emitter : persistentEmitters_) {
        const EmitterRuntimeState& runtime = emitter.runtime;
        result.persistentEffects.push_back(
            {emitter.sourceObjectId, runtime.emissionAccumulatorMilliseconds,
             runtime.activeElapsedMilliseconds,
             runtime.restartElapsedMilliseconds,
             runtime.startDelayElapsedMilliseconds,
             runtime.suspendedElapsedMilliseconds,
             runtime.selectedSystemLifetimeMilliseconds,
             runtime.selectedRestartMilliseconds, runtime.firstUpdate,
             runtime.emissionComplete, runtime.rotationAffectorInitialized,
             runtime.attractionAffectorInitialized, emitter.visible});
    }
    return result;
}

Result LevelEffectRuntime::loadCheckPointState(
    const LevelEffectCheckPointState& state) {
    if (state.persistentEffects.size() != persistentEmitters_.size()) {
        return Result::failure(
            "Checkpoint effect state does not match the loaded level");
    }
    for (std::size_t index = 0; index < persistentEmitters_.size(); ++index) {
        PersistentEmitter& emitter = persistentEmitters_[index];
        EmitterRuntimeState& runtime = emitter.runtime;
        const PersistentEffectCheckPointState& saved =
            state.persistentEffects[index];
        if (saved.sourceObjectId != emitter.sourceObjectId) {
            return Result::failure(
                "Checkpoint effect state references a different emitter");
        }
        runtime.emissionAccumulatorMilliseconds =
            saved.emissionAccumulatorMilliseconds;
        runtime.activeElapsedMilliseconds = saved.activeElapsedMilliseconds;
        runtime.restartElapsedMilliseconds = saved.restartElapsedMilliseconds;
        runtime.startDelayElapsedMilliseconds =
            saved.startDelayElapsedMilliseconds;
        runtime.suspendedElapsedMilliseconds =
            saved.suspendedElapsedMilliseconds;
        runtime.selectedSystemLifetimeMilliseconds =
            saved.selectedSystemLifetimeMilliseconds;
        runtime.selectedRestartMilliseconds = saved.selectedRestartMilliseconds;
        runtime.firstUpdate = saved.firstUpdate;
        runtime.emissionComplete = saved.emissionComplete;
        runtime.rotationAffectorInitialized = saved.rotationAffectorInitialized;
        runtime.attractionAffectorInitialized =
            saved.attractionAffectorInitialized;
        emitter.visible = saved.visible;
    }
    pendingEmitters_.clear();
    particles_.clear();
    renderParticles_.clear();
    return Result::success();
}

void LevelEffectRuntime::update(
    std::uint32_t elapsedMilliseconds,
    std::span<const bool> roomVisibility) noexcept {
    const auto roomIsVisible = [roomVisibility](std::int32_t roomId) {
        return roomVisibility.empty() || roomId < 1 ||
               static_cast<std::size_t>(roomId) > roomVisibility.size() ||
               roomVisibility[static_cast<std::size_t>(roomId - 1)];
    };
    const auto updateVisibleEmitter =
        [this, elapsedMilliseconds](EmitterRuntimeState& emitter,
                                    bool visible) {
            if (!visible) {
                if (!emitter.firstUpdate) {
                    emitter.suspendedElapsedMilliseconds +=
                        elapsedMilliseconds;
                }
                return;
            }
            const std::uint32_t resumedElapsed =
                elapsedMilliseconds + emitter.suspendedElapsedMilliseconds;
            emitter.suspendedElapsedMilliseconds = 0;
            updateEmitter(emitter, resumedElapsed);
        };
    for (PersistentEmitter& emitter : persistentEmitters_) {
        updateVisibleEmitter(
            emitter.runtime,
            emitter.visible && roomIsVisible(emitter.runtime.roomId));
    }

    for (PendingEmitter& emitter : pendingEmitters_) {
        updateVisibleEmitter(emitter.runtime,
                             roomIsVisible(emitter.runtime.roomId));
    }
    std::erase_if(pendingEmitters_, [this](const PendingEmitter& emitter) {
        return emitter.runtime.emissionComplete &&
               !emitterHasParticles(emitter.runtime.id);
    });

    rebuildRenderParticles();
}

void LevelEffectRuntime::initializeEmitter(EmitterRuntimeState& emitter,
                                           const EffectEmitterPreset& preset,
                                           const assets::Vector3& origin,
                                           std::int32_t roomId,
                                           bool restartAfterClone) noexcept {
    emitter = {};
    emitter.preset = &preset;
    emitter.origin = origin;
    emitter.id = nextEmitterId_++;
    emitter.roomId = roomId;
    emitter.selectedSystemLifetimeMilliseconds = -1;
    emitter.selectedRestartMilliseconds = -1;

    // CFpsParticleSystemSceneNode::clone (0x003a0c78) calls
    // SetRandomLifeTime after copying the four authored ranges. A thrown
    // CEffect then immediately calls child Restart through CEffect::Restart
    // (0x0030aea0), consuming and replacing both selections a second time.
    selectRandomLifetimes(emitter);
    if (restartAfterClone) {
        restartEmitter(emitter);
    }
}

void LevelEffectRuntime::selectRandomLifetimes(
    EmitterRuntimeState& emitter) noexcept {
    const EffectEmitterPreset& preset = *emitter.preset;
    const auto select = [this](std::int32_t minimum, std::int32_t maximum) {
        return minimum == maximum ? minimum
                                  : minimum + signedModulo(maximum - minimum);
    };
    // CFpsParticleSystemSceneNode::SetRandomLifeTime (0x0039fae4).
    emitter.selectedSystemLifetimeMilliseconds =
        select(preset.systemMinimumLifetimeMilliseconds,
               preset.systemMaximumLifetimeMilliseconds);
    emitter.selectedRestartMilliseconds = select(
        preset.restartMinimumMilliseconds, preset.restartMaximumMilliseconds);
}

void LevelEffectRuntime::restartEmitter(EmitterRuntimeState& emitter) noexcept {
    // CFpsParticleSystemSceneNode::Restart (0x0039fb4c) clears the particle
    // array and emitter accumulator, resets the node clock, then chooses new
    // system and restart lifetimes.
    eraseEmitterParticles(emitter.id);
    emitter.emissionAccumulatorMilliseconds = 0;
    emitter.activeElapsedMilliseconds = 0;
    emitter.restartElapsedMilliseconds = 0;
    emitter.startDelayElapsedMilliseconds = 0;
    emitter.suspendedElapsedMilliseconds = 0;
    emitter.firstUpdate = true;
    emitter.emissionComplete = false;
    emitter.rotationAffectorInitialized = false;
    emitter.attractionAffectorInitialized = false;
    selectRandomLifetimes(emitter);
}

void LevelEffectRuntime::updateEmitter(
    EmitterRuntimeState& emitter, std::uint32_t elapsedMilliseconds) noexcept {
    const EffectEmitterPreset& preset = *emitter.preset;
    const std::uint32_t deltaMilliseconds =
        emitter.firstUpdate ? 0U : elapsedMilliseconds;
    emitter.firstUpdate = false;

    // CFpsParticleSystemSceneNode::doParticleSystem (0x0039f34c) updates its
    // last timestamp but rejects any frame delta above 150 ms wholesale.
    if (deltaMilliseconds > 150U) {
        return;
    }

    bool emitting = false;
    if (!emitter.emissionComplete) {
        if (static_cast<std::int64_t>(emitter.startDelayElapsedMilliseconds) <
            static_cast<std::int64_t>(preset.startDelayMilliseconds)) {
            emitter.startDelayElapsedMilliseconds += deltaMilliseconds;
        } else if (preset.systemMinimumLifetimeMilliseconds == -1 ||
                   preset.systemMaximumLifetimeMilliseconds == -1 ||
                   emitter.activeElapsedMilliseconds <=
                       static_cast<std::uint32_t>(
                           emitter.selectedSystemLifetimeMilliseconds)) {
            emitting = true;
        } else {
            if (preset.restartMinimumMilliseconds != -1 &&
                preset.restartMaximumMilliseconds != -1) {
                emitter.restartElapsedMilliseconds += deltaMilliseconds;
                if (static_cast<std::uint32_t>(
                        emitter.selectedRestartMilliseconds) <
                    emitter.restartElapsedMilliseconds) {
                    restartEmitter(emitter);
                    return;
                }
            } else {
                emitter.emissionComplete = true;
            }
        }
    }

    if (emitting) {
        std::uint32_t emitterDelta = deltaMilliseconds;
        // Negative StartDelay is a native pre-roll request. On the node's
        // zero-delta first tick, doParticleSystem passes -StartDelay to the
        // emitter without aging the resulting particles (0x0039f4a0).
        if (deltaMilliseconds == 0U && preset.startDelayMilliseconds < 0) {
            emitterDelta = static_cast<std::uint32_t>(
                -static_cast<std::int64_t>(preset.startDelayMilliseconds));
        }
        emitter.emissionAccumulatorMilliseconds += emitterDelta;

        std::int32_t particlesPerSecond = preset.minimumParticlesPerSecond;
        const std::int32_t rateDifference =
            preset.maximumParticlesPerSecond - preset.minimumParticlesPerSecond;
        if (rateDifference != 0) {
            particlesPerSecond += signedModulo(rateDifference);
        }
        const float intervalMilliseconds =
            1000.0F / static_cast<float>(particlesPerSecond);
        if (static_cast<float>(emitter.emissionAccumulatorMilliseconds) >
            intervalMilliseconds) {
            const float roundedCount =
                static_cast<float>(emitter.emissionAccumulatorMilliseconds) /
                    intervalMilliseconds +
                0.5F;
            std::uint32_t count = roundedCount > 0.0F
                                      ? static_cast<std::uint32_t>(roundedCount)
                                      : 0U;
            count = std::min(count, static_cast<std::uint32_t>(
                                        preset.maximumParticlesPerSecond * 2));
            const std::size_t existingCount = static_cast<std::size_t>(
                std::count_if(particles_.begin(), particles_.end(),
                              [&emitter](const Particle& particle) {
                                  return particle.emitterId == emitter.id;
                              }));
            constexpr std::size_t kNativeNodeParticleLimit = 0x3f7aU;
            count = static_cast<std::uint32_t>(std::min<std::size_t>(
                count, kNativeNodeParticleLimit -
                           std::min(existingCount, kNativeNodeParticleLimit)));
            emitter.emissionAccumulatorMilliseconds = 0;
            for (std::uint32_t index = 0; index < count; ++index) {
                spawnParticle(emitter);
            }
        }
    }

    updateEmitterParticles(emitter, deltaMilliseconds);
    if (emitting) {
        emitter.activeElapsedMilliseconds += deltaMilliseconds;
    }
}

void LevelEffectRuntime::updateEmitterParticles(
    EmitterRuntimeState& emitter, std::uint32_t elapsedMilliseconds) noexcept {
    const EffectEmitterPreset& preset = *emitter.preset;
    const bool rotateParticles =
        preset.hasRotation && emitter.rotationAffectorInitialized;
    if (preset.hasRotation) {
        // CFpsParticleRotationAffector::affect (0x0039df08) only records the
        // current time on its first invocation; rotation begins next tick.
        emitter.rotationAffectorInitialized = true;
    }
    const bool attractParticles = !preset.attractionAffectors.empty() &&
                                  emitter.attractionAffectorInitialized;
    if (!preset.attractionAffectors.empty()) {
        // CFpsParticleAttractionAffector::affect (0x0039bda0) uses a zero
        // previous timestamp as its initialization sentinel, just like the
        // rotation affector: its first invocation only records the clock.
        emitter.attractionAffectorInitialized = true;
    }

    const auto intervalTime = [](std::uint32_t lifetime, std::int32_t percent) {
        const float value =
            static_cast<float>(lifetime) * static_cast<float>(percent) * 0.01F;
        return value > 0.0F ? static_cast<std::uint32_t>(value) : 0U;
    };
    const auto isActive = [](std::int64_t previous, std::uint32_t current,
                             std::uint32_t start, std::uint32_t end) {
        return start <= current &&
               (previous < static_cast<std::int64_t>(end) || current <= end);
    };
    const auto isEntry = [](std::int64_t previous, std::uint32_t current,
                            std::uint32_t start) {
        return previous < static_cast<std::int64_t>(start) || current == start;
    };
    const auto progress = [](std::uint32_t current, std::uint32_t start,
                             std::uint32_t end) {
        if (end <= start) {
            return 1.0F;
        }
        return static_cast<float>(std::min(current, end) - start) /
               static_cast<float>(end - start);
    };

    for (Particle& particle : particles_) {
        if (particle.emitterId != emitter.id) {
            continue;
        }
        if (preset.directionalRotation) {
            // doParticleSystem copies Pos into DirectionalOldPos before any
            // affector or velocity integration (0x0039f5e0-0x0039f602).
            // render then uses Pos-DirectionalOldPos for both directional
            // billboard paths (0x003a0206, 0x003a0274).
            particle.previousPosition = particle.position;
        }
        const std::int64_t previousAge =
            particle.spawnedThisUpdate
                ? -static_cast<std::int64_t>(elapsedMilliseconds)
                : static_cast<std::int64_t>(particle.ageMilliseconds);
        const std::uint32_t currentAge =
            particle.spawnedThisUpdate
                ? 0U
                : particle.ageMilliseconds + elapsedMilliseconds;

        for (const EffectAffectorReference& reference : preset.affectorOrder) {
            if (reference.kind == EffectAffectorKind::FadeOut) {
                const std::size_t index = reference.index;
                const EffectColorAffector& affector =
                    preset.colorAffectors[index];
                const std::uint32_t start = intervalTime(
                    particle.lifetimeMilliseconds, affector.startPercent);
                const std::uint32_t end = intervalTime(
                    particle.lifetimeMilliseconds, affector.endPercent);
                if (!isActive(previousAge, currentAge, start, end)) {
                    continue;
                }
                Particle::ColorTarget& target = particle.colorTargets[index];
                if (isEntry(previousAge, currentAge, start)) {
                    target.startColor = particle.color;
                    target.initialized = true;
                }
                if (target.initialized) {
                    particle.color = interpolateColor(
                        target.startColor, affector.targetColor,
                        progress(currentAge, start, end));
                }
                continue;
            }

            if (reference.kind == EffectAffectorKind::Gravity) {
                const std::uint32_t start = intervalTime(
                    particle.lifetimeMilliseconds, preset.gravityStartPercent);
                const std::uint32_t end = intervalTime(
                    particle.lifetimeMilliseconds, preset.gravityEndPercent);
                if (isActive(previousAge, currentAge, start, end)) {
                    if (isEntry(previousAge, currentAge, start)) {
                        particle.gravityStartVelocity = particle.velocity;
                        particle.gravityInitialized = true;
                    }
                    if (particle.gravityInitialized) {
                        const float amount = progress(currentAge, start, end);
                        particle.velocity.x =
                            particle.gravityStartVelocity.x +
                            (preset.gravity.x -
                             particle.gravityStartVelocity.x) *
                                amount;
                        particle.velocity.y =
                            particle.gravityStartVelocity.y +
                            (preset.gravity.y -
                             particle.gravityStartVelocity.y) *
                                amount;
                        particle.velocity.z =
                            particle.gravityStartVelocity.z +
                            (preset.gravity.z -
                             particle.gravityStartVelocity.z) *
                                amount;
                    }
                }
                continue;
            }

            if (reference.kind == EffectAffectorKind::Rotate &&
                rotateParticles) {
                const float seconds =
                    static_cast<float>(elapsedMilliseconds) * 0.001F;
                // CFpsParticleRotationAffector::affect (0x0039df08) applies YZ,
                // XZ, then XY rotations, corresponding to X, Y, then Z axes.
                rotateAroundX(particle.position,
                              preset.rotationSpeedDegreesPerSecond.x * seconds,
                              particle.rotationPivot);
                rotateAroundY(particle.position,
                              preset.rotationSpeedDegreesPerSecond.y * seconds,
                              particle.rotationPivot);
                rotateAroundZ(particle.position,
                              preset.rotationSpeedDegreesPerSecond.z * seconds,
                              particle.rotationPivot);
                continue;
            }

            if (reference.kind == EffectAffectorKind::Spin) {
                const std::uint32_t start = intervalTime(
                    particle.lifetimeMilliseconds, preset.spinStartPercent);
                const std::uint32_t end = intervalTime(
                    particle.lifetimeMilliseconds, preset.spinEndPercent);
                if (isActive(previousAge, currentAge, start, end)) {
                    if (isEntry(previousAge, currentAge, start)) {
                        std::int32_t chosen = preset.spinMinimumDegrees;
                        const std::int32_t difference =
                            preset.spinMaximumDegrees -
                            preset.spinMinimumDegrees;
                        if (difference > 0) {
                            chosen += signedModulo(difference);
                        } else if (difference < 0) {
                            // The negative-divisor branch explicitly negates
                            // the ARM remainder at 0x0039ea98.
                            chosen -= signedModulo(difference);
                        }
                        particle.spinBaseRotationDegrees =
                            particle.rotationDegrees;
                        particle.spinDeltaDegrees = static_cast<float>(chosen);
                        particle.spinInitialized = true;
                    }
                    if (particle.spinInitialized) {
                        particle.rotationDegrees =
                            particle.spinBaseRotationDegrees +
                            particle.spinDeltaDegrees *
                                progress(currentAge, start, end);
                    }
                }
                continue;
            }

            if (reference.kind == EffectAffectorKind::Attract &&
                attractParticles) {
                const EffectAttractionAffector& affector =
                    preset.attractionAffectors[reference.index];
                // EffectManager::InitEffect (0x00391d90) translates attraction
                // points by the thrown effect root, not by the child emitter.
                const assets::Vector3 target{
                    emitter.origin.x + affector.point.x,
                    emitter.origin.y + affector.point.y,
                    emitter.origin.z + affector.point.z,
                };
                float x = target.x - particle.position.x;
                float y = target.y - particle.position.y;
                float z = target.z - particle.position.z;
                const float length = std::sqrt(x * x + y * y + z * z);
                if (length > 0.0F) {
                    const float amount =
                        static_cast<float>(elapsedMilliseconds) * 0.001F *
                        affector.speed * (affector.attract ? 1.0F : -1.0F) /
                        length;
                    if (affector.affectX) {
                        particle.position.x += x * amount;
                    }
                    if (affector.affectY) {
                        particle.position.y += y * amount;
                    }
                    if (affector.affectZ) {
                        particle.position.z += z * amount;
                    }
                }
                continue;
            }

            if (reference.kind == EffectAffectorKind::Size) {
                const std::size_t index = reference.index;
                const EffectSizeAffector& affector =
                    preset.sizeAffectors[index];
                const std::uint32_t start = intervalTime(
                    particle.lifetimeMilliseconds, affector.startPercent);
                const std::uint32_t end = intervalTime(
                    particle.lifetimeMilliseconds, affector.endPercent);
                if (!isActive(previousAge, currentAge, start, end)) {
                    continue;
                }
                Particle::SizeTarget& target = particle.sizeTargets[index];
                if (isEntry(previousAge, currentAge, start)) {
                    target.startWidth = particle.width;
                    target.startHeight = particle.height;
                    float targetWidth = affector.targetWidth;
                    float targetHeight = affector.targetHeight;
                    if (affector.variationPercent > 0) {
                        const std::int32_t variation =
                            signedModulo(affector.variationPercent * 2) -
                            affector.variationPercent;
                        const float scale =
                            1.0F + static_cast<float>(variation) * 0.01F;
                        targetWidth *= scale;
                        targetHeight *= scale;
                    }
                    target.deltaWidth = targetWidth - target.startWidth;
                    target.deltaHeight = targetHeight - target.startHeight;
                    target.initialized = true;
                }
                if (target.initialized) {
                    const float amount = progress(currentAge, start, end);
                    particle.width =
                        target.startWidth + target.deltaWidth * amount;
                    particle.height =
                        target.startHeight + target.deltaHeight * amount;
                }
                continue;
            }
        }

        particle.position.x +=
            particle.velocity.x * static_cast<float>(elapsedMilliseconds);
        particle.position.y +=
            particle.velocity.y * static_cast<float>(elapsedMilliseconds);
        particle.position.z +=
            particle.velocity.z * static_cast<float>(elapsedMilliseconds);
        particle.ageMilliseconds = currentAge;
        particle.spawnedThisUpdate = false;
    }

    // The native removal check is strict EndTime < now (0x0039f674), so a
    // particle remains renderable at its exact authored endpoint.
    std::erase_if(particles_, [&emitter](const Particle& particle) {
        return particle.emitterId == emitter.id &&
               particle.lifetimeMilliseconds < particle.ageMilliseconds;
    });
}

void LevelEffectRuntime::spawnParticle(EmitterRuntimeState& emitter) noexcept {
    const EffectEmitterPreset& preset = *emitter.preset;
    Particle particle;
    particle.preset = &preset;
    particle.emitterId = emitter.id;

    const auto boxCoordinate = [this](float halfExtent) {
        // The Box attribute is converted to [-Box,+Box] by
        // CFpsParticleBoxEmitter::deserializeAttributes (0x0039c678), and
        // emitt always consumes one rand call per component (0x0039cd9a).
        const std::int32_t random = nativeRandomizer_->next();
        const float span = halfExtent * 2.0F;
        return span == 0.0F
                   ? -halfExtent
                   : -halfExtent + std::fmod(static_cast<float>(random), span);
    };
    const assets::Vector3 localOffset{boxCoordinate(preset.box.x),
                                      boxCoordinate(preset.box.y),
                                      boxCoordinate(preset.box.z)};
    // doParticleSystem transforms global particle positions by the emitter
    // node's absolute matrix (0x0039f55c). The authored node Scale therefore
    // scales its box distribution, but never the billboard dimensions.
    particle.position = {
        emitter.origin.x + preset.position.x + localOffset.x * preset.scale.x,
        emitter.origin.y + preset.position.y + localOffset.y * preset.scale.y,
        emitter.origin.z + preset.position.z + localOffset.z * preset.scale.z,
    };
    particle.previousPosition = particle.position;
    // STransparentNodeEntry (0x00398570) measures the particle scene node's
    // absolute transform against the camera. The node owns the serialized
    // emitter Position; box-distributed particles are not separate entries.
    particle.emitterPosition = {
        emitter.origin.x + preset.position.x,
        emitter.origin.y + preset.position.y,
        emitter.origin.z + preset.position.z,
    };

    const std::int32_t initialRotationDifference =
        preset.initialRotationMaximumDegrees -
        preset.initialRotationMinimumDegrees;
    std::int32_t initialRotation = preset.initialRotationMinimumDegrees;
    if (initialRotationDifference != 0) {
        initialRotation += signedModulo(initialRotationDifference);
    }

    float sizeScale = 1.0F;
    if (preset.sizeVariationPercent != 0) {
        const std::int32_t variation =
            signedModulo(preset.sizeVariationPercent * 2) -
            preset.sizeVariationPercent;
        sizeScale += static_cast<float>(variation) * 0.01F;
    }
    particle.width = preset.particleWidth * sizeScale;
    particle.height = preset.particleHeight * sizeScale;

    particle.velocity = preset.direction;
    const auto rotateDirection = [this, &particle](std::int32_t maximumDegrees,
                                                   auto rotate) {
        if (maximumDegrees != 0) {
            const std::int32_t degrees =
                signedModulo(maximumDegrees * 2) - maximumDegrees;
            rotate(particle.velocity, static_cast<float>(degrees), {});
        }
    };
    rotateDirection(preset.maximumAngleDegreesXY, rotateAroundZ);
    rotateDirection(preset.maximumAngleDegreesYZ, rotateAroundX);
    rotateDirection(preset.maximumAngleDegreesXZ, rotateAroundY);

    if (preset.minimumParticleLifetimeMilliseconds ==
        preset.maximumParticleLifetimeMilliseconds) {
        particle.lifetimeMilliseconds = static_cast<std::uint32_t>(
            preset.minimumParticleLifetimeMilliseconds);
    } else {
        particle.lifetimeMilliseconds = static_cast<std::uint32_t>(
            preset.minimumParticleLifetimeMilliseconds +
            signedModulo(preset.maximumParticleLifetimeMilliseconds -
                         preset.minimumParticleLifetimeMilliseconds));
    }

    // Color selection unconditionally consumes rand()%100, even for equal
    // endpoints (0x0039cf46-0x0039cf54).
    particle.color = interpolateColor(
        preset.minimumStartColor, preset.maximumStartColor,
        // min.getInterpolated(max, d) returns max at d=0 and min at d=1
        // (0x0039bee4-0x0039bf00), hence the complement for this helper's
        // conventional min-at-zero interpolation direction.
        1.0F - static_cast<float>(signedModulo(100)) * 0.01F);

    if (preset.speedVariationPercent != 0) {
        // Native speed variation is positive-only, not symmetric:
        // Direction *= 1 + rand()%SpeedVariation/100 (0x0039cf78).
        const float speedScale =
            1.0F +
            static_cast<float>(signedModulo(preset.speedVariationPercent)) *
                0.01F;
        particle.velocity.x *= speedScale;
        particle.velocity.y *= speedScale;
        particle.velocity.z *= speedScale;
    }

    particle.gravityStartVelocity = particle.velocity;
    // EffectManager::InitEffect (0x00391d90) adds the effect root position to
    // Rotate-affector pivots. It does not add the child emitter Position.
    particle.rotationPivot = {
        emitter.origin.x + preset.rotationPivot.x,
        emitter.origin.y + preset.rotationPivot.y,
        emitter.origin.z + preset.rotationPivot.z,
    };
    particle.colorTargets.resize(preset.colorAffectors.size());
    particle.sizeTargets.resize(preset.sizeAffectors.size());
    particle.rotationDegrees = static_cast<float>(initialRotation);
    particle.roomId = emitter.roomId;
    particles_.push_back(std::move(particle));
}

void LevelEffectRuntime::eraseEmitterParticles(
    std::uint64_t emitterId) noexcept {
    std::erase_if(particles_, [emitterId](const Particle& particle) {
        return particle.emitterId == emitterId;
    });
}

std::int32_t LevelEffectRuntime::signedModulo(std::int32_t divisor) noexcept {
    return divisor == 0 ? 0 : nativeRandomizer_->next() % divisor;
}

bool LevelEffectRuntime::emitterHasParticles(
    std::uint64_t emitterId) const noexcept {
    return std::any_of(particles_.begin(), particles_.end(),
                       [emitterId](const Particle& particle) {
                           return particle.emitterId == emitterId;
                       });
}

void LevelEffectRuntime::rebuildRenderParticles() noexcept {
    renderParticles_.clear();
    renderParticles_.reserve(particles_.size());
    for (const Particle& particle : particles_) {
        if (!particle.visible) {
            continue;
        }
        renderParticles_.push_back(
            {particle.position,
             particle.previousPosition,
             particle.preset->direction,
             particle.width,
             particle.height,
             particle.rotationDegrees,
             particle.color,
             particle.preset->frameId,
             particle.preset->additive,
             particle.preset->directionalRotation,
             particle.preset->projectDirection,
             particle.preset->hasSpin,
             particle.roomId,
             particle.emitterId,
             particle.emitterPosition});
    }
}

}  // namespace usm::game
