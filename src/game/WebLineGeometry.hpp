#pragma once

#include "assets/IrrScene.hpp"
#include "core/Result.hpp"

#include <cstdint>
#include <vector>

namespace usm::game {

struct WebLineVertex {
    assets::Vector3 position;
    float textureU{};
    float textureV{};
};

struct WebLineGeometry {
    std::vector<WebLineVertex> vertices;
    std::vector<std::uint16_t> indices;
};

// CTexLineSceneNode is the native textured-ribbon primitive used by CobWeb.
// These are the exact values installed by CLevel::InitAllWebLines at
// 0x0037faa4: 30 quads, a 20 cm ribbon width, a 100 cm repeat length, and the
// central V=[0.4, 0.6] strip of web_rope.tga.
inline constexpr std::uint16_t kWebLineMaximumSegments = 30;
inline constexpr float kWebLineWidth = 20.0F;
inline constexpr float kWebLineSegmentLength = 100.0F;
inline constexpr float kWebLineTextureTop = 0.4F;
inline constexpr float kWebLineTextureBottom = 0.6F;

// Reconstructs CTexLineSceneNode::setLineSegment (0x0039af00). Positions are
// returned in world space; the original stores the same ribbon relative to a
// scene node translated to `start`.
[[nodiscard]] Result buildWebLineGeometry(
    const assets::Vector3& start, const assets::Vector3& end,
    const assets::Vector3& orientation, WebLineGeometry& output) noexcept;

} // namespace usm::game
