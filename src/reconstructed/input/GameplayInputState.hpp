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

// Source-level replacement for the original m_p* gameplay input globals.
// Keeping edge and held state together makes frame ownership explicit.
struct GameplayInputState {
    ActionButtonState web;
    ActionButtonState jump;
    ActionButtonState quickTimeEvent;
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
    }
};

} // namespace usm::reconstructed
