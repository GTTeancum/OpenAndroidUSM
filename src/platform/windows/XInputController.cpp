#include "platform/windows/XInputController.hpp"

#include <algorithm>
#include <cmath>

namespace usm::platform {
namespace {

using reconstructed::XperiaKeyCode;
using reconstructed::XperiaScanCode;

struct Binding {
    WORD mask;
    XperiaKeyCode keyCode;
    XperiaScanCode scanCode;
};

constexpr std::array<Binding, 12> kBindings{{
    {XINPUT_GAMEPAD_A, XperiaKeyCode::Cross, XperiaScanCode::Cross},
    {XINPUT_GAMEPAD_B, XperiaKeyCode::Circle, XperiaScanCode::Circle},
    {XINPUT_GAMEPAD_X, XperiaKeyCode::Square, XperiaScanCode::Square},
    {XINPUT_GAMEPAD_Y, XperiaKeyCode::Triangle, XperiaScanCode::Triangle},
    {XINPUT_GAMEPAD_LEFT_SHOULDER, XperiaKeyCode::L1, XperiaScanCode::L1},
    {XINPUT_GAMEPAD_RIGHT_SHOULDER, XperiaKeyCode::R1, XperiaScanCode::R1},
    {XINPUT_GAMEPAD_BACK, XperiaKeyCode::Select, XperiaScanCode::Select},
    {XINPUT_GAMEPAD_START, XperiaKeyCode::Start, XperiaScanCode::Start},
    {XINPUT_GAMEPAD_DPAD_UP, XperiaKeyCode::DpadUp, XperiaScanCode::DpadUp},
    {XINPUT_GAMEPAD_DPAD_DOWN, XperiaKeyCode::DpadDown, XperiaScanCode::DpadDown},
    {XINPUT_GAMEPAD_DPAD_LEFT, XperiaKeyCode::DpadLeft, XperiaScanCode::DpadLeft},
    {XINPUT_GAMEPAD_DPAD_RIGHT, XperiaKeyCode::DpadRight, XperiaScanCode::DpadRight},
}};

constexpr SHORT kStickDirectionThreshold = 9000;

ControllerStick normalizeLeftStick(SHORT x, SHORT y) noexcept {
    constexpr float kMaximumAxis = 32767.0F;
    constexpr float kDeadZone =
        static_cast<float>(XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) / kMaximumAxis;
    float normalizedX = std::clamp(static_cast<float>(x) / kMaximumAxis,
                                   -1.0F, 1.0F);
    float normalizedY = std::clamp(static_cast<float>(y) / kMaximumAxis,
                                   -1.0F, 1.0F);
    const float magnitude =
        std::sqrt(normalizedX * normalizedX + normalizedY * normalizedY);
    if (magnitude <= kDeadZone) {
        return {};
    }
    const float remappedMagnitude =
        std::min((magnitude - kDeadZone) / (1.0F - kDeadZone), 1.0F);
    normalizedX = normalizedX / magnitude * remappedMagnitude;
    normalizedY = normalizedY / magnitude * remappedMagnitude;
    return {normalizedX, normalizedY};
}

WORD addLeftStickDirections(WORD buttons, SHORT x, SHORT y) noexcept {
    if (x < -kStickDirectionThreshold) {
        buttons |= XINPUT_GAMEPAD_DPAD_LEFT;
    } else if (x > kStickDirectionThreshold) {
        buttons |= XINPUT_GAMEPAD_DPAD_RIGHT;
    }
    if (y < -kStickDirectionThreshold) {
        buttons |= XINPUT_GAMEPAD_DPAD_DOWN;
    } else if (y > kStickDirectionThreshold) {
        buttons |= XINPUT_GAMEPAD_DPAD_UP;
    }
    return buttons;
}

} // namespace

void XInputController::poll(const EventHandler& handler) noexcept {
    XINPUT_STATE state{};
    if (XInputGetState(playerIndex_, &state) != ERROR_SUCCESS) {
        if (connected_) {
            for (const Binding& binding : kBindings) {
                emitTransition((previousButtons_ & binding.mask) != 0, false,
                               binding.keyCode, binding.scanCode, handler);
            }
        }
        previousButtons_ = 0;
        leftStick_ = {};
        connected_ = false;
        return;
    }

    connected_ = true;
    leftStick_ = normalizeLeftStick(state.Gamepad.sThumbLX,
                                    state.Gamepad.sThumbLY);
    const WORD currentButtons = addLeftStickDirections(
        state.Gamepad.wButtons, state.Gamepad.sThumbLX, state.Gamepad.sThumbLY);

    for (const Binding& binding : kBindings) {
        emitTransition((previousButtons_ & binding.mask) != 0,
                       (currentButtons & binding.mask) != 0, binding.keyCode,
                       binding.scanCode, handler);
    }
    previousButtons_ = currentButtons;
}

void XInputController::emitTransition(bool wasPressed, bool isPressed,
                                      XperiaKeyCode keyCode,
                                      XperiaScanCode scanCode,
                                      const EventHandler& handler) const {
    if (wasPressed == isPressed || !handler) {
        return;
    }
    handler({keyCode, scanCode, isPressed});
}

} // namespace usm::platform
