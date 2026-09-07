#pragma once

#include "assets/IrrScene.hpp"
#include "assets/SpriteAtlas.hpp"
#include "game/LevelEffectRuntime.hpp"

namespace usm::game {

struct EffectBillboardAxes {
    assets::Vector3 right;
    assets::Vector3 up;
};

struct EffectSpriteUvRect {
    float left{};
    float top{};
    float right{};
    float bottom{};
    bool valid{};
};

// Reconstructed from CFpsParticleSystemSceneNode::render at 0x0039ff5c.
// Keeping the orientation result renderer-neutral also makes the recovered
// native behavior independently testable.
[[nodiscard]] EffectBillboardAxes effectBillboardAxes(
    const EffectParticleState& particle, const assets::Vector3& cameraRight,
    const assets::Vector3& cameraUp,
    const assets::Vector3& cameraForward) noexcept;

// EffectManager::LoadEffectPresets (0x003925c4) resolves FrameID through the
// frame's first module and divides the module rectangle by textureDimension-1
// before CFpsParticleSystemSceneNode::setUVRect (0x0039eba4). Frame-module
// offsets and flip flags belong to 2D CSprite drawing and are not used here.
[[nodiscard]] EffectSpriteUvRect effectSpriteUvRect(
    const assets::SpriteAtlas& atlas, std::uint32_t textureWidth,
    std::uint32_t textureHeight, std::int32_t frameId) noexcept;

}  // namespace usm::game
