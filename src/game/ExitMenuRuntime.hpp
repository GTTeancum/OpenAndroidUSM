#pragma once

#include "core/Result.hpp"
#include "game/LocalizedStringTable.hpp"

#include <cstdint>
#include <string>

namespace usm::game {

// Source-level reconstruction of GS_ExitMenu mode 3, which is pushed by
// CLevel::DoConfirmation (0x003836f8) after selecting No on the death prompt.
// GS_ExitMenu::Update (0x002c0150) advances one state per 50 ms update and
// HandleGoToMainMenuWhenFail (0x002bfcb8) replaces the failed level with the
// main menu during states 16-20.
class ExitMenuRuntime final {
public:
    [[nodiscard]] Result bind(const LevelTextCatalog& strings);
    void beginAfterDeath() noexcept;
    void reset() noexcept;
    void update(std::uint32_t elapsedMilliseconds) noexcept;

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] std::uint32_t state() const noexcept { return state_; }
    [[nodiscard]] bool parentVisible() const noexcept {
        return active_ && state_ < 15;
    }
    [[nodiscard]] bool loadingTextVisible() const noexcept;
    [[nodiscard]] float blackOverlayAlpha() const noexcept;
    [[nodiscard]] bool mainMenuRequested() const noexcept {
        return active_ && state_ >= 20;
    }
    [[nodiscard]] const std::u16string& loadingLabel() const noexcept {
        return loadingLabel_;
    }
    [[nodiscard]] const std::u16string& loadingSuffix() const noexcept {
        return loadingSuffix_;
    }

private:
    std::u16string loadingLabel_;
    std::u16string loadingSuffix_;
    float accumulatorMilliseconds_{};
    std::uint32_t state_{};
    bool active_{};
};

} // namespace usm::game
