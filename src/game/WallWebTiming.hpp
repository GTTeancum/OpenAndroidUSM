#pragma once

#include <cstdint>

namespace usm::game {

// CQTEManager stores its elapsed/idle timing in floating-point milliseconds.
// Wall-web Player::UpdateQTE routes the same manager semantics while its
// character animation remains on game time.
[[nodiscard]] constexpr float advanceWallWebPromptClock(
    float elapsedMilliseconds, std::uint32_t realMilliseconds) noexcept {
    return elapsedMilliseconds + static_cast<float>(realMilliseconds);
}

// CQTEManager::IsOutTime (0x0037a4b8) uses a strict > comparison.
[[nodiscard]] constexpr bool wallWebPromptExpired(
    float elapsedMilliseconds, float durationMilliseconds) noexcept {
    return elapsedMilliseconds > durationMilliseconds;
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
    bool succeeded, float elapsedMilliseconds,
    float durationMilliseconds) noexcept {
    if (wallWebPromptExpired(elapsedMilliseconds, durationMilliseconds)) {
        return WallWebPromptOutcome::Failure;
    }
    return succeeded ? WallWebPromptOutcome::Success
                     : WallWebPromptOutcome::Running;
}

} // namespace usm::game
