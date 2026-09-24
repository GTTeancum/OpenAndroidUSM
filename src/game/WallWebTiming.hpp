#pragma once

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

enum class WallWebPromptOutcome {
    Running,
    Success,
    Failure,
};

// CQTEManager's tap/mash state calls CheckSuccess and then still executes
// IsOutTime in the same update. Therefore a final action arriving after the
// strict timeout boundary loses even when CheckSuccess briefly succeeds.
[[nodiscard]] constexpr WallWebPromptOutcome wallWebPromptOutcome(
    bool succeeded, std::uint32_t elapsedMilliseconds,
    float durationMilliseconds) noexcept {
    if (wallWebPromptExpired(elapsedMilliseconds, durationMilliseconds)) {
        return WallWebPromptOutcome::Failure;
    }
    return succeeded ? WallWebPromptOutcome::Success
                     : WallWebPromptOutcome::Running;
}

} // namespace usm::game
