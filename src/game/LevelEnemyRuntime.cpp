#include "game/LevelEnemyRuntime.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string_view>

namespace usm::game {
namespace {

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

} // namespace

Result LevelEnemyRuntime::initialize(const LevelOneBootstrap& level) {
    states_.clear();
    states_.reserve(level.enemies().size());
    for (const LevelEnemyAsset& enemy : level.enemies()) {
        if (enemy.archetypeIndex >= level.enemyArchetypes().size()) {
            states_.clear();
            return Result::failure("Enemy archetype index is invalid");
        }
        states_.push_back({&enemy,
                           enemy.position,
                           enemy.worldTransform,
                           enemy.initialAnimation,
                           0,
                           1.0F,
                           enemy.health,
                           enemy.visible,
                           enemy.aiEnabled});
    }
    return Result::success();
}

void LevelEnemyRuntime::update(std::uint32_t elapsedMilliseconds) noexcept {
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

Result LevelEnemyRuntime::applyCinematicCommand(
    const LevelOneBootstrap& level, const CinematicThread& thread,
    const CinematicCommand& command) {
    LevelEnemyState* enemy = findMutable(thread.objectId);
    if (enemy == nullptr) {
        return Result::success();
    }
    if (command.name == "DisableAI") {
        enemy->aiEnabled = false;
        return Result::success();
    }
    if (command.name == "EnableAI") {
        enemy->aiEnabled = true;
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
