#include "game/LevelCinematicRuntime.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
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

bool parseFloat(const CinematicCommand& command, std::string_view name,
                float& output) noexcept {
    const CinematicAttribute* value = attribute(command, name);
    if (value == nullptr) {
        return false;
    }
    const char* begin = value->value.data();
    const char* end = begin + value->value.size();
    const auto parsed = std::from_chars(begin, end, output);
    return parsed.ec == std::errc{} && parsed.ptr == end &&
           std::isfinite(output);
}

bool parseRoomList(std::string_view text,
                   std::array<bool, 16>& output) noexcept {
    output.fill(false);
    while (!text.empty()) {
        const std::size_t comma = text.find(',');
        std::string_view item = text.substr(0, comma);
        const std::size_t first = item.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos) {
            return false;
        }
        const std::size_t last = item.find_last_not_of(" \t\r\n");
        item = item.substr(first, last - first + 1);
        std::int32_t roomId = 0;
        const auto parsed = std::from_chars(item.data(),
                                            item.data() + item.size(), roomId);
        if (parsed.ec != std::errc{} ||
            parsed.ptr != item.data() + item.size() || roomId < 1 ||
            roomId > static_cast<std::int32_t>(output.size())) {
            return false;
        }
        output[static_cast<std::size_t>(roomId - 1)] = true;
        if (comma == std::string_view::npos) {
            break;
        }
        text.remove_prefix(comma + 1);
    }
    return true;
}

float flatDistance(const assets::Vector3& first,
                   const assets::Vector3& second) noexcept {
    const float x = first.x - second.x;
    const float y = first.y - second.y;
    return std::sqrt(x * x + y * y);
}

// Exact scalar form of GetYOnCubicBezier2D (0x003a3784). Despite the native
// symbol, this is a three-point quadratic curve: solve its X component for t,
// then evaluate Y at that same t.
float yOnQuadraticBezier(float x0, float y0, float x1, float y1,
                         float x2, float y2, float x) noexcept {
    if (x <= x0) {
        return y0;
    }
    if (x >= x2) {
        return y2;
    }
    const float a = x0 - 2.0F * x1 + x2;
    const float b = x1 - x0;
    float t = 0.0F;
    if (a == 0.0F) {
        t = (x - x0) * 0.5F / b;
    } else {
        const float discriminant = b * b - (x0 - x) * a;
        const float root = std::sqrt(std::max(discriminant, 0.0F));
        t = ((x0 - x1) + root) / a;
        if (t < 0.0F || t > 1.0F) {
            t = ((x0 - x1) - root) / a;
        }
    }
    const float inverse = 1.0F - t;
    return inverse * inverse * y0 + 2.0F * t * inverse * y1 +
           t * t * y2;
}

assets::Vector3 subtract(const assets::Vector3& first,
                         const assets::Vector3& second) noexcept {
    return {first.x - second.x, first.y - second.y, first.z - second.z};
}

assets::Vector3 add(const assets::Vector3& first,
                    const assets::Vector3& second) noexcept {
    return {first.x + second.x, first.y + second.y, first.z + second.z};
}

float dot(const assets::Vector3& first,
          const assets::Vector3& second) noexcept {
    return first.x * second.x + first.y * second.y + first.z * second.z;
}

float lengthSquared(const assets::Vector3& value) noexcept {
    return dot(value, value);
}

assets::Vector3 normalized(const assets::Vector3& value) noexcept {
    const float valueLength = std::sqrt(lengthSquared(value));
    if (valueLength <= std::numeric_limits<float>::epsilon()) {
        return {};
    }
    return {value.x / valueLength, value.y / valueLength,
            value.z / valueLength};
}

} // namespace

