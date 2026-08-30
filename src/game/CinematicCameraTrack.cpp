#include "game/CinematicCameraTrack.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>

namespace usm::game {
namespace {

bool parseFloat(std::string_view text, float& output) noexcept {
    const std::string storage(text);
    char* end = nullptr;
    output = std::strtof(storage.c_str(), &end);
    return end != storage.c_str() && *end == '\0' && std::isfinite(output);
}

bool parseVector3(std::string_view text, assets::Vector3& output) noexcept {
    const std::string storage(text);
    const char* cursor = storage.c_str();
    const char* const finish = cursor + storage.size();
    std::array<float*, 3> components{&output.x, &output.y, &output.z};
    for (std::size_t index = 0; index < components.size(); ++index) {
        char* end = nullptr;
        *components[index] = std::strtof(cursor, &end);
        if (end == cursor || !std::isfinite(*components[index])) {
            return false;
        }
        cursor = end;
        while (cursor != finish && (*cursor == ' ' || *cursor == '\t')) {
            ++cursor;
        }
        if (index + 1 < components.size()) {
            if (cursor == finish || *cursor != ',') {
                return false;
            }
            ++cursor;
        }
    }
    while (cursor != finish && (*cursor == ' ' || *cursor == '\t')) {
        ++cursor;
    }
    return cursor == finish;
}

assets::Vector3 add(const assets::Vector3& left,
                    const assets::Vector3& right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

assets::Vector3 subtract(const assets::Vector3& left,
                         const assets::Vector3& right) noexcept {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

assets::Vector3 scale(const assets::Vector3& value, float amount) noexcept {
    return {value.x * amount, value.y * amount, value.z * amount};
}

assets::Vector3 linear(const assets::Vector3& from,
                       const assets::Vector3& to, float time) noexcept {
    return add(from, scale(subtract(to, from), time));
}

assets::Vector3 catmullRom(const assets::Vector3& previous,
                           const assets::Vector3& from,
                           const assets::Vector3& to,
                           const assets::Vector3& next, float time) noexcept {
    const float timeSquared = time * time;
    const float timeCubed = timeSquared * time;
    const float fromWeight = 2.0F * timeCubed - 3.0F * timeSquared + 1.0F;
    const float toWeight = -2.0F * timeCubed + 3.0F * timeSquared;
    const float fromTangentWeight = timeCubed - 2.0F * timeSquared + time;
    const float toTangentWeight = timeCubed - timeSquared;
    return add(add(scale(from, fromWeight), scale(to, toWeight)),
               add(scale(subtract(to, previous),
                         0.5F * fromTangentWeight),
                   scale(subtract(next, from), 0.5F * toTangentWeight)));
}

} // namespace

Result CinematicCameraTrack::load(const CinematicScript& script) {
    keyframes_.clear();
    for (const CinematicThread& thread : script.threads()) {
        if (thread.type != 2) {
            continue;
        }
        for (const CinematicCommand& command : thread.commands) {
            if (command.name != "ChangeCamera") {
                continue;
            }
            const CinematicAttribute* targetAttribute =
                command.findAttribute("target");
            const CinematicAttribute* directionAttribute =
                command.findAttribute("dir");
            const CinematicAttribute* distanceAttribute =
                command.findAttribute("Distance");
            if (targetAttribute == nullptr || directionAttribute == nullptr ||
                distanceAttribute == nullptr) {
                keyframes_.clear();
                return Result::failure(
                    "ChangeCamera is missing target, dir, or Distance");
            }

            assets::Vector3 target;
            assets::Vector3 direction;
            float distance = 0.0F;
            if (!parseVector3(targetAttribute->value, target) ||
                !parseVector3(directionAttribute->value, direction) ||
                !parseFloat(distanceAttribute->value, distance)) {
                keyframes_.clear();
                return Result::failure("ChangeCamera has invalid coordinates");
            }

            CinematicCameraKeyframe keyframe;
            keyframe.timestampMilliseconds = command.timestampMilliseconds;
            keyframe.pose.target = target;
            keyframe.pose.position = subtract(target, scale(direction, distance));
            keyframe.pose.farPlane = 10000.0F;
            if (const CinematicAttribute* curve =
                    command.findAttribute("curve")) {
                if (curve->value == "true" || curve->value == "1") {
                    keyframe.curvedInterpolation = true;
                } else if (curve->value != "false" && curve->value != "0") {
                    keyframes_.clear();
                    return Result::failure(
                        "ChangeCamera has an invalid curve flag");
                }
            }
            keyframes_.push_back(keyframe);
        }
    }
    if (!std::is_sorted(
            keyframes_.begin(), keyframes_.end(),
            [](const CinematicCameraKeyframe& left,
               const CinematicCameraKeyframe& right) {
                return left.timestampMilliseconds <
                       right.timestampMilliseconds;
            })) {
        keyframes_.clear();
        return Result::failure("ChangeCamera keyframes are out of order");
    }
    return Result::success();
}

CameraPose CinematicCameraTrack::sample(
    std::uint32_t timestampMilliseconds) const noexcept {
    if (keyframes_.empty()) {
        return {};
    }
    const auto after = std::upper_bound(
        keyframes_.begin(), keyframes_.end(), timestampMilliseconds,
        [](std::uint32_t timestamp,
           const CinematicCameraKeyframe& keyframe) {
            return timestamp < keyframe.timestampMilliseconds;
        });
    if (after == keyframes_.begin()) {
        return keyframes_.front().pose;
    }
    const std::size_t fromIndex =
        static_cast<std::size_t>((after - keyframes_.begin()) - 1);
    if (after == keyframes_.end()) {
        return keyframes_[fromIndex].pose;
    }
    const std::size_t toIndex = fromIndex + 1;
    const auto& from = keyframes_[fromIndex];
    const auto& to = keyframes_[toIndex];
    const std::uint32_t duration =
        to.timestampMilliseconds - from.timestampMilliseconds;
    if (duration <= 50) {
        return from.pose;
    }
    const float time = static_cast<float>(timestampMilliseconds -
                                          from.timestampMilliseconds) /
                       static_cast<float>(duration);
    CameraPose pose = from.pose;
    if (!from.curvedInterpolation) {
        pose.position = linear(from.pose.position, to.pose.position, time);
        pose.target = linear(from.pose.target, to.pose.target, time);
        return pose;
    }
    const std::size_t previousIndex =
        fromIndex == 0 ? keyframes_.size() - 1 : fromIndex - 1;
    const std::size_t nextIndex =
        (toIndex + 1) % keyframes_.size();
    pose.position = catmullRom(keyframes_[previousIndex].pose.position,
                              from.pose.position, to.pose.position,
                              keyframes_[nextIndex].pose.position, time);
    pose.target = catmullRom(keyframes_[previousIndex].pose.target,
                            from.pose.target, to.pose.target,
                            keyframes_[nextIndex].pose.target, time);
    return pose;
}

} // namespace usm::game
