#include "game/LevelObjectRuntime.hpp"

#include "game/LevelCollision.hpp"
#include "game/PlayerPhysicsConstants.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace usm::game {
namespace {

// Physics::processCollision ignored-mask argument used by
// Unit::IsBlockedByWorld's center-ray path (0x00324670). This is the same
// native query made for enemies and targeted destroyables by
// Player::SearchTargetByEyeHorizon (0x00343b70).
constexpr std::uint32_t kTargetOcclusionIgnoredPhysicsFlags = 0xffff7f18U;

bool parseInteger(std::string_view text, std::int32_t& value) noexcept {
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto parsed = std::from_chars(begin, end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

float parseFloat(std::string_view text, float fallback) noexcept {
    const std::string storage(text);
    char* end = nullptr;
    const float value = std::strtof(storage.c_str(), &end);
    return end == storage.c_str() ? fallback : value;
}

bool parseBoolean(std::string_view text, bool fallback) noexcept {
    if (text == "true" || text == "1") {
        return true;
    }
    if (text == "false" || text == "0") {
        return false;
    }
    return fallback;
}

assets::Vector3 parseVector3(std::string_view text,
                             assets::Vector3 fallback) noexcept {
    std::string storage(text);
    std::replace(storage.begin(), storage.end(), ',', ' ');
    const char* cursor = storage.c_str();
    char* end = nullptr;
    assets::Vector3 result;
    for (float* component : {&result.x, &result.y, &result.z}) {
        *component = std::strtof(cursor, &end);
        if (end == cursor) {
            return fallback;
        }
        cursor = end;
    }
    return result;
}

assets::Quaternion parseQuaternion(std::string_view text,
                                   assets::Quaternion fallback) noexcept {
    std::string storage(text);
    std::replace(storage.begin(), storage.end(), ',', ' ');
    const char* cursor = storage.c_str();
    char* end = nullptr;
    assets::Quaternion result;
    for (float* component : {&result.x, &result.y, &result.z, &result.w}) {
        *component = std::strtof(cursor, &end);
        if (end == cursor) {
            return fallback;
        }
        cursor = end;
    }
    return result;
}

assets::Quaternion slerp(assets::Quaternion start,
                         assets::Quaternion end, float factor) noexcept {
    float dot = start.x * end.x + start.y * end.y + start.z * end.z +
                start.w * end.w;
    if (dot < 0.0F) {
        end.x = -end.x;
        end.y = -end.y;
        end.z = -end.z;
        end.w = -end.w;
        dot = -dot;
    }
    dot = std::clamp(dot, -1.0F, 1.0F);
    float startWeight = 1.0F - factor;
    float endWeight = factor;
    if (dot < 0.9995F) {
        const float angle = std::acos(dot);
        const float inverseSine = 1.0F / std::sin(angle);
        startWeight = std::sin((1.0F - factor) * angle) * inverseSine;
        endWeight = std::sin(factor * angle) * inverseSine;
    }
    assets::Quaternion result{start.x * startWeight + end.x * endWeight,
                              start.y * startWeight + end.y * endWeight,
                              start.z * startWeight + end.z * endWeight,
                              start.w * startWeight + end.w * endWeight};
    const float length =
        std::sqrt(result.x * result.x + result.y * result.y +
                  result.z * result.z + result.w * result.w);
    if (length > std::numeric_limits<float>::epsilon()) {
        result.x /= length;
        result.y /= length;
        result.z /= length;
        result.w /= length;
    }
    return result;
}

assets::Quaternion multiply(assets::Quaternion left,
                            assets::Quaternion right) noexcept {
    return {left.w * right.x + left.x * right.w + left.y * right.z -
                left.z * right.y,
            left.w * right.y - left.x * right.z + left.y * right.w +
                left.z * right.x,
            left.w * right.z + left.x * right.y - left.y * right.x +
                left.z * right.w,
            left.w * right.w - left.x * right.x - left.y * right.y -
                left.z * right.z};
}

assets::Vector3 rotate(assets::Quaternion rotation,
                       assets::Vector3 value) noexcept {
    const float lengthSquared = rotation.x * rotation.x +
                                rotation.y * rotation.y +
                                rotation.z * rotation.z +
                                rotation.w * rotation.w;
    if (lengthSquared <= std::numeric_limits<float>::epsilon()) {
        return value;
    }
    const assets::Quaternion inverse{-rotation.x / lengthSquared,
                                     -rotation.y / lengthSquared,
                                     -rotation.z / lengthSquared,
                                     rotation.w / lengthSquared};
    const auto rotated =
        multiply(multiply(rotation, {value.x, value.y, value.z, 0.0F}),
                 inverse);
    return {rotated.x, rotated.y, rotated.z};
}

assets::Quaternion objectRotation(const LevelObjectState& object) noexcept {
    if (object.asset == nullptr) {
        return {};
    }
    if (object.asset->kind == LevelObjectKind::BrokenBridge) {
        return object.bridgeRotation;
    }
    if (object.asset->kind == LevelObjectKind::SlideCar) {
        return object.slideCarRotation;
    }
    if (object.asset->kind == LevelObjectKind::Train) {
        return object.trainRotation;
    }
    return object.asset->rotation;
}

bool areaDamageContainsPlayer(const LevelObjectState& object,
                              const assets::Vector3& player) noexcept {
    if (object.asset == nullptr || !object.asset->hasCollisionBounds) {
        return false;
    }
    assets::Vector3 relative{player.x - object.position.x,
                             player.y - object.position.y,
                             player.z +
                                 kPlayerCollisionHalfHeightCentimeters -
                                 object.position.z};
    const auto rotation = object.asset->rotation;
    relative = rotate({-rotation.x, -rotation.y, -rotation.z, rotation.w},
                      relative);
    const auto scale = object.asset->scale;
    if (std::abs(scale.x) > 1.0e-6F) relative.x /= scale.x;
    if (std::abs(scale.y) > 1.0e-6F) relative.y /= scale.y;
    if (std::abs(scale.z) > 1.0e-6F) relative.z /= scale.z;
    const auto minimum = object.asset->collisionLocalMinimum;
    const auto maximum = object.asset->collisionLocalMaximum;
    return relative.x >= minimum.x - kPlayerCollisionRadiusCentimeters &&
           relative.x <= maximum.x + kPlayerCollisionRadiusCentimeters &&
           relative.y >= minimum.y - kPlayerCollisionRadiusCentimeters &&
           relative.y <= maximum.y + kPlayerCollisionRadiusCentimeters &&
           relative.z >=
               minimum.z - kPlayerCollisionHalfHeightCentimeters &&
           relative.z <=
               maximum.z + kPlayerCollisionHalfHeightCentimeters;
}

std::array<float, 16> worldMatrix(const assets::Vector3& position,
                                  assets::Quaternion rotation,
                                  const assets::Vector3& scale) noexcept {
    const float length =
        std::sqrt(rotation.x * rotation.x + rotation.y * rotation.y +
                  rotation.z * rotation.z + rotation.w * rotation.w);
    if (length > std::numeric_limits<float>::epsilon()) {
        rotation.x /= length;
        rotation.y /= length;
        rotation.z /= length;
        rotation.w /= length;
    }
    const float xx = rotation.x * rotation.x;
    const float yy = rotation.y * rotation.y;
    const float zz = rotation.z * rotation.z;
    const float xy = rotation.x * rotation.y;
    const float xz = rotation.x * rotation.z;
    const float yz = rotation.y * rotation.z;
    const float wx = rotation.w * rotation.x;
    const float wy = rotation.w * rotation.y;
    const float wz = rotation.w * rotation.z;
    return {(1.0F - 2.0F * (yy + zz)) * scale.x,
            (2.0F * (xy - wz)) * scale.x,
            (2.0F * (xz + wy)) * scale.x,
            0.0F,
            (2.0F * (xy + wz)) * scale.y,
            (1.0F - 2.0F * (xx + zz)) * scale.y,
            (2.0F * (yz - wx)) * scale.y,
            0.0F,
            (2.0F * (xz - wy)) * scale.z,
            (2.0F * (yz + wx)) * scale.z,
            (1.0F - 2.0F * (xx + yy)) * scale.z,
            0.0F,
            position.x,
            position.y,
            position.z,
            1.0F};
}

assets::Quaternion trainRotationForDirection(
    assets::Vector3 direction) noexcept {
    const float directionLength = std::hypot(
        direction.x, direction.y, direction.z);
    if (directionLength <= std::numeric_limits<float>::epsilon()) {
        return {};
    }
    direction.x /= directionLength;
    direction.y /= directionLength;
    direction.z /= directionLength;

    // CTrain::SetDirection (0x00321e94) builds its absolute rotation from
    // the model's native (-1,0,0) forward vector. Its two rotationFromTo
    // operations split horizontal yaw and vertical pitch; the normalized
    // shortest-arc quaternion below is the equivalent combined rotation.
    const float dot = -direction.x;
    assets::Quaternion rotation{0.0F, direction.z, -direction.y,
                                1.0F + dot};
    float length = std::sqrt(rotation.x * rotation.x +
                             rotation.y * rotation.y +
                             rotation.z * rotation.z +
                             rotation.w * rotation.w);
    if (length <= std::numeric_limits<float>::epsilon()) {
        // The only ambiguous shortest arc is -X to +X. Native
        // rotationFromTo also chooses an orthogonal 180-degree axis.
        rotation = {0.0F, 0.0F, 1.0F, 0.0F};
        return rotation;
    }
    rotation.x /= length;
    rotation.y /= length;
    rotation.z /= length;
    rotation.w /= length;
    return rotation;
}

std::int32_t commandObjectId(const CinematicThread& thread,
                             const CinematicCommand& command) noexcept {
    std::int32_t objectId = thread.objectId;
    const CinematicAttribute* explicitObject =
        command.findAttribute("ObjectID");
    if (explicitObject != nullptr) {
        std::int32_t parsed = -1;
        if (parseInteger(explicitObject->value, parsed) && parsed >= 0) {
            objectId = parsed;
        }
    }
    return objectId;
}

const CinematicCommand* nextMoveObjectCommand(
    const CinematicThread& thread, const CinematicCommand& command) noexcept {
    bool currentFound = false;
    for (const CinematicCommand& candidate : thread.commands) {
        if (&candidate == &command) {
            currentFound = true;
            continue;
        }
        if (currentFound && candidate.name == "MoveObject") {
            return &candidate;
        }
    }
    return nullptr;
}

void advanceCinematicMotion(LevelObjectState& object,
                            std::uint32_t elapsedMilliseconds) noexcept {
    ObjectCinematicMotionState& motion = object.cinematicMotion;
    if (!motion.active || motion.durationMilliseconds == 0 ||
        object.asset == nullptr) {
        return;
    }
    // CCinematicThread::DoExecChange (0x00371880) evaluates the current clock
    // before adding this frame's delta.
    const float factor = std::clamp(
        static_cast<float>(motion.elapsedMilliseconds) /
            static_cast<float>(motion.durationMilliseconds),
        0.0F, 1.0F);
    object.position = {
        motion.startPosition.x +
            (motion.endPosition.x - motion.startPosition.x) * factor,
        motion.startPosition.y +
            (motion.endPosition.y - motion.startPosition.y) * factor,
        motion.startPosition.z +
            (motion.endPosition.z - motion.startPosition.z) * factor};
    object.worldTransform =
        worldMatrix(object.position,
                    slerp(motion.startRotation, motion.endRotation, factor),
                    object.asset->scale);
    const std::uint64_t next =
        static_cast<std::uint64_t>(motion.elapsedMilliseconds) +
        elapsedMilliseconds;
    motion.elapsedMilliseconds = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(next, motion.durationMilliseconds));
    if (next >= motion.durationMilliseconds) {
        motion.active = false;
    }
}

std::string_view electricAnimation(ElectricPlatformState state) noexcept {
    // CElectricBoard::k_anim_clip_list at 0x0056e958 contains these three
    // pointers in CElectriferous state order: release, off, warning.
    switch (state) {
    case ElectricPlatformState::Release:
        return "release";
    case ElectricPlatformState::Off:
        return "off";
    case ElectricPlatformState::Warning:
        return "warning";
    }
    return "off";
}

bool changeElectricState(LevelObjectState& object,
                         const LevelObjectAsset& asset,
                         ElectricPlatformState requestedState) {
    // CElectriferous::ChangeState (0x0030d8a8) skips unavailable states. A
    // zero-duration warning redirects to release; a zero-duration release or
    // off state leaves the current state unchanged.
    ElectricPlatformState state = requestedState;
    if (state == ElectricPlatformState::Off &&
        asset.electricOffDurationMilliseconds == 0.0F) {
        return false;
    }
    if (state == ElectricPlatformState::Warning &&
        asset.electricReadyDurationMilliseconds == 0.0F) {
        state = ElectricPlatformState::Release;
    }
    if (state == ElectricPlatformState::Release &&
        asset.electricOnDurationMilliseconds == 0.0F) {
        return false;
    }
    object.electricState = state;
    object.electricStateElapsedMilliseconds = 0.0F;
    object.activeAnimation = electricAnimation(state);
    object.animationTimeMilliseconds = 0;
    object.animationSpeed = 1.0F;
    object.animationLoops = true;
    return true;
}

const LevelWayPointAsset* findWaypoint(const LevelOneBootstrap& level,
                                       std::int32_t objectId) noexcept {
    const auto match = std::find_if(
        level.waypoints().begin(), level.waypoints().end(),
        [objectId](const LevelWayPointAsset& waypoint) {
            return waypoint.objectId == objectId;
        });
    return match == level.waypoints().end() ? nullptr : &*match;
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
    const assets::Vector3 second{
        q.y * first.z - q.z * first.y,
        q.z * first.x - q.x * first.z,
        q.x * first.y - q.y * first.x};
    return {value.x + 2.0F * (rotation.w * first.x + second.x),
            value.y + 2.0F * (rotation.w * first.y + second.y),
            value.z + 2.0F * (rotation.w * first.z + second.z)};
}

bool electricPlatformContainsPlayer(
    const LevelObjectState& object,
    const assets::Vector3& playerPosition) noexcept {
    if (object.asset == nullptr || !object.asset->hasCollisionBounds) {
        return false;
    }
    const LevelObjectAsset& asset = *object.asset;
    assets::Vector3 local{
        playerPosition.x - object.position.x,
        playerPosition.y - object.position.y,
        playerPosition.z + kPlayerCollisionHalfHeightCentimeters -
            object.position.z};
    local = inverseRotate(local, asset.rotation);
    const auto localRadius = [](float worldSize, float scale) {
        return std::abs(scale) > 1e-6F
                   ? worldSize / std::abs(scale)
                   : std::numeric_limits<float>::max();
    };
    const assets::Vector3 expansion{
        localRadius(kPlayerCollisionRadiusCentimeters, asset.scale.x),
        localRadius(kPlayerCollisionRadiusCentimeters, asset.scale.y),
        localRadius(kPlayerCollisionHalfHeightCentimeters, asset.scale.z)};
    if (std::abs(asset.scale.x) > 1e-6F) {
        local.x /= asset.scale.x;
    }
    if (std::abs(asset.scale.y) > 1e-6F) {
        local.y /= asset.scale.y;
    }
    if (std::abs(asset.scale.z) > 1e-6F) {
        local.z /= asset.scale.z;
    }
    return local.x >= asset.collisionLocalMinimum.x - expansion.x &&
           local.x <= asset.collisionLocalMaximum.x + expansion.x &&
           local.y >= asset.collisionLocalMinimum.y - expansion.y &&
           local.y <= asset.collisionLocalMaximum.y + expansion.y &&
           local.z >= asset.collisionLocalMinimum.z - expansion.z &&
           local.z <= asset.collisionLocalMaximum.z + expansion.z;
}

void updatePlatform(LevelObjectState& object,
                    const LevelOneBootstrap& level,
                    std::uint32_t elapsedMilliseconds) noexcept {
    if (object.asset == nullptr ||
        (object.asset->kind != LevelObjectKind::Platform &&
         object.asset->kind != LevelObjectKind::ElectricPlatform)) {
        return;
    }
    const LevelObjectAsset& asset = *object.asset;
    if (asset.kind == LevelObjectKind::ElectricPlatform) {
        object.electricStateElapsedMilliseconds +=
            static_cast<float>(elapsedMilliseconds);
        // CElectriferous::Update (0x0030d990) performs at most one state
        // change per update and carries its overshoot into the next tick.
        switch (object.electricState) {
        case ElectricPlatformState::Off:
            if (object.electricStateElapsedMilliseconds >=
                asset.electricOffDurationMilliseconds) {
                if (object.electricSwitchActive) {
                    const float carried =
                        object.electricStateElapsedMilliseconds -
                        asset.electricOffDurationMilliseconds;
                    (void)changeElectricState(
                        object, asset, ElectricPlatformState::Warning);
                    object.electricStateElapsedMilliseconds = carried;
                } else {
                    object.electricStateElapsedMilliseconds = 0.0F;
                }
            }
            break;
        case ElectricPlatformState::Warning:
            if (object.electricStateElapsedMilliseconds >=
                asset.electricReadyDurationMilliseconds) {
                const float carried =
                    object.electricStateElapsedMilliseconds -
                    asset.electricReadyDurationMilliseconds;
                object.electricStateElapsedMilliseconds = 0.0F;
                (void)changeElectricState(
                    object, asset, ElectricPlatformState::Release);
                object.electricStateElapsedMilliseconds = carried;
            }
            break;
        case ElectricPlatformState::Release:
            if (object.electricStateElapsedMilliseconds >=
                asset.electricOnDurationMilliseconds) {
                if (asset.electricOffDurationMilliseconds <= 0.0F) {
                    object.electricStateElapsedMilliseconds = 0.0F;
                } else {
                    const float carried =
                        object.electricStateElapsedMilliseconds -
                        asset.electricOnDurationMilliseconds;
                    (void)changeElectricState(
                        object, asset, ElectricPlatformState::Off);
                    object.electricStateElapsedMilliseconds = carried;
                }
            }
            break;
        }
    }

    float remainingMilliseconds = static_cast<float>(elapsedMilliseconds);
    for (std::uint32_t pass = 0;
         pass < 4 && remainingMilliseconds > 0.0F; ++pass) {
        if (object.platformMotionState == PlatformMotionState::Park) {
            if (!object.platformMotionActive) {
                break;
            }
            const float parkRemaining = std::max(
                0.0F, asset.platformParkDurationMilliseconds -
                          object.platformMotionElapsedMilliseconds);
            const float consumed =
                std::min(remainingMilliseconds, parkRemaining);
            object.platformMotionElapsedMilliseconds += consumed;
            remainingMilliseconds -= consumed;
            if (object.platformMotionElapsedMilliseconds + 1e-4F <
                asset.platformParkDurationMilliseconds) {
                break;
            }
            object.platformMotionState = PlatformMotionState::Move;
            object.platformMotionElapsedMilliseconds = 0.0F;
            continue;
        }
        if (object.platformMotionState == PlatformMotionState::Brake) {
            break;
        }
        if (!object.platformMotionActive) {
            object.platformMotionState = PlatformMotionState::Brake;
            break;
        }
        const LevelWayPointAsset* target =
            findWaypoint(level, object.platformTargetWaypointId);
        if (target == nullptr) {
            break;
        }
        const assets::Vector3 delta{target->position.x - object.position.x,
                                    target->position.y - object.position.y,
                                    target->position.z - object.position.z};
        const float distance = std::sqrt(delta.x * delta.x +
                                         delta.y * delta.y +
                                         delta.z * delta.z);
        float timeToTarget = 0.0F;
        if (target->timeToMe > 0.0F) {
            timeToTarget = std::max(
                0.0F,
                target->timeToMe - object.platformMotionElapsedMilliseconds);
        } else if (asset.platformLineSpeedCentimetersPerMillisecond > 0.0F) {
            timeToTarget = distance /
                           asset.platformLineSpeedCentimetersPerMillisecond;
        }
        if (distance <= 1e-4F || timeToTarget <= 1e-4F) {
            object.position = target->position;
            object.worldTransform[12] = object.position.x;
            object.worldTransform[13] = object.position.y;
            object.worldTransform[14] = object.position.z;
            object.platformTargetWaypointId = target->nextWaypointIds[0];
            object.platformMotionState = PlatformMotionState::Park;
            object.platformMotionElapsedMilliseconds = 0.0F;
            continue;
        }
        const float consumed = std::min(remainingMilliseconds, timeToTarget);
        const float fraction = std::clamp(consumed / timeToTarget, 0.0F, 1.0F);
        object.position.x += delta.x * fraction;
        object.position.y += delta.y * fraction;
        object.position.z += delta.z * fraction;
        object.worldTransform[12] = object.position.x;
        object.worldTransform[13] = object.position.y;
        object.worldTransform[14] = object.position.z;
        object.platformMotionElapsedMilliseconds += consumed;
        remainingMilliseconds -= consumed;
        if (consumed + 1e-4F < timeToTarget) {
            break;
        }
        object.position = target->position;
        object.worldTransform[12] = object.position.x;
        object.worldTransform[13] = object.position.y;
        object.worldTransform[14] = object.position.z;
        object.platformTargetWaypointId = target->nextWaypointIds[0];
        object.platformMotionState = PlatformMotionState::Park;
        object.platformMotionElapsedMilliseconds = 0.0F;
    }
}

void updateTrain(LevelObjectState& object, const LevelOneBootstrap& level,
                 std::uint32_t elapsedMilliseconds) noexcept {
    object.trainVelocityCentimetersPerSecond = {};
    if (object.asset == nullptr ||
        object.asset->kind != LevelObjectKind::Train ||
        !object.trainActive ||
        object.trainCurrentSpeedCentimetersPerMillisecond <= 0.0F ||
        object.trainTargetWaypointId < 0 || elapsedMilliseconds == 0) {
        return;
    }

    float remainingMilliseconds = static_cast<float>(elapsedMilliseconds);
    for (std::uint32_t pass = 0;
         pass < 8 && remainingMilliseconds > 0.0F; ++pass) {
        const LevelWayPointAsset* target =
            findWaypoint(level, object.trainTargetWaypointId);
        if (target == nullptr) {
            object.trainTargetWaypointId = -1;
            return;
        }
        const assets::Vector3 delta{target->position.x - object.position.x,
                                    target->position.y - object.position.y,
                                    target->position.z - object.position.z};
        const float distance = std::sqrt(delta.x * delta.x +
                                         delta.y * delta.y +
                                         delta.z * delta.z);
        if (distance <= 1.0e-4F) {
            object.position = target->position;
            object.worldTransform[12] = object.position.x;
            object.worldTransform[13] = object.position.y;
            object.worldTransform[14] = object.position.z;
            // CWayPointMover::NextDestination (0x0032699c) follows the
            // first native waypoint link and copies its position.
            object.trainTargetWaypointId = target->nextWaypointIds[0];
            continue;
        }

        const assets::Vector3 direction{delta.x / distance,
                                        delta.y / distance,
                                        delta.z / distance};
        object.trainDirection = direction;
        object.trainRotation = trainRotationForDirection(direction);
        object.worldTransform = worldMatrix(
            object.position, object.trainRotation, object.asset->scale);
        const float timeToTarget =
            distance /
            object.trainCurrentSpeedCentimetersPerMillisecond;
        const float consumed = std::min(remainingMilliseconds, timeToTarget);
        const float distanceMoved =
            object.trainCurrentSpeedCentimetersPerMillisecond * consumed;
        object.position.x += direction.x * distanceMoved;
        object.position.y += direction.y * distanceMoved;
        object.position.z += direction.z * distanceMoved;
        object.worldTransform[12] = object.position.x;
        object.worldTransform[13] = object.position.y;
        object.worldTransform[14] = object.position.z;
        object.trainVelocityCentimetersPerSecond = {
            direction.x *
                object.trainCurrentSpeedCentimetersPerMillisecond * 1000.0F,
            direction.y *
                object.trainCurrentSpeedCentimetersPerMillisecond * 1000.0F,
            direction.z *
                object.trainCurrentSpeedCentimetersPerMillisecond * 1000.0F};
        remainingMilliseconds -= consumed;
        if (consumed + 1.0e-4F < timeToTarget) {
            return;
        }
        object.position = target->position;
        object.worldTransform[12] = object.position.x;
        object.worldTransform[13] = object.position.y;
        object.worldTransform[14] = object.position.z;
        object.trainTargetWaypointId = target->nextWaypointIds[0];
    }
}

} // namespace

Result LevelObjectRuntime::initialize(const LevelOneBootstrap& level) {
    level_ = &level;
    states_.clear();
    events_.clear();
    electricDamageEvents_.clear();
    areaDamageEvents_.clear();
    electricContactCooldownMilliseconds_ = 0;
    states_.reserve(level.objects().size());
    for (const LevelObjectAsset& object : level.objects()) {
        if (object.archetypeIndex >= level.objectArchetypes().size()) {
            states_.clear();
            return Result::failure("Level object archetype index is invalid");
        }
        LevelObjectState state;
        state.asset = &object;
        state.position = object.position;
        state.worldTransform = object.worldTransform;
        state.activeAnimation = object.initialAnimation;
        state.animationSpeed = 1.0F;
        state.animationLoops = object.initialAnimationLoops;
        state.visible = object.visible;
        state.collisionEnabled = object.hasCollision;
        state.health = object.health;
        if (object.kind == LevelObjectKind::SlideCar ||
            object.kind == LevelObjectKind::BrokenBridge) {
            state.physicsEnabled = object.hasCollisionBounds;
            state.collisionEnabled = object.hasCollisionBounds;
        }
        if (object.kind == LevelObjectKind::BrokenBridge) {
            state.bridgeState = 1;
            state.bridgeRotation = object.rotation;
        }
        if (object.kind == LevelObjectKind::SlideCar) {
            state.slideCarInitialPosition = object.position;
            state.slideCarInitialRotation = object.rotation;
            state.slideCarRotation = object.rotation;
        }
        if (object.kind == LevelObjectKind::AreaDamage) {
            state.areaDamageState = -1;
            state.areaDamageStateMilliseconds = 0.0F;
            state.areaDamageWaitMilliseconds =
                object.areaDamageRandomLowMilliseconds;
            state.physicsEnabled = !object.areaDamageIgnorePhysics ||
                                   !object.areaDamageAutomaticDetection;
            state.collisionEnabled = state.physicsEnabled;
        }
        if (object.kind == LevelObjectKind::Platform ||
            object.kind == LevelObjectKind::ElectricPlatform) {
            state.physicsEnabled = object.hasCollisionBounds;
            state.collisionEnabled = object.hasCollisionBounds;
            state.platformMotionActive = object.platformInitiallyActive;
            state.platformMotionState = PlatformMotionState::Park;
            state.platformTargetWaypointId =
                object.platformLinkedWaypointId;
        }
        if (object.kind == LevelObjectKind::ElectricPlatform) {
            // CElectricPlatForm::Init (0x0030d168) forces a false authored
            // switch on before CElectriferous::Init applies the initial state.
            state.electricSwitchActive = true;
            state.electricState = static_cast<ElectricPlatformState>(
                object.electricInitialState);
            state.electricStateElapsedMilliseconds =
                object.electricDelayMilliseconds;
            state.activeAnimation = electricAnimation(state.electricState);
            state.animationLoops = true;
        }
        if (object.kind == LevelObjectKind::Train) {
            state.trainActive = object.trainInitiallyActive;
            state.trainCurrentSpeedCentimetersPerMillisecond =
                state.trainActive
                    ? object.trainLineSpeedCentimetersPerMillisecond
                    : 0.0F;
            state.trainTargetWaypointId = object.trainLinkedWaypointId;
            state.trainPreviousObjectId = object.trainPreviousObjectId;
            state.trainNextObjectId = object.trainNextObjectId;
            state.trainRotation = object.rotation;
        }
        states_.push_back(std::move(state));
    }

    // CBrokenBridge::GetSlideCarList (0x00301da0) performs this exact
    // one-time, room-local axis-aligned footprint test. Linked vehicles are
    // placed on the bridge's top plane and retain that original rectangle
    // for CSlideCar::CheckOnBridge (0x0031bdb0).
    for (LevelObjectState& bridge : states_) {
        if (bridge.asset == nullptr ||
            bridge.asset->kind != LevelObjectKind::BrokenBridge) {
            continue;
        }
        const float halfWidth = std::abs(
            bridge.asset->collisionLocalMaximum.x -
            bridge.asset->collisionLocalMinimum.x) * 0.5F;
        const float halfDepth = std::abs(
            bridge.asset->collisionLocalMaximum.y -
            bridge.asset->collisionLocalMinimum.y) * 0.5F;
        const float halfHeight = std::abs(
            bridge.asset->collisionLocalMaximum.z -
            bridge.asset->collisionLocalMinimum.z) * 0.5F;
        const assets::Vector3 minimum{bridge.position.x - halfWidth,
                                      bridge.position.y - halfDepth, 0.0F};
        const assets::Vector3 maximum{bridge.position.x + halfWidth,
                                      bridge.position.y + halfDepth, 0.0F};
        for (LevelObjectState& car : states_) {
            if (car.asset == nullptr ||
                car.asset->kind != LevelObjectKind::SlideCar ||
                car.asset->roomId != bridge.asset->roomId ||
                car.slideCarBridgeObjectId >= 0 ||
                car.position.x < minimum.x || car.position.x > maximum.x ||
                car.position.y < minimum.y || car.position.y > maximum.y) {
                continue;
            }
            car.slideCarBridgeObjectId = bridge.asset->objectId;
            car.slideCarBridgeMinimum = minimum;
            car.slideCarBridgeMaximum = maximum;
            car.position.z = bridge.position.z + halfHeight;
            car.worldTransform = worldMatrix(
                car.position, car.slideCarRotation, car.asset->scale);
        }
    }
    return Result::success();
}

void LevelObjectRuntime::advanceAnimations(
    std::uint32_t elapsedMilliseconds) noexcept {
    for (LevelObjectState& object : states_) {
        advanceCinematicMotion(object, elapsedMilliseconds);
        if (level_ != nullptr) {
            updatePlatform(object, *level_, elapsedMilliseconds);
            updateTrain(object, *level_, elapsedMilliseconds);
        }
        const assets::ColladaAnimationClip* clip = nullptr;
        if (level_ != nullptr && object.asset != nullptr &&
            object.asset->archetypeIndex <
                level_->objectArchetypes().size() &&
            !object.activeAnimation.empty()) {
            clip = level_->objectArchetypes()[object.asset->archetypeIndex]
                       .animationBank.findClip(object.activeAnimation);
        }
        const double advanced =
            static_cast<double>(elapsedMilliseconds) * object.animationSpeed;
        const std::uint32_t step = static_cast<std::uint32_t>(
            std::clamp(advanced, 0.0,
                       static_cast<double>(
                           std::numeric_limits<std::uint32_t>::max())));
        const std::uint64_t next = object.animationTimeMilliseconds +
                                   static_cast<std::uint64_t>(step);
        object.animationTimeMilliseconds = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(next,
                                    std::numeric_limits<std::uint32_t>::max()));
        if (clip != nullptr && !object.animationLoops &&
            object.animationTimeMilliseconds >= clip->durationMilliseconds()) {
            object.animationTimeMilliseconds = clip->durationMilliseconds();
            if (object.destructionPhase ==
                LevelObjectDestructionPhase::Breaking) {
                finishDestruction(object);
            }
        }
    }
}

