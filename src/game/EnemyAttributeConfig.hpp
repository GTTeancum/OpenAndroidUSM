#pragma once

#include "core/Result.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace usm::game {

// The ranged-attack sequence embedded in EnemyAttributeInfo. The original
// runtime addresses these records by vector index, while the exported ID and
// name remain useful evidence when assigning recovered names.
struct EnemyAttributeDefinition {
    std::int16_t exportedId{-1};
    std::int16_t enemyTypeId{-1};
    std::string name;
    std::vector<std::int32_t> rangedAttackTypeMapIndices;
};

class EnemyAttributeConfigDatabase final {
public:
    [[nodiscard]] Result load(const std::filesystem::path& gameDataRoot);
    [[nodiscard]] Result load(std::span<const std::byte> bytes);

    [[nodiscard]] const EnemyAttributeDefinition* find(
        std::int16_t enemyTypeId) const noexcept;
    [[nodiscard]] const std::vector<EnemyAttributeDefinition>& definitions()
        const noexcept {
        return definitions_;
    }

private:
    std::vector<EnemyAttributeDefinition> definitions_;
};

} // namespace usm::game
