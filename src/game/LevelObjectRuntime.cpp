#include "game/LevelObjectRuntime.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace usm::game {
namespace {

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

} // namespace

Result LevelObjectRuntime::initialize(const LevelOneBootstrap& level) {
    states_.clear();
    states_.reserve(level.objects().size());
    for (const LevelObjectAsset& object : level.objects()) {
        if (object.archetypeIndex >= level.objectArchetypes().size()) {
            states_.clear();
            return Result::failure("Level object archetype index is invalid");
        }
        states_.push_back({&object, object.position, object.worldTransform,
                           object.initialAnimation, 0, 1.0F, true,
                           object.visible, false});
    }
    return Result::success();
}

void LevelObjectRuntime::advanceAnimations(
    std::uint32_t elapsedMilliseconds) noexcept {
    for (LevelObjectState& object : states_) {
        const double advanced =
            static_cast<double>(elapsedMilliseconds) * object.animationSpeed;
        object.animationTimeMilliseconds += static_cast<std::uint32_t>(
            std::clamp(advanced, 0.0,
                       static_cast<double>(
                           std::numeric_limits<std::uint32_t>::max())));
    }
}

Result LevelObjectRuntime::applyCinematicCommand(
    const LevelOneBootstrap& level, const CinematicThread& thread,
    const CinematicCommand& command) {
    std::int32_t objectId = commandObjectId(thread, command);
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
    if (command.name == "SetVisible" || command.name == "ShowStream") {
        const CinematicAttribute* visible = command.findAttribute("Visible");
        object->visible = visible == nullptr
                              ? true
                              : parseBoolean(visible->value, true);
        return Result::success();
    }
    if (command.name == "SetAnim") {
        const CinematicAttribute* animation = command.findAttribute("$Anim");
        if (animation == nullptr || object->asset == nullptr ||
            object->asset->archetypeIndex >= level.objectArchetypes().size()) {
            return Result::failure("Level object SetAnim has no valid clip");
        }
        const LevelObjectArchetypeAsset& archetype =
            level.objectArchetypes()[object->asset->archetypeIndex];
        if (archetype.animationBank.findClip(animation->value) == nullptr) {
            return Result::failure(
                "Level object SetAnim references a missing clip");
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
        return Result::success();
    }
    if (command.name == "Physics") {
        // CCinematicThread::Physics hands the object to
        // CDestroyableObject::SetPhysics (0x003061cc). The renderer-visible
        // state is retained here; rigid-body integration is reconstructed in
        // the collision/physics slice.
        object->physicsEnabled = true;
        return Result::success();
    }
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

LevelObjectState* LevelObjectRuntime::findMutable(
    std::int32_t objectId) noexcept {
    const auto match = std::find_if(
        states_.begin(), states_.end(), [objectId](const LevelObjectState& state) {
            return state.asset != nullptr && state.asset->objectId == objectId;
        });
    return match == states_.end() ? nullptr : &*match;
}

} // namespace usm::game
