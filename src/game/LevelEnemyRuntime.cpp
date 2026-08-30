#include "game/LevelEnemyRuntime.hpp"

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

assets::Vector3 facingFromMatrix(
    const std::array<float, 16>& matrix) noexcept {
    const float length = std::hypot(matrix[4], matrix[5]);
    if (length <= std::numeric_limits<float>::epsilon()) {
        return {1.0F, 0.0F, 0.0F};
    }
    return {-matrix[4] / length, -matrix[5] / length, 0.0F};
}

std::string_view idleAnimation(const LevelEnemyAsset& enemy) noexcept {
    if (enemy.gameType == "MeleeThugEnemy_knife") {
        return "idle_knife_at_idle";
    }
    if (enemy.gameType == "RangeThug_big") {
        return "idlebaz";
    }
    if (enemy.gameType == "MeleeThug_gun" ||
        enemy.gameType == "RangeThug_hammer" ||
        enemy.gameType == "Boss_Sandman") {
        return "idle";
    }
    return "idle_at1_idle";
}

void setFacing(LevelEnemyState& enemy, const assets::Vector3& facing) noexcept {
    if (enemy.asset == nullptr) {
        return;
    }
    enemy.facing = facing;
    const assets::Vector3& scale = enemy.asset->scale;
    enemy.worldTransform = {
        -facing.y * scale.x,
        facing.x * scale.x,
        0.0F,
        0.0F,
        -facing.x * scale.y,
        -facing.y * scale.y,
        0.0F,
        0.0F,
        0.0F,
        0.0F,
        scale.z,
        0.0F,
        enemy.position.x,
        enemy.position.y,
        enemy.position.z,
        1.0F};
}

bool crossedLoopEvent(std::uint32_t previousTime,
                      std::uint32_t currentTime, std::uint32_t duration,
                      std::uint32_t eventTime) noexcept {
    if (duration == 0 || currentTime <= previousTime || eventTime >= duration) {
        return false;
    }
    std::uint64_t nextOccurrence =
        (static_cast<std::uint64_t>(previousTime) / duration) * duration +
        eventTime;
    if (nextOccurrence <= previousTime) {
        nextOccurrence += duration;
    }
    return nextOccurrence <= currentTime;
}

} // namespace

Result LevelEnemyRuntime::initialize(const LevelOneBootstrap& level) {
    states_.clear();
    pendingPlayerHits_.clear();
    pendingSoundCues_.clear();
    level_ = &level;
    states_.reserve(level.enemies().size());
    for (const LevelEnemyAsset& enemy : level.enemies()) {
        if (enemy.archetypeIndex >= level.enemyArchetypes().size()) {
            states_.clear();
            return Result::failure("Enemy archetype index is invalid");
        }
        states_.push_back({&enemy,
                           enemy.position,
                           facingFromMatrix(enemy.worldTransform),
                           enemy.worldTransform,
                           enemy.initialAnimation,
                           0,
                           1.0F,
                           true,
                           enemy.health,
                           enemy.visible,
                           enemy.aiEnabled,
                           false,
                           enemy.aiEnabled ? EnemyBehaviorState::Idle
                                           : EnemyBehaviorState::Disabled,
                           0,
                           0});
    }
    return Result::success();
}

void LevelEnemyRuntime::advanceAnimations(
    std::uint32_t elapsedMilliseconds) noexcept {
    for (LevelEnemyState& enemy : states_) {
        const double advanced =
            static_cast<double>(elapsedMilliseconds) * enemy.animationSpeed;
        const std::uint64_t next = enemy.animationTimeMilliseconds +
                                   static_cast<std::uint64_t>(
                                       std::max(advanced, 0.0));
        enemy.animationTimeMilliseconds = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(next,
                                    std::numeric_limits<std::uint32_t>::max()));
    }
}

