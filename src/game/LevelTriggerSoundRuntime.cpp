#include "game/LevelTriggerSoundRuntime.hpp"

#include "game/PlayerPhysicsConstants.hpp"

#include <cmath>

namespace usm::game {
namespace {


assets::Vector3 triggerCenter(
    const LevelTriggerSoundAsset& trigger) noexcept {
    const auto& matrix = trigger.worldTransform;
    return matrix[15] != 0.0F
               ? assets::Vector3{matrix[12], matrix[13], matrix[14]}
               : trigger.position;
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

void LevelTriggerSoundRuntime::bind(
    std::span<const LevelTriggerSoundAsset> triggers) {
    states_.clear();
    states_.reserve(triggers.size());
    for (const LevelTriggerSoundAsset& trigger : triggers) {
        states_.push_back({&trigger, false});
    }
}

std::vector<TriggerSoundEvent> LevelTriggerSoundRuntime::update(
    const assets::Vector3& playerPosition) {
    std::vector<TriggerSoundEvent> events;
    for (State& state : states_) {
        if (state.asset == nullptr) {
            continue;
        }
        const bool inside = containsPlayer(*state.asset, playerPosition);
        if (inside == state.playing) {
            continue;
        }
        state.playing = inside;
        events.push_back(
            {state.asset->objectId, state.asset->eventName,
             inside ? TriggerSoundEventKind::Started
                    : TriggerSoundEventKind::Stopped});
    }
    return events;
}

std::vector<TriggerSoundEvent>
LevelTriggerSoundRuntime::checkPointLoadEvents() const {
    // CTriggerSound::Load (0x0036caf4) reads the saved state and routes it
    // through SetState (0x0036ca34), synchronizing the actual sound source.
    std::vector<TriggerSoundEvent> events;
    for (const State& state : states_) {
        if (state.asset != nullptr && state.playing) {
            events.push_back({state.asset->objectId, state.asset->eventName,
                              TriggerSoundEventKind::Started});
        }
    }
    return events;
}

bool LevelTriggerSoundRuntime::containsPlayer(
    const LevelTriggerSoundAsset& trigger,
    const assets::Vector3& playerPosition) noexcept {
    const assets::Vector3 center = triggerCenter(trigger);
    if (trigger.axisAlignedBox) {
        const assets::Vector3 half{trigger.sizes.x * 0.5F,
                                  trigger.sizes.y * 0.5F,
                                  trigger.sizes.z * 0.5F};
        return playerPosition.x + kPlayerCollisionRadiusCentimeters >=
                   center.x - half.x &&
               playerPosition.x - kPlayerCollisionRadiusCentimeters <=
                   center.x + half.x &&
               playerPosition.y + kPlayerCollisionRadiusCentimeters >=
                   center.y - half.y &&
               playerPosition.y - kPlayerCollisionRadiusCentimeters <=
                   center.y + half.y &&
               playerPosition.z + kPlayerCollisionHeightCentimeters >=
                   center.z - half.z &&
               playerPosition.z <= center.z + half.z;
    }

    assets::Vector3 relative{playerPosition.x - center.x,
                             playerPosition.y - center.y,
                             playerPosition.z +
                                 kPlayerCollisionHalfHeightCentimeters -
                                 center.z};
    relative = inverseRotate(relative, trigger.rotation);
    const assets::Vector3 half{
        std::abs(trigger.sizes.x * trigger.scale.x) * 0.5F +
            kPlayerCollisionRadiusCentimeters,
        std::abs(trigger.sizes.y * trigger.scale.y) * 0.5F +
            kPlayerCollisionRadiusCentimeters,
        std::abs(trigger.sizes.z * trigger.scale.z) * 0.5F +
            kPlayerCollisionHalfHeightCentimeters};
    return std::abs(relative.x) <= half.x &&
           std::abs(relative.y) <= half.y &&
           std::abs(relative.z) <= half.z;
}

} // namespace usm::game
