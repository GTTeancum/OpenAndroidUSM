#pragma once

#include "game/LevelEnemyRuntime.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace usm::game {

enum class LevelMusicTrack {
    DowntownCalm,
    DowntownMixed,
    BossSandman,
    Lose,
};

struct LevelMusicTransition {
    LevelMusicTrack from{LevelMusicTrack::DowntownCalm};
    LevelMusicTrack to{LevelMusicTrack::DowntownCalm};
    std::uint32_t fadeMilliseconds{500};
};

// Renderer- and backend-independent adaptive music state reconstructed from
// CAIEntityManager::Update (0x00376444). LevelSound's first entries are Vox
// IDs 4/5, and active boss type 16 selects Vox ID 22.
class LevelMusicRuntime final {
public:
    void reset() noexcept;
    [[nodiscard]] LevelMusicTransition update(
        std::span<const LevelEnemyState> enemies,
        bool playerDead) noexcept;
    [[nodiscard]] LevelMusicTrack currentTrack() const noexcept {
        return currentTrack_;
    }
    [[nodiscard]] static std::string_view eventName(
        LevelMusicTrack track) noexcept;
    [[nodiscard]] static bool loops(LevelMusicTrack track) noexcept;

private:
    LevelMusicTrack currentTrack_{LevelMusicTrack::DowntownCalm};
};

} // namespace usm::game
