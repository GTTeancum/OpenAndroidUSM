#pragma once

#include "core/Result.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace usm::game {

constexpr std::size_t kEnemyTypeCount = 25;

struct EnemyAttackIntervalDefinition {
    std::int16_t id{-1};
    std::string name;
    std::int32_t weaponTypeMapIndex{-1};
    std::array<float, kEnemyTypeCount> intervalMilliseconds{};
};

// Converts an EnemyAttributeInfo ranged-attack map index to the E_WeaponType
// consumed by CBehaviorRangeAttack. Recovered from the table referenced by
// CEnemy::InitEntityAttribute (0x003373e8).
[[nodiscard]] std::optional<std::int32_t> resolveEnemyRangeWeaponType(
    std::int32_t authoredMapIndex) noexcept;

class EnemyAttackIntervalConfigDatabase final {
public:
    [[nodiscard]] Result load(const std::filesystem::path& gameDataRoot);
    [[nodiscard]] Result load(std::span<const std::byte> bytes);

    [[nodiscard]] const EnemyAttackIntervalDefinition* findForWeaponType(
        std::int32_t weaponType) const noexcept;
    [[nodiscard]] const EnemyAttackIntervalDefinition* findByWeaponTypeMapIndex(
        std::int32_t weaponTypeMapIndex) const noexcept;
    [[nodiscard]] const std::vector<EnemyAttackIntervalDefinition>& definitions()
        const noexcept {
        return definitions_;
    }

private:
    std::vector<EnemyAttackIntervalDefinition> definitions_;
};

} // namespace usm::game
