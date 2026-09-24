#pragma once

#include <cstdint>
#include <string_view>

namespace usm::game {

struct SandmanPhaseUpdate {
    float health{};
    std::uint32_t phase{};
};

// CBoss::ParseLocalAiMessage (ELF 0x0032dea8): the local-hit path checks
// death before advancing the phase table. Surviving hits clamp at 66/33
// percent and one hit advances at most one phase.
[[nodiscard]] SandmanPhaseUpdate applySandmanPhaseDamage(
    float health, float maximumHealth, std::uint32_t phase,
    float damage) noexcept;

// CBoss::OnEnterState(3) (ELF 0x0032cd58): the phase table's float parameter
// selects the initial Sandman ground-attack clip 1/2/3.
[[nodiscard]] std::string_view sandmanGroundAttackAnimation(
    std::uint32_t phase) noexcept;

} // namespace usm::game
