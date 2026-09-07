#pragma once

#include "assets/ColladaAnimation.hpp"
#include "assets/IrrScene.hpp"
#include "game/ButtonConfig.hpp"
#include "game/ButtonMashProgress.hpp"
#include "game/PlayerStateConfig.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace usm::game {

enum class WallWebPhase : std::int8_t { Inactive = -1, Start = 0, Hold = 1, Success = 2, Failure = 3 };
enum class WallWebEventKind { Capture, Hold, Release, Finish };
struct WallWebEvent {
    WallWebEventKind kind{};
    std::int32_t targetObjectId{-1};
    bool success{};
};

// Player::SetOnWallWebDir/UpdateQTE/DoQTEAction/ExitQTE
// (0x00342bf4/0x0034caf8/0x0034c908/0x0034c888), without renderer/input APIs.
class WallWebRuntime final {
public:
    [[nodiscard]] bool begin(int angle, std::int32_t targetObjectId,
        const PlayerStateDefinition& state, const assets::ColladaAnimationFile& bank,
        const ButtonConfigDatabase& buttons);
    void update(std::uint32_t elapsedMilliseconds, bool actionPressed,
                bool targetAlive = true);
    void cancel();
    [[nodiscard]] std::vector<WallWebEvent> consumeEvents();
    [[nodiscard]] static int directionAngle(const assets::Vector3& direction,
                                          const assets::Vector3& wallNormal) noexcept;
    [[nodiscard]] bool active() const noexcept { return phase_ != WallWebPhase::Inactive; }
    [[nodiscard]] WallWebPhase phase() const noexcept { return phase_; }
    [[nodiscard]] bool promptActive() const noexcept { return phase_ == WallWebPhase::Hold; }
    [[nodiscard]] bool lineActive() const noexcept { return lineActive_; }
    [[nodiscard]] int angle() const noexcept { return angle_; }
    [[nodiscard]] std::int32_t targetObjectId() const noexcept { return targetObjectId_; }
    [[nodiscard]] std::uint32_t animationMilliseconds() const noexcept { return animationMilliseconds_; }
    [[nodiscard]] std::string_view animation() const noexcept;
    [[nodiscard]] std::string_view targetBone() const noexcept;
    [[nodiscard]] std::string_view playerHandNode() const noexcept {
        return angle_ <= 180 ? "FX_RH" : "FX_LH";
    }
    [[nodiscard]] float progress() const noexcept;
    [[nodiscard]] std::int16_t completedActionCount() const noexcept {
        return buttonProgress_.completed();
    }

private:
    void enter(WallWebPhase phase) noexcept;
    void release(bool success);
    std::array<const assets::ColladaAnimationClip*, 4> clips_{};
    const ButtonConfigDefinition* button_{};
    WallWebPhase phase_{WallWebPhase::Inactive};
    std::int32_t targetObjectId_{-1};
    int angle_{};
    std::uint32_t animationMilliseconds_{};
    std::uint32_t promptMilliseconds_{};
    std::uint32_t captureMilliseconds_{};
    std::uint32_t releaseMilliseconds_{};
    ButtonMashProgress buttonProgress_;
    bool captured_{};
    bool released_{};
    bool lineActive_{};
    std::vector<WallWebEvent> events_;
};

} // namespace usm::game
