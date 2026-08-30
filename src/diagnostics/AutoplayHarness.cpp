#include "diagnostics/AutoplayHarness.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace usm::diagnostics {
namespace {

float distance2D(const assets::Vector3& left,
                 const assets::Vector3& right) noexcept {
    const float x = left.x - right.x;
    const float y = left.y - right.y;
    return std::sqrt(x * x + y * y);
}

void writeU16(std::ofstream& stream, std::uint16_t value) {
    const char bytes[2]{static_cast<char>(value & 0xffU),
                        static_cast<char>((value >> 8U) & 0xffU)};
    stream.write(bytes, sizeof(bytes));
}

void writeU32(std::ofstream& stream, std::uint32_t value) {
    const char bytes[4]{static_cast<char>(value & 0xffU),
                        static_cast<char>((value >> 8U) & 0xffU),
                        static_cast<char>((value >> 16U) & 0xffU),
                        static_cast<char>((value >> 24U) & 0xffU)};
    stream.write(bytes, sizeof(bytes));
}

std::string sanitizeLabel(std::string_view label) {
    std::string result;
    result.reserve(label.size());
    for (const char character : label) {
        const bool valid = (character >= 'a' && character <= 'z') ||
                           (character >= 'A' && character <= 'Z') ||
                           (character >= '0' && character <= '9') ||
                           character == '-' || character == '_';
        result.push_back(valid ? character : '-');
    }
    return result.empty() ? "frame" : result;
}

std::string visibleRoomList(std::span<const bool> rooms) {
    std::string result;
    for (std::size_t index = 0; index < rooms.size(); ++index) {
        if (!rooms[index]) {
            continue;
        }
        if (!result.empty()) {
            result += '|';
        }
        result += std::to_string(index + 1);
    }
    return result;
}

} // namespace

Result AutoplayHarness::initialize(const std::filesystem::path& scriptPath,
                                   const std::filesystem::path& outputPath) {
    *this = {};
    outputPath_ = outputPath;
    std::error_code error;
    std::filesystem::create_directories(outputPath_, error);
    if (error) {
        return Result::failure("Could not create autoplay output directory: " +
                               error.message());
    }
    Result result = parseScript(scriptPath);
    if (!result) {
        return result;
    }
    frameLog_.open(outputPath_ / "frames.csv", std::ios::trunc);
    enemyLog_.open(outputPath_ / "enemies.csv", std::ios::trunc);
    eventLog_.open(outputPath_ / "events.csv", std::ios::trunc);
    if (!frameLog_ || !enemyLog_ || !eventLog_) {
        return Result::failure("Could not create autoplay trace files");
    }
    frameLog_ << "frame,real_ms,game_ms,phase,controls,player_x,player_y,"
                 "player_z,facing_x,facing_y,facing_z,health,animation,"
                 "animation_ms,camera_area,cinematic,qte,restore,"
                 "restore_alpha,visible_rooms,input_right,input_forward,"
                 "camera_x,camera_y,camera_z,target_x,target_y,target_z\n";
    enemyLog_ << "frame,real_ms,object_id,type_id,x,y,z,health,visible,ai,"
                 "detected,behavior,animation,animation_ms,animation_speed,"
                 "animation_loop,animation_reverse\n";
    eventLog_ << "real_ms,frame,type,detail\n";
    recordEvent(0, "harness_start",
                "script=" + scriptPath.generic_string());
    return Result::success();
}

