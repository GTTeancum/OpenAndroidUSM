#pragma once

#include "assets/IrrScene.hpp"
#include "core/Result.hpp"
#include "game/CinematicCamera.hpp"
#include "game/LevelOneBootstrap.hpp"

#include <array>
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
                                    const LevelCollision* collision = nullptr);
    void update(const PlayerMotionInput& input, const CameraPose& camera,
                std::uint32_t elapsedMilliseconds) noexcept;

    [[nodiscard]] const assets::Vector3& position() const noexcept {
        return position_;
    }
    [[nodiscard]] const std::array<float, 16>& worldTransform() const noexcept {
        return worldTransform_;
    }
    [[nodiscard]] std::string_view activeAnimation() const noexcept {
        return activeAnimation_;
    }
    [[nodiscard]] std::uint32_t animationTimeMilliseconds() const noexcept;

private:
    void setAnimation(std::string_view animation) noexcept;
    void updateWorldTransform(const assets::Vector3& facing) noexcept;

    assets::Vector3 position_;
    std::array<float, 16> worldTransform_{};
    assets::Vector3 scale_{1.0F, 1.0F, 1.0F};
    assets::Vector3 facing_{1.0F, 0.0F, 0.0F};
    std::string_view activeAnimation_{"idle_stand"};
    std::uint64_t animationTimeMilliseconds_{};
    const LevelCollision* collision_{};
};

} // namespace usm::game
