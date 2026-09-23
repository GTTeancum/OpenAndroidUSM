#pragma once

#include "platform/input/XInputStateTranslator.hpp"

#include <cstdint>

namespace usm::platform {

class XInputController final {
public:
    using EventHandler = XInputStateTranslator::EventHandler;

    explicit XInputController(std::uint32_t playerIndex = 0) noexcept
        : playerIndex_(playerIndex) {}

    void poll(const EventHandler& handler) noexcept;
    [[nodiscard]] bool connected() const noexcept {
        return translator_.connected();
    }
    [[nodiscard]] ControllerStick leftStick() const noexcept {
        return translator_.leftStick();
    }

private:
    std::uint32_t playerIndex_{};
    XInputStateTranslator translator_;
};

} // namespace usm::platform
