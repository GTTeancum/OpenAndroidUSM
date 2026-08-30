#include "game/EffectPreset.hpp"

#include <pugixml.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <string>

namespace usm::game {
namespace {

const pugi::xml_node namedAttribute(pugi::xml_node attributes,
                                    std::string_view name) noexcept {
    for (pugi::xml_node value : attributes.children()) {
        if (name == value.attribute("name").as_string()) {
            return value;
        }
    }
    return {};
}

std::string stringValue(pugi::xml_node attributes, std::string_view name,
                        std::string fallback = {}) {
    const pugi::xml_node value = namedAttribute(attributes, name);
    return value ? value.attribute("value").as_string() : std::move(fallback);
}

std::int32_t integerValue(pugi::xml_node attributes, std::string_view name,
                          std::int32_t fallback = 0) noexcept {
    const pugi::xml_node value = namedAttribute(attributes, name);
    return value ? value.attribute("value").as_int(fallback) : fallback;
}

float floatValue(pugi::xml_node attributes, std::string_view name,
                 float fallback = 0.0F) noexcept {
    const pugi::xml_node value = namedAttribute(attributes, name);
    return value ? value.attribute("value").as_float(fallback) : fallback;
}

assets::Vector3 vectorValue(pugi::xml_node attributes, std::string_view name,
                            assets::Vector3 fallback = {}) noexcept {
    const pugi::xml_node value = namedAttribute(attributes, name);
    if (!value) {
        return fallback;
    }
    std::string text = value.attribute("value").as_string();
    std::replace(text.begin(), text.end(), ',', ' ');
    const char* cursor = text.c_str();
    char* end = nullptr;
    assets::Vector3 parsed;
    for (float* component : {&parsed.x, &parsed.y, &parsed.z}) {
        *component = std::strtof(cursor, &end);
        if (end == cursor || !std::isfinite(*component)) {
            return fallback;
        }
        cursor = end;
    }
    return parsed;
}

std::uint32_t colorValue(pugi::xml_node value,
                         std::uint32_t fallback = 0xffffffffU) noexcept {
    if (!value) {
        return fallback;
    }
    const std::string_view text = value.attribute("value").as_string();
    std::uint32_t parsed{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(),
                                        parsed, 16);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size()
               ? parsed
               : fallback;
}

} // namespace

Result EffectPresetDatabase::load(std::span<const std::byte> bytes) {
    presets_.clear();
    if (bytes.empty()) {
        return Result::failure("Effect preset XML is empty");
    }
    pugi::xml_document document;
    const pugi::xml_parse_result parsed =
        document.load_buffer(bytes.data(), bytes.size(), pugi::parse_default,
                             pugi::encoding_auto);
    if (!parsed) {
        return Result::failure(std::string("Could not parse effect presets: ") +
                               parsed.description());
    }

    for (pugi::xml_node effect : document.children("effect")) {
        EffectPreset preset;
        preset.name = effect.attribute("name").as_string();
        if (preset.name.empty()) {
            presets_.clear();
            return Result::failure("Effect preset has no name");
        }
        for (pugi::xml_node particleSystem : effect.children("ps")) {
            const pugi::xml_node attributes =
                particleSystem.child("attributes");
            if (!attributes) {
                continue;
            }
            EffectEmitterPreset emitter;
            emitter.name = stringValue(attributes, "Name");
            emitter.position = vectorValue(attributes, "Position");
            emitter.scale = vectorValue(attributes, "Scale", {1.0F, 1.0F,
                                                                 1.0F});
            emitter.box = vectorValue(attributes, "Box");
            emitter.direction = vectorValue(attributes, "Direction");
            emitter.systemMinimumLifetimeMilliseconds =
                integerValue(attributes, "SysMinLifeTime");
            emitter.systemMaximumLifetimeMilliseconds =
                integerValue(attributes, "SysMaxLifeTime");
            emitter.startDelayMilliseconds =
                integerValue(attributes, "StartDelay");
            emitter.minimumParticlesPerSecond =
                integerValue(attributes, "MinParticlesPerSecond");
            emitter.maximumParticlesPerSecond =
                integerValue(attributes, "MaxParticlesPerSecond");
            emitter.particleWidth = floatValue(attributes, "ParticleWidth");
            emitter.particleHeight = floatValue(attributes, "ParticleHeight");
            emitter.sizeVariationPercent =
                integerValue(attributes, "SizeVariation");
            emitter.minimumStartColor =
                colorValue(namedAttribute(attributes, "MinStartColor"));
            emitter.maximumStartColor =
                colorValue(namedAttribute(attributes, "MaxStartColor"));
            emitter.minimumParticleLifetimeMilliseconds =
                integerValue(attributes, "MinLifeTime");
            emitter.maximumParticleLifetimeMilliseconds =
                integerValue(attributes, "MaxLifeTime");
            emitter.speedVariationPercent =
                integerValue(attributes, "SpeedVariation");
            emitter.initialRotationMinimumDegrees =
                integerValue(attributes, "InitialRotMin");
            emitter.initialRotationMaximumDegrees =
                integerValue(attributes, "InitialRotMax");
            emitter.frameId = integerValue(attributes, "FrameID", -1);
            emitter.additive =
                stringValue(attributes, "MaterialType") == "trans_add";

            std::string currentAffector;
            for (pugi::xml_node value : attributes.children()) {
                const std::string_view field =
                    value.attribute("name").as_string();
                if (field == "Affector") {
                    currentAffector = value.attribute("value").as_string();
                } else if (currentAffector == "FadeOut") {
                    if (field == "TargetColor") {
                        emitter.fadeTargetColor = colorValue(value, 0U);
                    } else if (field == "StartTime(%)") {
                        emitter.fadeStartPercent =
                            value.attribute("value").as_int(100);
                    } else if (field == "EndTime(%)") {
                        emitter.fadeEndPercent =
                            value.attribute("value").as_int(100);
                    }
                } else if (currentAffector == "Gravity") {
                    emitter.hasGravity = true;
                    if (field == "Gravity") {
                        emitter.gravity = vectorValue(attributes, "Gravity");
                    } else if (field == "StartTime(%)") {
                        emitter.gravityStartPercent =
                            value.attribute("value").as_int();
                    } else if (field == "EndTime(%)") {
                        emitter.gravityEndPercent =
                            value.attribute("value").as_int(100);
                    }
                } else if (currentAffector == "Spin") {
                    emitter.hasSpin = true;
                    if (field == "MinSpin") {
                        emitter.spinMinimumDegrees =
                            value.attribute("value").as_int();
                    } else if (field == "MaxSpin") {
                        emitter.spinMaximumDegrees =
                            value.attribute("value").as_int();
                    } else if (field == "StartTime(%)") {
                        emitter.spinStartPercent =
                            value.attribute("value").as_int();
                    } else if (field == "EndTime(%)") {
                        emitter.spinEndPercent =
                            value.attribute("value").as_int(100);
                    }
                } else if (currentAffector == "Rotate") {
                    emitter.hasRotation = true;
                    if (field == "PivotPoint") {
                        emitter.rotationPivot =
                            vectorValue(attributes, "PivotPoint");
                    } else if (field == "Speed") {
                        emitter.rotationSpeedDegreesPerSecond =
                            vectorValue(attributes, "Speed");
                    }
                } else if (currentAffector == "Size") {
                    if (field == "TargetWidth") {
                        emitter.targetWidth =
                            value.attribute("value").as_float();
                    } else if (field == "TargetHeight") {
                        emitter.targetHeight =
                            value.attribute("value").as_float();
                    } else if (field == "StartTime(%)") {
                        emitter.sizeStartPercent =
                            value.attribute("value").as_int();
                    } else if (field == "EndTime(%)") {
                        emitter.sizeEndPercent =
                            value.attribute("value").as_int(100);
                    }
                }
            }
            if (emitter.frameId < 0 || emitter.particleWidth <= 0.0F ||
                emitter.particleHeight <= 0.0F ||
                emitter.maximumParticleLifetimeMilliseconds <= 0) {
                presets_.clear();
                return Result::failure(
                    "Effect emitter has invalid particle fields");
            }
            preset.emitters.push_back(std::move(emitter));
        }
        if (!preset.emitters.empty()) {
            presets_.push_back(std::move(preset));
        }
    }
    return presets_.empty()
               ? Result::failure("Effect preset XML contains no emitters")
               : Result::success();
}

const EffectPreset* EffectPresetDatabase::find(
    std::string_view name) const noexcept {
    const auto match = std::find_if(
        presets_.begin(), presets_.end(), [name](const EffectPreset& preset) {
            return preset.name == name;
        });
    return match == presets_.end() ? nullptr : &*match;
}

} // namespace usm::game
