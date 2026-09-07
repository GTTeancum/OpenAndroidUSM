#pragma once

#include "core/Result.hpp"
#include "game/CinematicScript.hpp"
#include "game/LevelOneBootstrap.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace usm::game {

enum class LevelObjectDestructionPhase {
    Intact,
    Breaking,
    Destroyed,
};

enum class ElectricPlatformState : std::int32_t {
    Release = 0,
    Off = 1,
    Warning = 2,
};

enum class PlatformMotionState : std::int32_t {
    Park = 0,
    Move = 1,
    Brake = 2,
};

struct ObjectCinematicMotionState {
    assets::Vector3 startPosition;
    assets::Vector3 endPosition;
    assets::Quaternion startRotation;
    assets::Quaternion endRotation;
    std::uint32_t elapsedMilliseconds{};
    std::uint32_t durationMilliseconds{};
    bool active{};
};

struct LevelObjectState {
    const LevelObjectAsset* asset{};
    assets::Vector3 position;
    std::array<float, 16> worldTransform{};
    std::string activeAnimation;
    std::uint32_t animationTimeMilliseconds{};
    float animationSpeed{1.0F};
    bool animationLoops{true};
    ObjectCinematicMotionState cinematicMotion;
    bool visible{true};
    bool physicsEnabled{};
    bool collisionEnabled{};
    float health{};
    bool destructionAlternateAnimation{};
    bool comicCollected{};
    ElectricPlatformState electricState{ElectricPlatformState::Off};
    float electricStateElapsedMilliseconds{};
    bool electricSwitchActive{};
    PlatformMotionState platformMotionState{PlatformMotionState::Park};
    bool platformMotionActive{};
    float platformMotionElapsedMilliseconds{};
    std::int32_t platformTargetWaypointId{-1};
    bool trainActive{};
    bool trainCut{};
    float trainCurrentSpeedCentimetersPerMillisecond{};
    std::int32_t trainTargetWaypointId{-1};
    std::int32_t trainPreviousObjectId{-1};
    std::int32_t trainNextObjectId{-1};
    assets::Vector3 trainDirection{-1.0F, 0.0F, 0.0F};
    assets::Quaternion trainRotation;
    assets::Vector3 trainVelocityCentimetersPerSecond;
    std::int32_t bridgeState{};
    float bridgeStateSeconds{};
    assets::Vector3 bridgeVelocity;
    assets::Vector3 bridgeAngularVelocity;
    assets::Quaternion bridgeRotation;
    // CSlideCar::Init/SetOnBridge/SetState (0x0031bea0, 0x0031bbac,
    // 0x0031bc94). State 0 is the authored parked pose, state 2 is the
    // bridge-launched body, state 3 is its unsupported fall, and state 4 is
    // the native removal path.
    std::int32_t slideCarState{};
    float slideCarStateSeconds{};
    std::int32_t slideCarBridgeObjectId{-1};
    assets::Vector3 slideCarVelocity;
    assets::Vector3 slideCarInitialPosition;
    assets::Quaternion slideCarInitialRotation;
    assets::Quaternion slideCarRotation;
    assets::Vector3 slideCarBridgeMinimum;
    assets::Vector3 slideCarBridgeMaximum;
    std::int32_t areaDamageState{-1};
    float areaDamageStateMilliseconds{};
    float areaDamageWaitMilliseconds{};
    std::uint32_t areaDamageContactCooldownMilliseconds{};
    bool areaDamagePlayerHit{};
    LevelObjectDestructionPhase destructionPhase{
        LevelObjectDestructionPhase::Intact};
};

enum class LevelObjectEventKind {
    Hit,
    Destroyed,
    ComicCollected,
    BridgeDrop,
    BridgeStateChanged,
};

struct LevelObjectEvent {
    LevelObjectEventKind kind{LevelObjectEventKind::Hit};
    std::int32_t objectId{-1};
    std::int32_t roomId{-1};
    assets::Vector3 position;
    std::int32_t voxSoundId{-1};
    std::int32_t cinematicId{-1};
    std::string effectType;
    std::int32_t collectibleIndex{-1};
    std::string levelStringId;
    std::int32_t bridgeState{};
};

struct ElectricPlatformDamageEvent {
    std::int32_t objectId{-1};
    std::int32_t roomId{-1};
    assets::Vector3 position;
    float damage{};
    // Unit::CheckDamageAreaCollide (0x0032609c) emits damage kind 0xcb and
    // electric-body effect ID 0x96 for physics flag 0x2000 contacts.
    std::int32_t damageType{0xcb};
    std::int32_t effectId{0x96};
};

struct AreaDamageEvent {
    std::int32_t objectId{-1};
    std::int32_t roomId{-1};
    assets::Vector3 position;
    float damage{};
    std::int32_t damageType{};
    std::int32_t hitType{0xcc};
};

