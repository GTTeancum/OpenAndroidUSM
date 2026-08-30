#pragma once

#include <cstdint>

namespace usm::game {

// Reconstructs the immediate and trailing health values maintained by
// CLevel::UpdateInferfaceHealthAndWebPower at original address 0x0037d860.
class PlayerHudHealthState final {
public:
    void initialize(float health, float maximumHealth) noexcept;
    void update(float health, std::uint32_t deltaMilliseconds) noexcept;

    [[nodiscard]] float currentRatio() const noexcept;
    [[nodiscard]] float delayedRatio() const noexcept;

private:
    float maximumHealth_{1.0F};
    float currentHealth_{1.0F};
    float delayedHealth_{1.0F};
    float delayedDrainPerMillisecond_{};
    std::uint32_t delayRemainingMilliseconds_{};
};

} // namespace usm::game
