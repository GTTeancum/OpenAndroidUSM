#include "game/PlayerHudHealthState.hpp"

#include <algorithm>

namespace usm::game {
namespace {

// Literal pool values referenced by the original routine: a 50 ms hold and
// (damage * 1000 / 500) / 1000, i.e. a 500 ms trailing-bar drain.
constexpr std::uint32_t kDamageHoldMilliseconds = 50;
constexpr float kDamageDrainMilliseconds = 500.0F;

} // namespace

void PlayerHudHealthState::initialize(float health,
                                      float maximumHealth) noexcept {
    maximumHealth_ = std::max(maximumHealth, 1.0F);
    currentHealth_ = std::clamp(health, 0.0F, maximumHealth_);
    delayedHealth_ = currentHealth_;
    delayedDrainPerMillisecond_ = 0.0F;
    delayRemainingMilliseconds_ = 0;
}

void PlayerHudHealthState::update(float health,
                                  std::uint32_t deltaMilliseconds) noexcept {
    const float nextHealth = std::clamp(health, 0.0F, maximumHealth_);
    if (nextHealth > currentHealth_) {
        delayedHealth_ = nextHealth;
        delayedDrainPerMillisecond_ = 0.0F;
        delayRemainingMilliseconds_ = 0;
    } else if (nextHealth < currentHealth_) {
        delayedHealth_ = std::max(delayedHealth_, currentHealth_);
        delayedDrainPerMillisecond_ =
            (delayedHealth_ - nextHealth) / kDamageDrainMilliseconds;
        delayRemainingMilliseconds_ = kDamageHoldMilliseconds;
    }
    currentHealth_ = nextHealth;

    std::uint32_t drainMilliseconds = deltaMilliseconds;
    if (delayRemainingMilliseconds_ != 0) {
        const std::uint32_t heldMilliseconds =
            std::min(delayRemainingMilliseconds_, drainMilliseconds);
        delayRemainingMilliseconds_ -= heldMilliseconds;
        drainMilliseconds -= heldMilliseconds;
    }
    if (drainMilliseconds != 0 && delayedHealth_ > currentHealth_) {
        delayedHealth_ = std::max(
            currentHealth_, delayedHealth_ -
                                delayedDrainPerMillisecond_ *
                                    static_cast<float>(drainMilliseconds));
    }
    if (delayedHealth_ <= currentHealth_) {
        delayedHealth_ = currentHealth_;
        delayedDrainPerMillisecond_ = 0.0F;
    }
}

float PlayerHudHealthState::currentRatio() const noexcept {
    return currentHealth_ / maximumHealth_;
}

float PlayerHudHealthState::delayedRatio() const noexcept {
    return delayedHealth_ / maximumHealth_;
}

} // namespace usm::game
