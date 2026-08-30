#include "game/LevelCinematicRuntime.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
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

} // namespace

void LevelCinematicRuntime::bind(LevelTriggerRuntime& triggers,
                                 GameplayCamera& camera,
                                 std::span<const LevelWayPointAsset>
                                     waypoints) noexcept {
    triggers_ = &triggers;
    camera_ = &camera;
    waypoints_ = waypoints;
    cinematicStartRequests_.clear();
    slowMotionSoundCues_.clear();
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
    levelEnded_ = false;
    goToNextLevel_ = false;
    gameEnded_ = false;
    controlsEnabled_ = true;
    blackOverlayEnabled_ = false;
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
    if (command.name == "InterfaceControl") {
        bool controlsEnabled = false;
        bool blackOverlayEnabled = false;
        if (!parseBoolean(command, "ControlEnable", controlsEnabled) ||
            !parseBoolean(command, "BlackEnable", blackOverlayEnabled)) {
            return Result::failure(
                "InterfaceControl has invalid control or black flags");
        }
        controlsEnabled_ = controlsEnabled;
        blackOverlayEnabled_ = blackOverlayEnabled;
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
        slowMotionDenominator_ = std::max(denominator, 1.0F);
        slowMotionElapsedMilliseconds_ = 0.0F;
        slowMotionHoldMilliseconds_ = holdMilliseconds;
        slowMotionRampMilliseconds_ = rampMilliseconds;
        slowMotionSoundEnabled_ = soundEnabled;
        if (soundEnabled) {
            slowMotionSoundCues_.push_back(SlowMotionSoundCue::Enter);
        }
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
        if (frameCount > 0) {
            cameraShakeMaximumOffset_ = maximumOffset;
            cameraShakeFramesRemaining_ = frameCount;
            cameraShakeTotalFrames_ = frameCount;
            cameraShakeXRate_ = xRate;
            cameraShakeYRate_ = yRate;
            cameraShakeZRate_ = zRate;
            cameraShakeSign_ = 1;
            cameraShakeTickRemainderMilliseconds_ = 0;
            cameraShakeOffset_ = {};
        }
        return Result::success();
    }
    if (command.name == "StopShakeCamera") {
        // CGameCamera::StopShake (0x002f20c4).
        cameraShakeFramesRemaining_ = 0;
        cameraShakeTickRemainderMilliseconds_ = 0;
        cameraShakeOffset_ = {};
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

} // namespace usm::game