void LevelEnemyRuntime::updateGameplay(
    std::uint32_t elapsedMilliseconds,
    const assets::Vector3& playerPosition,
    const LevelCollision* collision) noexcept {
    for (LevelEnemyState& enemy : states_) {
        if (enemy.asset == nullptr) {
            continue;
        }
        if (enemy.health <= 0.0F) {
            enemy.behavior = EnemyBehaviorState::Dead;
            continue;
        }
        if (enemy.behavior == EnemyBehaviorState::Hurt) {
            const EnemyArchetypeAsset& archetype =
                level_->enemyArchetypes()[enemy.asset->archetypeIndex];
            const assets::ColladaAnimationClip* clip =
                archetype.animationBank.findClip(enemy.activeAnimation);
            if (clip != nullptr &&
                enemy.animationTimeMilliseconds <
                    clip->durationMilliseconds()) {
                continue;
            }
            enemy.behavior = EnemyBehaviorState::Idle;
            enemy.activeAnimation = std::string(idleAnimation(*enemy.asset));
            enemy.animationTimeMilliseconds = 0;
            enemy.animationLoops = true;
        }
        if (!enemy.visible || !enemy.aiEnabled) {
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

        const float attackRange = maximumAttackReach(enemy);
        if (attackRange > 0.0F &&
            distanceSquared <= attackRange * attackRange) {
            const EnemyBehaviorState previousBehavior = enemy.behavior;
            enemy.behavior = EnemyBehaviorState::AttackRange;
            const std::string_view idle = idleAnimation(*enemy.asset);
            const float distance = std::sqrt(distanceSquared);
            if (distance > std::numeric_limits<float>::epsilon()) {
                setFacing(enemy, {toPlayerX / distance, toPlayerY / distance,
                                  0.0F});
            }
            if (enemy.activeAnimation != idle ||
                previousBehavior != EnemyBehaviorState::AttackRange) {
                enemy.activeAnimation = idle;
                enemy.animationTimeMilliseconds = 0;
                enemy.animationLoops = true;
            }
            continue;
        }

        const float distance = std::sqrt(distanceSquared);
        if (distance <= std::numeric_limits<float>::epsilon()) {
            continue;
        }
        const float inverseDistance = 1.0F / distance;
        const assets::Vector3 facing{toPlayerX * inverseDistance,
                                     toPlayerY * inverseDistance, 0.0F};
        const float maximumTravel =
            enemy.asset->lineSpeedCentimetersPerMillisecond *
            static_cast<float>(elapsedMilliseconds);
        const float travel = std::min(
            maximumTravel, distance - attackRange);
        assets::Vector3 desired = enemy.position;
        desired.x += facing.x * travel;
        desired.y += facing.y * travel;
        if (collision != nullptr) {
            assets::Vector3 resolved;
            (void)collision->resolveGroundMotion(enemy.position, desired,
                                                 resolved);
            enemy.position = resolved;
        } else {
            enemy.position = desired;
        }
        setFacing(enemy, facing);
        enemy.behavior = EnemyBehaviorState::Chasing;
        if (enemy.activeAnimation != "run") {
            enemy.activeAnimation = "run";
            enemy.animationTimeMilliseconds = 0;
            enemy.animationLoops = true;
        }
    }
    std::vector<std::uint32_t> previousAnimationTimes;
    previousAnimationTimes.reserve(states_.size());
    for (const LevelEnemyState& enemy : states_) {
        previousAnimationTimes.push_back(enemy.animationTimeMilliseconds);
    }
    advanceAnimations(elapsedMilliseconds);
    for (std::size_t index = 0; index < states_.size(); ++index) {
        if (states_[index].behavior == EnemyBehaviorState::AttackRange) {
            queueAuthoredAttackEvents(states_[index],
                                      previousAnimationTimes[index],
                                      playerPosition);
        }
    }
}

std::optional<std::int32_t> LevelEnemyRuntime::applyPlayerMeleeHit(
    const assets::Vector3& attackPosition,
    const assets::Vector3& attackDirection, float radius, float damage,
    float minimumForwardDot) noexcept {
    if (radius <= 0.0F || damage <= 0.0F) {
        return std::nullopt;
    }
    LevelEnemyState* nearest = nullptr;
    float nearestDistanceSquared = radius * radius;
    const float directionLength = std::hypot(attackDirection.x,
                                             attackDirection.y);
    const float directionX = directionLength > 1e-5F
                                 ? attackDirection.x / directionLength
                                 : 0.0F;
    const float directionY = directionLength > 1e-5F
                                 ? attackDirection.y / directionLength
                                 : 0.0F;
    for (LevelEnemyState& enemy : states_) {
        if (enemy.asset == nullptr || !enemy.visible || enemy.health <= 0.0F) {
            continue;
        }
        const float x = enemy.position.x - attackPosition.x;
        const float y = enemy.position.y - attackPosition.y;
        const float distanceSquared = x * x + y * y;
        if (distanceSquared > 1e-5F && directionLength > 1e-5F) {
            const float inverseDistance = 1.0F / std::sqrt(distanceSquared);
            const float forwardDot =
                (x * directionX + y * directionY) * inverseDistance;
            if (forwardDot < minimumForwardDot) {
                continue;
            }
        }
        if (distanceSquared <= nearestDistanceSquared) {
            nearest = &enemy;
            nearestDistanceSquared = distanceSquared;
        }
    }
    if (nearest == nullptr) {
        return std::nullopt;
    }
    nearest->health = std::max(0.0F, nearest->health - damage);
    nearest->playerDetected = true;
    if (nearest->health == 0.0F) {
        enterDeadState(*nearest);
    } else {
        nearest->behavior = EnemyBehaviorState::Hurt;
        selectStateAnimation(*nearest, "ENEMY_BEHAVIOR_HURT_STATE_COMMON",
                             false);
        queueStateSound(*nearest, "ENEMY_BEHAVIOR_HURT_STATE_COMMON");
    }
    return nearest->asset->objectId;
}

bool LevelEnemyRuntime::destroy(std::int32_t objectId) noexcept {
    LevelEnemyState* enemy = findMutable(objectId);
    if (enemy == nullptr) {
        return false;
    }
    if (enemy->health > 0.0F) {
        enemy->health = 0.0F;
        enterDeadState(*enemy);
    }
    return true;
}

std::vector<EnemyMeleeHit> LevelEnemyRuntime::consumePlayerHits() noexcept {
    std::vector<EnemyMeleeHit> hits = std::move(pendingPlayerHits_);
    pendingPlayerHits_.clear();
    return hits;
}

std::vector<EnemySoundCue> LevelEnemyRuntime::consumeSoundCues() noexcept {
    std::vector<EnemySoundCue> cues = std::move(pendingSoundCues_);
    pendingSoundCues_.clear();
    return cues;
}

float LevelEnemyRuntime::maximumAttackReach(
    const LevelEnemyState& enemy) const noexcept {
    if (level_ == nullptr || enemy.asset == nullptr) {
        return 0.0F;
    }
    float maximumReach = 0.0F;
    const auto events = level_->enemySpecialActions().findAttackEvents(
        enemy.asset->enemyTypeId,
        enemy.asset->gameType == "MeleeThugEnemy_knife"
            ? "idle_knife_at_idle"
            : "idle_at1_idle");
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
    const auto events = level_->enemySpecialActions().findAttackEvents(
        enemy.asset->enemyTypeId, enemy.activeAnimation);
    for (const EnemyAnimationSpecialAction* event : events) {
        if (event == nullptr || event->keyFramePercent < 0 ||
            event->keyFramePercent >= 100 || event->attackId < 0 ||
            event->attackId > std::numeric_limits<std::int16_t>::max()) {
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
        const auto attackId = static_cast<std::int16_t>(event->attackId);
        const AttackDefinition* attack = level_->attackConfigs().find(attackId);
        if (attack == nullptr) {
            continue;
        }
        const float toPlayerX = playerPosition.x - enemy.position.x;
        const float toPlayerY = playerPosition.y - enemy.position.y;
        const float distance = std::hypot(toPlayerX, toPlayerY);
        if (distance > attack->maximumReach()) {
            continue;
        }
        if (distance > std::numeric_limits<float>::epsilon()) {
            const float inverseDistance = 1.0F / distance;
            const float forwardDot =
                enemy.facing.x * toPlayerX * inverseDistance +
                enemy.facing.y * toPlayerY * inverseDistance;
            const float side = enemy.facing.x * toPlayerY * inverseDistance -
                               enemy.facing.y * toPlayerX * inverseDistance;
            const float angleDegrees =
                std::atan2(side, forwardDot) * kRadiansToDegrees;
            if (angleDegrees < attack->minimumAngleDegrees ||
                angleDegrees > attack->maximumAngleDegrees) {
                continue;
            }
        }
        pendingPlayerHits_.push_back(
            {enemy.asset->objectId, attackId, attack->damage});
    }
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
    enemy.animationLoops = loop;
}

void LevelEnemyRuntime::enterDeadState(LevelEnemyState& enemy) {
    enemy.aiEnabled = false;
    enemy.behavior = EnemyBehaviorState::Dead;
    selectStateAnimation(enemy, "ENEMY_BEHAVIOR_DEAD_STATE", false);
    queueStateSound(enemy, "ENEMY_BEHAVIOR_DEAD_STATE");
}

Result LevelEnemyRuntime::applyCinematicCommand(
    const LevelOneBootstrap& level, const CinematicThread& thread,
    const CinematicCommand& command) {
    LevelEnemyState* enemy = findMutable(thread.objectId);
    if (enemy == nullptr) {
        return Result::success();
    }
    if (command.name == "KillObject") {
        (void)destroy(thread.objectId);
        return Result::success();
    }
    if (command.name == "DisableAI") {
        enemy->aiEnabled = false;
        enemy->behavior = EnemyBehaviorState::Disabled;
        return Result::success();
    }
    if (command.name == "EnableAI") {
        enemy->aiEnabled = true;
        enemy->behavior = EnemyBehaviorState::Idle;
        return Result::success();
    }
    if (command.name == "SetVisible") {
        const CinematicAttribute* visible = command.findAttribute("Visible");
        enemy->visible =
            visible == nullptr ? true : parseBool(visible->value, true);
        return Result::success();
    }
    if (command.name == "SetAnim") {
        const CinematicAttribute* animation = command.findAttribute("$Anim");
        if (animation == nullptr || enemy->asset == nullptr ||
            enemy->asset->archetypeIndex >= level.enemyArchetypes().size() ||
            level.enemyArchetypes()[enemy->asset->archetypeIndex]
                    .animationBank.findClip(animation->value) == nullptr) {
            return Result::failure("Enemy SetAnim references a missing clip");
        }
        enemy->activeAnimation = animation->value;
        enemy->animationTimeMilliseconds = 0;
        enemy->animationLoops = true;
        const CinematicAttribute* speed = command.findAttribute("speed");
        enemy->animationSpeed =
            speed == nullptr ? 1.0F : parseFloat(speed->value, 1.0F);
        return Result::success();
    }
    if (command.name == "MoveObject") {
        const CinematicAttribute* absolute = command.findAttribute("abspos");
        const CinematicAttribute* local = command.findAttribute("pos");
        const CinematicAttribute* rotation = command.findAttribute("rot");
        if (absolute != nullptr) {
            enemy->position = parseVector3(absolute->value, enemy->position);
        } else if (local != nullptr) {
            enemy->position = parseVector3(local->value, enemy->position);
        }
        const assets::Quaternion orientation =
            rotation == nullptr ? enemy->asset->rotation
                                : parseQuaternion(rotation->value);
        enemy->worldTransform = worldMatrix(
            enemy->position, orientation,
            enemy->asset == nullptr ? assets::Vector3{1.0F, 1.0F, 1.0F}
                                    : enemy->asset->scale);
        enemy->facing = facingFromMatrix(enemy->worldTransform);
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

LevelEnemyState* LevelEnemyRuntime::findMutable(
    std::int32_t objectId) noexcept {
    const auto match = std::find_if(
        states_.begin(), states_.end(), [objectId](const LevelEnemyState& state) {
            return state.asset != nullptr && state.asset->objectId == objectId;
        });
    return match == states_.end() ? nullptr : &*match;
}

} // namespace usm::game
