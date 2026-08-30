#include "game/LevelRestoreRuntime.hpp"

#include <algorithm>
#include <cmath>

namespace usm::game {
namespace {

constexpr float kPlayerRadius = 50.0F;
constexpr float kPlayerHalfHeight = 70.0F;
constexpr std::uint32_t kFadeMilliseconds = 1280;
constexpr std::uint32_t kHoldEndMilliseconds = 1792;

assets::Vector3 inverseRotate(assets::Vector3 value,
                              assets::Quaternion rotation) noexcept {
    const float lengthSquared = rotation.x * rotation.x +
                                rotation.y * rotation.y +
                                rotation.z * rotation.z +
                                rotation.w * rotation.w;
    if (lengthSquared <= 1e-8F) {
        return value;
    }
    rotation.x = -rotation.x / lengthSquared;
    rotation.y = -rotation.y / lengthSquared;
    rotation.z = -rotation.z / lengthSquared;
    rotation.w /= lengthSquared;
    const assets::Vector3 q{rotation.x, rotation.y, rotation.z};
    const assets::Vector3 first{q.y * value.z - q.z * value.y,
                                q.z * value.x - q.x * value.z,
                                q.x * value.y - q.y * value.x};
    const assets::Vector3 second{q.y * first.z - q.z * first.y,
                                 q.z * first.x - q.x * first.z,
                                 q.x * first.y - q.y * first.x};
    return {value.x + 2.0F * (rotation.w * first.x + second.x),
            value.y + 2.0F * (rotation.w * first.y + second.y),
            value.z + 2.0F * (rotation.w * first.z + second.z)};
}

} // namespace

Result LevelRestoreRuntime::bind(
    std::span<const LevelRestoreTriggerAsset> triggers,
    std::span<const LevelRestorePointAsset> restorePoints) {
    for (const LevelRestoreTriggerAsset& trigger : triggers) {
        const auto point = std::find_if(
            restorePoints.begin(), restorePoints.end(),
            [&trigger](const LevelRestorePointAsset& candidate) {
                return candidate.objectId == trigger.restorePointId;
            });
        if (point == restorePoints.end()) {
            return Result::failure(
                "TriggerRestore references a missing restore point");
        }
    }
    triggers_ = triggers;
    restorePoints_ = restorePoints;
    active_ = nullptr;
    elapsedMilliseconds_ = 0;
    alpha_ = 0.0F;
    restored_ = false;
    events_.clear();
    return Result::success();
}

void LevelRestoreRuntime::update(
    const assets::Vector3& playerPosition,
    std::uint32_t elapsedMilliseconds) noexcept {
    if (active_ == nullptr) {
        const auto trigger = std::find_if(
            triggers_.begin(), triggers_.end(),
            [&playerPosition](const LevelRestoreTriggerAsset& candidate) {
                return containsPlayer(candidate, playerPosition);
            });
        if (trigger != triggers_.end()) {
            active_ = &*trigger;
            elapsedMilliseconds_ = 0;
            alpha_ = 0.0F;
            restored_ = false;
        }
        return;
    }

    elapsedMilliseconds_ = std::min(
        kHoldEndMilliseconds, elapsedMilliseconds_ + elapsedMilliseconds);
    if (elapsedMilliseconds_ < kFadeMilliseconds) {
        const std::uint32_t nativeAlpha = std::min<std::uint32_t>(
            elapsedMilliseconds_ * 256U / kFadeMilliseconds, 255U);
        alpha_ = static_cast<float>(nativeAlpha) / 255.0F;
    } else {
        alpha_ = 1.0F;
        if (!restored_) {
            const auto point = std::find_if(
                restorePoints_.begin(), restorePoints_.end(),
                [this](const LevelRestorePointAsset& candidate) {
                    return candidate.objectId == active_->restorePointId;
                });
            if (point != restorePoints_.end()) {
                events_.push_back({active_, &*point});
            }
            restored_ = true;
        }
    }
    if (elapsedMilliseconds_ >= kHoldEndMilliseconds) {
        active_ = nullptr;
        elapsedMilliseconds_ = 0;
        alpha_ = 0.0F;
        restored_ = false;
    }
}

std::vector<LevelRestoreEvent> LevelRestoreRuntime::consumeEvents() {
    std::vector<LevelRestoreEvent> result;
    result.swap(events_);
    return result;
}

bool LevelRestoreRuntime::containsPlayer(
    const LevelRestoreTriggerAsset& trigger,
    const assets::Vector3& player) noexcept {
    assets::Vector3 relative{player.x - trigger.position.x,
                             player.y - trigger.position.y,
                             player.z + kPlayerHalfHeight -
                                 trigger.position.z};
    relative = inverseRotate(relative, trigger.rotation);
    const assets::Vector3 half{
        std::abs(trigger.sizes.x * trigger.scale.x) * 0.5F + kPlayerRadius,
        std::abs(trigger.sizes.y * trigger.scale.y) * 0.5F + kPlayerRadius,
        std::abs(trigger.sizes.z * trigger.scale.z) * 0.5F +
            kPlayerHalfHeight};
    return std::abs(relative.x) <= half.x &&
           std::abs(relative.y) <= half.y &&
           std::abs(relative.z) <= half.z;
}

} // namespace usm::game
