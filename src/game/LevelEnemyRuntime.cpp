#include "game/LevelEnemyRuntime.hpp"

#include "game/PlayerPhysicsConstants.hpp"

#include "assets/ColladaSkinning.hpp"
#include "game/LevelCollision.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string_view>

namespace usm::game {
namespace {

constexpr float kRadiansToDegrees = 57.29577951308232F;
constexpr float kEnemyGravityCentimetersPerSecondSquared = 1000.0F;
// Player::SendHitMessage (0x00346168, 0x00346270) mutates the shared
// AIHitTargetInfo immediately before every target dispatch. This is not a
// tuning value: each recipient receives 85% of the vertical force remaining
// after the preceding recipient.
constexpr float kPlayerHitVerticalForceDispatchScale = 0.85F;
constexpr float kGunLineSpeedCentimetersPerSecond = 1500.0F;
constexpr std::uint32_t kGunLineLifetimeMilliseconds = 2000;
constexpr float kGunLinePlayerRadiusCentimeters = 60.0F;
constexpr float kGunLineMaximumRangeCentimeters =
    kGunLineSpeedCentimetersPerSecond *
    (static_cast<float>(kGunLineLifetimeMilliseconds) / 1000.0F);
// CBullet::setType(0), CBullet::Fire, and CBullet::Update at
// 0x0035c444/0x0035c2f0/0x0035c8b0.
constexpr float kWebPelletSpeedCentimetersPerSecond = 1500.0F;
constexpr float kWebPelletRadiusCentimeters = 30.0F;
constexpr float kWebPelletLaunchOffsetCentimeters = 50.0F;
constexpr float kWebPelletMaximumTravelCentimeters = 5000.0F;
// ResetMaxMeleeEngagingEntities(1) (0x003744a4) selects one melee engager
// and stores 1000 ms at CAIEntityManager+0xd4 for the default difficulty.
// UnRegisterEntityForMeleeAttack (0x00375560) calls random(base, base * 2),
// so the actual hand-off delay is the half-open interval [1000, 2000).
constexpr std::int32_t kMeleeEngagementHandoffMinimumMilliseconds = 1000;
constexpr std::int32_t kMeleeEngagementHandoffMaximumMilliseconds = 2000;
// Unit::IsBlockedByWorld (0x00324670) receives -33000 / 0xffff7f18 from
// SearchTargetByEyeHorizon (0x00343b70). Physics::processCollision treats it
// as the ignored helper-surface mask while ordinary authored ground and wall
// triangles continue to occlude target acquisition.
constexpr std::uint32_t kTargetOcclusionIgnoredPhysicsFlags = 0xffff7f18U;

std::string_view hurtStateName(std::int16_t stateId) noexcept {
    switch (stateId) {
    case 49: return "ENEMY_BEHAVIOR_HURT_STATE_COMMON";
    case 50: return "ENEMY_BEHAVIOR_HURT_STATE_HEAVY";
    case 51: return "ENEMY_BEHAVIOR_HURT_STATE_BACK_HURT_COMMON";
    case 52: return "ENEMY_BEHAVIOR_HURT_STATE_BACK_HURT";
    case 53: return "ENEMY_BEHAVIOR_HURT_STATE_TO_FLYING";
    case 54: return "ENEMY_BEHAVIOR_HURT_STATE_FLYING";
    case 55: return "ENEMY_BEHAVIOR_HURT_STATE_FLYING_TO_GROUND";
    case 56: return "ENEMY_BEHAVIOR_HURT_STATE_FLYING_TO_WALL";
    case 57: return "ENEMY_BEHAVIOR_HURT_STATE_TO_AIR";
    case 58: return "ENEMY_BEHAVIOR_HURT_STATE_AIR_TO_FALL";
    case 59: return "ENEMY_BEHAVIOR_HURT_STATE_GROUND_TO_STAND";
    case 60: return "ENEMY_BEHAVIOR_HURT_STATE_AIR_COMMON";
    case 61: return "ENEMY_BEHAVIOR_HURT_STATE_AIR_TO_FLYING";
    case 62: return "ENEMY_BEHAVIOR_HURT_STATE_AIR_FLYING";
    case 63: return "ENEMY_BEHAVIOR_HURT_STATE_AIR_FLYING_TO_GROUND";
    case 64: return "ENEMY_BEHAVIOR_HURT_STATE_AIR_DRAGTO";
    case 65: return "ENEMY_BEHAVIOR_HURT_STATE_HEAVY_BLOW_TO_GROUND";
    case 66: return "ENEMY_BEHAVIOR_HURT_STATE_BY_ELECTRIC";
    case 67: return "ENEMY_BEHAVIOR_HURT_STATE_BY_TOXIN";
    case 68: return "ENEMY_BEHAVIOR_HURT_STATE_ON_WALL";
    case 69: return "ENEMY_BEHAVIOR_HURT_STATE_AIR_KICKDOWN";
    case 70: return "ENEMY_BEHAVIOR_HURT_STATE_GROUND_BOUNCE";
    default: return {};
    }
}

bool hurtStateLoops(std::int16_t stateId) noexcept {
    return stateId == 54 || stateId == 58 || stateId == 62;
}
// CThrowObject::InitPoolProp (0x003592cc) maps weapon type 5 to subtype 1.
// CThrowObject::Throw (0x00359af4) uses the common 1000 cm/s branch for that
// subtype, clamps flight time to 0.5 s, and advances the release by 50 ms.
constexpr float kMolotovSpeedCentimetersPerSecond = 1000.0F;
constexpr float kMolotovMinimumFlightSeconds = 0.5F;
constexpr float kMolotovInitialAdvanceSeconds = 0.05F;
constexpr float kMolotovCollisionRadiusCentimeters = 20.0F;
constexpr float kMolotovExplosionRadiusCentimeters = 250.0F;
constexpr float kMolotovStoppedFallSpeedCentimetersPerSecond = -200.0F;
constexpr float kThrowObjectWorldLimitCentimeters = 100000.0F;
// CBoss::ResetBehavior (0x0032afdc) configures Rhino's CBehaviorDush with
// 1200 cm/s and 45 degrees/s.  The dash behavior's target-contact branch
// uses the two actors' physics shapes; these are the reconstructed player
// capsule values paired with Rhino's authored collision radius.
constexpr float kRhinoDashSpeedCentimetersPerSecond = 1200.0F;
constexpr float kRhinoDashAngularSpeedDegreesPerSecond = 45.0F;
constexpr std::uint32_t kRhinoFailedStruggleMilliseconds = 5000;
// CBoss::ResetBehavior (0x0032afdc) passes 250.0F to
// CBehaviorThrow::SetGrapRange before phase-one/phase-two task 12 executes.
constexpr float kRhinoGrabRangeCentimeters = 250.0F;
// CBoss' Robot Phantom task records in _GLOBAL__I_CBoss (0x00329cd4) store
// 500.0F for both the task-3 melee and task-5 range approach records.
constexpr float kRobotPhantomApproachRangeCentimeters = 500.0F;
// CBehaviorRangeAttack::DynamicLoadMeshAndAnim (0x003c17fc) constructs the
// type-30 CBoomerang with 500.0F speed and 200.0F collision-pause time.
constexpr float kRobotPhantomBoomerangSpeedCentimetersPerSecond = 500.0F;
constexpr float kRobotPhantomBoomerangHandSpeedCentimetersPerSecond = 250.0F;
constexpr std::uint32_t kRobotPhantomBoomerangOutboundMilliseconds = 1500;
constexpr std::uint32_t kRobotPhantomBoomerangTargetPauseMilliseconds = 500;
constexpr std::uint32_t kRobotPhantomBoomerangCollisionPauseMilliseconds = 200;
// CBoomerang's constructors (0x0035b3fc/0x0035b604) pass 50.0F to
// createFlyableEntityPhysics after loading phantom_unit_weapons.bdae.
constexpr float kRobotPhantomBoomerangCollisionRadiusCentimeters = 50.0F;
constexpr float kRobotPhantomBoomerangReturnRadiusCentimeters = 300.0F;
constexpr float kRobotPhantomBoomerangHandEpsilonCentimeters = 25.0F;
constexpr std::uint32_t kRobotPhantomConcealMilliseconds = 500;
// CBoss::ResetBehavior (0x0032afdc), CBehaviorWeak::onMessage
// (0x003cb34c), CBehaviorRotate::BehaviorUpdate (0x003c297c), and
// CBehaviorElectroDush::StateExit/BehaviorUpdate
// (0x003b3190/0x003b2cf8) provide these native Electro parameters.
constexpr std::uint32_t kElectroWeakMilliseconds = 4000;
constexpr float kElectroWeakRadiusCentimeters = 400.0F;
constexpr float kElectroWeakDamage = 70.0F;
constexpr std::uint32_t kElectroRotateReadyMilliseconds = 2000;
constexpr std::uint32_t kElectroRotateMilliseconds = 10000;
constexpr float kElectroDashSpeedCentimetersPerSecond = 2500.0F;
constexpr float kElectroDashPlayerHitRadiusCentimeters = 80.0F;
constexpr float kElectroDashDamage = 70.0F;
constexpr std::uint32_t kElectroRangeWaitMilliseconds = 200;
constexpr float kElectroThunderclapRadiusCentimeters = 400.0F;
constexpr float kElectroThunderclapSpeedCentimetersPerSecond =
    400.0F / 3.0F;
constexpr float kElectroThunderclapImpactDistanceCentimeters = 60.0F;
constexpr float kElectroThunderclapDamage = 50.0F;
constexpr std::uint32_t kElectroThunderclapImpactMilliseconds = 633;
constexpr std::uint32_t kElectroThunderclapReleaseMilliseconds = 833;
constexpr std::uint32_t kElectroThunderclapFadeMilliseconds = 250;
// CElectricPost's electro_beam.bdae dummy_start1/dummy_end1 authored nodes
// measure 1429.7869 cm apart along local -Y. ResetData (0x003c2a94) supplies
// 50 damage; the animated post mesh is approximately 76 cm wide.
constexpr float kElectroPostLengthCentimeters = 1429.7869F;
constexpr float kElectroPostHalfWidthCentimeters = 38.0F;
constexpr float kElectroPostDamage = 50.0F;

assets::Vector3 parseVector3(std::string_view text,
                             assets::Vector3 fallback = {}) noexcept {
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

assets::Quaternion parseQuaternion(std::string_view text) noexcept {
    std::string storage(text);
    std::replace(storage.begin(), storage.end(), ',', ' ');
    const char* cursor = storage.c_str();
    char* end = nullptr;
    assets::Quaternion result;
    for (float* component : {&result.x, &result.y, &result.z, &result.w}) {
        *component = std::strtof(cursor, &end);
        if (end == cursor) {
            return {};
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

float parseFloat(std::string_view text, float fallback) noexcept {
    std::string storage(text);
    char* end = nullptr;
    const float value = std::strtof(storage.c_str(), &end);
    return end == storage.c_str() ? fallback : value;
}

bool parseBool(std::string_view text, bool fallback) noexcept {
    if (text == "true" || text == "1") {
        return true;
    }
    if (text == "false" || text == "0") {
        return false;
    }
    return fallback;
}

bool parseInteger(std::string_view text, std::int32_t& value) noexcept {
    const char* begin = text.data();
    const char* end = begin + text.size();
    const auto parsed = std::from_chars(begin, end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

std::array<float, 16> worldMatrix(const assets::Vector3& position,
                                  assets::Quaternion rotation,
                                  const assets::Vector3& scale) noexcept {
    const float quaternionLength =
        std::sqrt(rotation.x * rotation.x + rotation.y * rotation.y +
                  rotation.z * rotation.z + rotation.w * rotation.w);
    if (quaternionLength > std::numeric_limits<float>::epsilon()) {
        rotation.x /= quaternionLength;
        rotation.y /= quaternionLength;
        rotation.z /= quaternionLength;
        rotation.w /= quaternionLength;
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

std::array<float, 16> multiplyMatrix(
    const std::array<float, 16>& left,
    const std::array<float, 16>& right) noexcept {
    std::array<float, 16> result{};
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t component = 0; component < 4; ++component) {
                result[column * 4 + row] +=
                    left[component * 4 + row] *
                    right[column * 4 + component];
            }
        }
    }
    return result;
}

assets::Vector3 facingFromMatrix(
    const std::array<float, 16>& matrix) noexcept {
    const float length = std::hypot(matrix[4], matrix[5]);
    if (length <= std::numeric_limits<float>::epsilon()) {
        return {1.0F, 0.0F, 0.0F};
    }
    return {-matrix[4] / length, -matrix[5] / length, 0.0F};
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

void advanceCinematicMotion(LevelEnemyState& enemy,
                            std::uint32_t elapsedMilliseconds) noexcept {
    EnemyCinematicMotionState& motion = enemy.cinematicMotion;
    if (!motion.active || motion.durationMilliseconds == 0 ||
        enemy.asset == nullptr) {
        return;
    }
    // CCinematicThread::DoExecChange (0x00371880) evaluates the current clock
    // before adding this frame's delta.
    const float factor = std::clamp(
        static_cast<float>(motion.elapsedMilliseconds) /
            static_cast<float>(motion.durationMilliseconds),
        0.0F, 1.0F);
    enemy.position = {
        motion.startPosition.x +
            (motion.endPosition.x - motion.startPosition.x) * factor,
        motion.startPosition.y +
            (motion.endPosition.y - motion.startPosition.y) * factor,
        motion.startPosition.z +
            (motion.endPosition.z - motion.startPosition.z) * factor};
    enemy.worldTransform =
        worldMatrix(enemy.position,
                    slerp(motion.startRotation, motion.endRotation, factor),
                    enemy.asset->scale);
    enemy.facing = facingFromMatrix(enemy.worldTransform);
    const std::uint64_t next =
        static_cast<std::uint64_t>(motion.elapsedMilliseconds) +
        elapsedMilliseconds;
    motion.elapsedMilliseconds = static_cast<std::uint32_t>(
        std::min<std::uint64_t>(next, motion.durationMilliseconds));
    if (next >= motion.durationMilliseconds) {
        motion.active = false;
    }
}

std::string_view idleAnimation(const LevelEnemyAsset& enemy) noexcept {
    if (enemy.onWall) {
        return "wall_idle";
    }
    if (enemy.gameType == "MeleeThugEnemy_knife") {
        return "idle_knife_at_idle";
    }
    if (enemy.gameType == "RangeThug_big") {
        return "idlebaz";
    }
    if (enemy.gameType == "MeleeThug_gun" ||
        enemy.gameType == "RangeThug_hammer" ||
        enemy.gameType == "RangeThug_molotov" ||
        enemy.gameType.starts_with("SymbioteZombie_") ||
        enemy.gameType == "Robot_Shield" ||
        enemy.gameType == "Boss_Sandman" ||
        enemy.gameType == "Boss_Rhino") {
        return "idle";
    }
    if (enemy.gameType.starts_with("Boss_") ||
        enemy.gameType == "Robot_Phantom") {
        // Boss-specific task graphs select their own state animations. Until
        // each graph is reconstructed, retain the authored node animation
        // instead of falling through to a thug-only clip name.
        return enemy.initialAnimation;
    }
    if (enemy.gameType == "MeleeThugEnemy_bat") {
        return "idle_at1_idle";
    }
    // Enemy families beyond the original thug reconstruction have their own
    // task graphs. Preserve the clip selected by
    // IAnimatedObject::LoadMeshAndAnimator until those graphs select one of
    // their authored BehaviorState mappings.
    return enemy.initialAnimation;
}

std::string_view attackAnimation(const LevelEnemyAsset& enemy) noexcept {
    if (enemy.gameType == "RangeThug_big") {
        return "idlebaz_rush_attack_idlebaz";
    }
    if (enemy.gameType == "RangeThug_hammer") {
        return "idle_attack_hammer_idle";
    }
    if (enemy.gameType == "Boss_Sandman") {
        return "ground_attack1";
    }
    if (enemy.gameType == "Boss_Rhino") {
        return "punch_left";
    }
    if (enemy.gameType.starts_with("SymbioteZombie_")) {
        // ENEMY_BEHAVIOR_MELEE_ATTACK_STATE_DO_ATTACK maps enemy types
        // 8/9/11 to this clip. EnemySpecialAction records 44, 61, and 88
        // attach ATTACK_HIT_NORMAL_claw_zombie to its 50-percent key.
        return "idle_claw_idle";
    }
    return idleAnimation(enemy);
}

std::string_view chaseAnimation(const LevelEnemyAsset& enemy) noexcept {
    // BehaviorAnimMapList's ENEMY_BEHAVIOR_MOVE_STATE_RUN row deliberately
    // maps the heavier male symbiote and shield robot to walk. The female
    // variants use run. These names are data, not animation-name guesses.
    if (enemy.enemyTypeId == 8 || enemy.enemyTypeId == 12) {
        return "walk";
    }
    if (enemy.enemyTypeId == 9 || enemy.enemyTypeId == 10 ||
        enemy.enemyTypeId == 11) {
        return "run";
    }
    return "run";
}

void setFacing(LevelEnemyState& enemy, const assets::Vector3& facing) noexcept {
    if (enemy.asset == nullptr) {
        return;
    }
    enemy.facing = enemy.onWall && enemy.wallAttached
        ? assets::Vector3{-enemy.wallNormal.x, -enemy.wallNormal.y,
                          -enemy.wallNormal.z}
        : facing;
    const assets::Vector3& direction = enemy.facing;
    const assets::Vector3& scale = enemy.asset->scale;
    const assets::Vector3 worldRenderOffset{
        direction.x * enemy.animationRenderOffset.x -
            direction.y * enemy.animationRenderOffset.y,
        direction.y * enemy.animationRenderOffset.x +
            direction.x * enemy.animationRenderOffset.y,
        enemy.animationRenderOffset.z};
    enemy.worldTransform = {
        -direction.y * scale.x,
        direction.x * scale.x,
        0.0F,
        0.0F,
        -direction.x * scale.y,
        -direction.y * scale.y,
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        scale.z,
        0.0F,
        enemy.position.x + worldRenderOffset.x,
        enemy.position.y + worldRenderOffset.y,
        enemy.position.z + worldRenderOffset.z,
        1.0F};
    // CEnemy::UpdateForce (0x00331d78) omits the wall-radius scene-node
    // offset while Unit state 14 owns the held enemy. Applying it here
    // pushes the wall_be_drag pose into the building.
    if (enemy.onWall && enemy.wallAttached && !enemy.wallWebCaptured) {
        enemy.worldTransform[12] += direction.x * enemy.collisionRadius;
        enemy.worldTransform[13] += direction.y * enemy.collisionRadius;
        enemy.worldTransform[14] += direction.z * enemy.collisionRadius;
    }
}

bool crossedLoopEvent(std::uint32_t previousTime,
                      std::uint32_t currentTime, std::uint32_t duration,
                      std::uint32_t eventTime) noexcept {
    if (duration == 0 || currentTime <= previousTime || eventTime >= duration) {
        return false;
    }
    // Special-action rows at key percent zero fire when the animation is
    // entered (CBehaviorDush's run/rush and run_bash_attack rows are shipped
    // this way).  Treat the first positive advance as crossing that key.
    if (previousTime == 0 && eventTime == 0) {
        return true;
    }
    std::uint64_t nextOccurrence =
        (static_cast<std::uint64_t>(previousTime) / duration) * duration +
        eventTime;
    if (nextOccurrence <= previousTime) {
        nextOccurrence += duration;
    }
    return nextOccurrence <= currentTime;
}

assets::Vector3 turnToward2D(const assets::Vector3& current,
                             const assets::Vector3& target,
                             float maximumDegrees) noexcept {
    const float currentAngle = std::atan2(current.y, current.x);
    const float targetAngle = std::atan2(target.y, target.x);
    float difference = targetAngle - currentAngle;
    constexpr float kPi = 3.14159265358979323846F;
    while (difference > kPi) {
        difference -= 2.0F * kPi;
    }
    while (difference < -kPi) {
        difference += 2.0F * kPi;
    }
    const float maximumRadians = maximumDegrees / kRadiansToDegrees;
    const float angle = currentAngle +
                        std::clamp(difference, -maximumRadians,
                                   maximumRadians);
    return {std::cos(angle), std::sin(angle), 0.0F};
}

assets::Vector3 transformPoint(
    const std::array<float, 16>& matrix,
    const assets::Vector3& point = {}) noexcept {
    return {
        point.x * matrix[0] + point.y * matrix[4] + point.z * matrix[8] +
            matrix[12],
        point.x * matrix[1] + point.y * matrix[5] + point.z * matrix[9] +
            matrix[13],
        point.x * matrix[2] + point.y * matrix[6] + point.z * matrix[10] +
            matrix[14],
    };
}

bool segmentTouchesPlayer(const assets::Vector3& start,
                          const assets::Vector3& end,
                          const assets::Vector3& playerPosition,
                          float projectileRadius) noexcept {
    const assets::Vector3 center{
        playerPosition.x, playerPosition.y,
        playerPosition.z + kPlayerCollisionHeightCentimeters * 0.5F};
    const float segmentX = end.x - start.x;
    const float segmentY = end.y - start.y;
    const float segmentZ = end.z - start.z;
    const float lengthSquared = segmentX * segmentX + segmentY * segmentY +
                                segmentZ * segmentZ;
    float fraction = 0.0F;
    if (lengthSquared > std::numeric_limits<float>::epsilon()) {
        fraction = std::clamp(
            ((center.x - start.x) * segmentX +
             (center.y - start.y) * segmentY +
             (center.z - start.z) * segmentZ) /
                lengthSquared,
            0.0F, 1.0F);
    }
    const float x = start.x + segmentX * fraction - center.x;
    const float y = start.y + segmentY * fraction - center.y;
    const float z = start.z + segmentZ * fraction - center.z;
    const float radius =
        kPlayerCollisionRadiusCentimeters + projectileRadius;
    return x * x + y * y + z * z <= radius * radius;
}

bool segmentTouchesCircle2D(const assets::Vector3& start,
                            const assets::Vector3& end,
                            const assets::Vector3& center,
                            float radius) noexcept {
    const float segmentX = end.x - start.x;
    const float segmentY = end.y - start.y;
    const float lengthSquared = segmentX * segmentX + segmentY * segmentY;
    float factor = 0.0F;
    if (lengthSquared > std::numeric_limits<float>::epsilon()) {
        factor = std::clamp(
            ((center.x - start.x) * segmentX +
             (center.y - start.y) * segmentY) /
                lengthSquared,
            0.0F, 1.0F);
    }
    const float x = start.x + segmentX * factor - center.x;
    const float y = start.y + segmentY * factor - center.y;
    return x * x + y * y <= radius * radius;
}

// Generic portable cylinder-sector helper for base-position callers. Player
// melee uses the centered native shape path below instead.
bool cylinderSectorIntersects(const assets::Vector3& attackPosition,
                              float attackHeight,
                              const assets::Vector3& attackDirection,
                              float attackRadius,
                              float minimumAngleDegrees,
                              float maximumAngleDegrees,
                              const assets::Vector3& targetPosition,
                              float targetRadius,
                              float targetHeight) noexcept {
    if (attackHeight <= 0.0F || attackRadius <= 0.0F ||
        targetRadius < 0.0F || targetHeight <= 0.0F) {
        return false;
    }
    const float attackTop = attackPosition.z + attackHeight;
    const float targetTop = targetPosition.z + targetHeight;
    if (attackTop < targetPosition.z || targetTop < attackPosition.z) {
        return false;
    }

    const float x = targetPosition.x - attackPosition.x;
    const float y = targetPosition.y - attackPosition.y;
    const float distance = std::hypot(x, y);
    if (distance > attackRadius + targetRadius) {
        return false;
    }
    if (distance <= targetRadius ||
        distance <= std::numeric_limits<float>::epsilon()) {
        return true;
    }

    const float directionLength =
        std::hypot(attackDirection.x, attackDirection.y);
    if (directionLength <= std::numeric_limits<float>::epsilon()) {
        return true;
    }
    const float forward =
        (attackDirection.x * x + attackDirection.y * y) /
        (directionLength * distance);
    const float side =
        (attackDirection.x * y - attackDirection.y * x) /
        (directionLength * distance);
    const float centerAngle = std::atan2(side, forward) * kRadiansToDegrees;
    const float angularRadius =
        std::asin(std::clamp(targetRadius / distance, 0.0F, 1.0F)) *
        kRadiansToDegrees;
    return centerAngle + angularRadius >= minimumAngleDegrees &&
           centerAngle - angularRadius <= maximumAngleDegrees;
}

float nativeEnemyPhysicsHalfHeight(const LevelEnemyState& enemy) noexcept {
    if (enemy.asset == nullptr) {
        return enemy.collisionRadius;
    }
    // createEnemyPhysics (0x003d8980) seeds capsule X/Y/Z radii from the
    // attribute collision radius. CEnemy::InitEntityAttribute
    // (0x003373e8) changes only Z: enemy type 0x11 uses 400 cm and type 5
    // uses Unit::GetHeight()/2. All other enemy capsules retain radius in Z.
    if (enemy.asset->enemyTypeId == 0x11) {
        return 400.0F;
    }
    if (enemy.asset->enemyTypeId == 5) {
        return enemy.collisionHeight * 0.5F;
    }
    return enemy.collisionRadius;
}

bool nativePlayerSectorIntersects(
    const assets::Vector3& attackCenter,
    const assets::Vector3& attackDirection, float attackRadius,
    float minimumAngleDegrees, float maximumAngleDegrees,
    const LevelEnemyState& enemy) noexcept {
    if (!(attackRadius > 0.0F) || !(enemy.collisionRadius >= 0.0F)) {
        return false;
    }
    const float targetHalfHeight = nativeEnemyPhysicsHalfHeight(enemy);
    // Unit::CheckAttackByPos (0x00325118) supplies the animated Bip01 world
    // position as the attack center and player height/2 as its half-height.
    // Physics::testPieCollision (0x003d5434) supplies the enemy capsule's
    // localToWorld center and shape+0x18 as the target half-height. At
    // 0x003d5462 it transforms the shape-local vector at +0x08; the
    // createEnemyPhysics constructor at 0x003d89be stores {0, 0, radius}
    // there. The enemy state position is the Unit base, so the physics
    // cylinder center is one collision radius above it. The exact interval
    // test is testCylinderCylinder at 0x003d0e9c.
    constexpr float attackHalfHeight =
        kPlayerCollisionHeightCentimeters * 0.5F;
    const float targetCenterZ = enemy.position.z + enemy.collisionRadius;
    if (attackCenter.z - attackHalfHeight >
            targetCenterZ + targetHalfHeight ||
        attackCenter.z + attackHalfHeight <
            targetCenterZ - targetHalfHeight) {
        return false;
    }

    const float x = enemy.position.x - attackCenter.x;
    const float y = enemy.position.y - attackCenter.y;
    const float distance = std::hypot(x, y);
    if (distance > attackRadius + enemy.collisionRadius) {
        return false;
    }
    if (distance <= std::numeric_limits<float>::epsilon()) {
        return true;
    }
    const float directionLength =
        std::hypot(attackDirection.x, attackDirection.y);
    if (directionLength <= std::numeric_limits<float>::epsilon()) {
        return true;
    }
    // testCylinderPie (0x003d38f8) normalizes the center delta and compares
    // its dot product with the attack direction to cos(authored angle). The
    // target radius participates in cylinder overlap, not angular expansion.
    const float forward =
        (attackDirection.x * x + attackDirection.y * y) /
        (directionLength * distance);
    const float halfAngleDegrees = std::max(
        std::abs(minimumAngleDegrees), std::abs(maximumAngleDegrees));
    return forward >= std::cos(halfAngleDegrees / kRadiansToDegrees);
}

// Unit::CheckAttackByPosAndAxis (0x00324f58) and the two
// testCylinderPieAnyAxis overloads (0x003d0f18/0x003d380c). Native wall
// attacks use an axis-normal slab and a disk in the wall plane, centered
// on the animated Bip01 node. The angular test uses the target CENTER,
// unlike the ground-sector helper's angular-radius approximation.
bool wallSectorIntersects(const assets::Vector3& attackCenter,
                          const assets::Vector3& axis,
                          const assets::Vector3& attackDirection,
                          float attackRadius, float attackHalfHeight,
                          float minimumAngleDegrees, float maximumAngleDegrees,
                          const assets::Vector3& targetBase,
                          float targetRadius, float targetHeight) noexcept {
    assets::Vector3 delta{targetBase.x - attackCenter.x,
                          targetBase.y - attackCenter.y,
                          targetBase.z + targetHeight * 0.5F - attackCenter.z};
    const float normalDistance = delta.x * axis.x + delta.y * axis.y +
                                 delta.z * axis.z;
    if (std::abs(normalDistance) > attackHalfHeight + targetHeight * 0.5F) {
        return false;
    }
    delta.x -= axis.x * normalDistance;
    delta.y -= axis.y * normalDistance;
    delta.z -= axis.z * normalDistance;
    const float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y +
                                     delta.z * delta.z);
    if (distance > attackRadius + targetRadius) {
        return false;
    }
    assets::Vector3 direction = attackDirection;
    const float normalDirection = direction.x * axis.x + direction.y * axis.y +
                                  direction.z * axis.z;
    direction.x -= axis.x * normalDirection;
    direction.y -= axis.y * normalDirection;
    direction.z -= axis.z * normalDirection;
    const float length = std::sqrt(direction.x * direction.x +
        direction.y * direction.y + direction.z * direction.z);
    if (length <= std::numeric_limits<float>::epsilon()) {
        return false;
    }
    direction.x /= length;
    direction.y /= length;
    direction.z /= length;
    const float midpoint = -(minimumAngleDegrees + maximumAngleDegrees) *
                            0.5F / kRadiansToDegrees;
    const float cosine = std::cos(midpoint);
    const float sine = std::sin(midpoint);
    const assets::Vector3 rotated{
        direction.x * cosine + (axis.y * direction.z - axis.z * direction.y) * sine,
        direction.y * cosine + (axis.z * direction.x - axis.x * direction.z) * sine,
        direction.z * cosine + (axis.x * direction.y - axis.y * direction.x) * sine};
    const float forward = distance > std::numeric_limits<float>::epsilon()
        ? (delta.x * rotated.x + delta.y * rotated.y + delta.z * rotated.z) / distance
        : 0.0F;
    const float halfAngle = (maximumAngleDegrees - minimumAngleDegrees) *
                            0.5F / kRadiansToDegrees;
    return forward >= std::cos(halfAngle);
}

std::optional<float> segmentExpandedCylinderHitFraction(
    const assets::Vector3& start, const assets::Vector3& end,
    const assets::Vector3& cylinderBase, float cylinderRadius,
    float cylinderHeight, float expansionRadius) noexcept {
    const float dx = end.x - start.x;
    const float dy = end.y - start.y;
    const float dz = end.z - start.z;
    const float relativeX = start.x - cylinderBase.x;
    const float relativeY = start.y - cylinderBase.y;
    const float radius = cylinderRadius + expansionRadius;
    const float a = dx * dx + dy * dy;
    const float b = 2.0F * (relativeX * dx + relativeY * dy);
    const float c = relativeX * relativeX + relativeY * relativeY -
                    radius * radius;

    float horizontalEnter = 0.0F;
    float horizontalExit = 1.0F;
    if (a <= std::numeric_limits<float>::epsilon()) {
        if (c > 0.0F) {
            return std::nullopt;
        }
    } else {
        const float discriminant = b * b - 4.0F * a * c;
        if (discriminant < 0.0F) {
            return std::nullopt;
        }
        const float root = std::sqrt(discriminant);
        horizontalEnter = (-b - root) / (2.0F * a);
        horizontalExit = (-b + root) / (2.0F * a);
        if (horizontalEnter > horizontalExit) {
            std::swap(horizontalEnter, horizontalExit);
        }
    }

    float verticalEnter = 0.0F;
    float verticalExit = 1.0F;
    const float minimumZ = cylinderBase.z - expansionRadius;
    const float maximumZ = cylinderBase.z + cylinderHeight + expansionRadius;
    if (std::abs(dz) <= std::numeric_limits<float>::epsilon()) {
        if (start.z < minimumZ || start.z > maximumZ) {
            return std::nullopt;
        }
    } else {
        verticalEnter = (minimumZ - start.z) / dz;
        verticalExit = (maximumZ - start.z) / dz;
        if (verticalEnter > verticalExit) {
            std::swap(verticalEnter, verticalExit);
        }
    }

    const float enter = std::max({0.0F, horizontalEnter, verticalEnter});
    const float exit = std::min({1.0F, horizontalExit, verticalExit});
    return enter <= exit ? std::optional<float>{enter} : std::nullopt;
}

} // namespace

Result LevelEnemyRuntime::initialize(const LevelOneBootstrap& level) {
    states_.clear();
    pendingPlayerHits_.clear();
    pendingSoundCues_.clear();
    pendingCameraShakeCues_.clear();
    gunLines_.clear();
    molotovs_.clear();
    boomerangs_.clear();
    playerWebPellets_.clear();
    thunderclaps_.clear();
    electricPosts_.clear();
    electroBursts_.clear();
    landingAnimatedEffects_.clear();
    pendingLandingAnimatedEffectSpawnEvents_.clear();
    pendingProjectileEvents_.clear();
    pendingEffectCues_.clear();
    pendingPlayerWebPelletEvents_.clear();
    pendingEnemySeparationEvents_.clear();
    shownHealthBarObjectId_.reset();
    meleeEngagerObjectId_ = -1;
    meleeEngagementCooldownMilliseconds_ = 0.0F;
    rhinoQuickTimeAction_.cancel();
    rhinoQuickTimeEnemyId_ = -1;
    level_ = &level;
    states_.reserve(level.enemies().size());
    for (const LevelEnemyAsset& enemy : level.enemies()) {
        if (enemy.archetypeIndex >= level.enemyArchetypes().size()) {
            states_.clear();
            return Result::failure("Enemy archetype index is invalid");
        }
        const EnemyAttributeDefinition* attributes =
            level.enemyAttributeConfigs().find(enemy.enemyTypeId);
        if (attributes == nullptr || attributes->collisionRadius <= 0.0F ||
            attributes->collisionHeight <= 0.0F) {
            states_.clear();
            return Result::failure("Enemy collision dimensions are invalid");
        }
        LevelEnemyState state;
        state.asset = &enemy;
        state.position = enemy.position;
        state.facing = facingFromMatrix(enemy.worldTransform);
        state.worldTransform = enemy.worldTransform;
        state.activeAnimation = enemy.initialAnimation;
        state.collisionRadius = attributes->collisionRadius;
        state.collisionHeight = attributes->collisionHeight;
        state.canBeTiedUp = attributes->canBeTiedUp();
        state.canBeDraggedTo = attributes->canBeDraggedTo();
        state.allowsHorizontalHitForce =
            attributes->allowsHorizontalHitForce;
        state.allowsVerticalHitForce = attributes->allowsVerticalHitForce;
        state.allowsLaunchHitType = attributes->allowsLaunchHitType;
        state.canBeCounterHit = attributes->canBeCounterHit;
        state.onWall = enemy.onWall && attributes->canMoveOnWall();
        if (state.onWall) {
            state.activeAnimation = "wall_idle";
        }
        state.health = enemy.health;
        state.maximumHealth = enemy.health;
        state.electroHomePosition = enemy.position;
        state.visible = enemy.visible && !enemy.waitSpawn;
        state.aiEnabled = enemy.aiEnabled;
        state.physicsActive = enemy.aiEnabled;
        state.behavior = enemy.aiEnabled ? EnemyBehaviorState::Idle
                                         : EnemyBehaviorState::Disabled;
        states_.push_back(std::move(state));
    }
    const auto rhino = std::find_if(
        states_.begin(), states_.end(), [](const LevelEnemyState& state) {
            return state.asset != nullptr &&
                   state.asset->gameType == "Boss_Rhino";
        });
    if (rhino != states_.end()) {
        rhinoQuickTimeAction_.bind(
            level.quickTimeActionConfigs(), level.buttonConfigs(),
            level.player().animationBank,
            level.enemyArchetypes()[rhino->asset->archetypeIndex]
                .animationBank);
    }
    return Result::success();
}

std::optional<std::array<float, 16>>
LevelEnemyRuntime::rhinoQuickTimePlayerWorldTransform() const {
    if (!rhinoQuickTimeAction_.active() || level_ == nullptr) {
        return std::nullopt;
    }
    const LevelEnemyState* rhino = find(rhinoQuickTimeEnemyId_);
    if (rhino == nullptr || rhino->asset == nullptr ||
        rhino->asset->archetypeIndex >= level_->enemyArchetypes().size()) {
        return std::nullopt;
    }
    const EnemyArchetypeAsset& archetype =
        level_->enemyArchetypes()[rhino->asset->archetypeIndex];
    const assets::ColladaAnimationClip* clip =
        archetype.animationBank.findClip(rhinoQuickTimeAction_.npcAnimation());
    if (clip == nullptr) {
        return std::nullopt;
    }
    std::array<float, 16> handModelTransform{};
    const Result handResult = assets::evaluateColladaSceneNodeTransform(
        archetype.mesh, archetype.animationBank,
        clip->startMilliseconds +
            rhinoQuickTimeAction_.npcAnimationMilliseconds(),
        "R_Hand_Dummy", handModelTransform);
    if (!handResult) {
        return std::nullopt;
    }

    // CBehaviorThrow::StateEnter(88) (0x003c5f64) reparents the player's
    // scene node to CBoss::ResetBehavior's R_Hand_Dummy and assigns this exact
    // relative matrix: identity with local Z = -0.5 * Player::GetHeight().
    const std::array<float, 16> playerRelativeTransform{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, -0.5F * kPlayerCollisionHeightCentimeters, 1.0F};
    return multiplyMatrix(
        multiplyMatrix(rhino->worldTransform, handModelTransform),
        playerRelativeTransform);
}

std::optional<assets::Vector3>
LevelEnemyRuntime::rhinoQuickTimePlayerFacing() const noexcept {
    if (!rhinoQuickTimeAction_.active()) {
        return std::nullopt;
    }
    const LevelEnemyState* rhino = find(rhinoQuickTimeEnemyId_);
    return rhino == nullptr
               ? std::nullopt
               : std::optional<assets::Vector3>{assets::Vector3{
                     -rhino->facing.x, -rhino->facing.y,
                     -rhino->facing.z}};
}

std::optional<assets::Vector3>
LevelEnemyRuntime::rhinoQuickTimePlayerDetachPosition() const {
    const auto playerWorld = rhinoQuickTimePlayerWorldTransform();
    if (!playerWorld) {
        return std::nullopt;
    }
    // CBehaviorThrow state 90 retrieves the absolute position of the
    // player's parent (R_Hand_Dummy) before restoring the root parent and
    // re-enabling physics. Undo state 88's local -half-height translation to
    // recover that exact parent position from the evaluated child matrix.
    return assets::Vector3{
        (*playerWorld)[12] +
            0.5F * kPlayerCollisionHeightCentimeters * (*playerWorld)[8],
        (*playerWorld)[13] +
            0.5F * kPlayerCollisionHeightCentimeters * (*playerWorld)[9],
        (*playerWorld)[14] +
            0.5F * kPlayerCollisionHeightCentimeters * (*playerWorld)[10]};
}

void LevelEnemyRuntime::advanceAnimations(
    std::uint32_t elapsedMilliseconds,
    const LevelCollision* collision) noexcept {
    for (LevelEnemyState& enemy : states_) {
        if (rhinoQuickTimeAction_.active() && enemy.asset != nullptr &&
            enemy.asset->objectId == rhinoQuickTimeEnemyId_) {
            enemy.animationTimeMilliseconds =
                rhinoQuickTimeAction_.npcAnimationMilliseconds();
            continue;
        }
        const EnemyArchetypeAsset* archetype = nullptr;
        const assets::ColladaAnimationClip* clip = nullptr;
        if (level_ != nullptr && enemy.asset != nullptr &&
            enemy.asset->archetypeIndex < level_->enemyArchetypes().size()) {
            archetype =
                &level_->enemyArchetypes()[enemy.asset->archetypeIndex];
            clip = archetype->animationBank.findClip(enemy.activeAnimation);
        }
        const std::uint32_t duration =
            clip == nullptr ? 0U : clip->durationMilliseconds();
        const std::uint64_t previousTime = enemy.animationTimeMilliseconds;
        const double advanced =
            static_cast<double>(elapsedMilliseconds) * enemy.animationSpeed;
        const std::uint64_t step = static_cast<std::uint64_t>(
            std::max(advanced, 0.0));
        if (enemy.animationReversed) {
            if (enemy.animationLoops) {
                if (duration != 0) {
                    const std::uint64_t current =
                        enemy.animationTimeMilliseconds % duration;
                    enemy.animationTimeMilliseconds =
                        static_cast<std::uint32_t>(
                            (current + duration - step % duration) % duration);
                }
            } else {
                enemy.animationTimeMilliseconds =
                    step >= enemy.animationTimeMilliseconds
                        ? 0
                        : static_cast<std::uint32_t>(
                              enemy.animationTimeMilliseconds - step);
            }
        } else {
            const std::uint64_t next = enemy.animationTimeMilliseconds + step;
            enemy.animationTimeMilliseconds = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(
                    next, std::numeric_limits<std::uint32_t>::max()));
        }

        enemy.animationRenderOffset = {};
        if (clip == nullptr || archetype == nullptr || duration == 0 ||
            archetype->animationDisplacement.frameCount() == 0) {
            setFacing(enemy, enemy.facing);
            continue;
        }

        const auto physicalAtLocal = [&](std::uint32_t localTime) {
            return archetype->animationDisplacement.physicalAt(
                clip->startMilliseconds + std::min(localTime, duration));
        };
        const assets::Vector3 cycleStart = physicalAtLocal(0);
        const assets::Vector3 cycleEnd = physicalAtLocal(duration);
        assets::Vector3 localDelta;
        std::uint32_t currentLocal{};
        if (!enemy.animationReversed) {
            const std::uint64_t previousLocal =
                enemy.animationLoops ? previousTime % duration
                                     : std::min<std::uint64_t>(previousTime,
                                                                duration);
            const std::uint64_t unwrappedNext = previousLocal + step;
            std::uint64_t completedCycles = 0;
            if (enemy.animationLoops) {
                completedCycles = unwrappedNext / duration;
                currentLocal = static_cast<std::uint32_t>(
                    unwrappedNext % duration);
            } else {
                currentLocal = static_cast<std::uint32_t>(
                    std::min<std::uint64_t>(unwrappedNext, duration));
            }
            const assets::Vector3 previous =
                physicalAtLocal(static_cast<std::uint32_t>(previousLocal));
            const assets::Vector3 current = physicalAtLocal(currentLocal);
            localDelta = {
                current.x - previous.x +
                    (cycleEnd.x - cycleStart.x) * completedCycles,
                current.y - previous.y +
                    (cycleEnd.y - cycleStart.y) * completedCycles,
                current.z - previous.z +
                    (cycleEnd.z - cycleStart.z) * completedCycles};
        } else if (enemy.animationLoops) {
            const std::uint64_t previousLocal = previousTime % duration;
            std::uint64_t completedCycles = 0;
            if (step <= previousLocal) {
                currentLocal = static_cast<std::uint32_t>(previousLocal - step);
            } else {
                const std::uint64_t remaining = step - previousLocal;
                completedCycles = 1U + (remaining - 1U) / duration;
                currentLocal = static_cast<std::uint32_t>(
                    (duration - remaining % duration) % duration);
            }
            const assets::Vector3 previous =
                physicalAtLocal(static_cast<std::uint32_t>(previousLocal));
            const assets::Vector3 current = physicalAtLocal(currentLocal);
            localDelta = {
                current.x - previous.x -
                    (cycleEnd.x - cycleStart.x) * completedCycles,
                current.y - previous.y -
                    (cycleEnd.y - cycleStart.y) * completedCycles,
                current.z - previous.z -
                    (cycleEnd.z - cycleStart.z) * completedCycles};
        } else {
            const std::uint32_t previousLocal = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(previousTime, duration));
            currentLocal = step >= previousLocal
                               ? 0U
                               : static_cast<std::uint32_t>(previousLocal - step);
            const assets::Vector3 previous = physicalAtLocal(previousLocal);
            const assets::Vector3 current = physicalAtLocal(currentLocal);
            localDelta = {current.x - previous.x, current.y - previous.y,
                          current.z - previous.z};
        }

        // Unit::UpdateDisplacement (0x00324df0) rotates Dummy root deltas by
        // the actor's facing and commits them to its PhysicsEntity. Ordinary
        // chase locomotion is already advanced by CBehaviorMove's recovered
        // velocity path above; hurt behavior has no such movement path and
        // owns the authored animation displacement directly. This raises the
        // target capsule during air_to_flying instead of raising only its
        // visible skeleton and making the following fly-kick miss.
        const bool ordinaryMeleeAttackDisplacement =
            enemy.behavior == EnemyBehaviorState::AttackRange &&
            enemy.meleeAttackActive && enemy.asset != nullptr &&
            !enemy.asset->gameType.starts_with("Boss_") &&
            enemy.robotPhantomTask == RobotPhantomTaskState::None &&
            enemy.electroTask == ElectroBossTaskState::None;
        if (enemy.behavior == EnemyBehaviorState::Hurt ||
            enemy.behavior == EnemyBehaviorState::TiedUp ||
            ordinaryMeleeAttackDisplacement) {
            const assets::Vector3 worldDelta{
                enemy.facing.x * localDelta.x - enemy.facing.y * localDelta.y,
                enemy.facing.y * localDelta.x + enemy.facing.x * localDelta.y,
                localDelta.z};
            const assets::Vector3 desired{
                enemy.position.x + worldDelta.x,
                enemy.position.y + worldDelta.y,
                enemy.position.z + worldDelta.z};
            if (collision != nullptr && enemy.grounded &&
                !enemy.anchoredWithoutSupport && worldDelta.z <= 0.01F) {
                // Unit::UpdateDisplacement (0x00324df0) writes through the
                // native PhysicsEntity, so a grounded recovery clip cannot
                // walk its cylinder beyond the final sliver of supporting
                // floor. Preserve that manifold contract for portable root
                // motion. This matters at the Room 1 storefront edge where
                // onground_to_idle otherwise moves a just-landed thug about
                // one centimetre beyond its 50 cm support overlap.
                assets::Vector3 resolved;
                (void)collision->resolveGroundMotion(enemy.position, desired,
                                                      resolved);
                enemy.position = resolved;
            } else if (collision != nullptr && !enemy.grounded) {
                const assets::Vector3 airborneStart = enemy.position;
                assets::Vector3 resolved;
                collision->resolveAirMotion(airborneStart, desired,
                                            resolved);
                // Unit::UpdateDisplacement (0x00324df0) applies the Dummy
                // root translation through the native PhysicsEntity. The
                // actor therefore cannot tunnel its cylinder base through
                // the street when a grounded hurt clip is selected while an
                // enemy is airborne (CBehaviorHurt::BehaviorStart,
                // 0x003b87a8). resolveAirMotion only handles walls, so make
                // the missing downward cylinder contact explicit here.
                if (worldDelta.z < -0.01F) {
                    const float fallDistance =
                        std::max(airborneStart.z - resolved.z, 0.0F);
                    float supportHeight = 0.0F;
                    const bool crossedSupport = collision->groundHeight(
                        resolved, fallDistance + 5.0F, 5.0F,
                        supportHeight);
                    if (crossedSupport &&
                        supportHeight <= airborneStart.z + 5.0F &&
                        resolved.z <= supportHeight) {
                        resolved.z = supportHeight;
                        enemy.verticalVelocity = 0.0F;
                        enemy.hurtVelocity.z = 0.0F;
                        enemy.grounded = true;
                    }
                }
                enemy.position = resolved;
            } else {
                enemy.position = desired;
            }
            if (worldDelta.z > 0.01F) {
                enemy.grounded = false;
                enemy.anchoredWithoutSupport = false;
            }
            enemy.animationRenderOffset =
                archetype->animationDisplacement.renderOffsetAt(
                    clip->startMilliseconds + currentLocal);
        }
        setFacing(enemy, enemy.facing);
    }
}

void LevelEnemyRuntime::attachEnemyToWall(
    LevelEnemyState& enemy, const LevelCollision& collision) noexcept {
    // CheckWall (0x003305d8): first successful 1000 cm ray in this exact
    // order, with an EXCLUSION mask of ~0x2024. Physics::processCollision
    // (0x003d5b5c) rejects (mask & entityFlags) != 0; ordinary floors and
    // walls must therefore not win this query before the climbable surface.
    constexpr std::array<assets::Vector3, 6> directions{{
        {0.0F, -1.0F, 0.0F}, {0.0F, 0.0F, 1.0F},
        {0.0F, 0.0F, -1.0F}, {-1.0F, 0.0F, 0.0F},
        {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}}};
    for (const auto& direction : directions) {
        const assets::Vector3 end{
            enemy.position.x + direction.x * 1000.0F,
            enemy.position.y + direction.y * 1000.0F,
            enemy.position.z + direction.z * 1000.0F};
        const auto hit = collision.segmentFirstHit(
            enemy.position, end, ~std::uint32_t{0x2024});
        if (!hit) {
            continue;
        }
        enemy.wallNormal = hit->normal;
        // CheckWall projects onto the plane. The native capsule contact
        // solver then separates its radius from that plane; UpdateForce
        // offsets only the rendered mesh back toward the wall by that radius.
        const float correction =
            (hit->position.x - enemy.position.x) * hit->normal.x +
            (hit->position.y - enemy.position.y) * hit->normal.y +
            (hit->position.z - enemy.position.z) * hit->normal.z +
            enemy.collisionRadius;
        enemy.position.x += hit->normal.x * correction;
        enemy.position.y += hit->normal.y * correction;
        enemy.position.z += hit->normal.z * correction;
        enemy.wallAttached = true;
        enemy.verticalVelocity = 0.0F;
        enemy.grounded = false;
        enemy.anchoredWithoutSupport = false;
        setFacing(enemy, enemy.facing);
        return;
    }
}

void LevelEnemyRuntime::updateWallEnemy(
    LevelEnemyState& enemy, std::uint32_t elapsedMilliseconds,
    const assets::Vector3& playerPosition,
    const assets::Vector3& playerFacing, bool playerOnWall,
    const LevelCollision* collision) {
    if (level_ == nullptr || enemy.asset == nullptr) {
        return;
    }
    const auto* attributes =
        level_->enemyAttributeConfigs().find(enemy.asset->enemyTypeId);
    if (attributes == nullptr) {
        return;
    }
    const auto idle = [&] {
        enemy.behavior = EnemyBehaviorState::Idle;
        enemy.wallBehaviorState = 0;
        enemy.meleeAttackActive = false;
        unregisterMeleeEngager(enemy.asset->objectId);
        if (enemy.activeAnimation != "wall_idle") {
            enemy.activeAnimation = "wall_idle";
            enemy.animationTimeMilliseconds = 0;
            enemy.animationLoops = true;
            enemy.animationSpeed = 1.0F;
            enemy.animationReversed = false;
        }
    };
    const auto bodyPattern = [&](const assets::Vector3& target) {
        // GetMyBodyPatternOfPos (0x003bdf7c): quaternion-local +Z, -Z,
        // -X, +X, in that order, choosing the largest dot product.
        const assets::Vector3 delta{target.x - enemy.position.x,
                                    target.y - enemy.position.y,
                                    target.z - enemy.position.z};
        const std::array<float, 4> scores{
            delta.x * enemy.worldTransform[8] +
                delta.y * enemy.worldTransform[9] +
                delta.z * enemy.worldTransform[10],
            -delta.x * enemy.worldTransform[8] -
                delta.y * enemy.worldTransform[9] -
                delta.z * enemy.worldTransform[10],
            -delta.x * enemy.worldTransform[0] -
                delta.y * enemy.worldTransform[1] -
                delta.z * enemy.worldTransform[2],
            delta.x * enemy.worldTransform[0] +
                delta.y * enemy.worldTransform[1] +
                delta.z * enemy.worldTransform[2]};
        return static_cast<std::int32_t>(
            std::max_element(scores.begin(), scores.end()) - scores.begin());
    };
    const auto& behaviorStates = level_->enemyBehaviorConfigs().states();
    const auto selectBehavior = [&](std::int32_t id) {
        const auto state = std::find_if(behaviorStates.begin(),
            behaviorStates.end(), [id](const auto& value) {
                return value.id == id;
            });
        if (state == behaviorStates.end()) {
            return;
        }
        selectStateAnimation(enemy, state->name, state->looping);
        queueStateSound(enemy, state->name);
    };
    if (playerOnWall) {
        // EnvironmentCheck (0x003341f4) aligns the wall normal opposite the
        // player's face direction; SetState copies that face, not a ground
        // look-at rotation toward the target's XY coordinates.
        enemy.wallNormal = {-playerFacing.x, -playerFacing.y, -playerFacing.z};
        enemy.wallAttached = true;
        enemy.physicsActive = true;
        setFacing(enemy, playerFacing);
    }
    if (!enemy.wallAttached) {
        // An authored actor can start beside a gap in the climbable query
        // mesh. Native EnvironmentCheck supplies the player's wall axis
        // once climbing begins; do not turn a failed CheckWall into falling.
        idle();
        return;
    }
    assets::Vector3 destination = playerPosition;
    const assets::Vector3 delta{playerPosition.x - enemy.position.x,
                                playerPosition.y - enemy.position.y,
                                playerPosition.z - enemy.position.z};
    const float distanceSquared = delta.x * delta.x + delta.y * delta.y +
                                  delta.z * delta.z;
    if (!playerOnWall) {
        // UpdateAI (0x00335aa0) parks actors >500 cm above the ground;
        // Move (0x00331780) requests +200 cm Z for the lower wall actors.
        float ground = 0.0F;
        const bool nearGround = collision != nullptr &&
            collision->groundHeight(enemy.position, 5.0F, 1000.0F, ground) &&
            enemy.position.z - ground <= 500.0F;
        if (!nearGround && enemy.wallBehaviorState == 0) {
            idle();
            enemy.physicsActive = false;
            return;
        }
        enemy.physicsActive = true;
        destination = enemy.position;
        destination.z += 200.0F;
    } else if (distanceSquared > 500.0F * 500.0F) {
        enemy.playerDetected = false;
        idle();
        return;
    } else {
        enemy.playerDetected = true;
        // CEnemy::Init halves the SQUARED melee distances for wall actors.
        const float attackRangeSquared =
            attributes->maximumMeleeAttackDistance *
            attributes->maximumMeleeAttackDistance * 0.5F;
        if (enemy.meleeAttackActive) {
            const auto& bank =
                level_->enemyArchetypes()[enemy.asset->archetypeIndex]
                    .animationBank;
            const auto* clip = bank.findClip(enemy.activeAnimation);
            if (clip != nullptr && enemy.animationTimeMilliseconds <
                                       clip->durationMilliseconds()) {
                return;
            }
            idle();
            enemy.wallIdleMilliseconds = 1000;
            unregisterMeleeEngager(enemy.asset->objectId);
        }
        if (enemy.wallIdleMilliseconds != 0) {
            enemy.wallIdleMilliseconds =
                enemy.wallIdleMilliseconds > elapsedMilliseconds
                    ? enemy.wallIdleMilliseconds - elapsedMilliseconds : 0;
            return;
        }
        if (distanceSquared <= attackRangeSquared) {
            idle();
            if (registerMeleeEngager(enemy)) {
                // MeleeAttack::StateEnter (0x003baef8) selects directional
                // wall attack state 12..15 (then optional 16..19 variants).
                const auto pattern = bodyPattern(playerPosition);
                selectBehavior(12 + pattern);
                const float sign = (pattern & 1) == 0 ? 1.0F : -1.0F;
                const std::size_t axis = pattern < 2 ? 8U : 0U;
                const float localSign = pattern < 2 ? sign : -sign;
                enemy.wallAttackDirection = {
                    enemy.worldTransform[axis] * localSign,
                    enemy.worldTransform[axis + 1] * localSign,
                    enemy.worldTransform[axis + 2] * localSign};
                enemy.behavior = EnemyBehaviorState::AttackRange;
                enemy.meleeAttackActive = true;
            }
            return;
        }
    }
    if (enemy.asset->immobile) {
        idle();
        return;
    }
    const assets::Vector3 normal = enemy.wallNormal;
    if (enemy.wallBehaviorState == 0) {
        assets::Vector3 movement{destination.x - enemy.position.x,
                                 destination.y - enemy.position.y,
                                 destination.z - enemy.position.z};
        const float alongNormal = movement.x * normal.x +
            movement.y * normal.y + movement.z * normal.z;
        movement.x -= alongNormal * normal.x;
        movement.y -= alongNormal * normal.y;
        movement.z -= alongNormal * normal.z;
        const float distance = std::sqrt(movement.x * movement.x +
            movement.y * movement.y + movement.z * movement.z);
        if (distance < 0.0001F) {
            idle();
            return;
        }
        // SetTarget (0x003be8b4) pads short moves to 200 cm and consumes
        // only the first 200 cm subdivision when the full move is >=400 cm.
        const float step = distance >= 400.0F ? 200.0F
                                              : std::max(distance, 200.0F);
        movement.x *= step / distance;
        movement.y *= step / distance;
        movement.z *= step / distance;
        const assets::Vector3 target{enemy.position.x + movement.x,
                                     enemy.position.y + movement.y,
                                     enemy.position.z + movement.z};
        // Unit::CanMoveTo(...,true) (0x003247b0) tests this exclusion mask.
        // In particular the supporting climbable wall is not an obstacle to
        // tangent motion. Do not reuse the ground-navigation projection here.
        if (collision != nullptr && collision->segmentBlocked(
                enemy.position, target, 0xffffffaaU)) {
            idle();
            enemy.wallIdleMilliseconds = 1000;
            return;
        }
        enemy.wallMoveTarget = target;
        enemy.wallBehaviorState = 96 + bodyPattern(target);
        selectBehavior(enemy.wallBehaviorState);
        enemy.behavior = EnemyBehaviorState::Chasing;
    }
    assets::Vector3 remaining{enemy.wallMoveTarget.x - enemy.position.x,
                              enemy.wallMoveTarget.y - enemy.position.y,
                              enemy.wallMoveTarget.z - enemy.position.z};
    const float normalDistance = remaining.x * normal.x +
        remaining.y * normal.y + remaining.z * normal.z;
    remaining.x -= normalDistance * normal.x;
    remaining.y -= normalDistance * normal.y;
    remaining.z -= normalDistance * normal.z;
    const float remainingSquared = remaining.x * remaining.x +
        remaining.y * remaining.y + remaining.z * remaining.z;
    // UpdateMoveToDestination (0x003be134) compares squared distance to 500.
    if (remainingSquared < 500.0F) {
        idle();
        enemy.wallIdleMilliseconds = 1000;
        return;
    }
    const float travel = std::min(std::sqrt(remainingSquared),
        attributes->wallSpeedCentimetersPerMillisecond *
            static_cast<float>(elapsedMilliseconds));
    const float factor = travel / std::sqrt(remainingSquared);
    enemy.position.x += remaining.x * factor;
    enemy.position.y += remaining.y * factor;
    enemy.position.z += remaining.z * factor;
    setFacing(enemy, enemy.facing);
}

void LevelEnemyRuntime::updateGameplay(
    std::uint32_t elapsedMilliseconds,
    const assets::Vector3& playerPosition,
    const LevelCollision* collision,
    bool quickTimeActionPressed,
    const assets::Vector3& playerFacing,
    bool playerOnWall) noexcept {
    updatePlayerWebPellets(elapsedMilliseconds, collision);
    updateGunLines(elapsedMilliseconds, playerPosition, collision);
    updateMolotovs(elapsedMilliseconds, playerPosition, collision);
    updateBoomerangs(elapsedMilliseconds, playerPosition, collision);
    updateThunderclaps(elapsedMilliseconds, playerPosition);
    updateElectroBursts(elapsedMilliseconds);
    updateLandingAnimatedEffects(elapsedMilliseconds);
    if (meleeEngagementCooldownMilliseconds_ > 0.0F) {
        // CAIEntityManager::Update (0x00375090) subtracts the frame delta
        // from the float timer; CanRegisterEntityForMeleeAttack accepts it
        // once it is non-positive, without clamping it to zero.
        meleeEngagementCooldownMilliseconds_ -=
            static_cast<float>(elapsedMilliseconds);
    }
    if (meleeEngagerObjectId_ >= 0) {
        LevelEnemyState* engager = findMutable(meleeEngagerObjectId_);
        if (engager == nullptr || engager->asset == nullptr ||
            !engager->visible || !engager->aiEnabled ||
            engager->health <= 0.0F ||
            engager->behavior == EnemyBehaviorState::Hurt ||
            engager->behavior == EnemyBehaviorState::TiedUp ||
            engager->behavior == EnemyBehaviorState::Dead) {
            unregisterMeleeEngager(meleeEngagerObjectId_);
        }
    }
    for (LevelEnemyState& enemy : states_) {
        if (enemy.asset == nullptr) {
            continue;
        }
        advanceCinematicMotion(enemy, elapsedMilliseconds);
        if (enemy.wallWebCaptured && enemy.health > 0.0F) {
            continue;
        }
        if (!enemy.visible &&
            enemy.robotPhantomTask !=
                RobotPhantomTaskState::ConcealHidden) {
            enemy.behavior = EnemyBehaviorState::Disabled;
            continue;
        }

        // CEnemy::Update (0x00333fd0) calls Unit::UpdatePhysicsWithVisible
        // before behavior work. Cinematic DisableAI/EnableAI at
        // 0x00371f94/0x0037206c independently toggle PhysicsEntity::setActive,
        // so a visible staged actor can remain motion-scripted without gravity.
        if (enemy.onWall && collision != nullptr &&
            !enemy.wallAttached && !enemy.cinematicMotion.active) {
            attachEnemyToWall(enemy, *collision);
        }
        if (enemy.behavior == EnemyBehaviorState::Hurt &&
            enemy.physicsActive && !enemy.onWall &&
            (std::abs(enemy.hurtVelocity.x) >
                 std::numeric_limits<float>::epsilon() ||
             std::abs(enemy.hurtVelocity.y) >
                 std::numeric_limits<float>::epsilon())) {
            const float seconds =
                static_cast<float>(elapsedMilliseconds) / 1000.0F;
            const assets::Vector3 desired{
                enemy.position.x + enemy.hurtVelocity.x * seconds,
                enemy.position.y + enemy.hurtVelocity.y * seconds,
                enemy.position.z};
            if (collision == nullptr) {
                enemy.position = desired;
            } else if (enemy.grounded) {
                assets::Vector3 resolved;
                if (collision->resolveGroundMotion(enemy.position, desired,
                                                   resolved)) {
                    enemy.position = resolved;
                }
            } else {
                float destinationSupport{};
                const bool remainsOverSupport =
                    !enemy.hurtStartedGrounded ||
                    collision->groundHeight(
                        desired, enemy.collisionRadius, 5000.0F,
                        destinationSupport);
                if (remainsOverSupport) {
                    // Keep the sweep start distinct from the output. Passing
                    // enemy.position as both arguments aliases resolveAirMotion's
                    // initial `resolved = desired` assignment and erases the
                    // start side before resolveWalls can detect a crossing.
                    // The native PhysicsEntity used by Unit::UpdateDisplacement
                    // (0x00324df0) retains both endpoints of the cylinder sweep.
                    assets::Vector3 resolved;
                    collision->resolveAirMotion(enemy.position, desired,
                                                resolved);
                    enemy.position = resolved;
                } else {
                    // The native PhysicsEntity is a cylinder. The portable
                    // air query currently sweeps its center point, which can
                    // cross a floor boundary before the body reaches the
                    // arena wall. Retain the last supported XY for actors
                    // launched from the ground so their vertical sweep can
                    // contact that floor instead of falling out of combat.
                    enemy.hurtVelocity.x = 0.0F;
                    enemy.hurtVelocity.y = 0.0F;
                }
            }
            setFacing(enemy, enemy.facing);
        }
        if (collision != nullptr && enemy.physicsActive &&
            (!enemy.onWall || enemy.health <= 0.0F) &&
            enemy.sandmanTask != SandmanBossTaskState::Jump) {
            if (!enemy.supportInitialized) {
                float authoredSupport = 0.0F;
                enemy.supportInitialized = true;
                if (!collision->groundHeight(enemy.position, 5.0F, 5000.0F,
                                             authoredSupport)) {
                    enemy.anchoredWithoutSupport = true;
                    enemy.unsupportedSupportHeight = enemy.position.z;
                }
            }
            if (enemy.anchoredWithoutSupport) {
                float acquiredSupport = 0.0F;
                if (collision->groundHeight(enemy.position, 75.0F, 150.0F,
                                            acquiredSupport)) {
                    enemy.anchoredWithoutSupport = false;
                    enemy.position.z = acquiredSupport;
                } else {
                    enemy.position.z = enemy.unsupportedSupportHeight;
                }
                enemy.verticalVelocity = 0.0F;
                enemy.hurtVelocity.z = 0.0F;
                enemy.grounded = true;
                setFacing(enemy, enemy.facing);
            } else {
                const float seconds =
                    static_cast<float>(elapsedMilliseconds) / 1000.0F;
                float supportHeight = 0.0F;
                const bool alreadySupported =
                    enemy.verticalVelocity <= 0.0F &&
                    // createEnemyPhysics (0x003d8980) places the lower
                    // sphere center one radius above Unit's base. Native
                    // processSphereTriangle (0x003d23cc) resolves an overlap
                    // while that center is on the ground-facing side. The
                    // authored base can therefore start below the floor by
                    // up to its radius (Level 6 actors 41322/41324 do).
                    collision->groundHeight(enemy.position,
                                            enemy.collisionRadius, 5.0F,
                                            supportHeight) &&
                    supportHeight < enemy.position.z + enemy.collisionRadius;
                if (alreadySupported) {
                    enemy.position.z = supportHeight;
                    enemy.verticalVelocity = 0.0F;
                    enemy.hurtVelocity.z = 0.0F;
                    enemy.grounded = true;
                } else {
                    const float nextVelocity =
                        enemy.verticalVelocity -
                        kEnemyGravityCentimetersPerSecondSquared * seconds;
                    assets::Vector3 desired = enemy.position;
                    desired.z += (enemy.verticalVelocity + nextVelocity) *
                                 0.5F * seconds;
                    const float fallDistance =
                        std::max(enemy.position.z - desired.z, 0.0F);
                    const bool crossedSupport = collision->groundHeight(
                        desired, fallDistance + 5.0F, 5.0F, supportHeight);
                    if (crossedSupport &&
                        supportHeight <= enemy.position.z + 5.0F &&
                        desired.z <= supportHeight) {
                        enemy.position.z = supportHeight;
                        enemy.verticalVelocity = 0.0F;
                        enemy.hurtVelocity.z = 0.0F;
                        enemy.grounded = true;
                    } else {
                        enemy.position = desired;
                        enemy.verticalVelocity = nextVelocity;
                        enemy.hurtVelocity.z = nextVelocity;
                        enemy.grounded = false;
                    }
                }
                setFacing(enemy, enemy.facing);
            }
        }
        if (enemy.health <= 0.0F) {
            enemy.behavior = EnemyBehaviorState::Dead;
            continue;
        }
        if (enemy.behavior == EnemyBehaviorState::TiedUp) {
            if (enemy.tiedUpRemainingMilliseconds > elapsedMilliseconds) {
                enemy.tiedUpRemainingMilliseconds -= elapsedMilliseconds;
                continue;
            }
            enemy.tiedUpRemainingMilliseconds = 0;
            enemy.behavior = enemy.aiEnabled ? EnemyBehaviorState::Idle
                                             : EnemyBehaviorState::Disabled;
            enemy.activeAnimation = std::string(idleAnimation(*enemy.asset));
            enemy.animationTimeMilliseconds = 0;
            enemy.animationSpeed = 1.0F;
            enemy.animationLoops = true;
            enemy.animationReversed = false;
        }
        if (enemy.behavior == EnemyBehaviorState::Hurt) {
            const EnemyArchetypeAsset& archetype =
                level_->enemyArchetypes()[enemy.asset->archetypeIndex];
            const assets::ColladaAnimationClip* clip =
                archetype.animationBank.findClip(enemy.activeAnimation);
            const bool clipFinished =
                clip == nullptr ||
                (!enemy.animationLoops &&
                 enemy.animationTimeMilliseconds >=
                     clip->durationMilliseconds());
            std::int16_t nextHurtState = -1;
            switch (enemy.hurtStateId) {
            case 53: // idle_to_flying
                if (clipFinished) nextHurtState = 54;
                break;
            case 54: // hurt_to_flying loop
                if (enemy.grounded) nextHurtState = 55;
                break;
            case 55: // flying_to_ground
                if (clipFinished) nextHurtState = 59;
                break;
            case 57: // idle_to_air
                if (clipFinished) nextHurtState = 58;
                break;
            case 58: // falling loop
                if (enemy.grounded) nextHurtState = 59;
                break;
            case 60: // airborne common hurt
                if (clipFinished) nextHurtState = enemy.grounded ? 59 : 58;
                break;
            case 61: // air_to_flying
                if (clipFinished) nextHurtState = 62;
                break;
            case 62: // knockback_to_flying loop
                if (enemy.grounded) nextHurtState = 63;
                break;
            case 63: // knockback_to_ground
                if (clipFinished) nextHurtState = 59;
                break;
            case 64: // air_dragto
            case 65: // heavy blow to ground
                if (clipFinished) nextHurtState = enemy.grounded ? 59 : 58;
                break;
            case 69: // air kickdown
                if (enemy.grounded) {
                    // CBehaviorHurt::BehaviorUpdate state 0x45
                    // (0x003b8d2a-0x003b8e80): start the exact camera shake,
                    // throw both animated models and rock_splash at the
                    // manifold point, dispatch sound 0x4b, then enter 0x46.
                    // The portable capsule base has already been snapped to
                    // the supporting manifold height above.
                    pendingCameraShakeCues_.push_back(
                        {3.0F, 12, {1.0F, 1.0F, 1.0F}});
                    landingAnimatedEffects_.push_back(
                        {EnemyLandingAnimatedEffectKind::Shockwave,
                         enemy.asset->objectId, enemy.asset->roomId,
                         enemy.position, 1.5F, 1000, 0, true, true});
                    pendingLandingAnimatedEffectSpawnEvents_.push_back(
                        {EnemyLandingAnimatedEffectKind::Shockwave,
                         enemy.asset->objectId, enemy.asset->roomId,
                         enemy.position, 1.5F, 1000, true});
                    pendingEffectCues_.push_back(
                        {enemy.asset->objectId, enemy.asset->roomId,
                         enemy.position, "rock_splash"});
                    landingAnimatedEffects_.push_back(
                        {EnemyLandingAnimatedEffectKind::CrashWall,
                         enemy.asset->objectId, enemy.asset->roomId,
                         enemy.position, 5.0F, 5000, 0, false, true});
                    pendingLandingAnimatedEffectSpawnEvents_.push_back(
                        {EnemyLandingAnimatedEffectKind::CrashWall,
                         enemy.asset->objectId, enemy.asset->roomId,
                         enemy.position, 5.0F, 5000, false});
                    pendingSoundCues_.push_back(
                        {enemy.asset->objectId, 0x4b});
                    nextHurtState = 70;
                }
                break;
            case 70: // ground bounce
                if (clipFinished) nextHurtState = enemy.grounded ? 59 : 58;
                break;
            default:
                if (clipFinished) nextHurtState = 0;
                break;
            }
            if (nextHurtState > 0 &&
                !(nextHurtState == 59 &&
                  !hasHurtStateAnimation(enemy, 59))) {
                enterHurtState(enemy, nextHurtState);
                continue;
            }
            if (nextHurtState == 0 || nextHurtState == 59) {
                enemy.behavior = EnemyBehaviorState::Idle;
                enemy.hurtStateId = -1;
                enemy.hurtVelocity = {};
                enemy.hurtStartedGrounded = false;
                enemy.verticalVelocity = 0.0F;
                enemy.activeAnimation =
                    std::string(idleAnimation(*enemy.asset));
                enemy.animationTimeMilliseconds = 0;
                enemy.animationSpeed = 1.0F;
                enemy.animationLoops = true;
                enemy.animationReversed = false;
                enemy.meleeAttackActive = false;
                continue;
            }
            continue;
        }
        if (enemy.onWall && enemy.aiEnabled) {
            updateWallEnemy(enemy, elapsedMilliseconds, playerPosition,
                             playerFacing, playerOnWall, collision);
            continue;
        }
        const bool authoredAirborneMelee =
            enemy.behavior == EnemyBehaviorState::AttackRange &&
            enemy.meleeAttackActive;
        if (collision != nullptr && enemy.physicsActive && !enemy.grounded &&
            !authoredAirborneMelee) {
            enemy.behavior = EnemyBehaviorState::Idle;
            continue;
        }
        if (!enemy.aiEnabled) {
            enemy.behavior = EnemyBehaviorState::Disabled;
            continue;
        }

        const float toPlayerX = playerPosition.x - enemy.position.x;
        const float toPlayerY = playerPosition.y - enemy.position.y;
        const float distanceSquared =
            toPlayerX * toPlayerX + toPlayerY * toPlayerY;
        const float awarenessRadius = enemy.asset->awarenessRadius;
        if (!enemy.playerDetected && awarenessRadius > 0.0F &&
            distanceSquared <= awarenessRadius * awarenessRadius) {
            enemy.playerDetected = true;
        }
        if (!enemy.playerDetected) {
            enemy.behavior = EnemyBehaviorState::Idle;
            continue;
        }

        if (enemy.asset->gameType == "Boss_Sandman") {
            updateSandmanBoss(enemy, elapsedMilliseconds, playerPosition,
                              collision);
            continue;
        }
        if (enemy.asset->gameType == "Boss_Rhino") {
            updateRhinoBoss(enemy, elapsedMilliseconds, playerPosition,
                            collision, quickTimeActionPressed);
            continue;
        }
        if (enemy.asset->gameType == "Robot_Phantom") {
            updateRobotPhantomBoss(enemy, elapsedMilliseconds,
                                   playerPosition, playerFacing, collision);
            continue;
        }
        if (isElectroBoss(enemy)) {
            updateElectroBoss(enemy, elapsedMilliseconds, playerPosition,
                              collision);
            continue;
        }
        if (enemy.asset->gameType.starts_with("Boss_")) {
            // CLevel::LoadNextObject (0x003853fc) routes these through CBoss,
            // not CEnemy's generic thug behavior. Their meshes, animation,
            // cinematic transforms, visibility, and AI gates are already
            // reconstructed; do not invent a thug attack fallback while the
            // corresponding CBoss task graph remains outstanding.
            enemy.behavior = EnemyBehaviorState::Idle;
            continue;
        }

        const float attackRange = maximumAttackReach(enemy);
        const assets::Vector3 enemyAttackOrigin{
            enemy.position.x, enemy.position.y,
            enemy.position.z + enemy.collisionHeight * 0.5F};
        const assets::Vector3 playerAttackTarget{
            playerPosition.x, playerPosition.y, playerPosition.z + 75.0F};
        const bool attackPathBlocked =
            collision != nullptr &&
            collision->segmentBlocked(enemyAttackOrigin, playerAttackTarget);
        if (attackRange > 0.0F &&
            distanceSquared <= attackRange * attackRange &&
            !attackPathBlocked) {
            const EnemyBehaviorState previousBehavior = enemy.behavior;
            enemy.behavior = EnemyBehaviorState::AttackRange;
            const float distance = std::sqrt(distanceSquared);
            if (distance > std::numeric_limits<float>::epsilon()) {
                setFacing(enemy, {toPlayerX / distance, toPlayerY / distance,
                                  0.0F});
            }
            if (isGunLineEnemy(enemy)) {
                if (previousBehavior != EnemyBehaviorState::AttackRange) {
                    enemy.rangeAttackCooldownMilliseconds = 0;
                    startGunLineAttack(enemy);
                } else {
                    const EnemyArchetypeAsset& archetype =
                        level_->enemyArchetypes()[enemy.asset->archetypeIndex];
                    const assets::ColladaAnimationClip* clip =
                        archetype.animationBank.findClip(enemy.activeAnimation);
                    if (clip != nullptr && !enemy.animationLoops &&
                        enemy.animationTimeMilliseconds >=
                            clip->durationMilliseconds()) {
                        enemy.activeAnimation =
                            std::string(idleAnimation(*enemy.asset));
                        enemy.animationTimeMilliseconds = 0;
                        enemy.animationSpeed = 1.0F;
                        enemy.animationLoops = true;
                        enemy.animationReversed = false;
                        const auto* interval =
                            level_->enemyAttackIntervalConfigs()
                                .findForWeaponType(13);
                        enemy.rangeAttackCooldownMilliseconds =
                            interval == nullptr
                                ? 0U
                                : static_cast<std::uint32_t>(std::max(
                                      interval->intervalMilliseconds
                                          [enemy.asset->enemyTypeId],
                                      0.0F));
                    }
                    if (enemy.activeAnimation == idleAnimation(*enemy.asset)) {
                        if (enemy.rangeAttackCooldownMilliseconds <=
                            elapsedMilliseconds) {
                            enemy.rangeAttackCooldownMilliseconds = 0;
                            startGunLineAttack(enemy);
                        } else {
                            enemy.rangeAttackCooldownMilliseconds -=
                                elapsedMilliseconds;
                        }
                    }
                }
                continue;
            }
            if (isMolotovEnemy(enemy)) {
                if (previousBehavior != EnemyBehaviorState::AttackRange) {
                    enemy.rangeAttackCooldownMilliseconds = 0;
                    startMolotovAttack(enemy);
                } else {
                    const EnemyArchetypeAsset& archetype =
                        level_->enemyArchetypes()[enemy.asset->archetypeIndex];
                    const assets::ColladaAnimationClip* clip =
                        archetype.animationBank.findClip(enemy.activeAnimation);
                    if (clip != nullptr && !enemy.animationLoops &&
                        enemy.animationTimeMilliseconds >=
                            clip->durationMilliseconds()) {
                        enemy.activeAnimation =
                            std::string(idleAnimation(*enemy.asset));
                        enemy.animationTimeMilliseconds = 0;
                        enemy.animationSpeed = 1.0F;
                        enemy.animationLoops = true;
                        enemy.animationReversed = false;
                        const auto* interval =
                            level_->enemyAttackIntervalConfigs()
                                .findForWeaponType(5);
                        enemy.rangeAttackCooldownMilliseconds =
                            interval == nullptr
                                ? 0U
                                : static_cast<std::uint32_t>(std::max(
                                      interval->intervalMilliseconds
                                          [enemy.asset->enemyTypeId],
                                      0.0F));
                    }
                    if (enemy.activeAnimation == idleAnimation(*enemy.asset)) {
                        if (enemy.rangeAttackCooldownMilliseconds <=
                            elapsedMilliseconds) {
                            enemy.rangeAttackCooldownMilliseconds = 0;
                            startMolotovAttack(enemy);
                        } else {
                            enemy.rangeAttackCooldownMilliseconds -=
                                elapsedMilliseconds;
                        }
                    }
                }
                continue;
            }
            (void)registerMeleeEngager(enemy);
            if (meleeEngagerObjectId_ != enemy.asset->objectId) {
                enemy.meleeAttackRegistered = false;
                enemy.meleeAttackActive = false;
                if (enemy.activeAnimation != idleAnimation(*enemy.asset)) {
                    enemy.activeAnimation =
                        std::string(idleAnimation(*enemy.asset));
                    enemy.animationTimeMilliseconds = 0;
                    enemy.animationSpeed = 1.0F;
                    enemy.animationLoops = true;
                    enemy.animationReversed = false;
                }
                continue;
            }
            if (previousBehavior != EnemyBehaviorState::AttackRange) {
                enemy.meleeAttackCooldownMilliseconds = 0;
                startMeleeAttack(enemy, playerPosition);
            } else if (enemy.meleeAttackActive) {
                const EnemyArchetypeAsset& archetype =
                    level_->enemyArchetypes()[enemy.asset->archetypeIndex];
                const assets::ColladaAnimationClip* clip =
                    archetype.animationBank.findClip(enemy.activeAnimation);
                if (clip != nullptr && !enemy.animationLoops &&
                    enemy.animationTimeMilliseconds >=
                        clip->durationMilliseconds()) {
                    enemy.meleeAttackActive = false;
                    enemy.activeAnimation =
                        std::string(idleAnimation(*enemy.asset));
                    enemy.animationTimeMilliseconds = 0;
                    enemy.animationSpeed = 1.0F;
                    enemy.animationLoops = true;
                    enemy.animationReversed = false;
                    // GetAttackType/GetAttackIntervalTime at
                    // 0x0033ae04/0x0033ade0 resolve the authored common melee
                    // map (100). All first-level melee variants carry the same
                    // interval values, while Sandman intentionally uses zero.
                    const auto* interval =
                        level_->enemyAttackIntervalConfigs()
                            .findByWeaponTypeMapIndex(100);
                    enemy.meleeAttackCooldownMilliseconds =
                        interval == nullptr
                            ? 0U
                            : static_cast<std::uint32_t>(std::max(
                                  interval->intervalMilliseconds
                                      [enemy.asset->enemyTypeId],
                                  0.0F));
                    unregisterMeleeEngager(enemy.asset->objectId);
                    continue;
                }
            }
            if (!enemy.meleeAttackActive) {
                if (enemy.meleeAttackCooldownMilliseconds <=
                    elapsedMilliseconds) {
                    enemy.meleeAttackCooldownMilliseconds = 0;
                    startMeleeAttack(enemy, playerPosition);
                } else {
                    enemy.meleeAttackCooldownMilliseconds -=
                        elapsedMilliseconds;
                }
            }
            continue;
        }

        const float distance = std::sqrt(distanceSquared);
        if (distance <= std::numeric_limits<float>::epsilon()) {
            continue;
        }
        const float inverseDistance = 1.0F / distance;
        if (meleeEngagerObjectId_ == enemy.asset->objectId &&
            attackRange > 0.0F && distance > attackRange * 1.5F) {
            unregisterMeleeEngager(enemy.asset->objectId);
        }
        const assets::Vector3 facing{toPlayerX * inverseDistance,
                                     toPlayerY * inverseDistance, 0.0F};
        const float maximumTravel =
            (enemy.asset->immobile ? 0.0F :
             enemy.asset->lineSpeedCentimetersPerMillisecond) *
            static_cast<float>(elapsedMilliseconds);
        // A ranged enemy inside its nominal attack radius can still have its
        // shot occluded.  In that case it must continue closing the route;
        // subtracting the attack radius produced a negative travel distance
        // and launched Room 8 gunman 421 backwards across the first rooftop.
        const float stoppingDistance =
            attackPathBlocked ? 0.0F : attackRange;
        const float travel = std::min(
            maximumTravel, std::max(distance - stoppingDistance, 0.0F));
        assets::Vector3 desired = enemy.position;
        desired.x += facing.x * travel;
        desired.y += facing.y * travel;
        if (collision != nullptr) {
            assets::Vector3 resolved;
            if (enemy.anchoredWithoutSupport) {
                // An active native PhysicsEntity is not horizontally pinned
                // merely because its authored spawn has no ground triangle
                // beneath it. Room 4 actor 417 begins just outside the street
                // collision and must chase onto it. Preserve wall resolution
                // and the authored support height during that ingress, then
                // resume ordinary grounded motion as soon as support exists.
                collision->resolveAirMotion(enemy.position, desired, resolved);
                float acquiredSupport = 0.0F;
                if (collision->groundHeight(resolved, 75.0F, 150.0F,
                                            acquiredSupport)) {
                    resolved.z = acquiredSupport;
                    enemy.anchoredWithoutSupport = false;
                    enemy.grounded = true;
                } else {
                    resolved.z = enemy.unsupportedSupportHeight;
                }
            } else {
                (void)collision->resolveGroundMotion(enemy.position, desired,
                                                     resolved);
            }
            enemy.position = resolved;
        } else {
            enemy.position = desired;
        }
        setFacing(enemy, facing);
        enemy.behavior = EnemyBehaviorState::Chasing;
        enemy.meleeAttackActive = false;
        enemy.meleeAttackCooldownMilliseconds = 0;
        const std::string_view movementAnimation = chaseAnimation(*enemy.asset);
        const EnemyArchetypeAsset& movementArchetype =
            level_->enemyArchetypes()[enemy.asset->archetypeIndex];
        const std::string_view resolvedMovementAnimation =
            movementArchetype.animationBank.findClip(movementAnimation) !=
                    nullptr
                ? movementAnimation
                : idleAnimation(*enemy.asset);
        if (enemy.activeAnimation != resolvedMovementAnimation) {
            enemy.activeAnimation = resolvedMovementAnimation;
            enemy.animationTimeMilliseconds = 0;
            enemy.animationSpeed = 1.0F;
            enemy.animationLoops = true;
            enemy.animationReversed = false;
        }
    }
    resolveEnemyContacts(collision);
    std::vector<std::uint32_t> previousAnimationTimes;
    std::vector<std::string> previousAnimationNames;
    previousAnimationTimes.reserve(states_.size());
    previousAnimationNames.reserve(states_.size());
    for (const LevelEnemyState& enemy : states_) {
        previousAnimationTimes.push_back(enemy.animationTimeMilliseconds);
        previousAnimationNames.push_back(enemy.activeAnimation);
    }
    advanceAnimations(elapsedMilliseconds, collision);
    for (std::size_t index = 0; index < states_.size(); ++index) {
        if (states_[index].behavior == EnemyBehaviorState::AttackRange &&
            (states_[index].meleeAttackActive ||
             isGunLineEnemy(states_[index]) ||
             isMolotovEnemy(states_[index]) ||
             states_[index].rhinoTask != RhinoBossTaskState::None ||
             states_[index].robotPhantomTask !=
                 RobotPhantomTaskState::None)) {
            queueAuthoredAttackEvents(states_[index],
                                      states_[index].activeAnimation ==
                                              previousAnimationNames[index]
                                          ? previousAnimationTimes[index]
                                          : 0U,
                                      playerPosition);
        }
    }
}

bool LevelEnemyRuntime::launchPlayerWebPellet(
    const assets::Vector3& origin,
    const assets::Vector3& targetPosition,
    std::int32_t targetedEnemyObjectId,
    float damage) noexcept {
    if (damage <= 0.0F || !std::isfinite(damage) ||
        playerWebPellets_.size() >= 8) {
        return false;
    }
    const assets::Vector3 toTarget{targetPosition.x - origin.x,
                                   targetPosition.y - origin.y,
                                   targetPosition.z - origin.z};
    const float length = std::sqrt(toTarget.x * toTarget.x +
                                   toTarget.y * toTarget.y +
                                   toTarget.z * toTarget.z);
    if (length <= std::numeric_limits<float>::epsilon() ||
        !std::isfinite(length)) {
        return false;
    }
    const assets::Vector3 direction{toTarget.x / length,
                                    toTarget.y / length,
                                    toTarget.z / length};
    const assets::Vector3 position{
        origin.x + direction.x * kWebPelletLaunchOffsetCentimeters,
        origin.y + direction.y * kWebPelletLaunchOffsetCentimeters,
        origin.z + direction.z * kWebPelletLaunchOffsetCentimeters};
    playerWebPellets_.push_back(
        {targetedEnemyObjectId,
         position,
         {direction.x * kWebPelletSpeedCentimetersPerSecond,
          direction.y * kWebPelletSpeedCentimetersPerSecond,
          direction.z * kWebPelletSpeedCentimetersPerSecond},
         damage,
         0.0F,
         true});
    pendingPlayerWebPelletEvents_.push_back(
        {PlayerWebPelletEventKind::Spawned, targetedEnemyObjectId, -1,
         position, damage, 0.0F});
    return true;
}

void LevelEnemyRuntime::updatePlayerWebPellets(
    std::uint32_t elapsedMilliseconds,
    const LevelCollision* collision) noexcept {
    const float seconds =
        static_cast<float>(elapsedMilliseconds) / 1000.0F;
    for (PlayerWebPelletState& pellet : playerWebPellets_) {
        if (!pellet.active) {
            continue;
        }
        const float remaining = std::max(
            kWebPelletMaximumTravelCentimeters - pellet.traveledCentimeters,
            0.0F);
        const float requestedTravel =
            kWebPelletSpeedCentimetersPerSecond * seconds;
        const float travel = std::min(requestedTravel, remaining);
        const assets::Vector3 previous = pellet.position;
        const assets::Vector3 desired{
            previous.x + pellet.velocity.x *
                             (travel / kWebPelletSpeedCentimetersPerSecond),
            previous.y + pellet.velocity.y *
                             (travel / kWebPelletSpeedCentimetersPerSecond),
            previous.z + pellet.velocity.z *
                             (travel / kWebPelletSpeedCentimetersPerSecond)};

        float firstFraction = 1.0F;
        bool staticContact = false;
        if (collision != nullptr) {
            const auto hit = collision->segmentFirstHit(previous, desired);
            if (hit) {
                firstFraction = hit->segmentFraction;
                staticContact = true;
            }
        }
        LevelEnemyState* hitEnemy = nullptr;
        for (LevelEnemyState& enemy : states_) {
            if (enemy.asset == nullptr || !enemy.visible ||
                !enemy.physicsActive || enemy.health <= 0.0F) {
                continue;
            }
            const auto fraction = segmentExpandedCylinderHitFraction(
                previous, desired, enemy.position, enemy.collisionRadius,
                enemy.collisionHeight, kWebPelletRadiusCentimeters);
            if (fraction && *fraction <= firstFraction) {
                firstFraction = *fraction;
                staticContact = false;
                hitEnemy = &enemy;
            }
        }
        pellet.position = {
            previous.x + (desired.x - previous.x) * firstFraction,
            previous.y + (desired.y - previous.y) * firstFraction,
            previous.z + (desired.z - previous.z) * firstFraction};
        pellet.traveledCentimeters += travel * firstFraction;
        if (hitEnemy != nullptr) {
            const auto result = applyPlayerWebBindingDetailed(
                hitEnemy->asset->objectId, pellet.damage);
            pendingPlayerWebPelletEvents_.push_back(
                {PlayerWebPelletEventKind::EnemyContact,
                 pellet.targetedEnemyObjectId,
                 hitEnemy->asset->objectId,
                 pellet.position,
                 pellet.damage,
                 result ? result->actualDamage : 0.0F});
            pellet.active = false;
        } else if (staticContact) {
            pendingPlayerWebPelletEvents_.push_back(
                {PlayerWebPelletEventKind::StaticContact,
                 pellet.targetedEnemyObjectId, -1, pellet.position,
                 pellet.damage, 0.0F});
            pellet.active = false;
        } else if (travel >= remaining) {
            pendingPlayerWebPelletEvents_.push_back(
                {PlayerWebPelletEventKind::Expired,
                 pellet.targetedEnemyObjectId, -1, pellet.position,
                 pellet.damage, 0.0F});
            pellet.active = false;
        }
    }
    std::erase_if(playerWebPellets_, [](const PlayerWebPelletState& pellet) {
        return !pellet.active;
    });
}

void LevelEnemyRuntime::resolveEnemyContacts(
    const LevelCollision* collision) noexcept {
    std::vector<EnemySeparationEvent> frameEvents;
    for (std::uint32_t pass = 0; pass < 4; ++pass) {
        for (std::size_t firstIndex = 0; firstIndex < states_.size();
             ++firstIndex) {
            LevelEnemyState& first = states_[firstIndex];
            if (first.asset == nullptr || !first.visible ||
                !first.physicsActive || first.health <= 0.0F) {
                continue;
            }
            for (std::size_t secondIndex = firstIndex + 1;
                 secondIndex < states_.size(); ++secondIndex) {
                LevelEnemyState& second = states_[secondIndex];
                if (second.asset == nullptr || !second.visible ||
                    !second.physicsActive || second.health <= 0.0F ||
                    first.position.z + first.collisionHeight <
                        second.position.z ||
                    second.position.z + second.collisionHeight <
                        first.position.z) {
                    continue;
                }
                float dx = second.position.x - first.position.x;
                float dy = second.position.y - first.position.y;
                float distance = std::hypot(dx, dy);
                const float required =
                    first.collisionRadius + second.collisionRadius;
                if (distance >= required - 0.01F) {
                    continue;
                }
                if (std::none_of(
                        frameEvents.begin(), frameEvents.end(),
                        [&](const EnemySeparationEvent& event) {
                            return event.firstObjectId ==
                                       first.asset->objectId &&
                                   event.secondObjectId ==
                                       second.asset->objectId;
                        })) {
                    frameEvents.push_back(
                        {first.asset->objectId, second.asset->objectId,
                         distance, distance, required});
                }
                if (distance <= std::numeric_limits<float>::epsilon()) {
                    const std::uint32_t seed =
                        static_cast<std::uint32_t>(first.asset->objectId) *
                            1664525U +
                        static_cast<std::uint32_t>(second.asset->objectId) *
                            1013904223U;
                    const float angle = static_cast<float>(seed % 360U) /
                                        kRadiansToDegrees;
                    dx = std::cos(angle);
                    dy = std::sin(angle);
                    distance = 1.0F;
                }
                const float correction =
                    (required - distance + 0.01F) * 0.5F / distance;
                const assets::Vector3 firstDesired{
                    first.position.x - dx * correction,
                    first.position.y - dy * correction,
                    first.position.z};
                const assets::Vector3 secondDesired{
                    second.position.x + dx * correction,
                    second.position.y + dy * correction,
                    second.position.z};
                auto moveEnemy = [collision](LevelEnemyState& enemy,
                                             const assets::Vector3& desired) {
                    if (collision == nullptr) {
                        enemy.position = desired;
                    } else if (enemy.grounded &&
                               !enemy.anchoredWithoutSupport) {
                        assets::Vector3 resolved;
                        (void)collision->resolveGroundMotion(
                            enemy.position, desired, resolved);
                        enemy.position = resolved;
                    } else {
                        assets::Vector3 resolved;
                        collision->resolveAirMotion(enemy.position, desired,
                                                    resolved);
                        enemy.position = resolved;
                    }
                    setFacing(enemy, enemy.facing);
                };
                moveEnemy(first, firstDesired);
                moveEnemy(second, secondDesired);
            }
        }
    }
    for (EnemySeparationEvent& event : frameEvents) {
        const LevelEnemyState* first = find(event.firstObjectId);
        const LevelEnemyState* second = find(event.secondObjectId);
        if (first == nullptr || second == nullptr) {
            continue;
        }
        event.distanceAfter = std::hypot(second->position.x - first->position.x,
                                         second->position.y - first->position.y);
        pendingEnemySeparationEvents_.push_back(event);
    }
}

std::optional<std::int32_t> LevelEnemyRuntime::applyPlayerMeleeHit(
    const assets::Vector3& attackPosition,
    const assets::Vector3& attackDirection, float radius, float damage,
    float minimumForwardDot) noexcept {
    const std::optional<PlayerMeleeHitResult> result =
        applyPlayerMeleeHitDetailed(attackPosition, attackDirection, radius,
                                    damage, minimumForwardDot);
    return result ? std::optional<std::int32_t>{result->objectId}
                  : std::nullopt;
}

std::optional<PlayerMeleeHitResult>
LevelEnemyRuntime::applyPlayerMeleeHitDetailed(
    const assets::Vector3& attackPosition,
    const assets::Vector3& attackDirection, float radius, float damage,
    float minimumForwardDot, std::int16_t hitType,
    const assets::Vector3* sourcePosition, float horizontalForce,
    float verticalForce) noexcept {
    if (radius <= 0.0F || damage <= 0.0F) {
        return std::nullopt;
    }
    LevelEnemyState* nearest = nullptr;
    float nearestDistanceSquared = std::numeric_limits<float>::max();
    const float halfAngleDegrees =
        std::acos(std::clamp(minimumForwardDot, -1.0F, 1.0F)) *
        kRadiansToDegrees;
    for (LevelEnemyState& enemy : states_) {
        if (enemy.asset == nullptr || !enemy.visible || enemy.health <= 0.0F) {
            continue;
        }
        const float x = enemy.position.x - attackPosition.x;
        const float y = enemy.position.y - attackPosition.y;
        const float distanceSquared = x * x + y * y;
        if (nativePlayerSectorIntersects(
                attackPosition, attackDirection, radius,
                -halfAngleDegrees, halfAngleDegrees, enemy) &&
            distanceSquared <= nearestDistanceSquared) {
            nearest = &enemy;
            nearestDistanceSquared = distanceSquared;
        }
    }
    if (nearest == nullptr) {
        return std::nullopt;
    }
    const float healthBefore = nearest->health;
    const assets::Vector3* effectiveSource =
        sourcePosition != nullptr ? sourcePosition : &attackPosition;
    verticalForce *= kPlayerHitVerticalForceDispatchScale;
    applyCombatDamage(*nearest, damage, hitType, effectiveSource,
                      horizontalForce, verticalForce);
    // Player::SendHitMessage (0x00345fe8, instructions 0x00346090-
    // 0x003460da) samples the target's health before and after dispatch,
    // clamps the delta to zero, and passes that actual damage to AddCombo.
    return PlayerMeleeHitResult{
        nearest->asset->objectId,
        std::max(0.0F, healthBefore - nearest->health)};
}

std::vector<PlayerMeleeHitResult>
LevelEnemyRuntime::applyPlayerSectorMeleeHits(
    const assets::Vector3& attackPosition,
    const assets::Vector3& attackDirection, float radius, float damage,
    float minimumForwardDot, std::int16_t hitType,
    const assets::Vector3* sourcePosition, float horizontalForce,
    float verticalForce) noexcept {
    std::vector<PlayerMeleeHitResult> hits;
    if (!(radius > 0.0F) || !(damage > 0.0F) ||
        !std::isfinite(radius) || !std::isfinite(damage)) {
        return hits;
    }
    const float halfAngleDegrees =
        std::acos(std::clamp(minimumForwardDot, -1.0F, 1.0F)) *
        kRadiansToDegrees;
    const assets::Vector3* effectiveSource =
        sourcePosition != nullptr ? sourcePosition : &attackPosition;
    // Player::SendHitMessage (0x00346168) mutates the shared hit record just
    // before every dispatch, so later recipients receive compounded force.
    float dispatchedVerticalForce = verticalForce;
    for (LevelEnemyState& enemy : states_) {
        if (enemy.asset == nullptr || !enemy.visible || enemy.health <= 0.0F ||
            !nativePlayerSectorIntersects(
                attackPosition, attackDirection, radius,
                -halfAngleDegrees, halfAngleDegrees, enemy)) {
            continue;
        }
        const float healthBefore = enemy.health;
        dispatchedVerticalForce *= kPlayerHitVerticalForceDispatchScale;
        applyCombatDamage(enemy, damage, hitType, effectiveSource,
                          horizontalForce, dispatchedVerticalForce);
        hits.push_back({enemy.asset->objectId,
                        std::max(0.0F, healthBefore - enemy.health)});
    }
    return hits;
}

std::vector<PlayerMeleeHitResult>
LevelEnemyRuntime::applyPlayerAirKickDownSectorMeleeHits(
    const assets::Vector3& attackPosition,
    const assets::Vector3& attackDirection, float radius, float damage,
    float minimumForwardDot, std::int32_t retainedTargetObjectId,
    const assets::Vector3* sourcePosition, float retainedHorizontalForce,
    float retainedVerticalForce) noexcept {
    std::vector<PlayerMeleeHitResult> hits;
    if (!(radius > 0.0F) || !(damage > 0.0F) ||
        !std::isfinite(radius) || !std::isfinite(damage)) {
        return hits;
    }
    const float halfAngleDegrees =
        std::acos(std::clamp(minimumForwardDot, -1.0F, 1.0F)) *
        kRadiansToDegrees;
    const assets::Vector3* effectiveSource =
        sourcePosition != nullptr ? sourcePosition : &attackPosition;
    for (LevelEnemyState& enemy : states_) {
        if (enemy.asset == nullptr || !enemy.visible || enemy.health <= 0.0F ||
            !nativePlayerSectorIntersects(
                attackPosition, attackDirection, radius,
                -halfAngleDegrees, halfAngleDegrees, enemy)) {
            continue;
        }
        const float healthBefore = enemy.health;
        if (enemy.asset->objectId == retainedTargetObjectId) {
            // Player::CheckAttackTarget (0x0034fca0, 0x00350608) calls the
            // scalar SendHitMessage overload for Player+0x594. That overload
            // reuses the ordinary motion-0x6d record and applies its single
            // 0.85 vertical-force dispatch scale.
            applyCombatDamage(
                enemy, damage, 109, effectiveSource,
                retainedHorizontalForce,
                retainedVerticalForce * kPlayerHitVerticalForceDispatchScale);
        } else {
            // The branch at 0x003505ce constructs a second AIHitTargetInfo:
            // hit type 0x79, the same authored damage and player position,
            // horizontal force 200, vertical force 500. At 0x00350600 it is
            // sent through the pointer overload, so its vertical force is
            // not passed through the scalar overload's 0.85 scale.
            applyCombatDamage(enemy, damage, 121, effectiveSource,
                              200.0F, 500.0F);
        }
        hits.push_back({enemy.asset->objectId,
                        std::max(0.0F, healthBefore - enemy.health)});
    }
    return hits;
}

std::vector<PlayerMeleeHitResult>
LevelEnemyRuntime::applyPlayerRadialMeleeHits(
    const assets::Vector3& attackPosition, float radius,
    float damage, std::int16_t hitType, float horizontalForce,
    float verticalForce) noexcept {
    std::vector<PlayerMeleeHitResult> hits;
    if (!(radius > 0.0F) || !(damage > 0.0F) ||
        !std::isfinite(radius) || !std::isfinite(damage)) {
        return hits;
    }
    const assets::Vector3 forward{1.0F, 0.0F, 0.0F};
    float dispatchedVerticalForce = verticalForce;
    for (LevelEnemyState& enemy : states_) {
        if (enemy.asset == nullptr || !enemy.visible ||
            enemy.wallWebCaptured || enemy.health <= 0.0F ||
            !cylinderSectorIntersects(
                attackPosition, kPlayerCollisionHeightCentimeters, forward,
                radius, -180.0F, 180.0F, enemy.position,
                enemy.collisionRadius, enemy.collisionHeight)) {
            continue;
        }
        const float healthBefore = enemy.health;
        dispatchedVerticalForce *= kPlayerHitVerticalForceDispatchScale;
        applyCombatDamage(enemy, damage, hitType, &attackPosition,
                          horizontalForce, dispatchedVerticalForce);
        hits.push_back({enemy.asset->objectId,
                        std::max(0.0F, healthBefore - enemy.health)});
    }
    return hits;
}

std::vector<PlayerMeleeHitResult>
LevelEnemyRuntime::applyPlayerSenseMeleeHits(
    const assets::Vector3& attackPosition,
    const assets::Vector3& attackDirection, float radius, float damage,
    float minimumForwardDot, std::int32_t attackerObjectId,
    std::int16_t hitType, float horizontalForce,
    float verticalForce) noexcept {
    std::vector<PlayerMeleeHitResult> hits;
    if (!(radius > 0.0F) || !(damage > 0.0F) ||
        !std::isfinite(radius) || !std::isfinite(damage)) {
        return hits;
    }
    const float halfAngleDegrees =
        std::acos(std::clamp(minimumForwardDot, -1.0F, 1.0F)) *
        kRadiansToDegrees;
    bool attackerHit = false;
    float dispatchedVerticalForce = verticalForce;
    for (LevelEnemyState& enemy : states_) {
        if (enemy.asset == nullptr || !enemy.visible || enemy.health <= 0.0F ||
            !nativePlayerSectorIntersects(
                attackPosition, attackDirection, radius,
                -halfAngleDegrees, halfAngleDegrees, enemy)) {
            continue;
        }
        const float healthBefore = enemy.health;
        dispatchedVerticalForce *= kPlayerHitVerticalForceDispatchScale;
        applyCombatDamage(enemy, damage, hitType, &attackPosition,
                          horizontalForce, dispatchedVerticalForce);
        hits.push_back({enemy.asset->objectId,
                        std::max(0.0F, healthBefore - enemy.health)});
        attackerHit = attackerHit || enemy.asset->objectId == attackerObjectId;
    }
    if (!attackerHit) {
        const auto guaranteed = applyPlayerTargetedHitDetailed(
            attackerObjectId, damage, hitType, &attackPosition,
            horizontalForce, dispatchedVerticalForce);
        if (guaranteed.has_value()) {
            hits.push_back(*guaranteed);
        }
    }
    return hits;
}

std::optional<PlayerMeleeHitResult>
LevelEnemyRuntime::applyPlayerTargetedHitDetailed(
    std::int32_t objectId, float damage, std::int16_t hitType,
    const assets::Vector3* sourcePosition, float horizontalForce,
    float verticalForce) noexcept {
    LevelEnemyState* enemy = findMutable(objectId);
    if (enemy == nullptr || enemy->asset == nullptr || !enemy->visible ||
        enemy->wallWebCaptured || enemy->health <= 0.0F || damage <= 0.0F || !std::isfinite(damage)) {
        return std::nullopt;
    }
    const float healthBefore = enemy->health;
    verticalForce *= kPlayerHitVerticalForceDispatchScale;
    applyCombatDamage(*enemy, damage, hitType, sourcePosition,
                      horizontalForce, verticalForce);
    return PlayerMeleeHitResult{
        objectId, std::max(0.0F, healthBefore - enemy->health)};
}

std::optional<PlayerMeleeHitResult>
LevelEnemyRuntime::applyPlayerWebBindingDetailed(
    std::int32_t objectId, float damage) noexcept {
    LevelEnemyState* enemy = findMutable(objectId);
    if (enemy == nullptr || enemy->asset == nullptr || !enemy->visible ||
        enemy->wallWebCaptured || enemy->health <= 0.0F || damage < 0.0F || !std::isfinite(damage)) {
        return std::nullopt;
    }
    const float healthBefore = enemy->health;
    if (damage > 0.0F) {
        applyCombatDamage(*enemy, damage);
    }
    if (enemy->health > 0.0F && enemy->canBeTiedUp) {
        // Attack motion 0x6e/0x7b/0x7c becomes message 0x69 in
        // CEnemy::ParseLocalAiMessage (0x00331eb0). CBehaviorTiedUp::
        // onMessage (0x003c7558) enters state 0x23, whose serialized state
        // 35 selects `tied`; StateEnter (0x003c77d0) seeds 4000 ms.
        enemy->behavior = EnemyBehaviorState::TiedUp;
        enemy->tiedUpRemainingMilliseconds = 4000;
        enemy->meleeAttackActive = false;
        enemy->meleeAttackCooldownMilliseconds = 0;
        enemy->rangeAttackCooldownMilliseconds = 0;
        selectStateAnimation(
            *enemy, "ENEMY_BEHAVIOR_TIDE_UP_STATE_TIED", true);
    }
    return PlayerMeleeHitResult{
        objectId, std::max(0.0F, healthBefore - enemy->health)};
}

bool LevelEnemyRuntime::applyPlayerAirKnockdownBinding(
    std::int32_t objectId) noexcept {
    LevelEnemyState* enemy = findMutable(objectId);
    if (enemy == nullptr || enemy->asset == nullptr || !enemy->visible ||
        enemy->wallWebCaptured || enemy->health <= 0.0F ||
        !enemy->canBeTiedUp) {
        return false;
    }
    // Player motion 0x70 sends AIHitTargetInfo type 0x71 with flag 1.
    // CEnemy::ParseLocalAiMessage (0x00331eb0) maps that to message 0x6a;
    // CBehaviorTiedUp::onMessage (0x003c7558) enters 0x24 exactly, and
    // StateEnter (0x003c77d0) seeds the same 4000 ms tied lifetime.
    enemy->behavior = EnemyBehaviorState::TiedUp;
    enemy->tiedUpRemainingMilliseconds = 4000;
    enemy->meleeAttackActive = false;
    enemy->meleeAttackCooldownMilliseconds = 0;
    enemy->rangeAttackCooldownMilliseconds = 0;
    selectStateAnimation(
        *enemy, "ENEMY_BEHAVIOR_TIDE_UP_STATE_TIED_LIE", true);
    return true;
}

bool LevelEnemyRuntime::applyDiagnosticDamage(std::int32_t objectId,
                                              float damage) noexcept {
    LevelEnemyState* enemy = findMutable(objectId);
    if (enemy == nullptr || enemy->asset == nullptr || damage <= 0.0F ||
        !std::isfinite(damage) || enemy->health <= 0.0F) {
        return false;
    }
    applyCombatDamage(*enemy, damage);
    return true;
}

void LevelEnemyRuntime::applyCombatDamage(
    LevelEnemyState& enemy, float damage, std::int16_t hitType,
    const assets::Vector3* sourcePosition, float horizontalForce,
    float verticalForce) noexcept {
    if (enemy.wallWebCaptured) {
        return; // Native local hit message 0x12d is ignored in Unit state 14.
    }
    if (enemy.asset != nullptr &&
        meleeEngagerObjectId_ == enemy.asset->objectId) {
        unregisterMeleeEngager(enemy.asset->objectId);
    }
    enemy.tiedUpRemainingMilliseconds = 0;
    const bool wasRobotPhantomConcealed =
        enemy.robotPhantomTask == RobotPhantomTaskState::ConcealHidden;
    if (enemy.asset->gameType == "Boss_Rhino") {
        applyRhinoDamage(enemy, damage);
    } else if (isElectroBoss(enemy)) {
        enemy.health = std::max(0.0F, enemy.health - damage);
        const std::int32_t healthPercent =
            enemy.maximumHealth > 0.0F
                ? static_cast<std::int32_t>(
                      enemy.health * 100.0F / enemy.maximumHealth)
                : 0;
        // CBoss::ParseLocalAiMessage (0x0032dea8) deliberately clamps each
        // phase boundary so one large hit cannot skip either native queue.
        if (enemy.electroPhase < 1U && healthPercent < 67) {
            enemy.health = enemy.maximumHealth * 0.66F;
            enemy.electroPhase = 1;
        } else if (enemy.electroPhase <= 1U && healthPercent <= 33) {
            enemy.health = enemy.maximumHealth * 0.33F;
            enemy.electroPhase = 2;
        }
    } else {
        enemy.health = std::max(0.0F, enemy.health - damage);
    }
    enemy.playerDetected = true;
    cancelRhinoQuickTimeIfOwned(enemy);
    enemy.sandmanTask = SandmanBossTaskState::None;
    enemy.sandmanJumpElapsedMilliseconds = 0;
    enemy.sandmanJumpDurationMilliseconds = 0;
    enemy.rhinoTask = RhinoBossTaskState::None;
    enemy.rhinoTaskElapsedMilliseconds = 0;
    enemy.rhinoMeleeAttacksRemaining = 0;
    enemy.rhinoSequenceCycle = 0;
    enemy.robotPhantomTask = RobotPhantomTaskState::None;
    enemy.robotPhantomTaskElapsedMilliseconds = 0;
    enemy.robotPhantomSequenceIndex = 0;
    enemy.electroTask = ElectroBossTaskState::None;
    enemy.electroTaskElapsedMilliseconds = 0;
    enemy.electroSequenceIndex = 0;
    enemy.electroRangeAttacksRemaining = 0;
    enemy.electroRangeWaitMilliseconds = 0;
    enemy.electroDashesRemaining = 0;
    enemy.cinematicActionActive = false;
    enemy.cinematicActionObjectId = -1;
    enemy.electroRangeReleased = false;
    enemy.electroWeakReleased = false;
    enemy.electroDashEffectThrown = false;
    enemy.electroDashHitPlayer = false;
    if (wasRobotPhantomConcealed) {
        enemy.visible = true;
    }
    if (enemy.asset != nullptr) {
        std::erase_if(
            boomerangs_, [&enemy](const EnemyBoomerangState& boomerang) {
                return boomerang.sourceObjectId == enemy.asset->objectId;
            });
        std::erase_if(
            thunderclaps_, [&enemy](const EnemyThunderclapState& thunderclap) {
                return thunderclap.sourceObjectId == enemy.asset->objectId;
            });
        removeElectroPosts(enemy.asset->objectId);
    }
    enemy.meleeAttackActive = false;
    if (enemy.health == 0.0F) {
        enterDeadState(enemy);
    } else {
        enemy.behavior = EnemyBehaviorState::Hurt;
        enemy.wallBehaviorState = 0;
        enemy.wallIdleMilliseconds = 1000;
        // CEnemy::ProcessHitInfo normalizes the two player-only wheel hit
        // types before CBehaviorHurt::BehaviorStart selects a reaction.
        if (hitType == 136) hitType = 100;
        if (hitType == 137) hitType = 121;
        if (!enemy.allowsHorizontalHitForce) {
            horizontalForce = 0.0F;
        }
        if (!enemy.allowsVerticalHitForce) {
            verticalForce = 0.0F;
        }
        if (!enemy.allowsLaunchHitType &&
            (hitType == 102 || hitType == 114)) {
            verticalForce = 0.0F;
            hitType = 100;
        }
        enemy.lastPlayerHitType = hitType;
        assets::Vector3 hitAxis{-enemy.facing.x, -enemy.facing.y, 0.0F};
        bool sourceBehind = false;
        if (sourcePosition != nullptr) {
            hitAxis = {enemy.position.x - sourcePosition->x,
                       enemy.position.y - sourcePosition->y, 0.0F};
            const float length = std::hypot(hitAxis.x, hitAxis.y);
            if (length > std::numeric_limits<float>::epsilon()) {
                hitAxis.x /= length;
                hitAxis.y /= length;
            }
            sourceBehind = hitAxis.x * enemy.facing.x +
                               hitAxis.y * enemy.facing.y >
                           0.0F;
        }
        enemy.hurtStartedGrounded =
            enemy.hurtStartedGrounded || enemy.grounded;
        enemy.hurtVelocity = {hitAxis.x * horizontalForce,
                              hitAxis.y * horizontalForce,
                              verticalForce};
        enemy.verticalVelocity = verticalForce;
        if (verticalForce > 0.0F) {
            enemy.grounded = false;
            enemy.anchoredWithoutSupport = false;
        }

        std::int16_t hurtStateId = 49;
        if (enemy.onWall) {
            hurtStateId = 68;
        } else {
            switch (hitType) {
            case 102:
                hurtStateId = 57;
                break;
            case 100:
                hurtStateId = sourceBehind ? 51 : 49;
                break;
            case 121:
                hurtStateId = sourceBehind ? 52 :
                    (hasHurtStateAnimation(enemy, 53) ? 53 : 50);
                break;
            case 105:
                hurtStateId = 60;
                break;
            case 106:
                hurtStateId = 61;
                break;
            case 109:
                hurtStateId = 69;
                break;
            case 114:
                hurtStateId = 64;
                break;
            case 203:
                hurtStateId = 66;
                break;
            case 204:
                hurtStateId = 67;
                break;
            default:
                hurtStateId = 49;
                break;
            }
        }
        if (!hasHurtStateAnimation(enemy, hurtStateId)) {
            hurtStateId = 49;
        }
        enterHurtState(enemy, hurtStateId);
    }
}

std::vector<PlayerMeleeHitResult> LevelEnemyRuntime::applyPlayerWallMeleeHits(
    const assets::Vector3& attackCenter, const assets::Vector3& wallNormal,
    const assets::Vector3& direction, float reach, float damage,
    float minimumAngleDegrees, float maximumAngleDegrees,
    std::int16_t hitType, float horizontalForce,
    float verticalForce) noexcept {
    std::vector<PlayerMeleeHitResult> hits;
    if (!(damage > 0.0F) || !std::isfinite(damage) || !(reach > 0.0F)) {
        return hits;
    }
    // CheckAttackTarget (0x0034fca0) tests every living wall entity, not
    // just the target used to choose the directional attack animation.
    float dispatchedVerticalForce = verticalForce;
    for (auto& enemy : states_) {
        if (enemy.asset == nullptr || !enemy.visible || !enemy.onWall ||
            enemy.wallWebCaptured || enemy.health <= 0.0F || !wallSectorIntersects(attackCenter,
                wallNormal, direction, reach,
                kPlayerCollisionHeightCentimeters * 0.5F,
                minimumAngleDegrees, maximumAngleDegrees, enemy.position,
                enemy.collisionRadius, enemy.collisionHeight)) {
            continue;
        }
        const float previousHealth = enemy.health;
        dispatchedVerticalForce *= kPlayerHitVerticalForceDispatchScale;
        applyCombatDamage(enemy, damage, hitType, &attackCenter,
                          horizontalForce, dispatchedVerticalForce);
        hits.push_back({enemy.asset->objectId, previousHealth - enemy.health});
    }
    return hits;
}

bool LevelEnemyRuntime::canEnterWallWeb(std::int32_t objectId) const noexcept {
    const auto* enemy = find(objectId);
    // CEnemy::CanEnterQTE (0x0032f7e0) delegates to the current behavior.
    // CBehaviorMeleeAttack::CanEnterQTE (0x003b9694) permits wall claws
    // 12-15; the rejecting ground-attack states 16-19 cannot be active here.
    return enemy != nullptr && enemy->asset != nullptr && enemy->visible &&
        enemy->onWall && enemy->health > 0.0F && !enemy->wallWebCaptured &&
        !enemy->cinematicMotion.active && !enemy->cinematicActionActive;
}

bool LevelEnemyRuntime::applyWallWebEvent(const WallWebEvent& event) {
    if (event.kind == WallWebEventKind::Finish) {
        return true;
    }
    auto* enemy = findMutable(event.targetObjectId);
    if (enemy == nullptr || enemy->asset == nullptr) {
        return false;
    }
    if (event.kind == WallWebEventKind::Capture) {
        if (!canEnterWallWeb(event.targetObjectId)) {
            return false;
        }
        // ParseLocalAiMessage (0x00331eb0), message 0x130 -> Unit state 14:
        // clear active behavior without detaching from the wall.
        enemy->wallWebCaptured = true;
        setFacing(*enemy, enemy->facing);
        enemy->meleeAttackActive = false;
        enemy->wallBehaviorState = 0;
        unregisterMeleeEngager(event.targetObjectId);
        return true;
    }
    if (!enemy->wallWebCaptured) {
        return false;
    }
    if (event.kind == WallWebEventKind::Hold && enemy->health > 0.0F) {
        enemy->activeAnimation = "wall_be_drag";
        enemy->animationTimeMilliseconds = 0;
        enemy->animationLoops = true;
        enemy->animationSpeed = 1.0F;
        enemy->animationReversed = false;
        return true;
    }
    // Message 0x131 carries state 10 on success, 0 on failure. SetState
    // (0x003319f0) remaps zero to wall AI 15; CBehaviorDead::BehaviorStart
    // (0x003ab578) sets health zero rather than applying ordinary damage.
    enemy->wallWebCaptured = false;
    setFacing(*enemy, enemy->facing);
    if (event.success || enemy->health <= 0.0F) {
        enemy->health = 0.0F;
        enterDeadState(*enemy);
    } else {
        enemy->behavior = EnemyBehaviorState::Idle;
        enemy->wallIdleMilliseconds = 1000;
        enemy->activeAnimation = "wall_idle";
        enemy->animationTimeMilliseconds = 0;
        enemy->animationLoops = true;
        enemy->animationSpeed = 1.0F;
        enemy->animationReversed = false;
    }
    return true;
}

std::optional<assets::Vector3> LevelEnemyRuntime::nodeWorldPosition(
    std::int32_t objectId, std::string_view nodeName) const {
    const auto* enemy = find(objectId);
    if (enemy == nullptr || enemy->asset == nullptr || level_ == nullptr) {
        return std::nullopt;
    }
    const auto& archetype = level_->enemyArchetypes()[enemy->asset->archetypeIndex];
    const auto* clip = archetype.animationBank.findClip(enemy->activeAnimation);
    std::array<float, 16> node{};
    if (clip == nullptr || !assets::evaluateColladaSceneNodeTransform(
            archetype.mesh, archetype.animationBank,
            clip->startMilliseconds + std::min(enemy->animationTimeMilliseconds,
                                               clip->durationMilliseconds()),
            nodeName, node)) {
        return std::nullopt;
    }
    return transformPoint(enemy->worldTransform, transformPoint(node));
}

const LevelEnemyState* LevelEnemyRuntime::findPlayerWallAttackTarget(
    const assets::Vector3& playerPosition) const noexcept {
    // GetOnWallSpecialState (0x00342de0) calls GetNearestTarget over
    // GetEntitiesOnWall. GetNearestTarget (0x003417e0) searches backwards
    // within a strict 1000 cm 3D limit; hit geometry enforces actual reach.
    const LevelEnemyState* nearest = nullptr;
    float nearestSquared = 1000.0F * 1000.0F;
    for (auto iterator = states_.rbegin(); iterator != states_.rend(); ++iterator) {
        const auto& enemy = *iterator;
        if (enemy.asset == nullptr || !enemy.visible || !enemy.onWall ||
            enemy.health <= 0.0F) {
            continue;
        }
        const float x = enemy.position.x - playerPosition.x;
        const float y = enemy.position.y - playerPosition.y;
        const float z = enemy.position.z - playerPosition.z;
        const float squared = x * x + y * y + z * z;
        if (squared < nearestSquared) {
            nearestSquared = squared;
            nearest = &enemy;
        }
    }
    return nearest;
}

const LevelEnemyState* LevelEnemyRuntime::findSpiderSenseAttacker(
    const assets::Vector3& playerPosition) const noexcept {
    const LevelEnemyState* nearest = nullptr;
    float nearestSquared = std::numeric_limits<float>::max();
    for (const LevelEnemyState& enemy : states_) {
        if (enemy.asset == nullptr || !enemy.visible ||
            enemy.health <= 0.0F || !enemy.meleeAttackActive ||
            !enemy.meleeSenseActive) {
            continue;
        }
        const float x = enemy.position.x - playerPosition.x;
        const float y = enemy.position.y - playerPosition.y;
        const float z = enemy.position.z - playerPosition.z;
        const float squared = x * x + y * y + z * z;
        if (squared < nearestSquared) {
            nearestSquared = squared;
            nearest = &enemy;
        }
    }
    return nearest;
}

float LevelEnemyRuntime::spiderSenseSlowMotionDenominator(
    std::int32_t objectId) const noexcept {
    const LevelEnemyState* enemy = find(objectId);
    if (enemy == nullptr || enemy->asset == nullptr || level_ == nullptr) {
        return 1.0F;
    }
    const auto events = level_->enemySpecialActions().findAttackEvents(
        enemy->asset->enemyTypeId, enemy->activeAnimation);
    for (const EnemyAnimationSpecialAction* event : events) {
        if (event == nullptr || event->attackId < 0 ||
            event->attackId > std::numeric_limits<std::int16_t>::max()) {
            continue;
        }
        const AttackDefinition* attack = level_->attackConfigs().find(
            static_cast<std::int16_t>(event->attackId));
        if (attack != nullptr &&
            std::isfinite(attack->senseSlowMotionDenominator)) {
            return std::max(attack->senseSlowMotionDenominator, 1.0F);
        }
    }
    return 1.0F;
}

std::int32_t LevelEnemyRuntime::spiderSenseReactionType(
    std::int32_t objectId) const noexcept {
    const LevelEnemyState* enemy = find(objectId);
    if (enemy == nullptr || enemy->asset == nullptr || level_ == nullptr) {
        return 0;
    }
    const auto events = level_->enemySpecialActions().findAttackEvents(
        enemy->asset->enemyTypeId, enemy->activeAnimation);
    for (const EnemyAnimationSpecialAction* event : events) {
        if (event == nullptr || event->attackId < 0 ||
            event->attackId > std::numeric_limits<std::int16_t>::max()) {
            continue;
        }
        const AttackDefinition* attack = level_->attackConfigs().find(
            static_cast<std::int16_t>(event->attackId));
        if (attack != nullptr) {
            // AISenseInfo::AISenseInfo (0x003a7a30) copies
            // EnemyAttackInfo+0x48 into its first field; Player::onMessage
            // (0x0034ddb4) repacks that field at Player+0x514+4.
            return attack->senseReactionType;
        }
    }
    return 0;
}

bool LevelEnemyRuntime::isNearAttackKeyFrame(
    std::int32_t objectId) const noexcept {
    const LevelEnemyState* enemy = find(objectId);
    if (enemy == nullptr || enemy->asset == nullptr || level_ == nullptr ||
        enemy->asset->archetypeIndex >=
            level_->enemyArchetypes().size()) {
        return false;
    }
    const EnemyArchetypeAsset& archetype =
        level_->enemyArchetypes()[enemy->asset->archetypeIndex];
    const assets::ColladaAnimationClip* clip =
        archetype.animationBank.findClip(enemy->activeAnimation);
    if (clip == nullptr || clip->durationMilliseconds() == 0) {
        return false;
    }
    // CEnemy::IsNearAttackKeyFrame (0x00334dd0) scans action-type-zero
    // records for the live animation, rejects nonpositive attack IDs and
    // negative-damage records, and compares the integer percentage frame to
    // a window extending 200 ms before through 49 ms after contact.
    const auto events = level_->enemySpecialActions().findAttackEvents(
        enemy->asset->enemyTypeId, enemy->activeAnimation);
    for (const EnemyAnimationSpecialAction* event : events) {
        if (event == nullptr || event->attackId <= 0 ||
            event->attackId > std::numeric_limits<std::int16_t>::max() ||
            event->keyFramePercent < 0) {
            continue;
        }
        const AttackDefinition* attack = level_->attackConfigs().find(
            static_cast<std::int16_t>(event->attackId));
        if (attack == nullptr || attack->damage < 0.0F) {
            continue;
        }
        const auto keyFrameMilliseconds = static_cast<std::int32_t>(
            static_cast<float>(event->keyFramePercent) *
            static_cast<float>(clip->durationMilliseconds()) * 0.01F);
        const auto currentMilliseconds = static_cast<std::int32_t>(
            enemy->animationTimeMilliseconds);
        const std::int32_t nativeDifference =
            keyFrameMilliseconds - (currentMilliseconds - 50);
        if (nativeDifference >= 1 && nativeDifference <= 250) {
            return true;
        }
    }
    return false;
}

const LevelEnemyState* LevelEnemyRuntime::findPlayerAttackTarget(
    const assets::Vector3& playerPosition,
    const assets::Vector3& attackDirection, bool hasDirectionalInput,
    float maximumRange, const LevelCollision* collision,
    float minimumForwardDot) const noexcept {
    if (maximumRange <= 0.0F) {
        return nullptr;
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

    struct EyeCandidate {
        const LevelEnemyState* enemy{};
        float distanceSquared{};
    };
    std::vector<EyeCandidate> airborneEyeCandidates;
    std::vector<EyeCandidate> groundedEyeCandidates;
    airborneEyeCandidates.reserve(states_.size());
    groundedEyeCandidates.reserve(states_.size());

    // Player::SearchTargetByAttackRange (0x003430c8) asks
    // CTargetHelper::getNearestTarget(mask=3). The helper compares the first
    // entries of its separately sorted airborne and non-airborne lists, with
    // 1000 cm as the native threshold when the airborne list is absent or
    // its first entry is farther away.
    const LevelEnemyState* nearestAirborne = nullptr;
    const LevelEnemyState* nearestGrounded = nullptr;
    float nearestAirborneSquared = std::numeric_limits<float>::max();
    float nearestGroundedSquared = std::numeric_limits<float>::max();
    for (const LevelEnemyState& enemy : states_) {
        if (enemy.asset == nullptr || !enemy.visible || enemy.health <= 0.0F) {
            continue;
        }
        const float x = enemy.position.x - playerPosition.x;
        const float y = enemy.position.y - playerPosition.y;
        const float z = enemy.position.z - playerPosition.z;
        const float distanceSquared = x * x + y * y + z * z;
        const float distance = std::sqrt(distanceSquared);
        if (enemy.grounded) {
            if (distanceSquared < nearestGroundedSquared) {
                nearestGrounded = &enemy;
                nearestGroundedSquared = distanceSquared;
            }
        } else if (distanceSquared < nearestAirborneSquared) {
            nearestAirborne = &enemy;
            nearestAirborneSquared = distanceSquared;
        }
        // SearchTargetList (0x00342a80) gathers the helper's two
        // range-sorted lists. getEnemyInRange (0x00353c84) admits equality
        // after adding the target radius; the eye routine repeats that same
        // requested-range test before scoring the horizontal direction.
        if (distance - enemy.collisionRadius <= maximumRange) {
            (enemy.grounded ? groundedEyeCandidates
                            : airborneEyeCandidates)
                .push_back({&enemy, distanceSquared});
        }
    }

    const auto sortByRange = [](std::vector<EyeCandidate>& candidates) {
        std::stable_sort(candidates.begin(), candidates.end(),
                         [](const EyeCandidate& left,
                            const EyeCandidate& right) {
            return left.distanceSquared < right.distanceSquared;
        });
    };
    sortByRange(airborneEyeCandidates);
    sortByRange(groundedEyeCandidates);

    const LevelEnemyState* nearestTarget = nearestAirborne;
    float helperComparisonSquared = 1000.0F * 1000.0F;
    if (nearestAirborne != nullptr &&
        nearestAirborneSquared < helperComparisonSquared) {
        helperComparisonSquared = nearestAirborneSquared;
    }
    if (nearestGrounded != nullptr &&
        nearestGroundedSquared < helperComparisonSquared) {
        nearestTarget = nearestGrounded;
    }
    if (nearestTarget != nullptr) {
        const float selectedDistanceSquared =
            nearestTarget == nearestGrounded
                ? nearestGroundedSquared
                : nearestAirborneSquared;
        if (!(selectedDistanceSquared < maximumRange * maximumRange)) {
            nearestTarget = nullptr;
        }
    }

    const LevelEnemyState* eyeTarget = nullptr;
    float bestForwardDot = minimumForwardDot;
    // SearchTargetList appends mask-1 airborne entries and then mask-2
    // non-airborne entries. SearchTargetByEyeHorizon (0x00343b70) walks that
    // combined output backwards and replaces only for a strictly better dot.
    const auto scoreReverse = [&](const std::vector<EyeCandidate>& candidates) {
        for (auto candidate = candidates.rbegin();
             candidate != candidates.rend(); ++candidate) {
            const LevelEnemyState& enemy = *candidate->enemy;
            // SearchTargetByEyeHorizon's virtual Unit+0xcc call resolves to
            // Unit::IsBlockedByWorld (0x00324670). With its false center-ray
            // argument, the native segment joins the top of each Unit
            // (root + GetHeight), from the candidate toward Spider-Man.
            if (collision != nullptr && collision->segmentBlocked(
                    {enemy.position.x, enemy.position.y,
                     enemy.position.z + enemy.collisionHeight},
                    {playerPosition.x, playerPosition.y,
                     playerPosition.z +
                         kPlayerCollisionHeightCentimeters},
                    kTargetOcclusionIgnoredPhysicsFlags)) {
                continue;
            }
            const float x = enemy.position.x - playerPosition.x;
            const float y = enemy.position.y - playerPosition.y;
            const float horizontalLength = std::hypot(x, y);
            if (horizontalLength <= std::numeric_limits<float>::epsilon()) {
                continue;
            }
            const float forwardDot =
                (x * directionX + y * directionY) / horizontalLength;
            if (forwardDot > bestForwardDot) {
                eyeTarget = &enemy;
                bestForwardDot = forwardDot;
            }
        }
    };
    scoreReverse(groundedEyeCandidates);
    scoreReverse(airborneEyeCandidates);
    if (eyeTarget != nullptr || hasDirectionalInput) {
        return eyeTarget;
    }
    return nearestTarget;
}

const LevelEnemyState* LevelEnemyRuntime::findNearestGroundedPlayerTarget(
    const assets::Vector3& playerPosition, float maximumRange) const noexcept {
    if (maximumRange <= 0.0F) {
        return nullptr;
    }
    const LevelEnemyState* nearest = nullptr;
    float nearestDistanceSquared = maximumRange * maximumRange;
    for (const LevelEnemyState& enemy : states_) {
        if (enemy.asset == nullptr || !enemy.visible || enemy.health <= 0.0F ||
            !enemy.grounded) {
            continue;
        }
        const float x = enemy.position.x - playerPosition.x;
        const float y = enemy.position.y - playerPosition.y;
        const float z = enemy.position.z - playerPosition.z;
        const float distanceSquared = x * x + y * y + z * z;
        if (distanceSquared < nearestDistanceSquared) {
            nearest = &enemy;
            nearestDistanceSquared = distanceSquared;
        }
    }
    return nearest;
}

bool LevelEnemyRuntime::destroy(std::int32_t objectId) noexcept {
    LevelEnemyState* enemy = findMutable(objectId);
    if (enemy == nullptr) {
        return false;
    }
    cancelRhinoQuickTimeIfOwned(*enemy);
    if (enemy->health > 0.0F) {
        enemy->health = 0.0F;
        enterDeadState(*enemy);
    }
    return true;
}

bool LevelEnemyRuntime::setDiagnosticAiEnabled(
    std::int32_t objectId, bool enabled,
    bool forcePlayerDetected) noexcept {
    LevelEnemyState* enemy = findMutable(objectId);
    if (enemy == nullptr || enemy->health <= 0.0F) {
        return false;
    }
    cancelRhinoQuickTimeIfOwned(*enemy);
    enemy->aiEnabled = enabled;
    enemy->physicsActive = enabled;
    enemy->behavior = enabled ? EnemyBehaviorState::Idle
                              : EnemyBehaviorState::Disabled;
    enemy->meleeAttackActive = false;
    unregisterMeleeEngager(objectId);
    enemy->meleeAttackCooldownMilliseconds = 0;
    enemy->rangeAttackCooldownMilliseconds = 0;
    enemy->tiedUpRemainingMilliseconds = 0;
    enemy->sandmanTask = SandmanBossTaskState::None;
    enemy->rhinoTask = RhinoBossTaskState::None;
    enemy->rhinoTaskElapsedMilliseconds = 0;
    enemy->rhinoMeleeAttacksRemaining = 0;
    enemy->robotPhantomTask = RobotPhantomTaskState::None;
    enemy->robotPhantomTaskElapsedMilliseconds = 0;
    enemy->robotPhantomSequenceIndex = 0;
    enemy->electroTask = ElectroBossTaskState::None;
    enemy->electroTaskElapsedMilliseconds = 0;
    enemy->electroSequenceIndex = 0;
    enemy->electroRangeAttacksRemaining = 0;
    enemy->electroRangeWaitMilliseconds = 0;
    enemy->electroDashesRemaining = 0;
    enemy->electroRangeReleased = false;
    enemy->electroWeakReleased = false;
    enemy->electroDashEffectThrown = false;
    enemy->electroDashHitPlayer = false;
    if (enabled && isElectroBoss(*enemy)) {
        // CBoss::ResetBehavior (0x0032afdc) captures the boss origin used by
        // the final Electro dash when behavior is initialized.
        enemy->electroHomePosition = enemy->position;
    }
    std::erase_if(
        boomerangs_, [objectId](const EnemyBoomerangState& boomerang) {
            return boomerang.sourceObjectId == objectId;
        });
    std::erase_if(
        thunderclaps_, [objectId](const EnemyThunderclapState& thunderclap) {
            return thunderclap.sourceObjectId == objectId;
        });
    removeElectroPosts(objectId);
    if (enabled && forcePlayerDetected) {
        enemy->playerDetected = true;
    }
    return true;
}

bool LevelEnemyRuntime::setDiagnosticPhysicsActive(
    std::int32_t objectId, bool enabled) noexcept {
    LevelEnemyState* enemy = findMutable(objectId);
    if (enemy == nullptr || enemy->health <= 0.0F) {
        return false;
    }
    enemy->physicsActive = enabled;
    return true;
}

std::vector<EnemyPlayerHit> LevelEnemyRuntime::consumePlayerHits() noexcept {
    std::vector<EnemyPlayerHit> hits = std::move(pendingPlayerHits_);
    pendingPlayerHits_.clear();
    return hits;
}

std::vector<EnemySoundCue> LevelEnemyRuntime::consumeSoundCues() noexcept {
    std::vector<EnemySoundCue> cues = std::move(pendingSoundCues_);
    pendingSoundCues_.clear();
    return cues;
}

std::vector<EnemyCameraShakeCue>
LevelEnemyRuntime::consumeCameraShakeCues() noexcept {
    std::vector<EnemyCameraShakeCue> cues =
        std::move(pendingCameraShakeCues_);
    pendingCameraShakeCues_.clear();
    return cues;
}

std::vector<EnemyProjectileEvent>
LevelEnemyRuntime::consumeProjectileEvents() noexcept {
    std::vector<EnemyProjectileEvent> events =
        std::move(pendingProjectileEvents_);
    pendingProjectileEvents_.clear();
    return events;
}

std::vector<EnemyEffectCue> LevelEnemyRuntime::consumeEffectCues() noexcept {
    std::vector<EnemyEffectCue> cues = std::move(pendingEffectCues_);
    pendingEffectCues_.clear();
    return cues;
}

std::vector<EnemyLandingAnimatedEffectSpawnEvent>
LevelEnemyRuntime::consumeLandingAnimatedEffectSpawnEvents() noexcept {
    std::vector<EnemyLandingAnimatedEffectSpawnEvent> events =
        std::move(pendingLandingAnimatedEffectSpawnEvents_);
    pendingLandingAnimatedEffectSpawnEvents_.clear();
    return events;
}

std::vector<PlayerWebPelletEvent>
LevelEnemyRuntime::consumePlayerWebPelletEvents() noexcept {
    std::vector<PlayerWebPelletEvent> events =
        std::move(pendingPlayerWebPelletEvents_);
    pendingPlayerWebPelletEvents_.clear();
    return events;
}

std::vector<EnemySeparationEvent>
LevelEnemyRuntime::consumeEnemySeparationEvents() noexcept {
    std::vector<EnemySeparationEvent> events =
        std::move(pendingEnemySeparationEvents_);
    pendingEnemySeparationEvents_.clear();
    return events;
}

void LevelEnemyRuntime::resetTransientForCheckPointLoad() noexcept {
    // CAIEntityManager::Reset and EffectManager::ClearAllEffects are part of
    // CLevel::ResetLevel/RestartAtLastCheckPoint (0x003832d0/0x00383668).
    // The serialized enemy objects retain health/visibility/transforms, while
    // attacks, projectiles, event queues and an in-flight boss QTA do not.
    pendingPlayerHits_.clear();
    pendingSoundCues_.clear();
    pendingCameraShakeCues_.clear();
    gunLines_.clear();
    molotovs_.clear();
    boomerangs_.clear();
    playerWebPellets_.clear();
    thunderclaps_.clear();
    electricPosts_.clear();
    electroBursts_.clear();
    landingAnimatedEffects_.clear();
    pendingLandingAnimatedEffectSpawnEvents_.clear();
    pendingProjectileEvents_.clear();
    pendingEffectCues_.clear();
    pendingPlayerWebPelletEvents_.clear();
    pendingEnemySeparationEvents_.clear();
    shownHealthBarObjectId_.reset();
    meleeEngagerObjectId_ = -1;
    meleeEngagementCooldownMilliseconds_ = 0.0F;
    rhinoQuickTimeAction_.cancel();
    rhinoQuickTimeEnemyId_ = -1;
    for (LevelEnemyState& enemy : states_) {
        enemy.cinematicMotion = {};
        enemy.wallWebCaptured = false;
        enemy.cinematicActionActive = false;
        enemy.cinematicActionObjectId = -1;
        enemy.meleeAttackActive = false;
        enemy.meleeAttackRegistered = false;
        enemy.meleeRegistrationTimerMilliseconds = 0.0F;
        enemy.tiedUpRemainingMilliseconds = 0;
        if (enemy.robotPhantomTask ==
            RobotPhantomTaskState::ConcealHidden) {
            enemy.visible = true;
        }
        enemy.robotPhantomTask = RobotPhantomTaskState::None;
        enemy.robotPhantomTaskElapsedMilliseconds = 0;
        enemy.robotPhantomSequenceIndex = 0;
        enemy.electroTask = ElectroBossTaskState::None;
        enemy.electroTaskElapsedMilliseconds = 0;
        enemy.electroSequenceIndex = 0;
        enemy.electroRangeAttacksRemaining = 0;
        enemy.electroRangeWaitMilliseconds = 0;
        enemy.electroDashesRemaining = 0;
        enemy.electroRangeReleased = false;
        enemy.electroWeakReleased = false;
        enemy.electroDashEffectThrown = false;
        enemy.electroDashHitPlayer = false;
    }
}

float LevelEnemyRuntime::maximumAttackReach(
    const LevelEnemyState& enemy) const noexcept {
    if (level_ == nullptr || enemy.asset == nullptr) {
        return 0.0F;
    }
    if (isGunLineEnemy(enemy)) {
        return kGunLineMaximumRangeCentimeters;
    }
    if (isMolotovEnemy(enemy)) {
        const EnemyAttributeDefinition* attributes =
            level_->enemyAttributeConfigs().find(enemy.asset->enemyTypeId);
        return attributes == nullptr
                   ? 0.0F
                   : attributes->maximumRangeAttackDistance;
    }
    const EnemyAttributeDefinition* attributes =
        level_->enemyAttributeConfigs().find(enemy.asset->enemyTypeId);
    if (attributes != nullptr &&
        attributes->maximumMeleeAttackDistance > 0.0F) {
        // CEnemy::MoveToMeleeAttackPlayerRange consumes the archetype's
        // authored melee maximum. This is 300 cm for the bat thug so its
        // 300-cm jump attack can be selected before it crowds into the
        // standing swing's 200-cm volume.
        return attributes->maximumMeleeAttackDistance;
    }
    float maximumReach = 0.0F;
    const auto events = level_->enemySpecialActions().findAttackEvents(
        enemy.asset->enemyTypeId, attackAnimation(*enemy.asset));
    for (const EnemyAnimationSpecialAction* event : events) {
        if (event == nullptr || event->attackId < 0 ||
            event->attackId > std::numeric_limits<std::int16_t>::max()) {
            continue;
        }
        const AttackDefinition* attack = level_->attackConfigs().find(
            static_cast<std::int16_t>(event->attackId));
        if (attack != nullptr) {
            maximumReach = std::max(maximumReach, attack->maximumReach());
        }
    }
    return maximumReach;
}

void LevelEnemyRuntime::queueAuthoredAttackEvents(
    LevelEnemyState& enemy, std::uint32_t previousTimeMilliseconds,
    const assets::Vector3& playerPosition) {
    if (level_ == nullptr || enemy.asset == nullptr ||
        enemy.asset->archetypeIndex >= level_->enemyArchetypes().size()) {
        return;
    }
    const EnemyArchetypeAsset& archetype =
        level_->enemyArchetypes()[enemy.asset->archetypeIndex];
    const assets::ColladaAnimationClip* clip =
        archetype.animationBank.findClip(enemy.activeAnimation);
    if (clip == nullptr || clip->durationMilliseconds() == 0) {
        return;
    }
    const auto events = level_->enemySpecialActions().findEvents(
        enemy.asset->enemyTypeId, enemy.activeAnimation);
    const auto attackIntersectsPlayer =
        [&](const AttackDefinition& attack,
            std::uint32_t eventTime) noexcept {
        if (enemy.onWall) {
            assets::Vector3 origin = enemy.position;
            std::array<float, 16> pelvis{};
            if (assets::evaluateColladaSceneNodeTransform(
                    archetype.mesh, archetype.animationBank,
                    clip->startMilliseconds + eventTime, "Bip01", pelvis)) {
                origin = transformPoint(enemy.worldTransform,
                                        transformPoint(pelvis));
            }
            return wallSectorIntersects(
                origin, enemy.wallNormal, enemy.wallAttackDirection,
                attack.maximumReach(), enemy.collisionHeight * 0.5F,
                attack.minimumAngleDegrees, attack.maximumAngleDegrees,
                playerPosition, kPlayerCollisionRadiusCentimeters,
                kPlayerCollisionHeightCentimeters);
        }
        return cylinderSectorIntersects(
            enemy.position, enemy.collisionHeight, enemy.facing,
            attack.maximumReach(), attack.minimumAngleDegrees,
            attack.maximumAngleDegrees, playerPosition,
            kPlayerCollisionRadiusCentimeters,
            kPlayerCollisionHeightCentimeters);
    };
    for (const EnemyAnimationSpecialAction* event : events) {
        if (event == nullptr || event->keyFramePercent < 0 ||
            event->keyFramePercent >= 100) {
            continue;
        }
        const std::uint32_t eventTime = static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(clip->durationMilliseconds()) *
             static_cast<std::uint32_t>(event->keyFramePercent)) /
            100U);
        if (!crossedLoopEvent(previousTimeMilliseconds,
                              enemy.animationTimeMilliseconds,
                              clip->durationMilliseconds(), eventTime)) {
            continue;
        }
        for (const std::int16_t soundMapId : event->soundMapIds) {
            const std::int32_t voxSoundId =
                level_->enemyBehaviorConfigs().resolveSoundMap(
                    soundMapId, enemy.asset->enemyTypeId);
            if (voxSoundId >= 0) {
                pendingSoundCues_.push_back(
                    {enemy.asset->objectId, voxSoundId});
            }
        }
        if (event->actionType == 2 && enemy.meleeAttackActive) {
            // SpecialAnimActionCheck sends behavior message 0x66 here.
            // CBehaviorMeleeAttack::onMessage registers AISenseInfo only when
            // the selected attack volume currently intersects the player.
            const auto attackEvents =
                level_->enemySpecialActions().findAttackEvents(
                    enemy.asset->enemyTypeId, enemy.activeAnimation);
            const auto selected = std::find_if(
                attackEvents.begin(), attackEvents.end(),
                [&](const EnemyAnimationSpecialAction* candidate) {
                    if (candidate == nullptr || candidate->attackId < 0 ||
                        candidate->attackId >
                            std::numeric_limits<std::int16_t>::max()) {
                        return false;
                    }
                    const auto* attack = level_->attackConfigs().find(
                        static_cast<std::int16_t>(candidate->attackId));
                    return attack != nullptr &&
                           attackIntersectsPlayer(*attack, eventTime);
                });
            enemy.meleeSenseActive = selected != attackEvents.end();
            continue;
        }
        if (isGunLineEnemy(enemy) && event->actionType == 0 &&
            event->attackId < 0) {
            const EnemyAttackIntervalDefinition* interval =
                level_->enemyAttackIntervalConfigs().findForWeaponType(13);
            const EnemyRangeAttackDefinition* attack =
                interval == nullptr
                    ? nullptr
                    : level_->enemyRangeAttackConfigs().findByMapId(
                          interval->id);
            const float length = std::hypot(
                playerPosition.x - enemy.position.x,
                playerPosition.y - enemy.position.y);
            if (attack != nullptr &&
                length > std::numeric_limits<float>::epsilon()) {
                gunLines_.push_back(
                    {enemy.asset->objectId,
                     {enemy.position.x, enemy.position.y,
                      enemy.position.z + 100.0F},
                     {(playerPosition.x - enemy.position.x) / length,
                      (playerPosition.y - enemy.position.y) / length, 0.0F},
                     attack->damage,
                     0,
                     true});
            }
            continue;
        }
        if (isMolotovEnemy(enemy) && event->actionType == 0 &&
            event->attackId < 0) {
            throwMolotov(enemy, playerPosition, eventTime);
            continue;
        }
        if (event->attackId < 0 ||
            event->attackId > std::numeric_limits<std::int16_t>::max()) {
            continue;
        }
        const auto attackId = static_cast<std::int16_t>(event->attackId);
        const AttackDefinition* attack = level_->attackConfigs().find(attackId);
        // Negative-damage rows are native sense/transition markers (Rhino's
        // ATTACK_HIT_DUSHING is one), not damage to apply to the player.
        if (attack == nullptr || attack->damage <= 0.0F) {
            continue;
        }
        if (!attackIntersectsPlayer(*attack, eventTime)) {
            continue;
        }
        pendingPlayerHits_.push_back(
            {enemy.asset->objectId, attackId, attack->damage, attack->hitType});
    }
}

void LevelEnemyRuntime::updateGunLines(
    std::uint32_t elapsedMilliseconds,
    const assets::Vector3& playerPosition,
    const LevelCollision* collision) noexcept {
    for (EnemyGunLineState& line : gunLines_) {
        if (!line.active) {
            continue;
        }
        const assets::Vector3 previous = line.position;
        const float travel = kGunLineSpeedCentimetersPerSecond *
                             static_cast<float>(elapsedMilliseconds) / 1000.0F;
        line.position.x += line.direction.x * travel;
        line.position.y += line.direction.y * travel;
        line.position.z += line.direction.z * travel;
        line.ageMilliseconds = std::min<std::uint32_t>(
            line.ageMilliseconds + elapsedMilliseconds,
            kGunLineLifetimeMilliseconds);
        if (collision != nullptr &&
            collision->segmentBlocked(previous, line.position)) {
            line.active = false;
            continue;
        }
        const float segmentX = line.position.x - previous.x;
        const float segmentY = line.position.y - previous.y;
        const float segmentLengthSquared =
            segmentX * segmentX + segmentY * segmentY;
        float time = 0.0F;
        if (segmentLengthSquared > std::numeric_limits<float>::epsilon()) {
            time = std::clamp(
                ((playerPosition.x - previous.x) * segmentX +
                 (playerPosition.y - previous.y) * segmentY) /
                    segmentLengthSquared,
                0.0F, 1.0F);
        }
        const float closestX = previous.x + segmentX * time;
        const float closestY = previous.y + segmentY * time;
        const float playerX = playerPosition.x - closestX;
        const float playerY = playerPosition.y - closestY;
        if (playerX * playerX + playerY * playerY <=
            kGunLinePlayerRadiusCentimeters *
                kGunLinePlayerRadiusCentimeters) {
            pendingPlayerHits_.push_back(
                {line.sourceObjectId, -1, line.damage});
            line.active = false;
        } else if (line.ageMilliseconds >= kGunLineLifetimeMilliseconds) {
            line.active = false;
        }
    }
    std::erase_if(gunLines_, [](const EnemyGunLineState& line) {
        return !line.active;
    });
}

void LevelEnemyRuntime::updateMolotovs(
    std::uint32_t elapsedMilliseconds,
    const assets::Vector3& playerPosition,
    const LevelCollision* collision) noexcept {
    const float seconds = static_cast<float>(elapsedMilliseconds) / 1000.0F;
    const assets::ColladaAnimationClip* explodeReady =
        level_ == nullptr
            ? nullptr
            : level_->molotovProjectile().animationBank.findClip(
                  "explode_ready");
    for (EnemyMolotovState& molotov : molotovs_) {
        if (!molotov.active) {
            continue;
        }
        if (molotov.phase == EnemyMolotovPhase::ExplodeReady) {
            const std::uint64_t nextElapsed =
                static_cast<std::uint64_t>(molotov.phaseElapsedMilliseconds) +
                elapsedMilliseconds;
            molotov.phaseElapsedMilliseconds = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(
                    nextElapsed,
                    std::numeric_limits<std::uint32_t>::max()));
            if (explodeReady != nullptr &&
                molotov.phaseElapsedMilliseconds <
                    explodeReady->durationMilliseconds()) {
                continue;
            }

            // CThrowObject::SetState(4) subtype 1 (0x00359efc) measures from
            // the bottle to the player's authored top and uses 250 cm.
            const float x = playerPosition.x - molotov.position.x;
            const float y = playerPosition.y - molotov.position.y;
            const float z =
                playerPosition.z + kPlayerCollisionHeightCentimeters -
                molotov.position.z;
            if (x * x + y * y + z * z <
                kMolotovExplosionRadiusCentimeters *
                    kMolotovExplosionRadiusCentimeters) {
                pendingPlayerHits_.push_back(
                    {molotov.sourceObjectId, -1, molotov.damage});
            }
            pendingEffectCues_.push_back(
                {molotov.sourceObjectId, molotov.roomId, molotov.position,
                 "molotov_bomb"});
            pendingProjectileEvents_.push_back(
                {EnemyProjectileEventKind::Exploded,
                 molotov.sourceObjectId, molotov.position, molotov.velocity,
                 molotov.gravityCentimetersPerSecondSquared});
            molotov.active = false;
            continue;
        }

        const std::uint64_t nextElapsed =
            static_cast<std::uint64_t>(molotov.phaseElapsedMilliseconds) +
            elapsedMilliseconds;
        molotov.phaseElapsedMilliseconds = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(
                nextElapsed, std::numeric_limits<std::uint32_t>::max()));

        const assets::Vector3 previous = molotov.position;
        assets::Vector3 next{
            previous.x + molotov.velocity.x * seconds,
            previous.y + molotov.velocity.y * seconds,
            previous.z + molotov.velocity.z * seconds +
                0.5F * molotov.gravityCentimetersPerSecondSquared * seconds *
                    seconds};
        assets::Vector3 nextVelocity = molotov.velocity;
        nextVelocity.z +=
            molotov.gravityCentimetersPerSecondSquared * seconds;

        float supportHeight = 0.0F;
        const bool grounded =
            collision != nullptr && nextVelocity.z <= 0.0F &&
            collision->groundHeight(
                next,
                std::abs(previous.z - next.z) +
                    kMolotovCollisionRadiusCentimeters,
                kMolotovCollisionRadiusCentimeters, supportHeight) &&
            previous.z >= supportHeight && next.z <= supportHeight;
        if (grounded) {
            next.z = supportHeight;
            molotov.position = next;
            molotov.velocity = {};
            molotov.phase = EnemyMolotovPhase::ExplodeReady;
            molotov.phaseElapsedMilliseconds = 0;
            pendingProjectileEvents_.push_back(
                {EnemyProjectileEventKind::Grounded,
                 molotov.sourceObjectId, molotov.position, molotov.velocity,
                 molotov.gravityCentimetersPerSecondSquared});
            continue;
        }

        const bool touchedPlayer =
            !molotov.stoppedByPlayer &&
            segmentTouchesPlayer(previous, next, playerPosition,
                                 kMolotovCollisionRadiusCentimeters);
        const bool blocked =
            collision != nullptr && collision->segmentBlocked(previous, next);
        molotov.position = blocked ? previous : next;
        molotov.velocity = nextVelocity;
        if (touchedPlayer || blocked) {
            molotov.velocity.x = 0.0F;
            molotov.velocity.y = 0.0F;
            molotov.velocity.z = std::min(
                molotov.velocity.z,
                kMolotovStoppedFallSpeedCentimetersPerSecond);
        }
        if (touchedPlayer) {
            molotov.stoppedByPlayer = true;
            pendingProjectileEvents_.push_back(
                {EnemyProjectileEventKind::PlayerContact,
                 molotov.sourceObjectId, molotov.position, molotov.velocity,
                 molotov.gravityCentimetersPerSecondSquared});
        }
        if (molotov.position.z < -kThrowObjectWorldLimitCentimeters ||
            molotov.position.z > kThrowObjectWorldLimitCentimeters) {
            molotov.active = false;
        }
    }
    std::erase_if(molotovs_, [](const EnemyMolotovState& molotov) {
        return !molotov.active;
    });
}

void LevelEnemyRuntime::updateBoomerangs(
    std::uint32_t elapsedMilliseconds,
    const assets::Vector3& playerPosition,
    const LevelCollision* collision) noexcept {
    const float seconds = static_cast<float>(elapsedMilliseconds) / 1000.0F;
    const auto enterPhase = [](EnemyBoomerangState& boomerang,
                               EnemyBoomerangPhase phase) {
        boomerang.phase = phase;
        boomerang.phaseElapsedMilliseconds = 0;
    };
    for (EnemyBoomerangState& boomerang : boomerangs_) {
        if (!boomerang.active ||
            boomerang.phase == EnemyBoomerangPhase::Ready) {
            continue;
        }
        const LevelEnemyState* owner = find(boomerang.sourceObjectId);
        if (owner == nullptr || owner->health <= 0.0F) {
            boomerang.active = false;
            continue;
        }
        boomerang.phaseElapsedMilliseconds =
            static_cast<std::uint32_t>(std::min<std::uint64_t>(
                static_cast<std::uint64_t>(
                    boomerang.phaseElapsedMilliseconds) +
                    elapsedMilliseconds,
                std::numeric_limits<std::uint32_t>::max()));

        if (boomerang.phase == EnemyBoomerangPhase::TargetPause) {
            boomerang.velocity = {};
            if (boomerang.phaseElapsedMilliseconds >=
                kRobotPhantomBoomerangTargetPauseMilliseconds) {
                enterPhase(boomerang, EnemyBoomerangPhase::Returning);
            }
            continue;
        }
        if (boomerang.phase == EnemyBoomerangPhase::CollisionPause) {
            boomerang.velocity = {};
            if (boomerang.phaseElapsedMilliseconds >=
                kRobotPhantomBoomerangCollisionPauseMilliseconds) {
                enterPhase(boomerang, EnemyBoomerangPhase::Returning);
            }
            continue;
        }

        const assets::Vector3 hand = robotPhantomHandPosition(*owner);
        const assets::Vector3 destination =
            boomerang.phase == EnemyBoomerangPhase::Outbound
                ? boomerang.targetPosition
                : hand;
        const float deltaX = destination.x - boomerang.position.x;
        const float deltaY = destination.y - boomerang.position.y;
        const float deltaZ = destination.z - boomerang.position.z;
        const float distance =
            std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
        if (distance > std::numeric_limits<float>::epsilon()) {
            const float speed =
                boomerang.phase == EnemyBoomerangPhase::HandReturn
                    ? kRobotPhantomBoomerangHandSpeedCentimetersPerSecond
                    : boomerang.speedCentimetersPerSecond;
            boomerang.velocity = {deltaX / distance * speed,
                                  deltaY / distance * speed,
                                  deltaZ / distance * speed};
            boomerang.facing = {deltaX / distance, deltaY / distance,
                                deltaZ / distance};
        } else {
            boomerang.velocity = {};
        }
        const assets::Vector3 previous = boomerang.position;
        assets::Vector3 next{previous.x + boomerang.velocity.x * seconds,
                             previous.y + boomerang.velocity.y * seconds,
                             previous.z + boomerang.velocity.z * seconds};
        if (distance > 0.0F) {
            const float travel = std::hypot(
                std::hypot(next.x - previous.x, next.y - previous.y),
                next.z - previous.z);
            if (travel >= distance) {
                next = destination;
            }
        }

        if (boomerang.phase == EnemyBoomerangPhase::Outbound) {
            if (!boomerang.hitPlayer &&
                segmentTouchesPlayer(
                    previous, next, playerPosition,
                    kRobotPhantomBoomerangCollisionRadiusCentimeters)) {
                boomerang.hitPlayer = true;
                pendingPlayerHits_.push_back(
                    {boomerang.sourceObjectId, -1, boomerang.damage});
                pendingProjectileEvents_.push_back(
                    {EnemyProjectileEventKind::PlayerContact,
                     boomerang.sourceObjectId, next, boomerang.velocity,
                     0.0F});
                boomerang.position = next;
                enterPhase(boomerang, EnemyBoomerangPhase::Returning);
                continue;
            }
            if (collision != nullptr &&
                collision->segmentBlocked(previous, next)) {
                boomerang.position = previous;
                pendingProjectileEvents_.push_back(
                    {EnemyProjectileEventKind::StaticContact,
                     boomerang.sourceObjectId, previous,
                     boomerang.velocity, 0.0F});
                enterPhase(boomerang,
                           EnemyBoomerangPhase::CollisionPause);
                continue;
            }
            const float beforeX = boomerang.targetPosition.x - previous.x;
            const float beforeY = boomerang.targetPosition.y - previous.y;
            const float beforeZ = boomerang.targetPosition.z - previous.z;
            const float afterX = boomerang.targetPosition.x - next.x;
            const float afterY = boomerang.targetPosition.y - next.y;
            const float afterZ = boomerang.targetPosition.z - next.z;
            boomerang.position = next;
            if (beforeX * afterX + beforeY * afterY + beforeZ * afterZ <=
                0.0F) {
                enterPhase(boomerang, EnemyBoomerangPhase::TargetPause);
            } else if (boomerang.phaseElapsedMilliseconds >=
                       kRobotPhantomBoomerangOutboundMilliseconds) {
                enterPhase(boomerang, EnemyBoomerangPhase::Returning);
            }
            continue;
        }

        boomerang.position = next;
        if (boomerang.phase == EnemyBoomerangPhase::Returning &&
            distance <= kRobotPhantomBoomerangReturnRadiusCentimeters) {
            enterPhase(boomerang, EnemyBoomerangPhase::HandReturn);
            continue;
        }
        if (boomerang.phase == EnemyBoomerangPhase::HandReturn &&
            distance <= kRobotPhantomBoomerangHandEpsilonCentimeters) {
            boomerang.position = hand;
            boomerang.velocity = {};
            enterPhase(boomerang, EnemyBoomerangPhase::Ready);
            pendingProjectileEvents_.push_back(
                {EnemyProjectileEventKind::Returned,
                 boomerang.sourceObjectId, boomerang.position, {}, 0.0F});
        }
    }
    std::erase_if(boomerangs_, [](const EnemyBoomerangState& boomerang) {
        return !boomerang.active;
    });
}

void LevelEnemyRuntime::startMolotovAttack(LevelEnemyState& enemy) {
    if (level_ == nullptr || enemy.asset == nullptr ||
        enemy.asset->archetypeIndex >= level_->enemyArchetypes().size()) {
        return;
    }
    constexpr std::string_view kThrowAnimation =
        "idle_throw_molotov_idle";
    const EnemyArchetypeAsset& archetype =
        level_->enemyArchetypes()[enemy.asset->archetypeIndex];
    const assets::ColladaAnimationClip* clip =
        archetype.animationBank.findClip(kThrowAnimation);
    const EnemyAttackIntervalDefinition* interval =
        level_->enemyAttackIntervalConfigs().findForWeaponType(5);
    const EnemyRangeAttackDefinition* attack =
        interval == nullptr
            ? nullptr
            : level_->enemyRangeAttackConfigs().findByMapId(interval->id);
    if (clip == nullptr || attack == nullptr ||
        attack->animationDurationMilliseconds <= 0.0F) {
        return;
    }
    enemy.activeAnimation = kThrowAnimation;
    enemy.animationTimeMilliseconds = 0;
    // StateEnter (0x003c143c) scales the 1500 ms authored clip to the
    // RANGE_ATTACK_01 1000 ms action window.
    enemy.animationSpeed =
        static_cast<float>(clip->durationMilliseconds()) /
        attack->animationDurationMilliseconds;
    enemy.animationLoops = false;
    enemy.animationReversed = false;
}

void LevelEnemyRuntime::throwMolotov(
    LevelEnemyState& enemy, const assets::Vector3& playerPosition,
    std::uint32_t authoredEventTimeMilliseconds) {
    if (level_ == nullptr || enemy.asset == nullptr ||
        enemy.asset->archetypeIndex >= level_->enemyArchetypes().size()) {
        return;
    }
    if (std::any_of(molotovs_.begin(), molotovs_.end(),
                    [&enemy](const EnemyMolotovState& molotov) {
                        return molotov.active && enemy.asset != nullptr &&
                               molotov.sourceObjectId == enemy.asset->objectId;
                    })) {
        return;
    }
    const EnemyArchetypeAsset& archetype =
        level_->enemyArchetypes()[enemy.asset->archetypeIndex];
    const assets::ColladaAnimationClip* clip =
        archetype.animationBank.findClip(enemy.activeAnimation);
    if (clip == nullptr) {
        return;
    }
    std::array<float, 16> handTransform{};
    const Result handResult = assets::evaluateColladaSceneNodeTransform(
        archetype.mesh, archetype.animationBank,
        clip->startMilliseconds + authoredEventTimeMilliseconds,
        "Bip01_R_Hand", handTransform);
    if (!handResult) {
        return;
    }
    const assets::Vector3 localOrigin = transformPoint(handTransform);
    assets::Vector3 origin = transformPoint(enemy.worldTransform, localOrigin);
    const assets::Vector3 target{
        playerPosition.x, playerPosition.y,
        playerPosition.z + kPlayerCollisionHeightCentimeters * 0.5F};
    const float deltaX = target.x - origin.x;
    const float deltaY = target.y - origin.y;
    const float deltaZ = target.z - origin.z;
    const float horizontalDistance = std::hypot(deltaX, deltaY);
    const float flightSeconds = std::max(
        horizontalDistance / kMolotovSpeedCentimetersPerSecond,
        kMolotovMinimumFlightSeconds);
    const float effectiveSpeed =
        flightSeconds <= std::numeric_limits<float>::epsilon()
            ? 0.0F
            : horizontalDistance / flightSeconds;
    assets::Vector3 facing = enemy.facing;
    if (horizontalDistance > std::numeric_limits<float>::epsilon()) {
        facing = {deltaX / horizontalDistance, deltaY / horizontalDistance,
                  0.0F};
    }
    assets::Vector3 velocity{facing.x * effectiveSpeed,
                             facing.y * effectiveSpeed, 0.0F};
    const float gravity = -std::abs(
        2.0F * (std::abs(deltaZ) + kPlayerCollisionHeightCentimeters) /
        (flightSeconds * flightSeconds));
    origin.x += velocity.x * kMolotovInitialAdvanceSeconds;
    origin.y += velocity.y * kMolotovInitialAdvanceSeconds;

    const EnemyAttackIntervalDefinition* interval =
        level_->enemyAttackIntervalConfigs().findForWeaponType(5);
    const EnemyRangeAttackDefinition* attack =
        interval == nullptr
            ? nullptr
            : level_->enemyRangeAttackConfigs().findByMapId(interval->id);
    const float damage = attack == nullptr ? 0.0F : attack->damage;
    molotovs_.push_back({enemy.asset->objectId, enemy.asset->roomId, origin,
                         velocity, facing, gravity, damage, 0,
                         EnemyMolotovPhase::Flying, false, true});
    pendingProjectileEvents_.push_back(
        {EnemyProjectileEventKind::Spawned, enemy.asset->objectId, origin,
         velocity, gravity});
}

void LevelEnemyRuntime::startGunLineAttack(LevelEnemyState& enemy) {
    if (level_ == nullptr || enemy.asset == nullptr) {
        return;
    }
    constexpr std::array<std::string_view, 2> kGunAnimations{
        "idle_shoot_left_idle", "idle_shoot_right_idle"};
    const std::string_view animation =
        kGunAnimations[enemy.rangeAttackVariantCursor % kGunAnimations.size()];
    enemy.rangeAttackVariantCursor = static_cast<std::uint32_t>(
        (enemy.rangeAttackVariantCursor + 1) % kGunAnimations.size());
    const EnemyArchetypeAsset& archetype =
        level_->enemyArchetypes()[enemy.asset->archetypeIndex];
    if (archetype.animationBank.findClip(animation) == nullptr) {
        return;
    }
    enemy.activeAnimation = animation;
    enemy.animationTimeMilliseconds = 0;
    enemy.animationSpeed = 1.0F;
    enemy.animationLoops = false;
    enemy.animationReversed = false;
}

void LevelEnemyRuntime::startMeleeAttack(
    LevelEnemyState& enemy,
    const assets::Vector3& playerPosition) {
    if (level_ == nullptr || enemy.asset == nullptr ||
        enemy.asset->archetypeIndex >= level_->enemyArchetypes().size()) {
        return;
    }
    const EnemyArchetypeAsset& archetype =
        level_->enemyArchetypes()[enemy.asset->archetypeIndex];
    std::vector<std::string_view> attackAnimations =
        level_->enemyBehaviorConfigs().resolveStateAnimationNames(
            "ENEMY_BEHAVIOR_MELEE_ATTACK_STATE_DO_ATTACK",
            enemy.asset->enemyTypeId);
    std::erase_if(attackAnimations, [&](std::string_view candidate) {
        return archetype.animationBank.findClip(candidate) == nullptr ||
               level_->enemySpecialActions()
                   .findAttackEvents(enemy.asset->enemyTypeId, candidate)
                   .empty();
    });
    if (attackAnimations.empty()) {
        const std::string_view fallback = attackAnimation(*enemy.asset);
        if (archetype.animationBank.findClip(fallback) == nullptr) {
            return;
        }
        attackAnimations.push_back(fallback);
    }
    // CBehaviorMeleeAttack::StateEnter (0x003baef8; selection loop
    // 0x003bb204-0x003bb290) compares the absolute delta between target
    // distance and EnemyAttackInfo+0x38 for every resolved attack. A smaller
    // delta replaces the winner; an exact tie does so only when
    // random(0, 100) <= 49. The portable attack reader exposes that exact
    // serialized +0x38 float as maximumAngleDegrees.
    const float dx = playerPosition.x - enemy.position.x;
    const float dy = playerPosition.y - enemy.position.y;
    const float dz = playerPosition.z - enemy.position.z;
    const float targetDistance = std::sqrt(dx * dx + dy * dy + dz * dz);
    std::size_t selectedAnimation = 0;
    float selectedDelta = std::numeric_limits<float>::infinity();
    bool foundAttack = false;
    for (std::size_t candidate = 0; candidate < attackAnimations.size();
         ++candidate) {
        std::int32_t previousAttackId = -1;
        for (const EnemyAnimationSpecialAction* event :
             level_->enemySpecialActions().findAttackEvents(
                 enemy.asset->enemyTypeId, attackAnimations[candidate])) {
            if (event == nullptr || event->attackId < 0 ||
                event->attackId == previousAttackId ||
                event->attackId > std::numeric_limits<std::int16_t>::max()) {
                continue;
            }
            previousAttackId = event->attackId;
            const AttackDefinition* attack = level_->attackConfigs().find(
                static_cast<std::int16_t>(event->attackId));
            if (attack == nullptr) {
                continue;
            }
            const float delta = std::abs(
                attack->maximumAngleDegrees - targetDistance);
            if (!foundAttack || delta < selectedDelta ||
                (delta == selectedDelta &&
                 nativeRandomizer_.range(0, 100) <= 49)) {
                selectedAnimation = candidate;
                selectedDelta = delta;
                foundAttack = true;
            }
        }
    }
    const std::string_view animation = attackAnimations[selectedAnimation];
    if (archetype.animationBank.findClip(animation) == nullptr) {
        return;
    }
    enemy.activeAnimation = animation;
    enemy.animationTimeMilliseconds = 0;
    enemy.animationSpeed = 1.0F;
    enemy.animationLoops = false;
    enemy.animationReversed = false;
    enemy.meleeAttackActive = true;
    enemy.meleeSenseActive = false;
}

void LevelEnemyRuntime::startSandmanGroundAttack(LevelEnemyState& enemy) {
    if (level_ == nullptr || enemy.asset == nullptr ||
        enemy.asset->archetypeIndex >= level_->enemyArchetypes().size()) {
        return;
    }
    constexpr std::string_view kGroundAttack = "ground_attack1";
    const EnemyArchetypeAsset& archetype =
        level_->enemyArchetypes()[enemy.asset->archetypeIndex];
    if (archetype.animationBank.findClip(kGroundAttack) == nullptr) {
        return;
    }
    enemy.sandmanTask = SandmanBossTaskState::GroundAttack;
    enemy.behavior = EnemyBehaviorState::AttackRange;
    enemy.activeAnimation = kGroundAttack;
    enemy.animationTimeMilliseconds = 0;
    enemy.animationSpeed = 1.0F;
    enemy.animationLoops = false;
    enemy.animationReversed = false;
    enemy.meleeAttackActive = true;
}

void LevelEnemyRuntime::startSandmanJump(
    LevelEnemyState& enemy, const assets::Vector3& playerPosition,
    const LevelCollision* collision) {
    if (level_ == nullptr || enemy.asset == nullptr ||
        enemy.asset->archetypeIndex >= level_->enemyArchetypes().size()) {
        return;
    }
    const EnemyArchetypeAsset& archetype =
        level_->enemyArchetypes()[enemy.asset->archetypeIndex];
    constexpr std::string_view kJumpRise = "idle_to_jump_to_air";
    constexpr std::string_view kJumpFall = "air_to_fall_to_idle";
    const assets::ColladaAnimationClip* rise =
        archetype.animationBank.findClip(kJumpRise);
    const assets::ColladaAnimationClip* fall =
        archetype.animationBank.findClip(kJumpFall);
    if (rise == nullptr || fall == nullptr) {
        return;
    }

    const float x = playerPosition.x - enemy.position.x;
    const float y = playerPosition.y - enemy.position.y;
    const float distance = std::hypot(x, y);
    const assets::Vector3 direction =
        distance > std::numeric_limits<float>::epsilon()
            ? assets::Vector3{x / distance, y / distance, 0.0F}
            : enemy.facing;
    enemy.sandmanJumpStart = enemy.position;
    // CBoss::Jump mode 1 (0x0032935c) deliberately lands beyond the player
    // on the current boss-to-player line. The original distance comes from
    // the mean of the authored near/mid attack ranges; 500 cm is the
    // first-level value recovered from that state path.
    enemy.sandmanJumpTarget =
        {playerPosition.x + direction.x * 500.0F,
         playerPosition.y + direction.y * 500.0F, playerPosition.z};
    if (collision != nullptr) {
        float supportHeight = 0.0F;
        if (collision->groundHeight(enemy.sandmanJumpTarget, 1000.0F,
                                    3000.0F, supportHeight)) {
            enemy.sandmanJumpTarget.z = supportHeight;
        }
    }
    enemy.sandmanJumpElapsedMilliseconds = 0;
    enemy.sandmanJumpDurationMilliseconds =
        rise->durationMilliseconds() + fall->durationMilliseconds();
    enemy.sandmanTask = SandmanBossTaskState::Jump;
    enemy.behavior = EnemyBehaviorState::Chasing;
    enemy.activeAnimation = kJumpRise;
    enemy.animationTimeMilliseconds = 0;
    enemy.animationSpeed = 1.0F;
    enemy.animationLoops = false;
    enemy.animationReversed = false;
    enemy.meleeAttackActive = false;
    setFacing(enemy, direction);
}

void LevelEnemyRuntime::updateSandmanBoss(
    LevelEnemyState& enemy, std::uint32_t elapsedMilliseconds,
    const assets::Vector3& playerPosition, const LevelCollision* collision) {
    if (level_ == nullptr || enemy.asset == nullptr ||
        enemy.asset->archetypeIndex >= level_->enemyArchetypes().size()) {
        return;
    }
    const EnemyArchetypeAsset& archetype =
        level_->enemyArchetypes()[enemy.asset->archetypeIndex];
    const float toPlayerX = playerPosition.x - enemy.position.x;
    const float toPlayerY = playerPosition.y - enemy.position.y;
    const float playerDistance = std::hypot(toPlayerX, toPlayerY);
    if (playerDistance > std::numeric_limits<float>::epsilon()) {
        setFacing(enemy, {toPlayerX / playerDistance,
                          toPlayerY / playerDistance, 0.0F});
    }

    if (enemy.sandmanTask == SandmanBossTaskState::None) {
        startSandmanGroundAttack(enemy);
        return;
    }
    if (enemy.sandmanTask == SandmanBossTaskState::GroundAttack) {
        const assets::ColladaAnimationClip* clip =
            archetype.animationBank.findClip(enemy.activeAnimation);
        if (clip != nullptr && enemy.animationTimeMilliseconds >=
                                   clip->durationMilliseconds()) {
            constexpr std::string_view kRecovery =
                "ground_attack1_to_idle";
            enemy.sandmanTask = SandmanBossTaskState::GroundAttackRecovery;
            enemy.activeAnimation = kRecovery;
            enemy.animationTimeMilliseconds = 0;
            enemy.animationSpeed = 1.0F;
            enemy.animationLoops = false;
            enemy.animationReversed = false;
            enemy.meleeAttackActive = false;
        }
        return;
    }
    if (enemy.sandmanTask == SandmanBossTaskState::GroundAttackRecovery) {
        const assets::ColladaAnimationClip* clip =
            archetype.animationBank.findClip(enemy.activeAnimation);
        if (clip != nullptr && enemy.animationTimeMilliseconds >=
                                   clip->durationMilliseconds()) {
            startSandmanJump(enemy, playerPosition, collision);
        }
        return;
    }

    const assets::ColladaAnimationClip* rise =
        archetype.animationBank.findClip("idle_to_jump_to_air");
    if (enemy.sandmanJumpDurationMilliseconds == 0 || rise == nullptr) {
        enemy.sandmanTask = SandmanBossTaskState::None;
        startSandmanGroundAttack(enemy);
        return;
    }
    enemy.sandmanJumpElapsedMilliseconds =
        std::min(enemy.sandmanJumpDurationMilliseconds,
                 enemy.sandmanJumpElapsedMilliseconds + elapsedMilliseconds);
    const float factor =
        static_cast<float>(enemy.sandmanJumpElapsedMilliseconds) /
        static_cast<float>(enemy.sandmanJumpDurationMilliseconds);
    enemy.position.x = enemy.sandmanJumpStart.x +
                       (enemy.sandmanJumpTarget.x -
                        enemy.sandmanJumpStart.x) *
                           factor;
    enemy.position.y = enemy.sandmanJumpStart.y +
                       (enemy.sandmanJumpTarget.y -
                        enemy.sandmanJumpStart.y) *
                           factor;
    enemy.position.z = enemy.sandmanJumpStart.z +
                       (enemy.sandmanJumpTarget.z -
                        enemy.sandmanJumpStart.z) *
                           factor +
                       std::sin(factor * 3.14159265358979323846F) * 400.0F;
    setFacing(enemy, enemy.facing);
    if (enemy.sandmanJumpElapsedMilliseconds >=
            rise->durationMilliseconds() &&
        enemy.activeAnimation != "air_to_fall_to_idle") {
        enemy.activeAnimation = "air_to_fall_to_idle";
        enemy.animationTimeMilliseconds = 0;
        enemy.animationSpeed = 1.0F;
        enemy.animationLoops = false;
        enemy.animationReversed = false;
    }
    if (enemy.sandmanJumpElapsedMilliseconds >=
        enemy.sandmanJumpDurationMilliseconds) {
        enemy.position = enemy.sandmanJumpTarget;
        setFacing(enemy, enemy.facing);
        enemy.sandmanTask = SandmanBossTaskState::None;
        enemy.sandmanJumpElapsedMilliseconds = 0;
        enemy.sandmanJumpDurationMilliseconds = 0;
        startSandmanGroundAttack(enemy);
    }
}

void LevelEnemyRuntime::applyRhinoDamage(LevelEnemyState& enemy,
                                         float damage) noexcept {
    if (damage <= 0.0F || enemy.maximumHealth <= 0.0F) {
        return;
    }
    enemy.health = std::max(0.0F, enemy.health - damage);
    const auto healthPercent = static_cast<std::int32_t>(
        enemy.health * 100.0F / enemy.maximumHealth);
    // CBoss::ParseLocalAiMessage (0x0032dea8) gates the task tables and
    // clamps health at their boundary. The first branch is deliberately an
    // else-if: one hit cannot skip phase one even if its raw damage crosses
    // both thresholds.
    if (healthPercent < 67 && enemy.rhinoPhase < 1) {
        enemy.health = enemy.maximumHealth * 0.66F;
        enemy.rhinoPhase = 1;
        enemy.rhinoSequenceCycle = 0;
    } else if (healthPercent <= 33 && enemy.rhinoPhase <= 1) {
        enemy.health = enemy.maximumHealth * 0.33F;
        enemy.rhinoPhase = 2;
        enemy.rhinoSequenceCycle = 0;
    }
}

void LevelEnemyRuntime::enterRhinoTask(LevelEnemyState& enemy,
                                       RhinoBossTaskState task) {
    enemy.rhinoTask = task;
    enemy.rhinoTaskElapsedMilliseconds = 0;
    enemy.animationTimeMilliseconds = 0;
    enemy.animationSpeed = 1.0F;
    enemy.animationReversed = false;
    enemy.meleeAttackActive = false;

    std::string_view animation;
    bool loop = false;
    switch (task) {
    case RhinoBossTaskState::Approach:
        animation = "run";
        loop = true;
        enemy.behavior = EnemyBehaviorState::Chasing;
        break;
    case RhinoBossTaskState::Melee:
        animation = "punch_left";
        enemy.behavior = EnemyBehaviorState::AttackRange;
        enemy.meleeAttackActive = true;
        break;
    case RhinoBossTaskState::DashReady:
        animation = "idle_charge_run";
        enemy.behavior = EnemyBehaviorState::AttackRange;
        break;
    case RhinoBossTaskState::DashRush:
        animation = "rush";
        loop = true;
        enemy.behavior = EnemyBehaviorState::AttackRange;
        break;
    case RhinoBossTaskState::DashSuccess:
        animation = "run_bash_attack";
        enemy.behavior = EnemyBehaviorState::AttackRange;
        enemy.meleeAttackActive = true;
        break;
    case RhinoBossTaskState::DashSkid:
        animation = "bash_attack_to_idle";
        enemy.behavior = EnemyBehaviorState::AttackRange;
        break;
    case RhinoBossTaskState::DashFailed:
        animation = "run_bash_to_dizzy";
        enemy.behavior = EnemyBehaviorState::AttackRange;
        break;
    case RhinoBossTaskState::DashFailedStruggle:
        // State 84 has no direct BehaviorState animation mapping.  The
        // failed-dash transition resolves to Rhino's shipped looping dizzy
        // clip while its recovered 5000 ms struggle timer runs.
        animation = "dizzy";
        loop = true;
        enemy.behavior = EnemyBehaviorState::AttackRange;
        break;
    case RhinoBossTaskState::ThrowApproach:
        animation = "run";
        loop = true;
        enemy.behavior = EnemyBehaviorState::Chasing;
        break;
    case RhinoBossTaskState::ThrowReady:
        // CBehaviorThrow state 86.
        animation = "idle_to_grab_ready";
        enemy.behavior = EnemyBehaviorState::AttackRange;
        break;
    case RhinoBossTaskState::ThrowRush:
        // CBehaviorThrow state 87. Its key-zero attack-18 sense marker
        // decides whether action 9 begins or state 91 handles the miss.
        animation = "idle_to_grab";
        enemy.behavior = EnemyBehaviorState::AttackRange;
        break;
    case RhinoBossTaskState::ThrowCatch:
        animation = "grab_to_hold";
        enemy.behavior = EnemyBehaviorState::AttackRange;
        break;
    case RhinoBossTaskState::ThrowStruggle:
        animation = "struggleing";
        loop = true;
        enemy.behavior = EnemyBehaviorState::AttackRange;
        break;
    case RhinoBossTaskState::ThrowSuccess:
        animation = "struggle_to_hurt_to_idle";
        enemy.behavior = EnemyBehaviorState::AttackRange;
        break;
    case RhinoBossTaskState::ThrowRelease:
        animation = "hold_to_throw";
        enemy.behavior = EnemyBehaviorState::AttackRange;
        break;
    case RhinoBossTaskState::ThrowMiss:
        // CBehaviorThrow state 91.
        animation = "grab_to_failed";
        enemy.behavior = EnemyBehaviorState::AttackRange;
        break;
    case RhinoBossTaskState::None:
        animation = "idle";
        loop = true;
        enemy.behavior = EnemyBehaviorState::Idle;
        break;
    }
    enemy.activeAnimation = animation;
    enemy.animationLoops = loop;
}

void LevelEnemyRuntime::updateRhinoBoss(
    LevelEnemyState& enemy, std::uint32_t elapsedMilliseconds,
    const assets::Vector3& playerPosition, const LevelCollision* collision,
    bool quickTimeActionPressed) {
    if (level_ == nullptr || enemy.asset == nullptr ||
        enemy.asset->archetypeIndex >= level_->enemyArchetypes().size()) {
        return;
    }
    const EnemyArchetypeAsset& archetype =
        level_->enemyArchetypes()[enemy.asset->archetypeIndex];
    const auto animationFinished = [&enemy, &archetype]() {
        const assets::ColladaAnimationClip* clip =
            archetype.animationBank.findClip(enemy.activeAnimation);
        return clip != nullptr && !enemy.animationLoops &&
               enemy.animationTimeMilliseconds >=
                   clip->durationMilliseconds();
    };
    enemy.rhinoTaskElapsedMilliseconds =
        std::min<std::uint32_t>(
            std::numeric_limits<std::uint32_t>::max() - elapsedMilliseconds,
            enemy.rhinoTaskElapsedMilliseconds) +
        elapsedMilliseconds;

    const float toPlayerX = playerPosition.x - enemy.position.x;
    const float toPlayerY = playerPosition.y - enemy.position.y;
    const float playerDistance = std::hypot(toPlayerX, toPlayerY);
    const assets::Vector3 playerDirection =
        playerDistance > std::numeric_limits<float>::epsilon()
            ? assets::Vector3{toPlayerX / playerDistance,
                              toPlayerY / playerDistance, 0.0F}
            : enemy.facing;

    if (rhinoQuickTimeEnemyId_ == enemy.asset->objectId &&
        rhinoQuickTimeAction_.active()) {
        if (!rhinoQuickTimeAction_
                 .update(elapsedMilliseconds, quickTimeActionPressed)) {
            rhinoQuickTimeAction_.cancel();
            rhinoQuickTimeEnemyId_ = -1;
            enterRhinoTask(enemy, RhinoBossTaskState::ThrowMiss);
            return;
        }
        if (const auto entered =
                rhinoQuickTimeAction_.consumeEnteredActionState()) {
            switch (*entered) {
            case 9:
                enterRhinoTask(enemy, RhinoBossTaskState::ThrowCatch);
                break;
            case 6:
                enterRhinoTask(enemy, RhinoBossTaskState::ThrowStruggle);
                break;
            case 7:
                enterRhinoTask(enemy, RhinoBossTaskState::ThrowSuccess);
                break;
            case 8:
                enterRhinoTask(enemy, RhinoBossTaskState::ThrowRelease);
                break;
            default:
                break;
            }
        }
        enemy.activeAnimation =
            std::string(rhinoQuickTimeAction_.npcAnimation());
        enemy.animationTimeMilliseconds =
            rhinoQuickTimeAction_.npcAnimationMilliseconds();
        enemy.animationLoops = rhinoQuickTimeAction_.animationLoops();
        for (const QuickTimeActionHit& hit :
             rhinoQuickTimeAction_.consumeHits()) {
            if (hit.target == QuickTimeActionTarget::Npc) {
                applyRhinoDamage(enemy, hit.damage);
            } else {
                pendingPlayerHits_.push_back(
                    {enemy.asset->objectId, -1, hit.damage});
            }
        }
        if (rhinoQuickTimeAction_.consumeCompletion().has_value()) {
            finishRhinoThrow(enemy);
        }
        return;
    }

    if (enemy.rhinoTask == RhinoBossTaskState::None) {
        // _GLOBAL__I_CBoss (0x00329cd4) begins every Rhino phase with task 3,
        // repeats=2. ParseAiTaskInfo (0x0032c0c8) injects task 1 movement;
        // PushAiTask/PopAiTask use FIFO list order.
        enemy.rhinoMeleeAttacksRemaining = 2;
        enterRhinoTask(enemy, RhinoBossTaskState::Approach);
        return;
    }

    if (enemy.rhinoTask == RhinoBossTaskState::Approach) {
        const AttackDefinition* meleeAttack = level_->attackConfigs().find(12);
        const float attackRange =
            meleeAttack == nullptr ? 0.0F : meleeAttack->maximumReach();
        if (playerDistance <= attackRange) {
            setFacing(enemy, playerDirection);
            enterRhinoTask(enemy, RhinoBossTaskState::Melee);
            return;
        }
        const float travel =
            enemy.asset->lineSpeedCentimetersPerMillisecond *
            static_cast<float>(elapsedMilliseconds);
        if (travel > 0.0F &&
            playerDistance > std::numeric_limits<float>::epsilon()) {
            assets::Vector3 desired = enemy.position;
            const float step = std::min(travel,
                                        std::max(playerDistance - attackRange,
                                                 0.0F));
            desired.x += playerDirection.x * step;
            desired.y += playerDirection.y * step;
            if (collision != nullptr) {
                assets::Vector3 resolved;
                (void)collision->resolveGroundMotion(enemy.position, desired,
                                                     resolved);
                enemy.position = resolved;
            } else {
                enemy.position = desired;
            }
            setFacing(enemy, playerDirection);
        }
        return;
    }

    if (enemy.rhinoTask == RhinoBossTaskState::Melee) {
        setFacing(enemy, playerDirection);
        if (!animationFinished()) {
            return;
        }
        enemy.meleeAttackActive = false;
        if (enemy.rhinoMeleeAttacksRemaining > 0) {
            --enemy.rhinoMeleeAttacksRemaining;
        }
        if (enemy.rhinoMeleeAttacksRemaining > 0) {
            enterRhinoTask(enemy, RhinoBossTaskState::Melee);
        } else if (enemy.rhinoPhase > 0) {
            // Phase one/two insert task 12 (CBehaviorThrow) before task 11.
            enterRhinoTask(enemy, RhinoBossTaskState::ThrowApproach);
        } else {
            enterRhinoTask(enemy, RhinoBossTaskState::DashReady);
        }
        return;
    }

    if (enemy.rhinoTask == RhinoBossTaskState::ThrowApproach) {
        if (playerDistance <= kRhinoGrabRangeCentimeters) {
            setFacing(enemy, playerDirection);
            enterRhinoTask(enemy, RhinoBossTaskState::ThrowReady);
            return;
        }
        const float travel =
            enemy.asset->lineSpeedCentimetersPerMillisecond *
            static_cast<float>(elapsedMilliseconds);
        if (travel > 0.0F &&
            playerDistance > std::numeric_limits<float>::epsilon()) {
            assets::Vector3 desired = enemy.position;
            const float step = std::min(
                travel, std::max(playerDistance - kRhinoGrabRangeCentimeters,
                                 0.0F));
            desired.x += playerDirection.x * step;
            desired.y += playerDirection.y * step;
            if (collision != nullptr) {
                assets::Vector3 resolved;
                (void)collision->resolveGroundMotion(enemy.position, desired,
                                                     resolved);
                enemy.position = resolved;
            } else {
                enemy.position = desired;
            }
            setFacing(enemy, playerDirection);
        }
        return;
    }

    if (enemy.rhinoTask == RhinoBossTaskState::ThrowReady) {
        setFacing(enemy, playerDirection);
        if (animationFinished()) {
            enterRhinoTask(enemy, RhinoBossTaskState::ThrowRush);
        }
        return;
    }

    if (enemy.rhinoTask == RhinoBossTaskState::ThrowRush) {
        setFacing(enemy, playerDirection);
        if (enemy.rhinoTaskElapsedMilliseconds <= elapsedMilliseconds &&
            playerDistance <= kRhinoGrabRangeCentimeters &&
            rhinoQuickTimeAction_.begin(9)) {
            rhinoQuickTimeEnemyId_ = enemy.asset->objectId;
            (void)rhinoQuickTimeAction_.consumeEnteredActionState();
            (void)alignRhinoThrowCatch(enemy, playerPosition);
            enterRhinoTask(enemy, RhinoBossTaskState::ThrowCatch);
            enemy.activeAnimation =
                std::string(rhinoQuickTimeAction_.npcAnimation());
            enemy.animationTimeMilliseconds =
                rhinoQuickTimeAction_.npcAnimationMilliseconds();
            enemy.animationLoops = rhinoQuickTimeAction_.animationLoops();
            return;
        }
        if (animationFinished()) {
            enterRhinoTask(enemy, RhinoBossTaskState::ThrowMiss);
        }
        return;
    }

    if (enemy.rhinoTask == RhinoBossTaskState::ThrowMiss) {
        if (animationFinished()) {
            finishRhinoThrow(enemy);
        }
        return;
    }

    if (enemy.rhinoTask == RhinoBossTaskState::DashReady) {
        setFacing(enemy, playerDirection);
        if (animationFinished()) {
            enemy.rhinoDashDirection = playerDirection;
            enterRhinoTask(enemy, RhinoBossTaskState::DashRush);
        }
        return;
    }

    if (enemy.rhinoTask == RhinoBossTaskState::DashRush) {
        const float maximumTurn =
            kRhinoDashAngularSpeedDegreesPerSecond *
            static_cast<float>(elapsedMilliseconds) / 1000.0F;
        enemy.rhinoDashDirection = turnToward2D(
            enemy.rhinoDashDirection, playerDirection, maximumTurn);
        const assets::Vector3 previous = enemy.position;
        assets::Vector3 desired = previous;
        const float travel = kRhinoDashSpeedCentimetersPerSecond *
                             static_cast<float>(elapsedMilliseconds) /
                             1000.0F;
        desired.x += enemy.rhinoDashDirection.x * travel;
        desired.y += enemy.rhinoDashDirection.y * travel;
        assets::Vector3 resolved = desired;
        if (collision != nullptr) {
            (void)collision->resolveGroundMotion(previous, desired, resolved);
        }
        enemy.position = resolved;
        setFacing(enemy, enemy.rhinoDashDirection);

        const float contactRadius =
            enemy.collisionRadius + kPlayerCollisionRadiusCentimeters;
        if (segmentTouchesCircle2D(previous, resolved, playerPosition,
                                   contactRadius)) {
            enterRhinoTask(enemy, RhinoBossTaskState::DashSuccess);
            return;
        }
        const float resolvedTravel =
            std::hypot(resolved.x - previous.x, resolved.y - previous.y);
        if (collision != nullptr && resolvedTravel < travel * 0.5F) {
            enterRhinoTask(enemy, RhinoBossTaskState::DashFailed);
        }
        return;
    }

    if (enemy.rhinoTask == RhinoBossTaskState::DashSuccess) {
        if (animationFinished()) {
            enemy.meleeAttackActive = false;
            enterRhinoTask(enemy, RhinoBossTaskState::DashSkid);
        }
        return;
    }
    if (enemy.rhinoTask == RhinoBossTaskState::DashSkid) {
        if (animationFinished()) {
            ++enemy.rhinoSequenceCycle;
            enemy.rhinoMeleeAttacksRemaining = 2;
            enterRhinoTask(enemy, RhinoBossTaskState::Approach);
        }
        return;
    }
    if (enemy.rhinoTask == RhinoBossTaskState::DashFailed) {
        if (animationFinished()) {
            enterRhinoTask(enemy,
                           RhinoBossTaskState::DashFailedStruggle);
        }
        return;
    }
    if (enemy.rhinoTask == RhinoBossTaskState::DashFailedStruggle &&
        enemy.rhinoTaskElapsedMilliseconds >=
            kRhinoFailedStruggleMilliseconds) {
        ++enemy.rhinoSequenceCycle;
        enemy.rhinoMeleeAttacksRemaining = 2;
        enterRhinoTask(enemy, RhinoBossTaskState::Approach);
    }
}

void LevelEnemyRuntime::finishRhinoThrow(LevelEnemyState& enemy) {
    rhinoQuickTimeAction_.cancel();
    rhinoQuickTimeEnemyId_ = -1;
    enterRhinoTask(enemy, RhinoBossTaskState::DashReady);
}

void LevelEnemyRuntime::enterRobotPhantomTask(
    LevelEnemyState& enemy, RobotPhantomTaskState task) {
    enemy.robotPhantomTask = task;
    enemy.robotPhantomTaskElapsedMilliseconds = 0;
    enemy.behavior = EnemyBehaviorState::AttackRange;
    enemy.animationTimeMilliseconds = 0;
    enemy.animationSpeed = 1.0F;
    enemy.animationLoops = false;
    enemy.animationReversed = false;
    enemy.meleeAttackActive = false;

    switch (task) {
    case RobotPhantomTaskState::ApproachMelee:
    case RobotPhantomTaskState::ApproachRange:
        enemy.activeAnimation = "run";
        enemy.animationLoops = true;
        break;
    case RobotPhantomTaskState::RushReady:
        enemy.activeAnimation = "rush_attack_ready";
        break;
    case RobotPhantomTaskState::RushFirst:
        enemy.activeAnimation = "rush_attack1";
        enemy.meleeAttackActive = true;
        break;
    case RobotPhantomTaskState::RushSecond:
        enemy.activeAnimation = "rush_attack2";
        enemy.meleeAttackActive = true;
        break;
    case RobotPhantomTaskState::RushRecovery:
        enemy.activeAnimation = "rush_attack2_to_idle";
        break;
    case RobotPhantomTaskState::ConcealReady:
        enemy.activeAnimation = "conceal_ready";
        break;
    case RobotPhantomTaskState::ConcealHidden:
        // CBehaviorConceal state 131 (0x003aa7e0) keeps running while the
        // SceneNodeComponent is hidden for exactly 500 ms.
        enemy.visible = false;
        enemy.activeAnimation = "conceal_ready";
        break;
    case RobotPhantomTaskState::ConcealAttack:
        enemy.visible = true;
        enemy.activeAnimation = "conceal_attack";
        enemy.meleeAttackActive = true;
        break;
    case RobotPhantomTaskState::ConcealRecovery:
        enemy.activeAnimation = "conceal_attack_to_idle";
        break;
    case RobotPhantomTaskState::ThrowReady:
        enemy.activeAnimation = "throw_ready";
        break;
    case RobotPhantomTaskState::Throw:
        enemy.activeAnimation = "throw";
        break;
    case RobotPhantomTaskState::ThrowWait:
        enemy.activeAnimation = "throw_wait";
        enemy.animationLoops = true;
        break;
    case RobotPhantomTaskState::ThrowRecovery:
        enemy.activeAnimation = "throw_wait_to_idle";
        break;
    case RobotPhantomTaskState::None:
        enemy.behavior = EnemyBehaviorState::Idle;
        enemy.activeAnimation = "idle";
        enemy.animationLoops = true;
        break;
    }
}

void LevelEnemyRuntime::advanceRobotPhantomSequence(
    LevelEnemyState& enemy) {
    enemy.robotPhantomSequenceIndex =
        (enemy.robotPhantomSequenceIndex + 1U) % 4U;
    switch (enemy.robotPhantomSequenceIndex) {
    case 0:
        enterRobotPhantomTask(enemy,
                              RobotPhantomTaskState::ApproachMelee);
        break;
    case 1:
    case 3:
        enterRobotPhantomTask(enemy,
                              RobotPhantomTaskState::ConcealReady);
        break;
    case 2:
        enterRobotPhantomTask(enemy,
                              RobotPhantomTaskState::ApproachRange);
        break;
    default: break;
    }
}

assets::Vector3 LevelEnemyRuntime::robotPhantomHandPosition(
    const LevelEnemyState& enemy) const {
    const assets::Vector3 fallback{
        enemy.position.x, enemy.position.y,
        enemy.position.z + enemy.collisionHeight * 0.75F};
    if (level_ == nullptr || enemy.asset == nullptr ||
        enemy.asset->archetypeIndex >= level_->enemyArchetypes().size()) {
        return fallback;
    }
    const EnemyArchetypeAsset& archetype =
        level_->enemyArchetypes()[enemy.asset->archetypeIndex];
    const assets::ColladaAnimationClip* clip =
        archetype.animationBank.findClip(enemy.activeAnimation);
    if (clip == nullptr) {
        return fallback;
    }
    std::array<float, 16> handTransform{};
    const std::uint32_t localTime = std::min(
        enemy.animationTimeMilliseconds, clip->durationMilliseconds());
    Result handResult = assets::evaluateColladaSceneNodeTransform(
        archetype.mesh, archetype.animationBank,
        clip->startMilliseconds + localTime, "R_Hand_Dummy",
        handTransform);
    if (!handResult) {
        handResult = assets::evaluateColladaSceneNodeTransform(
            archetype.mesh, archetype.animationBank,
            clip->startMilliseconds + localTime, "Bip01_R_Hand",
            handTransform);
    }
    return handResult
               ? transformPoint(enemy.worldTransform,
                                transformPoint(handTransform))
               : fallback;
}

void LevelEnemyRuntime::throwRobotPhantomBoomerang(
    LevelEnemyState& enemy,
    const assets::Vector3& playerPosition) {
    if (level_ == nullptr || enemy.asset == nullptr) {
        return;
    }
    if (std::any_of(
            boomerangs_.begin(), boomerangs_.end(),
            [&enemy](const EnemyBoomerangState& boomerang) {
                return boomerang.active && enemy.asset != nullptr &&
                       boomerang.sourceObjectId == enemy.asset->objectId;
            })) {
        return;
    }
    const EnemyAttributeDefinition* attributes =
        level_->enemyAttributeConfigs().find(enemy.asset->enemyTypeId);
    const std::int32_t rangeMapId =
        attributes == nullptr ||
                attributes->rangedAttackTypeMapIndices.empty()
            ? -1
            : attributes->rangedAttackTypeMapIndices.front();
    const EnemyRangeAttackDefinition* attack =
        level_->enemyRangeAttackConfigs().findByMapId(rangeMapId);
    if (rangeMapId < 0 || resolveEnemyRangeWeaponType(rangeMapId) != 30 ||
        attack == nullptr) {
        return;
    }
    const assets::Vector3 origin = robotPhantomHandPosition(enemy);
    const assets::Vector3 target{
        playerPosition.x, playerPosition.y,
        playerPosition.z + kPlayerCollisionHeightCentimeters * 0.5F};
    const float deltaX = target.x - origin.x;
    const float deltaY = target.y - origin.y;
    const float deltaZ = target.z - origin.z;
    const float distance =
        std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
    const assets::Vector3 direction =
        distance > std::numeric_limits<float>::epsilon()
            ? assets::Vector3{deltaX / distance, deltaY / distance,
                              deltaZ / distance}
            : enemy.facing;
    const assets::Vector3 velocity{
        direction.x * kRobotPhantomBoomerangSpeedCentimetersPerSecond,
        direction.y * kRobotPhantomBoomerangSpeedCentimetersPerSecond,
        direction.z * kRobotPhantomBoomerangSpeedCentimetersPerSecond};
    boomerangs_.push_back(
        {enemy.asset->objectId,
         enemy.asset->roomId,
         origin,
         velocity,
         target,
         direction,
         kRobotPhantomBoomerangSpeedCentimetersPerSecond,
         attack->damage,
         0,
         EnemyBoomerangPhase::Outbound,
         false,
         true});
    pendingProjectileEvents_.push_back(
        {EnemyProjectileEventKind::Spawned, enemy.asset->objectId, origin,
         velocity, 0.0F});
}

void LevelEnemyRuntime::updateRobotPhantomBoss(
    LevelEnemyState& enemy, std::uint32_t elapsedMilliseconds,
    const assets::Vector3& playerPosition,
    const assets::Vector3& playerFacing,
    const LevelCollision* collision) {
    if (level_ == nullptr || enemy.asset == nullptr ||
        enemy.asset->archetypeIndex >= level_->enemyArchetypes().size()) {
        return;
    }
    enemy.robotPhantomTaskElapsedMilliseconds =
        static_cast<std::uint32_t>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(
                enemy.robotPhantomTaskElapsedMilliseconds) +
                elapsedMilliseconds,
            std::numeric_limits<std::uint32_t>::max()));

    const EnemyArchetypeAsset& archetype =
        level_->enemyArchetypes()[enemy.asset->archetypeIndex];
    const auto animationFinished = [&enemy, &archetype]() {
        const assets::ColladaAnimationClip* clip =
            archetype.animationBank.findClip(enemy.activeAnimation);
        return clip == nullptr ||
               enemy.animationTimeMilliseconds >=
                   clip->durationMilliseconds();
    };
    const float toPlayerX = playerPosition.x - enemy.position.x;
    const float toPlayerY = playerPosition.y - enemy.position.y;
    const float playerDistance = std::hypot(toPlayerX, toPlayerY);
    const assets::Vector3 playerDirection =
        playerDistance > std::numeric_limits<float>::epsilon()
            ? assets::Vector3{toPlayerX / playerDistance,
                              toPlayerY / playerDistance, 0.0F}
            : enemy.facing;

    if (enemy.robotPhantomTask == RobotPhantomTaskState::None) {
        // CBoss::InitAiTask (0x0032b66c) starts phase zero at the first of
        // the four Robot Phantom records emitted by _GLOBAL__I_CBoss.
        enemy.robotPhantomSequenceIndex = 0;
        enterRobotPhantomTask(enemy,
                              RobotPhantomTaskState::ApproachMelee);
        return;
    }
    if (enemy.robotPhantomTask ==
            RobotPhantomTaskState::ApproachMelee ||
        enemy.robotPhantomTask ==
            RobotPhantomTaskState::ApproachRange) {
        if (playerDistance <=
            kRobotPhantomApproachRangeCentimeters) {
            setFacing(enemy, playerDirection);
            enterRobotPhantomTask(
                enemy,
                enemy.robotPhantomTask ==
                        RobotPhantomTaskState::ApproachMelee
                    ? RobotPhantomTaskState::RushReady
                    : RobotPhantomTaskState::ThrowReady);
            return;
        }
        const float travel =
            enemy.asset->lineSpeedCentimetersPerMillisecond *
            static_cast<float>(elapsedMilliseconds);
        if (travel > 0.0F) {
            assets::Vector3 desired = enemy.position;
            const float step = std::min(
                travel,
                playerDistance -
                    kRobotPhantomApproachRangeCentimeters);
            desired.x += playerDirection.x * step;
            desired.y += playerDirection.y * step;
            if (collision != nullptr) {
                assets::Vector3 resolved;
                (void)collision->resolveGroundMotion(enemy.position,
                                                     desired, resolved);
                enemy.position = resolved;
            } else {
                enemy.position = desired;
            }
            setFacing(enemy, playerDirection);
        }
        return;
    }
    if (enemy.robotPhantomTask == RobotPhantomTaskState::RushReady) {
        setFacing(enemy, playerDirection);
        if (animationFinished()) {
            enterRobotPhantomTask(enemy,
                                  RobotPhantomTaskState::RushFirst);
        }
        return;
    }
    if (enemy.robotPhantomTask == RobotPhantomTaskState::RushFirst) {
        setFacing(enemy, playerDirection);
        if (animationFinished()) {
            enterRobotPhantomTask(enemy,
                                  RobotPhantomTaskState::RushSecond);
        }
        return;
    }
    if (enemy.robotPhantomTask == RobotPhantomTaskState::RushSecond) {
        setFacing(enemy, playerDirection);
        if (animationFinished()) {
            enterRobotPhantomTask(enemy,
                                  RobotPhantomTaskState::RushRecovery);
        }
        return;
    }
    if (enemy.robotPhantomTask == RobotPhantomTaskState::RushRecovery) {
        if (animationFinished()) {
            advanceRobotPhantomSequence(enemy);
        }
        return;
    }
    if (enemy.robotPhantomTask == RobotPhantomTaskState::ConcealReady) {
        setFacing(enemy, playerDirection);
        if (animationFinished()) {
            enterRobotPhantomTask(enemy,
                                  RobotPhantomTaskState::ConcealHidden);
        }
        return;
    }
    if (enemy.robotPhantomTask == RobotPhantomTaskState::ConcealHidden) {
        if (enemy.robotPhantomTaskElapsedMilliseconds <
            kRobotPhantomConcealMilliseconds) {
            return;
        }
        const float facingLength =
            std::hypot(playerFacing.x, playerFacing.y);
        const assets::Vector3 targetFacing =
            facingLength > std::numeric_limits<float>::epsilon()
                ? assets::Vector3{playerFacing.x / facingLength,
                                  playerFacing.y / facingLength, 0.0F}
                : assets::Vector3{1.0F, 0.0F, 0.0F};
        const float separation =
            kPlayerCollisionRadiusCentimeters + enemy.collisionRadius;
        assets::Vector3 destination{
            playerPosition.x - targetFacing.x * separation,
            playerPosition.y - targetFacing.y * separation,
            playerPosition.z};
        if (collision != nullptr) {
            const assets::Vector3 start{
                enemy.position.x, enemy.position.y,
                enemy.position.z + 50.0F};
            const assets::Vector3 end{destination.x, destination.y,
                                      destination.z + 50.0F};
            // CBehaviorConceal::BehaviorUpdate (0x003aa7e0) falls back to
            // the target position when SegmentCollision rejects the behind
            // point; it does not search for an invented alternate offset.
            if (collision->segmentBlocked(start, end)) {
                destination = playerPosition;
            }
        }
        enemy.position = destination;
        const float newX = playerPosition.x - enemy.position.x;
        const float newY = playerPosition.y - enemy.position.y;
        const float newLength = std::hypot(newX, newY);
        setFacing(enemy,
                  newLength > std::numeric_limits<float>::epsilon()
                      ? assets::Vector3{newX / newLength,
                                        newY / newLength, 0.0F}
                      : playerDirection);
        enterRobotPhantomTask(enemy,
                              RobotPhantomTaskState::ConcealAttack);
        return;
    }
    if (enemy.robotPhantomTask == RobotPhantomTaskState::ConcealAttack) {
        setFacing(enemy, playerDirection);
        if (animationFinished()) {
            enterRobotPhantomTask(enemy,
                                  RobotPhantomTaskState::ConcealRecovery);
        }
        return;
    }
    if (enemy.robotPhantomTask == RobotPhantomTaskState::ConcealRecovery) {
        if (animationFinished()) {
            advanceRobotPhantomSequence(enemy);
        }
        return;
    }
    if (enemy.robotPhantomTask == RobotPhantomTaskState::ThrowReady) {
        setFacing(enemy, playerDirection);
        if (animationFinished()) {
            enterRobotPhantomTask(enemy,
                                  RobotPhantomTaskState::Throw);
        }
        return;
    }
    if (enemy.robotPhantomTask == RobotPhantomTaskState::Throw) {
        setFacing(enemy, playerDirection);
        const assets::ColladaAnimationClip* clip =
            archetype.animationBank.findClip("throw");
        const std::uint32_t releaseTime =
            clip == nullptr
                ? 1U
                : std::max<std::uint32_t>(
                      clip->durationMilliseconds() / 100U, 1U);
        if (enemy.animationTimeMilliseconds >= releaseTime) {
            // Robot Phantom special-action record 182 releases at one
            // percent of `throw`; range map 17 resolves to weapon type 30.
            throwRobotPhantomBoomerang(enemy, playerPosition);
        }
        if (animationFinished()) {
            enterRobotPhantomTask(enemy,
                                  RobotPhantomTaskState::ThrowWait);
        }
        return;
    }
    if (enemy.robotPhantomTask == RobotPhantomTaskState::ThrowWait) {
        setFacing(enemy, playerDirection);
        const auto boomerang = std::find_if(
            boomerangs_.begin(), boomerangs_.end(),
            [&enemy](const EnemyBoomerangState& state) {
                return state.active && enemy.asset != nullptr &&
                       state.sourceObjectId == enemy.asset->objectId;
            });
        if (boomerang != boomerangs_.end() &&
            boomerang->phase == EnemyBoomerangPhase::Ready) {
            enterRobotPhantomTask(enemy,
                                  RobotPhantomTaskState::ThrowRecovery);
        }
        return;
    }
    if (enemy.robotPhantomTask == RobotPhantomTaskState::ThrowRecovery &&
        animationFinished()) {
        for (EnemyBoomerangState& boomerang : boomerangs_) {
            if (boomerang.sourceObjectId == enemy.asset->objectId) {
                boomerang.active = false;
            }
        }
        advanceRobotPhantomSequence(enemy);
    }
}

