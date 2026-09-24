// Deterministic transition stress test of the production PC adapter/router.
// This checks inherited PC mapping and lifecycle behavior; it is not an
// execution or visual comparison against the ARM game.
#include "platform/input/XInputStateTranslator.hpp"
#include "reconstructed/input/XperiaKeyRouter.hpp"

#include <cstdint>
#include <iostream>

int main() {
    using namespace usm::reconstructed;
    namespace xi = usm::platform::xinput;
    usm::platform::XInputStateTranslator translator;
    XperiaKeyRouter router;
    std::uint32_t random = 0x55534d01;
    std::uint16_t previous = 0;
    bool previousJumpEdge = false;
    bool previousPunchEdge = false;
    std::uint64_t checks = 0;
    const auto check = [&](bool condition, std::uint32_t frame, const char* name) {
        ++checks;
        if (!condition) {
            std::cerr << "FAIL frame=" << frame << " property=" << name << '\n';
        }
        return condition;
    };
    for (std::uint32_t frame = 0; frame < 100000; ++frame) {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        const bool connected = (random & 0x1f0000U) != 0;
        const auto raw = static_cast<std::uint16_t>(random);
        const std::uint16_t current = connected ? raw : 0;
        const std::uint16_t down = static_cast<std::uint16_t>(current & ~previous);
        const std::uint16_t up = static_cast<std::uint16_t>(previous & ~current);
        router.beginFrame();
        translator.update(connected, raw, 0, 0, [&](const XperiaKeyEvent& event) {
            router.route(event, InputContext::Gameplay);
        });
        const auto& state = router.state();
        const bool jumpHeld = (current & xi::A) != 0;
        const bool punchHeld = (current & xi::X) != 0;
        const bool jumpEdge = (down & xi::A) != 0;
        const bool punchEdge = (down & xi::X) != 0;
        if (!check(translator.connected() == connected, frame, "connected") ||
            !check(state.jump.held == jumpHeld, frame, "jump held") ||
            !check(state.punch.held == punchHeld, frame, "punch held") ||
            !check(state.quickTimeEvent.held == jumpHeld, frame, "QTE held") ||
            !check(state.quickTimeEvent.pressed == jumpEdge, frame, "single QTE edge") ||
            !check(state.jump.pressed == (jumpHeld && (jumpEdge || previousJumpEdge)), frame, "two-update jump window") ||
            !check(state.punch.pressed == (punchHeld && (punchEdge || previousPunchEdge)), frame, "two-update punch window") ||
            !check(state.rescueRequested == (connected && ((up & xi::RightShoulder) != 0)), frame, "R1 release") ||
            !check(state.interactiveButtonRequested == state.rescueRequested, frame, "interactive button event") ||
            !check(!state.interactiveButtonRequested || state.switchCounter == 4, frame, "switch counter seed") ||
            !check(state.web.held == ((current & xi::B) != 0), frame, "web held") ||
            !check(state.spiderSense.held == ((current & xi::LeftShoulder) != 0), frame, "sense held")) {
            return 1;
        }
        previous = current;
        previousJumpEdge = jumpEdge;
        previousPunchEdge = punchEdge;
    }
    std::cout << "PASS 100000 deterministic transition frames; " << checks << " checks\n";
    return 0;
}
