#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace usm::game {

// CQTEManager::IsOutTime (0x0037a4b8) compares its accumulated absolute
// timer with a strict > duration check. Wall-web Player::UpdateQTE routes the
// same manager semantics while its character animation remains on game time.
[[nodiscard]] constexpr std::uint32_t advanceWallWebPromptClock(
    std::uint32_t elapsedMilliseconds,
    std::uint32_t realMilliseconds) noexcept {
    return elapsedMilliseconds >
                   std::numeric_limits<std::uint32_t>::max() - realMilliseconds
               ? std::numeric_limits<std::uint32_t>::max()
               : elapsedMilliseconds + realMilliseconds;
}

[[nodiscard]] constexpr bool wallWebPromptExpired(
    std::uint32_t elapsedMilliseconds,
    float durationMilliseconds) noexcept {
    return static_cast<float>(elapsedMilliseconds) > durationMilliseconds;
}

} // namespace usm::game
