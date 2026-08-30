#pragma once

#include "assets/IrrScene.hpp"
#include "core/Result.hpp"
#include "game/CinematicCamera.hpp"
#include "game/LevelOneBootstrap.hpp"
#include "game/PlayerStateConfig.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace usm::game {

class LevelCollision;

struct PlayerMotionInput {
    float right{};
    float forward{};
};

// Portable, renderer-independent reconstruction of the normal ground movement
// path in Player::GetJoyStickDirToGameDir (0x00343a64), Player::moveForward
// (0x00341524), and Player::UpdateMCSpeed (0x00346f50).
class GameplayPlayer final {
public:
    [[nodiscard]] Result initialize(const LevelPlayerAsset& asset,
                                    const LevelCollision* collision = nullptr,
                                    const PlayerStateConfigDatabase* states =
                                        nullptr);
    [[nodiscard]] bool requestPunch() noexcept;
    [[nodiscard]] bool requestJump() noexcept;
    [[nodiscard]] bool applyDamage(float damage) noexcept;
    void update(const PlayerMotionInput& input, const CameraPose& camera,
                std::uint32_t elapsedMilliseconds) noexcept;
    [[nodiscard]] bool consumePunchImpact() noexcept;
    [[nodiscard]] bool consumePunchSoundFrame() noexcept;
    [[nodiscard]] std::string_view consumeEnteredState() noexcept;

    [[nodiscard]] const assets::Vector3& position() const noexcept {
        return position_;
    }
    [[nodiscard]] const assets::Vector3& facing() const noexcept {
        return facing_;
    }
    [[nodiscard]] const std::array<float, 16>& worldTransform() const noexcept {
        return worldTransform_;
    }
    [[nodiscard]] std::string_view activeAnimation() const noexcept {
        return activeAnimation_;
    }
    [[nodiscard]] std::uint32_t animationTimeMilliseconds() const noexcept;
    [[nodiscard]] float health() const noexcept { return health_; }
    [[nodiscard]] float maximumHealth() const noexcept { return maximumHealth_; }
    [[nodiscard]] bool dead() const noexcept { return health_ <= 0.0F; }
    [[nodiscard]] bool airborne() const noexcept;
    [[nodiscard]] std::uint16_t activeStateId() const noexcept;
    [[nodiscard]] float animatedFootHeight() const noexcept;

private:
    enum class AttackState {
        None,
        PunchRight,
        Recover,
    };

    enum class LocomotionState {
        Grounded,
        JumpStart,
        JumpFall,
        SustainedFall,
        JumpLand,
    };

    void setAnimation(std::string_view animation) noexcept;
    void queueEnteredState(std::string_view stateName) noexcept;
    void enterLocomotionState(LocomotionState state) noexcept;
    void updateJump(const PlayerMotionInput& input, const CameraPose& camera,
                    std::uint32_t elapsedMilliseconds) noexcept;
    void updateAirHorizontalMotion(const PlayerMotionInput& input,
                                   const CameraPose& camera,
                                   std::uint32_t elapsedMilliseconds) noexcept;
    [[nodiscard]] float currentRootHeight() const noexcept;
    [[nodiscard]] bool findLandingHeight(float referenceHeight,
                                         float& height) const noexcept;
    void updateWorldTransform(const assets::Vector3& facing) noexcept;

    assets::Vector3 position_;
    assets::Vector3 renderPosition_;
    float jumpAnchorHeight_{};
    std::array<float, 16> worldTransform_{};
    assets::Vector3 scale_{1.0F, 1.0F, 1.0F};
    assets::Vector3 facing_{1.0F, 0.0F, 0.0F};
    std::string_view activeAnimation_{"idle_stand"};
    std::uint64_t animationTimeMilliseconds_{};
    const LevelCollision* collision_{};
    const assets::ColladaAnimationFile* animationBank_{};
    const PlayerStateDefinition* jumpStartState_{};
    const PlayerStateDefinition* jumpFallState_{};
    const PlayerStateDefinition* sustainedFallState_{};
    const PlayerStateDefinition* jumpLandState_{};
    const PlayerStateDefinition* activeLocomotionState_{};
    LocomotionState locomotionState_{LocomotionState::Grounded};
    float verticalVelocityCentimetersPerSecond_{};
    AttackState attackState_{AttackState::None};
    bool punchImpactPending_{};
    bool punchImpactEmitted_{};
    bool punchSoundFramePending_{};
    bool punchSoundFrameEmitted_{};
    std::uint32_t punchSoundFrameMilliseconds_{300};
    std::array<std::string_view, 4> enteredStates_{};
    std::size_t enteredStateCount_{};
    float health_{1000.0F};
    float maximumHealth_{1000.0F};
};

} // namespace usm::game
