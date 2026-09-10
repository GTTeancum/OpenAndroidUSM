#pragma once

#include "core/Result.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace usm::game {

struct EnemyAnimationSpecialAction {
    std::int16_t recordId{};
    std::string name;
    std::int16_t enemyTypeId{};
    std::string animationName;
    std::int32_t actionType{};
    std::int32_t keyFramePercent{};
    std::int32_t attackId{-1};
    // BehaviorSoundMapList row IDs, resolved by the original
    // IBehaviorBase::SpecialAnimActionCheck at 0x003a8c60.
    std::vector<std::int16_t> soundMapIds;
    // AIAnimSpecialActionInfo+0x38: SpecialAnimActionCheck (0x003a8c60)
    // resolves this as an animation index and stores it at IBehaviorBase+0x6c.
    // SpecialAnimNextActionCheck (0x003a857c) consumes it as the next clip.
    std::string nextAnimationName;
};

// Typed reader for EnemysSpecialAnimConfigs.bin, reconstructed from
// EnemyAttributeFile::ReadAnimSpeciaActionInfo (0x0033b9d8). Action type zero
// is the authored attack-volume event consumed by CBehaviorMeleeAttack.
class EnemySpecialActionConfigDatabase final {
public:
    [[nodiscard]] Result load(const std::filesystem::path& gameDataRoot);
    [[nodiscard]] Result load(std::span<const std::byte> bytes);

    [[nodiscard]] std::span<const EnemyAnimationSpecialAction> actions()
        const noexcept {
        return actions_;
    }
    [[nodiscard]] std::vector<const EnemyAnimationSpecialAction*> findAttackEvents(
        std::int16_t enemyTypeId, std::string_view animationName) const;
    [[nodiscard]] std::vector<const EnemyAnimationSpecialAction*> findEvents(
        std::int16_t enemyTypeId, std::string_view animationName) const;

private:
    std::vector<EnemyAnimationSpecialAction> actions_;
};

} // namespace usm::game
