#pragma once

#include <array>
#include <cstdint>

namespace usm::game {
// Paint requests for the recovered CQTEManager result display, in original
// draw order. These are output sprites, not interactive touch targets.
struct QteFeedbackPosition { std::int32_t x{}, y{}; };
struct QteFeedbackSprite {
    std::uint16_t frameIndex{};
    QteFeedbackPosition position{};
    std::uint8_t flags{};
    std::uint8_t alpha{255};
};
struct QteFeedbackFrame {
    std::array<QteFeedbackSprite, 3> sprites{};
    std::uint8_t count{};
};
} // namespace usm::game
