#include "game/LevelDamageRuntime.hpp"

#include "game/PlayerPhysicsConstants.hpp"

#include <algorithm>
#include <cmath>

namespace usm::game {
namespace {

constexpr std::uint32_t kNativeReactionMilliseconds = 1000;

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

void LevelDamageRuntime::bind(
    std::span<const LevelDamageAsset> assets) noexcept {
    assets_ = assets;
    events_.clear();
    cooldownRemainingMilliseconds_ = 0;
}

void LevelDamageRuntime::update(
    const assets::Vector3& playerPosition,
    std::uint32_t elapsedMilliseconds) noexcept {
    cooldownRemainingMilliseconds_ =
        elapsedMilliseconds >= cooldownRemainingMilliseconds_
            ? 0
            : cooldownRemainingMilliseconds_ - elapsedMilliseconds;
    if (cooldownRemainingMilliseconds_ != 0) {
        return;
    }
    for (const LevelDamageAsset& asset : assets_) {
        if (!asset.enabled || !containsPlayer(asset, playerPosition)) {
            continue;
        }
        events_.push_back(
            {asset.objectId, asset.damageType, asset.damage});
        cooldownRemainingMilliseconds_ = kNativeReactionMilliseconds;
        break;
    }
}

std::vector<LevelDamageEvent> LevelDamageRuntime::consumeEvents() {
    std::vector<LevelDamageEvent> result;
    result.swap(events_);
    return result;
}

bool LevelDamageRuntime::containsPlayer(
    const LevelDamageAsset& asset,
    const assets::Vector3& player) noexcept {
    assets::Vector3 relative{player.x - asset.position.x,
                             player.y - asset.position.y,
                             player.z +
                                 kPlayerCollisionHalfHeightCentimeters -
                                 asset.position.z};
    relative = inverseRotate(relative, asset.rotation);
    const assets::Vector3 half{
        std::abs(asset.sizes.x * asset.scale.x) * 0.5F +
            kPlayerCollisionRadiusCentimeters,
        std::abs(asset.sizes.y * asset.scale.y) * 0.5F +
            kPlayerCollisionRadiusCentimeters,
        std::abs(asset.sizes.z * asset.scale.z) * 0.5F +
            kPlayerCollisionHalfHeightCentimeters};
    return std::abs(relative.x) <= half.x &&
           std::abs(relative.y) <= half.y &&
           std::abs(relative.z) <= half.z;
}

} // namespace usm::game
