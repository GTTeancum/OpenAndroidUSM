#pragma once

#include "core/Result.hpp"
#include "game/GameplayPlayer.hpp"
#include "game/LevelBonusRuntime.hpp"
#include "game/LevelObjectRuntime.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace usm::game {

enum class HostageRescuePhase : std::int32_t {
    Tied = 0,
    RescueStart = 1,
    QuickTime = 2,
    RescueEnd = 3,
    Release = 4,
    Thank = 5,
    Freed = 6,
};

enum class HostageSoundAction {
    PlayOnce,
    StartLoop,
    StopLoop,
};

struct HostageSoundCue {
    std::int32_t hostageObjectId{-1};
    std::uint16_t voxSoundId{};
    HostageSoundAction action{HostageSoundAction::PlayOnce};
};

struct LevelHostageState {
    const LevelObjectAsset* asset{};
    HostageRescuePhase phase{HostageRescuePhase::Tied};
    std::uint32_t phaseElapsedMilliseconds{};
    std::uint32_t quickTimeElapsedMilliseconds{};
    std::uint32_t quickTimeDurationMilliseconds{};
    std::int16_t completedActions{};
    std::int16_t requiredActions{1};
    bool promptVisible{};
};

// Portable reconstruction of CHostage::Update/OnEnterState/OnExitState at
// 0x00338068, 0x00337de4, and 0x00337d98. The seven native states own the
// player rescue clips, button-config 11 QTE, hostage animation sequence, and
// health/skill reward-orb creation.
class LevelHostageRuntime final {
public:
    [[nodiscard]] Result initialize(const LevelOneBootstrap& level,
                                    LevelObjectRuntime* objects = nullptr);
    [[nodiscard]] Result update(GameplayPlayer& player,
                                LevelObjectRuntime& objects,
                                LevelBonusRuntime& bonuses,
                                std::uint32_t elapsedMilliseconds,
                                bool rescuePressed);

    [[nodiscard]] bool canStartRescue(
        const GameplayPlayer& player) const noexcept;
    [[nodiscard]] bool ownsPlayerControl() const noexcept;
    [[nodiscard]] bool quickTimeActive() const noexcept;
    [[nodiscard]] float quickTimeProgress() const noexcept;
    [[nodiscard]] bool contextPromptVisible() const noexcept;
    [[nodiscard]] std::span<const LevelHostageState> states() const noexcept {
        return states_;
    }
    [[nodiscard]] const LevelHostageState* find(
        std::int32_t objectId) const noexcept;
    [[nodiscard]] std::vector<HostageSoundCue> consumeSoundCues();
    void clearTransientCues() noexcept { soundCues_.clear(); }

private:
    [[nodiscard]] Result setPhase(LevelHostageState& hostage,
                                  HostageRescuePhase phase,
                                  GameplayPlayer& player,
                                  LevelObjectRuntime& objects,
                                  LevelBonusRuntime& bonuses);
    [[nodiscard]] bool playerInsideEnableRadius(
        const LevelHostageState& hostage,
        const GameplayPlayer& player) const noexcept;

    const ButtonConfigDefinition* quickTimeConfig_{};
    std::vector<LevelHostageState> states_;
    std::vector<HostageSoundCue> soundCues_;
};

} // namespace usm::game
