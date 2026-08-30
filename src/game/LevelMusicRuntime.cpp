#include "game/LevelMusicRuntime.hpp"

#include <algorithm>

namespace usm::game {
namespace {

bool participatesInCombat(const LevelEnemyState& enemy) noexcept {
    return enemy.asset != nullptr && enemy.health > 0.0F && enemy.visible &&
           enemy.aiEnabled &&
           (enemy.playerDetected ||
            (enemy.behavior != EnemyBehaviorState::Disabled &&
             enemy.behavior != EnemyBehaviorState::Idle));
}

} // namespace

void LevelMusicRuntime::reset() noexcept {
    currentTrack_ = LevelMusicTrack::DowntownCalm;
}

LevelMusicTransition LevelMusicRuntime::update(
    std::span<const LevelEnemyState> enemies, bool playerDead) noexcept {
    LevelMusicTrack nextTrack = LevelMusicTrack::DowntownCalm;
    if (playerDead) {
        nextTrack = LevelMusicTrack::Lose;
    } else {
        const bool activeSandman = std::any_of(
            enemies.begin(), enemies.end(), [](const LevelEnemyState& enemy) {
                return participatesInCombat(enemy) &&
                       enemy.asset->enemyTypeId == 16;
            });
        const bool activeEnemy = std::any_of(
            enemies.begin(), enemies.end(), participatesInCombat);
        if (activeSandman) {
            nextTrack = LevelMusicTrack::BossSandman;
        } else if (activeEnemy) {
            nextTrack = LevelMusicTrack::DowntownMixed;
        }
    }
    const LevelMusicTransition transition{currentTrack_, nextTrack, 500};
    currentTrack_ = nextTrack;
    return transition;
}

std::string_view LevelMusicRuntime::eventName(LevelMusicTrack track) noexcept {
    switch (track) {
    case LevelMusicTrack::DowntownCalm:
        return "M_DOWNTOWN_CALM";
    case LevelMusicTrack::DowntownMixed:
        return "M_DOWNTOWN_MIXED";
    case LevelMusicTrack::BossSandman:
        return "M_BOSS_SANDMAN";
    case LevelMusicTrack::Lose:
        return "M_LOSE";
    }
    return {};
}

bool LevelMusicRuntime::loops(LevelMusicTrack track) noexcept {
    return track != LevelMusicTrack::Lose;
}

} // namespace usm::game