bool LevelEnemyRuntime::isElectroBoss(
    const LevelEnemyState& enemy) const noexcept {
    // Level 3 object 30418 is a type-6 Boss_Electro cinematic dummy. Native
    // CBoss dispatches by EnemyType, and only type 7 owns the Electro queue.
    return enemy.asset != nullptr && enemy.asset->gameType == "Boss_Electro" &&
           enemy.asset->enemyTypeId == 7;
}

void LevelEnemyRuntime::enterElectroTask(
    LevelEnemyState& enemy, ElectroBossTaskState task) {
    if (enemy.asset != nullptr &&
        enemy.electroTask == ElectroBossTaskState::Rotate &&
        task != ElectroBossTaskState::Rotate) {
        removeElectroPosts(enemy.asset->objectId);
    }
    enemy.electroTask = task;
    enemy.electroTaskElapsedMilliseconds = 0;
    enemy.animationTimeMilliseconds = 0;
    enemy.animationSpeed = 1.0F;
    enemy.animationLoops = false;
    enemy.animationReversed = false;
    enemy.meleeAttackActive = false;
    enemy.behavior = EnemyBehaviorState::AttackRange;

    // These are the shipped type-7 clips selected by native behavior states
    // 24, 92-94, 102-104, and 122-125 respectively.
    switch (task) {
    case ElectroBossTaskState::RangeAttack:
        enemy.activeAnimation = "attack_fall";
        enemy.electroRangeReleased = false;
        break;
    case ElectroBossTaskState::WeakStart:
        enemy.activeAnimation = "stand_to_weak";
        enemy.electroWeakReleased = false;
        break;
    case ElectroBossTaskState::Weak:
        enemy.activeAnimation = "weak";
        enemy.animationLoops = true;
        break;
    case ElectroBossTaskState::WeakEnd:
        enemy.activeAnimation = "scream";
        enemy.electroWeakReleased = false;
        break;
    case ElectroBossTaskState::RotateReady:
        enemy.activeAnimation = "attack_beam_ready";
        break;
    case ElectroBossTaskState::Rotate:
        enemy.activeAnimation = "attack_beam";
        enemy.animationLoops = true;
        activateElectroPosts(enemy);
        break;
    case ElectroBossTaskState::RotateEnd:
        enemy.activeAnimation = "attack_beam_to_idle";
        break;
    case ElectroBossTaskState::DashReady:
        enemy.activeAnimation = "attack_rush_ready";
        enemy.electroDashEffectThrown = false;
        enemy.electroDashHitPlayer = false;
        break;
    case ElectroBossTaskState::DashRush:
        enemy.activeAnimation = "attack_rush";
        enemy.animationLoops = true;
        break;
    case ElectroBossTaskState::DashLand:
        enemy.activeAnimation = "attack_rush_release";
        break;
    case ElectroBossTaskState::DashEnd:
        enemy.activeAnimation = "attack_rush_to_idle";
        enemy.position = enemy.electroHomePosition;
        setFacing(enemy, enemy.facing);
        break;
    case ElectroBossTaskState::None:
        enemy.behavior = EnemyBehaviorState::Idle;
        enemy.activeAnimation = "stand";
        enemy.animationLoops = true;
        break;
    }
    // Keep each native state clip local to that state. This assignment lives
    // after the animation selection so transitions from looping clips cannot
    // carry their accumulated timestamp into the next state's one-shot clip.
    enemy.animationTimeMilliseconds = 0;
}

