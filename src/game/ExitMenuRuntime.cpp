#include "game/ExitMenuRuntime.hpp"

#include <algorithm>

namespace usm::game {

Result ExitMenuRuntime::bind(const LevelTextCatalog& strings) {
    // GS_ExitMenu::Render (0x002c0020) draws Main strings 0x13 and 0x26a
    // using font_outline_big at UI item 0x1b.
    const std::u16string* label = strings.main().at(0x13);
    const std::u16string* suffix = strings.main().at(0x26a);
    if (label == nullptr || suffix == nullptr) {
        return Result::failure("Exit-menu loading strings are missing from Main");
    }
    loadingLabel_ = *label;
    loadingSuffix_ = *suffix;
    reset();
    return Result::success();
}

void ExitMenuRuntime::beginAfterDeath() noexcept {
    accumulatorMilliseconds_ = 0.0F;
    state_ = 0;
    active_ = true;
}

void ExitMenuRuntime::reset() noexcept {
    accumulatorMilliseconds_ = 0.0F;
    state_ = 0;
    active_ = false;
}

void ExitMenuRuntime::update(std::uint32_t elapsedMilliseconds) noexcept {
    if (!active_ || state_ >= 20) {
        return;
    }
    accumulatorMilliseconds_ += static_cast<float>(elapsedMilliseconds);
    // GS_ExitMenu::Update increments at most once per application update; it
    // retains any excess after subtracting the fixed 50 ms UI tick.
    if (accumulatorMilliseconds_ >= 50.0F) {
        accumulatorMilliseconds_ -= 50.0F;
        ++state_;
    }
}

bool ExitMenuRuntime::loadingTextVisible() const noexcept {
    if (!active_ || state_ >= 20 || state_ == 16) {
        return false;
    }
    const std::uint32_t alpha =
        state_ < 15 ? 150U + state_ * 7U : 255U;
    return alpha > 150U;
}

float ExitMenuRuntime::blackOverlayAlpha() const noexcept {
    if (!active_) {
        return 0.0F;
    }
    const std::uint32_t alpha =
        state_ < 15 ? 150U + state_ * 7U : 255U;
    return static_cast<float>(std::min(alpha, 255U)) / 255.0F;
}

} // namespace usm::game
