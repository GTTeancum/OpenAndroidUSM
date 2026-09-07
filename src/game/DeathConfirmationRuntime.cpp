#include "game/DeathConfirmationRuntime.hpp"

namespace usm::game {

Result DeathConfirmationRuntime::bind(const LevelTextCatalog& strings) {
    const auto copyMain = [&strings](std::size_t index,
                                     std::u16string& output) {
        const std::u16string* value = strings.main().at(index);
        if (value == nullptr) {
            return false;
        }
        output = *value;
        return true;
    };
    // GS_Confirmation::Render/Create at 0x002dbfa8/0x002dc2ec.
    if (!copyMain(0x22, title_) || !copyMain(0x30, message_) ||
        !copyMain(0x2e, yes_) || !copyMain(0x2f, no_)) {
        return Result::failure(
            "Death confirmation strings are missing from Main");
    }
    reset();
    return Result::success();
}

void DeathConfirmationRuntime::reset() noexcept {
    active_ = false;
    selection_ = 0;
    outcome_ = DeathConfirmationOutcome::None;
}

void DeathConfirmationRuntime::update(
    bool confirmationReady, bool upReleased, bool downReleased,
    bool menuSelectedReleased) noexcept {
    if (!confirmationReady) {
        reset();
        return;
    }
    if (!active_) {
        // CLevel::UpdateBlackScreen (0x003805f0) sets GS_Confirmation+0xc0
        // to zero immediately before pushing the new state.
        active_ = true;
        selection_ = 0;
        outcome_ = DeathConfirmationOutcome::None;
        return;
    }
    if (upReleased) {
        selection_ = selection_ == 0 ? 1 : 0;
    } else if (downReleased) {
        selection_ = selection_ == 1 ? 0 : 1;
    }
    if (menuSelectedReleased) {
        // CLevel::DoConfirmation (0x003836f8) receives zero for Yes and one
        // for No from GS_Confirmation::Update.
        outcome_ = selection_ == 0 ? DeathConfirmationOutcome::Retry
                                   : DeathConfirmationOutcome::Exit;
    }
}

DeathConfirmationOutcome DeathConfirmationRuntime::consumeOutcome() noexcept {
    const DeathConfirmationOutcome result = outcome_;
    outcome_ = DeathConfirmationOutcome::None;
    return result;
}

DeathConfirmationFrame DeathConfirmationRuntime::frame() const {
    return {active_, selection_, title_, message_, yes_, no_};
}

} // namespace usm::game
