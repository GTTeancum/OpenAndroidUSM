#include "game/LevelTriggerRuntime.hpp"

#include <algorithm>
#include <cmath>

namespace usm::game {
namespace {

constexpr float kPlayerRadius = 50.0F;
constexpr float kPlayerHeight = 140.0F;

assets::Vector3 triggerCenter(const LevelTriggerAsset& trigger) noexcept {
    const auto& matrix = trigger.worldTransform;
    if (matrix[15] != 0.0F) {
        return {matrix[12], matrix[13], matrix[14]};
    }
    return trigger.position;
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
    rotation.w = rotation.w / lengthSquared;
    const assets::Vector3 quaternionVector{rotation.x, rotation.y, rotation.z};
    const assets::Vector3 firstCross{
        quaternionVector.y * value.z - quaternionVector.z * value.y,
        quaternionVector.z * value.x - quaternionVector.x * value.z,
        quaternionVector.x * value.y - quaternionVector.y * value.x};
    const assets::Vector3 secondCross{
        quaternionVector.y * firstCross.z -
            quaternionVector.z * firstCross.y,
        quaternionVector.z * firstCross.x -
            quaternionVector.x * firstCross.z,
        quaternionVector.x * firstCross.y -
            quaternionVector.y * firstCross.x};
    return {value.x + 2.0F *
                          (rotation.w * firstCross.x + secondCross.x),
            value.y + 2.0F *
                          (rotation.w * firstCross.y + secondCross.y),
            value.z + 2.0F *
                          (rotation.w * firstCross.z + secondCross.z)};
}

} // namespace

void LevelTriggerRuntime::bind(std::span<const LevelTriggerAsset> triggers) {
    states_.clear();
    states_.reserve(triggers.size());
    for (const LevelTriggerAsset& trigger : triggers) {
        states_.push_back({&trigger, trigger.enabled});
    }
}

std::vector<TriggerEvent> LevelTriggerRuntime::update(
    const assets::Vector3& playerPosition) {
    std::vector<TriggerEvent> events;
    for (State& state : states_) {
        if (!state.enabled || state.asset == nullptr) {
            continue;
        }
        bool transitionDispatched = false;
        const bool inside = containsPlayer(*state.asset, playerPosition);
        std::int32_t transitionCinematic = -1;
        TriggerEventKind transitionKind = TriggerEventKind::Entered;
        if (state.initialized && inside != state.inside) {
            if (inside) {
                transitionCinematic = state.asset->outToInCinematicId;
                transitionKind = TriggerEventKind::Entered;
            } else {
                transitionCinematic = state.asset->inToOutCinematicId;
                transitionKind = TriggerEventKind::Exited;
            }
            state.whileEventDispatched = false;
        }
        state.initialized = true;
        state.inside = inside;
        if (transitionCinematic >= 0) {
            events.push_back(
                {state.asset->objectId, transitionCinematic, transitionKind});
            transitionDispatched = true;
        }
        if (!state.whileEventDispatched) {
            const std::int32_t whileCinematic =
                inside ? state.asset->whileInsideCinematicId
                       : state.asset->whileOutsideCinematicId;
            if (whileCinematic >= 0) {
                events.push_back(
                    {state.asset->objectId, whileCinematic,
                     inside ? TriggerEventKind::WhileInside
                            : TriggerEventKind::WhileOutside});
            }
            state.whileEventDispatched = true;
        }
        if (state.asset->autoDisabled && transitionDispatched) {
            state.enabled = false;
        }
    }
    return events;
}

bool LevelTriggerRuntime::isEnabled(std::int32_t triggerId) const noexcept {
    const auto match = std::find_if(
        states_.begin(), states_.end(), [triggerId](const State& state) {
            return state.asset != nullptr && state.asset->objectId == triggerId;
        });
    return match != states_.end() && match->enabled;
}

bool LevelTriggerRuntime::setEnabled(std::int32_t triggerId,
                                     bool enabled) noexcept {
    const auto match = std::find_if(
        states_.begin(), states_.end(), [triggerId](const State& state) {
            return state.asset != nullptr && state.asset->objectId == triggerId;
        });
    if (match == states_.end()) {
        return false;
    }
    match->enabled = enabled;
    match->initialized = false;
    match->inside = false;
    match->whileEventDispatched = false;
    return true;
}

bool LevelTriggerRuntime::containsPlayer(
    const LevelTriggerAsset& trigger,
    const assets::Vector3& playerPosition) noexcept {
    const assets::Vector3 center = triggerCenter(trigger);
    if (!trigger.orientedBox) {
        const assets::Vector3 half{trigger.sizes.x * 0.5F,
                                  trigger.sizes.y * 0.5F,
                                  trigger.sizes.z * 0.5F};
        return playerPosition.x + kPlayerRadius >= center.x - half.x &&
               playerPosition.x - kPlayerRadius <= center.x + half.x &&
               playerPosition.y + kPlayerRadius >= center.y - half.y &&
               playerPosition.y - kPlayerRadius <= center.y + half.y &&
               playerPosition.z + kPlayerHeight >= center.z - half.z &&
               playerPosition.z <= center.z + half.z;
    }

    assets::Vector3 relative{playerPosition.x - center.x,
                             playerPosition.y - center.y,
                             playerPosition.z + kPlayerHeight * 0.5F -
                                 center.z};
    relative = inverseRotate(relative, trigger.rotation);
    const assets::Vector3 half{
        std::abs(trigger.sizes.x * trigger.scale.x) * 0.5F + kPlayerRadius,
        std::abs(trigger.sizes.y * trigger.scale.y) * 0.5F + kPlayerRadius,
        std::abs(trigger.sizes.z * trigger.scale.z) * 0.5F +
            kPlayerHeight * 0.5F};
    return std::abs(relative.x) <= half.x &&
           std::abs(relative.y) <= half.y &&
           std::abs(relative.z) <= half.z;
}

} // namespace usm::game
