#pragma once

#include "reconstructed/input/GameplayInputState.hpp"
#include "reconstructed/input/XperiaKeyEvent.hpp"

namespace usm::reconstructed {

enum class InputContext {
    Gameplay,
    UpgradeMenu,
    Menu,
};

// Reconstructed from appKeyPressed (ELF 0x003cc75c) and appKeyReleased
// (ELF 0x003cbd5c). The supplied Ghidra project rebases both by +0x10000.
// This is native C++ behavior, not a hook or binary bridge.
class XperiaKeyRouter final {
public:
    void beginFrame() noexcept { state_.beginFrame(); gameplayKeypad_.beginFrame(); }

    // Game-facing PC keypad publication is separate from the direct Xperia
    // flags consumed by QTE/UI. Native ResetAllKeys (ELF 0x2e8888) resets the
    // keypad and movement, not m_pQTEPressed, rescue/switch, or physical XInput.
    void resetGameplayKeypad() noexcept {
        gameplayKeypad_ = {};
        gameplayStickX_ = gameplayStickY_ = 0;
        ++gameplayKeypadResetCount_;
    }
    // PC input policy: publish the normalized physical stick once per poll.
    // A reset clears only this update's game-facing copy; QTE gestures still
    // see the original physical sample and retain RE03's neutral/edge rules.
    void publishGameplayStick(float x, float y) noexcept {
        gameplayStickX_ = x; gameplayStickY_ = y;
    }
    [[nodiscard]] float gameplayStickX() const noexcept { return gameplayStickX_; }
    [[nodiscard]] float gameplayStickY() const noexcept { return gameplayStickY_; }
    [[nodiscard]] std::uint64_t gameplayKeypadResetCount() const noexcept { return gameplayKeypadResetCount_; }
    [[nodiscard]] const GameplayInputState& gameplayKeypad() const noexcept { return gameplayKeypad_; }
    void route(const XperiaKeyEvent& event, InputContext context) noexcept;

    // Native global writes by CQTEManager::Update. These are NOT physical
    // releases: held state and CKeyPad's separate history remain untouched.
    void consumeQuickTimePress() noexcept { state_.quickTimeEvent.pressed = false; }
    void consumeJumpPress() noexcept { state_.jump.pressed = false; }
    void consumeRescueRequest() noexcept { state_.rescueRequested = false; }
    // CQTEManager::SetState clears m_pIGMPressed before success/fail handoff.
    void consumePausePress() noexcept { state_.pause.pressed = false; }

    [[nodiscard]] const GameplayInputState& state() const noexcept {
        return state_;
    }

private:
    GameplayInputState state_;
    GameplayInputState gameplayKeypad_; // existing per-action keypad facade, not raw globals
    float gameplayStickX_{}, gameplayStickY_{};
    std::uint64_t gameplayKeypadResetCount_{};
};

} // namespace usm::reconstructed
