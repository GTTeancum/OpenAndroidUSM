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
    float collisionRadius{};
    float collisionHeight{};
    // ReadAttributeInfo's three F32 movement fields (+0x10/14/18).
    // SetMoveLineSpeed converts their cm/ms values to cm/s internally.
    float walkSpeedCentimetersPerMillisecond{};
    float runSpeedCentimetersPerMillisecond{};
    float wallSpeedCentimetersPerMillisecond{};
    float minimumMeleeAttackDistance{};
    float maximumMeleeAttackDistance{};
    // ReadAttributeInfo (0x0033bf00) stores these serialized integer fields
    // at EnemyAttributeInfo +0x24/+0x28. CEnemy::InitEntityAttribute
    // (0x003373e8) squares them into CEnemy +0x3d8/+0x3d4, and
    // CEnemy::MoveToRangeAttackPlayerRange (0x003316b0) selects a desired
    // range between them.
    float minimumRangeAttackDistance{};
    float maximumRangeAttackDistance{};
    // CEnemy::InitEntityAttribute copies the three consecutive bytes at
    // EnemyAttributeInfo+0x48..0x4a to CEnemy+0x452..0x454.
    // ProcessHitInfo uses them to retain or suppress the two hit-force
    // components and launch-class reactions for each enemy archetype.
    bool allowsHorizontalHitForce{};
    bool allowsVerticalHitForce{};
    bool allowsLaunchHitType{};
    // EnemyAttributeInfo+0x4b is copied to Unit+0x20d. The virtual
    // Unit::CanBeCounterHit (0x002fe8ec) returns that byte directly, and
    // Player::DoNormalSenseAction uses it to choose counter versus evade.
    bool canBeCounterHit{};
    std::vector<std::int32_t> rangedAttackTypeMapIndices;
    // EnemysBehaviorConfigs.bin stores indices into the native
    // k_enemy_behavior_map_table. Slot 4 maps to interface 0x132,
    // CBehaviorTiedUp, and is the capability queried by air web targeting.
    std::vector<std::int32_t> behaviorTypeMapIndices;

    [[nodiscard]] bool canBeTiedUp() const noexcept;
    [[nodiscard]] bool canBeDraggedTo() const noexcept;
    [[nodiscard]] bool canMoveOnWall() const noexcept;
};

class EnemyAttributeConfigDatabase final {
public:
    [[nodiscard]] Result load(const std::filesystem::path& gameDataRoot);
    [[nodiscard]] Result load(std::span<const std::byte> bytes);
    [[nodiscard]] Result loadBehaviors(std::span<const std::byte> bytes);

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