void LevelObjectRuntime::updateBrokenBridges(
    const assets::Vector3& playerPosition,
    std::uint32_t elapsedMilliseconds) noexcept {
    if (elapsedMilliseconds == 0) {
        return;
    }
    const float seconds = elapsedMilliseconds * 0.001F;
    for (auto& object : states_) {
        if (!object.asset || object.asset->kind != LevelObjectKind::BrokenBridge ||
            object.bridgeState == 0 || object.bridgeState == 11) {
            continue;
        }
        const auto& asset = *object.asset;
        const float width = asset.collisionLocalMaximum.x - asset.collisionLocalMinimum.x;
        const float depth = asset.collisionLocalMaximum.y - asset.collisionLocalMinimum.y;
        const float height = asset.collisionLocalMaximum.z - asset.collisionLocalMinimum.z;
        const auto enter = [&](std::int32_t state) {
            object.bridgeState = state;
            LevelObjectEvent stateEvent;
            stateEvent.kind = LevelObjectEventKind::BridgeStateChanged;
            stateEvent.objectId = asset.objectId;
            stateEvent.roomId = asset.roomId;
            stateEvent.position = object.position;
            stateEvent.bridgeState = state;
            events_.push_back(std::move(stateEvent));
            switch (state) {
            case 2:
                object.activeAnimation = "shake";
                object.animationTimeMilliseconds = 0;
                object.bridgeStateSeconds = asset.bridgeIdleShakeSeconds - 0.6F;
                break;
            case 3: case 6: case 9:
                object.bridgeStateSeconds = 0.6F;
                break;
            case 4: case 7: {
                const bool second = state == 7;
                const float angle = (second ? asset.bridgeSecondAngleDegrees :
                                              asset.bridgeDropAngleDegrees) * 0.017453292F;
                const float duration = second ? asset.bridgeSecondDropSeconds : asset.bridgeDropSeconds;
                const float distance = second ? asset.bridgeSecondDropDistance : asset.bridgeDropDistance;
                object.bridgeStateSeconds = duration;
                // PhysicsEntity::init (0x003d73b4) initializes its direction
                // to +X. SetState forms the first rotation axis {-y,x,0}.
                object.bridgeAngularVelocity = {0.0F,
                    angle / duration * (second && asset.bridgeType == 3 ? -1.0F : 1.0F), 0.0F};
                object.bridgeVelocity = {0.0F, 0.0F,
                    -(distance + width * std::sin(angle) * 0.5F) / duration};
                break;
            }
            case 5: case 8:
                object.bridgeVelocity = {};
                object.bridgeAngularVelocity = {};
                object.bridgeStateSeconds = (state == 5 ? asset.bridgeDropShakeSeconds :
                                                          asset.bridgeSecondShakeSeconds) - 0.6F;
                if (state == 5) {
                    assets::Vector3 run = rotate(
                        object.bridgeRotation,
                        {asset.bridgeCarRunSpeed, 0.0F, 0.0F});
                    if (asset.bridgeType == 3) {
                        run.x = -run.x;
                        run.y = -run.y;
                        run.z = -run.z;
                    }
                    for (LevelObjectState& car : states_) {
                        if (car.slideCarBridgeObjectId != asset.objectId) {
                            continue;
                        }
                        car.slideCarVelocity = run;
                        car.slideCarState = 2;
                    }
                }
                break;
            case 10:
                object.bridgeStateSeconds = 1.0F;
                object.bridgeVelocity = {0.0F, 0.0F, -4000.0F};
                break;
            case 11:
                object.visible = object.physicsEnabled = object.collisionEnabled = false;
                break;
            default: break;
            }
            if (state == 4 || state == 7 || state == 10) {
                events_.push_back({LevelObjectEventKind::BridgeDrop, asset.objectId,
                    asset.roomId, object.position, 0x86});
            }
            if (state == 4 || state == 10) {
                // CBrokenBridge::ShowEffect (0x00300a68): five smoke and
                // four rock splashes along the authored forward edge.
                const assets::Vector3 edge{object.position.x + width * 0.5F,
                    object.position.y, object.position.z + height *
                        (asset.bridgeType == 3 ? -0.5F : 0.5F)};
                for (const float offset : {0.0F, 1.0F, 2.0F, -1.0F, -2.0F}) {
                    events_.push_back({LevelObjectEventKind::BridgeDrop, asset.objectId,
                        asset.roomId, {edge.x, edge.y + offset * width / 10.0F, edge.z},
                        -1, -1, "smoke_splash"});
                }
                for (const float offset : {0.5F, 1.5F, -0.5F, -1.5F}) {
                    events_.push_back({LevelObjectEventKind::BridgeDrop, asset.objectId,
                        asset.roomId, {edge.x, edge.y + offset * width / 10.0F, edge.z},
                        -1, -1, "rock_splash"});
                }
            }
        };
        if (object.bridgeState == 1) {
            if (asset.bridgeType == 2) {
                // Native type 2 activates by XY distance, even in the air.
                if (std::abs(playerPosition.x - object.position.x) <
                        asset.bridgeActivationDistance + width * 0.5F &&
                    std::abs(playerPosition.y - object.position.y) <
                        asset.bridgeActivationDistance + depth * 0.5F) {
                    enter(10);
                }
            } else if (electricPlatformContainsPlayer(object, playerPosition)) {
                // Unit::GetPhysicsContextFlags(0x100): player contact.
                enter(2);
            } else {
                // Native activation also tests the 0x100 player-contact flag
                // on every linked CSlideCar and CAreaDamage.
                for (const LevelObjectState& car : states_) {
                    if (car.slideCarBridgeObjectId == asset.objectId &&
                        electricPlatformContainsPlayer(car, playerPosition)) {
                        enter(2);
                        break;
                    }
                }
            }
        } else {
            object.bridgeStateSeconds -= seconds;
            if (object.bridgeStateSeconds <= 0.0F) {
                const auto next = object.bridgeState == 6 && asset.bridgeType != 1 ?
                    10 : std::min(object.bridgeState + 1, 11);
                enter(next);
            }
        }
        if (!object.visible) {
            continue;
        }
        // PhysicsEntity::preUpdate (0x003d799c) and final-fall gravity.
        if (object.bridgeState == 10) {
            object.bridgeVelocity.z -= 1000.0F * seconds;
        }
        object.position.z += object.bridgeVelocity.z * seconds;
        const auto omega = object.bridgeAngularVelocity;
        if (omega.x != 0.0F || omega.y != 0.0F || omega.z != 0.0F) {
            const auto q = object.bridgeRotation;
            const float factor = seconds * 0.5F;
            // The native angular quaternion explicitly has W=1.
            object.bridgeRotation = {
                q.x + factor * (q.w * omega.x + q.x + q.y * omega.z - q.z * omega.y),
                q.y + factor * (q.w * omega.y - q.x * omega.z + q.y + q.z * omega.x),
                q.z + factor * (q.w * omega.z + q.x * omega.y - q.y * omega.x + q.z),
                q.w + factor * (q.w - q.x * omega.x - q.y * omega.y - q.z * omega.z)};
            object.bridgeRotation = slerp(object.bridgeRotation, object.bridgeRotation, 0.0F);
        }
        object.worldTransform = worldMatrix(object.position, object.bridgeRotation, asset.scale);

        if (object.bridgeState == 4) {
            const float halfHeight = std::abs(
                asset.collisionLocalMaximum.z -
                asset.collisionLocalMinimum.z) * 0.5F;
            const assets::Vector3 positiveX =
                rotate(object.bridgeRotation, {1.0F, 0.0F, 0.0F});
            for (LevelObjectState& car : states_) {
                if (car.slideCarBridgeObjectId != asset.objectId ||
                    car.slideCarState >= 3 || car.asset == nullptr) {
                    continue;
                }
                car.position.z = object.position.z + halfHeight -
                    (object.position.x - car.position.x) * positiveX.z;
                car.slideCarRotation = multiply(object.bridgeRotation,
                                                car.slideCarInitialRotation);
                car.worldTransform = worldMatrix(
                    car.position, car.slideCarRotation, car.asset->scale);
            }
        }
    }

    // CSlideCar::Update (0x0031be48) is driven independently after the
    // bridge has assigned its launch speed. The native physics world carries
    // state-2 cars across the saved footprint; leaving it enters the
    // unsupported fall (state 3), then the removal path after three seconds.
    for (LevelObjectState& car : states_) {
        if (car.asset == nullptr ||
            car.asset->kind != LevelObjectKind::SlideCar ||
            car.slideCarState < 2 || car.slideCarState >= 4 ||
            !car.visible) {
            continue;
        }
        car.position.x += car.slideCarVelocity.x * seconds;
        car.position.y += car.slideCarVelocity.y * seconds;
        car.position.z += car.slideCarVelocity.z * seconds;
        if (car.slideCarState == 2) {
            const LevelObjectState* bridge = find(car.slideCarBridgeObjectId);
            if (bridge == nullptr || !bridge->visible ||
                car.position.x < car.slideCarBridgeMinimum.x ||
                car.position.x > car.slideCarBridgeMaximum.x ||
                car.position.y < car.slideCarBridgeMinimum.y ||
                car.position.y > car.slideCarBridgeMaximum.y) {
                car.slideCarState = 3;
                car.slideCarStateSeconds = 3.0F;
                car.slideCarVelocity.z -= 100.0F;
            }
        } else {
            car.slideCarStateSeconds -= seconds;
            car.slideCarVelocity.z -= 1000.0F * seconds;
            if (car.slideCarStateSeconds <= 0.0F) {
                car.slideCarState = 4;
                car.slideCarVelocity = {};
                car.visible = false;
                car.physicsEnabled = false;
                car.collisionEnabled = false;
            }
        }
        if (car.visible) {
            car.worldTransform = worldMatrix(
                car.position, car.slideCarRotation, car.asset->scale);
        }
    }
}

