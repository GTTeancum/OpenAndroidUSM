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
    std::uint32_t ageMilliseconds{};
    std::uint32_t lifetimeMilliseconds{};
    float initialWidth{};
    float initialHeight{};
    float rotationDegrees{};
    std::uint32_t startColor{0xffffffffU};
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
        // Fps gravity affectors interpolate velocity toward the configured
        // vector over lifetime; a frame-rate-independent blend preserves that
        // behavior without importing Irrlicht particle objects.
        const float gravityBlend =
            std::min(delta / static_cast<float>(particle.lifetimeMilliseconds),
                     1.0F);
        particle.velocity.x +=
            (particle.preset->gravity.x - particle.velocity.x) * gravityBlend;
        particle.velocity.y +=
            (particle.preset->gravity.y - particle.velocity.y) * gravityBlend;
        particle.velocity.z +=
            (particle.preset->gravity.z - particle.velocity.z) * gravityBlend;
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