void LevelEnemyRuntime::configureElectroDash(
    LevelEnemyState& enemy,
    const assets::Vector3& playerPosition) {
    // CBehaviorElectroDush::StateEnter(122) at 0x003b25a8 decrements the
    // authored count before selecting a target. Every non-final dash captures
    // the player's current position; the final one returns to ResetBehavior's
    // stored home position.
    enemy.electroDashStart = enemy.position;
    if (enemy.electroDashesRemaining > 0U) {
        --enemy.electroDashesRemaining;
    }
    enemy.electroDashTarget =
        enemy.electroDashesRemaining < 1U
            ? enemy.electroHomePosition
            : assets::Vector3{playerPosition.x, playerPosition.y,
                              enemy.position.z};
    const float deltaX = enemy.electroDashTarget.x - enemy.position.x;
    const float deltaY = enemy.electroDashTarget.y - enemy.position.y;
    const float distance = std::hypot(deltaX, deltaY);
    enemy.electroDashDirection =
        distance > std::numeric_limits<float>::epsilon()
            ? assets::Vector3{deltaX / distance, deltaY / distance, 0.0F}
            : enemy.facing;
    setFacing(enemy, enemy.electroDashDirection);
}

void LevelEnemyRuntime::advanceElectroSequence(
    LevelEnemyState& enemy,
    const assets::Vector3& playerPosition) {
    enemy.electroSequenceIndex = (enemy.electroSequenceIndex + 1U) % 6U;
    switch (enemy.electroSequenceIndex) {
    case 0:
        enemy.electroRangeAttacksRemaining = 3U + enemy.electroPhase;
        enemy.electroRangeWaitMilliseconds = 0;
        enterElectroTask(enemy, ElectroBossTaskState::RangeAttack);
        break;
    case 1:
    case 3:
    case 5:
        enterElectroTask(enemy, ElectroBossTaskState::WeakStart);
        break;
    case 2:
        enterElectroTask(enemy, ElectroBossTaskState::RotateReady);
        break;
    case 4:
        enemy.electroDashesRemaining = 4U + enemy.electroPhase;
        enterElectroTask(enemy, ElectroBossTaskState::DashReady);
        configureElectroDash(enemy, playerPosition);
        break;
    default: break;
    }
}

