#include "game/LevelDeathRuntime.hpp"

#include <algorithm>

namespace usm::game {
namespace {

constexpr std::uint32_t kPlayerDeathFadeDurationMilliseconds = 2000;

} // namespace

void LevelDeathRuntime::reset() noexcept {
    active_ = false;
    confirmationReady_ = false;
    elapsedMilliseconds_ = 0;
    fadeDurationMilliseconds_ = kPlayerDeathFadeDurationMilliseconds;
    alpha_ = 0.0F;
}

void LevelDeathRuntime::update(
    bool playerDeadOver, std::uint32_t elapsedMilliseconds,
    std::uint32_t fadeDurationMilliseconds) noexcept {
    if (!playerDeadOver) {
        reset();
        return;
    }
    if (!active_) {
        // CLevel::Update (0x003820bc) initializes CBlackScreen and invokes
        // FadeIn on this first frame; CBlackScreen::Update sees state zero,
        // so no elapsed time is consumed until the following frame.
        active_ = true;
        fadeDurationMilliseconds_ =
            std::max(fadeDurationMilliseconds, 1U);
        return;
    }
    if (confirmationReady_) {
        return;
    }

    elapsedMilliseconds_ = std::min(
        fadeDurationMilliseconds_,
        elapsedMilliseconds_ + elapsedMilliseconds);
    const std::uint32_t remaining =
        fadeDurationMilliseconds_ - elapsedMilliseconds_;

    // CBlackScreen::Update (0x00368858) leaves the screen transparent while
    // more than 30 percent of the configured duration remains. Its final branch
    // uses the preserved 70-percent denominator exactly, including the
    // authored alpha jump when the branch first becomes active.
    const std::uint32_t remainingThreshold =
        fadeDurationMilliseconds_ * 3U / 10U;
    const std::uint32_t alphaDenominator =
        std::max(fadeDurationMilliseconds_ * 7U / 10U, 1U);
    if (remaining <= remainingThreshold) {
        const std::uint32_t nativeAlpha = std::min<std::uint32_t>(
            255U,
            255U * (alphaDenominator -
                    std::min(remaining, alphaDenominator)) /
                alphaDenominator);
        alpha_ = static_cast<float>(nativeAlpha) / 255.0F;
    }
    if (elapsedMilliseconds_ >= fadeDurationMilliseconds_) {
        alpha_ = 1.0F;
        confirmationReady_ = true;
    }
}

} // namespace usm::game
