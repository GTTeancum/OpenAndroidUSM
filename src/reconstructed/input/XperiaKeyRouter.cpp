#include "reconstructed/input/XperiaKeyRouter.hpp"

namespace usm::reconstructed {

void XperiaKeyRouter::route(const XperiaKeyEvent& event,
                            InputContext context) noexcept {
    const bool gameplay = context == InputContext::Gameplay;

    switch (event.keyCode) {
    case XperiaKeyCode::Circle:
        if (event.scanCode == XperiaScanCode::Circle && gameplay) {
            state_.web.set(event.pressed);
        }
        break;
    case XperiaKeyCode::Cross:
        if (event.scanCode != XperiaScanCode::Cross) {
            break;
        }
        if (gameplay) {
            state_.jump.set(event.pressed);
            state_.quickTimeEvent.set(event.pressed);
        } else if (context == InputContext::UpgradeMenu) {
            state_.upgrade.set(event.pressed);
            state_.upgradeProceed.set(event.pressed);
        }
        break;
    case XperiaKeyCode::Square:
        if (event.scanCode == XperiaScanCode::Square && gameplay) {
            state_.punch.set(event.pressed);
        }
        break;
    case XperiaKeyCode::Triangle:
        if (event.scanCode == XperiaScanCode::Triangle && gameplay) {
            state_.superAttack.set(event.pressed);
        }
        break;
    case XperiaKeyCode::L1:
        if (event.scanCode == XperiaScanCode::L1 && gameplay) {
            state_.spiderSense.set(event.pressed);
        }
        break;
    case XperiaKeyCode::DpadUp:
        if (gameplay) {
            state_.moveUp.set(event.pressed);
        }
        break;
    case XperiaKeyCode::DpadDown:
        if (gameplay) {
            state_.moveDown.set(event.pressed);
        }
        break;
    case XperiaKeyCode::DpadLeft:
        if (gameplay) {
            state_.moveLeft.set(event.pressed);
        }
        break;
    case XperiaKeyCode::DpadRight:
        if (gameplay) {
            state_.moveRight.set(event.pressed);
        }
        break;
    case XperiaKeyCode::Start:
        state_.pause.set(event.pressed);
        break;
    default:
        break;
    }
}

} // namespace usm::reconstructed
