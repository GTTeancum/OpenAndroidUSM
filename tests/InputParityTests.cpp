#include "diagnostics/AutoplayHarness.hpp"
#include "game/LevelHostageRuntime.hpp"
#include "game/LevelOneBootstrap.hpp"
#include "game/PlayerStateConfig.hpp"
#include "platform/input/XInputStateTranslator.hpp"
#include "reconstructed/input/XperiaKeyRouter.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace usm::reconstructed;
using usm::platform::XInputStateTranslator;
namespace xi = usm::platform::xinput;
std::uint64_t checks = 0;

void check(bool condition, const char* expression, int line) {
    ++checks;
    if (!condition) {
        throw std::runtime_error("line " + std::to_string(line) + ": " + expression);
    }
}
#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

struct Binding {
    std::uint16_t mask;
    XperiaKeyCode key;
    XperiaScanCode scan;
};
constexpr std::array<Binding, 12> bindings{{
    {0x1000, XperiaKeyCode::Cross, XperiaScanCode::Cross},
    {0x2000, XperiaKeyCode::Circle, XperiaScanCode::Circle},
    {0x4000, XperiaKeyCode::Square, XperiaScanCode::Square},
    {0x8000, XperiaKeyCode::Triangle, XperiaScanCode::Triangle},
    {0x0100, XperiaKeyCode::L1, XperiaScanCode::L1},
    {0x0200, XperiaKeyCode::R1, XperiaScanCode::R1},
    {0x0020, XperiaKeyCode::Select, XperiaScanCode::Select},
    {0x0010, XperiaKeyCode::Start, XperiaScanCode::Start},
    {0x0001, XperiaKeyCode::DpadUp, XperiaScanCode::DpadUp},
    {0x0002, XperiaKeyCode::DpadDown, XperiaScanCode::DpadDown},
    {0x0004, XperiaKeyCode::DpadLeft, XperiaScanCode::DpadLeft},
    {0x0008, XperiaKeyCode::DpadRight, XperiaScanCode::DpadRight},
}};

void buttons() {
    // Exhaust every combination, including contradictory D-pad directions.
    // These assertions preserve the uploaded PC-adapter policy, not Android
    // hardware calibration. Values are independently written SDK constants.
    for (std::uint32_t combination = 0; combination < 4096; ++combination) {
        XInputStateTranslator translator;
        std::vector<XperiaKeyEvent> events;
        const auto emit = [&](const XperiaKeyEvent& event) { events.push_back(event); };
        std::uint16_t mask = 0;
        std::vector<Binding> expected;
        for (std::size_t i = 0; i < bindings.size(); ++i) {
            if ((combination & (1U << i)) != 0) {
                mask |= bindings[i].mask;
                expected.push_back(bindings[i]);
            }
        }
        translator.update(true, mask, 0, 0, emit);
        CHECK(translator.connected());
        CHECK(events.size() == expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i) {
            CHECK(events[i].keyCode == expected[i].key);
            CHECK(events[i].scanCode == expected[i].scan);
            CHECK(events[i].pressed);
        }
        events.clear();
        translator.update(true, mask, 0, 0, emit);
        CHECK(events.empty());
        translator.update(true, 0, 0, 0, emit);
        CHECK(events.size() == expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i) {
            CHECK(events[i].keyCode == expected[i].key);
            CHECK(events[i].scanCode == expected[i].scan);
            CHECK(!events[i].pressed);
        }
        events.clear();
        translator.update(true, 0, 0, 0, emit);
        CHECK(events.empty());
    }
    XInputStateTranslator translator;
    std::vector<XperiaKeyEvent> events;
    const auto emit = [&](const XperiaKeyEvent& event) { events.push_back(event); };
    translator.update(true, 0x0cc0, 0, 0, emit); // Unmapped API bits.
    CHECK(events.empty());
    translator.update(true, 0xffff, 32000, -32000, emit);
    CHECK(events.size() == bindings.size());
    events.clear();
    translator.update(false, 0xffff, 32000, -32000, emit);
    CHECK(!translator.connected());
    CHECK(translator.leftStick().x == 0.0F && translator.leftStick().y == 0.0F);
    CHECK(events.size() == 1 && events.front().deviceCancelled);
    CHECK(!events.front().pressed);
    events.clear();
    translator.update(false, 0, 0, 0, emit);
    CHECK(events.empty());
    translator.update(true, xi::A, 0, 0, emit);
    CHECK(events.size() == 1 && events.front().pressed);
    translator.update(true, xi::A, 0, 0, {}); // Empty callback is supported.
}

