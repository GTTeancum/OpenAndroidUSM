#pragma once

#include "core/Result.hpp"
#include "game/CinematicScript.hpp"
#include "game/GameplayCamera.hpp"
#include "game/LevelTriggerRuntime.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace usm::game {

// Portable command-state reconstruction for the global commands issued by
// CCinematicThread. Object-specific animation and AI commands remain owned by
// their corresponding gameplay runtimes.
class LevelCinematicRuntime final {
public:
    void bind(LevelTriggerRuntime& triggers, GameplayCamera& camera,
              std::span<const LevelWayPointAsset> waypoints = {}) noexcept;
    [[nodiscard]] Result applyCommand(const CinematicCommand& command);

    [[nodiscard]] std::vector<std::int32_t> consumeCinematicStartRequests();
    [[nodiscard]] bool levelEnded() const noexcept { return levelEnded_; }
    [[nodiscard]] bool goToNextLevel() const noexcept { return goToNextLevel_; }
    [[nodiscard]] bool gameEnded() const noexcept { return gameEnded_; }

private:
    LevelTriggerRuntime* triggers_{};
    GameplayCamera* camera_{};
    std::span<const LevelWayPointAsset> waypoints_;
    std::vector<std::int32_t> cinematicStartRequests_;
    bool levelEnded_{};
    bool goToNextLevel_{};
    bool gameEnded_{};
};

} // namespace usm::game
