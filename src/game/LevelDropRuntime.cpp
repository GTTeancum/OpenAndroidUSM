#include "game/LevelDropRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace usm::game {
namespace {

constexpr float kPlayerRadius = 50.0F;
constexpr float kPlayerHeight = 140.0F;
constexpr float kDropAcceleration = 4000.0F;

} // namespace

Result LevelDropRuntime::initialize(
    std::span<const LevelDropAreaAsset> areas,
    std::span<const LevelDropObjectAsset> objects) {
    areas_.clear();
    states_.clear();
    events_.clear();
    areas_.reserve(areas.size());
    states_.reserve(objects.size());
    for (const LevelDropAreaAsset& area : areas) {
        areas_.push_back({&area, false});
    }
    for (const LevelDropObjectAsset& object : objects) {
        const auto owner = std::find_if(
            areas.begin(), areas.end(), [&object](const LevelDropAreaAsset& area) {
                return area.objectId == object.ownerAreaId;
            });
        if (owner == areas.end() || object.delayMilliseconds < 0 ||
            object.damage < 0.0F) {
            areas_.clear();
            states_.clear();
            return Result::failure(
                "Drop object has an invalid owner, delay, or damage");
        }
        LevelDropObjectState state;
        state.asset = &object;
        state.position = object.position;
        state.delayRemainingMilliseconds = object.delayMilliseconds;
        states_.push_back(state);
    }
    return Result::success();
}

void LevelDropRuntime::update(
    const assets::Vector3& playerPosition,
    std::uint32_t elapsedMilliseconds) noexcept {
    for (AreaState& area : areas_) {
        if (area.activated || area.asset == nullptr ||
            !areaContainsPlayer(*area.asset, playerPosition)) {
            continue;
        }
        area.activated = true;
        for (LevelDropObjectState& state : states_) {
            if (state.asset == nullptr ||
                state.asset->ownerAreaId != area.asset->objectId ||
                state.phase != LevelDropPhase::Dormant) {
                continue;
            }
            state.phase = LevelDropPhase::Delay;
            state.visible = true;
            state.delayRemainingMilliseconds =
                state.asset->delayMilliseconds;
            events_.push_back(
                {LevelDropEventKind::Activated, state.asset->objectId,
                 state.asset->roomId, state.position, area.asset->effectType,
                 0.0F});
        }
    }

    for (LevelDropObjectState& state : states_) {
        if (state.asset == nullptr ||
            state.phase == LevelDropPhase::Dormant ||
            state.phase == LevelDropPhase::Hidden) {
            continue;
        }
        std::uint32_t fallingMilliseconds = elapsedMilliseconds;
        if (state.phase == LevelDropPhase::Delay) {
            if (state.delayRemainingMilliseconds >
                static_cast<std::int32_t>(elapsedMilliseconds)) {
                state.delayRemainingMilliseconds -=
                    static_cast<std::int32_t>(elapsedMilliseconds);
                continue;
            }
            fallingMilliseconds -= static_cast<std::uint32_t>(
                std::max(state.delayRemainingMilliseconds, 0));
            state.delayRemainingMilliseconds = 0;
            state.phase = LevelDropPhase::Falling;
            state.physicsEnabled = true;
            events_.push_back(
                {LevelDropEventKind::BeganFalling, state.asset->objectId,
                 state.asset->roomId, state.position,
                 state.asset->effectType, 0.0F});
        }
        if (state.phase != LevelDropPhase::Falling) {
            continue;
        }
        const float fallingSeconds =
            static_cast<float>(fallingMilliseconds) * 0.001F;
        state.position.z -= state.downwardVelocity * fallingSeconds;
        state.downwardVelocity += kDropAcceleration * fallingSeconds;
        if (!state.hitPlayer && objectHitsPlayer(
                                    *state.asset, state.position,
                                    playerPosition)) {
            state.hitPlayer = true;
            events_.push_back(
                {LevelDropEventKind::HitPlayer, state.asset->objectId,
                 state.asset->roomId, state.position, {},
                 state.asset->damage});
        }
        if (playerPosition.z - state.position.z > 1000.0F) {
            state.phase = LevelDropPhase::Hidden;
            state.visible = false;
            state.physicsEnabled = false;
        }
    }
}

std::vector<LevelDropEvent> LevelDropRuntime::consumeEvents() {
    std::vector<LevelDropEvent> result;
    result.swap(events_);
    return result;
}

bool LevelDropRuntime::areaContainsPlayer(
    const LevelDropAreaAsset& area,
    const assets::Vector3& player) noexcept {
    const assets::Vector3 half{std::abs(area.sizes.x) * 0.5F,
                              std::abs(area.sizes.y) * 0.5F,
                              std::abs(area.sizes.z) * 0.5F};
    return player.x + kPlayerRadius >= area.position.x - half.x &&
           player.x - kPlayerRadius <= area.position.x + half.x &&
           player.y + kPlayerRadius >= area.position.y - half.y &&
           player.y - kPlayerRadius <= area.position.y + half.y &&
           player.z + kPlayerHeight >= area.position.z - half.z &&
           player.z <= area.position.z + half.z;
}

bool LevelDropRuntime::objectHitsPlayer(
    const LevelDropObjectAsset& object,
    const assets::Vector3& objectPosition,
    const assets::Vector3& player) noexcept {
    return objectPosition.x + object.halfExtents.x >=
               player.x - kPlayerRadius &&
           objectPosition.x - object.halfExtents.x <=
               player.x + kPlayerRadius &&
           objectPosition.y + object.halfExtents.y >=
               player.y - kPlayerRadius &&
           objectPosition.y - object.halfExtents.y <=
               player.y + kPlayerRadius &&
           objectPosition.z + object.halfExtents.z >= player.z &&
           objectPosition.z - object.halfExtents.z <=
               player.z + kPlayerHeight;
}

} // namespace usm::game