Result AutoplayHarness::parseScript(
    const std::filesystem::path& scriptPath) {
    std::ifstream stream(scriptPath);
    if (!stream) {
        return Result::failure("Could not open autoplay script: " +
                               scriptPath.string());
    }
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(stream, line)) {
        ++lineNumber;
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        std::istringstream tokens(line);
        std::string command;
        if (!(tokens >> command)) {
            continue;
        }
        const auto invalid = [lineNumber](std::string_view reason) {
            return Result::failure("Autoplay script line " +
                                   std::to_string(lineNumber) + ": " +
                                   std::string(reason));
        };
        if (command == "fixed_step_ms") {
            if (!(tokens >> fixedStepMilliseconds_) ||
                fixedStepMilliseconds_ == 0 ||
                fixedStepMilliseconds_ > 100) {
                return invalid("fixed_step_ms must be in [1, 100]");
            }
            continue;
        }
        if (command == "sample_interval_ms") {
            if (!(tokens >> sampleIntervalMilliseconds_) ||
                sampleIntervalMilliseconds_ == 0) {
                return invalid("sample_interval_ms must be positive");
            }
            continue;
        }
        if (command == "capture_interval_ms") {
            if (!(tokens >> captureIntervalMilliseconds_)) {
                return invalid("capture_interval_ms requires a value");
            }
            continue;
        }
        if (command == "max_time_ms") {
            if (!(tokens >> maximumTimeMilliseconds_) ||
                maximumTimeMilliseconds_ == 0) {
                return invalid("max_time_ms must be positive");
            }
            continue;
        }
        if (command == "start_time_ms") {
            if (!(tokens >> startTimeMilliseconds_)) {
                return invalid("start_time_ms requires a value");
            }
            continue;
        }
        if (command == "render_size") {
            if (!(tokens >> renderWidth_ >> renderHeight_) ||
                renderWidth_ == 0 || renderHeight_ == 0) {
                return invalid("render_size requires positive width/height");
            }
            continue;
        }

        Step step;
        step.sourceLine = lineNumber;
        if (command == "wait_gameplay") {
            step.kind = StepKind::WaitGameplay;
            if (!(tokens >> step.durationOrTimeoutMilliseconds)) {
                return invalid("wait_gameplay requires a timeout");
            }
        } else if (command == "wait") {
            step.kind = StepKind::Wait;
            if (!(tokens >> step.durationOrTimeoutMilliseconds)) {
                return invalid("wait requires a duration");
            }
        } else if (command == "move_to") {
            step.kind = StepKind::MoveTo;
            if (!(tokens >> step.position.x >> step.position.y >>
                  step.position.z >> step.radius >>
                  step.durationOrTimeoutMilliseconds) ||
                step.radius <= 0.0F) {
                return invalid("move_to requires x y z radius timeout");
            }
        } else if (command == "attack") {
            step.kind = StepKind::Attack;
            if (!(tokens >> step.durationOrTimeoutMilliseconds >>
                  step.radius) ||
                step.radius <= 0.0F) {
                return invalid("attack requires timeout preferred_distance IDs");
            }
            std::int32_t objectId = -1;
            while (tokens >> objectId) {
                step.objectIds.push_back(objectId);
            }
            if (step.objectIds.empty()) {
                return invalid("attack requires at least one enemy ID");
            }
        } else if (command == "jump") {
            step.kind = StepKind::Jump;
        } else if (command == "web_on") {
            step.kind = StepKind::WebOn;
        } else if (command == "web_off") {
            step.kind = StepKind::WebOff;
        } else if (command == "teleport") {
            step.kind = StepKind::Teleport;
            if (!(tokens >> step.position.x >> step.position.y >>
                  step.position.z >> step.facing.x >> step.facing.y >>
                  step.facing.z)) {
                return invalid("teleport requires x y z facing_x facing_y facing_z");
            }
        } else if (command == "capture") {
            step.kind = StepKind::Capture;
            if (!(tokens >> step.label)) {
                return invalid("capture requires a label");
            }
        } else if (command == "assert_near") {
            step.kind = StepKind::AssertNear;
            if (!(tokens >> step.position.x >> step.position.y >>
                  step.position.z >> step.radius) ||
                step.radius <= 0.0F) {
                return invalid("assert_near requires x y z radius");
            }
        } else if (command == "assert_health_above") {
            step.kind = StepKind::AssertHealthAbove;
            if (!(tokens >> step.value)) {
                return invalid("assert_health_above requires a value");
            }
        } else if (command == "finish") {
            step.kind = StepKind::Finish;
        } else {
            return invalid("unknown command " + command);
        }
        std::string trailing;
        if (tokens >> trailing) {
            return invalid("unexpected trailing token " + trailing);
        }
        steps_.push_back(std::move(step));
    }
    if (steps_.empty() || steps_.back().kind != StepKind::Finish) {
        return Result::failure("Autoplay script must end with finish");
    }
    if (startTimeMilliseconds_ >= maximumTimeMilliseconds_) {
        return Result::failure(
            "Autoplay start_time_ms must be less than max_time_ms");
    }
    return Result::success();
}

