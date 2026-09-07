#include "game/WallWebRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace usm::game {

int WallWebRuntime::directionAngle(const assets::Vector3& direction,
                                    const assets::Vector3& normal) noexcept {
    const float side = direction.x * -normal.y + direction.y * normal.x;
    float angle = std::atan2(side, direction.z) * 57.2957795F;
    if (angle >= -22.5F && angle <= 22.5F) {
        return 0;
    }
    if (angle < 0.0F) {
        angle += 360.0F;
    }
    for (int center = 45; center <= 315; center += 45) {
        if (angle <= static_cast<float>(center) + 22.5F) {
            // There is no 180-degree clip in the shipped bank.
            return center == 180 ? (angle <= 180.0F ? 135 : 225) : center;
        }
    }
    return 0;
}

bool WallWebRuntime::begin(int angle, std::int32_t targetObjectId,
    const PlayerStateDefinition& state, const assets::ColladaAnimationFile& bank,
    const ButtonConfigDatabase& buttons) {
    if (active() || targetObjectId < 0 || state.id != 73 || state.motionType != 132 ||
        state.auxiliaryParameters[0] < 0 || state.soundTriggerFrame < 0) {
        return false;
    }
    const auto* button = buttons.find(12);
    if (button == nullptr || button->interactionType != 3 ||
        button->durationMilliseconds <= 0.0F || button->requiredActionCount <= 0) {
        return false;
    }
    constexpr std::array<std::string_view, 4> prefixes{
        "wall_drag_start", "wall_drag_keep", "wall_drag_success", "wall_drag_fail"};
    std::array<const assets::ColladaAnimationClip*, 4> clips{};
    for (std::size_t index = 0; index < clips.size(); ++index) {
        clips[index] = bank.findClip(std::string(prefixes[index]) + std::to_string(angle));
        if (clips[index] == nullptr || clips[index]->durationMilliseconds() == 0) {
            return false;
        }
    }
    clips_ = clips;
    button_ = button;
    angle_ = angle;
    targetObjectId_ = targetObjectId;
    captureMilliseconds_ = static_cast<std::uint32_t>(state.auxiliaryParameters[0]) * 1000U / 30U;
    releaseMilliseconds_ = static_cast<std::uint32_t>(state.soundTriggerFrame) * 1000U / 30U;
    captured_ = released_ = lineActive_ = false;
    promptMilliseconds_ = 0;
    buttonProgress_.reset();
    enter(WallWebPhase::Start);
    return true;
}

void WallWebRuntime::enter(WallWebPhase phase) noexcept {
    phase_ = phase;
    animationMilliseconds_ = 0;
}

void WallWebRuntime::release(bool success) {
    if (captured_ && !released_) {
        events_.push_back({WallWebEventKind::Release, targetObjectId_, success});
        released_ = true;
    }
    lineActive_ = false;
}

void WallWebRuntime::cancel() {
    const bool wasActive = active();
    release(false);
    if (wasActive) {
        events_.push_back({WallWebEventKind::Finish, targetObjectId_, false});
    }
    enter(WallWebPhase::Inactive);
}

void WallWebRuntime::update(std::uint32_t elapsedMilliseconds, bool actionPressed,
                            bool targetAlive) {
    if (!active()) {
        return;
    }
    if (!targetAlive && !released_) {
        cancel();
        return;
    }
    const auto* clip = clips_[static_cast<std::size_t>(phase_)];
    const auto duration = clip->durationMilliseconds();
    const auto nextTime = static_cast<std::uint64_t>(animationMilliseconds_) + elapsedMilliseconds;
    animationMilliseconds_ = static_cast<std::uint32_t>(
        phase_ == WallWebPhase::Hold ? nextTime % duration : std::min<std::uint64_t>(nextTime, duration));
    if (phase_ == WallWebPhase::Start) {
        if (!captured_ && animationMilliseconds_ >= captureMilliseconds_) {
            captured_ = lineActive_ = true;
            events_.push_back({WallWebEventKind::Capture, targetObjectId_, false});
        }
        if (animationMilliseconds_ >= duration) {
            enter(WallWebPhase::Hold);
            events_.push_back({WallWebEventKind::Hold, targetObjectId_, false});
        }
    } else if (phase_ == WallWebPhase::Hold) {
        const auto limit = static_cast<std::uint32_t>(std::lround(button_->durationMilliseconds));
        promptMilliseconds_ = static_cast<std::uint32_t>(std::min<std::uint64_t>(limit,
            static_cast<std::uint64_t>(promptMilliseconds_) + elapsedMilliseconds));
        if (buttonProgress_.update(elapsedMilliseconds, actionPressed, button_->requiredActionCount, true)) {
            enter(WallWebPhase::Success);
        } else if (promptMilliseconds_ >= limit) {
            lineActive_ = false;
            enter(WallWebPhase::Failure);
        }
    } else {
        if (phase_ == WallWebPhase::Success && animationMilliseconds_ >= releaseMilliseconds_) {
            release(true);
        }
        if (animationMilliseconds_ >= duration) {
            release(phase_ == WallWebPhase::Success);
            events_.push_back({WallWebEventKind::Finish, targetObjectId_,
                               phase_ == WallWebPhase::Success});
            enter(WallWebPhase::Inactive);
        }
    }
}

std::vector<WallWebEvent> WallWebRuntime::consumeEvents() {
    std::vector<WallWebEvent> result;
    result.swap(events_);
    return result;
}

std::string_view WallWebRuntime::animation() const noexcept {
    return active() ? clips_[static_cast<std::size_t>(phase_)]->name : std::string_view{};
}

std::string_view WallWebRuntime::targetBone() const noexcept {
    switch (angle_) {
    case 0: case 315: return "Bip01_R_Foot";
    case 45: return "Bip01_L_Foot";
    case 90: return "Bip01_L_Forearm";
    case 135: case 225: return "Bip01_Head";
    case 270: return "Bip01_R_Forearm";
    default: return {};
    }
}

float WallWebRuntime::progress() const noexcept {
    return button_ == nullptr ? 0.0F : std::clamp(
        static_cast<float>(buttonProgress_.completed()) / button_->requiredActionCount, 0.0F, 1.0F);
}

} // namespace usm::game