void LevelObjectRuntime::updateAreaDamage(
    const assets::Vector3& playerPosition,
    std::uint32_t elapsedMilliseconds) noexcept {
    for (LevelObjectState& object : states_) {
        if (object.asset == nullptr ||
            object.asset->kind != LevelObjectKind::AreaDamage ||
            !object.visible) {
            continue;
        }
        const LevelObjectAsset& asset = *object.asset;
        object.areaDamageContactCooldownMilliseconds =
            elapsedMilliseconds >=
                    object.areaDamageContactCooldownMilliseconds
                ? 0U
                : object.areaDamageContactCooldownMilliseconds -
                      elapsedMilliseconds;

        const auto enterActive = [&]() {
            object.areaDamageState = 0;
            object.areaDamageStateMilliseconds = 0.0F;
            object.animationTimeMilliseconds = 0;
            object.animationLoops = false;
            if (asset.areaDamageAutomaticDetection) {
                // CAreaDamage::SetState(0), 0x003025a0, clears +0x2f8 so a
                // newly started attack animation can hit again.
                object.areaDamagePlayerHit = false;
            }
        };
        if (object.areaDamageState == -1) {
            object.areaDamageStateMilliseconds += elapsedMilliseconds;
            if (object.areaDamageStateMilliseconds >=
                asset.areaDamageBeginDelayMilliseconds) {
                object.areaDamageStateMilliseconds -=
                    asset.areaDamageBeginDelayMilliseconds;
                enterActive();
            }
        } else if (object.areaDamageState == 0) {
            const assets::ColladaAnimationClip* clip = nullptr;
            if (level_ != nullptr &&
                asset.archetypeIndex < level_->objectArchetypes().size()) {
                clip = level_->objectArchetypes()[asset.archetypeIndex]
                           .animationBank.findClip(object.activeAnimation);
            }
            const bool containsPlayer =
                areaDamageContainsPlayer(object, playerPosition);
            if (!containsPlayer &&
                !asset.areaDamageAutomaticDetection) {
                // Physics-driven trap/bomb bodies use persistent contact;
                // re-arm after separation rather than every fixed tick.
                object.areaDamagePlayerHit = false;
            }
            if (object.areaDamageContactCooldownMilliseconds == 0U &&
                !object.areaDamagePlayerHit && containsPlayer) {
                areaDamageEvents_.push_back(
                    {asset.objectId, asset.roomId, object.position,
                     asset.damage, asset.areaDamageType, 0xcc});
                object.areaDamageContactCooldownMilliseconds = 1000U;
                object.areaDamagePlayerHit = true;
            }
            if (clip != nullptr &&
                object.animationTimeMilliseconds >=
                    clip->durationMilliseconds()) {
                object.areaDamageState = 1;
                object.areaDamageStateMilliseconds = 0.0F;
                const float low = asset.areaDamageRandomLowMilliseconds;
                const float high = asset.areaDamageRandomHighMilliseconds;
                if (high > low + 1.0e-6F) {
                    // The original calls random() at 0x003029d6. Keep the
                    // process-local harness deterministic while selecting a
                    // stable point in the same authored interval.
                    const std::uint32_t hash =
                        static_cast<std::uint32_t>(asset.objectId) *
                        2654435761U;
                    const float fraction =
                        static_cast<float>(hash & 0xffffU) / 65535.0F;
                    object.areaDamageWaitMilliseconds =
                        low + (high - low) * fraction;
                } else {
                    object.areaDamageWaitMilliseconds = low;
                }
            }
        } else if (object.areaDamageState == 1) {
            object.areaDamageStateMilliseconds += elapsedMilliseconds;
            if (object.areaDamageStateMilliseconds >=
                object.areaDamageWaitMilliseconds) {
                object.areaDamageStateMilliseconds -=
                    object.areaDamageWaitMilliseconds;
                enterActive();
            }
        }
    }
}

