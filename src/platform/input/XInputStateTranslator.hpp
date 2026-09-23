#pragma once

#include "reconstructed/input/XperiaKeyEvent.hpp"

#include <cstdint>
#include <functional>

namespace usm::platform {

struct ControllerStick {
    float x{};
    float y{};
};

// These are XINPUT_GAMEPAD API constants, not guessed Android controls.
// XInputController.cpp statically checks every value against the Windows SDK.
namespace xinput {
inline constexpr std::uint16_t DpadUp = 0x0001;
inline constexpr std::uint16_t DpadDown = 0x0002;
inline constexpr std::uint16_t DpadLeft = 0x0004;
inline constexpr std::uint16_t DpadRight = 0x0008;
inline constexpr std::uint16_t Start = 0x0010;
inline constexpr std::uint16_t Back = 0x0020;
inline constexpr std::uint16_t LeftShoulder = 0x0100;
inline constexpr std::uint16_t RightShoulder = 0x0200;
inline constexpr std::uint16_t A = 0x1000;
inline constexpr std::uint16_t B = 0x2000;
inline constexpr std::uint16_t X = 0x4000;
inline constexpr std::uint16_t Y = 0x8000;
inline constexpr std::int16_t LeftThumbDeadzone = 7849;
} // namespace xinput

// Production XInput-state translation shared by the Windows poller and
// deterministic tests. It never calls the original ARM binary.
class XInputStateTranslator final {
public:
    using EventHandler =
        std::function<void(const reconstructed::XperiaKeyEvent&)>;

    void update(bool connected, std::uint16_t buttons,
                std::int16_t leftX, std::int16_t leftY,
                const EventHandler& handler) noexcept;
    [[nodiscard]] bool connected() const noexcept { return connected_; }
    [[nodiscard]] ControllerStick leftStick() const noexcept {
        return leftStick_;
    }

private:
    void emitTransition(bool wasPressed, bool isPressed,
                        reconstructed::XperiaKeyCode keyCode,
                        reconstructed::XperiaScanCode scanCode,
                        const EventHandler& handler) const;
    std::uint16_t previousButtons_{};
    bool connected_{};
    ControllerStick leftStick_;
};

} // namespace usm::platform
