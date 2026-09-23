#include "platform/input/XInputStateTranslator.hpp"

#include <array>

#include <algorithm>
#include <cmath>

namespace usm::platform {
namespace {

using reconstructed::XperiaKeyCode;
using reconstructed::XperiaScanCode;

struct Binding {
    std::uint16_t mask;
    XperiaKeyCode keyCode;
    XperiaScanCode scanCode;
};

constexpr std::array<Binding, 12> kBindings{{
    {xinput::A, XperiaKeyCode::Cross, XperiaScanCode::Cross},
    {xinput::B, XperiaKeyCode::Circle, XperiaScanCode::Circle},
    {xinput::X, XperiaKeyCode::Square, XperiaScanCode::Square},
    {xinput::Y, XperiaKeyCode::Triangle, XperiaScanCode::Triangle},
    {xinput::LeftShoulder, XperiaKeyCode::L1, XperiaScanCode::L1},
    {xinput::RightShoulder, XperiaKeyCode::R1, XperiaScanCode::R1},
    {xinput::Back, XperiaKeyCode::Select, XperiaScanCode::Select},
    {xinput::Start, XperiaKeyCode::Start, XperiaScanCode::Start},
    {xinput::DpadUp, XperiaKeyCode::DpadUp, XperiaScanCode::DpadUp},
    {xinput::DpadDown, XperiaKeyCode::DpadDown, XperiaScanCode::DpadDown},
    {xinput::DpadLeft, XperiaKeyCode::DpadLeft, XperiaScanCode::DpadLeft},
    {xinput::DpadRight, XperiaKeyCode::DpadRight, XperiaScanCode::DpadRight},
}};

// Existing PC-adapter calibration from the uploaded checkpoint; unchanged.
constexpr std::int16_t kStickDirectionThreshold = 9000;

ControllerStick normalizeLeftStick(std::int16_t x, std::int16_t y) noexcept {
    constexpr float kMaximumAxis = 32767.0F;
    constexpr float kDeadZone =
        static_cast<float>(xinput::LeftThumbDeadzone) / kMaximumAxis;
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

std::uint16_t addLeftStickDirections(std::uint16_t buttons, std::int16_t x, std::int16_t y) noexcept {
    if (x < -kStickDirectionThreshold) {
        buttons |= xinput::DpadLeft;
    } else if (x > kStickDirectionThreshold) {
        buttons |= xinput::DpadRight;
    }
    if (y < -kStickDirectionThreshold) {
        buttons |= xinput::DpadDown;
    } else if (y > kStickDirectionThreshold) {
        buttons |= xinput::DpadUp;
    }
    return buttons;
}

} // namespace

void XInputStateTranslator::update(bool connected, std::uint16_t buttons,
                                   std::int16_t leftX, std::int16_t leftY,
                                   const EventHandler& handler) noexcept {
    if (!connected) {
        if (connected_ && handler) {
            // Losing the device is NOT appKeyReleased(R1). Synthetic UP
            // events would rescue/switch/select when the controller vanished.
            handler({{}, {}, false, true});
        }
        previousButtons_ = 0;
        leftStick_ = {};
        connected_ = false;
        return;
    }

    connected_ = true;
    leftStick_ = normalizeLeftStick(leftX,
                                    leftY);
    const std::uint16_t currentButtons = addLeftStickDirections(
        buttons, leftX, leftY);

    for (const Binding& binding : kBindings) {
        emitTransition((previousButtons_ & binding.mask) != 0,
                       (currentButtons & binding.mask) != 0, binding.keyCode,
                       binding.scanCode, handler);
    }
    previousButtons_ = currentButtons;
}

void XInputStateTranslator::emitTransition(bool wasPressed, bool isPressed,
                                      XperiaKeyCode keyCode,
                                      XperiaScanCode scanCode,
                                      const EventHandler& handler) const {
    if (wasPressed == isPressed || !handler) {
        return;
    }
    handler({keyCode, scanCode, isPressed});
}

} // namespace usm::platform
