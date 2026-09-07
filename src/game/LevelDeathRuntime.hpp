#pragma once

#include <cstdint>

namespace usm::game {

// Portable state for CLevel's confirmation-producing CBlackScreen path. This
// covers the 2000 ms player-death fade and CProgressBar's authored 10 ms Rhino
// chase failure; it remains separate from CTriggerRestore's fall overlay.
class LevelDeathRuntime final {
public:
    void reset() noexcept;
    void update(bool playerDeadOver,
                std::uint32_t elapsedMilliseconds,
                std::uint32_t fadeDurationMilliseconds = 2000) noexcept;

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] bool confirmationReady() const noexcept {
        return confirmationReady_;
    }
    [[nodiscard]] float blackOverlayAlpha() const noexcept { return alpha_; }
    [[nodiscard]] std::uint32_t elapsedMilliseconds() const noexcept {
        return elapsedMilliseconds_;
    }

private:
    bool active_{};
    bool confirmationReady_{};
    std::uint32_t elapsedMilliseconds_{};
    std::uint32_t fadeDurationMilliseconds_{2000};
    float alpha_{};
};

} // namespace usm::game
