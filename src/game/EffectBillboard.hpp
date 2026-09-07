#pragma once

#include "assets/IrrScene.hpp"
#include "game/LevelEffectRuntime.hpp"

namespace usm::game {

struct EffectBillboardAxes {
    assets::Vector3 right;
    assets::Vector3 up;
};

// Reconstructed from CFpsParticleSystemSceneNode::render at 0x0039ff5c.
// Keeping the orientation result renderer-neutral also makes the recovered
// native behavior independently testable.
[[nodiscard]] EffectBillboardAxes effectBillboardAxes(
    const EffectParticleState& particle, const assets::Vector3& cameraRight,
    const assets::Vector3& cameraUp,
    const assets::Vector3& cameraForward) noexcept;

}  // namespace usm::game