void LevelObjectRuntime::updateComicCollections(
    const assets::Vector3& playerPosition) noexcept {
    const assets::Vector3 playerMinimum{
        playerPosition.x - kPlayerCollisionRadiusCentimeters,
        playerPosition.y - kPlayerCollisionRadiusCentimeters,
        playerPosition.z};
    const assets::Vector3 playerMaximum{
        playerPosition.x + kPlayerCollisionRadiusCentimeters,
        playerPosition.y + kPlayerCollisionRadiusCentimeters,
        playerPosition.z + kPlayerCollisionHeightCentimeters};
    for (LevelObjectState& object : states_) {
        if (object.asset == nullptr ||
            object.asset->kind != LevelObjectKind::Comic ||
            object.comicCollected || !object.visible) {
            continue;
        }
        const assets::Vector3 offset{
            object.position.x - object.asset->position.x,
            object.position.y - object.asset->position.y,
            object.position.z - object.asset->position.z};
        const assets::Vector3 minimum{
            object.asset->comicCollectionMinimum.x + offset.x,
            object.asset->comicCollectionMinimum.y + offset.y,
            object.asset->comicCollectionMinimum.z + offset.z};
        const assets::Vector3 maximum{
            object.asset->comicCollectionMaximum.x + offset.x,
            object.asset->comicCollectionMaximum.y + offset.y,
            object.asset->comicCollectionMaximum.z + offset.z};
        const bool intersects =
            minimum.x <= playerMaximum.x &&
            minimum.y <= playerMaximum.y &&
            minimum.z <= playerMaximum.z &&
            maximum.x >= playerMinimum.x &&
            maximum.y >= playerMinimum.y &&
            maximum.z >= playerMinimum.z;
        if (!intersects) {
            continue;
        }
        object.comicCollected = true;
        object.visible = false;
        // CComicCover::Update at 0x003043b0 loads r1 = 0x63 before
        // VoxSoundManager::Play2D; the decompiler's bool prototype obscures
        // that the second argument is the VoxSound record index.
        events_.push_back({LevelObjectEventKind::ComicCollected,
                           object.asset->objectId,
                           object.asset->roomId,
                           object.position,
                           0x63,
                           -1,
                           {},
                           object.asset->comicIndex,
                           object.asset->comicLevelStringId});
    }
}

