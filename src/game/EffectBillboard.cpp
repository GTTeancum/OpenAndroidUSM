#include "game/EffectBillboard.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace usm::game {
namespace {

float dot(const assets::Vector3& left,
          const assets::Vector3& right) noexcept {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

assets::Vector3 cross(const assets::Vector3& left,
                      const assets::Vector3& right) noexcept {
    return {left.y * right.z - left.z * right.y,
            left.z * right.x - left.x * right.z,
            left.x * right.y - left.y * right.x};
}

bool normalize(assets::Vector3& value) noexcept {
    const float squaredLength = dot(value, value);
    if (squaredLength <= std::numeric_limits<float>::epsilon()) {
        return false;
    }
    const float inverseLength = 1.0F / std::sqrt(squaredLength);
    value.x *= inverseLength;
    value.y *= inverseLength;
    value.z *= inverseLength;
    return true;
}

assets::Vector3 rotateAroundAxis(const assets::Vector3& value,
                                 const assets::Vector3& axis,
                                 float radians) noexcept {
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    const assets::Vector3 perpendicular = cross(axis, value);
    const float parallelScale = dot(axis, value) * (1.0F - cosine);
    return {value.x * cosine + perpendicular.x * sine +
                axis.x * parallelScale,
            value.y * cosine + perpendicular.y * sine +
                axis.y * parallelScale,
            value.z * cosine + perpendicular.z * sine +
                axis.z * parallelScale};
}

assets::Vector3 rotateFromTo(const assets::Vector3& value,
                             assets::Vector3 source,
                             assets::Vector3 target) noexcept {
    if (!normalize(source) || !normalize(target)) {
        return value;
    }
    const float cosine = std::clamp(dot(source, target), -1.0F, 1.0F);
    if (cosine >= 0.999999F) {
        return value;
    }
    assets::Vector3 axis = cross(source, target);
    if (!normalize(axis)) {
        axis = cross({1.0F, 0.0F, 0.0F}, source);
        if (!normalize(axis)) {
            axis = cross({0.0F, 1.0F, 0.0F}, source);
            if (!normalize(axis)) {
                return value;
            }
        }
    }
    return rotateAroundAxis(value, axis, std::acos(cosine));
}

}  // namespace

EffectBillboardAxes effectBillboardAxes(
    const EffectParticleState& particle, const assets::Vector3& cameraRight,
    const assets::Vector3& cameraUp,
    const assets::Vector3& cameraForward) noexcept {
    constexpr float degreesToRadians = 0.017453292519943295F;
    const float initialRadians = particle.rotationDegrees * degreesToRadians;
    EffectBillboardAxes axes{
        rotateAroundAxis(cameraRight, cameraForward, initialRadians),
        rotateAroundAxis(cameraUp, cameraForward, initialRadians)};

    // Affector type 6 is Spin (IFpsParticleSpinAffector::getType,
    // 0x0039e5ec). render gives it precedence over both DirectionalRotation
    // paths. The runtime has folded InitialRot and Spin into rotationDegrees.
    if (particle.hasSpinAffector || !particle.directionalRotation) {
        return axes;
    }

    assets::Vector3 displacement{
        particle.position.x - particle.previousPosition.x,
        particle.position.y - particle.previousPosition.y,
        particle.position.z - particle.previousPosition.z};
    if (!particle.projectDirection) {
        axes.right =
            rotateFromTo(axes.right, particle.emitterDirection, displacement);
        axes.up =
            rotateFromTo(axes.up, particle.emitterDirection, displacement);
        return axes;
    }

    // 0x003a0274 projects Pos-OldPos onto the camera plane along its normal,
    // normalizes it, measures the signed angle from camera-up, and rotates
    // both billboard axes around camera-forward.
    if (!normalize(displacement)) {
        return axes;
    }
    assets::Vector3 projected{
        displacement.x - cameraForward.x * dot(displacement, cameraForward),
        displacement.y - cameraForward.y * dot(displacement, cameraForward),
        displacement.z - cameraForward.z * dot(displacement, cameraForward)};
    if (!normalize(projected)) {
        return axes;
    }
    float radians =
        std::acos(std::clamp(dot(cameraUp, projected), -1.0F, 1.0F));
    if (dot(cross(cameraUp, projected), cameraForward) < 0.0F) {
        radians = -radians;
    }
    axes.right = rotateAroundAxis(axes.right, cameraForward, radians);
    axes.up = rotateAroundAxis(axes.up, cameraForward, radians);
    return axes;
}

EffectSpriteUvRect effectSpriteUvRect(const assets::SpriteAtlas& atlas,
                                      std::uint32_t textureWidth,
                                      std::uint32_t textureHeight,
                                      std::int32_t frameId) noexcept {
    if (frameId < 0 || textureWidth < 2 || textureHeight < 2) {
        return {};
    }
    const auto frameModules =
        atlas.modulesForFrame(static_cast<std::size_t>(frameId));
    if (frameModules.empty() ||
        frameModules.front().moduleIndex >= atlas.modules().size()) {
        return {};
    }
    const assets::SpriteModule& module =
        atlas.modules()[frameModules.front().moduleIndex];
    if (module.imageIndex != 0) {
        return {};
    }
    const float widthDenominator = static_cast<float>(textureWidth - 1);
    const float heightDenominator = static_cast<float>(textureHeight - 1);
    return {static_cast<float>(module.x) / widthDenominator,
            static_cast<float>(module.y) / heightDenominator,
            static_cast<float>(module.x + module.width) / widthDenominator,
            static_cast<float>(module.y + module.height) / heightDenominator,
            true};
}

}  // namespace usm::game
