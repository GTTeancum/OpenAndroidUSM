#pragma once

#include "assets/ColladaMesh.hpp"
#include "core/Result.hpp"
#include "game/CinematicCamera.hpp"
#include "game/CinematicScript.hpp"
#include "game/GameplayPlayer.hpp"
#include "game/LevelBonusRuntime.hpp"
#include "game/LevelCinematicRuntime.hpp"
#include "game/LevelDropRuntime.hpp"
#include "game/LevelEnemyRuntime.hpp"
#include "game/LevelHostageRuntime.hpp"
#include "game/LevelObjectRuntime.hpp"
#include "game/LevelOneBootstrap.hpp"
#include "game/PlayerStateConfig.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace usm::diagnostics {

struct AutoplaySnapshot {
    std::uint64_t frameIndex{};
    std::uint64_t realTimeMilliseconds{};
    std::uint64_t gameTimeMilliseconds{};
    float slowMotionDenominator{1.0F};
    bool gameplayActive{};
    bool controlsEnabled{};
    bool attributionEnabled{true};
    bool quickTimeEventActive{};
    bool tutorialVisible{};
    bool deathScreenActive{};
    float deathScreenAlpha{};
    bool deathConfirmationActive{};
    std::int32_t deathConfirmationSelection{};
    bool exitMenuActive{};
    std::uint32_t exitMenuState{};
    bool mainMenuRequested{};
    bool restoreActive{};
    float restoreAlpha{};
    std::int32_t cameraAreaId{-1};
    std::int32_t lastCheckPointId{-1};
    std::int32_t activeCinematicId{-1};
    std::vector<std::int32_t> activeCinematicIds;
    assets::Vector3 playerPosition;
    assets::Vector3 playerRenderPosition;
    assets::Vector3 playerAttackRootTranslation;
    assets::Vector3 playerFacing{1.0F, 0.0F, 0.0F};
    float playerHealth{};
    float playerWebPower{};
    std::int32_t playerSkillPoints{};
    std::int32_t playerComboScore{};
    std::string_view playerAnimation;
    std::uint32_t playerAnimationTimeMilliseconds{};
    std::uint16_t playerStateId{};
    std::string_view playerStateName;
    bool playerPunchTransitionReady{};
    bool playerPunchAttackTransitionReady{};
    bool playerJumpAttackTransitionReady{};
    bool playerJumpReleaseAttackTransitionReady{};
    bool playerWebAttackTransitionReady{};
    bool playerWebHeldAttackTransitionReady{};
    std::span<const game::PlayerHitEffectState> playerHitEffects;
    bool playerOnWall{};
    bool playerCinematicMotionActive{};
    std::uint32_t playerCinematicMotionElapsedMilliseconds{};
    std::uint32_t playerCinematicMotionDurationMilliseconds{};
    game::CameraPose camera;
    std::span<const bool> visibleRooms;
    std::span<const game::RoomMotionState> roomMotions;
    std::span<const game::LevelEnemyState> enemies;
    std::int32_t meleeEngagerObjectId{-1};
    float meleeEngagementCooldownMilliseconds{};
    std::int32_t nativeRandomState{};
    std::span<const game::EnemyMolotovState> molotovs;
    std::span<const game::EnemyBoomerangState> boomerangs;
    std::span<const game::EnemyThunderclapState> thunderclaps;
    std::span<const game::EnemyElectricPostState> electricPosts;
    std::span<const game::EnemyElectroBurstState> electroBursts;
    std::span<const game::LevelObjectState> objects;
    std::span<const game::LevelBonusState> bonuses;
    std::span<const game::LevelHostageState> hostages;
    std::span<const game::LevelDropObjectState> drops;
    bool hostageQuickTimeEventActive{};
    bool rhinoQuickTimeActionActive{};
    std::int16_t rhinoQuickTimeActionState{-1};
    std::uint32_t rhinoQuickTimeStateElapsedMilliseconds{};
    std::uint32_t rhinoQuickTimeButtonElapsedMilliseconds{};
    std::uint32_t rhinoQuickTimeDurationMilliseconds{};
    std::int16_t rhinoQuickTimeCompletedActions{};
    std::int16_t rhinoQuickTimeRequiredActions{};
    float rhinoQuickTimeProgress{};
    bool bossProgressVisible{};
    bool bossProgressClosing{};
    bool bossProgressFailed{};
    std::int32_t bossProgressBossObjectId{-1};
    float bossProgressDistance{};
    float bossProgressFailureDistance{};
    float bossProgressRatio{};
    game::TransportState transportState{game::TransportState::Inactive};
    float transportElapsedMilliseconds{};
    float transportScale{};
    bool levelEnded{};
    bool gameEnded{};
    bool cinematicLetterboxVisible{};
    bool playerWebLineActive{};
    std::size_t playerWebLineCount{};
    std::int32_t playerWebLineTargetObjectId{-1};
    game::WallWebPhase wallWebPhase{game::WallWebPhase::Inactive};
    std::int32_t wallWebTargetObjectId{-1};
    int wallWebAngle{};
    std::int16_t wallWebCompletedActions{};
    bool wallWebLineActive{};
    std::int32_t playerWebGrabPointObjectId{-1};
    bool spiderSenseCueVisible{};
};

