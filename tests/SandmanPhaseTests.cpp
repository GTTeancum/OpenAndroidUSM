#include "game/SandmanPhaseRuntime.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
std::uint64_t checks{};

void check(bool value, const char* expression, int line) {
    ++checks;
    if (!value) {
        throw std::runtime_error(
            "line " + std::to_string(line) + ": " + expression);
    }
}
#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

bool close(float left, float right) {
    return std::abs(left - right) < 0.001F;
}
} // namespace

int main() {
    try {
        using usm::game::applySandmanPhaseDamage;
        using usm::game::sandmanGroundAttackAnimation;

        constexpr float maximumHealth = 2500.0F;

        // Truncated 67% does not cross the native "< 67" first boundary.
        auto update = applySandmanPhaseDamage(
            maximumHealth, maximumHealth, 0, 825.0F);
        CHECK(close(update.health, 1675.0F));
        CHECK(update.phase == 0U);

        // The next point below 67% clamps to exactly 66% and enters phase 1.
        update = applySandmanPhaseDamage(
            maximumHealth, maximumHealth, 0, 826.0F);
        CHECK(close(update.health, 1650.0F));
        CHECK(update.phase == 1U);

        // A very large surviving hit still advances at most one phase.
        update = applySandmanPhaseDamage(
            maximumHealth, maximumHealth, 0, 2000.0F);
        CHECK(close(update.health, 1650.0F));
        CHECK(update.phase == 1U);

        // Exactly 33% crosses the second native "<= 33" boundary.
        update = applySandmanPhaseDamage(
            1650.0F, maximumHealth, 1, 825.0F);
        CHECK(close(update.health, 825.0F));
        CHECK(update.phase == 2U);

        // Once phase 2 is active, later surviving damage is not clamped again.
        update = applySandmanPhaseDamage(
            825.0F, maximumHealth, 2, 100.0F);
        CHECK(close(update.health, 725.0F));
        CHECK(update.phase == 2U);

        // The death check precedes phase-table advancement.
        update = applySandmanPhaseDamage(
            maximumHealth, maximumHealth, 0, maximumHealth);
        CHECK(close(update.health, 0.0F));
        CHECK(update.phase == 0U);

        // The original guard also declines phase-table work without max health.
        update = applySandmanPhaseDamage(100.0F, 0.0F, 1, 25.0F);
        CHECK(close(update.health, 75.0F));
        CHECK(update.phase == 1U);

        CHECK(sandmanGroundAttackAnimation(0) == "ground_attack1");
        CHECK(sandmanGroundAttackAnimation(1) == "ground_attack12");
        CHECK(sandmanGroundAttackAnimation(2) == "ground_attack13");
        CHECK(sandmanGroundAttackAnimation(99) == "ground_attack13");

        std::cout << checks << " Sandman phase checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: "
                  << error.what() << '\n';
        return 1;
    }
}