void LevelCinematicRuntime::bind(LevelTriggerRuntime& triggers,
                                 GameplayCamera& camera,
                                 std::span<const LevelWayPointAsset>
                                     waypoints,
                                 std::span<const LevelRoomAsset> rooms)
    noexcept {
    triggers_ = &triggers;
    camera_ = &camera;
    waypoints_ = waypoints;
    roomMotionStates_.clear();
    roomMotionStates_.reserve(rooms.size());
    for (std::size_t roomIndex = 0; roomIndex < rooms.size(); ++roomIndex) {
        const LevelRoomAsset& asset = rooms[roomIndex];
        RoomMotionState state;
        state.objectId = asset.objectId;
        state.roomId = static_cast<std::int32_t>(roomIndex + 1);
        state.linkedWaypointId = asset.linkedWaypointId;
        state.position = asset.position;
        state.lineSpeedCentimetersPerMillisecond =
            asset.lineSpeedCentimetersPerMillisecond;
        state.active = asset.motionInitiallyActive;
        state.movingRoom = asset.linkedWaypointId > 0;
        initializeRoomPath(state, false);
        roomMotionStates_.push_back(state);
    }
    cinematicStartRequests_.clear();
    slowMotionSoundCues_.clear();
    transportSoundCues_.clear();
    slowMotionDenominator_ = 1.0F;
    slowMotionElapsedMilliseconds_ = 0.0F;
    slowMotionHoldMilliseconds_ = 0.0F;
    slowMotionRampMilliseconds_ = 0.0F;
    slowMotionSoundEnabled_ = false;
    cameraShakeMaximumOffset_ = 0.0F;
    cameraShakeFramesRemaining_ = 0;
    cameraShakeTotalFrames_ = 0;
    cameraShakeXRate_ = 0.0F;
    cameraShakeYRate_ = 0.0F;
    cameraShakeZRate_ = 0.0F;
    cameraShakeSign_ = 1;
    cameraShakeTickRemainderMilliseconds_ = 0;
    cameraShakeOffset_ = {};
    forcedVisibleRooms_.fill(false);
    levelEnded_ = false;
    goToNextLevel_ = false;
    gameEnded_ = false;
    lastCheckpointId_ = -1;
    unlockedSkills_.fill(false);
    bossRushTimerRunning_ = false;
    transportRequested_ = false;
    transport_ = {};
    controlsEnabled_ = true;
    attributionEnabled_ = true;
    objectiveArrowEnabled_ = true;
    blackOverlayEnabled_ = false;
    skipEnabled_ = false;
    listenerOnMainCharacter_ = true;
    bossProgress_ = {};
}

