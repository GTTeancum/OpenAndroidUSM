#pragma once

#include <cstdint>

namespace usm::reconstructed {

// Android/Xperia Play key values consumed by the original appKeyPressed and
// appKeyReleased entry points. They are retained as named compatibility values;
// the Windows port routes them in source code and never calls into the ARM ELF.
enum class XperiaKeyCode : std::int32_t {
    Circle = 4,
    DpadUp = 19,
    DpadDown = 20,
    DpadLeft = 21,
    DpadRight = 22,
    Cross = 23,
    Square = 99,
    Triangle = 100,
    L1 = 102,
    R1 = 103,
    Start = 108,
    Select = 109,
};

enum class XperiaScanCode : std::int32_t {
    DpadUp = 103,
    DpadLeft = 105,
    DpadRight = 106,
    DpadDown = 108,
    Cross = 304,
    Circle = 305,
    Square = 307,
    Triangle = 308,
    L1 = 310,
    R1 = 311,
    Select = 314,
    Start = 315,
};

struct XperiaKeyEvent {
    XperiaKeyCode keyCode{};
    XperiaScanCode scanCode{};
    bool pressed{};
};

} // namespace usm::reconstructed