void LevelObjectRuntime::updateElectricPlatformContacts(
    const assets::Vector3& playerPosition,
    std::uint32_t elapsedMilliseconds) noexcept {
    // Unit::CheckDamageAreaCollide (0x0032609c) returns immediately on every
    // tick that begins with a positive cooldown, even when this tick reduces
    // it to zero. Preserve that one-tick boundary behavior.
    if (electricContactCooldownMilliseconds_ > 0) {
        electricContactCooldownMilliseconds_ =
            elapsedMilliseconds >= electricContactCooldownMilliseconds_
                ? 0
                : electricContactCooldownMilliseconds_ - elapsedMilliseconds;
        return;
    }
    for (const LevelObjectState& object : states_) {
        if (object.asset == nullptr ||
            object.asset->kind != LevelObjectKind::ElectricPlatform ||
            !object.visible || !object.collisionEnabled ||
            !object.electricSwitchActive ||
            object.electricState != ElectricPlatformState::Release ||
            object.asset->electricDamage <= 0.0F ||
            !electricPlatformContainsPlayer(object, playerPosition)) {
            continue;
        }
        electricDamageEvents_.push_back(
            {object.asset->objectId, object.asset->roomId, object.position,
             object.asset->electricDamage});
        electricContactCooldownMilliseconds_ = 2000;
        break;
    }
}

void LevelObjectRuntime::endColladaAnimation(std::int32_t objectId) noexcept {
    if (LevelObjectState* object = findMutable(objectId)) {
        // UnUseDAEAnim (0x00370624) leaves the original scene node visible.
        object->visible = true;
    }
}