struct AutoplayTeleport {
    assets::Vector3 position;
    assets::Vector3 facing{1.0F, 0.0F, 0.0F};
};

struct AutoplayEnemyAiOverride {
    std::int32_t objectId{-1};
    bool enabled{};
    bool forcePlayerDetected{true};
};

struct AutoplayEnemyPhysicsOverride {
    std::int32_t objectId{-1};
    bool enabled{};
};

struct AutoplayEnemyDamage {
    std::int32_t objectId{-1};
    float damage{};
};

struct AutoplayFrameInput {
    game::PlayerMotionInput motion;
    bool jumpPressed{};
    bool jumpHeld{};
    bool jumpReleased{};
    bool webPressed{};
    bool webHeld{};
    bool webReleased{};
    bool punchPressed{};
    bool spiderSensePressed{};
    bool superAttackPressed{};
    bool quickTimeEventPressed{};
    bool menuUpReleased{};
    bool menuDownReleased{};
    bool menuSelectedReleased{};
    std::optional<AutoplayTeleport> teleport;
    std::vector<AutoplayEnemyAiOverride> enemyAiOverrides;
    std::vector<AutoplayEnemyPhysicsOverride> enemyPhysicsOverrides;
    std::vector<AutoplayEnemyDamage> enemyDamage;
    std::vector<std::int32_t> skillUnlockRequests;
    std::vector<std::int32_t> cinematicStartRequests;
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
    void bindTriggers(std::span<const game::LevelTriggerAsset> triggers) noexcept {
        triggers_ = triggers;
    }
    void notifyCinematicStarted(std::int32_t cinematicId);
    void recordCommand(std::uint64_t timeMilliseconds,
                       std::int32_t cinematicId,
                       std::int32_t threadObjectId,
                       const game::CinematicCommand& command);
    void recordCinematicAssets(
        std::span<const game::LevelCinematicAsset> cinematics);
    void recordCollisionAssets(
        std::span<const game::LevelRoomAsset> rooms);
    void recordMaterialAssets(const game::LevelOneBootstrap& level);
    void recordPlayerStateAssets(
        const game::PlayerStateConfigDatabase& states);
    void recordAudio(std::uint64_t timeMilliseconds, std::string_view action,
                     std::string_view eventName, bool loop = false,
                     bool spatial = false, float volume = 1.0F);
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
        WaitIntro,
        WaitControls,
        Wait,
        MoveTo,
        MoveTo3D,
        ClimbTo,
        MoveInput,
        MoveUntilWall,
        MoveUntilState,
        MoveToUntilState,
        MoveUntilCinematic,
        MoveInputUntilCinematic,
        WaitCinematicStarted,
        WaitTransportState,
        WaitDeathScreen,
        WaitDeathConfirmation,
        WaitExitMenu,
        WaitMainMenuRequested,
        CrossTrigger,
        WaitEnemiesGrounded,
        WaitEnemiesActive,
        WaitWebGrabPoint,
        WaitEnemyMeleeAttack,
        WaitEnemyMeleeInactive,
        WaitEnemyProjectile,
        SetEnemyAi,
        SetEnemyPhysics,
        DamageEnemy,
        WaitEnemyAnimation,
        WaitObjectElectricState,
        WaitAreaDamageState,
        WaitObjectNear,
        WaitLevelEnd,
        Attack,
        AttackObject,
        CollectBonus,
        RescueHostage,
        WaitDropHit,
        CollectComic,
        Jump,
        Punch,
        PressButtons,
        PunchWhenReady,
        PunchAttackWhenReady,
        JumpAttackWhenReady,
        JumpReleaseAttackWhenReady,
        WebAttackWhenReady,
        WebHeldAttackWhenReady,
        SpiderSense,
        SuperAttack,
        WebOn,
        WebOff,
        SetAutoQuickTime,
        QuickTimeTap,
        MenuUp,
        MenuDown,
        MenuSelect,
        Teleport,
        UnlockSkill,
        StartCinematic,
        Capture,
        AssertNear,
        AssertCameraArea,
        AssertLastCheckPoint,
        AssertEnemyNear,
        AssertEnemyDistanceAbove,
        AssertEnemyHealthBelow,
        AssertEnemyHealthNear,
        AssertEnemyMeleeAttackActive,
        AssertEnemyBehavior,
        AssertObjectDestroyed,
        AssertObjectHidden,
        AssertObjectAnimation,
        AssertObjectElectricState,
        AssertObjectNear,
        AssertComicCollected,
        AssertBonusCollected,
        AssertHostageFreed,
        AssertSkillPointsAtLeast,
        AssertComboScoreAtLeast,
        AssertPlayerState,
        AssertPlayerEffect,
        AssertSpiderSenseCue,
        AssertPlayerWebLine,
        AssertSlowMotion,
        AssertHealthAbove,
        AssertWebPowerNear,
        AssertGameplayUi,
        AssertHealthBelow,
        AssertCinematicNotStarted,
        AssertDeathScreenActive,
        AssertDeathScreenInactive,
        AssertDeathAlphaAbove,
        AssertDeathConfirmationActive,
        AssertDeathConfirmationInactive,
        AssertDeathConfirmationSelection,
        AssertAudioPlayed,
        AssertAudioNotPlayed,
        AssertAudioStopped,
        AssertAudioPlayCount,
        AssertAudioStopCount,
        AssertEventNotObserved,
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
        bool physicsActive{};
        bool playerDetected{};
        game::EnemyBehaviorState behavior{game::EnemyBehaviorState::Disabled};
        std::string animation;
        bool grounded{};
        bool cinematicMotionActive{};
        bool cinematicActionActive{};
        std::int32_t cinematicActionObjectId{-1};
        game::RhinoBossTaskState rhinoTask{
            game::RhinoBossTaskState::None};
        std::uint32_t rhinoPhase{};
        std::uint32_t rhinoSequenceCycle{};
        game::RobotPhantomTaskState robotPhantomTask{
            game::RobotPhantomTaskState::None};
        std::uint32_t robotPhantomSequenceIndex{};
        game::ElectroBossTaskState electroTask{
            game::ElectroBossTaskState::None};
        std::uint32_t electroPhase{};
        std::uint32_t electroSequenceIndex{};
        std::uint32_t electroRangeAttacksRemaining{};
        std::uint32_t electroDashesRemaining{};
    };

    struct RoomTraceState {
        bool active{};
        std::int32_t targetWaypointId{-1};
    };

    struct ObjectTraceState {
        float health{};
        bool visible{};
        bool collisionEnabled{};
        bool comicCollected{};
        game::LevelObjectDestructionPhase destructionPhase{
            game::LevelObjectDestructionPhase::Intact};
        std::string animation;
        game::ElectricPlatformState electricState{
            game::ElectricPlatformState::Off};
        std::int32_t areaDamageState{-1};
        game::PlatformMotionState platformMotionState{
            game::PlatformMotionState::Park};
        bool platformMotionActive{};
        bool trainActive{};
        bool trainCut{};
        std::int32_t trainTargetWaypointId{-1};
        bool cinematicMotionActive{};
    };

    struct BonusTraceState {
        bool visible{};
        bool orbActive{};
        float progress{};
    };

    struct DropTraceState {
        game::LevelDropPhase phase{game::LevelDropPhase::Dormant};
        bool visible{};
        bool physicsEnabled{};
        bool hitPlayer{};
    };

    struct HostageTraceState {
        game::HostageRescuePhase phase{game::HostageRescuePhase::Tied};
        std::int16_t completedActions{};
        bool promptVisible{};
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
    [[nodiscard]] static std::string rhinoTaskName(
        game::RhinoBossTaskState task);
    [[nodiscard]] static std::string robotPhantomTaskName(
        game::RobotPhantomTaskState task);
    [[nodiscard]] static std::string electroTaskName(
        game::ElectroBossTaskState task);
    [[nodiscard]] static std::string csv(std::string_view value);

    std::filesystem::path outputPath_;
    std::ofstream frameLog_;
    std::ofstream roomLog_;
    std::ofstream enemyLog_;
    std::ofstream projectileLog_;
    std::ofstream objectLog_;
    std::ofstream bonusLog_;
    std::ofstream hostageLog_;
    std::ofstream dropLog_;
    std::ofstream eventLog_;
    std::ofstream cinematicAssetLog_;
    std::ofstream sceneNodeAssetLog_;
    std::ofstream objectAssetLog_;
    std::ofstream triggerAssetLog_;
    std::ofstream waypointAssetLog_;
    std::ofstream webGrabPointAssetLog_;
    std::ofstream slideAssetLog_;
    std::ofstream checkPointAssetLog_;
    std::ofstream cameraAreaAssetLog_;
    std::ofstream geometryAssetLog_;
    std::ofstream materialAssetLog_;
    std::ofstream textureAssetLog_;
    std::ofstream skyTriangleAssetLog_;
    std::ofstream colladaNodeAssetLog_;
    std::ofstream collisionAssetLog_;
    std::ofstream collisionTriangleLog_;
    std::ofstream navigationTriangleLog_;
    std::ofstream playerStateAssetLog_;
    std::vector<Step> steps_;
    std::size_t activeStepIndex_{};
    std::optional<std::uint64_t> activeStepStartMilliseconds_;
    std::uint32_t activeStepPhase_{};
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
    bool autoQuickTimeActions_{true};
    bool complete_{};
    bool failed_{};
    bool finishedLog_{};
    std::string failureMessage_;
    bool previousGameplayActive_{};
    float previousSlowMotionDenominator_{1.0F};
    bool previousAttributionEnabled_{true};
    bool previousPlayerOnWall_{};
    bool previousPlayerCinematicMotionActive_{};
    bool previousDeathConfirmationActive_{};
    std::int32_t previousDeathConfirmationSelection_{};
    bool previousExitMenuActive_{};
    std::uint32_t previousExitMenuState_{};
    bool previousMainMenuRequested_{};
    bool activeAttackFarStateObserved_{};
    std::int32_t activeAttackTargetObjectId_{-1};
    assets::Vector3 activeAttackProgressPosition_{};
    std::uint64_t activeAttackProgressMilliseconds_{};
    std::uint32_t activeAttackObstacleRecoveryCount_{};
    float previousPlayerHealth_{};
    float previousPlayerWebPower_{};
    std::string previousPlayerAnimation_;
    std::int32_t previousCameraAreaId_{-1};
    std::int32_t previousLastCheckPointId_{-1};
    std::int32_t previousCinematicId_{-1};
    std::string previousVisibleRooms_;
    bool previousBossProgressVisible_{};
    bool previousBossProgressClosing_{};
    bool previousBossProgressFailed_{};
    game::TransportState previousTransportState_{
        game::TransportState::Inactive};
    bool previousLevelEnded_{};
    bool previousGameEnded_{};
    game::PlayerMotionInput lastMotionInput_;
    std::span<const game::LevelTriggerAsset> triggers_;
    std::vector<std::int32_t> observedCinematicStarts_;
    std::set<std::string> playedAudioEvents_;
    std::set<std::string> stoppedAudioEvents_;
    std::set<std::string> observedEventTypes_;
    std::map<std::string, std::size_t> audioPlayCounts_;
    std::map<std::string, std::size_t> audioStopCounts_;
    std::map<std::int32_t, EnemyTraceState> previousEnemies_;
    std::map<std::int32_t, RoomTraceState> previousRooms_;
    std::map<std::int32_t, ObjectTraceState> previousObjects_;
    std::map<std::int32_t, BonusTraceState> previousBonuses_;
    std::map<std::int32_t, HostageTraceState> previousHostages_;
    std::map<std::int32_t, DropTraceState> previousDrops_;
};

} // namespace usm::diagnostics