AutoplayFrameInput AutoplayHarness::update(
    const AutoplaySnapshot& snapshot) {
    lastTimeMilliseconds_ = snapshot.realTimeMilliseconds;
    lastFrameIndex_ = snapshot.frameIndex;
    AutoplayFrameInput input;
    input.quickTimeEventPressed = snapshot.quickTimeEventActive;
    if (complete_ || failed_) {
        lastMotionInput_ = input.motion;
        return input;
    }
    if (snapshot.realTimeMilliseconds > maximumTimeMilliseconds_) {
        failed_ = true;
        failureMessage_ = "autoplay exceeded max_time_ms";
        recordEvent(snapshot.realTimeMilliseconds, "harness_failure",
                    failureMessage_);
        lastMotionInput_ = input.motion;
        return input;
    }
    while (activeStepIndex_ < steps_.size() && !complete_ && !failed_) {
        const Step& step = steps_[activeStepIndex_];
        if (!activeStepStartMilliseconds_) {
            beginStep(snapshot, step);
        }
        input = updateActiveStep(snapshot, step);
        input.quickTimeEventPressed = snapshot.quickTimeEventActive;
        const bool immediate = step.kind == StepKind::Jump ||
                               step.kind == StepKind::WebOn ||
                               step.kind == StepKind::WebOff ||
                               step.kind == StepKind::Teleport ||
                               step.kind == StepKind::Capture;
        if (!immediate || complete_ || failed_) {
            break;
        }
        // Immediate steps must return their one-frame action before advancing
        // into another step, otherwise an edge-triggered input can disappear.
        break;
    }
    lastMotionInput_ = input.motion;
    return input;
}

AutoplayFrameInput AutoplayHarness::updateActiveStep(
    const AutoplaySnapshot& snapshot, const Step& step) {
    AutoplayFrameInput input;
    const std::uint64_t elapsed = snapshot.realTimeMilliseconds -
                                  *activeStepStartMilliseconds_;
    const auto timedOut = [&] {
        return step.durationOrTimeoutMilliseconds != 0 &&
               elapsed > step.durationOrTimeoutMilliseconds;
    };
    switch (step.kind) {
    case StepKind::WaitGameplay:
        if (snapshot.gameplayActive) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step, "gameplay did not start before timeout");
        }
        break;
    case StepKind::Wait:
        if (elapsed >= step.durationOrTimeoutMilliseconds) {
            completeStep(snapshot, step);
        }
        break;
    case StepKind::MoveTo:
        if (!snapshot.gameplayActive) {
            if (timedOut()) {
                failStep(snapshot, step, "move_to timed out before gameplay");
            }
            break;
        }
        if (distance2D(snapshot.playerPosition, step.position) <= step.radius) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step, "move_to did not reach its target");
        } else if (snapshot.controlsEnabled) {
            input.motion = steerToward(snapshot, step.position);
        }
        break;
    case StepKind::Attack: {
        const game::LevelEnemyState* target = nullptr;
        float targetDistance = std::numeric_limits<float>::max();
        bool anyAlive = false;
        for (const std::int32_t objectId : step.objectIds) {
            const auto match = std::find_if(
                snapshot.enemies.begin(), snapshot.enemies.end(),
                [objectId](const game::LevelEnemyState& enemy) {
                    return enemy.asset != nullptr &&
                           enemy.asset->objectId == objectId;
                });
            if (match == snapshot.enemies.end() || match->health <= 0.0F) {
                continue;
            }
            anyAlive = true;
            const float candidateDistance =
                distance2D(snapshot.playerPosition, match->position);
            if (candidateDistance < targetDistance) {
                target = &*match;
                targetDistance = candidateDistance;
            }
        }
        if (!anyAlive) {
            completeStep(snapshot, step);
        } else if (timedOut()) {
            failStep(snapshot, step, "attack targets remained alive");
        } else if (target != nullptr && snapshot.controlsEnabled) {
            input.motion = steerToward(snapshot, target->position);
            if (targetDistance <= step.radius) {
                const float toTargetX = target->position.x -
                                        snapshot.playerPosition.x;
                const float toTargetY = target->position.y -
                                        snapshot.playerPosition.y;
                const float length =
                    std::sqrt(toTargetX * toTargetX + toTargetY * toTargetY);
                const float facingDot = length > 0.001F
                    ? (snapshot.playerFacing.x * toTargetX +
                       snapshot.playerFacing.y * toTargetY) / length
                    : 1.0F;
                if (facingDot >= 0.92F) {
                    input.motion = {};
                    input.punchPressed = true;
                } else {
                    input.motion.right *= 0.25F;
                    input.motion.forward *= 0.25F;
                }
            }
        }
        break;
    }
    case StepKind::Jump:
        input.jumpPressed = true;
        completeStep(snapshot, step);
        break;
    case StepKind::WebOn:
        input.webPressed = true;
        completeStep(snapshot, step);
        break;
    case StepKind::WebOff:
        input.webReleased = true;
        completeStep(snapshot, step);
        break;
    case StepKind::Teleport:
        input.teleport = AutoplayTeleport{step.position, step.facing};
        completeStep(snapshot, step);
        break;
    case StepKind::Capture:
        input.captureLabels.push_back(step.label);
        completeStep(snapshot, step);
        break;
    case StepKind::AssertNear:
        if (distance2D(snapshot.playerPosition, step.position) > step.radius ||
            std::abs(snapshot.playerPosition.z - step.position.z) >
                step.radius) {
            failStep(snapshot, step, "player is outside assert_near radius");
        } else {
            completeStep(snapshot, step);
        }
        break;
    case StepKind::AssertHealthAbove:
        if (snapshot.playerHealth <= step.value) {
            failStep(snapshot, step,
                     "player health did not satisfy assert_health_above");
        } else {
            completeStep(snapshot, step);
        }
        break;
    case StepKind::Finish:
        completeStep(snapshot, step);
        complete_ = true;
        recordEvent(snapshot.realTimeMilliseconds, "harness_complete", "ok");
        break;
    }
    return input;
}