Result LevelObjectRuntime::applyCinematicCommand(
    const LevelOneBootstrap& level, const CinematicThread& thread,
    const CinematicCommand& command) {
    // Native CCinematicThread::Init (0x00371de0) gives camera threads their
    // camera target and player threads the active player.  Their serialized
    // object IDs are not level-object lookups, even when an ID collides with
    // a real scene object.
    if (thread.type == 2 || thread.type == 3) {
        return Result::success();
    }
    std::int32_t objectId = commandObjectId(thread, command);
    if (command.name == "MoveObject") {
        // CCinematicThread::MoveObject (0x00370494) operates only on the
        // scene object bound at thread+0x60; it never reads ObjectID.
        objectId = thread.objectId;
    }
    if (command.name == "SetAnim" && thread.type != 1) {
        // CCinematicThread::SetAnim (0x00372468) resolves ObjectID only for a
        // basic thread (type 1). Object threads animate their bound object and
        // deliberately ignore the serialized ObjectID. Level 3 cinematic
        // 30408 contains cross-ID beam commands that expose this distinction.
        objectId = thread.objectId;
    }
    if (command.name == "ShowStream") {
        const CinematicAttribute* stream =
            command.findAttribute("ID^StreamPiping");
        if (stream == nullptr || !parseInteger(stream->value, objectId)) {
            return Result::failure("ShowStream has an invalid stream ID");
        }
    }
    LevelObjectState* object = findMutable(objectId);
    if (object == nullptr) {
        return Result::success();
    }
    if (command.name == "PlayDAEAnim") {
        // PlayDAEAnim (0x003709ec) reveals its existing scene object;
        // the temporary Collada render resource is not a separate spawn.
        object->visible = true;
        return Result::success();
    }
    if (object->asset != nullptr &&
        object->asset->kind == LevelObjectKind::Train &&
        command.name == "CutTrain") {
        // CCinematicThread::CutTrain (0x00370efc) invokes CTrain::Cut at
        // 0x00321a0c on the bound object. The selected carriage becomes the
        // head of its remaining next-car chain.
        if (LevelObjectState* previous =
                findMutable(object->trainPreviousObjectId)) {
            previous->trainNextObjectId = -1;
        }
        object->trainPreviousObjectId = -1;
        object->trainCut = true;
        std::int32_t nextId = object->trainNextObjectId;
        while (nextId >= 0) {
            LevelObjectState* next = findMutable(nextId);
            if (next == nullptr || next->asset == nullptr ||
                next->asset->kind != LevelObjectKind::Train) {
                break;
            }
            next->trainTargetWaypointId = object->trainTargetWaypointId;
            nextId = next->trainNextObjectId;
        }
        return Result::success();
    }
    if (object->asset != nullptr &&
        object->asset->kind == LevelObjectKind::Train &&
        command.name == "FollowWayPoint") {
        const CinematicAttribute* waypoint =
            command.findAttribute("^ID^WayPoint");
        std::int32_t waypointId = -1;
        if (waypoint == nullptr || !parseInteger(waypoint->value, waypointId) ||
            findWaypoint(level, waypointId) == nullptr) {
            return Result::failure(
                "FollowWayPoint references an unknown waypoint");
        }
        // CCinematicThread::FollowWayPoint (0x00370288) maps optional
        // $RunType, but CTrain::CM_FollowWayPoint at 0x003213b8 ignores that
        // mode and the bool argument and calls SetDestination directly.
        object->trainTargetWaypointId = waypointId;
        return Result::success();
    }
    if (object->asset != nullptr &&
        object->asset->kind == LevelObjectKind::Train &&
        command.name == "EnableAI") {
        // CTrain::TurnOn (0x00320fc0) restores the nominal Line_Speed through
        // SetSpeedToReach after setting CWayPointMover::Active.
        object->trainActive = true;
        object->trainCurrentSpeedCentimetersPerMillisecond =
            object->asset->trainLineSpeedCentimetersPerMillisecond;
        return Result::success();
    }
    if (object->asset != nullptr &&
        object->asset->kind == LevelObjectKind::Train &&
        command.name == "DisableAI") {
        object->trainActive = false;
        object->trainCurrentSpeedCentimetersPerMillisecond = 0.0F;
        object->trainVelocityCentimetersPerSecond = {};
        return Result::success();
    }
    if (object->asset != nullptr &&
        (object->asset->kind == LevelObjectKind::Platform ||
         object->asset->kind == LevelObjectKind::ElectricPlatform) &&
        command.name == "EnableAI") {
        // CCinematicThread::EnableAI (0x0037206c) dispatches type 0x29 to
        // CWayPointMover::EnableAI (0x0030ce4c), which calls CPlatForm::TurnOn.
        object->platformMotionActive = true;
        if (object->platformMotionState == PlatformMotionState::Park ||
            object->platformMotionState == PlatformMotionState::Brake) {
            object->platformMotionState = PlatformMotionState::Move;
            object->platformMotionElapsedMilliseconds = 0.0F;
        }
        return Result::success();
    }
    if (object->asset != nullptr &&
        (object->asset->kind == LevelObjectKind::Platform ||
         object->asset->kind == LevelObjectKind::ElectricPlatform) &&
        command.name == "DisableAI") {
        object->platformMotionActive = false;
        if (object->platformMotionState == PlatformMotionState::Move) {
            object->platformMotionState = PlatformMotionState::Brake;
        }
        return Result::success();
    }
    if (command.name == "SetVisible" || command.name == "ShowStream") {
        const CinematicAttribute* visible = command.findAttribute("Visible");
        object->visible = visible == nullptr
                              ? true
                              : parseBoolean(visible->value, true);
        return Result::success();
    }
    if (command.name == "KillObject") {
        (void)destroy(objectId);
        return Result::success();
    }
    if (command.name == "SetAnim") {
        const CinematicAttribute* animation = command.findAttribute("$Anim");
        if (animation == nullptr || object->asset == nullptr ||
            object->asset->archetypeIndex >= level.objectArchetypes().size()) {
            return Result::success();
        }
        const LevelObjectArchetypeAsset& archetype =
            level.objectArchetypes()[object->asset->archetypeIndex];
        if (archetype.animationBank.findClip(animation->value) == nullptr) {
            // AnimationProxy::GetAnimIdByName / IAnimatedObject::SetAnim
            // (0x0038e9b0 / 0x00310fec) turn an unresolved name into a
            // native no-op, not a cinematic-thread failure.
            return Result::success();
        }
        object->activeAnimation = animation->value;
        object->animationTimeMilliseconds = 0;
        if (const CinematicAttribute* loop = command.findAttribute("loop")) {
            object->animationLoops = parseBoolean(loop->value, true);
        }
        if (const CinematicAttribute* speed = command.findAttribute("speed")) {
            object->animationSpeed = parseFloat(speed->value, 1.0F);
        }
        return Result::success();
    }
    if (command.name == "MoveObject") {
        if (const CinematicAttribute* absolute =
                command.findAttribute("abspos")) {
            object->position = parseVector3(absolute->value, object->position);
        } else if (const CinematicAttribute* local =
                       command.findAttribute("pos")) {
            object->position = parseVector3(local->value, object->position);
        }
        assets::Quaternion rotation = object->asset->rotation;
        if (const CinematicAttribute* authoredRotation =
                command.findAttribute("rot")) {
            rotation =
                parseQuaternion(authoredRotation->value, object->asset->rotation);
        }
        object->worldTransform = worldMatrix(object->position, rotation,
                                              object->asset->scale);
        object->cinematicMotion = {};
        if (const CinematicCommand* next =
                nextMoveObjectCommand(thread, command);
            next != nullptr &&
            next->timestampMilliseconds >=
                command.timestampMilliseconds + 51U) {
            const CinematicAttribute* nextAbsolute =
                next->findAttribute("abspos");
            const CinematicAttribute* nextRotation =
                next->findAttribute("rot");
            if (nextAbsolute != nullptr && nextRotation != nullptr) {
                object->cinematicMotion.startPosition = object->position;
                object->cinematicMotion.endPosition =
                    parseVector3(nextAbsolute->value, object->position);
                object->cinematicMotion.startRotation = rotation;
                object->cinematicMotion.endRotation =
                    parseQuaternion(nextRotation->value, rotation);
                object->cinematicMotion.durationMilliseconds =
                    next->timestampMilliseconds - command.timestampMilliseconds;
                object->cinematicMotion.active = true;
            }
        }
        return Result::success();
    }
    if (command.name == "Physics") {
        // CDestroyableObject::SetPhysics (0x003061cc) creates an active
        // behavior-4 triangle-mesh PhysicsEntity at the scene-node transform.
        // createDestoryableEntityPhysics (0x003d84b4) explicitly clears its
        // velocity and gravity, so this command is a collider handoff rather
        // than a request for autonomous rigid-body motion.
        object->physicsEnabled = true;
        return Result::success();
    }
    return Result::success();
}

std::optional<std::int32_t> LevelObjectRuntime::applyPlayerMeleeHit(
    const assets::Vector3& attackPosition,
    const assets::Vector3& attackDirection, float radius, float damage,
    float minimumForwardDot) noexcept {
    const std::optional<PlayerObjectMeleeHitResult> result =
        applyPlayerMeleeHitDetailed(attackPosition, attackDirection, radius,
                                    damage, minimumForwardDot);
    return result ? std::optional<std::int32_t>{result->objectId}
                  : std::nullopt;
}

