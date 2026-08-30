#include "game/LevelEffectRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace usm::game {
namespace {

assets::Vector3 parseVector3(std::string_view text,
                             bool& valid) noexcept {
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
        result |= static_cast<std::uint32_t>(
                      std::clamp(std::lround(start + (end - start) * progress),
                                 0L, 255L))
                  << shift;
    }
    return result;
}

float intervalProgress(float lifeProgress, std::int32_t startPercent,
                       std::int32_t endPercent) noexcept {
    const float start = static_cast<float>(startPercent) / 100.0F;
    const float end = static_cast<float>(endPercent) / 100.0F;
    if (lifeProgress < start) {
        return 0.0F;
    }
    return end <= start
               ? 1.0F
               : std::clamp((lifeProgress - start) / (end - start), 0.0F,
                            1.0F);
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

} // namespace

struct LevelEffectRuntime::PendingEmitter {
    const EffectEmitterPreset* preset{};
    assets::Vector3 origin;
    std::int32_t delayMilliseconds{};
};

struct LevelEffectRuntime::Particle {
    const EffectEmitterPreset* preset{};
    assets::Vector3 position;
    assets::Vector3 velocity;
    assets::Vector3 initialVelocity;
    assets::Vector3 rotationPivot;
    std::uint32_t ageMilliseconds{};
    std::uint32_t lifetimeMilliseconds{};
    float initialWidth{};
    float initialHeight{};
    float rotationDegrees{};
    float spinBaseRotationDegrees{};
    float spinDeltaDegrees{};
    std::uint32_t startColor{0xffffffffU};
    bool spinInitialized{};
};

LevelEffectRuntime::LevelEffectRuntime() = default;
LevelEffectRuntime::~LevelEffectRuntime() = default;

Result LevelEffectRuntime::initialize(const EffectPresetDatabase& presets) {
    if (presets.presets().empty()) {
        return Result::failure("Effect runtime has no presets");
    }
    presets_ = &presets;
    pendingEmitters_.clear();
    particles_.clear();
    renderParticles_.clear();
    randomState_ = 0x6d2b79f5U;
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
    const EffectPreset* preset = presets_->find(type->value);
    if (preset == nullptr) {
        return Result::failure("PlayEffect references an unknown preset");
    }
    for (const EffectEmitterPreset& emitter : preset->emitters) {
        pendingEmitters_.push_back(
            {&emitter, origin,
             std::max(emitter.startDelayMilliseconds, 0)});
    }
    return Result::success();
}

