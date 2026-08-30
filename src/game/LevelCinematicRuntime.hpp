#pragma once

#include "core/Result.hpp"
#include "game/CinematicScript.hpp"
#include "game/GameplayCamera.hpp"
#include "game/LevelTriggerRuntime.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace usm::game {

enum class SlowMotionSoundCue {
    Enter,
    Exit,
};

// Portable command-state reconstruction for the global commands issued by
// CCinematicThread. Object-specific animation and AI commands remain owned by
// their corresponding gameplay runtimes.
class LevelCinematicRuntime final {
public:
    void bind(LevelTriggerRuntime& triggers, GameplayCamera& camera,
              std::span<const LevelWayPointAsset> waypoints = {}) noexcept;
    [[nodiscard]] Result applyCommand(const CinematicCommand& command);

    // Application::UpdateSlowMotion (0x003e05f0) scales the complete game
    // update, while leaving audio and rendering on real time.
    [[nodiscard]] float updateSlowMotion(
        float realDeltaMilliseconds) noexcept;
    // CGameCamera::UpdateShake (0x002f20cc) advances on the original 50 ms
    // game tick and applies its decaying alternating offset to position only.
    void advanceCameraShake(std::uint32_t deltaMilliseconds) noexcept;
    [[nodiscard]] CameraPose applyCameraShake(CameraPose pose) const noexcept;

    [[nodiscard]] std::vector<std::int32_t> consumeCinematicStartRequests();
    [[nodiscard]] std::vector<SlowMotionSoundCue>
    consumeSlowMotionSoundCues();
    [[nodiscard]] bool levelEnded() const noexcept { return levelEnded_; }
    [[nodiscard]] bool goToNextLevel() const noexcept { return goToNextLevel_; }
    [[nodiscard]] bool gameEnded() const noexcept { return gameEnded_; }
    [[nodiscard]] std::int32_t lastCheckpointId() const noexcept {
        return lastCheckpointId_;
    }
    [[nodiscard]] bool bossRushTimerRunning() const noexcept {
        return bossRushTimerRunning_;
    }
    [[nodiscard]] bool skillUnlocked(std::size_t skillIndex) const noexcept {
        return skillIndex < unlockedSkills_.size() &&
               unlockedSkills_[skillIndex];
    }
    [[nodiscard]] bool transportRequested() const noexcept {
        return transportRequested_;
    }
    [[nodiscard]] bool controlsEnabled() const noexcept {
        return controlsEnabled_;
    }
    [[nodiscard]] bool blackOverlayEnabled() const noexcept {
        return blackOverlayEnabled_;
    }
    [[nodiscard]] const std::array<bool, 16>& forcedVisibleRooms() const
        noexcept {
        return forcedVisibleRooms_;
    }

private:
    LevelTriggerRuntime* triggers_{};
    GameplayCamera* camera_{};
    std::span<const LevelWayPointAsset> waypoints_;
    std::vector<std::int32_t> cinematicStartRequests_;
    std::vector<SlowMotionSoundCue> slowMotionSoundCues_;
    float slowMotionDenominator_{1.0F};
    float slowMotionElapsedMilliseconds_{};
    float slowMotionHoldMilliseconds_{};
    float slowMotionRampMilliseconds_{};
    bool slowMotionSoundEnabled_{};
    float cameraShakeMaximumOffset_{};
    std::int32_t cameraShakeFramesRemaining_{};
    std::int32_t cameraShakeTotalFrames_{};
    float cameraShakeXRate_{};
    float cameraShakeYRate_{};
    float cameraShakeZRate_{};
    std::int32_t cameraShakeSign_{1};
    std::uint32_t cameraShakeTickRemainderMilliseconds_{};
    assets::Vector3 cameraShakeOffset_{};
    // CGameCamera stores the cinematic room override in a 16-bit bitset.
    std::array<bool, 16> forcedVisibleRooms_{};
    bool levelEnded_{};
    bool goToNextLevel_{};
    bool gameEnded_{};
    std::int32_t lastCheckpointId_{-1};
    std::array<bool, 2> unlockedSkills_{};
    bool bossRushTimerRunning_{};
    bool transportRequested_{};
    bool controlsEnabled_{true};
    bool blackOverlayEnabled_{};
};

} // namespace usm::game