void LevelEnemyRuntime::launchElectroThunderclap(
    LevelEnemyState& enemy,
    const assets::Vector3& playerPosition) {
    if (enemy.asset == nullptr) {
        return;
    }
    // CLevel::GetThunderclapPool (0x003837f0) supplies a five-object manager,
    // while CBehaviorRangeAttack::ThrowMolotov's weapon-0x12 branch at
    // 0x003c051a launches exactly three. CSummonObjManage::Launch
    // (0x003670dc) calls srand48(0), whose first lrand48 angle is 334 degrees,
    // then advances by 360/3 degrees for each summon.
    const assets::Vector3 target{playerPosition.x, playerPosition.y,
                                 enemy.position.z};
    for (std::uint32_t index = 0; index < 3U; ++index) {
        const float angleRadians =
            (334.0F + 120.0F * static_cast<float>(index)) /
            kRadiansToDegrees;
        const assets::Vector3 position{
            target.x + std::cos(angleRadians) *
                           kElectroThunderclapRadiusCentimeters,
            target.y + std::sin(angleRadians) *
                           kElectroThunderclapRadiusCentimeters,
            target.z};
        const float directionX = target.x - position.x;
        const float directionY = target.y - position.y;
        const float distance = std::hypot(directionX, directionY);
        const assets::Vector3 velocity{
            directionX / distance *
                kElectroThunderclapSpeedCentimetersPerSecond,
            directionY / distance *
                kElectroThunderclapSpeedCentimetersPerSecond,
            0.0F};
        thunderclaps_.push_back(
            {enemy.asset->objectId, enemy.asset->roomId, target, position,
             velocity, kElectroThunderclapDamage, 0,
             EnemyThunderclapPhase::Converging, false, true});
        pendingProjectileEvents_.push_back(
            {EnemyProjectileEventKind::Spawned, enemy.asset->objectId,
             position, velocity, 0.0F});
    }
}