void AutoplayHarness::beginStep(const AutoplaySnapshot& snapshot,
                                const Step& step) {
    activeStepStartMilliseconds_ = snapshot.realTimeMilliseconds;
    recordEvent(snapshot.realTimeMilliseconds, "step_begin",
                std::to_string(activeStepIndex_) + ":" +
                    stepName(step.kind) + ":line=" +
                    std::to_string(step.sourceLine));
}

void AutoplayHarness::completeStep(const AutoplaySnapshot& snapshot,
                                   const Step& step) {
    recordEvent(snapshot.realTimeMilliseconds, "step_complete",
                std::to_string(activeStepIndex_) + ":" +
                    stepName(step.kind));
    ++activeStepIndex_;
    activeStepStartMilliseconds_.reset();
}

void AutoplayHarness::failStep(const AutoplaySnapshot& snapshot,
                               const Step& step, std::string message) {
    failed_ = true;
    failureMessage_ = "step " + std::to_string(activeStepIndex_) + " (" +
                      stepName(step.kind) + ", line " +
                      std::to_string(step.sourceLine) + "): " + message;
    recordEvent(snapshot.realTimeMilliseconds, "harness_failure",
                failureMessage_);
}

game::PlayerMotionInput AutoplayHarness::steerToward(
    const AutoplaySnapshot& snapshot,
    const assets::Vector3& target) const noexcept {
    float desiredX = target.x - snapshot.playerPosition.x;
    float desiredY = target.y - snapshot.playerPosition.y;
    const float desiredLength =
        std::sqrt(desiredX * desiredX + desiredY * desiredY);
    if (desiredLength <= 0.001F) {
        return {};
    }
    desiredX /= desiredLength;
    desiredY /= desiredLength;
    float forwardX = snapshot.camera.target.x - snapshot.camera.position.x;
    float forwardY = snapshot.camera.target.y - snapshot.camera.position.y;
    const float forwardLength =
        std::sqrt(forwardX * forwardX + forwardY * forwardY);
    if (forwardLength <= 0.001F) {
        forwardX = snapshot.playerFacing.x;
        forwardY = snapshot.playerFacing.y;
    } else {
        forwardX /= forwardLength;
        forwardY /= forwardLength;
    }
    const float rightX = forwardY;
    const float rightY = -forwardX;
    return {desiredX * rightX + desiredY * rightY,
            desiredX * forwardX + desiredY * forwardY};
}

