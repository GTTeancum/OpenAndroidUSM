#pragma once

#include "core/Result.hpp"
#include "game/CinematicScript.hpp"
#include "game/LevelOneBootstrap.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace usm::game {

class LevelCollision;

enum class EnemyBehaviorState {
    Disabled,
    Idle,
    Chasing,
    AttackRange,
    Hurt,
    Dead,
};

struct LevelEnemyState {
    const LevelEnemyAsset* asset{};
    assets::Vector3 position;
    assets::Vector3 facing{1.0F, 0.0F, 0.0F};
    std::array<float, 16> worldTransform{};
    std::string activeAnimation;
    std::uint32_t animationTimeMilliseconds{};
    float animationSpeed{1.0F};
    bool animationLoops{true};
    float health{};
    bool visible{};
    bool aiEnabled{};
    bool playerDetected{};
    EnemyBehaviorState behavior{EnemyBehaviorState::Disabled};
    std::uint32_t hurtVariantCursor{};
    std::uint32_t soundVariantCursor{};
    std::uint32_t rangeAttackVariantCursor{};
    std::uint32_t rangeAttackCooldownMilliseconds{};
};

struct EnemyPlayerHit {
    std::int32_t sourceObjectId{-1};
    std::int16_t attackId{-1};
    float damage{};
};

struct EnemySoundCue {
    std::int32_t sourceObjectId{-1};
    std::int32_t voxSoundId{-1};
};

// Portable counterpart of CGunLine. The original advances a short tracer at
// 1500 cm/s, checks each swept segment against the player and level, and
// retires it after two seconds.
struct EnemyGunLineState {
    std::int32_t sourceObjectId{-1};
    assets::Vector3 position;
    assets::Vector3 direction;
    float damage{};
    std::uint32_t ageMilliseconds{};
    bool active{};
};

// Mutable native state for the authored enemy objects. Cinematic command names
// remain intact so recovered scripts can manipulate state without binary hooks.
class LevelEnemyRuntime final {
public:
    [[nodiscard]] Result initialize(const LevelOneBootstrap& level);
    void advanceAnimations(std::uint32_t elapsedMilliseconds) noexcept;
    void updateGameplay(std::uint32_t elapsedMilliseconds,
                        const assets::Vector3& playerPosition,
                        const LevelCollision* collision = nullptr) noexcept;
    [[nodiscard]] std::optional<std::int32_t> applyPlayerMeleeHit(
        const assets::Vector3& attackPosition,
        const assets::Vector3& attackDirection, float radius, float damage,
        float minimumForwardDot = 0.0F) noexcept;
    [[nodiscard]] std::vector<EnemyPlayerHit> consumePlayerHits() noexcept;
    [[nodiscard]] std::vector<EnemySoundCue> consumeSoundCues() noexcept;
    [[nodiscard]] bool destroy(std::int32_t objectId) noexcept;
    [[nodiscard]] Result applyCinematicCommand(
        const LevelOneBootstrap& level, const CinematicThread& thread,
        const CinematicCommand& command);

    [[nodiscard]] std::span<const LevelEnemyState> states() const noexcept {
        return states_;
    }
    [[nodiscard]] std::span<const EnemyGunLineState> gunLines() const noexcept {
        return gunLines_;
    }
    [[nodiscard]] const LevelEnemyState* find(std::int32_t objectId) const
        noexcept;
    [[nodiscard]] const LevelEnemyState* shownHealthBarEnemy() const noexcept;

private:
    [[nodiscard]] LevelEnemyState* findMutable(std::int32_t objectId) noexcept;
    [[nodiscard]] float maximumAttackReach(
        const LevelEnemyState& enemy) const noexcept;
    void queueAuthoredAttackEvents(LevelEnemyState& enemy,
                                   std::uint32_t previousTimeMilliseconds,
                                   const assets::Vector3& playerPosition);
    void updateGunLines(std::uint32_t elapsedMilliseconds,
                        const assets::Vector3& playerPosition,
                        const LevelCollision* collision) noexcept;
    void startGunLineAttack(LevelEnemyState& enemy);
    [[nodiscard]] bool isGunLineEnemy(
        const LevelEnemyState& enemy) const noexcept;
    void queueStateSound(LevelEnemyState& enemy,
                         std::string_view behaviorStateName);
    void selectStateAnimation(LevelEnemyState& enemy,
                              std::string_view behaviorStateName,
                              bool loop);
    void enterDeadState(LevelEnemyState& enemy);

    std::vector<LevelEnemyState> states_;
    std::vector<EnemyPlayerHit> pendingPlayerHits_;
    std::vector<EnemySoundCue> pendingSoundCues_;
    std::vector<EnemyGunLineState> gunLines_;
    const LevelOneBootstrap* level_{};
    std::optional<std::int32_t> shownHealthBarObjectId_;
};

} // namespace usm::game
