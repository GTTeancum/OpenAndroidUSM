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
    Dead,
};

struct LevelEnemyState {
    const LevelEnemyAsset* asset{};
    assets::Vector3 position;
    std::array<float, 16> worldTransform{};
    std::string activeAnimation;
    std::uint32_t animationTimeMilliseconds{};
    float animationSpeed{1.0F};
    float health{};
    bool visible{};
    bool aiEnabled{};
    bool playerDetected{};
    EnemyBehaviorState behavior{EnemyBehaviorState::Disabled};
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
    [[nodiscard]] Result applyCinematicCommand(
        const LevelOneBootstrap& level, const CinematicThread& thread,
        const CinematicCommand& command);

    [[nodiscard]] std::span<const LevelEnemyState> states() const noexcept {
        return states_;
    }
    [[nodiscard]] const LevelEnemyState* find(std::int32_t objectId) const
        noexcept;

private:
    [[nodiscard]] LevelEnemyState* findMutable(std::int32_t objectId) noexcept;

    std::vector<LevelEnemyState> states_;
};

} // namespace usm::game