void AutoplayHarness::recordFrame(const AutoplaySnapshot& snapshot) {
    lastTimeMilliseconds_ = snapshot.realTimeMilliseconds;
    lastFrameIndex_ = snapshot.frameIndex;
    const auto transition = [this, &snapshot](std::string_view type,
                                               std::string detail) {
        recordEvent(snapshot.realTimeMilliseconds, type, detail);
    };
    if (snapshot.gameplayActive != previousGameplayActive_) {
        transition("phase", snapshot.gameplayActive ? "gameplay" : "intro");
        previousGameplayActive_ = snapshot.gameplayActive;
    }
    if (snapshot.playerAnimation != previousPlayerAnimation_) {
        transition("player_animation",
                   previousPlayerAnimation_ + "->" +
                       std::string(snapshot.playerAnimation));
        previousPlayerAnimation_ = snapshot.playerAnimation;
    }
    if (snapshot.playerHealth != previousPlayerHealth_) {
        transition("player_health",
                   std::to_string(previousPlayerHealth_) + "->" +
                       std::to_string(snapshot.playerHealth));
        previousPlayerHealth_ = snapshot.playerHealth;
    }
    if (snapshot.cameraAreaId != previousCameraAreaId_) {
        transition("camera_area", std::to_string(previousCameraAreaId_) +
                                      "->" +
                                      std::to_string(snapshot.cameraAreaId));
        previousCameraAreaId_ = snapshot.cameraAreaId;
    }
    if (snapshot.activeCinematicId != previousCinematicId_) {
        transition("active_cinematic",
                   std::to_string(previousCinematicId_) + "->" +
                       std::to_string(snapshot.activeCinematicId));
        previousCinematicId_ = snapshot.activeCinematicId;
    }
    const std::string visibleRooms = visibleRoomList(snapshot.visibleRooms);
    if (visibleRooms != previousVisibleRooms_) {
        transition("visible_rooms", previousVisibleRooms_ + "->" +
                                        visibleRooms);
        previousVisibleRooms_ = visibleRooms;
    }
    for (const game::LevelEnemyState& enemy : snapshot.enemies) {
        if (enemy.asset == nullptr) {
            continue;
        }
        EnemyTraceState current{enemy.health,
                                enemy.visible,
                                enemy.aiEnabled,
                                enemy.playerDetected,
                                enemy.behavior,
                                enemy.activeAnimation};
        auto [entry, inserted] = previousEnemies_.try_emplace(
            enemy.asset->objectId, current);
        if (!inserted &&
            (entry->second.health != current.health ||
             entry->second.visible != current.visible ||
             entry->second.aiEnabled != current.aiEnabled ||
             entry->second.playerDetected != current.playerDetected ||
             entry->second.behavior != current.behavior ||
             entry->second.animation != current.animation)) {
            transition("enemy_state",
                       "id=" + std::to_string(enemy.asset->objectId) +
                           ";health=" + std::to_string(current.health) +
                           ";visible=" + std::to_string(current.visible) +
                           ";ai=" + std::to_string(current.aiEnabled) +
                           ";detected=" +
                           std::to_string(current.playerDetected) +
                           ";behavior=" + behaviorName(current.behavior) +
                           ";animation=" + current.animation);
            entry->second = std::move(current);
        }
    }

    if (snapshot.realTimeMilliseconds < nextSampleMilliseconds_) {
        return;
    }
    nextSampleMilliseconds_ = snapshot.realTimeMilliseconds +
                              sampleIntervalMilliseconds_;
    frameLog_ << snapshot.frameIndex << ',' << snapshot.realTimeMilliseconds
              << ',' << snapshot.gameTimeMilliseconds << ','
              << (snapshot.gameplayActive ? "gameplay" : "intro") << ','
              << snapshot.controlsEnabled << ',' << snapshot.playerPosition.x
              << ',' << snapshot.playerPosition.y << ','
              << snapshot.playerPosition.z << ',' << snapshot.playerFacing.x
              << ',' << snapshot.playerFacing.y << ','
              << snapshot.playerFacing.z << ',' << snapshot.playerHealth << ','
              << csv(snapshot.playerAnimation) << ','
              << snapshot.playerAnimationTimeMilliseconds << ','
              << snapshot.cameraAreaId << ',' << snapshot.activeCinematicId
              << ',' << snapshot.quickTimeEventActive << ','
              << snapshot.restoreActive << ',' << snapshot.restoreAlpha << ','
              << csv(visibleRooms) << ',' << lastMotionInput_.right << ','
              << lastMotionInput_.forward << ',' << snapshot.camera.position.x
              << ',' << snapshot.camera.position.y << ','
              << snapshot.camera.position.z << ',' << snapshot.camera.target.x
              << ',' << snapshot.camera.target.y << ','
              << snapshot.camera.target.z << '\n';
    for (const game::LevelEnemyState& enemy : snapshot.enemies) {
        if (enemy.asset == nullptr) {
            continue;
        }
        enemyLog_ << snapshot.frameIndex << ','
                  << snapshot.realTimeMilliseconds << ','
                  << enemy.asset->objectId << ',' << enemy.asset->enemyTypeId
                  << ',' << enemy.position.x << ',' << enemy.position.y << ','
                  << enemy.position.z << ',' << enemy.health << ','
                  << enemy.visible << ',' << enemy.aiEnabled << ','
                  << enemy.playerDetected << ',' << behaviorName(enemy.behavior)
                  << ',' << csv(enemy.activeAnimation) << ','
                  << enemy.animationTimeMilliseconds << ','
                  << enemy.animationSpeed << ',' << enemy.animationLoops << ','
                  << enemy.animationReversed << '\n';
    }
    frameLog_.flush();
    enemyLog_.flush();
}