struct PlayerObjectMeleeHitResult {
    std::int32_t objectId{-1};
    float actualDamage{};
};

struct LevelObjectSupportPose {
    std::int32_t objectId{-1};
    assets::Vector3 position;
    assets::Quaternion rotation;
    assets::Vector3 scale{1.0F, 1.0F, 1.0F};
};

// Portable state for the concrete CAnimatedObject, CDestroyableObject,
// CStaticObject, CDestroyableStreamPiping, and CTrain instances authored in room IRR
// files. It owns only gameplay state; mesh resources remain in the bootstrap.
class LevelObjectRuntime final {
public:
    [[nodiscard]] Result initialize(const LevelOneBootstrap& level);
    void advanceAnimations(std::uint32_t elapsedMilliseconds) noexcept;
    void updateBrokenBridges(const assets::Vector3& playerPosition,
                             std::uint32_t elapsedMilliseconds) noexcept;
    void updateAreaDamage(const assets::Vector3& playerPosition,
                          std::uint32_t elapsedMilliseconds) noexcept;
    void updateComicCollections(
        const assets::Vector3& playerPosition) noexcept;
    void updateElectricPlatformContacts(
        const assets::Vector3& playerPosition,
        std::uint32_t elapsedMilliseconds) noexcept;
    [[nodiscard]] Result applyCinematicCommand(
        const LevelOneBootstrap& level, const CinematicThread& thread,
        const CinematicCommand& command);
    void endColladaAnimation(std::int32_t objectId) noexcept;
    void resetTransientForCheckPointLoad() noexcept;
    [[nodiscard]] Result setRuntimeState(std::int32_t objectId,
                                         const assets::Vector3& position,
                                         bool visible,
                                         bool physicsEnabled);
    [[nodiscard]] std::optional<std::int32_t> applyPlayerMeleeHit(
        const assets::Vector3& attackPosition,
        const assets::Vector3& attackDirection, float radius, float damage,
        float minimumForwardDot = 0.0F) noexcept;
    [[nodiscard]] std::optional<PlayerObjectMeleeHitResult>
    applyPlayerMeleeHitDetailed(
        const assets::Vector3& attackPosition,
        const assets::Vector3& attackDirection, float radius, float damage,
        float minimumForwardDot = 0.0F) noexcept;
    [[nodiscard]] std::vector<PlayerObjectMeleeHitResult>
    applyPlayerMeleeHits(
        const assets::Vector3& attackPosition,
        const assets::Vector3& attackDirection, float radius, float damage,
        float minimumForwardDot = 0.0F) noexcept;
    [[nodiscard]] bool destroy(std::int32_t objectId) noexcept;
    [[nodiscard]] bool isDestroyed(std::int32_t objectId) const noexcept;
    [[nodiscard]] bool isComicCollected(
        std::int32_t objectId) const noexcept;
    [[nodiscard]] Result setAnimation(std::int32_t objectId,
                                      std::string_view animation,
                                      bool loop);
    [[nodiscard]] bool animationFinished(
        std::int32_t objectId) const noexcept;
    [[nodiscard]] std::vector<LevelObjectEvent> consumeEvents();
    [[nodiscard]] std::vector<ElectricPlatformDamageEvent>
    consumeElectricPlatformDamageEvents();
    [[nodiscard]] std::vector<AreaDamageEvent> consumeAreaDamageEvents();
    [[nodiscard]] std::uint32_t electricContactCooldownMilliseconds() const
        noexcept {
        return electricContactCooldownMilliseconds_;
    }

    [[nodiscard]] std::span<const LevelObjectState> states() const noexcept {
        return states_;
    }
    [[nodiscard]] const LevelObjectState* find(std::int32_t objectId) const
        noexcept;
    [[nodiscard]] std::optional<LevelObjectSupportPose> supportPose(
        std::int32_t objectId) const noexcept;
    [[nodiscard]] assets::Vector3 supportMotionDelta(
        const LevelObjectSupportPose& previous,
        const assets::Vector3& supportedPoint) const noexcept;

private:
    [[nodiscard]] LevelObjectState* findMutable(std::int32_t objectId) noexcept;
    void beginDestruction(LevelObjectState& object) noexcept;
    void finishDestruction(LevelObjectState& object) noexcept;

    const LevelOneBootstrap* level_{};
    std::vector<LevelObjectState> states_;
    std::vector<LevelObjectEvent> events_;
    std::vector<ElectricPlatformDamageEvent> electricDamageEvents_;
    std::vector<AreaDamageEvent> areaDamageEvents_;
    std::uint32_t electricContactCooldownMilliseconds_{};
};

} // namespace usm::game
