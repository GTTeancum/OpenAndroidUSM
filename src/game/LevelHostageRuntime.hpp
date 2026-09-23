#pragma once

#include "core/Result.hpp"
#include "game/QuickTimeEventRuntime.hpp"
#include "game/QteClock.hpp"
#include "game/GameplayPlayer.hpp"
#include "game/HostageSound.hpp"
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

// CHostage::Update starts from m_pRescuePressed (R1 release);
// CQTEManager::Update counts m_pQTEPressed (Cross press). They must not
// be represented by a shared contextual punch input.
// The mash-idle timer reads Application::GetRealTs, not the scaled
// simulation delta. Keep the two clocks explicit at the call boundary.
struct HostageTimeStep {
    std::uint32_t simulationMilliseconds;
    std::uint32_t realMilliseconds;
    std::uint32_t timerMilliseconds;

    HostageTimeStep(std::uint32_t simulation, std::uint32_t real,
                    std::uint32_t timer) noexcept
        : simulationMilliseconds(simulation), realMilliseconds(real),
          timerMilliseconds(timer) {}

};

struct HostageInput {
    bool rescueRequested{};
    bool quickTimeActionPressed{};
};

enum class HostageQteOutcome { Inactive, Running, Success, Failure };

struct LevelHostageState {
    const LevelObjectAsset* asset{};
    HostageRescuePhase phase{HostageRescuePhase::Tied};
    std::uint32_t phaseElapsedMilliseconds{};
    std::uint32_t quickTimeElapsedMilliseconds{};
    std::uint32_t quickTimeDurationMilliseconds{};
    std::int16_t completedActions{};
    std::int16_t requiredActions{1};
    bool promptVisible{};
    HostageQteOutcome quickTimeOutcome{HostageQteOutcome::Inactive};
};

// Portable reconstruction of CHostage::Update/OnEnterState/OnExitState at
// ELF 0x00328068, 0x00327de4, and 0x00327d98 (+0x10000 in supplied Ghidra). The seven native states own the
// player rescue clips, button-config 11 QTE, hostage animation sequence, and
// health/skill reward-orb creation.
class LevelHostageRuntime final {
public:
    // sharedQte is the level-owned manager, already bound to this level's
    // original configs/atlas. It must outlive this runtime and its snapshots.
    [[nodiscard]] Result initialize(const LevelOneBootstrap& level,
                                    QuickTimeEventRuntime& sharedQte,
                                    LevelObjectRuntime* objects = nullptr);
    [[nodiscard]] Result update(GameplayPlayer& player,
                                LevelObjectRuntime& objects,
                                LevelBonusRuntime& bonuses,
                                HostageTimeStep time,
                                HostageInput input,
                                const HostageUpdateHooks& hooks = {});

    // CLevel::Update (ELF 0x003720bc) advances hostage objects before the
    // QTE manager. The Windows loop uses these two explicit phases. update()
    // is a composed object-then-manager step for portable fixtures.
    [[nodiscard]] Result updateObjects(GameplayPlayer& player,
                                       LevelObjectRuntime& objects,
                                       LevelBonusRuntime& bonuses,
                                       HostageTimeStep time,
                                       bool rescueRequested,
                                       const HostageUpdateHooks& hooks = {});
    // Composed test/headless entry: advances the SAME shared manager once.
    // Application updates that manager directly and calls observeQuickTime;
    // it must not also call updateQuickTime during that update.
    void updateQuickTime(QteTimeStep time, bool actionPressed) noexcept;
    void observeQuickTime() noexcept; // diagnostics only, no gameplay effects
    void setPause(bool paused, std::uint32_t timerMilliseconds) noexcept;
    // CQTEManager cues are global Play2D events, unlike CHostage's spatial
    // cutting/thanks voices. They are drained at the manager update stage.
    [[nodiscard]] std::vector<std::uint16_t> consumeQuickTimeSoundCues() noexcept;
    [[nodiscard]] bool consumeControlRelease() noexcept;

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
    void clearTransientCues() noexcept {
        soundCues_.clear();

    }

private:
    [[nodiscard]] Result setPhase(LevelHostageState& hostage,
                                  HostageRescuePhase phase,
                                  GameplayPlayer& player,
                                  LevelObjectRuntime& objects,
                                  LevelBonusRuntime& bonuses,
                                  const HostageUpdateHooks& hooks);
    [[nodiscard]] bool playerInsideEnableRadius(
        const LevelHostageState& hostage,
        const GameplayPlayer& player) const noexcept;

    [[nodiscard]] Result emitSound(const HostageSoundCue& cue,
                                    const HostageUpdateHooks& hooks);

    QuickTimeEventRuntime* sharedQte_{};
    const ButtonConfigDefinition* quickTimeConfig_{};
    std::vector<LevelHostageState> states_;
    std::vector<HostageSoundCue> soundCues_;
};

} // namespace usm::game