void LevelEffectRuntime::update(
    std::uint32_t elapsedMilliseconds) noexcept {
    for (auto iterator = pendingEmitters_.begin();
         iterator != pendingEmitters_.end();) {
        iterator->delayMilliseconds -=
            static_cast<std::int32_t>(elapsedMilliseconds);
        if (iterator->delayMilliseconds <= 0) {
            spawnEmitter(*iterator);
            iterator = pendingEmitters_.erase(iterator);
        } else {
            ++iterator;
        }
    }

    for (Particle& particle : particles_) {
        const float delta = static_cast<float>(elapsedMilliseconds);
        particle.ageMilliseconds = std::min(
            particle.lifetimeMilliseconds,
            particle.ageMilliseconds + elapsedMilliseconds);
        particle.position.x += particle.velocity.x * delta;
        particle.position.y += particle.velocity.y * delta;
        particle.position.z += particle.velocity.z * delta;
        const EffectEmitterPreset& preset = *particle.preset;
        const float lifeProgress =
            static_cast<float>(particle.ageMilliseconds) /
            static_cast<float>(particle.lifetimeMilliseconds);
        if (preset.hasGravity) {
            // CFpsParticleGravityAffector::affect (0x0039d7d4) captures the
            // entry velocity and linearly reaches Gravity at EndTime(%).
            const float progress =
                intervalProgress(lifeProgress, preset.gravityStartPercent,
                                 preset.gravityEndPercent);
            particle.velocity.x = particle.initialVelocity.x +
                                  (preset.gravity.x -
                                   particle.initialVelocity.x) *
                                      progress;
            particle.velocity.y = particle.initialVelocity.y +
                                  (preset.gravity.y -
                                   particle.initialVelocity.y) *
                                      progress;
            particle.velocity.z = particle.initialVelocity.z +
                                  (preset.gravity.z -
                                   particle.initialVelocity.z) *
                                      progress;
        }
        if (preset.hasRotation) {
            // CFpsParticleRotationAffector::affect (0x0039df08) applies the
            // three authored angular speeds in degrees per second.
            const float seconds = delta * 0.001F;
            rotateAroundX(particle.position,
                          preset.rotationSpeedDegreesPerSecond.x * seconds,
                          particle.rotationPivot);
            rotateAroundY(particle.position,
                          preset.rotationSpeedDegreesPerSecond.y * seconds,
                          particle.rotationPivot);
            rotateAroundZ(particle.position,
                          preset.rotationSpeedDegreesPerSecond.z * seconds,
                          particle.rotationPivot);
        }
        if (preset.hasSpin) {
            const float spinStart =
                static_cast<float>(preset.spinStartPercent) / 100.0F;
            if (lifeProgress >= spinStart && !particle.spinInitialized) {
                particle.spinBaseRotationDegrees = particle.rotationDegrees;
                particle.spinDeltaDegrees = std::floor(randomRange(
                    static_cast<float>(preset.spinMinimumDegrees),
                    static_cast<float>(preset.spinMaximumDegrees)));
                particle.spinInitialized = true;
            }
            if (particle.spinInitialized) {
                // CFpsParticleSpinAffector::affect (0x0039e9c4) treats the
                // chosen spin as a total angle across the authored interval.
                particle.rotationDegrees =
                    particle.spinBaseRotationDegrees +
                    particle.spinDeltaDegrees *
                        intervalProgress(lifeProgress,
                                         preset.spinStartPercent,
                                         preset.spinEndPercent);
            }
        }
    }
    std::erase_if(particles_, [](const Particle& particle) {
        return particle.ageMilliseconds >= particle.lifetimeMilliseconds;
    });

    renderParticles_.clear();
    renderParticles_.reserve(particles_.size());
    for (const Particle& particle : particles_) {
        const EffectEmitterPreset& preset = *particle.preset;
        const float lifeProgress =
            static_cast<float>(particle.ageMilliseconds) /
            static_cast<float>(particle.lifetimeMilliseconds);
        float fadeProgress = 0.0F;
        const float fadeStart =
            static_cast<float>(preset.fadeStartPercent) / 100.0F;
        const float fadeEnd =
            static_cast<float>(preset.fadeEndPercent) / 100.0F;
        if (lifeProgress >= fadeStart) {
            fadeProgress = fadeEnd <= fadeStart
                               ? 1.0F
                               : (lifeProgress - fadeStart) /
                                     (fadeEnd - fadeStart);
        }
        float width = particle.initialWidth;
        float height = particle.initialHeight;
        if (preset.targetWidth > 0.0F && preset.targetHeight > 0.0F) {
            const float sizeStart =
                static_cast<float>(preset.sizeStartPercent) / 100.0F;
            const float sizeEnd =
                static_cast<float>(preset.sizeEndPercent) / 100.0F;
            const float sizeProgress =
                sizeEnd <= sizeStart
                    ? 1.0F
                    : std::clamp((lifeProgress - sizeStart) /
                                     (sizeEnd - sizeStart),
                                 0.0F, 1.0F);
            width += (preset.targetWidth * preset.scale.x - width) *
                     sizeProgress;
            height += (preset.targetHeight * preset.scale.y - height) *
                      sizeProgress;
        }
        renderParticles_.push_back(
            {particle.position, width, height, particle.rotationDegrees,
             interpolateColor(particle.startColor, preset.fadeTargetColor,
                              fadeProgress),
             preset.frameId, preset.additive});
    }
}