Result LevelCinematicRuntime::applyCommand(const CinematicThread& thread,
                                           const CinematicCommand& command) {
    if (triggers_ == nullptr || camera_ == nullptr) {
        return Result::failure("Level cinematic runtime is not bound");
    }
    if (command.name == "ActiveRoom" || command.name == "DeactiveRoom" ||
        command.name == "RevertRoomsPosition") {
        std::int32_t roomObjectId = -1;
        if (!parseInteger(command, "^ID^Geometry", roomObjectId)) {
            return Result::failure(command.name +
                                   " has no valid Geometry ID");
        }
        RoomMotionState* room = findRoomMotionMutable(roomObjectId);
        if (room == nullptr) {
            // CCinematicThread::{ActiveRoom,DeactiveRoom,
            // RevertRoomsPosition} (0x00371d54, 0x00371d28, 0x00371d80)
            // return zero when CLevel::GetRoomFromID cannot resolve the
            // authored Geometry node.
            return Result::failure(command.name +
                                   " references an unknown room");
        }
        if (command.name == "ActiveRoom") {
            room->active = true;
        } else if (command.name == "DeactiveRoom") {
            room->active = false;
            room->velocity = {};
        } else {
            // CRoom::RevertPosition (0x0036d798) sets a moving room's scene
            // position to literal zero and reinitializes its authored
            // waypoint chain. Non-moving rooms are successful no-ops.
            initializeRoomPath(*room, true);
        }
        return Result::success();
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
    if (command.name == "PlayDAECamera") {
        setColladaMovieUi(true);
        return Result::success();
    }
    if (command.name == "InterfaceControl") {
        bool controlsEnabled = false;
        bool attributionEnabled = false;
        bool objectiveArrowEnabled = false;
        bool blackOverlayEnabled = false;
        bool skipEnabled = false;
        if (!parseBoolean(command, "ControlEnable", controlsEnabled) ||
            !parseBoolean(command, "AttributionEnable",
                          attributionEnabled) ||
            !parseBoolean(command, "ArrowEnable", objectiveArrowEnabled) ||
            !parseBoolean(command, "BlackEnable", blackOverlayEnabled) ||
            !parseBoolean(command, "SkipEnable", skipEnabled)) {
            return Result::failure(
                "InterfaceControl has invalid interface flags");
        }
        // CCinematicThread::InterfaceControlCmd (0x003711e0) writes all five
        // flags in command order. CLevel::Render2DInterface (0x00387a54)
        // gates the complete player/enemy HUD at CLevel+0x2d, the field fed
        // by AttributionEnable, while leaving cinematic subtitles active.
        controlsEnabled_ = controlsEnabled;
        attributionEnabled_ = attributionEnabled;
        objectiveArrowEnabled_ = objectiveArrowEnabled;
        blackOverlayEnabled_ = blackOverlayEnabled;
        skipEnabled_ = skipEnabled;
        return Result::success();
    }
    if (command.name == "ListenerPosition") {
        bool listenerOnMainCharacter = false;
        if (!parseBoolean(command, "ListenerOnMC",
                          listenerOnMainCharacter)) {
            return Result::failure(
                "ListenerPosition has no valid ListenerOnMC flag");
        }
        // CCinematicThread::ListenerPositionCmd (0x0036fe28) copies the
        // authored flag to CLevel+0x30c. CLevel::Update (0x003820bc) then
        // chooses Player position when set, camera position when clear, while
        // retaining the camera look/up vectors in both cases.
        listenerOnMainCharacter_ = listenerOnMainCharacter;
        return Result::success();
    }
    if (command.name == "MustBeVisibleRoom") {
        bool set = false;
        if (!parseBoolean(command, "Set", set)) {
            return Result::failure(
                "MustBeVisibleRoom has no valid Set flag");
        }
        if (!set) {
            forcedVisibleRooms_.fill(false);
            return Result::success();
        }
        const CinematicAttribute* roomList =
            attribute(command, "MustBeVisible");
        std::array<bool, 16> parsedRooms{};
        if (roomList == nullptr || roomList->value.empty() ||
            !parseRoomList(roomList->value, parsedRooms)) {
            return Result::failure(
                "MustBeVisibleRoom has an invalid room list");
        }
        forcedVisibleRooms_ = parsedRooms;
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
    if (command.name == "Save") {
        std::int32_t checkpointId = -1;
        if (!parseInteger(command, "^ID^CheckPoint", checkpointId) ||
            checkpointId < 0) {
            return Result::failure("Save has no valid checkpoint ID");
        }
        // CCinematicThread::SaveCheckpoint (0x00370384) stores the current
        // player/camera state against this authored checkpoint. Persistent
        // profile I/O is outside the single-level Windows target, but the
        // active checkpoint remains explicit runtime state.
        lastCheckpointId_ = checkpointId;
        return Result::success();
    }
    if (command.name == "StartTimer") {
        std::int32_t initialValue = 0;
        if (!parseInteger(command, "InitValue", initialValue) ||
            initialValue < 0) {
            return Result::failure("StartTimer has an invalid InitValue");
        }
        // CCinematicThread::StartTimerOfBossRush (0x0036fe58) reads but does
        // not retain InitValue; it only releases CBossRush's timer gate.
        bossRushTimerRunning_ = true;
        return Result::success();
    }
    if (command.name == "StartProgress") {
        if (thread.type != 1) {
            return Result::failure(
                "StartProgress requires a Basic cinematic thread");
        }
        std::int32_t bossObjectId = -1;
        std::int32_t startWayPointId = -1;
        std::int32_t endWayPointId = -1;
        if (!parseInteger(command, "ID_BOSS", bossObjectId) ||
            !parseInteger(command, "^SID^WayPoint", startWayPointId) ||
            !parseInteger(command, "^EID^WayPoint", endWayPointId)) {
            return Result::failure("StartProgress has invalid object IDs");
        }
        const auto findWayPoint = [this](std::int32_t objectId) {
            return std::find_if(
                waypoints_.begin(), waypoints_.end(),
                [objectId](const LevelWayPointAsset& wayPoint) {
                    return wayPoint.objectId == objectId;
                });
        };
        const auto start = findWayPoint(startWayPointId);
        const auto end = findWayPoint(endWayPointId);
        if (start == waypoints_.end() || end == waypoints_.end()) {
            return Result::failure(
                "StartProgress references an unknown waypoint");
        }

        // CCinematicThread::StartProgress (0x0037145c) resolves the two
        // authored waypoint positions and calls CProgressBar::Init. Init
        // (0x0031a7b0) flattens Z and uses 88 percent of the endpoint distance
        // as the chase-failure separation.
        bossProgress_ = {};
        bossProgress_.bossObjectId = bossObjectId;
        bossProgress_.startWayPointId = startWayPointId;
        bossProgress_.endWayPointId = endWayPointId;
        bossProgress_.startPosition = start->position;
        bossProgress_.endPosition = end->position;
        bossProgress_.startPosition.z = 0.0F;
        bossProgress_.endPosition.z = 0.0F;
        bossProgress_.failureDistance =
            flatDistance(bossProgress_.startPosition,
                         bossProgress_.endPosition) *
            0.88F;
        bossProgress_.visible = true;
        return Result::success();
    }
    if (command.name == "StopProgress") {
        // CCinematicThread::StopProgress (0x0036fdcc) clears CProgressBar's
        // visible and failed bytes, but retains its authored endpoints.
        if (thread.type != 1) {
            return Result::failure(
                "StopProgress requires a Basic cinematic thread");
        }
        bossProgress_.visible = false;
        bossProgress_.failed = false;
        return Result::success();
    }
    if (command.name == "RhinoStop") {
        // CCinematicThread::RhinoStop (0x0036fdf0) sets CProgressBar+0x2d.
        // CProgressBar::Update owns the resulting 0.9 cm/ms contraction.
        if (thread.type != 1) {
            return Result::failure(
                "RhinoStop requires a Basic cinematic thread");
        }
        bossProgress_.closing = true;
        return Result::success();
    }
    if (command.name == "Unlock") {
        const CinematicAttribute* skill = attribute(command, "$SkillID");
        if (skill == nullptr || skill->value.empty() ||
            skill->value.front() < '0' || skill->value.front() > '1') {
            return Result::failure("Unlock has an invalid skill ID");
        }
        // CCinematicThread::OnUnlock (0x0037240c) derives the two-bit profile
        // index from the first character of the authored value.
        unlockedSkills_[static_cast<std::size_t>(skill->value.front() - '0')] =
            true;
        return Result::success();
    }
    if (command.name == "Transport") {
        // CCinematicThread::Transport (0x00371654) accepts only a Player
        // thread and forwards to CLevel::BeginTransport (0x0037f990). The
        // level ignores a second request while its CTransport is active.
        if (thread.type != 3) {
            return Result::failure(
                "Transport requires a Player cinematic thread");
        }
        transportRequested_ = true;
        if (!transport_.visible()) {
            transport_.state = TransportState::Closing;
            transport_.elapsedMilliseconds = 0.0F;
            transport_.scale = 0.0F;
            transportSoundCues_.push_back(TransportSoundCue::In);
        }
        return Result::success();
    }
    if (command.name == "SetSlowMotion") {
        bool enabled = false;
        if (!parseBoolean(command, "Enable", enabled)) {
            return Result::failure("SetSlowMotion has no valid Enable flag");
        }
        if (!enabled) {
            // Application::ResetSlowMotion (0x003e0598).
            if (slowMotionDenominator_ > 1.0F &&
                slowMotionSoundEnabled_) {
                slowMotionSoundCues_.push_back(SlowMotionSoundCue::Exit);
            }
            slowMotionDenominator_ = 1.0F;
            slowMotionElapsedMilliseconds_ = 0.0F;
            slowMotionHoldMilliseconds_ = 0.0F;
            slowMotionRampMilliseconds_ = 0.0F;
            slowMotionSoundEnabled_ = false;
            return Result::success();
        }

        float denominator = 0.0F;
        float holdMilliseconds = 0.0F;
        float rampMilliseconds = 0.0F;
        bool soundEnabled = false;
        const CinematicAttribute* soundAttribute = attribute(command, "isSFX");
        if (!parseFloat(command, "Denominator", denominator) ||
            !parseFloat(command, "TimeOn", holdMilliseconds) ||
            !parseFloat(command, "TimeOnToEnd", rampMilliseconds) ||
            (soundAttribute != nullptr &&
             !parseBoolean(command, "isSFX", soundEnabled)) ||
            holdMilliseconds < 0.0F || rampMilliseconds < 0.0F) {
            return Result::failure("SetSlowMotion has invalid timing attributes");
        }
        // CCinematicThread::SetSlowMotion (0x00371a88) passes force=true, so
        // the probabilistic player-combat gate is intentionally bypassed.
        setSlowMotion(denominator, holdMilliseconds, rampMilliseconds,
                      soundEnabled);
        return Result::success();
    }
    if (command.name == "ShakeCamera") {
        float maximumOffset = 0.0F;
        std::int32_t frameCount = 0;
        float xRate = 0.0F;
        float yRate = 0.0F;
        float zRate = 0.0F;
        if (!parseFloat(command, "MaxOff", maximumOffset) ||
            !parseInteger(command, "ShakeFrame", frameCount) ||
            !parseFloat(command, "XRate", xRate) ||
            !parseFloat(command, "YRate", yRate) ||
            !parseFloat(command, "ZRate", zRate)) {
            return Result::failure("ShakeCamera has invalid attributes");
        }
        // CGameCamera::StartShake (0x002f2098) ignores non-positive counts.
        startCameraShake(maximumOffset, frameCount, {xRate, yRate, zRate});
        return Result::success();
    }
    if (command.name == "StopShakeCamera") {
        // CGameCamera::StopShake (0x002f20c4).
        stopCameraShake();
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

const LevelWayPointAsset* LevelCinematicRuntime::findWayPoint(
    std::int32_t objectId) const noexcept {
    const auto match = std::find_if(
        waypoints_.begin(), waypoints_.end(),
        [objectId](const LevelWayPointAsset& wayPoint) {
            return wayPoint.objectId == objectId;
        });
    return match == waypoints_.end() ? nullptr : &*match;
}

RoomMotionState* LevelCinematicRuntime::findRoomMotionMutable(
    std::int32_t objectId) noexcept {
    const auto match = std::find_if(
        roomMotionStates_.begin(), roomMotionStates_.end(),
        [objectId](const RoomMotionState& room) {
            return room.objectId == objectId;
        });
    return match == roomMotionStates_.end() ? nullptr : &*match;
}

const RoomMotionState* LevelCinematicRuntime::findRoomMotion(
    std::int32_t objectId) const noexcept {
    const auto match = std::find_if(
        roomMotionStates_.begin(), roomMotionStates_.end(),
        [objectId](const RoomMotionState& room) {
            return room.objectId == objectId;
        });
    return match == roomMotionStates_.end() ? nullptr : &*match;
}

void LevelCinematicRuntime::initializeRoomPath(
    RoomMotionState& room, bool revertPosition) noexcept {
    room.velocity = {};
    if (!room.movingRoom) {
        return;
    }
    if (revertPosition) {
        // CRoom::RevertPosition (0x0036d798) supplies a literal zero vector
        // to CRoom::SetPosition rather than restoring a cached scene value.
        room.position = {};
    }
    const LevelWayPointAsset* first = findWayPoint(room.linkedWaypointId);
    if (first == nullptr) {
        room.pathOriginOffset = {};
        room.previousReferencePosition = room.position;
        room.targetWaypointId = -1;
        return;
    }
    room.pathOriginOffset = first->position;
    room.previousReferencePosition =
        add(room.position, room.pathOriginOffset);
    room.targetWaypointId = first->nextWaypointIds[0];
}

void LevelCinematicRuntime::advanceRoomMotion(
    std::uint32_t elapsedMilliseconds) noexcept {
    constexpr float kArrivalEpsilon = 1.0e-6F;
    for (RoomMotionState& room : roomMotionStates_) {
        room.velocity = {};
        if (!room.active || !room.movingRoom ||
            room.targetWaypointId < 1 || elapsedMilliseconds == 0) {
            continue;
        }

        const LevelWayPointAsset* target =
            findWayPoint(room.targetWaypointId);
        if (target == nullptr) {
            room.targetWaypointId = -1;
            continue;
        }
        assets::Vector3 reference = add(room.position,
                                       room.pathOriginOffset);
        const assets::Vector3 fromPrevious =
            subtract(reference, room.previousReferencePosition);
        const assets::Vector3 targetFromPrevious =
            subtract(target->position, room.previousReferencePosition);
        const assets::Vector3 targetFromReference =
            subtract(target->position, reference);
        const bool atTarget =
            std::abs(targetFromReference.x) <= kArrivalEpsilon &&
            std::abs(targetFromReference.y) <= kArrivalEpsilon &&
            std::abs(targetFromReference.z) <= kArrivalEpsilon;
        const float targetDistanceFromPreviousSquared =
            lengthSquared(targetFromPrevious);
        const bool reachedOrPassed =
            atTarget ||
            targetDistanceFromPreviousSquared <
                lengthSquared(fromPrevious) ||
            targetDistanceFromPreviousSquared <
                lengthSquared(targetFromReference);

        bool advancedTarget = false;
        assets::Vector3 oldTargetPosition{};
        if (reachedOrPassed) {
            oldTargetPosition = target->position;
            room.targetWaypointId = target->nextWaypointIds[0];
            if (room.targetWaypointId < 1) {
                continue;
            }
            target = findWayPoint(room.targetWaypointId);
            if (target == nullptr) {
                room.targetWaypointId = -1;
                continue;
            }
            advancedTarget = true;
        }

        assets::Vector3 direction =
            normalized(subtract(target->position, reference));
        if (advancedTarget) {
            const assets::Vector3 previousDirection =
                normalized(subtract(reference,
                                    room.previousReferencePosition));
            if (dot(direction, previousDirection) <= 0.0F) {
                // This is the path-corner carry branch at 0x0036dac4: retain
                // the overshoot relative to the old target, move it around
                // the new corner, then advance to the following waypoint.
                reference = add(target->position,
                                subtract(reference, oldTargetPosition));
                room.position = subtract(reference,
                                         room.pathOriginOffset);
                room.targetWaypointId = target->nextWaypointIds[0];
                if (room.targetWaypointId < 1) {
                    continue;
                }
                target = findWayPoint(room.targetWaypointId);
                if (target == nullptr) {
                    room.targetWaypointId = -1;
                    continue;
                }
                direction = normalized(
                    subtract(target->position, reference));
            }
        }

        const float distance =
            room.lineSpeedCentimetersPerMillisecond *
            static_cast<float>(elapsedMilliseconds);
        room.previousReferencePosition = reference;
        room.position = {
            reference.x + direction.x * distance - room.pathOriginOffset.x,
            reference.y + direction.y * distance - room.pathOriginOffset.y,
            reference.z + direction.z * distance - room.pathOriginOffset.z};
        // CRoom::Move writes speed * direction * 1000 to its physics body at
        // 0x0036db78. Preserve centimetres-per-second for collision/carriage
        // consumers even though the scene position advances in milliseconds.
        room.velocity = {
            room.lineSpeedCentimetersPerMillisecond * direction.x * 1000.0F,
            room.lineSpeedCentimetersPerMillisecond * direction.y * 1000.0F,
            room.lineSpeedCentimetersPerMillisecond * direction.z * 1000.0F};
    }
}

void LevelCinematicRuntime::advanceTransport(
    std::uint32_t elapsedMilliseconds) noexcept {
    const float delta = static_cast<float>(elapsedMilliseconds);
    switch (transport_.state) {
    case TransportState::Closing:
        transport_.scale = yOnQuadraticBezier(
            0.0F, 50.0F, 450.0F, 1.0F, 900.0F, 0.0F,
            transport_.elapsedMilliseconds);
        transport_.elapsedMilliseconds += delta;
        if (transport_.elapsedMilliseconds > 900.0F) {
            transport_.state = TransportState::Covered;
        }
        break;
    case TransportState::Covered:
        transport_.scale = 0.0F;
        transport_.elapsedMilliseconds += delta;
        if (transport_.elapsedMilliseconds > 1000.0F) {
            transport_.state = TransportState::Opening;
            transportSoundCues_.push_back(TransportSoundCue::Out);
        }
        break;
    case TransportState::Opening:
        transport_.scale = yOnQuadraticBezier(
            0.0F, 0.0F, 765.0F, 1.0F, 900.0F, 50.0F,
            transport_.elapsedMilliseconds - 1000.0F);
        transport_.elapsedMilliseconds += delta;
        if (transport_.elapsedMilliseconds > 1900.0F) {
            transport_.state = TransportState::Inactive;
        }
        break;
    case TransportState::Inactive:
        break;
    }
}

void LevelCinematicRuntime::completeColladaPlayback(
    bool levelEnd, bool gameEnd) noexcept {
    setColladaMovieUi(false);
    if (levelEnd) {
        levelEnded_ = true;
        goToNextLevel_ = true;
    }
    if (gameEnd) {
        gameEnded_ = true;
    }
}

void LevelCinematicRuntime::setColladaMovieUi(bool active) noexcept {
    // OnOffDaeMovieUI writes CLevel+0x2d (HUD), +0x30 (bands), +0x31
    // (skip) and EnableControls(!active, false), but not the arrow flag.
    controlsEnabled_ = !active;
    attributionEnabled_ = !active;
    blackOverlayEnabled_ = active;
    skipEnabled_ = active;
}

void LevelCinematicRuntime::endQuickTimeEvent() noexcept {
    // CQTEManager::EndQTE (0x0038a5b0) calls
    // CLevel::EnableControls(true, false) after the QTE feedback state ends.
    // That path does not rewrite the independently authored presentation
    // flags from InterfaceControl.
    controlsEnabled_ = true;
    // EndQTE also calls Application::ResetSlowMotion (0x003e0598).
    if (slowMotionDenominator_ > 1.0F && slowMotionSoundEnabled_) {
        slowMotionSoundCues_.push_back(SlowMotionSoundCue::Exit);
    }
    slowMotionDenominator_ = 1.0F;
    slowMotionElapsedMilliseconds_ = 0.0F;
    slowMotionHoldMilliseconds_ = 0.0F;
    slowMotionRampMilliseconds_ = 0.0F;
    slowMotionSoundEnabled_ = false;
}

void LevelCinematicRuntime::advanceBossProgress(
    std::uint32_t elapsedMilliseconds,
    const assets::Vector3& playerPosition,
    const assets::Vector3* bossPosition) noexcept {
    if (!bossProgress_.visible || bossProgress_.failed ||
        bossPosition == nullptr) {
        return;
    }

    bossProgress_.currentDistance =
        flatDistance(playerPosition, *bossPosition);
    if (bossProgress_.closing) {
        bossProgress_.failureDistance -=
            static_cast<float>(elapsedMilliseconds) * 0.9F;
    }
    // CProgressBar::Update (0x0031a858) completes when the flattened
    // player/boss separation exceeds the live failure distance. Equality is
    // retained as non-failure by the native != guard.
    if (bossProgress_.currentDistance != bossProgress_.failureDistance &&
        bossProgress_.currentDistance > bossProgress_.failureDistance) {
        bossProgress_.failed = true;
    }
}

float LevelCinematicRuntime::bossProgressRatio() const noexcept {
    if (bossProgress_.failureDistance <= 0.0F) {
        return bossProgress_.currentDistance > 0.0F ? 1.0F : 0.0F;
    }
    return std::clamp(bossProgress_.currentDistance /
                          bossProgress_.failureDistance,
                      0.0F, 1.0F);
}

float LevelCinematicRuntime::updateSlowMotion(
    float realDeltaMilliseconds) noexcept {
    if (slowMotionDenominator_ == 1.0F) {
        return realDeltaMilliseconds;
    }
    if (slowMotionDenominator_ < 1.0F) {
        return realDeltaMilliseconds / slowMotionDenominator_;
    }
    if (slowMotionElapsedMilliseconds_ < slowMotionHoldMilliseconds_) {
        slowMotionElapsedMilliseconds_ += realDeltaMilliseconds;
        return realDeltaMilliseconds / slowMotionDenominator_;
    }
    if (slowMotionElapsedMilliseconds_ >=
        slowMotionHoldMilliseconds_ + slowMotionRampMilliseconds_) {
        if (slowMotionSoundEnabled_) {
            slowMotionSoundCues_.push_back(SlowMotionSoundCue::Exit);
        }
        slowMotionDenominator_ = 1.0F;
        // The original returns 1.0 rather than the incoming delta on this
        // transition frame (0x003e066c-0x003e0684).
        return 1.0F;
    }

    slowMotionElapsedMilliseconds_ += realDeltaMilliseconds;
    const float rampProgress =
        (slowMotionElapsedMilliseconds_ - slowMotionHoldMilliseconds_) /
        slowMotionRampMilliseconds_;
    const float effectiveDenominator =
        slowMotionDenominator_ +
        rampProgress * (1.0F - slowMotionDenominator_);
    return realDeltaMilliseconds / effectiveDenominator;
}

void LevelCinematicRuntime::setSlowMotion(
    float denominator, float holdMilliseconds, float rampMilliseconds,
    bool soundEnabled) noexcept {
    slowMotionDenominator_ = std::max(denominator, 1.0F);
    slowMotionElapsedMilliseconds_ = 0.0F;
    slowMotionHoldMilliseconds_ = std::max(holdMilliseconds, 0.0F);
    slowMotionRampMilliseconds_ = std::max(rampMilliseconds, 0.0F);
    slowMotionSoundEnabled_ = soundEnabled;
    if (soundEnabled) {
        slowMotionSoundCues_.push_back(SlowMotionSoundCue::Enter);
    }
}

void LevelCinematicRuntime::resetSlowMotion() noexcept {
    if (slowMotionDenominator_ > 1.0F && slowMotionSoundEnabled_) {
        slowMotionSoundCues_.push_back(SlowMotionSoundCue::Exit);
    }
    slowMotionDenominator_ = 1.0F;
    slowMotionElapsedMilliseconds_ = 0.0F;
    slowMotionHoldMilliseconds_ = 0.0F;
    slowMotionRampMilliseconds_ = 0.0F;
    slowMotionSoundEnabled_ = false;
}

void LevelCinematicRuntime::startCameraShake(
    float maximumOffset, std::int32_t frameCount,
    const assets::Vector3& rates) noexcept {
    if (frameCount <= 0) {
        return;
    }
    cameraShakeMaximumOffset_ = maximumOffset;
    cameraShakeFramesRemaining_ = cameraShakeTotalFrames_ = frameCount;
    cameraShakeXRate_ = rates.x;
    cameraShakeYRate_ = rates.y;
    cameraShakeZRate_ = rates.z;
    cameraShakeSign_ = 1;
    cameraShakeTickRemainderMilliseconds_ = 0;
    cameraShakeOffset_ = {};
}

void LevelCinematicRuntime::stopCameraShake() noexcept {
    cameraShakeFramesRemaining_ = 0;
    cameraShakeTickRemainderMilliseconds_ = 0;
    cameraShakeOffset_ = {};
}

void LevelCinematicRuntime::advanceCameraShake(
    std::uint32_t deltaMilliseconds) noexcept {
    constexpr std::uint32_t kOriginalGameTickMilliseconds = 50;
    cameraShakeTickRemainderMilliseconds_ += deltaMilliseconds;
    while (cameraShakeTickRemainderMilliseconds_ >=
           kOriginalGameTickMilliseconds) {
        cameraShakeTickRemainderMilliseconds_ -=
            kOriginalGameTickMilliseconds;
        if (cameraShakeFramesRemaining_ <= 0 ||
            cameraShakeTotalFrames_ <= 0) {
            cameraShakeOffset_ = {};
            continue;
        }
        float offset = cameraShakeMaximumOffset_;
        if (cameraShakeSign_ == 1) {
            offset = -offset;
        }
        offset *= static_cast<float>(cameraShakeFramesRemaining_) /
                  static_cast<float>(cameraShakeTotalFrames_);
        --cameraShakeFramesRemaining_;
        cameraShakeOffset_ = {offset * cameraShakeXRate_,
                              offset * cameraShakeYRate_,
                              offset * cameraShakeZRate_};
        cameraShakeSign_ = -cameraShakeSign_;
    }
}

CameraPose LevelCinematicRuntime::applyCameraShake(
    CameraPose pose) const noexcept {
    pose.position.x += cameraShakeOffset_.x;
    pose.position.y += cameraShakeOffset_.y;
    pose.position.z += cameraShakeOffset_.z;
    return pose;
}

void LevelCinematicRuntime::resetTransientForCheckPointLoad() noexcept {
    // CLevel::ResetLevel (0x003832d0) calls Application::ResetSlowMotion,
    // CCinematicManager::Reset and CBlackScreen::Reset before checkpoint
    // objects are loaded. Keep persistent command state from the save, but
    // discard effects owned by the interrupted death-time frame.
    cinematicStartRequests_.clear();
    slowMotionSoundCues_.clear();
    transportSoundCues_.clear();
    slowMotionDenominator_ = 1.0F;
    slowMotionElapsedMilliseconds_ = 0.0F;
    slowMotionHoldMilliseconds_ = 0.0F;
    slowMotionRampMilliseconds_ = 0.0F;
    slowMotionSoundEnabled_ = false;
    cameraShakeMaximumOffset_ = 0.0F;
    cameraShakeFramesRemaining_ = 0;
    cameraShakeTotalFrames_ = 0;
    cameraShakeXRate_ = 0.0F;
    cameraShakeYRate_ = 0.0F;
    cameraShakeZRate_ = 0.0F;
    cameraShakeSign_ = 1;
    cameraShakeTickRemainderMilliseconds_ = 0;
    cameraShakeOffset_ = {};
    transport_ = {};
    forcedVisibleRooms_.fill(false);
    controlsEnabled_ = true;
    blackOverlayEnabled_ = false;
    skipEnabled_ = false;
    listenerOnMainCharacter_ = true;
    bossProgress_ = {};
}

std::vector<std::int32_t>
LevelCinematicRuntime::consumeCinematicStartRequests() {
    std::vector<std::int32_t> requests;
    requests.swap(cinematicStartRequests_);
    return requests;
}

std::vector<SlowMotionSoundCue>
LevelCinematicRuntime::consumeSlowMotionSoundCues() {
    std::vector<SlowMotionSoundCue> cues;
    cues.swap(slowMotionSoundCues_);
    return cues;
}

std::vector<TransportSoundCue>
LevelCinematicRuntime::consumeTransportSoundCues() {
    std::vector<TransportSoundCue> cues;
    cues.swap(transportSoundCues_);
    return cues;
}

} // namespace usm::game
