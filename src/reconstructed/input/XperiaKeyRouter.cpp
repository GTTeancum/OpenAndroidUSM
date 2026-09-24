#include "reconstructed/input/XperiaKeyRouter.hpp"

namespace usm::reconstructed {

namespace {
void routeState(GameplayInputState& state_, const XperiaKeyEvent& event,
                InputContext context) noexcept {
    if (event.deviceCancelled) {
        // Clear held buttons and pending PC events across all contexts. This
        // does not enter any native key handler or generate a release action.
        state_ = {};
        return;
    }
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
        } else if (context == InputContext::Menu) {
            state_.menuSelected.set(event.pressed);
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
    case XperiaKeyCode::R1:
        // appKeyReleased, ELF 0x003cc2b8-0x003cc2ea:
        // key 0x67 + scan 0x137 in gameplay writes rescue=1, the
        // InteractiveButton boolean=1, and the CSwitchObject counter=4.
        // appKeyPressed has no corresponding R1 action.
        if (event.scanCode == XperiaScanCode::R1 && gameplay &&
            !event.pressed) {
            state_.rescueRequested = true;
            state_.interactiveButtonRequested = true;
            state_.switchCounter = 4;
        }
        break;
    case XperiaKeyCode::DpadUp:
        if (gameplay || context == InputContext::Menu) {
            state_.moveUp.set(event.pressed);
        }
        break;
    case XperiaKeyCode::DpadDown:
        if (gameplay || context == InputContext::Menu) {
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

} // namespace

void XperiaKeyRouter::route(const XperiaKeyEvent& event, InputContext context) noexcept {
    routeState(state_, event, context);
    if (event.deviceCancelled) {
        resetGameplayKeypad();
    } else if (context == InputContext::Gameplay) {
        routeState(gameplayKeypad_, event, context);
    }
}

} // namespace usm::reconstructed