std::optional<PlayerObjectMeleeHitResult>
LevelObjectRuntime::applyPlayerMeleeHitDetailed(
    const assets::Vector3& attackPosition,
    const assets::Vector3& attackDirection, float radius, float damage,
    float minimumForwardDot) noexcept {
    if (radius <= 0.0F || damage <= 0.0F) {
        return std::nullopt;
    }
    const float directionLength =
        std::hypot(attackDirection.x, attackDirection.y);
    const float directionX =
        directionLength > std::numeric_limits<float>::epsilon()
            ? attackDirection.x / directionLength
            : 1.0F;
    const float directionY =
        directionLength > std::numeric_limits<float>::epsilon()
            ? attackDirection.y / directionLength
            : 0.0F;
    LevelObjectState* nearest = nullptr;
    float nearestDistanceSquared = std::numeric_limits<float>::max();
    for (LevelObjectState& object : states_) {
        if (object.asset == nullptr ||
            object.asset->kind != LevelObjectKind::Destroyable ||
            !object.asset->attackable || !object.visible ||
            object.destructionPhase != LevelObjectDestructionPhase::Intact ||
            object.health <= 0.0F) {
            continue;
        }
        const float x = object.position.x - attackPosition.x;
        const float y = object.position.y - attackPosition.y;
        const float z = object.position.z - attackPosition.z;
        const float horizontalDistance = std::hypot(x, y);
        const float objectRadius = std::max(object.asset->collisionRadius, 1.0F);
        if (horizontalDistance > radius + objectRadius ||
            std::abs(z) > 160.0F + objectRadius) {
            continue;
        }
        if (horizontalDistance > objectRadius &&
            horizontalDistance > std::numeric_limits<float>::epsilon()) {
            const float forwardDot =
                (x * directionX + y * directionY) / horizontalDistance;
            const float angularAllowance = std::clamp(
                objectRadius / horizontalDistance, 0.0F, 1.0F);
            if (forwardDot + angularAllowance < minimumForwardDot) {
                continue;
            }
        }
        const float distanceSquared = x * x + y * y + z * z;
        if (distanceSquared < nearestDistanceSquared) {
            nearest = &object;
            nearestDistanceSquared = distanceSquared;
        }
    }
    if (nearest == nullptr) {
        return std::nullopt;
    }
    const float healthBefore = nearest->health;
    nearest->health = std::max(0.0F, nearest->health - damage);
    events_.push_back({LevelObjectEventKind::Hit,
                       nearest->asset->objectId,
                       nearest->asset->roomId,
                       nearest->position,
                       nearest->asset->hitVoxSoundId,
                       -1,
                       {}});
    if (nearest->health <= 0.0F) {
        const float sourceDotLocalX =
            (attackPosition.x - nearest->position.x) *
                nearest->worldTransform[0] +
            (attackPosition.y - nearest->position.y) *
                nearest->worldTransform[1] +
            (attackPosition.z - nearest->position.z) *
                nearest->worldTransform[2];
        nearest->destructionAlternateAnimation = sourceDotLocalX < 0.0F;
        beginDestruction(*nearest);
    }
    return PlayerObjectMeleeHitResult{
        nearest->asset->objectId,
        std::max(0.0F, healthBefore - nearest->health)};
}

std::vector<PlayerObjectMeleeHitResult>
LevelObjectRuntime::applyPlayerMeleeHits(
    const assets::Vector3& attackPosition,
    const assets::Vector3& attackDirection, float radius, float damage,
    float minimumForwardDot) noexcept {
    std::vector<PlayerObjectMeleeHitResult> hits;
    if (!(radius > 0.0F) || !(damage > 0.0F) ||
        !std::isfinite(radius) || !std::isfinite(damage)) {
        return hits;
    }
    const float directionLength =
        std::hypot(attackDirection.x, attackDirection.y);
    const float directionX =
        directionLength > std::numeric_limits<float>::epsilon()
            ? attackDirection.x / directionLength
            : 1.0F;
    const float directionY =
        directionLength > std::numeric_limits<float>::epsilon()
            ? attackDirection.y / directionLength
            : 0.0F;
    for (LevelObjectState& object : states_) {
        if (object.asset == nullptr ||
            object.asset->kind != LevelObjectKind::Destroyable ||
            !object.asset->attackable || !object.visible ||
            object.destructionPhase != LevelObjectDestructionPhase::Intact ||
            object.health <= 0.0F) {
            continue;
        }
        const float x = object.position.x - attackPosition.x;
        const float y = object.position.y - attackPosition.y;
        const float z = object.position.z - attackPosition.z;
        const float horizontalDistance = std::hypot(x, y);
        const float objectRadius =
            std::max(object.asset->collisionRadius, 1.0F);
        if (horizontalDistance > radius + objectRadius ||
            std::abs(z) > 160.0F + objectRadius) {
            continue;
        }
        if (horizontalDistance > objectRadius &&
            horizontalDistance > std::numeric_limits<float>::epsilon()) {
            const float forwardDot =
                (x * directionX + y * directionY) / horizontalDistance;
            const float angularAllowance = std::clamp(
                objectRadius / horizontalDistance, 0.0F, 1.0F);
            if (forwardDot + angularAllowance < minimumForwardDot) {
                continue;
            }
        }
        const float healthBefore = object.health;
        object.health = std::max(0.0F, object.health - damage);
        events_.push_back({LevelObjectEventKind::Hit,
                           object.asset->objectId, object.asset->roomId,
                           object.position, object.asset->hitVoxSoundId,
                           -1, {}});
        if (object.health <= 0.0F) {
            const float sourceDotLocalX =
                (attackPosition.x - object.position.x) *
                    object.worldTransform[0] +
                (attackPosition.y - object.position.y) *
                    object.worldTransform[1] +
                (attackPosition.z - object.position.z) *
                    object.worldTransform[2];
            object.destructionAlternateAnimation = sourceDotLocalX < 0.0F;
            beginDestruction(object);
        }
        hits.push_back({object.asset->objectId,
                        std::max(0.0F, healthBefore - object.health)});
    }
    return hits;
}

const LevelObjectState* LevelObjectRuntime::findPlayerAttackRangeTarget(
    const assets::Vector3& playerPosition, float maximumRange) const noexcept {
    if (!(maximumRange > 0.0F) || !std::isfinite(maximumRange)) {
        return nullptr;
    }
    const LevelObjectState* nearest = nullptr;
    float nearestDistanceSquared = maximumRange * maximumRange;
    // CLevel::GetTargetedDestroyableList (0x0037df54) preserves CLevel's
    // object order. SearchTargetByAttackRange (0x003430c8) walks it forward
    // and replaces only for a strictly shorter three-dimensional distance.
    for (const LevelObjectState& object : states_) {
        if (object.asset == nullptr ||
            object.asset->kind != LevelObjectKind::Destroyable ||
            !object.asset->attackable || !object.visible ||
            object.destructionPhase != LevelObjectDestructionPhase::Intact ||
            object.health <= 0.0F) {
            continue;
        }
        const float x = object.position.x - playerPosition.x;
        const float y = object.position.y - playerPosition.y;
        const float z = object.position.z - playerPosition.z;
        const float distanceSquared = x * x + y * y + z * z;
        if (distanceSquared < nearestDistanceSquared) {
            nearest = &object;
            nearestDistanceSquared = distanceSquared;
        }
    }
    return nearest;
}

const LevelObjectState* LevelObjectRuntime::findPlayerEyeAttackTarget(
    const assets::Vector3& playerPosition,
    const assets::Vector3& attackDirection, float maximumRange,
    const LevelCollision* collision, float minimumForwardDot) const noexcept {
    if (!(maximumRange > 0.0F) || !std::isfinite(maximumRange)) {
        return nullptr;
    }
    const float directionLength =
        std::hypot(attackDirection.x, attackDirection.y);
    if (directionLength <= std::numeric_limits<float>::epsilon()) {
        return nullptr;
    }
    const float directionX = attackDirection.x / directionLength;
    const float directionY = attackDirection.y / directionLength;
    const LevelObjectState* selected = nullptr;
    float bestForwardDot = minimumForwardDot;
    // SearchTargetByEyeHorizon (0x00343b70) appends CLevel's targeted
    // destroyables after the enemy lists and then traverses the resulting
    // array backwards. A tie therefore retains the later-authored object.
    for (auto iterator = states_.rbegin(); iterator != states_.rend();
         ++iterator) {
        const LevelObjectState& object = *iterator;
        if (object.asset == nullptr ||
            object.asset->kind != LevelObjectKind::Destroyable ||
            !object.asset->attackable || !object.visible ||
            object.destructionPhase != LevelObjectDestructionPhase::Intact ||
            object.health <= 0.0F) {
            continue;
        }
        const float x = object.position.x - playerPosition.x;
        const float y = object.position.y - playerPosition.y;
        const float z = object.position.z - playerPosition.z;
        const float distance = std::sqrt(x * x + y * y + z * z);
        if (distance - object.asset->collisionRadius > maximumRange) {
            continue;
        }
        if (collision != nullptr && collision->segmentBlocked(
                {object.position.x, object.position.y,
                 object.position.z + object.asset->collisionHeight},
                {playerPosition.x, playerPosition.y,
                 playerPosition.z + kPlayerCollisionHeightCentimeters},
                kTargetOcclusionIgnoredPhysicsFlags)) {
            continue;
        }
        const float horizontalLength = std::hypot(x, y);
        if (horizontalLength <= std::numeric_limits<float>::epsilon()) {
            continue;
        }
        const float forwardDot =
            (x * directionX + y * directionY) / horizontalLength;
        if (forwardDot > bestForwardDot) {
            selected = &object;
            bestForwardDot = forwardDot;
        }
    }
    return selected;
}

bool LevelObjectRuntime::destroy(std::int32_t objectId) noexcept {
    LevelObjectState* object = findMutable(objectId);
    if (object == nullptr || object->asset == nullptr ||
        object->asset->kind != LevelObjectKind::Destroyable) {
        return false;
    }
    if (object->destructionPhase == LevelObjectDestructionPhase::Intact) {
        object->health = 0.0F;
        beginDestruction(*object);
    }
    return true;
}

bool LevelObjectRuntime::isDestroyed(std::int32_t objectId) const noexcept {
    const LevelObjectState* object = find(objectId);
    return object != nullptr &&
           object->destructionPhase != LevelObjectDestructionPhase::Intact;
}

bool LevelObjectRuntime::isComicCollected(
    std::int32_t objectId) const noexcept {
    const LevelObjectState* object = find(objectId);
    return object != nullptr && object->asset != nullptr &&
           object->asset->kind == LevelObjectKind::Comic &&
           object->comicCollected;
}