void AutoplayHarness::recordEvent(std::uint64_t timeMilliseconds,
                                  std::string_view type,
                                  std::string_view detail) {
    if (!eventLog_) {
        return;
    }
    eventLog_ << timeMilliseconds << ',' << lastFrameIndex_ << ',' << csv(type)
              << ',' << csv(detail) << '\n';
    eventLog_.flush();
}

void AutoplayHarness::recordCommand(
    std::uint64_t timeMilliseconds, std::int32_t threadObjectId,
    const game::CinematicCommand& command) {
    std::string detail =
        "thread=" + std::to_string(threadObjectId) + ";name=" +
        command.name + ";timestamp=" +
        std::to_string(command.timestampMilliseconds);
    for (const game::CinematicAttribute& attribute : command.attributes) {
        detail += ";" + attribute.name + "=" + attribute.value;
    }
    recordEvent(timeMilliseconds, "cinematic_command", detail);
}

void AutoplayHarness::recordAudio(std::uint64_t timeMilliseconds,
                                  std::string_view action,
                                  std::string_view eventName, bool loop,
                                  bool spatial) {
    recordEvent(timeMilliseconds, "audio",
                "action=" + std::string(action) + ";event=" +
                    std::string(eventName) + ";loop=" +
                    std::to_string(loop) + ";spatial=" +
                    std::to_string(spatial));
}

bool AutoplayHarness::periodicCaptureDue(
    std::uint64_t timeMilliseconds) noexcept {
    if (captureIntervalMilliseconds_ == 0 ||
        timeMilliseconds < nextCaptureMilliseconds_) {
        return false;
    }
    nextCaptureMilliseconds_ = timeMilliseconds + captureIntervalMilliseconds_;
    return true;
}

