#pragma once

#include "assets/SpriteAtlas.hpp"
#include "core/Result.hpp"
#include "game/QteFeedbackFrame.hpp"

#include <cstdint>
#include <vector>

namespace usm::game {
struct QteFeedbackVertex {
    float x{}, y{}, u{}, v{}; // clip-space XY and original atlas UV
    std::uint32_t rgba{};
};
// Backend-independent extraction for the native result frames actually used
// by the shipped interface atlas. Unsupported transforms fail, never guess.
[[nodiscard]] Result buildQteFeedbackGeometry(
    const QteFeedbackFrame& frame, const assets::SpriteAtlas& atlas,
    std::uint32_t textureWidth, std::uint32_t textureHeight,
    std::uint32_t viewportWidth, std::uint32_t viewportHeight,
    std::vector<QteFeedbackVertex>& output);
} // namespace usm::game
