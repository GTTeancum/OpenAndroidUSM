#pragma once

namespace usm::reconstructed {

struct ActionButtonState {
    bool held{};
    bool pressed{};
    bool released{};

    void beginFrame() noexcept {
        pressed = false;
        released = false;
    }

    void set(bool isHeld) noexcept {
        if (held == isHeld) {
            return;
        }
        held = isHeld;
        pressed = isHeld;
        released = !isHeld;
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
