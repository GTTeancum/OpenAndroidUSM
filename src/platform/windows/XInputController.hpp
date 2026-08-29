#pragma once

#include "reconstructed/input/XperiaKeyEvent.hpp"

#include <Windows.h>
#include <Xinput.h>

#include <array>
#include <cstdint>
#include <functional>

namespace usm::platform {

class XInputController final {
public:
    using EventHandler =
        std::function<void(const reconstructed::XperiaKeyEvent&)>;

    explicit XInputController(std::uint32_t playerIndex = 0) noexcept
        : playerIndex_(playerIndex) {}

    void poll(const EventHandler& handler) noexcept;
    [[nodiscard]] bool connected() const noexcept { return connected_; }

private:
    void emitTransition(bool wasPressed, bool isPressed,
                        reconstructed::XperiaKeyCode keyCode,
                        reconstructed::XperiaScanCode scanCode,
                        const EventHandler& handler) const;

    std::uint32_t playerIndex_{};
    WORD previousButtons_{};
    bool connected_{};
};

} // namespace usm::platform
