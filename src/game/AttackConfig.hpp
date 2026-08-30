#pragma once

#include "core/Result.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace usm::game {

struct AttackDefinition {
    std::int16_t id{-1};
    std::string name;
    float damage{};
    std::array<float, 4> hitBoxExtents{};
    float minimumAngleDegrees{};
    float maximumAngleDegrees{};

    [[nodiscard]] float maximumReach() const noexcept;
};

// Typed reader for EnemysAttackConfigs.bin, reconstructed from
// EnemyAttributeFile::ReadEnemyAttackInfo at original address 0x0033c2c0.
class AttackConfigDatabase final {
public:
    [[nodiscard]] Result load(const std::filesystem::path& gameDataRoot);
    [[nodiscard]] Result load(std::span<const std::byte> bytes);

    [[nodiscard]] const AttackDefinition* find(std::int16_t id) const noexcept;
    [[nodiscard]] const std::vector<AttackDefinition>& attacks() const noexcept {
        return attacks_;
    }

private:
    std::vector<AttackDefinition> attacks_;
};

} // namespace usm::game