void LevelEnemyRuntime::updateThunderclaps(
    std::uint32_t elapsedMilliseconds,
    const assets::Vector3& playerPosition) noexcept {
    const float seconds = static_cast<float>(elapsedMilliseconds) / 1000.0F;
    for (EnemyThunderclapState& thunderclap : thunderclaps_) {
        if (!thunderclap.active) {
            continue;
        }
        thunderclap.phaseElapsedMilliseconds =
            static_cast<std::uint32_t>(std::min<std::uint64_t>(
                static_cast<std::uint64_t>(
                    thunderclap.phaseElapsedMilliseconds) +
                    elapsedMilliseconds,
                std::numeric_limits<std::uint32_t>::max()));
        const auto enterPhase = [&thunderclap](EnemyThunderclapPhase phase) {
            thunderclap.phase = phase;
            thunderclap.phaseElapsedMilliseconds = 0;
        };
        if (thunderclap.phase == EnemyThunderclapPhase::Converging) {
            const float deltaX =
                thunderclap.targetPosition.x - thunderclap.position.x;
            const float deltaY =
                thunderclap.targetPosition.y - thunderclap.position.y;
            const float distance = std::hypot(deltaX, deltaY);
            const float nextDistance = std::max(
                0.0F, distance -
                          kElectroThunderclapSpeedCentimetersPerSecond *
                              seconds);
            if (distance > std::numeric_limits<float>::epsilon()) {
                thunderclap.position.x = thunderclap.targetPosition.x -
                    deltaX / distance * nextDistance;
                thunderclap.position.y = thunderclap.targetPosition.y -
                    deltaY / distance * nextDistance;
            }
            if (nextDistance <
                    kElectroThunderclapImpactDistanceCentimeters ||
                thunderclap.phaseElapsedMilliseconds >= 3000U) {
                enterPhase(EnemyThunderclapPhase::Impact);
                if (level_ != nullptr &&
                    level_->effects().presets.find("rock_splash") != nullptr) {
                    pendingEffectCues_.push_back(
                        {thunderclap.sourceObjectId, thunderclap.roomId,
                         thunderclap.position, "rock_splash"});
                }
                pendingProjectileEvents_.push_back(
                    {EnemyProjectileEventKind::Grounded,
                     thunderclap.sourceObjectId, thunderclap.position, {},
                     0.0F});
            }
            continue;
        }
        if (thunderclap.phase == EnemyThunderclapPhase::Impact) {
            const float x = playerPosition.x - thunderclap.position.x;
            const float y = playerPosition.y - thunderclap.position.y;
            // CSummonObject::CheckCollisionByRadius (0x00364cb4) derives a
            // player-box-dependent radius and adds 90 cm. The portable player
            // capsule contributes its 50 cm radius to the same 185 cm bound.
            constexpr float impactRadius =
                90.0F + kPlayerCollisionRadiusCentimeters;
            if (!thunderclap.hitPlayer &&
                x * x + y * y < impactRadius * impactRadius) {
                thunderclap.hitPlayer = true;
                pendingPlayerHits_.push_back(
                    {thunderclap.sourceObjectId, 22,
                     kElectroThunderclapDamage});
                pendingProjectileEvents_.push_back(
                    {EnemyProjectileEventKind::PlayerContact,
                     thunderclap.sourceObjectId, thunderclap.position, {},
                     0.0F});
            }
            if (thunderclap.phaseElapsedMilliseconds >=
                kElectroThunderclapImpactMilliseconds) {
                enterPhase(EnemyThunderclapPhase::Release);
            }
            continue;
        }
        if (thunderclap.phase == EnemyThunderclapPhase::Release) {
            if (thunderclap.phaseElapsedMilliseconds >=
                kElectroThunderclapReleaseMilliseconds) {
                enterPhase(EnemyThunderclapPhase::Fading);
            }
            continue;
        }
        if (thunderclap.phase == EnemyThunderclapPhase::Fading &&
            thunderclap.phaseElapsedMilliseconds >=
                kElectroThunderclapFadeMilliseconds) {
            thunderclap.phase = EnemyThunderclapPhase::Ready;
            thunderclap.active = false;
            pendingProjectileEvents_.push_back(
                {EnemyProjectileEventKind::Returned,
                 thunderclap.sourceObjectId, thunderclap.position, {},
                 0.0F});
        }
    }
}

