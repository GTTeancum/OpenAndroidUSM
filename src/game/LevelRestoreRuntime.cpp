#include "game/LevelRestoreRuntime.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <string_view>

namespace usm::game {
namespace {

constexpr std::uint32_t kFadeMilliseconds = 1280;
constexpr std::uint32_t kHoldEndMilliseconds = 1792;

bool parseInteger(const CinematicCommand& command, std::string_view name,
                  std::int32_t& output) noexcept {
    const CinematicAttribute* attribute = command.findAttribute(name);
    if (attribute == nullptr) {
        return false;
    }
    const char* begin = attribute->value.data();
    const char* end = begin + attribute->value.size();
    const auto parsed = std::from_chars(begin, end, output);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

bool parseBoolean(const CinematicCommand& command, std::string_view name,
                  bool& output) noexcept {
    const CinematicAttribute* attribute = command.findAttribute(name);
    if (attribute == nullptr) {
        return false;
    }
    if (attribute->value == "true" || attribute->value == "1") {
        output = true;
        return true;
    }
    if (attribute->value == "false" || attribute->value == "0") {
        output = false;
        return true;
    }
    return false;
}

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

assets::Vector3 transformPointByInverse(
    const std::array<float, 16>& matrix,
    const assets::Vector3& point) noexcept {
    // CTriggerRestore::ProcessAttr (0x0036c170) reads the serialized
    // AbsoluteTransformation, calls CMatrix4::getInverse, and gives that
    // matrix to obbox::init_obb. obbox::test_obb (0x003d45a4) then transforms
    // the player's world point by it before comparing authored half extents.
    const float a00 = matrix[0];
    const float a01 = matrix[4];
    const float a02 = matrix[8];
    const float a10 = matrix[1];
    const float a11 = matrix[5];
    const float a12 = matrix[9];
    const float a20 = matrix[2];
    const float a21 = matrix[6];
    const float a22 = matrix[10];
    const float determinant =
        a00 * (a11 * a22 - a12 * a21) -
        a01 * (a10 * a22 - a12 * a20) +
        a02 * (a10 * a21 - a11 * a20);
    if (std::abs(determinant) <= 1e-8F) {
        return {0.0F, 0.0F, 0.0F};
    }
    const float inverseDeterminant = 1.0F / determinant;
    const float x = point.x - matrix[12];
    const float y = point.y - matrix[13];
    const float z = point.z - matrix[14];
    return {
        ((a11 * a22 - a12 * a21) * x +
         (a02 * a21 - a01 * a22) * y +
         (a01 * a12 - a02 * a11) * z) *
            inverseDeterminant,
        ((a12 * a20 - a10 * a22) * x +
         (a00 * a22 - a02 * a20) * y +
         (a02 * a10 - a00 * a12) * z) *
            inverseDeterminant,
        ((a10 * a21 - a11 * a20) * x +
         (a01 * a20 - a00 * a21) * y +
         (a00 * a11 - a01 * a10) * z) *
            inverseDeterminant};
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
    enabled_.assign(triggers.size(), true);
    active_ = nullptr;
    elapsedMilliseconds_ = 0;
    alpha_ = 0.0F;
    restored_ = false;
    events_.clear();
    return Result::success();
}

Result LevelRestoreRuntime::applyCinematicCommand(
    const CinematicThread& thread, const CinematicCommand& command) {
    if (command.name != "EnableTriggerRestore") {
        return Result::success();
    }
    // CCinematicThread::EnableTriggerRestore (0x003719d8) accepts this
    // command only on a Basic thread (type 1). It resolves the authored ID
    // through CLevel::FindTriggerRestoreById (0x0037dc94), treats a missing
    // object as a successful no-op, and writes CTriggerRestore+0xc8.
    if (thread.type != 1) {
        return Result::failure(
            "EnableTriggerRestore requires a Basic cinematic thread");
    }
    std::int32_t objectId = -1;
    bool enable = false;
    if (!parseInteger(command, "^ID^TriggerRestore", objectId) ||
        !parseBoolean(command, "enable", enable)) {
        return Result::failure(
            "EnableTriggerRestore has invalid attributes");
    }
    const auto trigger = std::find_if(
        triggers_.begin(), triggers_.end(),
        [objectId](const LevelRestoreTriggerAsset& candidate) {
            return candidate.objectId == objectId;
        });
    if (objectId < 0 || trigger == triggers_.end()) {
        return Result::success();
    }
    enabled_[static_cast<std::size_t>(trigger - triggers_.begin())] = enable;
    return Result::success();
}

void LevelRestoreRuntime::update(
    const assets::Vector3& playerPosition,
    std::uint32_t elapsedMilliseconds, bool updatesEnabled,
    bool playerCanEnable) noexcept {
    // CTriggerRestore::Update (0x0036bf50) consults
    // Player::CanEnableTriggerRestore only while idle: web motions 26/27,
    // class-8 death states, and IsDead (0x00340084) reject activation.
    // A separate update gate applies once CLevel's death screen is
    // active it returns before updating TriggerRestore at all, so preserve a
    // currently opaque restore frame rather than recycling the volume.
    if (!updatesEnabled) {
        return;
    }
    if (active_ != nullptr) {
        const std::size_t activeIndex =
            static_cast<std::size_t>(active_ - triggers_.data());
        if (activeIndex >= enabled_.size() || !enabled_[activeIndex]) {
            return;
        }
    }
    if (active_ == nullptr) {
        if (!playerCanEnable) {
            return;
        }
        for (std::size_t index = 0; index < triggers_.size(); ++index) {
            if (enabled_[index] &&
                containsPlayer(triggers_[index], playerPosition)) {
                active_ = &triggers_[index];
                elapsedMilliseconds_ = 0;
                alpha_ = 0.0F;
                restored_ = false;
                break;
            }
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

std::optional<bool> LevelRestoreRuntime::enabled(
    std::int32_t objectId) const noexcept {
    const auto trigger = std::find_if(
        triggers_.begin(), triggers_.end(),
        [objectId](const LevelRestoreTriggerAsset& candidate) {
            return candidate.objectId == objectId;
        });
    if (trigger == triggers_.end()) {
        return std::nullopt;
    }
    return enabled_[static_cast<std::size_t>(trigger - triggers_.begin())];
}

std::vector<LevelRestoreEvent> LevelRestoreRuntime::consumeEvents() {
    std::vector<LevelRestoreEvent> result;
    result.swap(events_);
    return result;
}

bool LevelRestoreRuntime::containsPlayer(
    const LevelRestoreTriggerAsset& trigger,
    const assets::Vector3& player) noexcept {
    // obbox::test_obb(vector3d const&) (0x003d45a4) first performs a
    // world-space broad-phase test using twice the smallest authored size.
    // This is observable on the unusually long Room 7 restore volume: its
    // oriented box extends beneath Slide 1039, but native code rejects slide
    // positions that are more than 1000 cm from the volume center before it
    // evaluates the inverse transform.
    const float broadPhaseHalfExtent =
        std::min({trigger.sizes.x, trigger.sizes.y, trigger.sizes.z}) *
        2.0F;
    if (std::abs(player.x - trigger.position.x) > broadPhaseHalfExtent ||
        std::abs(player.y - trigger.position.y) > broadPhaseHalfExtent ||
        std::abs(player.z - trigger.position.z) > broadPhaseHalfExtent) {
        return false;
    }

    assets::Vector3 relative;
    if (trigger.worldTransform[15] != 0.0F) {
        relative = transformPointByInverse(trigger.worldTransform, player);
    } else {
        // Synthetic fixtures predating retained absolute matrices still use
        // the equivalent translation/rotation/scale decomposition.
        relative = {player.x - trigger.position.x,
                    player.y - trigger.position.y,
                    player.z - trigger.position.z};
        relative = inverseRotate(relative, trigger.rotation);
    }

    // CTriggerRestore::Update (0x0036bf50) asks Player's position vfunc for a
    // vector3d and passes that point to the native OBB test. It does not test
    // or inflate by the player's collision OBB.
    const bool absoluteTransformAvailable = trigger.worldTransform[15] != 0.0F;
    const assets::Vector3 half{
        std::abs(trigger.sizes.x *
                 (absoluteTransformAvailable ? 1.0F : trigger.scale.x)) *
            0.5F,
        std::abs(trigger.sizes.y *
                 (absoluteTransformAvailable ? 1.0F : trigger.scale.y)) *
            0.5F,
        std::abs(trigger.sizes.z *
                 (absoluteTransformAvailable ? 1.0F : trigger.scale.z)) *
            0.5F};
    return std::abs(relative.x) <= half.x &&
           std::abs(relative.y) <= half.y &&
           std::abs(relative.z) <= half.z;
}

} // namespace usm::game
