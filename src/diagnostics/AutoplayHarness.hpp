#pragma once

#include "assets/ColladaMesh.hpp"
#include "core/Result.hpp"
#include "game/CinematicCamera.hpp"
#include "game/CinematicScript.hpp"
#include "game/GameplayPlayer.hpp"
#include "game/LevelEnemyRuntime.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace usm::diagnostics {

struct AutoplaySnapshot {
    std::uint64_t frameIndex{};
    std::uint64_t realTimeMilliseconds{};
    std::uint64_t gameTimeMilliseconds{};
    bool gameplayActive{};
    bool controlsEnabled{};
    bool quickTimeEventActive{};
    bool restoreActive{};
    float restoreAlpha{};
    std::int32_t cameraAreaId{-1};
    std::int32_t activeCinematicId{-1};
    assets::Vector3 playerPosition;
    assets::Vector3 playerFacing{1.0F, 0.0F, 0.0F};
    float playerHealth{};
    std::string_view playerAnimation;
    std::uint32_t playerAnimationTimeMilliseconds{};
    std::uint16_t playerStateId{};
    std::string_view playerStateName;
    bool playerPunchTransitionReady{};
    game::CameraPose camera;
    std::span<const bool> visibleRooms;
    std::span<const game::LevelEnemyState> enemies;
};

struct AutoplayTeleport {
    assets::Vector3 position;
    assets::Vector3 facing{1.0F, 0.0F, 0.0F};
};

struct AutoplayFrameInput {
    game::PlayerMotionInput motion;
    bool jumpPressed{};
    bool webPressed{};
    bool webReleased{};
    bool punchPressed{};
    bool quickTimeEventPressed{};
    std::optional<AutoplayTeleport> teleport;
    std::vector<std::string> captureLabels;
};

// Deterministic driver and trace sink for the real level-one application
// loop. Scripts express goals rather than raw frame inputs so the same route
// remains useful while movement timing is corrected during reconstruction.
class AutoplayHarness final {
public:
    [[nodiscard]] Result initialize(const std::filesystem::path& scriptPath,
                                    const std::filesystem::path& outputPath);
    [[nodiscard]] AutoplayFrameInput update(
        const AutoplaySnapshot& snapshot);
    void recordFrame(const AutoplaySnapshot& snapshot);
    void recordEvent(std::uint64_t timeMilliseconds, std::string_view type,
                     std::string_view detail);
    void recordCommand(std::uint64_t timeMilliseconds,
                       std::int32_t threadObjectId,
                       const game::CinematicCommand& command);
    void recordAudio(std::uint64_t timeMilliseconds, std::string_view action,
                     std::string_view eventName, bool loop = false,
                     bool spatial = false);
    [[nodiscard]] bool periodicCaptureDue(
        std::uint64_t timeMilliseconds) noexcept;
    [[nodiscard]] Result writeCapture(const assets::RgbaImage& image,
                                      std::string_view label,
                                      std::uint64_t timeMilliseconds);
    void finish(bool applicationSucceeded, std::string_view detail = {});

    [[nodiscard]] bool complete() const noexcept { return complete_; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] std::string_view failureMessage() const noexcept {
        return failureMessage_;
    }
    [[nodiscard]] std::uint32_t fixedStepMilliseconds() const noexcept {
        return fixedStepMilliseconds_;
    }
    [[nodiscard]] std::uint64_t maximumTimeMilliseconds() const noexcept {
        return maximumTimeMilliseconds_;
    }
    [[nodiscard]] std::uint64_t startTimeMilliseconds() const noexcept {
        return startTimeMilliseconds_;
    }
    [[nodiscard]] std::uint32_t renderWidth() const noexcept {
        return renderWidth_;
    }
    [[nodiscard]] std::uint32_t renderHeight() const noexcept {
        return renderHeight_;
    }
    [[nodiscard]] const std::filesystem::path& outputPath() const noexcept {
        return outputPath_;
    }

private:
    enum class StepKind {
        WaitGameplay,
        Wait,
        MoveTo,
        Attack,
        Jump,
        WebOn,
        WebOff,
        Teleport,
        Capture,
        AssertNear,
        AssertHealthAbove,
        Finish,
    };

    struct Step {
        StepKind kind{};
        std::size_t sourceLine{};
        std::uint64_t durationOrTimeoutMilliseconds{};
        assets::Vector3 position;
        assets::Vector3 facing{1.0F, 0.0F, 0.0F};
        float radius{};
        float value{};
        std::vector<std::int32_t> objectIds;
        std::string label;
    };

    struct EnemyTraceState {
        float health{};
        bool visible{};
        bool aiEnabled{};
        bool playerDetected{};
        game::EnemyBehaviorState behavior{game::EnemyBehaviorState::Disabled};
        std::string animation;
    };

    [[nodiscard]] Result parseScript(const std::filesystem::path& scriptPath);
    [[nodiscard]] AutoplayFrameInput updateActiveStep(
        const AutoplaySnapshot& snapshot, const Step& step);
    void beginStep(const AutoplaySnapshot& snapshot, const Step& step);
    void completeStep(const AutoplaySnapshot& snapshot, const Step& step);
    void failStep(const AutoplaySnapshot& snapshot, const Step& step,
                  std::string message);
    [[nodiscard]] game::PlayerMotionInput steerToward(
        const AutoplaySnapshot& snapshot,
        const assets::Vector3& target) const noexcept;
    [[nodiscard]] static std::string stepName(StepKind kind);
    [[nodiscard]] static std::string behaviorName(
        game::EnemyBehaviorState behavior);
    [[nodiscard]] static std::string csv(std::string_view value);

    std::filesystem::path outputPath_;
    std::ofstream frameLog_;
    std::ofstream enemyLog_;
    std::ofstream eventLog_;
    std::vector<Step> steps_;
    std::size_t activeStepIndex_{};
    std::optional<std::uint64_t> activeStepStartMilliseconds_;
    std::uint64_t nextSampleMilliseconds_{};
    std::uint64_t nextCaptureMilliseconds_{};
    std::uint64_t lastTimeMilliseconds_{};
    std::uint64_t lastFrameIndex_{};
    std::uint32_t fixedStepMilliseconds_{50};
    std::uint32_t sampleIntervalMilliseconds_{50};
    std::uint32_t captureIntervalMilliseconds_{1000};
    std::uint64_t maximumTimeMilliseconds_{120000};
    std::uint64_t startTimeMilliseconds_{};
    std::uint32_t renderWidth_{1280};
    std::uint32_t renderHeight_{720};
    bool complete_{};
    bool failed_{};
    bool finishedLog_{};
    std::string failureMessage_;
    bool previousGameplayActive_{};
    float previousPlayerHealth_{};
    std::string previousPlayerAnimation_;
    std::int32_t previousCameraAreaId_{-1};
    std::int32_t previousCinematicId_{-1};
    std::string previousVisibleRooms_;
    game::PlayerMotionInput lastMotionInput_;
    std::map<std::int32_t, EnemyTraceState> previousEnemies_;
};

} // namespace usm::diagnostics
