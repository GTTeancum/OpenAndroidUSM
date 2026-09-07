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

enum class TransportSoundCue {
    In,
    Out,
};

enum class TransportState : std::int8_t {
    Inactive = -1,
    Closing = 0,
    Covered = 1,
    Opening = 2,
};

struct TransportFrame {
    TransportState state{TransportState::Inactive};
    float elapsedMilliseconds{};
    float scale{};

    [[nodiscard]] bool visible() const noexcept {
        return state != TransportState::Inactive;
    }
};

struct BossProgressState {
    std::int32_t bossObjectId{-1};
    std::int32_t startWayPointId{-1};
    std::int32_t endWayPointId{-1};
    assets::Vector3 startPosition;
    assets::Vector3 endPosition;
    float failureDistance{};
    float currentDistance{};
    bool visible{};
    bool closing{};
    bool failed{};
};

struct RoomMotionState {
    std::int32_t objectId{-1};
    std::int32_t roomId{-1};
    std::int32_t linkedWaypointId{-1};
    std::int32_t targetWaypointId{-1};
    assets::Vector3 position;
    assets::Vector3 pathOriginOffset;
    assets::Vector3 previousReferencePosition;
    assets::Vector3 velocity;
    float lineSpeedCentimetersPerMillisecond{};
    bool active{};
    bool movingRoom{};
};

// Portable command-state reconstruction for the global commands issued by
// CCinematicThread. Object-specific animation and AI commands remain owned by
// their corresponding gameplay runtimes.
class LevelCinematicRuntime final {
public:
    void bind(LevelTriggerRuntime& triggers, GameplayCamera& camera,
              std::span<const LevelWayPointAsset> waypoints = {},
              std::span<const LevelRoomAsset> rooms = {}) noexcept;
    [[nodiscard]] Result applyCommand(const CinematicThread& thread,
                                      const CinematicCommand& command);
    [[nodiscard]] Result applyCommand(const CinematicCommand& command) {
        CinematicThread basicThread;
        basicThread.type = 1;
        return applyCommand(basicThread, command);
    }

    // CProgressBar::Update (0x0031a858) measures the flattened player-to-boss
    // separation every level tick. RhinoStop starts shrinking the allowed
    // separation by 0.9 centimetres per millisecond until the chase fails.
    void advanceBossProgress(std::uint32_t elapsedMilliseconds,
                             const assets::Vector3& playerPosition,
                             const assets::Vector3* bossPosition) noexcept;
    // CRoom::Update (0x0036e068) calls CRoom::Move (0x0036d8c0) once per
    // scaled game update. Room geometry commands only toggle this movement;
    // they do not change render visibility.
    void advanceRoomMotion(std::uint32_t elapsedMilliseconds) noexcept;
    // CTransport::Update (0x0038b4f0) drives the authored spider-logo wipe
    // on the level clock. Closing lasts 900 ms, the fully covered state lasts
    // until 1000 ms, and opening ends after 1900 ms total.
    void advanceTransport(std::uint32_t elapsedMilliseconds) noexcept;
    // AnimCamera completion forwards the two flags authored on
    // PlayDAECamera to CLevel::End/GameEnd.
    void completeColladaPlayback(bool levelEnd, bool gameEnd) noexcept;
    // CCinematicThread::OnOffDaeMovieUI (0x0037043c). Unlike the five-flag
    // InterfaceControl command this leaves the objective-arrow flag intact.
    void setColladaMovieUi(bool active) noexcept;
    // CQTEManager::EndQTE (0x0038a5b0) restores player control through
    // CLevel::EnableControls(true, false). The other InterfaceControl flags
    // remain owned by their authored cinematic commands.
    void endQuickTimeEvent() noexcept;

