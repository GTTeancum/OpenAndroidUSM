#pragma once

#include "reconstructed/input/GameplayInputState.hpp"
#include "reconstructed/input/XperiaKeyEvent.hpp"

namespace usm::reconstructed {

enum class InputContext {
    Gameplay,
    UpgradeMenu,
    Menu,
};

// Reconstructed from appKeyPressed (0x003dc75c) and appKeyReleased
// (0x003dbd5c). This is native C++ behavior, not a hook or binary bridge.
class XperiaKeyRouter final {
public:
    void beginFrame() noexcept { state_.beginFrame(); }
    void route(const XperiaKeyEvent& event, InputContext context) noexcept;

    [[nodiscard]] const GameplayInputState& state() const noexcept {
        return state_;
    }

private:
    GameplayInputState state_;
};

} // namespace usm::reconstructed