void LevelEnemyRuntime::removeElectroPosts(
    std::int32_t sourceObjectId) noexcept {
    std::erase_if(electricPosts_,
                  [sourceObjectId](const EnemyElectricPostState& post) {
                      return post.sourceObjectId == sourceObjectId;
                  });
}

void LevelEnemyRuntime::spawnElectroBurst(
    const LevelEnemyState& enemy, const assets::Vector3& position,
    float scale) {
    if (enemy.asset == nullptr) {
        return;
    }
    electroBursts_.push_back({enemy.asset->objectId, enemy.asset->roomId,
                              position, scale, 0, true});
}

void LevelEnemyRuntime::updateElectroBursts(
    std::uint32_t elapsedMilliseconds) noexcept {
    // electro_wave_billboard's shipped `wave` clip is the longer member of
    // the native pair at exactly 1000 ms; EffectManager releases the animated
    // effect after its non-looping clip completes.
    constexpr std::uint32_t lifetimeMilliseconds = 1000;
    for (EnemyElectroBurstState& burst : electroBursts_) {
        if (!burst.active) {
            continue;
        }
        burst.elapsedMilliseconds =
            static_cast<std::uint32_t>(std::min<std::uint64_t>(
                static_cast<std::uint64_t>(burst.elapsedMilliseconds) +
                    elapsedMilliseconds,
                std::numeric_limits<std::uint32_t>::max()));
        if (burst.elapsedMilliseconds >= lifetimeMilliseconds) {
            burst.active = false;
        }
    }
    std::erase_if(electroBursts_,
                  [](const EnemyElectroBurstState& burst) {
                      return !burst.active;
                  });
}