    // Application::UpdateSlowMotion (0x003e05f0) scales the complete game
    // update, while leaving audio and rendering on real time.
    [[nodiscard]] float updateSlowMotion(
        float realDeltaMilliseconds) noexcept;
    // Application::SetSlowMotion (0x003e0690). Player::DoUltimate calls this
    // directly rather than going through a cinematic command, with force=true
    // and the generic enter/exit SFX disabled.
    void setSlowMotion(float denominator, float holdMilliseconds,
                       float rampMilliseconds,
                       bool soundEnabled) noexcept;
    void resetSlowMotion() noexcept;
    [[nodiscard]] float slowMotionDenominator() const noexcept {
        return slowMotionDenominator_;
    }
    [[nodiscard]] float slowMotionHoldMilliseconds() const noexcept {
        return slowMotionHoldMilliseconds_;
    }
    // CGameCamera::UpdateShake (0x002f20cc) advances on the original 50 ms
    // game tick and applies its decaying alternating offset to position only.
    void advanceCameraShake(std::uint32_t deltaMilliseconds) noexcept;
    void startCameraShake(float maximumOffset, std::int32_t frameCount,
                          const assets::Vector3& rates) noexcept;
    void stopCameraShake() noexcept;
    [[nodiscard]] CameraPose applyCameraShake(CameraPose pose) const noexcept;
    void resetTransientForCheckPointLoad() noexcept;

    [[nodiscard]] std::vector<std::int32_t> consumeCinematicStartRequests();
    [[nodiscard]] std::vector<SlowMotionSoundCue>
    consumeSlowMotionSoundCues();
    [[nodiscard]] std::vector<TransportSoundCue>
    consumeTransportSoundCues();
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
    [[nodiscard]] const TransportFrame& transport() const noexcept {
        return transport_;
    }
    [[nodiscard]] bool controlsEnabled() const noexcept {
        return controlsEnabled_;
    }
    [[nodiscard]] bool attributionEnabled() const noexcept {
        return attributionEnabled_;
    }
    [[nodiscard]] bool objectiveArrowEnabled() const noexcept {
        return objectiveArrowEnabled_;
    }
    [[nodiscard]] bool blackOverlayEnabled() const noexcept {
        return blackOverlayEnabled_;
    }
    [[nodiscard]] bool skipEnabled() const noexcept { return skipEnabled_; }
    [[nodiscard]] bool listenerOnMainCharacter() const noexcept {
        return listenerOnMainCharacter_;
    }
    [[nodiscard]] const BossProgressState& bossProgress() const noexcept {
        return bossProgress_;
    }
    [[nodiscard]] bool bossProgressVisible() const noexcept {
        return bossProgress_.visible && !bossProgress_.failed;
    }
    [[nodiscard]] float bossProgressRatio() const noexcept;
    [[nodiscard]] std::span<const RoomMotionState> roomMotionStates() const
        noexcept {
        return roomMotionStates_;
    }
    [[nodiscard]] const RoomMotionState* findRoomMotion(
        std::int32_t objectId) const noexcept;
    [[nodiscard]] const std::array<bool, 16>& forcedVisibleRooms() const
        noexcept {
        return forcedVisibleRooms_;
    }
    void clearCinematicRoomOverride() noexcept {
        forcedVisibleRooms_.fill(false);
    }

private:
    [[nodiscard]] const LevelWayPointAsset* findWayPoint(
        std::int32_t objectId) const noexcept;
    void initializeRoomPath(RoomMotionState& room,
                            bool revertPosition) noexcept;
    [[nodiscard]] RoomMotionState* findRoomMotionMutable(
        std::int32_t objectId) noexcept;

    LevelTriggerRuntime* triggers_{};
    GameplayCamera* camera_{};
    std::span<const LevelWayPointAsset> waypoints_;
    std::vector<RoomMotionState> roomMotionStates_;
    std::vector<std::int32_t> cinematicStartRequests_;
    std::vector<SlowMotionSoundCue> slowMotionSoundCues_;
    std::vector<TransportSoundCue> transportSoundCues_;
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
    TransportFrame transport_;
    bool controlsEnabled_{true};
    bool attributionEnabled_{true};
    bool objectiveArrowEnabled_{true};
    bool blackOverlayEnabled_{};
    bool skipEnabled_{};
    bool listenerOnMainCharacter_{true};
    BossProgressState bossProgress_;
};

} // namespace usm::game
