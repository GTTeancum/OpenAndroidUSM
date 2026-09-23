#include "platform/windows/XInputController.hpp"

#include <Windows.h>
#include <Xinput.h>

namespace usm::platform {

static_assert(xinput::A == XINPUT_GAMEPAD_A);
static_assert(xinput::B == XINPUT_GAMEPAD_B);
static_assert(xinput::X == XINPUT_GAMEPAD_X);
static_assert(xinput::Y == XINPUT_GAMEPAD_Y);
static_assert(xinput::LeftShoulder == XINPUT_GAMEPAD_LEFT_SHOULDER);
static_assert(xinput::RightShoulder == XINPUT_GAMEPAD_RIGHT_SHOULDER);
static_assert(xinput::Back == XINPUT_GAMEPAD_BACK);
static_assert(xinput::Start == XINPUT_GAMEPAD_START);
static_assert(xinput::DpadUp == XINPUT_GAMEPAD_DPAD_UP);
static_assert(xinput::DpadDown == XINPUT_GAMEPAD_DPAD_DOWN);
static_assert(xinput::DpadLeft == XINPUT_GAMEPAD_DPAD_LEFT);
static_assert(xinput::DpadRight == XINPUT_GAMEPAD_DPAD_RIGHT);
static_assert(xinput::LeftThumbDeadzone == XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);

void XInputController::poll(const EventHandler& handler) noexcept {
    XINPUT_STATE state{};
    const bool connected =
        XInputGetState(playerIndex_, &state) == ERROR_SUCCESS;
    translator_.update(connected, state.Gamepad.wButtons,
                       state.Gamepad.sThumbLX, state.Gamepad.sThumbLY,
                       handler);
}

} // namespace usm::platform
