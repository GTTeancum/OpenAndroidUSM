#include "game/SandmanPhaseRuntime.hpp"

#include <algorithm>

namespace usm::game {

SandmanPhaseUpdate applySandmanPhaseDamage(
    float health, float maximumHealth, std::uint32_t phase,
    float damage) noexcept {
    health = std::max(0.0F, health - damage);
    if (health > 0.0F && maximumHealth > 0.0F) {
        const auto healthPercent = static_cast<std::int32_t>(
            health * 100.0F / maximumHealth);
        if (healthPercent < 67 && phase < 1U) {
            health = maximumHealth * 0.66F;
            phase = 1;
        } else if (healthPercent <= 33 && phase <= 1U) {
            health = maximumHealth * 0.33F;
            phase = 2;
        }
    }
    return {health, phase};
}

std::string_view sandmanGroundAttackAnimation(
    std::uint32_t phase) noexcept {
    if (phase == 0U) {
        return "ground_attack1";
    }
    if (phase == 1U) {
        return "ground_attack12";
    }
    return "ground_attack13";
}

} // namespace usm::game