void LevelEnemyRuntime::updateLandingAnimatedEffects(
    std::uint32_t elapsedMilliseconds) noexcept {
    for (EnemyLandingAnimatedEffectState& effect :
         landingAnimatedEffects_) {
        if (!effect.active) {
            continue;
        }
        effect.elapsedMilliseconds =
            static_cast<std::uint32_t>(std::min<std::uint64_t>(
                static_cast<std::uint64_t>(effect.elapsedMilliseconds) +
                    elapsedMilliseconds,
                std::numeric_limits<std::uint32_t>::max()));
        if (effect.elapsedMilliseconds >= effect.lifetimeMilliseconds) {
            effect.active = false;
        }
    }
    std::erase_if(
        landingAnimatedEffects_,
        [](const EnemyLandingAnimatedEffectState& effect) {
            return !effect.active;
        });
}

void LevelEnemyRuntime::activateElectroPosts(LevelEnemyState& enemy) {
    if (enemy.asset == nullptr) {
        return;
    }
    removeElectroPosts(enemy.asset->objectId);
    electricPosts_.reserve(electricPosts_.size() + 3U);

    // CBehaviorRotate::ResetElectricPost (0x003c2848) reads the boss
    // quaternion, positions every post at height * 0.4, then multiplies the
    // quaternion by a +2.094395-radian Z rotation for the next post.
    constexpr float separationRadians = 2.094395F;
    const assets::Vector3 center{
        enemy.position.x, enemy.position.y,
        enemy.position.z + enemy.collisionHeight * 0.4F};
    for (std::uint32_t index = 0; index < 3U; ++index) {
        const float angle = separationRadians * static_cast<float>(index);
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        const assets::Vector3 direction{
            enemy.facing.x * cosine - enemy.facing.y * sine,
            enemy.facing.x * sine + enemy.facing.y * cosine, 0.0F};
        electricPosts_.push_back(
            {enemy.asset->objectId, enemy.asset->roomId, center, direction,
             kElectroPostDamage, 0, false, true});
    }
}

void LevelEnemyRuntime::updateElectroPosts(
    LevelEnemyState& enemy, std::uint32_t elapsedMilliseconds,
    const assets::Vector3& playerPosition) noexcept {
    if (enemy.asset == nullptr) {
        return;
    }
    const assets::Vector3 center{
        enemy.position.x, enemy.position.y,
        enemy.position.z + enemy.collisionHeight * 0.4F};
    constexpr float separationRadians = 2.094395F;
    std::uint32_t index = 0;
    for (EnemyElectricPostState& post : electricPosts_) {
        if (!post.active || post.sourceObjectId != enemy.asset->objectId) {
            continue;
        }
        const float angle = separationRadians * static_cast<float>(index++);
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        post.position = center;
        post.facing = {
            enemy.facing.x * cosine - enemy.facing.y * sine,
            enemy.facing.x * sine + enemy.facing.y * cosine, 0.0F};
        post.animationTimeMilliseconds =
            static_cast<std::uint32_t>(std::min<std::uint64_t>(
                static_cast<std::uint64_t>(post.animationTimeMilliseconds) +
                    elapsedMilliseconds,
                std::numeric_limits<std::uint32_t>::max()));

        // CElectricPost::Update (0x00355558) builds the animated post OBB and
        // tests it against Unit. The shipped dummy_start1/dummy_end1 nodes
        // define the 1429.7869 cm local -Y axis; this capsule/segment test is
        // the portable equivalent for the player's 50 x 185 cm bounds.
        const float playerCenterZ =
            playerPosition.z + kPlayerCollisionHeightCentimeters * 0.5F;
        if (post.hitPlayer ||
            std::abs(playerCenterZ - center.z) >
                kPlayerCollisionHeightCentimeters * 0.5F +
                    kElectroPostHalfWidthCentimeters) {
            continue;
        }
        const float deltaX = playerPosition.x - center.x;
        const float deltaY = playerPosition.y - center.y;
        const float projected = std::clamp(
            deltaX * post.facing.x + deltaY * post.facing.y, 0.0F,
            kElectroPostLengthCentimeters);
        const float closestX = center.x + post.facing.x * projected;
        const float closestY = center.y + post.facing.y * projected;
        const float distanceX = playerPosition.x - closestX;
        const float distanceY = playerPosition.y - closestY;
        constexpr float contactRadius =
            kPlayerCollisionRadiusCentimeters +
            kElectroPostHalfWidthCentimeters;
        if (distanceX * distanceX + distanceY * distanceY <=
            contactRadius * contactRadius) {
            post.hitPlayer = true;
            pendingPlayerHits_.push_back(
                {post.sourceObjectId, 25, post.damage});
            pendingProjectileEvents_.push_back(
                {EnemyProjectileEventKind::PlayerContact,
                 post.sourceObjectId, closestX == center.x &&
                             closestY == center.y
                         ? center
                         : assets::Vector3{closestX, closestY, center.z},
                 {}, 0.0F});
            if (level_ != nullptr &&
                level_->effects().presets.find("electro_splash") != nullptr) {
                pendingEffectCues_.push_back(
                    {post.sourceObjectId, post.roomId,
                     {closestX, closestY, center.z}, "electro_splash"});
            }
        }
    }
}