Result AutoplayHarness::writeCapture(const assets::RgbaImage& image,
                                     std::string_view label,
                                     std::uint64_t timeMilliseconds) {
    if (image.width == 0 || image.height == 0 ||
        image.pixels.size() !=
            static_cast<std::size_t>(image.width) * image.height * 4U) {
        return Result::failure("Autoplay capture has invalid image data");
    }
    const std::string filename =
        std::to_string(timeMilliseconds) + "-" + sanitizeLabel(label) + ".bmp";
    std::ofstream stream(outputPath_ / filename,
                         std::ios::binary | std::ios::trunc);
    if (!stream) {
        return Result::failure("Could not create autoplay capture " + filename);
    }
    constexpr std::uint32_t fileHeaderSize = 14;
    constexpr std::uint32_t infoHeaderSize = 40;
    const std::uint32_t pixelBytes = image.width * image.height * 4U;
    stream.put('B');
    stream.put('M');
    writeU32(stream, fileHeaderSize + infoHeaderSize + pixelBytes);
    writeU16(stream, 0);
    writeU16(stream, 0);
    writeU32(stream, fileHeaderSize + infoHeaderSize);
    writeU32(stream, infoHeaderSize);
    writeU32(stream, image.width);
    writeU32(stream, static_cast<std::uint32_t>(-
        static_cast<std::int32_t>(image.height)));
    writeU16(stream, 1);
    writeU16(stream, 32);
    writeU32(stream, 0);
    writeU32(stream, pixelBytes);
    writeU32(stream, 0);
    writeU32(stream, 0);
    writeU32(stream, 0);
    writeU32(stream, 0);
    for (std::size_t offset = 0; offset < image.pixels.size(); offset += 4) {
        stream.put(static_cast<char>(image.pixels[offset + 2]));
        stream.put(static_cast<char>(image.pixels[offset + 1]));
        stream.put(static_cast<char>(image.pixels[offset]));
        stream.put(static_cast<char>(image.pixels[offset + 3]));
    }
    if (!stream) {
        return Result::failure("Could not write autoplay capture " + filename);
    }
    recordEvent(timeMilliseconds, "capture", filename);
    return Result::success();
}

void AutoplayHarness::finish(bool applicationSucceeded,
                             std::string_view detail) {
    if (finishedLog_) {
        return;
    }
    finishedLog_ = true;
    std::ofstream summary(outputPath_ / "summary.txt", std::ios::trunc);
    summary << "application_succeeded=" << applicationSucceeded << '\n'
            << "harness_complete=" << complete_ << '\n'
            << "harness_failed=" << failed_ << '\n'
            << "last_frame=" << lastFrameIndex_ << '\n'
            << "last_time_ms=" << lastTimeMilliseconds_ << '\n'
            << "completed_steps=" << activeStepIndex_ << '\n'
            << "total_steps=" << steps_.size() << '\n'
            << "failure=" << failureMessage_ << '\n'
            << "detail=" << detail << '\n';
}

std::string AutoplayHarness::stepName(StepKind kind) {
    switch (kind) {
    case StepKind::WaitGameplay: return "wait_gameplay";
    case StepKind::Wait: return "wait";
    case StepKind::MoveTo: return "move_to";
    case StepKind::Attack: return "attack";
    case StepKind::Jump: return "jump";
    case StepKind::WebOn: return "web_on";
    case StepKind::WebOff: return "web_off";
    case StepKind::Teleport: return "teleport";
    case StepKind::Capture: return "capture";
    case StepKind::AssertNear: return "assert_near";
    case StepKind::AssertHealthAbove: return "assert_health_above";
    case StepKind::Finish: return "finish";
    }
    return "unknown";
}

std::string AutoplayHarness::behaviorName(game::EnemyBehaviorState behavior) {
    switch (behavior) {
    case game::EnemyBehaviorState::Disabled: return "disabled";
    case game::EnemyBehaviorState::Idle: return "idle";
    case game::EnemyBehaviorState::Chasing: return "chasing";
    case game::EnemyBehaviorState::AttackRange: return "attack_range";
    case game::EnemyBehaviorState::Hurt: return "hurt";
    case game::EnemyBehaviorState::Dead: return "dead";
    }
    return "unknown";
}

std::string AutoplayHarness::csv(std::string_view value) {
    std::string result{"\""};
    for (const char character : value) {
        if (character == '"') {
            result += "\"\"";
        } else if (character == '\r' || character == '\n') {
            result.push_back(' ');
        } else {
            result.push_back(character);
        }
    }
    result.push_back('"');
    return result;
}

} // namespace usm::diagnostics