void routing() {
    XperiaKeyRouter router;
    XInputStateTranslator translator;
    auto frame = [&](std::uint16_t mask, bool connected = true) {
        router.beginFrame();
        translator.update(connected, mask, 0, 0, [&](const XperiaKeyEvent& event) {
            router.route(event, InputContext::Gameplay);
        });
    };
    frame(xi::RightShoulder);
    CHECK(!router.state().rescueRequested && !router.state().interactiveButtonRequested);
    frame(xi::RightShoulder);
    CHECK(!router.state().rescueRequested);
    frame(0);
    CHECK(router.state().rescueRequested && router.state().interactiveButtonRequested);
    CHECK(router.state().switchCounter == 4);
    CHECK(!router.state().punch.pressed && !router.state().quickTimeEvent.pressed);
    frame(0);
    CHECK(!router.state().rescueRequested && !router.state().interactiveButtonRequested);
    frame(xi::X);
    CHECK(router.state().punch.pressed && !router.state().rescueRequested);
    frame(0);
    frame(xi::A);
    CHECK(router.state().jump.pressed && router.state().quickTimeEvent.pressed);
    frame(xi::A);
    CHECK(router.state().jump.pressed && !router.state().quickTimeEvent.pressed);
    for (int i = 0; i < 32; ++i) {
        frame(xi::A);
        CHECK(router.state().jump.held && !router.state().jump.pressed);
        CHECK(router.state().quickTimeEvent.held && !router.state().quickTimeEvent.pressed);
    }
    frame(0);
    CHECK(router.state().quickTimeEvent.released);
    frame(xi::A);
    CHECK(router.state().quickTimeEvent.pressed);
    frame(0);

    // Native m_pQTEPressed survives a same-update Cross DOWN / UP pair.
    router.beginFrame();
    router.route({XperiaKeyCode::Cross, XperiaScanCode::Cross, true}, InputContext::Gameplay);
    router.route({XperiaKeyCode::Cross, XperiaScanCode::Cross, false}, InputContext::Gameplay);
    CHECK(router.state().quickTimeEvent.pressed);
    CHECK(router.state().quickTimeEvent.released && !router.state().quickTimeEvent.held);
    router.beginFrame();
    CHECK(!router.state().quickTimeEvent.pressed);
    router.route({XperiaKeyCode::R1, XperiaScanCode::L1, false}, InputContext::Gameplay);
    CHECK(!router.state().rescueRequested);
    for (const auto context : {InputContext::Menu, InputContext::UpgradeMenu}) {
        router.route({XperiaKeyCode::R1, XperiaScanCode::R1, false}, context);
        CHECK(!router.state().rescueRequested);
    }

    // CQTEManager::SetState (ELF 0x0037a7a0-0x0037a7b2) clears only
    // the direct IGM/Start press flag before the result handoff. It is not a
    // physical release and does not reset CKeyPad's separate press history.
    frame(xi::Start);
    CHECK(router.state().pause.pressed && router.state().pause.held);
    CHECK(router.gameplayKeypad().pause.pressed &&
          router.gameplayKeypad().pause.held);
    const auto directPauseFrames =
        router.state().pause.pressedFramesRemaining;
    const auto keypadPause = router.gameplayKeypad().pause;
    router.consumePausePress();
    CHECK(!router.state().pause.pressed);
    CHECK(router.state().pause.held && !router.state().pause.released);
    CHECK(router.state().pause.pressedFramesRemaining == directPauseFrames);
    CHECK(router.gameplayKeypad().pause.held == keypadPause.held);
    CHECK(router.gameplayKeypad().pause.pressed == keypadPause.pressed);
    CHECK(router.gameplayKeypad().pause.released == keypadPause.released);
    CHECK(router.gameplayKeypad().pause.pressedFramesRemaining ==
          keypadPause.pressedFramesRemaining);
    frame(0);
    CHECK(!router.state().pause.held && router.state().pause.released);

    // Device cancellation is a PC transport event, never native R1 UP.
    frame(xi::RightShoulder | xi::A | xi::X);
    frame(0, false);
    CHECK(!router.state().rescueRequested && !router.state().interactiveButtonRequested);
    CHECK(!router.state().jump.held && !router.state().jump.pressed && !router.state().jump.released);
    CHECK(!router.state().punch.held && !router.state().punch.pressed && !router.state().punch.released);
    CHECK(!router.state().quickTimeEvent.held && !router.state().quickTimeEvent.pressed && !router.state().quickTimeEvent.released);
    frame(0, false);
    CHECK(!router.state().rescueRequested);
    // Cancellation clears an old context without synthesizing menu selection.
    router.route({XperiaKeyCode::Cross, XperiaScanCode::Cross, true}, InputContext::Menu);
    CHECK(router.state().menuSelected.held);
    router.route({{}, {}, false, true}, InputContext::Gameplay);
    CHECK(!router.state().menuSelected.held && !router.state().menuSelected.released);
}

