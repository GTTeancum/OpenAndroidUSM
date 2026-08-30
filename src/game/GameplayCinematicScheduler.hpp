#pragma once

#include "core/Result.hpp"
#include "game/CinematicPlayer.hpp"
#include "game/LevelOneBootstrap.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace usm::game {

struct GameplayCinematicPlayback {
    const LevelCinematicAsset* asset{};
    std::uint32_t elapsedMilliseconds{};
    std::uint32_t durationMilliseconds{};
    bool completed{};
};

// Portable counterpart of CCinematicManager. Unlike a cutscene player, the
// original manager runs several command-only cinematics at once (encounter
// condition monitors are authored this way) while at most one cinematic owns
// camera/actor presentation.
class GameplayCinematicScheduler final {
public:
    void bind(std::span<const LevelCinematicAsset> cinematics) noexcept;
    [[nodiscard]] Result start(std::int32_t cinematicId);
    [[nodiscard]] Result update(
        std::uint32_t deltaMilliseconds,
        const ConditionalCinematicCommandHandler& handler);

    [[nodiscard]] std::vector<const LevelCinematicAsset*>
    consumeCompletions();
    void remove(std::int32_t cinematicId) noexcept;

    [[nodiscard]] const GameplayCinematicPlayback* presentation() const
        noexcept;
    [[nodiscard]] bool hasActiveColladaPlayback() const noexcept;
    [[nodiscard]] bool active(std::int32_t cinematicId) const noexcept;
    [[nodiscard]] std::vector<std::int32_t> activeIds() const;

private:
    struct Instance {
        GameplayCinematicPlayback playback;
        CinematicPlayer player;
    };

    std::span<const LevelCinematicAsset> cinematics_;
    std::vector<Instance> instances_;
    std::vector<const LevelCinematicAsset*> completions_;
};

} // namespace usm::game