void LevelEffectRuntime::spawnEmitter(
    const PendingEmitter& emitter) noexcept {
    const EffectEmitterPreset& preset = *emitter.preset;
    const float systemLifetimeSeconds =
        static_cast<float>(std::max(
            (preset.systemMinimumLifetimeMilliseconds +
             preset.systemMaximumLifetimeMilliseconds) /
                2,
            50)) /
        1000.0F;
    const float particlesPerSecond =
        static_cast<float>(preset.minimumParticlesPerSecond +
                           preset.maximumParticlesPerSecond) /
        2.0F;
    const std::int32_t particleCount = std::clamp(
        static_cast<std::int32_t>(
            std::lround(particlesPerSecond * systemLifetimeSeconds)),
        1, 128);
    particles_.reserve(particles_.size() +
                       static_cast<std::size_t>(particleCount));
    for (std::int32_t index = 0; index < particleCount; ++index) {
        Particle particle;
        particle.preset = &preset;
        particle.position = {
            emitter.origin.x + preset.position.x +
                randomRange(-preset.box.x * 0.5F, preset.box.x * 0.5F),
            emitter.origin.y + preset.position.y +
                randomRange(-preset.box.y * 0.5F, preset.box.y * 0.5F),
            emitter.origin.z + preset.position.z +
                randomRange(-preset.box.z * 0.5F, preset.box.z * 0.5F),
        };
        const float speedScale =
            1.0F + randomRange(-static_cast<float>(preset.speedVariationPercent),
                               static_cast<float>(preset.speedVariationPercent)) /
                       100.0F;
        particle.velocity = {preset.direction.x * speedScale,
                             preset.direction.y * speedScale,
                             preset.direction.z * speedScale};
        particle.initialVelocity = particle.velocity;
        particle.rotationPivot = {
            emitter.origin.x + preset.position.x + preset.rotationPivot.x,
            emitter.origin.y + preset.position.y + preset.rotationPivot.y,
            emitter.origin.z + preset.position.z + preset.rotationPivot.z,
        };
        particle.lifetimeMilliseconds = static_cast<std::uint32_t>(
            std::max(1.0F,
                     randomRange(
                         static_cast<float>(
                             preset.minimumParticleLifetimeMilliseconds),
                         static_cast<float>(
                             preset.maximumParticleLifetimeMilliseconds))));
        const float sizeScale =
            1.0F + randomRange(-static_cast<float>(preset.sizeVariationPercent),
                               static_cast<float>(preset.sizeVariationPercent)) /
                       100.0F;
        particle.initialWidth =
            preset.particleWidth * preset.scale.x * sizeScale;
        particle.initialHeight =
            preset.particleHeight * preset.scale.y * sizeScale;
        particle.rotationDegrees = randomRange(
            static_cast<float>(preset.initialRotationMinimumDegrees),
            static_cast<float>(preset.initialRotationMaximumDegrees));
        const float colorProgress = randomUnit();
        particle.startColor = interpolateColor(preset.minimumStartColor,
                                               preset.maximumStartColor,
                                               colorProgress);
        particles_.push_back(particle);
    }
}

float LevelEffectRuntime::randomUnit() noexcept {
    randomState_ ^= randomState_ << 13U;
    randomState_ ^= randomState_ >> 17U;
    randomState_ ^= randomState_ << 5U;
    return static_cast<float>(randomState_ & 0x00ffffffU) /
           static_cast<float>(0x01000000U);
}

float LevelEffectRuntime::randomRange(float minimum, float maximum) noexcept {
    if (maximum <= minimum) {
        return minimum;
    }
    return minimum + (maximum - minimum) * randomUnit();
}

} // namespace usm::game