void sticks() {
    constexpr std::array<std::int16_t, 15> values{{
        -32768, -32767, -9001, -9000, -7849, -1, 0, 1,
        7848, 7849, 7850, 9000, 9001, 32766, 32767}};
    for (const auto x : values) {
        for (const auto y : values) {
            XInputStateTranslator translator;
            std::vector<XperiaKeyEvent> events;
            translator.update(true, 0, x, y, [&](const XperiaKeyEvent& event) {
                events.push_back(event);
            });
            const auto stick = translator.leftStick();
            CHECK(std::isfinite(stick.x) && std::isfinite(stick.y));
            CHECK(stick.x * stick.x + stick.y * stick.y <= 1.000001F);
            const auto has = [&](XperiaKeyCode key) {
                return std::any_of(events.begin(), events.end(), [&](const auto& event) {
                    return event.keyCode == key && event.pressed;
                });
            };
            CHECK(has(XperiaKeyCode::DpadLeft) == (x < -9000));
            CHECK(has(XperiaKeyCode::DpadRight) == (x > 9000));
            CHECK(has(XperiaKeyCode::DpadDown) == (y < -9000));
            CHECK(has(XperiaKeyCode::DpadUp) == (y > 9000));
            if (x == 0 && std::abs(static_cast<int>(y)) <= 7849) {
                CHECK(stick.x == 0.0F && stick.y == 0.0F);
            }
            if (y == 0 && std::abs(static_cast<int>(x)) <= 7849) {
                CHECK(stick.x == 0.0F && stick.y == 0.0F);
            }
        }
    }
    XInputStateTranslator translator;
    translator.update(true, 0, 32767, 0, {});
    CHECK(translator.leftStick().x == 1.0F && translator.leftStick().y == 0.0F);
    translator.update(true, 0, -32768, 0, {});
    CHECK(translator.leftStick().x == -1.0F && translator.leftStick().y == 0.0F);
}

