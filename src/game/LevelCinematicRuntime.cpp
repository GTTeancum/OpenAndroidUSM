#include "game/LevelCinematicRuntime.hpp"

#include <algorithm>
#include <charconv>
#include <string_view>

namespace usm::game {
namespace {

const CinematicAttribute* attribute(const CinematicCommand& command,
                                    std::string_view name) noexcept {
    return command.findAttribute(name);
}

bool parseInteger(const CinematicCommand& command, std::string_view name,
                  std::int32_t& output) noexcept {
    const CinematicAttribute* value = attribute(command, name);
    if (value == nullptr) {
        return false;
    }
    const char* begin = value->value.data();
    const char* end = begin + value->value.size();
    const auto parsed = std::from_chars(begin, end, output);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

bool parseBoolean(const CinematicCommand& command, std::string_view name,
                  bool& output) noexcept {
    const CinematicAttribute* value = attribute(command, name);
    if (value == nullptr) {
        return false;
    }
    if (value->value == "true" || value->value == "1") {
        output = true;
        return true;
    }
    if (value->value == "false" || value->value == "0") {
        output = false;
        return true;
    }
    return false;
}

} // namespace

void LevelCinematicRuntime::bind(LevelTriggerRuntime& triggers,
                                 GameplayCamera& camera,
                                 std::span<const LevelWayPointAsset>
                                     waypoints) noexcept {
    triggers_ = &triggers;
    camera_ = &camera;
    waypoints_ = waypoints;
    cinematicStartRequests_.clear();
    levelEnded_ = false;
    goToNextLevel_ = false;
    gameEnded_ = false;
}

Result LevelCinematicRuntime::applyCommand(const CinematicCommand& command) {
    if (triggers_ == nullptr || camera_ == nullptr) {
        return Result::failure("Level cinematic runtime is not bound");
    }
    if (command.name == "DisableTrigger" || command.name == "EnableTrigger") {
        std::int32_t triggerId = -1;
        if (!parseInteger(command, "^ID^Trigger", triggerId) ||
            !triggers_->setEnabled(triggerId,
                                   command.name == "EnableTrigger")) {
            return Result::failure(command.name +
                                   " references an unknown trigger");
        }
        return Result::success();
    }
    if (command.name == "EnableCameraArea") {
        std::int32_t areaId = -1;
        bool enabled = false;
        if (!parseInteger(command, "^ID^CameraArea", areaId) ||
            !parseBoolean(command, "enable", enabled)) {
            return Result::failure("EnableCameraArea has invalid attributes");
        }
        // CCinematicThread::EnalbeCameraArea (0x00371990) deliberately
        // succeeds when the authored area lookup returns null.
        (void)camera_->setAreaEnabled(areaId, enabled);
        return Result::success();
    }
    if (command.name == "SetCameraArea") {
        std::int32_t areaId = -1;
        if (!parseInteger(command, "^ID^CameraArea", areaId)) {
            return Result::failure("SetCameraArea has no valid area ID");
        }
        // CCinematicThread::SetCameraArea (0x00371a28) has the same no-op
        // success behavior. Level 1 contains one such stale area-224 command.
        (void)camera_->setCurrentArea(areaId);
        return Result::success();
    }
    if (command.name == "StartCinematic") {
        std::int32_t cinematicId = -1;
        if (!parseInteger(command, "CinematicID", cinematicId)) {
            return Result::failure("StartCinematic has no valid CinematicID");
        }
        cinematicStartRequests_.push_back(cinematicId);
        return Result::success();
    }
    if (command.name == "StartSlide") {
        std::int32_t startId = -1;
        std::int32_t endId = -1;
        if (!parseInteger(command, "^SID^WayPoint", startId) ||
            !parseInteger(command, "^EID^WayPoint", endId)) {
            return Result::failure("StartSlide has invalid waypoint IDs");
        }
        const auto hasWaypoint = [this](std::int32_t id) {
            return std::any_of(
                waypoints_.begin(), waypoints_.end(),
                [id](const LevelWayPointAsset& point) {
                    return point.objectId == id;
                });
        };
        // CCinematicThread::StartSlide (0x003701c0) only resolves and
        // validates both endpoints. CSlider performs the actual proximity
        // catch independently in its update path.
        if (!hasWaypoint(startId) || !hasWaypoint(endId)) {
            return Result::failure("StartSlide references an unknown waypoint");
        }
        return Result::success();
    }
    if (command.name == "LevelEnd") {
        bool goToNext = false;
        if (!parseBoolean(command, "GoToNext", goToNext)) {
            return Result::failure("LevelEnd has no valid GoToNext flag");
        }
        levelEnded_ = true;
        goToNextLevel_ = goToNext;
        return Result::success();
    }
    if (command.name == "GameEnd") {
        gameEnded_ = true;
        return Result::success();
    }
    return Result::success();
}

std::vector<std::int32_t>
LevelCinematicRuntime::consumeCinematicStartRequests() {
    std::vector<std::int32_t> requests;
    requests.swap(cinematicStartRequests_);
    return requests;
}

} // namespace usm::game
