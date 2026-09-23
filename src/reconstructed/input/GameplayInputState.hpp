#pragma once

#include <cstdint>

namespace usm::reconstructed {

struct ActionButtonState {
    bool held{};
    bool pressed{};
    bool released{};
    std::uint8_t pressedFramesRemaining{};

    void beginFrame() noexcept {
        // CKeyPad::update (0x002f9434) copies a new press as state 1 and
        // advances the real-time state to 2. The next update copies state 2
        // before advancing to 3. CKeyPad::wasKeyPressed (0x002f964c) accepts
        // exactly states 1 and 2, so every physical press remains visible to
        // gameplay for two updates rather than only the event's first frame.
        pressed = pressedFramesRemaining != 0;
        if (pressedFramesRemaining != 0) {
            --pressedFramesRemaining;
        }
        released = false;
    }

    void set(bool isHeld) noexcept {
        if (held == isHeld) {
            return;
        }
        held = isHeld;
        pressed = isHeld;
        released = !isHeld;
        pressedFramesRemaining = isHeld ? 1U : 0U;
    }
};

// appKeyPressed (ELF 0x003cc75c; Ghidra 0x003dc75c) sets
// m_pQTEPressed on Cross DOWN. CQTEManager::Update (ELF 0x0037b240;
// Ghidra 0x0038b240) consumes that flag once, independently of CKeyPad.
// In particular, holding Cross must not contribute a second QTE action on
// the next update. A release in the same frame does not erase the press.
struct EventButtonState {
    bool held{};
    bool pressed{};
    bool released{};

    void beginFrame() noexcept { pressed = false; released = false; }
    void set(bool isHeld) noexcept {
        if (held == isHeld) {
            return;
        }
        held = isHeld;
        if (isHeld) {
            pressed = true;
        } else {
            released = true;
        }
    }
};

// Source-level replacement for the original m_p* gameplay input globals.
// Keeping edge and held state together makes frame ownership explicit.
struct GameplayInputState {
    ActionButtonState web;
    ActionButtonState jump;
    EventButtonState quickTimeEvent;
    ActionButtonState punch;
    ActionButtonState spiderSense;
    ActionButtonState superAttack;
    ActionButtonState moveUp;
    ActionButtonState moveDown;
    ActionButtonState moveLeft;
    ActionButtonState moveRight;
    ActionButtonState upgrade;
    ActionButtonState upgradeProceed;
    ActionButtonState pause;
    ActionButtonState menuSelected;
    // R1 UP sets these separate native globals; it is neither a punch nor
    // a QTE tap. The switch payload is retained without inventing a switch
    // target or assigning meaning to the native literal 4.
    bool rescueRequested{};
    bool switchRequested{};
    std::int32_t switchRequestValue{};

    void beginFrame() noexcept {
        web.beginFrame();
        jump.beginFrame();
        quickTimeEvent.beginFrame();
        punch.beginFrame();
        spiderSense.beginFrame();
        superAttack.beginFrame();
        moveUp.beginFrame();
        moveDown.beginFrame();
        moveLeft.beginFrame();
        moveRight.beginFrame();
        upgrade.beginFrame();
        upgradeProceed.beginFrame();
        pause.beginFrame();
        menuSelected.beginFrame();
        rescueRequested = false;
        switchRequested = false;
    }
};

} // namespace usm::reconstructed