struct HostageFixture {
    usm::game::LevelObjectRuntime objects;
    usm::game::LevelBonusRuntime bonuses;
    usm::game::QuickTimeEventRuntime qte;
    usm::game::LevelHostageRuntime hostage;
    usm::game::GameplayPlayer player;
    XperiaKeyRouter router;
    XInputStateTranslator translator;
    std::size_t initialBonusCount{};
    std::uint32_t timerMilliseconds{};
    HostageFixture(const usm::game::LevelOneBootstrap& level,
                   const usm::game::PlayerStateConfigDatabase& configs) {
        CHECK(objects.initialize(level));
        CHECK(bonuses.initialize(level.bonuses()));
        qte.bind(level.buttonConfigs(), level.hud().interfaceAtlas);
        CHECK(hostage.initialize(level, qte, &objects));
        CHECK(player.initialize(level.player(), nullptr, &configs));
        const auto* state = hostage.find(30018);
        CHECK(state != nullptr && state->asset != nullptr);
        CHECK(state->asset->roomId == 2);
        player.restoreAt(state->asset->position, {1.0F, 0.0F, 0.0F});
        initialBonusCount = bonuses.states().size();
        frame(0);
        CHECK(hostage.contextPromptVisible());
    }
    void frame(std::uint16_t buttons, std::uint32_t elapsed = 0) {
        frameClocks(buttons, elapsed, elapsed);
    }
    void frameClocks(std::uint16_t buttons, std::uint32_t simulation,
                     std::uint32_t real) {
        timerMilliseconds += real;
        const usm::game::HostageTimeStep time{simulation, real, timerMilliseconds};
        router.beginFrame();
        translator.update(true, buttons, 0, 0, [&](const XperiaKeyEvent& event) {
            router.route(event, InputContext::Gameplay);
        });
        CHECK(hostage.update(player, objects, bonuses, time,
              {router.state().rescueRequested, router.state().quickTimeEvent.pressed}));
    }
    const usm::game::LevelHostageState& state() const { return *hostage.find(30018); }
    void start() {
        frame(xi::RightShoulder);
        CHECK(state().phase == usm::game::HostageRescuePhase::Tied);
        frame(0);
        CHECK(player.activeStateId() == 27);
        CHECK(player.activeAnimation() == "stand_to_unhitch");
        player.update({}, {}, 1000);
        frame(0);
        CHECK(hostage.quickTimeActive());
        CHECK(player.activeStateId() == 28);
        CHECK(state().requiredActions == 8);
        CHECK(state().quickTimeDurationMilliseconds == 4000);
    }
};

