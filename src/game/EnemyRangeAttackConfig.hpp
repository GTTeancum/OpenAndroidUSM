#pragma once

#include "core/Result.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace usm::game {

// Serialized 0x10-byte runtime payload loaded by
// EnemyAttributeFile::ReadEnemyRangeAttackInfo (0x0033b820). The exported
// record ID/name precede this payload in EnemysRangeAttackConfigs.bin.
struct EnemyRangeAttackDefinition {
    std::int16_t id{-1};
    std::string name;
    std::int32_t attackTypeMapId{-1};
    float animationDurationMilliseconds{};
    float projectileSpeedCentimetersPerSecond{};
    float damage{};
};

class EnemyRangeAttackConfigDatabase final {
public:
    [[nodiscard]] Result load(const std::filesystem::path& gameDataRoot);
    [[nodiscard]] Result load(std::span<const std::byte> bytes);

    [[nodiscard]] const EnemyRangeAttackDefinition* findByMapId(
        std::int32_t mapId) const noexcept;
    [[nodiscard]] const std::vector<EnemyRangeAttackDefinition>& definitions()
        const noexcept {
        return definitions_;
    }

private:
    std::vector<EnemyRangeAttackDefinition> definitions_;
};

} // namespace usm::game
