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
    // EnemyAttackInfo+4, copied to AIHitTargetInfo+0 by
    // IBehaviorBase::SpecialAnimActionCheck (0x003a8c60).
    std::int32_t hitType{100};
    // EnemyAttackInfo+8 is multiplied by the difficulty attack-time scale in
    // CBehaviorMeleeAttack::StateEnter (0x003baef8) before the selected
    // animation begins.
    float startupMilliseconds{};
    // EnemyAttackInfo+0xc controls startup turning in StateEnter/Update, and
    // +0xd/+0x10 gate and identify the paired QTE action.
    bool turnTowardTargetDuringStartup{};
    bool quickTimeEnabled{};
    std::int32_t quickTimeActionId{-1};
    float damage{};
    // EnemyAttackInfo+0x18 is copied to AIHitTargetInfo+0x24 and retained by
    // Player::OnHit as the post-hit protection interval when positive.
    float hitProtectionMilliseconds{};
    // EnemyAttackInfo+0x1c/+0x20 become AIHitTargetInfo+0x14/+0x18.  The
    // original Player::OnHit passes them to Unit::AddForce as horizontal and
    // vertical knockback magnitudes.
    float horizontalForce{};
    float verticalForce{};
    std::array<float, 4> hitBoxExtents{};
    float minimumAngleDegrees{};
    float maximumAngleDegrees{};
    // EnemyAttackInfo+0x3d, read as the second boolean after the two attack
    // angles. CBehaviorMeleeAttack::CanBeInterrupt (0x003b9644) returns this
    // while its native state is 9/11 (the live attack phase).
    bool interruptibleDuringExecution{};
    // EnemyAttackInfo+0x44 enables InitAttackMove's target-relative velocity;
    // +0x45 chooses the special-action key time instead of the full clip as
    // its duration; +0x46 permits an authored SpecialAnim successor.
    bool usesTargetRelativeMovement{};
    bool movementEndsAtSpecialAction{};
    bool permitsSpecialAnimationSuccessor{};
    // EnemyAttackInfo+0x48, copied by AISenseInfo into its first field and
    // then repacked at Player+0x514+4. Player::DoNormalSenseAction
    // (0x0034f630) uses values 1/6 for ordinary melee selection and values
    // 2..5 as fixed directional evade selectors.
    std::int32_t senseReactionType{};
    // EnemyAttackInfo+0x50, copied by AISenseInfo into the warning record
    // and ultimately passed as Application::SetSlowMotion's denominator
    // when the player accepts the registered Spider-Sense attack.
    float senseSlowMotionDenominator{1.0F};
    // EnemyAttackInfo+0x54, copied to AISenseInfo+8. Player::onMessage
    // (0x0034ddb4) routes non-negative values to the forced-sense/QTE path;
    // negative values are queued for the normal player-input path.
    std::int32_t forceSenseActionId{-1};
    // EnemyAttackInfo+0x58, copied to AISenseInfo+0x10 and passed to
    // Player::DoTakePhoto when non-negative.
    std::int32_t sensePhotoTargetId{-1};

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