void hostage() {
    const std::filesystem::path dataRoot = USM_TEST_GAME_DATA_ROOT;
    // Fail, rather than skip the substantive test, if original data is absent.
    CHECK(std::filesystem::is_regular_file(dataRoot / "configs.pack"));
    usm::game::LevelOneBootstrap level;
    CHECK(level.load(dataRoot));
    usm::game::PlayerStateConfigDatabase configs;
    CHECK(configs.load(dataRoot));
    CHECK(level.buttonConfigs().find(11)->interactionType == 3);
    {
        HostageFixture f(level, configs);
        f.frame(xi::X); f.frame(0);
        f.frame(xi::A); f.frame(0);
        CHECK(f.state().phase == usm::game::HostageRescuePhase::Tied);
        f.start();
        f.frame(xi::RightShoulder); f.frame(0);
        f.frame(xi::X); f.frame(0);
        CHECK(f.state().completedActions == 0);
        f.frame(xi::A);
        CHECK(f.state().completedActions == 1);
        for (int i = 0; i < 8; ++i) { f.frame(xi::A, 50); }
        CHECK(f.state().completedActions == 1); // Not nine taps.
        f.frame(0, 99);
        CHECK(f.state().completedActions == 1);
        f.frame(0, 1);
        CHECK(f.state().completedActions == 0); // Exactly 500 ms idle.
        for (int i = 0; i < 8; ++i) {
            f.frame(xi::A, 1);
            if (i < 7) { CHECK(f.state().completedActions == i + 1); }
            f.frame(0, 1);
        }
        CHECK(f.state().phase == usm::game::HostageRescuePhase::RescueEnd);
        CHECK(f.player.activeStateId() == 29);
        CHECK(f.player.activeAnimation() == "unhitch_to_stand");
        f.player.update({}, {}, 1000);
        f.frame(0);
        CHECK(f.state().phase == usm::game::HostageRescuePhase::Release);
        CHECK(f.bonuses.states().size() == f.initialBonusCount + 10);
        f.objects.advanceAnimations(100000); f.frame(0);
        CHECK(f.state().phase == usm::game::HostageRescuePhase::Thank);
        f.objects.advanceAnimations(100000); f.frame(0);
        CHECK(f.state().phase == usm::game::HostageRescuePhase::Freed);
        CHECK(f.objects.find(30018)->activeAnimation == "idle_watching_idle");
    }
    {
        HostageFixture f(level, configs);
        f.start();
        f.frame(xi::A); f.frame(0);
        f.frame(0, 4001);
        CHECK(f.state().quickTimeOutcome == usm::game::HostageQteOutcome::Failure);
        f.frame(0); // Object observes the manager result on its next update.
        CHECK(f.state().phase == usm::game::HostageRescuePhase::Tied);
        CHECK(f.state().completedActions == 0);
        CHECK(f.player.activeStateId() == 0);
        CHECK(f.bonuses.states().size() == f.initialBonusCount);
        f.start();
        CHECK(f.state().completedActions == 0);
        f.frame(xi::A);
        CHECK(f.state().completedActions == 1);
        // Native GetRealTs is independent of simulation slow motion.
        f.frameClocks(0, 0, 499);
        CHECK(f.state().completedActions == 1);
        f.frameClocks(0, 0, 1);
        CHECK(f.state().completedActions == 0);
        CHECK(f.state().quickTimeElapsedMilliseconds == 500);
    }
}

struct TemporaryDirectory {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("usm-re01-autoplay-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryDirectory() { std::filesystem::create_directories(path); }
    ~TemporaryDirectory() { std::error_code error; std::filesystem::remove_all(path, error); }
};

void autoplay() {
    TemporaryDirectory temporary;
    const auto script = temporary.path / "rescue.usmauto";
    { std::ofstream stream(script); stream << "auto_qte 1\nrescue_hostage 10000 30018 freed\nfinish\n"; }
    usm::diagnostics::AutoplayHarness harness;
    CHECK(harness.initialize(script, temporary.path / "trace"));
    usm::game::LevelObjectAsset asset;
    asset.objectId = 30018;
    std::array<usm::game::LevelHostageState, 1> states{};
    states[0].asset = &asset;
    states[0].promptVisible = true;
    usm::diagnostics::AutoplaySnapshot snapshot;
    snapshot.gameplayActive = true;
    snapshot.controlsEnabled = true;
    snapshot.hostages = states;
    auto input = harness.update(snapshot);
    CHECK(input.rescueRequested && !input.punchPressed && !input.quickTimeEventPressed);
    states[0].phase = usm::game::HostageRescuePhase::QuickTime;
    snapshot.hostageQuickTimeEventActive = true;
    snapshot.controlsEnabled = false;
    snapshot.frameIndex = 1;
    snapshot.realTimeMilliseconds = 50;
    input = harness.update(snapshot);
    CHECK(!input.rescueRequested && !input.punchPressed && input.quickTimeEventPressed);
}
} // namespace

int main(int argc, char** argv) {
    try {
        const std::string_view group = argc == 2 ? argv[1] : "all";
        bool matched = false;
        const auto run = [&](std::string_view name, auto function) {
            if (group == "all" || group == name) {
                matched = true;
                function();
                std::cout << "PASS " << name << '\n';
            }
        };
        run("buttons", buttons);
        run("routing", routing);
        run("sticks", sticks);
        run("hostage", hostage);
        run("autoplay", autoplay);
        if (!matched) { throw std::runtime_error("Unknown test group"); }
        std::cout << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