void LevelEnemyRuntime::updateElectroBoss(
    LevelEnemyState& enemy, std::uint32_t elapsedMilliseconds,
    const assets::Vector3& playerPosition,
    const LevelCollision* collision) {
    if (level_ == nullptr || enemy.asset == nullptr ||
        enemy.asset->archetypeIndex >= level_->enemyArchetypes().size()) {
        return;
    }
    enemy.electroTaskElapsedMilliseconds =
        static_cast<std::uint32_t>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(enemy.electroTaskElapsedMilliseconds) +
                elapsedMilliseconds,
            std::numeric_limits<std::uint32_t>::max()));
    const EnemyArchetypeAsset& archetype =
        level_->enemyArchetypes()[enemy.asset->archetypeIndex];
    const auto animationFinished = [&enemy, &archetype]() {
        const assets::ColladaAnimationClip* clip =
            archetype.animationBank.findClip(enemy.activeAnimation);
        return clip == nullptr ||
               enemy.animationTimeMilliseconds >=
                   clip->durationMilliseconds();
    };
    const auto activeThunderclap = [this, &enemy]() {
        return std::any_of(
            thunderclaps_.begin(), thunderclaps_.end(),
            [&enemy](const EnemyThunderclapState& thunderclap) {
                return thunderclap.active && enemy.asset != nullptr &&
                       thunderclap.sourceObjectId == enemy.asset->objectId;
            });
    };
    const float toPlayerX = playerPosition.x - enemy.position.x;
    const float toPlayerY = playerPosition.y - enemy.position.y;
    const float playerDistance = std::hypot(toPlayerX, toPlayerY);
    const assets::Vector3 playerDirection =
        playerDistance > std::numeric_limits<float>::epsilon()
            ? assets::Vector3{toPlayerX / playerDistance,
                              toPlayerY / playerDistance, 0.0F}
            : enemy.facing;

    if (enemy.electroTask == ElectroBossTaskState::None) {
        enemy.electroSequenceIndex = 0;
        enemy.electroRangeAttacksRemaining = 3U + enemy.electroPhase;
        enemy.electroRangeWaitMilliseconds = 0;
        enterElectroTask(enemy, ElectroBossTaskState::RangeAttack);
        return;
    }
    if (enemy.electroTask == ElectroBossTaskState::RangeAttack) {
        setFacing(enemy, playerDirection);
        if (enemy.electroRangeWaitMilliseconds > 0U) {
            if (elapsedMilliseconds < enemy.electroRangeWaitMilliseconds) {
                enemy.electroRangeWaitMilliseconds -= elapsedMilliseconds;
                return;
            }
            enemy.electroRangeWaitMilliseconds = 0;
            enemy.activeAnimation = "attack_fall";
            enemy.animationTimeMilliseconds = 0;
            enemy.animationLoops = false;
            enemy.electroRangeReleased = false;
            return;
        }
        const assets::ColladaAnimationClip* clip =
            archetype.animationBank.findClip("attack_fall");
        const std::uint32_t releaseTime =
            clip == nullptr ? 1U : clip->durationMilliseconds() / 2U;
        if (!enemy.electroRangeReleased &&
            enemy.animationTimeMilliseconds >= releaseTime) {
            enemy.electroRangeReleased = true;
            launchElectroThunderclap(enemy, playerPosition);
        }
        if (!animationFinished() || activeThunderclap()) {
            return;
        }
        if (enemy.electroRangeAttacksRemaining > 0U) {
            --enemy.electroRangeAttacksRemaining;
        }
        if (enemy.electroRangeAttacksRemaining == 0U) {
            advanceElectroSequence(enemy, playerPosition);
        } else {
            enemy.electroRangeWaitMilliseconds =
                kElectroRangeWaitMilliseconds;
            enemy.activeAnimation = "stand";
            enemy.animationTimeMilliseconds = 0;
            enemy.animationLoops = true;
        }
        return;
    }
    if (enemy.electroTask == ElectroBossTaskState::WeakStart) {
        setFacing(enemy, playerDirection);
        if (animationFinished()) {
            enterElectroTask(enemy, ElectroBossTaskState::Weak);
        }
        return;
    }
    if (enemy.electroTask == ElectroBossTaskState::Weak) {
        setFacing(enemy, playerDirection);
        if (enemy.electroTaskElapsedMilliseconds >=
            kElectroWeakMilliseconds) {
            enterElectroTask(enemy, ElectroBossTaskState::WeakEnd);
        }
        return;
    }
    if (enemy.electroTask == ElectroBossTaskState::WeakEnd) {
        setFacing(enemy, playerDirection);
        const assets::ColladaAnimationClip* clip =
            archetype.animationBank.findClip("scream");
        const std::uint32_t releaseTime =
            clip == nullptr
                ? 1U
                : clip->durationMilliseconds() * 30U / 100U;
        if (!enemy.electroWeakReleased &&
            enemy.animationTimeMilliseconds >= releaseTime) {
            // Type-7 special-action record 82 sends message 0x65 at 30% of
            // `scream`; CBehaviorWeak::onMessage (0x003cb34c) throws the
            // configured effects from the boss's top and arms the 400 cm hit.
            enemy.electroWeakReleased = true;
            const assets::Vector3 origin{
                enemy.position.x, enemy.position.y,
                enemy.position.z + enemy.collisionHeight};
            // CBoss::ResetBehavior (0x0032afdc) configures Weak with
            // electro_wave/electro_wave_billboard at scale 3.0, and
            // CBehaviorWeak::ThrowEffect (0x003cb1bc) launches both at the
            // boss-top position received from message 0x65.
            spawnElectroBurst(enemy, origin, 3.0F);
            if (level_->effects().presets.find(
                    "effect_lighting_splash") != nullptr) {
                pendingEffectCues_.push_back(
                    {enemy.asset->objectId, enemy.asset->roomId, origin,
                     "effect_lighting_splash"});
            }
            const float x = playerPosition.x - origin.x;
            const float y = playerPosition.y - origin.y;
            const float z = playerPosition.z - origin.z;
            if (x * x + y * y + z * z <
                kElectroWeakRadiusCentimeters *
                    kElectroWeakRadiusCentimeters) {
                pendingPlayerHits_.push_back(
                    {enemy.asset->objectId, 23, kElectroWeakDamage});
            }
        }
        if (animationFinished()) {
            advanceElectroSequence(enemy, playerPosition);
        }
        return;
    }
    if (enemy.electroTask == ElectroBossTaskState::RotateReady) {
        setFacing(enemy, playerDirection);
        if (enemy.electroTaskElapsedMilliseconds >=
            kElectroRotateReadyMilliseconds) {
            enterElectroTask(enemy, ElectroBossTaskState::Rotate);
        }
        return;
    }
    if (enemy.electroTask == ElectroBossTaskState::Rotate) {
        constexpr std::array<float, 3> phaseDegreesPerSecond{
            54.0F, 90.0F, 108.0F};
        const float radians =
            phaseDegreesPerSecond[std::min<std::size_t>(enemy.electroPhase, 2)] /
            kRadiansToDegrees *
            (static_cast<float>(elapsedMilliseconds) / 1000.0F);
        const float cosine = std::cos(radians);
        const float sine = std::sin(radians);
        setFacing(enemy,
                  {enemy.facing.x * cosine - enemy.facing.y * sine,
                   enemy.facing.x * sine + enemy.facing.y * cosine, 0.0F});
        updateElectroPosts(enemy, elapsedMilliseconds, playerPosition);
        if (enemy.electroTaskElapsedMilliseconds >=
            kElectroRotateMilliseconds) {
            enterElectroTask(enemy, ElectroBossTaskState::RotateEnd);
        }
        return;
    }
    if (enemy.electroTask == ElectroBossTaskState::RotateEnd) {
        if (animationFinished()) {
            advanceElectroSequence(enemy, playerPosition);
        }
        return;
    }
    if (enemy.electroTask == ElectroBossTaskState::DashReady) {
        if (animationFinished()) {
            enterElectroTask(enemy, ElectroBossTaskState::DashRush);
        }
        return;
    }
    if (enemy.electroTask == ElectroBossTaskState::DashRush) {
        const float seconds =
            static_cast<float>(elapsedMilliseconds) / 1000.0F;
        const assets::Vector3 previous = enemy.position;
        assets::Vector3 desired = enemy.position;
        desired.x += enemy.electroDashDirection.x *
                     kElectroDashSpeedCentimetersPerSecond * seconds;
        desired.y += enemy.electroDashDirection.y *
                     kElectroDashSpeedCentimetersPerSecond * seconds;
        assets::Vector3 resolved = desired;
        if (collision != nullptr) {
            (void)collision->resolveGroundMotion(previous, desired, resolved);
        }
        const bool staticContact =
            std::hypot(resolved.x - desired.x, resolved.y - desired.y) > 0.5F;
        enemy.position = resolved;
        setFacing(enemy, enemy.electroDashDirection);

        const float targetX =
            enemy.electroDashTarget.x - enemy.electroDashStart.x;
        const float targetY =
            enemy.electroDashTarget.y - enemy.electroDashStart.y;
        const float targetDistanceSquared =
            targetX * targetX + targetY * targetY;
        const float travelledX = enemy.position.x - enemy.electroDashStart.x;
        const float travelledY = enemy.position.y - enemy.electroDashStart.y;
        const float travelledDistanceSquared =
            travelledX * travelledX + travelledY * travelledY;

        if (!enemy.electroDashEffectThrown) {
            const float projection = std::max(
                0.0F,
                (playerPosition.x - enemy.electroDashStart.x) *
                        enemy.electroDashDirection.x +
                    (playerPosition.y - enemy.electroDashStart.y) *
                        enemy.electroDashDirection.y);
            const float travelled =
                travelledX * enemy.electroDashDirection.x +
                travelledY * enemy.electroDashDirection.y;
            if (travelled >= projection) {
                enemy.electroDashEffectThrown = true;
                const assets::Vector3 effectPosition{
                    enemy.electroDashStart.x +
                        enemy.electroDashDirection.x * projection,
                    enemy.electroDashStart.y +
                        enemy.electroDashDirection.y * projection,
                    enemy.position.z};
                // CBehaviorElectroDush::ThrowEffectRoundPlayer
                // (0x003b2b74) launches the same two packaged wave meshes at
                // scale 1.0 when the rush crosses the closest player point.
                spawnElectroBurst(enemy, effectPosition, 1.0F);
                if (level_->effects().presets.find(
                        "effect_lighting_splash") != nullptr) {
                    pendingEffectCues_.push_back(
                        {enemy.asset->objectId, enemy.asset->roomId,
                         effectPosition, "effect_lighting_splash"});
                }
                const float hitX = playerPosition.x - enemy.position.x;
                const float hitY = playerPosition.y - enemy.position.y;
                if (!enemy.electroDashHitPlayer &&
                    hitX * hitX + hitY * hitY <
                        kElectroDashPlayerHitRadiusCentimeters *
                            kElectroDashPlayerHitRadiusCentimeters) {
                    enemy.electroDashHitPlayer = true;
                    pendingPlayerHits_.push_back(
                        {enemy.asset->objectId, 24, kElectroDashDamage});
                }
            }
        }
        if (staticContact ||
            travelledDistanceSquared >= targetDistanceSquared) {
            enterElectroTask(enemy, ElectroBossTaskState::DashLand);
        }
        return;
    }
    if (enemy.electroTask == ElectroBossTaskState::DashLand) {
        if (!animationFinished()) {
            return;
        }
        if (enemy.electroDashesRemaining > 0U) {
            enterElectroTask(enemy, ElectroBossTaskState::DashReady);
            configureElectroDash(enemy, playerPosition);
        } else {
            enterElectroTask(enemy, ElectroBossTaskState::DashEnd);
        }
        return;
    }
    if (enemy.electroTask == ElectroBossTaskState::DashEnd &&
        animationFinished()) {
        advanceElectroSequence(enemy, playerPosition);
    }
}

bool LevelEnemyRuntime::alignRhinoThrowCatch(
    LevelEnemyState& enemy, const assets::Vector3& playerPosition) {
    if (level_ == nullptr || enemy.asset == nullptr ||
        enemy.asset->archetypeIndex >= level_->enemyArchetypes().size()) {
        return false;
    }
    const EnemyArchetypeAsset& archetype =
        level_->enemyArchetypes()[enemy.asset->archetypeIndex];
    const assets::ColladaAnimationClip* clip =
        archetype.animationBank.findClip(rhinoQuickTimeAction_.npcAnimation());
    if (clip == nullptr) {
        return false;
    }
    std::array<float, 16> handModelTransform{};
    if (!assets::evaluateColladaSceneNodeTransform(
            archetype.mesh, archetype.animationBank, clip->startMilliseconds,
            "R_Hand_Dummy", handModelTransform)) {
        return false;
    }
    const assets::Vector3 handWorld = transformPoint(
        enemy.worldTransform, transformPoint(handModelTransform));
    const assets::Vector3 handOffset{handWorld.x - enemy.position.x,
                                     handWorld.y - enemy.position.y, 0.0F};

    // CBehaviorThrow::CatchPlayer (0x003c6164) advances Rhino's scene node to
    // the active frame, measures R_Hand_Dummy minus the boss position, and
    // moves Rhino so that offset lands on the target's current X/Y. Native
    // Unit Z is center-based; this reconstruction stores capsule-base Z, so
    // retain the already-grounded Z while preserving the exact horizontal
    // alignment and authored hand hierarchy.
    enemy.position.x = playerPosition.x - handOffset.x;
    enemy.position.y = playerPosition.y - handOffset.y;
    setFacing(enemy, enemy.facing);
    return true;
}

void LevelEnemyRuntime::cancelRhinoQuickTimeIfOwned(
    const LevelEnemyState& enemy) noexcept {
    if (enemy.asset != nullptr &&
        enemy.asset->objectId == rhinoQuickTimeEnemyId_) {
        rhinoQuickTimeAction_.cancel();
        rhinoQuickTimeEnemyId_ = -1;
    }
}

bool LevelEnemyRuntime::isGunLineEnemy(
    const LevelEnemyState& enemy) const noexcept {
    if (level_ == nullptr || enemy.asset == nullptr) {
        return false;
    }
    const EnemyAttributeDefinition* attributes =
        level_->enemyAttributeConfigs().find(enemy.asset->enemyTypeId);
    if (attributes == nullptr ||
        attributes->rangedAttackTypeMapIndices.empty()) {
        return false;
    }
    const auto weaponType = resolveEnemyRangeWeaponType(
        attributes->rangedAttackTypeMapIndices.front());
    return weaponType == 13;
}

bool LevelEnemyRuntime::isMolotovEnemy(
    const LevelEnemyState& enemy) const noexcept {
    if (level_ == nullptr || enemy.asset == nullptr) {
        return false;
    }
    const EnemyAttributeDefinition* attributes =
        level_->enemyAttributeConfigs().find(enemy.asset->enemyTypeId);
    if (attributes == nullptr ||
        attributes->rangedAttackTypeMapIndices.empty()) {
        return false;
    }
    return resolveEnemyRangeWeaponType(
               attributes->rangedAttackTypeMapIndices.front()) == 5;
}

void LevelEnemyRuntime::queueStateSound(
    LevelEnemyState& enemy, std::string_view behaviorStateName) {
    if (level_ == nullptr || enemy.asset == nullptr) {
        return;
    }
    const auto soundIds = level_->enemyBehaviorConfigs().resolveStateSoundIds(
        behaviorStateName, enemy.asset->enemyTypeId);
    if (soundIds.empty()) {
        return;
    }
    const std::size_t selected =
        enemy.soundVariantCursor % soundIds.size();
    enemy.soundVariantCursor = static_cast<std::uint32_t>(
        (selected + 1) % soundIds.size());
    pendingSoundCues_.push_back(
        {enemy.asset->objectId, soundIds[selected]});
}

bool LevelEnemyRuntime::hasHurtStateAnimation(
    const LevelEnemyState& enemy, std::int16_t stateId) const noexcept {
    const std::string_view stateName = hurtStateName(stateId);
    if (level_ == nullptr || enemy.asset == nullptr || stateName.empty()) {
        return false;
    }
    return !level_->enemyBehaviorConfigs().resolveStateAnimationNames(
                stateName, enemy.asset->enemyTypeId).empty();
}

void LevelEnemyRuntime::enterHurtState(LevelEnemyState& enemy,
                                       std::int16_t stateId) noexcept {
    if (!hasHurtStateAnimation(enemy, stateId)) {
        stateId = 49;
    }
    const std::string_view stateName = hurtStateName(stateId);
    if (stateName.empty()) {
        return;
    }
    enemy.behavior = EnemyBehaviorState::Hurt;
    enemy.hurtStateId = stateId;
    selectStateAnimation(enemy, stateName, hurtStateLoops(stateId));
    queueStateSound(enemy, stateName);
}

void LevelEnemyRuntime::selectStateAnimation(
    LevelEnemyState& enemy, std::string_view behaviorStateName, bool loop) {
    if (level_ == nullptr || enemy.asset == nullptr) {
        return;
    }
    const auto animationNames =
        level_->enemyBehaviorConfigs().resolveStateAnimationNames(
            behaviorStateName, enemy.asset->enemyTypeId);
    if (animationNames.empty()) {
        return;
    }
    const std::size_t selected =
        enemy.hurtVariantCursor % animationNames.size();
    enemy.hurtVariantCursor = static_cast<std::uint32_t>(
        (selected + 1) % animationNames.size());
    enemy.activeAnimation = animationNames[selected];
    enemy.animationTimeMilliseconds = 0;
    enemy.animationSpeed = 1.0F;
    enemy.animationLoops = loop;
    enemy.animationReversed = false;
}

void LevelEnemyRuntime::enterDeadState(LevelEnemyState& enemy) {
    enemy.wallWebCaptured = false;
    cancelRhinoQuickTimeIfOwned(enemy);
    if (enemy.asset != nullptr) {
        unregisterMeleeEngager(enemy.asset->objectId);
    }
    enemy.aiEnabled = false;
    enemy.behavior = EnemyBehaviorState::Dead;
    enemy.meleeAttackActive = false;
    enemy.meleeAttackCooldownMilliseconds = 0;
    enemy.hurtStateId = -1;
    enemy.hurtVelocity = {};
    enemy.hurtStartedGrounded = false;
    enemy.tiedUpRemainingMilliseconds = 0;
    enemy.sandmanTask = SandmanBossTaskState::None;
    enemy.sandmanJumpElapsedMilliseconds = 0;
    enemy.sandmanJumpDurationMilliseconds = 0;
    enemy.rhinoTask = RhinoBossTaskState::None;
    enemy.rhinoTaskElapsedMilliseconds = 0;
    enemy.rhinoMeleeAttacksRemaining = 0;
    enemy.robotPhantomTask = RobotPhantomTaskState::None;
    enemy.robotPhantomTaskElapsedMilliseconds = 0;
    enemy.robotPhantomSequenceIndex = 0;
    enemy.electroTask = ElectroBossTaskState::None;
    enemy.electroTaskElapsedMilliseconds = 0;
    enemy.electroSequenceIndex = 0;
    enemy.electroRangeAttacksRemaining = 0;
    enemy.electroRangeWaitMilliseconds = 0;
    enemy.electroDashesRemaining = 0;
    if (enemy.asset != nullptr) {
        std::erase_if(
            boomerangs_, [&enemy](const EnemyBoomerangState& boomerang) {
                return boomerang.sourceObjectId == enemy.asset->objectId;
            });
        std::erase_if(
            thunderclaps_, [&enemy](const EnemyThunderclapState& thunderclap) {
                return thunderclap.sourceObjectId == enemy.asset->objectId;
            });
        removeElectroPosts(enemy.asset->objectId);
    }
    // CBehaviorDead::BehaviorStart (0x003ab578) restores world gravity
    // and selects state 72 for wall enemies, not ordinary state 71.
    const std::string_view deadState = enemy.onWall
        ? "ENEMY_BEHAVIOR_DEAD_STATE_ON_WALL"
        : "ENEMY_BEHAVIOR_DEAD_STATE";
    if (enemy.onWall) {
        enemy.physicsActive = true;
        enemy.verticalVelocity = 0.0F;
        enemy.wallBehaviorState = 0;
    }
    selectStateAnimation(enemy, deadState, false);
    queueStateSound(enemy, deadState);
}

void LevelEnemyRuntime::endColladaAnimation(std::int32_t objectId) noexcept {
    if (LevelEnemyState* enemy = findMutable(objectId)) {
        enemy->visible = true;
        enemy->physicsActive = true;
        if (enemy->health > 0.0F) {
            enemy->behavior = enemy->aiEnabled ? EnemyBehaviorState::Idle
                                              : EnemyBehaviorState::Disabled;
        }
    }
}

Result LevelEnemyRuntime::applyCinematicCommand(
    const LevelOneBootstrap& level, const CinematicThread& thread,
    const CinematicCommand& command) {
    if (command.name == "Throwing" || command.name == "StopAction") {
        const CinematicAttribute* enemyAttribute =
            command.findAttribute("EnmeyID");
        std::int32_t enemyId = -1;
        if (enemyAttribute == nullptr ||
            !parseInteger(enemyAttribute->value, enemyId)) {
            return Result::failure(command.name +
                                   " has an invalid EnmeyID");
        }
        LevelEnemyState* commandEnemy = findMutable(enemyId);
        if (commandEnemy == nullptr) {
            // Both native handlers return false when FindObjectInRooms fails.
            return Result::failure(command.name +
                                   " references a missing enemy");
        }
        if (command.name == "Throwing") {
            std::int32_t relatedObjectId = -1;
            if (const CinematicAttribute* objectAttribute =
                    command.findAttribute("ObjectID")) {
                (void)parseInteger(objectAttribute->value, relatedObjectId);
            }
            // CCinematicThread::ThrowingSomething (0x00371364) sends local
            // CBoss message {type=0x0c, code=0x59, ObjectID}. CBoss::
            // ParseLocalAiMessage (0x0032dea8) enters state 6 and activates
            // CBehaviorPickUp for the related object.
            commandEnemy->cinematicActionActive = true;
            commandEnemy->cinematicActionObjectId = relatedObjectId;
        } else {
            // StopAction (0x003713f4) sends code 0x5e; the same native boss
            // handler clears its external-action byte and enters state zero.
            commandEnemy->cinematicActionActive = false;
            commandEnemy->cinematicActionObjectId = -1;
        }
        return Result::success();
    }
    // CCinematicThread::ShowHealth (0x00370fb8) resolves the optional
    // ObjectID and registers that enemy with CLevel::RegisterHealthBar
    // (0x0037dfec). The original renderer then queries the registered enemy
    // every frame through CAIEntityManager::GetShowHealthBoss (0x003760e0).
    if (command.name == "ShowHealth") {
        std::int32_t objectId = thread.objectId;
        if (thread.type == 1) {
            const CinematicAttribute* attribute =
                command.findAttribute("ObjectID");
            if (attribute == nullptr ||
                !parseInteger(attribute->value, objectId) || objectId == -1) {
                // CCinematicThread::ShowHealth (0x00370fb8) reads ObjectID
                // only for a basic thread and returns false for its -1
                // sentinel. Object threads ignore the serialized attribute
                // and use their bound object pointer; Level 6 cinematic 24
                // deliberately combines object 41460 with ObjectID=-1.
                return Result::failure(
                    "Enemy ShowHealth has an invalid ObjectID");
            }
        }
        if (find(objectId) == nullptr) {
            return Result::failure("Enemy ShowHealth references a missing enemy");
        }
        shownHealthBarObjectId_ = objectId;
        return Result::success();
    }
    // CCinematicThread::Init (0x00371de0) binds type 3 to the active player
    // without resolving its serialized object ID.  A stale ID that happens
    // to name an enemy (Level 2 cinematic 467 uses 1141) must therefore not
    // dispatch the same command to that enemy in parallel.
    if (thread.type == 2 || thread.type == 3) {
        return Result::success();
    }
    std::int32_t objectId = thread.objectId;
    if (command.name == "SetAnim" && thread.type == 1) {
        // CCinematicThread::SetAnim (0x00372468) looks up ObjectID only on a
        // basic thread. An object thread always uses its bound object even if
        // its serialized command carries a different ObjectID.
        if (const CinematicAttribute* explicitObject =
                command.findAttribute("ObjectID")) {
            std::int32_t parsed = -1;
            if (parseInteger(explicitObject->value, parsed) && parsed >= 0) {
                objectId = parsed;
            }
        }
    }
    if (command.name == "SetVisible") {
        // CCinematicThread::SetVisible (0x00371e9c) resolves an explicit
        // ObjectID before falling back to the owning object thread. Level 3
        // presentation 30408 exposes its three opening enemies from the
        // basic thread, whose object ID is -1.
        if (const CinematicAttribute* explicitObject =
                command.findAttribute("ObjectID")) {
            std::int32_t parsed = -1;
            if (parseInteger(explicitObject->value, parsed) && parsed >= 0) {
                objectId = parsed;
            }
        }
    }
    LevelEnemyState* enemy = findMutable(objectId);
    if (enemy == nullptr) {
        return Result::success();
    }
    if (command.name == "PlayDAEAnim") {
        // CCinematicThread::PlayDAEAnim (0x003709ec) makes its bound scene
        // object visible after pushing the Collada animator and explicitly
        // deactivates the Unit's PhysicsEntity for the authored motion. A
        // following EnableAI command restores physics when gameplay resumes.
        enemy->visible = true;
        enemy->physicsActive = false;
        enemy->behavior = EnemyBehaviorState::Disabled;
        enemy->meleeAttackActive = false;
        unregisterMeleeEngager(objectId);
        return Result::success();
    }
    if (command.name == "KillObject") {
        (void)destroy(thread.objectId);
        return Result::success();
    }
    if (command.name == "DisableAI") {
        cancelRhinoQuickTimeIfOwned(*enemy);
        enemy->aiEnabled = false;
        enemy->physicsActive = false;
        enemy->behavior = EnemyBehaviorState::Disabled;
        enemy->meleeAttackActive = false;
        unregisterMeleeEngager(objectId);
        enemy->meleeAttackCooldownMilliseconds = 0;
        enemy->sandmanTask = SandmanBossTaskState::None;
        enemy->sandmanJumpElapsedMilliseconds = 0;
        enemy->sandmanJumpDurationMilliseconds = 0;
        enemy->rhinoTask = RhinoBossTaskState::None;
        enemy->rhinoTaskElapsedMilliseconds = 0;
        enemy->rhinoMeleeAttacksRemaining = 0;
        enemy->robotPhantomTask = RobotPhantomTaskState::None;
        enemy->robotPhantomTaskElapsedMilliseconds = 0;
        enemy->robotPhantomSequenceIndex = 0;
        enemy->electroTask = ElectroBossTaskState::None;
        enemy->electroTaskElapsedMilliseconds = 0;
        enemy->electroSequenceIndex = 0;
        enemy->electroRangeAttacksRemaining = 0;
        enemy->electroRangeWaitMilliseconds = 0;
        enemy->electroDashesRemaining = 0;
        std::erase_if(
            boomerangs_, [objectId](const EnemyBoomerangState& boomerang) {
                return boomerang.sourceObjectId == objectId;
            });
        std::erase_if(
            thunderclaps_, [objectId](const EnemyThunderclapState& thunderclap) {
                return thunderclap.sourceObjectId == objectId;
            });
        removeElectroPosts(objectId);
        return Result::success();
    }
    if (command.name == "EnableAI") {
        cancelRhinoQuickTimeIfOwned(*enemy);
        enemy->aiEnabled = true;
        enemy->physicsActive = true;
        enemy->behavior = EnemyBehaviorState::Idle;
        enemy->rhinoTask = RhinoBossTaskState::None;
        enemy->rhinoTaskElapsedMilliseconds = 0;
        enemy->rhinoMeleeAttacksRemaining = 0;
        enemy->robotPhantomTask = RobotPhantomTaskState::None;
        enemy->robotPhantomTaskElapsedMilliseconds = 0;
        enemy->robotPhantomSequenceIndex = 0;
        enemy->electroTask = ElectroBossTaskState::None;
        enemy->electroTaskElapsedMilliseconds = 0;
        enemy->electroSequenceIndex = 0;
        enemy->electroRangeAttacksRemaining = 0;
        enemy->electroRangeWaitMilliseconds = 0;
        enemy->electroDashesRemaining = 0;
        if (isElectroBoss(*enemy)) {
            enemy->electroHomePosition = enemy->position;
        }
        std::erase_if(
            boomerangs_, [objectId](const EnemyBoomerangState& boomerang) {
                return boomerang.sourceObjectId == objectId;
            });
        std::erase_if(
            thunderclaps_, [objectId](const EnemyThunderclapState& thunderclap) {
                return thunderclap.sourceObjectId == objectId;
            });
        removeElectroPosts(objectId);
        return Result::success();
    }
    if (command.name == "SetVisible") {
        const CinematicAttribute* visible = command.findAttribute("Visible");
        enemy->visible =
            visible == nullptr ? true : parseBool(visible->value, true);
        if (!enemy->visible) {
            cancelRhinoQuickTimeIfOwned(*enemy);
        }
        return Result::success();
    }
    if (command.name == "SetAnim") {
        const CinematicAttribute* animation = command.findAttribute("$Anim");
        if (animation == nullptr || enemy->asset == nullptr ||
            enemy->asset->archetypeIndex >= level.enemyArchetypes().size() ||
            level.enemyArchetypes()[enemy->asset->archetypeIndex]
                    .animationBank.findClip(animation->value) == nullptr) {
            // Native name lookup returns -1 and IAnimatedObject::SetAnim
            // (0x00310fec) keeps the current clip. Do not suppress later
            // commands in the same cinematic tick.
            return Result::success();
        }
        enemy->activeAnimation = animation->value;
        const CinematicAttribute* loop = command.findAttribute("loop");
        const CinematicAttribute* reverse = command.findAttribute("reverse");
        enemy->animationLoops =
            loop == nullptr ? true : parseBool(loop->value, true);
        enemy->animationReversed =
            reverse == nullptr ? false : parseBool(reverse->value, false);
        const CinematicAttribute* speed = command.findAttribute("speed");
        enemy->animationSpeed =
            std::abs(speed == nullptr ? 1.0F
                                      : parseFloat(speed->value, 1.0F));
        const auto* clip =
            level.enemyArchetypes()[enemy->asset->archetypeIndex]
                .animationBank.findClip(animation->value);
        enemy->animationTimeMilliseconds =
            enemy->animationReversed && clip != nullptr
                ? clip->durationMilliseconds()
                : 0;
        enemy->meleeAttackActive = false;
        enemy->meleeAttackCooldownMilliseconds = 0;
        enemy->sandmanTask = SandmanBossTaskState::None;
        enemy->sandmanJumpElapsedMilliseconds = 0;
        enemy->sandmanJumpDurationMilliseconds = 0;
        enemy->robotPhantomTask = RobotPhantomTaskState::None;
        enemy->robotPhantomTaskElapsedMilliseconds = 0;
        enemy->robotPhantomSequenceIndex = 0;
        enemy->electroTask = ElectroBossTaskState::None;
        enemy->electroTaskElapsedMilliseconds = 0;
        enemy->electroSequenceIndex = 0;
        enemy->electroRangeAttacksRemaining = 0;
        enemy->electroRangeWaitMilliseconds = 0;
        enemy->electroDashesRemaining = 0;
        std::erase_if(
            boomerangs_, [objectId](const EnemyBoomerangState& boomerang) {
                return boomerang.sourceObjectId == objectId;
            });
        std::erase_if(
            thunderclaps_, [objectId](const EnemyThunderclapState& thunderclap) {
                return thunderclap.sourceObjectId == objectId;
            });
        removeElectroPosts(objectId);
        return Result::success();
    }
    if (command.name == "MoveObject") {
        const CinematicAttribute* absolute = command.findAttribute("abspos");
        const CinematicAttribute* rotation = command.findAttribute("rot");
        if (absolute != nullptr) {
            enemy->position = parseVector3(absolute->value, enemy->position);
        }
        enemy->verticalVelocity = 0.0F;
        enemy->grounded = false;
        enemy->supportInitialized = false;
        enemy->anchoredWithoutSupport = false;
        const assets::Quaternion orientation =
            rotation == nullptr ? enemy->asset->rotation
                                : parseQuaternion(rotation->value);
        enemy->worldTransform = worldMatrix(
            enemy->position, orientation,
            enemy->asset == nullptr ? assets::Vector3{1.0F, 1.0F, 1.0F}
                                    : enemy->asset->scale);
        enemy->facing = facingFromMatrix(enemy->worldTransform);
        enemy->cinematicMotion = {};
        if (const CinematicCommand* next =
                nextMoveObjectCommand(thread, command);
            next != nullptr &&
            next->timestampMilliseconds >=
                command.timestampMilliseconds + 51U) {
            const CinematicAttribute* nextAbsolute =
                next->findAttribute("abspos");
            const CinematicAttribute* nextRotation = next->findAttribute("rot");
            if (nextAbsolute != nullptr && nextRotation != nullptr) {
                enemy->cinematicMotion.startPosition = enemy->position;
                enemy->cinematicMotion.endPosition =
                    parseVector3(nextAbsolute->value, enemy->position);
                enemy->cinematicMotion.startRotation = orientation;
                enemy->cinematicMotion.endRotation =
                    parseQuaternion(nextRotation->value);
                enemy->cinematicMotion.durationMilliseconds =
                    next->timestampMilliseconds - command.timestampMilliseconds;
                enemy->cinematicMotion.active = true;
            }
        }
        return Result::success();
    }
    return Result::success();
}

const LevelEnemyState* LevelEnemyRuntime::find(
    std::int32_t objectId) const noexcept {
    const auto match = std::find_if(
        states_.begin(), states_.end(), [objectId](const LevelEnemyState& state) {
            return state.asset != nullptr && state.asset->objectId == objectId;
        });
    return match == states_.end() ? nullptr : &*match;
}

bool LevelEnemyRuntime::cinematicEnemyDead(
    const CinematicThread& thread,
    const CinematicCommand& command) const noexcept {
    // CCinematicThread::IfEnemyDead (0x003700e0) has a dedicated type-zero
    // branch: when the thread is object-bound and that object is an enemy, it
    // asks the bound enemy whether it is dead without reading IDEnemy. Level
    // 7 cinematic 141 serializes IDEnemy=-1 on its object threads, so parsing
    // that sentinel here would leave the encounter gate pending forever.
    std::int32_t enemyId = thread.objectId;
    if (thread.type != 0) {
        const CinematicAttribute* attribute =
            command.findAttribute("IDEnemy");
        if (attribute == nullptr ||
            !parseInteger(attribute->value, enemyId) || enemyId < 0) {
            return false;
        }
    }
    const LevelEnemyState* enemy = find(enemyId);
    return enemy != nullptr && enemy->health <= 0.0F;
}

const LevelEnemyState* LevelEnemyRuntime::shownHealthBarEnemy() const noexcept {
    if (!shownHealthBarObjectId_) {
        return nullptr;
    }
    const LevelEnemyState* enemy = find(*shownHealthBarObjectId_);
    // CLevel::ShowHealthBarOfEnemy (0x00387548) returns before painting when
    // the registered enemy has no health left.
    return enemy != nullptr && enemy->health > 0.0F ? enemy : nullptr;
}

LevelEnemyState* LevelEnemyRuntime::findMutable(
    std::int32_t objectId) noexcept {
    const auto match = std::find_if(
        states_.begin(), states_.end(), [objectId](const LevelEnemyState& state) {
            return state.asset != nullptr && state.asset->objectId == objectId;
        });
    return match == states_.end() ? nullptr : &*match;
}

bool LevelEnemyRuntime::registerMeleeEngager(
    LevelEnemyState& enemy) noexcept {
    if (enemy.asset == nullptr) {
        return false;
    }
    if (meleeEngagerObjectId_ == enemy.asset->objectId) {
        enemy.meleeAttackRegistered = true;
        return true;
    }
    // CanRegisterEntityForMeleeAttack (0x00375654): normal difficulty has a
    // one-entry cap. With exactly one free slot, an ordinary (non-forced)
    // registration is accepted only after the manager timer reaches <= 0.
    if (meleeEngagerObjectId_ >= 0 ||
        meleeEngagementCooldownMilliseconds_ > 0.0F) {
        enemy.meleeAttackRegistered = false;
        return false;
    }
    meleeEngagerObjectId_ = enemy.asset->objectId;
    enemy.meleeAttackRegistered = true;
    // RegisterEntityForMeleeAttack (0x00375710) consumes random(5000,15000)
    // for the list entry. UpdateRegisterEntityForMeleeAttackTimer
    // (0x003755fc) only ages entries while more than one is registered, so
    // this remains constant under the default one-attacker cap.
    enemy.meleeRegistrationTimerMilliseconds = static_cast<float>(
        nativeRandomizer_.range(5000, 15000));
    return true;
}

void LevelEnemyRuntime::unregisterMeleeEngager(
    std::int32_t objectId) noexcept {
    if (meleeEngagerObjectId_ != objectId) {
        if (LevelEnemyState* enemy = findMutable(objectId)) {
            enemy->meleeAttackRegistered = false;
            enemy->meleeRegistrationTimerMilliseconds = 0.0F;
        }
        return;
    }
    if (LevelEnemyState* enemy = findMutable(objectId)) {
        enemy->meleeAttackRegistered = false;
        enemy->meleeRegistrationTimerMilliseconds = 0.0F;
    }
    meleeEngagerObjectId_ = -1;
    // UnRegisterEntityForMeleeAttack (0x00375560) refreshes the manager gate
    // through random(baseDelay, baseDelay * 2), where the difficulty-one
    // base delay installed by 0x003744a4 is 1000 ms.
    meleeEngagementCooldownMilliseconds_ = static_cast<float>(
        nativeRandomizer_.range(
            kMeleeEngagementHandoffMinimumMilliseconds,
            kMeleeEngagementHandoffMaximumMilliseconds));
}

} // namespace usm::game
