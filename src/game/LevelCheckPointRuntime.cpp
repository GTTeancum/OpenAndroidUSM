#include "game/LevelCheckPointRuntime.hpp"

#include "game/PlayerPhysicsConstants.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace usm::game {
namespace {


assets::Vector3 normalizedFacing(assets::Vector3 facing) noexcept {
    const float length =
        std::sqrt(facing.x * facing.x + facing.y * facing.y);
    if (length <= std::numeric_limits<float>::epsilon()) {
        return {1.0F, 0.0F, 0.0F};
    }
    return {facing.x / length, facing.y / length, 0.0F};
}

assets::Vector3 transformPointByInverse(
    const std::array<float, 16>& matrix,
    const assets::Vector3& point) noexcept {
    // Irrlicht's CMatrix4 transform used by obbox::test_obb (0x003d45a4)
    // maps local axes through columns [0..2], [4..6], [8..10] and stores
    // translation in [12..14]. Solve that exact affine 3x3 system here.
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
        return {std::numeric_limits<float>::infinity(),
                std::numeric_limits<float>::infinity(),
                std::numeric_limits<float>::infinity()};
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

Result LevelCheckPointRuntime::bind(
    std::span<const LevelCheckPointAsset> checkPoints,
    std::span<const LevelWayPointAsset> waypoints) {
    states_.clear();
    states_.reserve(checkPoints.size());
    waypoints_ = waypoints;
    lastCheckPointId_ = -1;
    std::set<std::int32_t> ids;
    for (const LevelCheckPointAsset& checkPoint : checkPoints) {
        if (checkPoint.objectId < 0 || !ids.insert(checkPoint.objectId).second) {
            return Result::failure(
                "CheckPoint runtime has an invalid or duplicate object ID");
        }
        if (checkPoint.sizes.x <= 0.0F || checkPoint.sizes.y <= 0.0F ||
            checkPoint.sizes.z <= 0.0F) {
            return Result::failure(
                "CheckPoint runtime has nonpositive authored dimensions");
        }
        states_.push_back({&checkPoint, checkPoint.enabled});
    }
    return Result::success();
}

std::optional<CheckPointActivation> LevelCheckPointRuntime::update(
    const assets::Vector3& playerPosition,
    const assets::Vector3& playerFacing, float playerHealth,
    std::int32_t cameraAreaId) noexcept {
    // CCheckPoint::Update (0x00369280) is disabled while health is not
    // positive, and each checkpoint dispatches only once until Reset.
    if (playerHealth <= 0.0F) {
        return std::nullopt;
    }
    for (State& state : states_) {
        if (state.asset == nullptr || !state.enabled || !state.armed ||
            !containsPlayer(*state.asset, playerPosition)) {
            continue;
        }
        state.armed = false;
        state.hasSavedData = true;
        state.savedCameraAreaId = cameraAreaId;
        state.savedPosition = playerPosition;
        state.savedFacing = normalizedFacing(playerFacing);
        lastCheckPointId_ = state.asset->objectId;
        return CheckPointActivation{state.asset->objectId, cameraAreaId,
                                    playerPosition, state.savedFacing, true};
    }
    return std::nullopt;
}

Result LevelCheckPointRuntime::save(
    std::int32_t checkPointId, std::int32_t cameraAreaId,
    const assets::Vector3& playerPosition,
    const assets::Vector3& playerFacing) {
    const auto match = std::find_if(
        states_.begin(), states_.end(),
        [checkPointId](const State& state) {
            return state.asset != nullptr &&
                   state.asset->objectId == checkPointId;
        });
    if (match == states_.end()) {
        // CCinematicThread::SaveCheckpoint (0x00370384) returns false when
        // FindCheckPointByID cannot resolve the authored ID.
        return Result::failure("Save references an unknown CheckPoint ID");
    }
    match->armed = false;
    match->hasSavedData = true;
    match->savedCameraAreaId = cameraAreaId;
    match->savedPosition = playerPosition;
    match->savedFacing = normalizedFacing(playerFacing);
    lastCheckPointId_ = checkPointId;
    return Result::success();
}

void LevelCheckPointRuntime::resetForLevelRestart() noexcept {
    for (State& state : states_) {
        state.armed = true;
        state.enabled = state.asset != nullptr && state.asset->enabled;
    }
}

std::optional<CheckPointRestartPlacement>
LevelCheckPointRuntime::restartPlacement() const noexcept {
    const auto match = std::find_if(
        states_.begin(), states_.end(),
        [this](const State& state) {
            return state.asset != nullptr &&
                   state.asset->objectId == lastCheckPointId_;
        });
    if (match == states_.end() || match->asset == nullptr) {
        return std::nullopt;
    }
    CheckPointRestartPlacement placement;
    placement.objectId = match->asset->objectId;
    placement.cameraAreaId = match->savedCameraAreaId;
    if (const LevelWayPointAsset* wayPoint =
            findWayPoint(match->asset->linkedWaypointId)) {
        // RestartAtCheckPoint prioritizes CCheckPoint+0x2a0 over
        // SavePosition and uses the waypoint-linked camera area when present.
        placement.playerPosition = wayPoint->position;
        if (wayPoint->linkedCameraAreaId >= 0) {
            placement.cameraAreaId = wayPoint->linkedCameraAreaId;
        }
        placement.kind = CheckPointPlacementKind::LinkedWaypoint;
        placement.faceCameraAfterPlacement = true;
        return placement;
    }
    if (match->asset->savePosition && match->hasSavedData) {
        placement.playerPosition = match->savedPosition;
        placement.playerFacing = match->savedFacing;
        placement.kind = CheckPointPlacementKind::SavedPlayerTransform;
        return placement;
    }
    placement.playerPosition = match->asset->position;
    placement.kind = CheckPointPlacementKind::AuthoredNode;
    placement.faceCameraAfterPlacement = true;
    return placement;
}

bool LevelCheckPointRuntime::containsPlayer(
    const LevelCheckPointAsset& checkPoint,
    const assets::Vector3& playerPosition) const noexcept {
    if (checkPoint.orientedBox) {
        // The OBB overload selected by CCheckPoint::Update passes only the
        // live player position to obbox::test_obb (0x003d45a4); it does not
        // inflate the box by the player's collision dimensions.
        const assets::Vector3 local =
            transformPointByInverse(checkPoint.worldTransform,
                                    playerPosition);
        return std::abs(local.x) <= checkPoint.sizes.x * 0.5F &&
               std::abs(local.y) <= checkPoint.sizes.y * 0.5F &&
               std::abs(local.z) <= checkPoint.sizes.z * 0.5F;
    }
    const assets::Vector3 half{checkPoint.sizes.x * 0.5F,
                              checkPoint.sizes.y * 0.5F,
                              checkPoint.sizes.z * 0.5F};
    const assets::Vector3 checkPointMinimum{
        checkPoint.position.x - half.x, checkPoint.position.y - half.y,
        checkPoint.position.z - half.z};
    const assets::Vector3 checkPointMaximum{
        checkPoint.position.x + half.x, checkPoint.position.y + half.y,
        checkPoint.position.z + half.z};
    // Player::GetPlayerBox (0x00343408): radius from 0x0056ec90 and the
    // live player height at +0x50. The recovered values are 50/185.
    const assets::Vector3 playerMinimum{
        playerPosition.x - kPlayerCollisionRadiusCentimeters,
        playerPosition.y - kPlayerCollisionRadiusCentimeters,
        playerPosition.z};
    const assets::Vector3 playerMaximum{
        playerPosition.x + kPlayerCollisionRadiusCentimeters,
        playerPosition.y + kPlayerCollisionRadiusCentimeters,
        playerPosition.z + kPlayerCollisionHeightCentimeters};
    return checkPointMaximum.x >= playerMinimum.x &&
           checkPointMaximum.y >= playerMinimum.y &&
           checkPointMaximum.z >= playerMinimum.z &&
           playerMaximum.x >= checkPointMinimum.x &&
           playerMaximum.y >= checkPointMinimum.y &&
           playerMaximum.z >= checkPointMinimum.z;
}

const LevelWayPointAsset* LevelCheckPointRuntime::findWayPoint(
    std::int32_t objectId) const noexcept {
    if (objectId < 0) {
        return nullptr;
    }
    const auto match = std::find_if(
        waypoints_.begin(), waypoints_.end(),
        [objectId](const LevelWayPointAsset& wayPoint) {
            return wayPoint.objectId == objectId;
        });
    return match == waypoints_.end() ? nullptr : &*match;
}

} // namespace usm::game
