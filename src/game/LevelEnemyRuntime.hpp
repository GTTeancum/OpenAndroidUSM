#pragma once

#include "core/Result.hpp"
#include "game/CinematicScript.hpp"
#include "game/LevelOneBootstrap.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace usm::game {

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
};

// Mutable native state for the authored enemy objects. Cinematic command names
// remain intact so recovered scripts can manipulate state without binary hooks.
class LevelEnemyRuntime final {
public:
    [[nodiscard]] Result initialize(const LevelOneBootstrap& level);
    void update(std::uint32_t elapsedMilliseconds) noexcept;
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