Result LevelObjectRuntime::setAnimation(std::int32_t objectId,
                                        std::string_view animation,
                                        bool loop) {
    LevelObjectState* object = findMutable(objectId);
    if (object == nullptr || object->asset == nullptr || level_ == nullptr) {
        return Result::failure("Level object animation target was not found");
    }
    const LevelObjectArchetypeAsset& archetype =
        level_->objectArchetypes()[object->asset->archetypeIndex];
    if (archetype.animationBank.findClip(animation) == nullptr) {
        return Result::failure("Level object animation was not found: " +
                               std::string(animation));
    }
    object->activeAnimation = animation;
    object->animationTimeMilliseconds = 0;
    object->animationSpeed = 1.0F;
    object->animationLoops = loop;
    return Result::success();
}

bool LevelObjectRuntime::animationFinished(
    std::int32_t objectId) const noexcept {
    const LevelObjectState* object = find(objectId);
    if (object == nullptr || object->asset == nullptr || level_ == nullptr ||
        object->animationLoops || object->activeAnimation.empty()) {
        return false;
    }
    const assets::ColladaAnimationClip* clip =
        level_->objectArchetypes()[object->asset->archetypeIndex]
            .animationBank.findClip(object->activeAnimation);
    return clip != nullptr &&
           object->animationTimeMilliseconds >= clip->durationMilliseconds();
}

std::vector<LevelObjectEvent> LevelObjectRuntime::consumeEvents() {
    std::vector<LevelObjectEvent> events = std::move(events_);
    events_.clear();
    return events;
}

std::vector<ElectricPlatformDamageEvent>
LevelObjectRuntime::consumeElectricPlatformDamageEvents() {
    std::vector<ElectricPlatformDamageEvent> events =
        std::move(electricDamageEvents_);
    electricDamageEvents_.clear();
    return events;
}

std::vector<AreaDamageEvent>
LevelObjectRuntime::consumeAreaDamageEvents() {
    std::vector<AreaDamageEvent> events = std::move(areaDamageEvents_);
    areaDamageEvents_.clear();
    return events;
}

void LevelObjectRuntime::resetTransientForCheckPointLoad() noexcept {
    events_.clear();
    electricDamageEvents_.clear();
    areaDamageEvents_.clear();
    electricContactCooldownMilliseconds_ = 0;
    for (LevelObjectState& object : states_) {
        object.cinematicMotion = {};
        if (object.asset && object.asset->kind == LevelObjectKind::BrokenBridge) {
            // CBrokenBridge::Load/ResetObject (0x00300920/0x003012b0)
            // restarts the section at its authored transform and idle state.
            object.position = object.asset->position;
            object.worldTransform = object.asset->worldTransform;
            object.bridgeRotation = object.asset->rotation;
            object.bridgeState = 1;
            object.bridgeStateSeconds = 0.0F;
            object.bridgeVelocity = object.bridgeAngularVelocity = {};
            object.activeAnimation = "idle";
            object.animationTimeMilliseconds = 0;
            object.animationLoops = true;
            object.visible = object.physicsEnabled = object.collisionEnabled = true;
        } else if (object.asset &&
                   object.asset->kind == LevelObjectKind::SlideCar) {
            // CSlideCar::ResetObject/SetState(0) (0x0031bd7c/0x0031bc94).
            object.position = object.slideCarInitialPosition;
            object.slideCarRotation = object.slideCarInitialRotation;
            object.slideCarState = 0;
            object.slideCarStateSeconds = 0.0F;
            object.slideCarVelocity = {};
            object.visible = true;
            object.physicsEnabled = object.asset->hasCollisionBounds;
            object.collisionEnabled = object.asset->hasCollisionBounds;
            if (const LevelObjectState* bridge =
                    find(object.slideCarBridgeObjectId);
                bridge != nullptr && bridge->asset != nullptr) {
                const float halfHeight = std::abs(
                    bridge->asset->collisionLocalMaximum.z -
                    bridge->asset->collisionLocalMinimum.z) * 0.5F;
                object.position.z = bridge->asset->position.z + halfHeight;
            }
            object.worldTransform = worldMatrix(
                object.position, object.slideCarRotation,
                object.asset->scale);
        } else if (object.asset &&
                   object.asset->kind == LevelObjectKind::AreaDamage) {
            // CAreaDamage::ResetObject/SetState(-1), 0x003025f4.
            object.position = object.asset->position;
            object.worldTransform = object.asset->worldTransform;
            object.areaDamageState = -1;
            object.areaDamageStateMilliseconds = 0.0F;
            object.areaDamageWaitMilliseconds =
                object.asset->areaDamageRandomLowMilliseconds;
            object.areaDamageContactCooldownMilliseconds = 0;
            object.areaDamagePlayerHit = false;
            object.animationTimeMilliseconds = 0;
            object.visible = object.asset->visible;
            object.physicsEnabled =
                !object.asset->areaDamageIgnorePhysics ||
                !object.asset->areaDamageAutomaticDetection;
            object.collisionEnabled = object.physicsEnabled;
        }
    }
}

void LevelObjectRuntime::beginDestruction(LevelObjectState& object) noexcept {
    if (object.asset == nullptr || level_ == nullptr ||
        object.destructionPhase != LevelObjectDestructionPhase::Intact) {
        return;
    }
    object.destructionPhase = LevelObjectDestructionPhase::Breaking;
    object.collisionEnabled = false;
    const LevelObjectArchetypeAsset& archetype =
        level_->objectArchetypes()[object.asset->archetypeIndex];
    const std::size_t breakingClipIndex =
        object.destructionAlternateAnimation &&
                archetype.animationBank.clips().size() > 3
            ? 3U
            : 1U;
    if (archetype.animationBank.clips().size() > breakingClipIndex) {
        object.activeAnimation =
            archetype.animationBank.clips()[breakingClipIndex].name;
        object.animationTimeMilliseconds = 0;
        object.animationSpeed = 1.0F;
        object.animationLoops = false;
    } else {
        finishDestruction(object);
    }
    events_.push_back({LevelObjectEventKind::Destroyed,
                       object.asset->objectId,
                       object.asset->roomId,
                       object.position,
                       -1,
                       object.asset->deadCinematicId,
                       object.asset->destructionEffectType});
}

void LevelObjectRuntime::finishDestruction(LevelObjectState& object) noexcept {
    if (object.asset == nullptr || level_ == nullptr) {
        return;
    }
    object.destructionPhase = LevelObjectDestructionPhase::Destroyed;
    object.collisionEnabled = object.asset->collisionAfterDestruction;
    const LevelObjectArchetypeAsset& archetype =
        level_->objectArchetypes()[object.asset->archetypeIndex];
    const std::size_t terminalClipIndex =
        object.destructionAlternateAnimation &&
                archetype.animationBank.clips().size() > 4
            ? 4U
            : 2U;
    if (archetype.animationBank.clips().size() > terminalClipIndex) {
        object.activeAnimation =
            archetype.animationBank.clips()[terminalClipIndex].name;
        object.animationTimeMilliseconds = 0;
        object.animationSpeed = 1.0F;
        object.animationLoops = false;
    }
    if (object.asset->deadSpawnObjectId >= 0) {
        if (LevelObjectState* spawned =
                findMutable(object.asset->deadSpawnObjectId)) {
            spawned->visible = true;
        }
    }
}

Result LevelObjectRuntime::setRuntimeState(
    std::int32_t objectId, const assets::Vector3& position, bool visible,
    bool physicsEnabled) {
    LevelObjectState* object = findMutable(objectId);
    if (object == nullptr || object->asset == nullptr) {
        return Result::failure("Level object runtime state target was not found");
    }
    object->position = position;
    object->worldTransform = worldMatrix(
        position, object->asset->rotation, object->asset->scale);
    object->cinematicMotion = {};
    object->visible = visible;
    object->physicsEnabled = physicsEnabled;
    return Result::success();
}

const LevelObjectState* LevelObjectRuntime::find(
    std::int32_t objectId) const noexcept {
    const auto match = std::find_if(
        states_.begin(), states_.end(), [objectId](const LevelObjectState& state) {
            return state.asset != nullptr && state.asset->objectId == objectId;
        });
    return match == states_.end() ? nullptr : &*match;
}

std::optional<LevelObjectSupportPose> LevelObjectRuntime::supportPose(
    std::int32_t objectId) const noexcept {
    const LevelObjectState* object = find(objectId);
    if (object == nullptr || object->asset == nullptr || !object->visible ||
        !object->physicsEnabled || !object->collisionEnabled) {
        return std::nullopt;
    }
    return LevelObjectSupportPose{objectId, object->position,
                                  objectRotation(*object),
                                  object->asset->scale};
}

assets::Vector3 LevelObjectRuntime::supportMotionDelta(
    const LevelObjectSupportPose& previous,
    const assets::Vector3& supportedPoint) const noexcept {
    const auto current = supportPose(previous.objectId);
    if (!current.has_value()) {
        return {};
    }
    assets::Vector3 local{supportedPoint.x - previous.position.x,
                          supportedPoint.y - previous.position.y,
                          supportedPoint.z - previous.position.z};
    local = rotate({-previous.rotation.x, -previous.rotation.y,
                    -previous.rotation.z, previous.rotation.w}, local);
    if (std::abs(previous.scale.x) > 1.0e-6F) local.x /= previous.scale.x;
    if (std::abs(previous.scale.y) > 1.0e-6F) local.y /= previous.scale.y;
    if (std::abs(previous.scale.z) > 1.0e-6F) local.z /= previous.scale.z;
    local.x *= current->scale.x;
    local.y *= current->scale.y;
    local.z *= current->scale.z;
    const assets::Vector3 moved = rotate(current->rotation, local);
    const assets::Vector3 next{current->position.x + moved.x,
                               current->position.y + moved.y,
                               current->position.z + moved.z};
    return {next.x - supportedPoint.x, next.y - supportedPoint.y,
            next.z - supportedPoint.z};
}

LevelObjectState* LevelObjectRuntime::findMutable(
    std::int32_t objectId) noexcept {
    const auto match = std::find_if(
        states_.begin(), states_.end(), [objectId](const LevelObjectState& state) {
            return state.asset != nullptr && state.asset->objectId == objectId;
        });
    return match == states_.end() ? nullptr : &*match;
}

} // namespace usm::game
