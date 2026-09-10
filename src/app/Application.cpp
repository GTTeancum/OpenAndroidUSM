#include "app/Application.hpp"

#include "assets/ColladaSkinning.hpp"
#include "audio/GameAudioMix.hpp"
#include "diagnostics/AutoplayHarness.hpp"
#include "platform/windows/GameDataLocator.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace usm {
namespace {

bool gShowErrorDialogs = true;
diagnostics::AutoplayHarness* gAutoplayDiagnostics = nullptr;

// GS_Loading::Update calls Application::SetTargetFPS(20) at 0x002c05ce.
// SetTargetFPS (0x003de618) stores 1000 / 20 = 50 ms, and Application::Update
// (0x003e1624) advances gameplay only when that fixed interval is crossed,
// with at most two catch-up updates. Keeping interactive gameplay on the
// display's 60 Hz Present cadence shortened CKeyPad's two-update press state
// from two native combat ticks to roughly 33 ms and made valid combo inputs
// disappear before UpdateKeyTrigger could sample them.
constexpr std::uint32_t kNativeGameplayTickMilliseconds = 50;
// CLevel::GetRocketPool creates four CRocket instances. Each owns one
// rocket_smoke CEffect for its entire lifetime, so reserve four stable source
// IDs beside the scene-authored persistent emitters.
constexpr std::int32_t kRocketSmokeEffectSourceBase = -2000000000;
constexpr std::int32_t kRocketPoolSize = 4;

int fail(std::string_view message) {
    if (gAutoplayDiagnostics != nullptr) {
        diagnostics::AutoplayHarness* autoplay = gAutoplayDiagnostics;
        gAutoplayDiagnostics = nullptr;
        autoplay->finish(false, message);
    }
    if (!gShowErrorDialogs) {
        std::cerr << "OpenAndroidUSM error: " << message << '\n';
        return EXIT_FAILURE;
    }
    const std::wstring wideMessage(message.begin(), message.end());
    MessageBoxW(nullptr, wideMessage.c_str(), L"OpenAndroidUSM error",
                MB_OK | MB_ICONERROR);
    return EXIT_FAILURE;
}

bool commandInteger(const game::CinematicCommand& command,
                    std::string_view name, std::int32_t& value) noexcept {
    const game::CinematicAttribute* attribute = command.findAttribute(name);
    if (attribute == nullptr) {
        return false;
    }
    const char* begin = attribute->value.data();
    const char* end = begin + attribute->value.size();
    const auto parsed = std::from_chars(begin, end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

Result restoreFastForwardedCinematicUi(
    game::CinematicUiRuntime& ui,
    const game::CinematicCommand& command,
    std::uint32_t playbackTimestampMilliseconds) {
    // Autoplay may enter the opening at an authored timestamp so a focused
    // visual probe does not have to replay the preceding 40 seconds. Persistent
    // interface state must still be reconstructed, and a timed ShowMessage
    // that spans the destination remains visible for only its unelapsed time.
    // Audio stays suppressed by the caller because a sound whose start lies
    // before the destination is historical, not active presentation state.
    if (command.name == "InterfaceControl") {
        return ui.applyCommand(command);
    }
    if (command.name != "ShowMessage") {
        return Result::success();
    }

    std::int32_t timerMilliseconds = 0;
    if (!commandInteger(command, "Timer", timerMilliseconds) ||
        timerMilliseconds < 0) {
        return ui.applyCommand(command);
    }
    const std::uint64_t commandEndMilliseconds =
        static_cast<std::uint64_t>(command.timestampMilliseconds) +
        static_cast<std::uint32_t>(timerMilliseconds);
    const std::uint64_t remainingMilliseconds =
        commandEndMilliseconds > playbackTimestampMilliseconds
            ? commandEndMilliseconds - playbackTimestampMilliseconds
            : 0;

    game::CinematicCommand restored = command;
    const auto timer = std::find_if(
        restored.attributes.begin(), restored.attributes.end(),
        [](const game::CinematicAttribute& attribute) {
            return attribute.name == "Timer";
        });
    if (timer == restored.attributes.end()) {
        return Result::failure("ShowMessage has no Timer attribute");
    }
    timer->value = std::to_string(remainingMilliseconds);
    return ui.applyCommand(restored);
}

// Portable counterpart of the object stream written by Application::
// SaveCheckPoint -> CLevel::Save (0x003e0d3c, 0x00388558). Asset resources
// remain owned by LevelOneBootstrap; these copies contain only mutable
// gameplay state and stable references back to those authored assets.
struct RuntimeCheckPointSnapshot {
    std::int32_t objectId{-1};
    game::GameplayPlayer player;
    game::PlayerHudHealthState playerHud;
    game::GameplayCamera camera;
    game::LevelTriggerRuntime triggers;
    game::LevelTriggerSoundRuntime triggerSounds;
    game::LevelCinematicRuntime levelCinematic;
    game::LevelEnemyRuntime enemies;
    game::LevelObjectRuntime objects;
    game::LevelDropRuntime drops;
    game::LevelHintRuntime hints;
    game::LevelDamageRuntime damage;
    game::LevelMusicRuntime music;
    game::LevelBonusCheckPointState bonuses;
    game::LevelHostageRuntime hostages;
    game::LevelEffectCheckPointState effects;
};

} // namespace

int Application::run(HINSTANCE instance, const ApplicationOptions& options) {
    gAutoplayDiagnostics = nullptr;
    gShowErrorDialogs = !options.autoplayScript.has_value();
    std::optional<diagnostics::AutoplayHarness> autoplay;
    Result result = Result::success();
    if (options.autoplayScript) {
        autoplay.emplace();
        gAutoplayDiagnostics = &*autoplay;
        result = autoplay->initialize(
            *options.autoplayScript,
            options.autoplayOutput.value_or("autoplay-output"));
        if (result) {
            result = renderer_.initializeOffscreen(autoplay->renderWidth(),
                                                   autoplay->renderHeight());
        }
    } else {
        result = window_.create(instance, L"OpenAndroidUSM", 1280, 720);
        if (result) {
            result = renderer_.initialize(window_.nativeHandle(),
                                          window_.clientWidth(),
                                          window_.clientHeight());
        }
    }
    if (!result) {
        return fail(result.message());
    }

    const bool audioEnabled = !autoplay || options.autoplayAudio;
    if (audioEnabled) {
        result = audio_.initialize();
    }
    if (!result) {
        return fail(result.message());
    }
    std::uint64_t traceTimeMilliseconds = 0;
    const auto playAudio =
        [this, &autoplay, &traceTimeMilliseconds, audioEnabled](
            const audio::PcmAudio& clip, bool loop,
            std::string_view diagnosticName = "unnamed_pcm") -> Result {
        if (autoplay) {
            autoplay->recordAudio(traceTimeMilliseconds, "play",
                                  diagnosticName, loop, false,
                                  audio::GameAudioMix::
                                      DefaultSoundEffectsGroupVolume);
        }
        return audioEnabled
            ? audio_.playNamed(
                  {}, clip, loop,
                  audio::GameAudioMix::DefaultSoundEffectsGroupVolume)
            : Result::success();
    };
    const auto playNamedAudio =
        [this, &autoplay, &traceTimeMilliseconds, audioEnabled](
            std::string_view eventName, const audio::PcmAudio& clip, bool loop,
            float volume =
                audio::GameAudioMix::DefaultSoundEffectsGroupVolume,
            std::uint32_t fadeMilliseconds = 0,
            std::string_view diagnosticName = {}) -> Result {
        if (autoplay) {
            autoplay->recordAudio(
                traceTimeMilliseconds, "play",
                diagnosticName.empty() ? eventName : diagnosticName,
                loop, false, volume);
        }
        return audioEnabled
            ? audio_.playNamed(eventName, clip, loop, volume,
                               fadeMilliseconds)
            : Result::success();
    };
    const auto playSpatialAudio =
        [this, &autoplay, &traceTimeMilliseconds, audioEnabled](
            std::string_view eventName, const audio::PcmAudio& clip,
            const audio::SpatialSoundSource& source, bool loop = false,
            std::string_view diagnosticName = {},
            float volume =
                audio::GameAudioMix::DefaultSoundEffectsGroupVolume)
            -> Result {
        if (autoplay) {
            autoplay->recordAudio(
                traceTimeMilliseconds, "play",
                diagnosticName.empty() ? eventName : diagnosticName,
                loop, true, volume);
        }
        return audioEnabled
            ? audio_.playNamed3D(eventName, clip, source, loop, volume)
            : Result::success();
    };
    const auto stopNamedAudio =
        [this, &autoplay, &traceTimeMilliseconds, audioEnabled](
            std::string_view eventName,
            std::uint32_t fadeMilliseconds = 0,
            std::string_view diagnosticName = {}) -> Result {
        if (autoplay) {
            autoplay->recordAudio(
                traceTimeMilliseconds, "stop",
                diagnosticName.empty() ? eventName : diagnosticName);
        }
        return audioEnabled ? audio_.stopNamed(eventName, fadeMilliseconds)
                            : Result::success();
    };
    const auto setNamedAudioVolume =
        [this, &autoplay, &traceTimeMilliseconds, audioEnabled](
            std::string_view eventName, float volume,
            std::uint32_t fadeMilliseconds = 0) -> Result {
        if (autoplay) {
            autoplay->recordEvent(
                traceTimeMilliseconds, "audio_volume",
                "event=" + std::string(eventName) + ";volume=" +
                    std::to_string(volume) + ";fade_ms=" +
                    std::to_string(fadeMilliseconds));
        }
        return audioEnabled
            ? audio_.setNamedVolume(eventName, volume, fadeMilliseconds)
            : Result::success();
    };

    std::filesystem::path gameDataRoot;
    result = platform::locateGameData(gameDataRoot);
    if (!result) {
        return fail(result.message());
    }
    result = levelOne_.load(gameDataRoot, options.levelNumber);
    if (!result) {
        return fail(result.message());
    }
    const bool hasIntroCinematic = levelOne_.hasIntroCinematic();
    std::vector<std::int16_t> levelEnemyTypes;
    for (const game::LevelEnemyAsset& enemy : levelOne_.enemies()) {
        if (std::find(levelEnemyTypes.begin(), levelEnemyTypes.end(),
                      enemy.enemyTypeId) == levelEnemyTypes.end()) {
            levelEnemyTypes.push_back(enemy.enemyTypeId);
        }
    }
    if (autoplay) {
        autoplay->bindTriggers(levelOne_.triggers());
        autoplay->recordEvent(
            0, "level_asset",
            "level=" + std::to_string(levelOne_.levelNumber()) +
                ";player=" + std::to_string(levelOne_.player().objectId) +
                ";initial_camera=" +
                std::to_string(levelOne_.player().initialCameraAreaId) +
                ";rooms=" + std::to_string(levelOne_.rooms().size()) +
                ";enemies=" + std::to_string(levelOne_.enemies().size()) +
                ";objects=" + std::to_string(levelOne_.objects().size()) +
                ";triggers=" + std::to_string(levelOne_.triggers().size()) +
                ";checkpoints=" +
                std::to_string(levelOne_.checkPoints().size()));
        autoplay->recordCinematicAssets(levelOne_.cinematics());
        autoplay->recordCollisionAssets(levelOne_.rooms());
        autoplay->recordMaterialAssets(levelOne_);
        if (levelOne_.hasIntroCinematic()) {
            autoplay->recordEvent(
                0, "cinematic_camera_asset",
                "cinematic=1265;file_duration_ms=" +
                    std::to_string(levelOne_.introCameraAnimation()
                                       .durationMilliseconds()) +
                    ";clip_id=" +
                    std::to_string(levelOne_.introCamera().clipId()) +
                    ";clip_start_ms=" +
                    std::to_string(levelOne_.introCamera()
                                       .clipStartMilliseconds()) +
                    ";clip_end_ms=" +
                    std::to_string(levelOne_.introCamera()
                                       .clipEndMilliseconds()) +
                    ";clip_duration_ms=" +
                    std::to_string(levelOne_.introCamera()
                                       .clipDurationMilliseconds()));
            for (const game::CinematicActorAsset& actor :
                 levelOne_.introActors()) {
                autoplay->recordEvent(
                    0, "cinematic_actor_asset",
                    "cinematic=1265;object=" +
                        std::to_string(actor.objectId) +
                        ";start_ms=" +
                        std::to_string(actor.animationStartMilliseconds) +
                        ";file_duration_ms=" +
                        std::to_string(
                            actor.animation.durationMilliseconds()) +
                        ";clip_id=" +
                        std::to_string(actor.animationClipId) +
                        ";clip_start_ms=" +
                        std::to_string(
                            actor.animationClipStartMilliseconds) +
                        ";clip_end_ms=" +
                        std::to_string(
                            actor.animationClipEndMilliseconds) +
                        ";clip_duration_ms=" +
                        std::to_string(
                            actor.animationClipDurationMilliseconds()));
            }
        }
        for (const game::LevelCinematicAsset& cinematic :
             levelOne_.cinematics()) {
            if (cinematic.animatedCamera.valid()) {
                autoplay->recordEvent(
                    0, "cinematic_camera_asset",
                    "cinematic=" + std::to_string(cinematic.objectId) +
                        ";file=" + cinematic.cameraAnimationFile +
                        ";start_ms=" +
                        std::to_string(
                            cinematic.cameraAnimationStartMilliseconds) +
                        ";file_duration_ms=" +
                        std::to_string(cinematic.cameraAnimation
                                           .durationMilliseconds()) +
                        ";clip_id=" +
                        std::to_string(
                            cinematic.animatedCamera.clipId()) +
                        ";clip_start_ms=" +
                        std::to_string(cinematic.animatedCamera
                                           .clipStartMilliseconds()) +
                        ";clip_end_ms=" +
                        std::to_string(cinematic.animatedCamera
                                           .clipEndMilliseconds()) +
                        ";clip_duration_ms=" +
                        std::to_string(cinematic.animatedCamera
                                           .clipDurationMilliseconds()) +
                        ";playback_end_ms=" +
                        std::to_string(
                            cinematic.colladaDurationMilliseconds));
            }
            for (const game::CinematicActorAsset& actor :
                 cinematic.actors) {
                autoplay->recordEvent(
                    0, "cinematic_actor_asset",
                    "cinematic=" + std::to_string(cinematic.objectId) +
                        ";object=" + std::to_string(actor.objectId) +
                        ";start_ms=" +
                        std::to_string(actor.animationStartMilliseconds) +
                        ";file_duration_ms=" +
                        std::to_string(
                            actor.animation.durationMilliseconds()) +
                        ";clip_id=" +
                        std::to_string(actor.animationClipId) +
                        ";clip_start_ms=" +
                        std::to_string(
                            actor.animationClipStartMilliseconds) +
                        ";clip_end_ms=" +
                        std::to_string(
                            actor.animationClipEndMilliseconds) +
                        ";clip_duration_ms=" +
                        std::to_string(
                            actor.animationClipDurationMilliseconds()));
            }
        }
        const auto recordSceneNode = [&autoplay](
                                         const assets::IrrSceneNode& node,
                                         std::int32_t roomId) {
            const auto attribute = [&node](std::string_view name) {
                const auto match = node.userAttributes.find(
                    std::string(name));
                return match == node.userAttributes.end()
                    ? std::string{}
                    : match->second;
            };
            autoplay->recordEvent(
                0, "scene_node_asset",
                "object=" + std::to_string(node.id) +
                    ";name=" + node.name +
                    ";game_type=" + node.gameType +
                    ";room=" + std::to_string(roomId) +
                    ";parent=" + std::to_string(node.parentId) +
                    ";local_x=" + std::to_string(node.position.x) +
                    ";local_y=" + std::to_string(node.position.y) +
                    ";local_z=" + std::to_string(node.position.z) +
                    ";rotation_x=" + std::to_string(node.rotation.x) +
                    ";rotation_y=" + std::to_string(node.rotation.y) +
                    ";rotation_z=" + std::to_string(node.rotation.z) +
                    ";rotation_w=" + std::to_string(node.rotation.w) +
                    ";scale_x=" + std::to_string(node.scale.x) +
                    ";scale_y=" + std::to_string(node.scale.y) +
                    ";scale_z=" + std::to_string(node.scale.z) +
                    ";x=" + std::to_string(node.absoluteTransform[12]) +
                    ";y=" + std::to_string(node.absoluteTransform[13]) +
                    ";z=" + std::to_string(node.absoluteTransform[14]) +
                    ";world_m00=" +
                    std::to_string(node.absoluteTransform[0]) +
                    ";world_m01=" +
                    std::to_string(node.absoluteTransform[1]) +
                    ";world_m02=" +
                    std::to_string(node.absoluteTransform[2]) +
                    ";world_m10=" +
                    std::to_string(node.absoluteTransform[4]) +
                    ";world_m11=" +
                    std::to_string(node.absoluteTransform[5]) +
                    ";world_m12=" +
                    std::to_string(node.absoluteTransform[6]) +
                    ";world_m20=" +
                    std::to_string(node.absoluteTransform[8]) +
                    ";world_m21=" +
                    std::to_string(node.absoluteTransform[9]) +
                    ";world_m22=" +
                    std::to_string(node.absoluteTransform[10]) +
                    ";visible=" + std::to_string(node.visible) +
                    ";collision=" + std::to_string(node.hasCollision) +
                    ";ai_enable=" + attribute("AI_Enable") +
                    ";boss_type=" + attribute("Boss_Type") +
                    ";enemy_type=" + attribute("Enemy_Type") +
                    ";health=" + attribute("Health") +
                    ";aware_radius=" + attribute("Aware_Radius") +
                    ";aware_angle=" + attribute("Aware_Angle") +
                    ";immobile=" + attribute("Imobile") +
                    ";in_air=" + attribute("InAir") +
                    ";on_wall=" + attribute("OnWall") +
                    ";add_color=" + attribute("AddColor") +
                    ";off_duration=" + attribute("OffDuration") +
                    ";on_duration=" + attribute("OnDuration") +
                    ";ready_duration=" + attribute("ReadyDuration") +
                    ";delay=" + attribute("Delay") +
                    ";damage=" + attribute("Damage") +
                    ";active=" + attribute("Active") +
                    ";electric_state=" +
                    attribute("$ElectricBoardState") +
                    ";park_duration=" + attribute("ParkDuration") +
                    ";line_speed=" + attribute("Line_Speed") +
                    ";active_forever=" + attribute("ActiveForever") +
                    ";next_related_object=" + attribute("NextRelatedObj") +
                    ";linked_waypoint=" + attribute("^Link^WayPoint") +
                    ";checkpoint_sizes=" + attribute("Sizes") +
                    ";checkpoint_save_position=" +
                    attribute("SavePosition") +
                    ";checkpoint_enabled=" + attribute("Enabled") +
                    ";checkpoint_is_obbox=" + attribute("IsOBBox") +
                    ";waypoint1=" + attribute("^WayPoint1") +
                    ";waypoint2=" + attribute("^WayPoint2") +
                    ";anim1=" + attribute("@Anim1") +
                    ";anim2=" + attribute("@Anim2") +
                    ";anim3=" + attribute("@Anim3") +
                    ";anim4=" + attribute("@Anim4") +
                    ";mesh=" + node.meshFile +
                    ";animation=" + node.animationFile +
                    ";initial_animation=" + node.initialAnimation);
        };
        for (const assets::IrrSceneNode& node :
             levelOne_.mainScene().nodes()) {
            recordSceneNode(node, 0);
        }
        for (std::size_t roomIndex = 0;
             roomIndex < levelOne_.rooms().size(); ++roomIndex) {
            for (const assets::IrrSceneNode& node :
                 levelOne_.rooms()[roomIndex].scene.nodes()) {
                recordSceneNode(node,
                                static_cast<std::int32_t>(roomIndex + 1));
            }
        }
        for (const game::LevelTriggerAsset& trigger : levelOne_.triggers()) {
            autoplay->recordEvent(
                0, "trigger_asset",
                "object=" + std::to_string(trigger.objectId) +
                    ";name=" + trigger.name +
                    ";room=" + std::to_string(trigger.roomId) +
                    ";x=" + std::to_string(trigger.position.x) +
                    ";y=" + std::to_string(trigger.position.y) +
                    ";z=" + std::to_string(trigger.position.z) +
                    ";size_x=" + std::to_string(trigger.sizes.x) +
                    ";size_y=" + std::to_string(trigger.sizes.y) +
                    ";size_z=" + std::to_string(trigger.sizes.z) +
                    ";scale_x=" + std::to_string(trigger.scale.x) +
                    ";scale_y=" + std::to_string(trigger.scale.y) +
                    ";scale_z=" + std::to_string(trigger.scale.z) +
                    ";rotation_x=" + std::to_string(trigger.rotation.x) +
                    ";rotation_y=" + std::to_string(trigger.rotation.y) +
                    ";rotation_z=" + std::to_string(trigger.rotation.z) +
                    ";rotation_w=" + std::to_string(trigger.rotation.w) +
                    ";oriented=" +
                    std::to_string(trigger.orientedBox) +
                    ";enabled=" + std::to_string(trigger.enabled) +
                    ";auto_disabled=" +
                    std::to_string(trigger.autoDisabled) +
                    ";out_to_in=" +
                    std::to_string(trigger.outToInCinematicId) +
                    ";in_to_out=" +
                    std::to_string(trigger.inToOutCinematicId) +
                    ";while_in=" +
                    std::to_string(trigger.whileInsideCinematicId) +
                    ";while_out=" +
                    std::to_string(trigger.whileOutsideCinematicId));
        }
        for (const game::LevelRestoreTriggerAsset& trigger :
             levelOne_.restoreTriggers()) {
            autoplay->recordEvent(
                0, "restore_trigger_asset",
                "object=" + std::to_string(trigger.objectId) +
                    ";room=" + std::to_string(trigger.roomId) +
                    ";restore_point=" +
                    std::to_string(trigger.restorePointId) +
                    ";x=" + std::to_string(trigger.position.x) +
                    ";y=" + std::to_string(trigger.position.y) +
                    ";z=" + std::to_string(trigger.position.z) +
                    ";size_x=" + std::to_string(trigger.sizes.x) +
                    ";size_y=" + std::to_string(trigger.sizes.y) +
                    ";size_z=" + std::to_string(trigger.sizes.z) +
                    ";scale_x=" + std::to_string(trigger.scale.x) +
                    ";scale_y=" + std::to_string(trigger.scale.y) +
                    ";scale_z=" + std::to_string(trigger.scale.z) +
                    ";rotation_x=" + std::to_string(trigger.rotation.x) +
                    ";rotation_y=" + std::to_string(trigger.rotation.y) +
                    ";rotation_z=" + std::to_string(trigger.rotation.z) +
                    ";rotation_w=" + std::to_string(trigger.rotation.w) +
                    ";damage=" + std::to_string(trigger.damage) +
                    ";cinematic=" +
                    std::to_string(trigger.cinematicId) +
                    ";fall_after_restore=" +
                    std::to_string(trigger.fallAfterRestore) +
                    ";use_last_checkpoint=" +
                    std::to_string(trigger.useLastCheckpoint));
        }
        for (const game::LevelCheckPointAsset& checkpoint :
             levelOne_.checkPoints()) {
            autoplay->recordEvent(
                0, "checkpoint_asset",
                "object=" + std::to_string(checkpoint.objectId) +
                    ";room=" + std::to_string(checkpoint.roomId) +
                    ";x=" + std::to_string(checkpoint.position.x) +
                    ";y=" + std::to_string(checkpoint.position.y) +
                    ";z=" + std::to_string(checkpoint.position.z) +
                    ";size_x=" + std::to_string(checkpoint.sizes.x) +
                    ";size_y=" + std::to_string(checkpoint.sizes.y) +
                    ";size_z=" + std::to_string(checkpoint.sizes.z) +
                    ";save_position=" +
                    std::to_string(checkpoint.savePosition) +
                    ";enabled=" + std::to_string(checkpoint.enabled) +
                    ";oriented=" +
                    std::to_string(checkpoint.orientedBox) +
                    ";linked_waypoint=" +
                    std::to_string(checkpoint.linkedWaypointId));
        }
        for (const game::LevelTriggerSoundAsset& sound :
             levelOne_.triggerSounds()) {
            autoplay->recordEvent(
                0, "trigger_sound_asset",
                "object=" + std::to_string(sound.objectId) +
                    ";room=" + std::to_string(sound.roomId) +
                    ";event=" + sound.eventName +
                    ";x=" + std::to_string(sound.position.x) +
                    ";y=" + std::to_string(sound.position.y) +
                    ";z=" + std::to_string(sound.position.z) +
                    ";size_x=" + std::to_string(sound.sizes.x) +
                    ";size_y=" + std::to_string(sound.sizes.y) +
                    ";size_z=" + std::to_string(sound.sizes.z) +
                    ";scale_x=" + std::to_string(sound.scale.x) +
                    ";scale_y=" + std::to_string(sound.scale.y) +
                    ";scale_z=" + std::to_string(sound.scale.z) +
                    ";axis_aligned=" +
                    std::to_string(sound.axisAlignedBox));
        }
        for (const game::LevelWayPointAsset& waypoint :
             levelOne_.waypoints()) {
            autoplay->recordEvent(
                0, "waypoint_asset",
                "object=" + std::to_string(waypoint.objectId) +
                    ";x=" + std::to_string(waypoint.position.x) +
                    ";y=" + std::to_string(waypoint.position.y) +
                    ";z=" + std::to_string(waypoint.position.z) +
                    ";next0=" +
                    std::to_string(waypoint.nextWaypointIds[0]) +
                    ";next1=" +
                    std::to_string(waypoint.nextWaypointIds[1]) +
                    ";enabled=" + std::to_string(waypoint.enabled) +
                    ";use_gravity=" +
                    std::to_string(waypoint.useGravityWhenEnd) +
                    ";electric=" +
                    std::to_string(waypoint.electricShock) +
                    ";time_to_me=" + std::to_string(waypoint.timeToMe));
        }
        for (const game::LevelWebGrabPointAsset& point :
             levelOne_.webGrabPoints()) {
            autoplay->recordEvent(
                0, "web_grab_asset",
                "object=" + std::to_string(point.objectId) +
                    ";x=" + std::to_string(point.position.x) +
                    ";y=" + std::to_string(point.position.y) +
                    ";z=" + std::to_string(point.position.z) +
                    ";direction_control=" +
                    std::to_string(point.directionControlPointId) +
                    ";direction_x=" + std::to_string(point.direction.x) +
                    ";direction_y=" + std::to_string(point.direction.y) +
                    ";direction_z=" + std::to_string(point.direction.z) +
                    ";length=" + std::to_string(point.length) +
                    ";visible_length=" +
                    std::to_string(point.visibleLength) +
                    ";vertical_angle=" +
                    std::to_string(point.verticalAngleDegrees) +
                    ";horizontal_angle=" +
                    std::to_string(point.horizontalAngleDegrees) +
                    ";exit_speed=" + std::to_string(point.exitSpeed) +
                    ";cannot_control=" +
                    std::to_string(point.cannotControl) +
                    ";target_waypoint=" +
                    std::to_string(point.targetWaypointId) +
                    ";target_slide=" +
                    std::to_string(point.targetSlideId));
        }
        for (const game::LevelSlideAsset& slide : levelOne_.slides()) {
            std::string detail =
                "object=" + std::to_string(slide.objectId) +
                ";entry=" + std::to_string(slide.linkedWaypointId) +
                ";enabled=" + std::to_string(slide.enabled) +
                ";electric=" + std::to_string(slide.electricShock);
            for (std::size_t index = 0; index < slide.waypointIds.size();
                 ++index) {
                detail += ";waypoint" + std::to_string(index) + "=" +
                          std::to_string(slide.waypointIds[index]);
            }
            autoplay->recordEvent(0, "slide_asset", detail);
        }
        for (const game::LevelEnemyAsset& enemy : levelOne_.enemies()) {
            const game::EnemyAttributeDefinition* attributes =
                levelOne_.enemyAttributeConfigs().find(enemy.enemyTypeId);
            autoplay->recordEvent(
                0, "enemy_asset",
                 "object=" + std::to_string(enemy.objectId) +
                     ";room=" + std::to_string(enemy.roomId) +
                     ";type=" + std::to_string(enemy.enemyTypeId) +
                     ";x=" + std::to_string(enemy.position.x) +
                     ";y=" + std::to_string(enemy.position.y) +
                     ";z=" + std::to_string(enemy.position.z) +
                     ";scale_x=" + std::to_string(enemy.scale.x) +
                     ";scale_y=" + std::to_string(enemy.scale.y) +
                     ";scale_z=" + std::to_string(enemy.scale.z) +
                     ";radius=" +
                     std::to_string(attributes == nullptr
                                        ? 0.0F
                                        : attributes->collisionRadius) +
                     ";height=" +
                     std::to_string(attributes == nullptr
                                        ? 0.0F
                                        : attributes->collisionHeight) +
                     ";visible=" + std::to_string(enemy.visible) +
                    ";ai=" + std::to_string(enemy.aiEnabled) +
                    ";wait_spawn=" + std::to_string(enemy.waitSpawn) +
                    ";on_wall=" + std::to_string(enemy.onWall) +
                    ";in_air=" + std::to_string(enemy.inAir) +
                    ";immobile=" + std::to_string(enemy.immobile) +
                    ";rotation_x=" + std::to_string(enemy.rotation.x) +
                    ";rotation_y=" + std::to_string(enemy.rotation.y) +
                    ";rotation_z=" + std::to_string(enemy.rotation.z) +
                    ";rotation_w=" + std::to_string(enemy.rotation.w));
        }
        for (const auto& definition :
             levelOne_.enemyAttributeConfigs().definitions()) {
            std::string detail = "type=" +
                std::to_string(definition.enemyTypeId) +
                ";name=" + definition.name +
                ";walk_speed=" + std::to_string(
                    definition.walkSpeedCentimetersPerMillisecond) +
                ";run_speed=" + std::to_string(
                    definition.runSpeedCentimetersPerMillisecond) +
                ";wall_speed=" + std::to_string(
                    definition.wallSpeedCentimetersPerMillisecond) +
                ";melee_min=" + std::to_string(
                    definition.minimumMeleeAttackDistance) +
                ";melee_max=" + std::to_string(
                    definition.maximumMeleeAttackDistance);
            for (const auto behavior : definition.behaviorTypeMapIndices) {
                detail += ";behavior=" + std::to_string(behavior);
            }
            autoplay->recordEvent(0, "enemy_attribute_config", detail);
        }
        for (const auto& attack : levelOne_.attackConfigs().attacks()) {
            autoplay->recordEvent(0, "enemy_attack_config",
                 "id=" + std::to_string(attack.id) + ";name=" + attack.name +
                 ";hit_type=" + std::to_string(attack.hitType) +
                 ";startup_ms=" +
                 std::to_string(attack.startupMilliseconds) +
                 ";turn_during_startup=" +
                 std::to_string(attack.turnTowardTargetDuringStartup ? 1 : 0) +
                 ";qte_enabled=" +
                 std::to_string(attack.quickTimeEnabled ? 1 : 0) +
                 ";qte_action=" +
                 std::to_string(attack.quickTimeActionId) +
                 ";damage=" + std::to_string(attack.damage) +
                 ";hit_protection_ms=" +
                 std::to_string(attack.hitProtectionMilliseconds) +
                 ";horizontal_force=" +
                 std::to_string(attack.horizontalForce) +
                 ";vertical_force=" +
                 std::to_string(attack.verticalForce) +
                 ";reach=" + std::to_string(attack.maximumReach()) +
                ";angle_min=" +
                std::to_string(attack.minimumAngleDegrees) +
                ";angle_max=" +
                std::to_string(attack.maximumAngleDegrees) +
                 ";interruptible=" +
                 std::to_string(attack.interruptibleDuringExecution ? 1 : 0) +
                 ";target_relative_movement=" +
                 std::to_string(attack.usesTargetRelativeMovement ? 1 : 0) +
                 ";movement_to_special_action=" +
                 std::to_string(attack.movementEndsAtSpecialAction ? 1 : 0) +
                 ";special_animation_successor=" +
                 std::to_string(
                     attack.permitsSpecialAnimationSuccessor ? 1 : 0) +
                 ";sense_denominator=" +
                std::to_string(attack.senseSlowMotionDenominator) +
                ";sense_reaction=" +
                std::to_string(attack.senseReactionType) +
                ";force_sense_action=" +
                std::to_string(attack.forceSenseActionId) +
                ";sense_photo=" +
                std::to_string(attack.sensePhotoTargetId));
        }
        for (const auto& config : levelOne_.buttonConfigs().definitions()) {
            autoplay->recordEvent(0, "button_config",
                "id=" + std::to_string(config.id) + ";name=" + config.name +
                ";interaction_type=" + std::to_string(config.interactionType) +
                ";interaction_value=" + std::to_string(config.interactionValue) +
                ";duration_ms=" + std::to_string(config.durationMilliseconds) +
                ";required_actions=" + std::to_string(config.requiredActionCount));
        }
        for (const auto& volume : levelOne_.damageVolumes()) {
            autoplay->recordEvent(0, "damage_volume_asset",
                "object=" + std::to_string(volume.objectId) +
                ";room=" + std::to_string(volume.roomId) +
                ";x=" + std::to_string(volume.position.x) +
                ";y=" + std::to_string(volume.position.y) +
                ";z=" + std::to_string(volume.position.z) +
                ";size_x=" + std::to_string(volume.sizes.x) +
                ";size_y=" + std::to_string(volume.sizes.y) +
                ";size_z=" + std::to_string(volume.sizes.z) +
                ";damage=" + std::to_string(volume.damage) +
                ";type=" + std::to_string(volume.damageType));
        }
        std::size_t playerClipId = 0;
        for (const auto& clip : levelOne_.player().animationBank.clips()) {
            const auto& displacement = levelOne_.player().animationDisplacement;
            const auto first = displacement.physicalAt(clip.startMilliseconds);
            const auto last = displacement.physicalAt(clip.endMilliseconds - 1);
            autoplay->recordEvent(0, "player_animation_config",
                "id=" + std::to_string(playerClipId++) + ";name=" + clip.name +
                ";duration_ms=" + std::to_string(clip.durationMilliseconds()) +
                ";physical_x=" + std::to_string(last.x - first.x) +
                ";physical_y=" + std::to_string(last.y - first.y) +
                ";physical_z=" + std::to_string(last.z - first.z));
        }
        const auto& enemyBehaviors = levelOne_.enemyBehaviorConfigs();
        for (const auto& state : enemyBehaviors.states()) {
            for (const auto& definition :
                 levelOne_.enemyAttributeConfigs().definitions()) {
                const auto animations = enemyBehaviors.resolveStateAnimationNames(
                    state.name, definition.enemyTypeId);
                if (animations.empty()) {
                    continue;
                }
                std::string detail = "state=" + std::to_string(state.id) +
                    ";name=" + state.name + ";type=" +
                    std::to_string(definition.enemyTypeId) + ";loop=" +
                    std::to_string(state.looping);
                for (const auto animation : animations) {
                    detail += ";animation=" + std::string(animation);
                }
                autoplay->recordEvent(0, "enemy_behavior_config", detail);
            }
        }
        for (const game::EnemyArchetypeAsset& archetype :
             levelOne_.enemyArchetypes()) {
            for (const assets::ColladaAnimationClip& clip :
                 archetype.animationBank.clips()) {
                const auto& displacement = archetype.animationDisplacement;
                const assets::Vector3 physicalStart =
                    displacement.physicalAt(clip.startMilliseconds);
                const assets::Vector3 physicalMiddle = displacement.physicalAt(
                    clip.startMilliseconds + clip.durationMilliseconds() / 2U);
                const assets::Vector3 physicalEnd =
                    displacement.physicalAt(clip.endMilliseconds);
                const assets::Vector3 renderStart =
                    displacement.renderOffsetAt(clip.startMilliseconds);
                const assets::Vector3 renderEnd =
                    displacement.renderOffsetAt(clip.endMilliseconds);
                autoplay->recordEvent(
                    0, "enemy_animation_asset",
                    "game_type=" + archetype.gameType +
                        ";name=" + clip.name +
                        ";start_ms=" +
                        std::to_string(clip.startMilliseconds) +
                        ";end_ms=" + std::to_string(clip.endMilliseconds) +
                        ";duration_ms=" +
                        std::to_string(clip.durationMilliseconds()) +
                        ";physical_start=" +
                        std::to_string(physicalStart.x) + "|" +
                        std::to_string(physicalStart.y) + "|" +
                        std::to_string(physicalStart.z) +
                        ";physical_middle=" +
                        std::to_string(physicalMiddle.x) + "|" +
                        std::to_string(physicalMiddle.y) + "|" +
                        std::to_string(physicalMiddle.z) +
                        ";physical_end=" +
                        std::to_string(physicalEnd.x) + "|" +
                        std::to_string(physicalEnd.y) + "|" +
                        std::to_string(physicalEnd.z) +
                        ";render_start=" +
                        std::to_string(renderStart.x) + "|" +
                        std::to_string(renderStart.y) + "|" +
                        std::to_string(renderStart.z) +
                        ";render_end=" +
                        std::to_string(renderEnd.x) + "|" +
                        std::to_string(renderEnd.y) + "|" +
                        std::to_string(renderEnd.z));
            }
            for (std::size_t skinIndex = 0;
                 skinIndex < archetype.mesh.skins().size(); ++skinIndex) {
                const assets::ColladaSkin& skin =
                    archetype.mesh.skins()[skinIndex];
                const auto& bindShape = skin.bindShapeMatrix;
                std::size_t influenceCount = 0;
                std::size_t maximumInfluences = 0;
                for (const auto& influences : skin.vertexInfluences) {
                    influenceCount += influences.size();
                    maximumInfluences =
                        std::max(maximumInfluences, influences.size());
                }
                autoplay->recordEvent(
                    0, "enemy_skin_asset",
                    "game_type=" + archetype.gameType +
                        ";skin=" + std::to_string(skinIndex) +
                        ";controller=" + skin.controllerId +
                        ";geometry=" + skin.geometryId +
                        ";joints=" +
                        std::to_string(skin.jointNames.size()) +
                        ";vertices=" +
                        std::to_string(skin.vertexInfluences.size()) +
                        ";influences=" +
                        std::to_string(influenceCount) +
                        ";maximum_influences=" +
                        std::to_string(maximumInfluences) +
                        ";bind_tx=" + std::to_string(bindShape[12]) +
                        ";bind_ty=" + std::to_string(bindShape[13]) +
                        ";bind_tz=" + std::to_string(bindShape[14]));
                if (archetype.gameType == "Boss_Electro") {
                    std::vector<std::size_t> jointInfluenceCounts(
                        skin.jointNames.size());
                    std::vector<float> jointMinimumWeights(
                        skin.jointNames.size(),
                        std::numeric_limits<float>::infinity());
                    std::vector<float> jointMaximumWeights(
                        skin.jointNames.size(), 0.0F);
                    for (const auto& influences : skin.vertexInfluences) {
                        for (const assets::ColladaVertexInfluence& influence :
                             influences) {
                            if (influence.jointIndex <
                                jointInfluenceCounts.size()) {
                                ++jointInfluenceCounts[influence.jointIndex];
                                jointMinimumWeights[influence.jointIndex] =
                                    std::min(
                                        jointMinimumWeights[
                                            influence.jointIndex],
                                        influence.weight);
                                jointMaximumWeights[influence.jointIndex] =
                                    std::max(
                                        jointMaximumWeights[
                                            influence.jointIndex],
                                        influence.weight);
                            }
                        }
                    }
                    for (std::size_t jointIndex = 0;
                         jointIndex < skin.jointNames.size(); ++jointIndex) {
                        const auto& inverseBind =
                            skin.inverseBindMatrices[jointIndex];
                        autoplay->recordEvent(
                            0, "enemy_skin_joint_asset",
                            "game_type=" + archetype.gameType +
                                ";skin=" + std::to_string(skinIndex) +
                                ";joint=" + std::to_string(jointIndex) +
                                ";scope=" + skin.jointNames[jointIndex] +
                                ";influences=" +
                                std::to_string(
                                    jointInfluenceCounts[jointIndex]) +
                                ";inverse_bind_tx=" +
                                std::to_string(inverseBind[12]) +
                                ";inverse_bind_ty=" +
                                std::to_string(inverseBind[13]) +
                                ";inverse_bind_tz=" +
                                std::to_string(inverseBind[14]) +
                                ";minimum_weight=" +
                                std::to_string(
                                    jointInfluenceCounts[jointIndex] == 0
                                        ? 0.0F
                                        : jointMinimumWeights[jointIndex]) +
                                ";maximum_weight=" +
                                std::to_string(
                                    jointMaximumWeights[jointIndex]));
                    }
                }
            }
            if (archetype.gameType == "Boss_Electro") {
                for (std::size_t trackIndex = 0;
                     trackIndex < archetype.animationBank.tracks().size();
                     ++trackIndex) {
                    const assets::ColladaAnimationTrack& track =
                        archetype.animationBank.tracks()[trackIndex];
                    std::string detail =
                        "game_type=" + archetype.gameType +
                        ";index=" + std::to_string(trackIndex) +
                        ";id=" + track.id +
                        ";target=" + track.targetNode +
                        ";property=" +
                        std::to_string(static_cast<std::int32_t>(
                            track.property)) +
                        ";components=" +
                        std::to_string(track.componentCount) +
                        ";keys=" +
                        std::to_string(track.timestampsMilliseconds.size());
                    if (!track.timestampsMilliseconds.empty()) {
                        const std::uint32_t firstTimestamp =
                            track.timestampsMilliseconds.front();
                        const std::uint32_t lastTimestamp =
                            track.timestampsMilliseconds.back();
                        const auto first = track.sample(firstTimestamp);
                        const auto last = track.sample(lastTimestamp);
                        detail +=
                            ";first_ms=" + std::to_string(firstTimestamp) +
                            ";last_ms=" + std::to_string(lastTimestamp);
                        for (std::uint32_t component = 0;
                             component < track.componentCount && component < 4;
                             ++component) {
                            detail +=
                                ";first" + std::to_string(component) + "=" +
                                std::to_string(first.value[component]) +
                                ";last" + std::to_string(component) + "=" +
                                std::to_string(last.value[component]);
                        }
                    }
                    autoplay->recordEvent(
                        0, "enemy_animation_track_asset", detail);
                }
            }
            if (archetype.gameType == "RangeThug_molotov") {
                const assets::ColladaAnimationClip* throwClip =
                    archetype.animationBank.findClip(
                        "idle_throw_molotov_idle");
                if (throwClip != nullptr) {
                    const std::uint32_t throwTime =
                        throwClip->startMilliseconds +
                        throwClip->durationMilliseconds() / 2U;
                    std::array<float, 16> hand{};
                    std::array<float, 16> center{};
                    const Result handResult =
                        assets::evaluateColladaSceneNodeTransform(
                            archetype.mesh, archetype.animationBank, throwTime,
                            "Bip01_R_Hand", hand);
                    const Result centerResult =
                        assets::evaluateColladaSceneNodeTransform(
                            archetype.mesh, archetype.animationBank, throwTime,
                            "Dummy_center", center);
                    if (handResult && centerResult) {
                        autoplay->recordEvent(
                            0, "enemy_attachment_asset",
                            "game_type=" + archetype.gameType +
                                ";node=Bip01_R_Hand;animation=" +
                                throwClip->name +
                                ";time_ms=" + std::to_string(throwTime) +
                                ";raw_x=" + std::to_string(hand[12]) +
                                ";raw_y=" + std::to_string(hand[13]) +
                                ";raw_z=" + std::to_string(hand[14]) +
                                ";center_relative_x=" +
                                std::to_string(hand[12] - center[12]) +
                                ";center_relative_y=" +
                                std::to_string(hand[13] - center[13]) +
                                ";center_relative_z=" +
                                std::to_string(hand[14] - center[14]));
                    }
                }
            }
        }
        const game::WebPelletProjectileAsset& webPelletProjectile =
            levelOne_.webPelletProjectile();
        autoplay->recordEvent(
            0, "player_web_pellet_asset",
            "mesh=" + webPelletProjectile.meshFile +
                ";geometries=" +
                std::to_string(
                    webPelletProjectile.mesh.sceneGeometries().size()) +
                ";textures=" +
                std::to_string(webPelletProjectile.textures.size()) +
                ";speed=1500.000000;radius=30.000000;launch_offset=50.000000");
        for (const game::PlayerHitEffectAsset& effect :
             levelOne_.playerHitEffects()) {
            autoplay->recordEvent(
                0, "player_hit_mesh_effect_asset",
                "id=" + std::to_string(effect.definition.id) +
                    ";name=" + effect.definition.name +
                    ";mesh=" + effect.definition.meshFile +
                    ";bone=" + effect.definition.boneName +
                    ";snapshot=" +
                    std::to_string(effect.definition.snapshotBoneTransform) +
                    ";rendering_parameter=" +
                    std::to_string(effect.definition.renderingParameter) +
                    ";lifetime_ms=" +
                    std::to_string(effect.definition.lifetimeMilliseconds) +
                    ";geometries=" +
                    std::to_string(effect.mesh.sceneGeometries().size()) +
                    ";textures=" +
                    std::to_string(effect.textures.size()));
            for (std::size_t morphIndex = 0;
                 morphIndex < effect.mesh.morphs().size(); ++morphIndex) {
                const assets::ColladaMorph& morph =
                    effect.mesh.morphs()[morphIndex];
                std::string detail =
                    "effect_id=" + std::to_string(effect.definition.id) +
                    ";morph_index=" + std::to_string(morphIndex) +
                    ";controller=" + morph.controllerId +
                    ";source=" + morph.sourceGeometryId +
                    ";method=" + std::to_string(morph.method);
                for (std::size_t targetIndex = 0;
                     targetIndex < morph.targetGeometryIndices.size();
                     ++targetIndex) {
                    detail += ";target" + std::to_string(targetIndex) +
                              "=" + std::to_string(
                                  morph.targetGeometryIndices[targetIndex]) +
                              ";weight" + std::to_string(targetIndex) +
                              "=" + std::to_string(
                                  morph.weights[targetIndex]);
                }
                autoplay->recordEvent(
                    0, "player_hit_mesh_morph_asset", detail);
            }
            for (std::size_t materialIndex = 0;
                 materialIndex < effect.mesh.materials().size();
                 ++materialIndex) {
                const assets::ColladaMaterial& material =
                    effect.mesh.materials()[materialIndex];
                autoplay->recordEvent(
                    0, "player_hit_mesh_material_asset",
                    "effect_id=" + std::to_string(effect.definition.id) +
                        ";material_index=" +
                        std::to_string(materialIndex) +
                        ";name=" + material.name +
                        ";ambient_r=" +
                        std::to_string(material.ambientColor[0]) +
                        ";ambient_g=" +
                        std::to_string(material.ambientColor[1]) +
                        ";ambient_b=" +
                        std::to_string(material.ambientColor[2]) +
                        ";ambient_a=" +
                        std::to_string(material.ambientColor[3]));
            }
            for (std::size_t textureIndex = 0;
                 textureIndex < effect.textures.size(); ++textureIndex) {
                const assets::BtexTexture& texture =
                    effect.textures[textureIndex];
                const assets::RgbaImage& image = texture.mipLevels().front();
                std::uint8_t minimumAlpha = 0xff;
                std::uint8_t maximumAlpha = 0;
                for (std::size_t alpha = 3; alpha < image.pixels.size();
                     alpha += 4) {
                    minimumAlpha =
                        std::min(minimumAlpha, image.pixels[alpha]);
                    maximumAlpha =
                        std::max(maximumAlpha, image.pixels[alpha]);
                }
                autoplay->recordEvent(
                    0, "player_hit_mesh_texture_asset",
                    "effect_id=" + std::to_string(effect.definition.id) +
                        ";texture_index=" +
                        std::to_string(textureIndex) +
                        ";contains_alpha=" +
                        std::to_string(texture.containsAlpha()) +
                        ";width=" + std::to_string(image.width) +
                        ";height=" + std::to_string(image.height) +
                        ";minimum_alpha=" +
                        std::to_string(minimumAlpha) +
                        ";maximum_alpha=" +
                        std::to_string(maximumAlpha));
            }
            for (std::size_t trackIndex = 0;
                 trackIndex < effect.animation.tracks().size(); ++trackIndex) {
                const assets::ColladaAnimationTrack& track =
                    effect.animation.tracks()[trackIndex];
                std::string detail =
                    "effect_id=" + std::to_string(effect.definition.id) +
                    ";track_index=" + std::to_string(trackIndex) +
                    ";id=" + track.id +
                    ";target=" + track.targetNode +
                    ";property=" +
                    std::to_string(static_cast<std::int32_t>(track.property)) +
                    ";target_index=" +
                    std::to_string(track.targetIndex) +
                    ";components=" +
                    std::to_string(track.componentCount) +
                    ";keys=" +
                    std::to_string(track.timestampsMilliseconds.size());
                if (!track.timestampsMilliseconds.empty()) {
                    const std::uint32_t firstTimestamp =
                        track.timestampsMilliseconds.front();
                    const std::uint32_t lastTimestamp =
                        track.timestampsMilliseconds.back();
                    const auto first = track.sample(firstTimestamp);
                    const auto last = track.sample(lastTimestamp);
                    detail += ";first_ms=" + std::to_string(firstTimestamp) +
                              ";last_ms=" + std::to_string(lastTimestamp);
                    for (std::uint32_t component = 0;
                         component < track.componentCount && component < 4;
                         ++component) {
                        detail +=
                            ";first" + std::to_string(component) + "=" +
                            std::to_string(first.value[component]) +
                            ";last" + std::to_string(component) + "=" +
                            std::to_string(last.value[component]);
                    }
                }
                autoplay->recordEvent(
                    0, "player_hit_mesh_animation_track_asset", detail);
            }
        }
        const game::MolotovProjectileAsset& molotovProjectile =
            levelOne_.molotovProjectile();
        for (const assets::ColladaAnimationClip& clip :
             molotovProjectile.animationBank.clips()) {
            autoplay->recordEvent(
                0, "molotov_projectile_animation_asset",
                "mesh=" + molotovProjectile.meshFile + ";name=" + clip.name +
                    ";start_ms=" + std::to_string(clip.startMilliseconds) +
                    ";duration_ms=" +
                    std::to_string(clip.durationMilliseconds()));
        }
        const game::BoomerangProjectileAsset& boomerangProjectile =
            levelOne_.boomerangProjectile();
        for (std::size_t clipIndex = 0;
             clipIndex < boomerangProjectile.animationBank.clips().size();
             ++clipIndex) {
            const assets::ColladaAnimationClip& clip =
                boomerangProjectile.animationBank.clips()[clipIndex];
            autoplay->recordEvent(
                0, "boomerang_projectile_animation_asset",
                "mesh=" + boomerangProjectile.meshFile +
                    ";index=" + std::to_string(clipIndex) +
                    ";name=" + clip.name +
                    ";start_ms=" +
                    std::to_string(clip.startMilliseconds) +
                    ";duration_ms=" +
                    std::to_string(clip.durationMilliseconds()));
        }
        const game::ElectroEffectAsset& electroEffects =
            levelOne_.electroEffects();
        const std::array electroModels{
            &electroEffects.wave,
            &electroEffects.waveBillboard,
            &electroEffects.beam,
        };
        for (const game::ElectroEffectModelAsset* model : electroModels) {
            for (std::size_t clipIndex = 0;
                 clipIndex < model->animationBank.clips().size();
                 ++clipIndex) {
                const assets::ColladaAnimationClip& clip =
                    model->animationBank.clips()[clipIndex];
                autoplay->recordEvent(
                    0, "electro_effect_animation_asset",
                    "mesh=" + model->meshFile +
                        ";index=" + std::to_string(clipIndex) +
                        ";name=" + clip.name +
                        ";start_ms=" +
                        std::to_string(clip.startMilliseconds) +
                        ";duration_ms=" +
                        std::to_string(clip.durationMilliseconds()));
            }
        }
        for (const game::EnemyAttributeDefinition& attributes :
             levelOne_.enemyAttributeConfigs().definitions()) {
            std::string detail =
                "type=" + std::to_string(attributes.enemyTypeId) +
                ";exported_id=" + std::to_string(attributes.exportedId) +
                ";name=" + attributes.name +
                ";radius=" + std::to_string(attributes.collisionRadius) +
                ";height=" + std::to_string(attributes.collisionHeight) +
                ";range_min=" +
                std::to_string(attributes.minimumRangeAttackDistance) +
                ";range_max=" +
                std::to_string(attributes.maximumRangeAttackDistance);
            for (std::size_t index = 0;
                 index < attributes.rangedAttackTypeMapIndices.size();
                 ++index) {
                detail += ";range_map" + std::to_string(index) + "=" +
                          std::to_string(
                              attributes.rangedAttackTypeMapIndices[index]);
            }
            autoplay->recordEvent(0, "enemy_attribute_asset", detail);
        }
        for (const game::EnemyRangeAttackDefinition& attack :
             levelOne_.enemyRangeAttackConfigs().definitions()) {
            autoplay->recordEvent(
                0, "enemy_range_attack_asset",
                "id=" + std::to_string(attack.id) +
                    ";name=" + attack.name +
                    ";map_id=" + std::to_string(attack.attackTypeMapId) +
                    ";duration_ms=" +
                    std::to_string(attack.animationDurationMilliseconds) +
                    ";speed=" +
                    std::to_string(
                        attack.projectileSpeedCentimetersPerSecond) +
                    ";damage=" + std::to_string(attack.damage));
        }
        for (const game::EnemyAnimationSpecialAction& action :
             levelOne_.enemySpecialActions().actions()) {
            if (std::find(levelEnemyTypes.begin(), levelEnemyTypes.end(),
                          action.enemyTypeId) == levelEnemyTypes.end()) {
                continue;
            }
            std::string detail =
                "record=" + std::to_string(action.recordId) +
                ";name=" + action.name +
                ";type=" + std::to_string(action.enemyTypeId) +
                ";animation=" + action.animationName +
                ";action_type=" + std::to_string(action.actionType) +
                ";key_percent=" +
                std::to_string(action.keyFramePercent) +
                ";attack=" + std::to_string(action.attackId) +
                ";next_animation=" + action.nextAnimationName;
            for (std::size_t index = 0; index < action.soundMapIds.size();
                 ++index) {
                detail += ";sound_map" + std::to_string(index) + "=" +
                          std::to_string(action.soundMapIds[index]);
            }
            autoplay->recordEvent(0, "enemy_special_action_asset", detail);
        }
        for (const game::AttackDefinition& attack :
             levelOne_.attackConfigs().attacks()) {
            autoplay->recordEvent(
                0, "enemy_attack_asset",
                "id=" + std::to_string(attack.id) +
                    ";name=" + attack.name +
                    ";damage=" + std::to_string(attack.damage) +
                    ";reach=" + std::to_string(attack.maximumReach()));
        }
        for (const game::EnemyAttackIntervalDefinition& interval :
             levelOne_.enemyAttackIntervalConfigs().definitions()) {
            std::string detail =
                "id=" + std::to_string(interval.id) +
                ";name=" + interval.name +
                ";weapon_type_map_index=" +
                std::to_string(interval.weaponTypeMapIndex);
            for (std::size_t enemyType = 0;
                 enemyType < interval.intervalMilliseconds.size();
                 ++enemyType) {
                detail += ";type" + std::to_string(enemyType) + "=" +
                          std::to_string(
                              interval.intervalMilliseconds[enemyType]);
            }
            autoplay->recordEvent(0, "enemy_attack_interval_asset", detail);
        }
        for (const game::EnemyBehaviorAnimationList& animationList :
             levelOne_.enemyBehaviorConfigs().animationLists()) {
            std::string detail =
                "id=" + std::to_string(animationList.id) +
                ";name=" + animationList.name +
                ";selection_mode=" +
                std::to_string(animationList.selectionMode);
            for (const std::int32_t animationMapId :
                 animationList.animationMapIds) {
                detail += ";animation_map=" +
                          std::to_string(animationMapId);
            }
            for (const std::int16_t enemyType : levelEnemyTypes) {
                const auto animations =
                    levelOne_.enemyBehaviorConfigs()
                        .resolveAnimationListNames(animationList.id,
                                                   enemyType);
                for (std::size_t index = 0; index < animations.size();
                     ++index) {
                    detail += ";type" + std::to_string(enemyType) +
                              "_animation" + std::to_string(index) + "=" +
                              std::string(animations[index]);
                }
            }
            autoplay->recordEvent(0, "enemy_behavior_animation_list_asset",
                                  detail);
        }
        for (const game::EnemyBehaviorStateDefinition& state :
             levelOne_.enemyBehaviorConfigs().states()) {
            std::string detail =
                "id=" + std::to_string(state.id) +
                ";name=" + state.name +
                ";loop=" + std::to_string(state.looping) +
                ";parameter0=" + std::to_string(state.parameter0) +
                ";animation_selection_mode=" +
                std::to_string(state.animationSelectionMode);
            for (const std::int16_t listId : state.animationListIds) {
                detail += ";animation_list=" + std::to_string(listId);
                const auto list = std::find_if(
                    levelOne_.enemyBehaviorConfigs().animationLists().begin(),
                    levelOne_.enemyBehaviorConfigs().animationLists().end(),
                    [listId](const game::EnemyBehaviorAnimationList& candidate) {
                        return candidate.id == listId;
                    });
                if (list != levelOne_.enemyBehaviorConfigs()
                                .animationLists().end()) {
                    detail += ":selection_mode=" +
                              std::to_string(list->selectionMode);
                }
            }
            for (const std::int16_t enemyType : levelEnemyTypes) {
                const auto animations =
                    levelOne_.enemyBehaviorConfigs()
                        .resolveStateAnimationNames(state.name, enemyType);
                for (std::size_t index = 0; index < animations.size();
                     ++index) {
                    detail += ";type" + std::to_string(enemyType) +
                              "_animation" + std::to_string(index) + "=" +
                              std::string(animations[index]);
                }
            }
            autoplay->recordEvent(0, "enemy_behavior_state_asset", detail);
        }
        const auto objectKindName = [](game::LevelObjectKind kind) {
            switch (kind) {
            case game::LevelObjectKind::Animated:
                return "animated";
            case game::LevelObjectKind::Destroyable:
                return "destroyable";
            case game::LevelObjectKind::Comic:
                return "comic";
            case game::LevelObjectKind::Car:
                return "car";
            case game::LevelObjectKind::DropObject:
                return "drop_object";
            case game::LevelObjectKind::SpiderWebWall:
                return "spider_web_wall";
            case game::LevelObjectKind::StaticObject:
                return "static";
            case game::LevelObjectKind::Hostage:
                return "hostage";
            case game::LevelObjectKind::StreamPiping:
                return "stream_piping";
            case game::LevelObjectKind::SlideCar:
                return "slide_car";
            case game::LevelObjectKind::Platform:
                return "platform";
            case game::LevelObjectKind::ElectricPlatform:
                return "electric_platform";
            case game::LevelObjectKind::Train:
                return "train";
            case game::LevelObjectKind::BrokenBridge:
                return "broken_bridge";
            case game::LevelObjectKind::AreaDamage:
                return "area_damage";
            }
            return "unknown";
        };
        for (const game::LevelObjectAsset& object : levelOne_.objects()) {
            const std::string meshFile =
                object.archetypeIndex < levelOne_.objectArchetypes().size()
                    ? levelOne_.objectArchetypes()[object.archetypeIndex]
                          .meshFile
                    : std::string{};
            autoplay->recordEvent(
                0, "object_asset",
                "object=" + std::to_string(object.objectId) +
                    ";name=" + object.name +
                    ";game_type=" + object.gameType +
                    ";kind=" + objectKindName(object.kind) +
                    ";room=" + std::to_string(object.roomId) +
                    ";x=" + std::to_string(object.position.x) +
                    ";y=" + std::to_string(object.position.y) +
                    ";z=" + std::to_string(object.position.z) +
                    ";rotation_x=" + std::to_string(object.rotation.x) +
                    ";rotation_y=" + std::to_string(object.rotation.y) +
                    ";rotation_z=" + std::to_string(object.rotation.z) +
                    ";rotation_w=" + std::to_string(object.rotation.w) +
                    ";scale_x=" + std::to_string(object.scale.x) +
                    ";scale_y=" + std::to_string(object.scale.y) +
                    ";scale_z=" + std::to_string(object.scale.z) +
                    ";visible=" + std::to_string(object.visible) +
                    ";collision=" + std::to_string(object.hasCollision) +
                    ";collision_radius=" +
                    std::to_string(object.collisionRadius) +
                    ";health=" + std::to_string(object.health) +
                    ";damage_radius=" +
                    std::to_string(object.damageRadius) +
                    ";damage=" + std::to_string(object.damage) +
                    ";effect=" + object.destructionEffectType +
                    ";dead_spawn=" +
                    std::to_string(object.deadSpawnObjectId) +
                    ";dead_cinematic=" +
                    std::to_string(object.deadCinematicId) +
                    ";attackable=" + std::to_string(object.attackable) +
                    ";collision_dead=" +
                    std::to_string(object.collisionAfterDestruction) +
                    ";hit_vox=" + std::to_string(object.hitVoxSoundId) +
                    ";bridge_type=" + std::to_string(object.bridgeType) +
                    ";bridge_idle_shake=" + std::to_string(object.bridgeIdleShakeSeconds) +
                    ";bridge_drop_shake=" + std::to_string(object.bridgeDropShakeSeconds) +
                    ";bridge_drop_distance=" + std::to_string(object.bridgeDropDistance) +
                    ";bridge_drop_angle=" + std::to_string(object.bridgeDropAngleDegrees) +
                    ";bridge_drop_seconds=" + std::to_string(object.bridgeDropSeconds) +
                    ";bridge_second_shake=" + std::to_string(object.bridgeSecondShakeSeconds) +
                    ";bridge_second_distance=" + std::to_string(object.bridgeSecondDropDistance) +
                    ";bridge_second_angle=" + std::to_string(object.bridgeSecondAngleDegrees) +
                    ";bridge_second_seconds=" + std::to_string(object.bridgeSecondDropSeconds) +
                    ";bridge_activation_distance=" + std::to_string(object.bridgeActivationDistance) +
                    ";bridge_car_speed=" + std::to_string(object.bridgeCarRunSpeed) +
                    ";comic_index=" +
                    std::to_string(object.comicIndex) +
                    ";comic_level_string=" + object.comicLevelStringId +
                    ";comic_min=" +
                    std::to_string(object.comicCollectionMinimum.x) + "|" +
                    std::to_string(object.comicCollectionMinimum.y) + "|" +
                    std::to_string(object.comicCollectionMinimum.z) +
                    ";comic_max=" +
                    std::to_string(object.comicCollectionMaximum.x) + "|" +
                    std::to_string(object.comicCollectionMaximum.y) + "|" +
                    std::to_string(object.comicCollectionMaximum.z) +
                    ";hostage_anim_1=" + object.hostageAnimations[0] +
                    ";hostage_anim_2=" + object.hostageAnimations[1] +
                    ";hostage_anim_3=" + object.hostageAnimations[2] +
                    ";hostage_anim_4=" + object.hostageAnimations[3] +
                    ";hostage_enable_radius=" +
                    std::to_string(object.hostageEnableRadius) +
                    ";hostage_health_orbs=" +
                    std::to_string(object.hostageHealthOrbCount) +
                    ";hostage_skill_orbs=" +
                    std::to_string(object.hostageSkillPointOrbCount) +
                    ";hostage_hint_height=" +
                    std::to_string(object.hostageHintHeight) +
                    ";hostage_button_height=" +
                    std::to_string(object.hostageButtonHeight) +
                    ";hostage_woman=" +
                    std::to_string(object.hostageIsWoman) +
                    ";mesh=" + meshFile);
        }
        for (std::size_t archetypeIndex = 0;
             archetypeIndex < levelOne_.objectArchetypes().size();
             ++archetypeIndex) {
            const game::LevelObjectArchetypeAsset& archetype =
                levelOne_.objectArchetypes()[archetypeIndex];
            for (std::size_t clipIndex = 0;
                 clipIndex < archetype.animationBank.clips().size();
                 ++clipIndex) {
                const assets::ColladaAnimationClip& clip =
                    archetype.animationBank.clips()[clipIndex];
                autoplay->recordEvent(
                    0, "object_animation_asset",
                    "archetype=" + std::to_string(archetypeIndex) +
                        ";mesh=" + archetype.meshFile +
                        ";index=" + std::to_string(clipIndex) +
                        ";name=" + clip.name +
                        ";duration_ms=" +
                        std::to_string(clip.durationMilliseconds()));
            }
            for (std::size_t trackIndex = 0;
                 trackIndex < archetype.animationBank.tracks().size();
                 ++trackIndex) {
                const assets::ColladaAnimationTrack& track =
                    archetype.animationBank.tracks()[trackIndex];
                std::string detail =
                    "archetype=" + std::to_string(archetypeIndex) +
                    ";mesh=" + archetype.meshFile +
                    ";index=" + std::to_string(trackIndex) +
                    ";id=" + track.id +
                    ";target=" + track.targetNode +
                    ";property=" + std::to_string(static_cast<std::int32_t>(
                                        track.property)) +
                    ";components=" +
                    std::to_string(track.componentCount) +
                    ";keys=" +
                    std::to_string(track.timestampsMilliseconds.size());
                if (!track.timestampsMilliseconds.empty()) {
                    const auto first =
                        track.sample(track.timestampsMilliseconds.front());
                    const auto last =
                        track.sample(track.timestampsMilliseconds.back());
                    detail +=
                        ";first_ms=" + std::to_string(
                            track.timestampsMilliseconds.front()) +
                        ";last_ms=" + std::to_string(
                            track.timestampsMilliseconds.back());
                    for (std::uint32_t component = 0;
                         component < track.componentCount && component < 4;
                         ++component) {
                        detail += ";first" + std::to_string(component) +
                                  "=" + std::to_string(
                                      first.value[component]) +
                                  ";last" + std::to_string(component) +
                                  "=" + std::to_string(
                                      last.value[component]);
                    }
                }
                autoplay->recordEvent(0, "object_animation_track_asset",
                                      detail);
            }
        }
        for (const game::CameraArea& area : levelOne_.cameraAreas()) {
            std::string detail =
                "object=" + std::to_string(area.objectId) +
                ";disabled=" + std::to_string(area.disabled) +
                ";height=" + std::to_string(area.height) +
                ";z_follow_rate=" + std::to_string(area.zFollowRate) +
                ";inverse_normal=" +
                std::to_string(area.inverseNormal) +
                ";far_plane_offset=" +
                std::to_string(area.farPlaneOffset);
            for (std::size_t index = 0; index < area.nextAreaIds.size();
                 ++index) {
                detail += ";next" + std::to_string(index) + "=" +
                          std::to_string(area.nextAreaIds[index]) +
                          ";switch" + std::to_string(index) + "=" +
                          std::to_string(area.switchTimeUnits[index]);
            }
            for (std::size_t index = 0; index < area.controlPoints.size();
                 ++index) {
                const game::CameraControlPoint& control =
                    area.controlPoints[index];
                const assets::Vector3& point = control.position;
                detail += ";p" + std::to_string(index) + "=" +
                          std::to_string(point.x) + "|" +
                          std::to_string(point.y) + "|" +
                          std::to_string(point.z) +
                          ";d" + std::to_string(index) + "=" +
                          std::to_string(control.direction.x) + "|" +
                          std::to_string(control.direction.y) + "|" +
                          std::to_string(control.direction.z) +
                          ";distance" + std::to_string(index) + "=" +
                          std::to_string(control.distance) +
                          ";offset" + std::to_string(index) + "=" +
                          std::to_string(control.targetOffset.x) + "|" +
                          std::to_string(control.targetOffset.y) + "|" +
                          std::to_string(control.targetOffset.z) +
                          ";height_offset" + std::to_string(index) + "=" +
                          std::to_string(control.targetHeightOffset);
            }
            autoplay->recordEvent(0, "camera_area_asset", detail);
        }
    }
    result = voxSounds_.load(gameDataRoot);
    if (!result) {
        return fail(result.message());
    }
    result = soundCatalog_.index(gameDataRoot / "sound", &voxSounds_);
    if (!result) {
        return fail(result.message());
    }
    result = levelMusicBank_.preload(soundCatalog_);
    if (!result) {
        return fail(result.message());
    }
    result = soundCatalog_.decode("SFX_ORBS_COLLECT", bonusCollectSound_);
    if (!result) {
        return fail(result.message());
    }
    constexpr std::size_t comicCollectVoxSoundId = 0x63;
    if (voxSounds_.records().size() <= comicCollectVoxSoundId) {
        return fail("VoxSounds has no comic-cover collection record");
    }
    result = soundCatalog_.decode(
        voxSounds_.records()[comicCollectVoxSoundId].eventName,
        comicCollectSound_);
    if (!result) {
        return fail(result.message());
    }
    result = soundCatalog_.decode("SFX_BATTERY_CELL_EXPLOSION",
                                  dropObjectSound_);
    if (!result) {
        return fail(result.message());
    }
    // Player::UpdateAttacks (0x00351204) explicitly calls PlayerSFX(0x54)
    // two frames after the ultimate damage frame, together with the
    // super_web_splash particle preset.
    constexpr std::uint16_t ultimateSplashVoxSoundId = 0x54;
    const audio::VoxSoundRecord* ultimateSplashRecord =
        voxSounds_.find(ultimateSplashVoxSoundId);
    if (ultimateSplashRecord == nullptr) {
        return fail("VoxSounds has no ultimate splash record");
    }
    result = soundCatalog_.decode(ultimateSplashRecord->eventName,
                                  ultimateSplashSound_);
    if (!result) {
        return fail(result.message());
    }
    hostageSounds_.clear();
    wallWebSounds_.clear();
    wallWebLoopSoundId_ = -1;
    // Player::UpdateQTE/ExitQTE and CEnemy::SetState use the armored
    // (types 8/10) or ordinary wall-drag loop/release voice pair.
    for (const auto id : std::array<std::uint16_t, 4>{0xd9, 0xda, 0xe6, 0xe7}) {
        const auto* record = voxSounds_.find(id);
        if (record == nullptr) {
            return fail("Wall-web sound has an invalid VoxSound ID");
        }
        audio::PcmAudio clip;
        result = soundCatalog_.decode(record->eventName, clip);
        if (!result) {
            return fail(result.message());
        }
        wallWebSounds_.emplace(id, std::move(clip));
    }
    constexpr std::array<std::uint16_t, 3> hostageVoxSoundIds{
        0x18b, 0xa3, 0xa5};
    for (const std::uint16_t voxSoundId : hostageVoxSoundIds) {
        const audio::VoxSoundRecord* record = voxSounds_.find(voxSoundId);
        if (record == nullptr) {
            return fail("Hostage sound has an invalid VoxSound ID");
        }
        audio::PcmAudio clip;
        result = soundCatalog_.decode(record->eventName, clip);
        if (!result) {
            return fail(result.message());
        }
        hostageSounds_.emplace(voxSoundId, std::move(clip));
    }
    destroyableHitSounds_.clear();
    for (const game::LevelObjectAsset& object : levelOne_.objects()) {
        if ((object.kind != game::LevelObjectKind::Destroyable &&
             object.kind != game::LevelObjectKind::BrokenBridge) ||
            object.hitVoxSoundId < 0 ||
            destroyableHitSounds_.contains(object.hitVoxSoundId)) {
            continue;
        }
        if (static_cast<std::size_t>(object.hitVoxSoundId) >=
            voxSounds_.records().size()) {
            return fail("Destroyable hit sound has an invalid VoxSound ID");
        }
        const audio::VoxSoundRecord& record =
            voxSounds_.records()[static_cast<std::size_t>(
                object.hitVoxSoundId)];
        audio::PcmAudio clip;
        result = soundCatalog_.decode(record.eventName, clip);
        if (!result) {
            return fail(result.message());
        }
        destroyableHitSounds_.emplace(object.hitVoxSoundId, std::move(clip));
    }
    // Application::SetSlowMotion/ResetSlowMotion use VoxSound IDs 0x186 and
    // 0x187 respectively. Their recovered event names make the native lookup
    // independent of record ordering.
    result = soundCatalog_.decode("SFX_SPIDER_SENSE_IN",
                                  slowMotionEnterSound_);
    if (!result) {
        return fail(result.message());
    }
    result = soundCatalog_.decode("SFX_SPIDER_SENSE_OUT",
                                  slowMotionExitSound_);
    if (!result) {
        return fail(result.message());
    }
    // CTransport::SetState (0x0038b450) plays VoxSound IDs 0x18c/0x18d
    // when the spider-logo wipe closes and opens.
    result = soundCatalog_.decode("SFX_SPIDER_LOGO_IN", transportInSound_);
    if (!result) {
        return fail(result.message());
    }
    result = soundCatalog_.decode("SFX_SPIDER_LOGO_OUT", transportOutSound_);
    if (!result) {
        return fail(result.message());
    }
    result = playerStateConfigs_.load(gameDataRoot);
    if (!result) {
        return fail(result.message());
    }
    if (autoplay) {
        autoplay->recordPlayerStateAssets(playerStateConfigs_);
        const auto joinSoundConfigIds = [](const auto& ids) {
            std::string joined;
            for (const std::int16_t id : ids) {
                if (!joined.empty()) {
                    joined += '|';
                }
                joined += std::to_string(id);
            }
            return joined;
        };
        for (const game::PlayerSoundConfig& config :
             playerStateConfigs_.soundConfigs()) {
            autoplay->recordEvent(
                0, "player_sound_config_asset",
                "id=" + std::to_string(config.id) + ";name=" + config.name +
                    ";playback_type=" +
                    std::to_string(config.playbackType) +
                    ";selection_mode=" +
                    std::to_string(config.selectionMode) +
                    ";target_mode=" + std::to_string(config.targetMode) +
                    ";parameter=" + std::to_string(config.parameter) +
                    ";vox_ids=" + joinSoundConfigIds(config.voxSoundIds) +
                    ";active_emitter_ids=" +
                    joinSoundConfigIds(config.activeEmitterIds));
        }
        for (const game::PlayerStateDefinition& state :
             playerStateConfigs_.states()) {
            if (state.enterSoundConfigIds.empty() &&
                state.frameSoundConfigIds.empty()) {
                continue;
            }
            autoplay->recordEvent(
                0, "player_state_sound_asset",
                "id=" + std::to_string(state.id) + ";name=" + state.name +
                    ";enter_config_ids=" +
                    joinSoundConfigIds(state.enterSoundConfigIds) +
                    ";hit_config_ids=" +
                    joinSoundConfigIds(state.frameSoundConfigIds));
        }
        for (const assets::ColladaAnimationClip& clip :
             levelOne_.player().animationBank.clips()) {
            const assets::Vector3 displacementStart =
                levelOne_.player().animationDisplacement.physicalAt(
                    clip.startMilliseconds);
            const assets::Vector3 displacementMiddle =
                levelOne_.player().animationDisplacement.physicalAt(
                    clip.startMilliseconds +
                    clip.durationMilliseconds() / 2U);
            const assets::Vector3 displacementEnd =
                levelOne_.player().animationDisplacement.physicalAt(
                    clip.endMilliseconds);
            autoplay->recordEvent(
                0, "player_animation_asset",
                "name=" + clip.name +
                    ";start_ms=" +
                    std::to_string(clip.startMilliseconds) +
                    ";end_ms=" + std::to_string(clip.endMilliseconds) +
                    ";duration_ms=" +
                    std::to_string(clip.durationMilliseconds()) +
                    ";dummy_start=" +
                    std::to_string(displacementStart.x) + "|" +
                    std::to_string(displacementStart.y) + "|" +
                    std::to_string(displacementStart.z) +
                    ";dummy_middle=" +
                    std::to_string(displacementMiddle.x) + "|" +
                    std::to_string(displacementMiddle.y) + "|" +
                    std::to_string(displacementMiddle.z) +
                    ";dummy_end=" +
                    std::to_string(displacementEnd.x) + "|" +
                    std::to_string(displacementEnd.y) + "|" +
                    std::to_string(displacementEnd.z));
        }
    }
    // Player state selection now follows the shipped transition database, so
    // any authored state can become reachable without being named in this
    // bootstrap function. Predecode every referenced SoundConfig up front;
    // restricting this list to the first reconstructed moves made newly
    // reachable states such as air-web bind fail during their exit sound.
    std::vector<std::string_view> gameplaySoundStates;
    gameplaySoundStates.reserve(playerStateConfigs_.states().size());
    for (const game::PlayerStateDefinition& state :
         playerStateConfigs_.states()) {
        gameplaySoundStates.push_back(state.name);
    }
    result = playerSounds_.preload(playerStateConfigs_, voxSounds_,
                                   soundCatalog_, gameplaySoundStates,
                                   &nativeRandomizer_);
    if (!result) {
        return fail(result.message());
    }
    result = enemySounds_.preload(
        levelOne_.enemyBehaviorConfigs(), levelOne_.enemySpecialActions(),
        voxSounds_, soundCatalog_, levelEnemyTypes);
    if (!result) {
        return fail(result.message());
    }
    if (hasIntroCinematic) {
        result = introSounds_.preload(levelOne_.introScript(), soundCatalog_);
        if (!result) {
            return fail(result.message());
        }
    }
    std::vector<const game::CinematicScript*> gameplaySoundScripts;
    gameplaySoundScripts.reserve(levelOne_.cinematics().size());
    for (const game::LevelCinematicAsset& cinematic :
         levelOne_.cinematics()) {
        if (cinematic.scriptAvailable) {
            gameplaySoundScripts.push_back(&cinematic.script);
        }
    }
    result = gameplaySounds_.preload(gameplaySoundScripts, soundCatalog_);
    if (!result) {
        return fail(result.message());
    }
    for (const game::LevelTriggerSoundAsset& sound :
         levelOne_.triggerSounds()) {
        if (triggerSoundClips_.contains(sound.eventName)) {
            continue;
        }
        audio::PcmAudio clip;
        result = soundCatalog_.decode(sound.eventName, clip);
        if (!result) {
            return fail(result.message());
        }
        triggerSoundClips_.emplace(sound.eventName, std::move(clip));
    }
    game::CinematicPlayer introEndCommands;
    if (hasIntroCinematic) {
        result = introPlayer_.start(levelOne_.introScript());
        if (!result) {
            return fail(result.message());
        }
        result = introEndCommands.start(levelOne_.introEndScript());
        if (!result) {
            return fail(result.message());
        }
    }
    result = renderer_.uploadLevelOneScene(levelOne_);
    if (!result) {
        return fail(result.message());
    }
    result = gameplayCamera_.bind(levelOne_.cameraAreas(),
                                  levelOne_.player().initialCameraAreaId);
    if (!result) {
        return fail(result.message());
    }
    const game::CameraPose initialCameraPose =
        hasIntroCinematic
            ? levelOne_.introCamera().sample(0)
            : gameplayCamera_.sample(levelOne_.player().position);
    result = renderer_.setCamera(initialCameraPose);
    if (!result) {
        return fail(result.message());
    }
    if (audioEnabled) {
        audio_.setListener(initialCameraPose.position, initialCameraPose.target,
                           initialCameraPose.up);
    }
    result = levelCollision_.build(levelOne_.rooms());
    if (!result) {
        return fail(result.message());
    }
    result = gameplayPlayer_.initialize(levelOne_.player(), &levelCollision_,
                                        &playerStateConfigs_,
                                        levelOne_.webGrabPoints(),
                                        levelOne_.slides(),
                                        levelOne_.waypoints(),
                                        &levelOne_.buttonConfigs(),
                                        &levelOne_.playerHitEffectConfigs(),
                                        levelOne_.playerHitEffects(),
                                        &nativeRandomizer_);
    if (!result) {
        return fail(result.message());
    }
    playerHudHealth_.initialize(gameplayPlayer_.health(),
                                gameplayPlayer_.maximumHealth());
    triggerRuntime_.bind(levelOne_.triggers());
    triggerSoundRuntime_.bind(levelOne_.triggerSounds());
    levelDamageRuntime_.bind(levelOne_.damageVolumes());
    result = restoreRuntime_.bind(levelOne_.restoreTriggers(),
                                  levelOne_.restorePoints());
    if (!result) {
        return fail(result.message());
    }
    levelDeathRuntime_.reset();
    result = enemyRuntime_.initialize(levelOne_, &nativeRandomizer_);
    if (!result) {
        return fail(result.message());
    }
    result = objectRuntime_.initialize(levelOne_);
    if (!result) {
        return fail(result.message());
    }
    result = levelCollision_.updateObjectColliders(objectRuntime_.states());
    if (!result) {
        return fail(result.message());
    }
    result = dropRuntime_.initialize(levelOne_.dropAreas(),
                                     levelOne_.dropObjects());
    if (!result) {
        return fail(result.message());
    }
    result = effectRuntime_.initialize(levelOne_.effects().presets,
                                       &nativeRandomizer_);
    if (!result) {
        return fail(result.message());
    }
    for (const game::LevelPersistentEffectAsset& effect :
         levelOne_.persistentEffectsInSceneOrder()) {
        result = effectRuntime_.addPersistentEffect(
            effect.effectType, effect.position, effect.roomId, effect.visible,
            effect.objectId);
        if (!result) {
            return fail(result.message());
        }
    }
    for (std::int32_t poolIndex = 0; poolIndex < kRocketPoolSize;
         ++poolIndex) {
        result = effectRuntime_.addPersistentEffect(
            "rocket_smoke", {}, -1, false,
            kRocketSmokeEffectSourceBase + poolIndex);
        if (!result) {
            return fail(result.message());
        }
    }
    result = levelBonusRuntime_.initialize(levelOne_.bonuses());
    if (!result) {
        return fail(result.message());
    }
    result = hostageRuntime_.initialize(levelOne_, &objectRuntime_);
    if (!result) {
        return fail(result.message());
    }
    result = hintRuntime_.initialize(levelOne_.hints());
    if (!result) {
        return fail(result.message());
    }
    levelCinematicRuntime_.bind(triggerRuntime_, gameplayCamera_,
                                levelOne_.waypoints(), levelOne_.rooms());
    result = checkPointRuntime_.bind(levelOne_.checkPoints(),
                                     levelOne_.waypoints());
    if (!result) {
        return fail(result.message());
    }
    std::optional<RuntimeCheckPointSnapshot> runtimeCheckPointSnapshot;
    const auto makeRuntimeSnapshot =
        [this](std::int32_t objectId) -> RuntimeCheckPointSnapshot {
            RuntimeCheckPointSnapshot snapshot;
            snapshot.objectId = objectId;
            snapshot.player = gameplayPlayer_;
            snapshot.playerHud = playerHudHealth_;
            snapshot.camera = gameplayCamera_;
            snapshot.triggers = triggerRuntime_;
            snapshot.triggerSounds = triggerSoundRuntime_;
            snapshot.levelCinematic = levelCinematicRuntime_;
            snapshot.enemies = enemyRuntime_;
            snapshot.objects = objectRuntime_;
            snapshot.drops = dropRuntime_;
            snapshot.hints = hintRuntime_;
            snapshot.damage = levelDamageRuntime_;
            snapshot.music = levelMusicRuntime_;
            snapshot.bonuses = levelBonusRuntime_.saveCheckPointState();
            snapshot.hostages = hostageRuntime_;
            snapshot.effects = effectRuntime_.saveCheckPointState();
            return snapshot;
        };
    const auto captureRuntimeCheckPoint =
        [&makeRuntimeSnapshot,
         &runtimeCheckPointSnapshot](std::int32_t objectId) {
            runtimeCheckPointSnapshot = makeRuntimeSnapshot(objectId);
        };
    const auto saveCinematicCheckPoint =
        [this, &captureRuntimeCheckPoint](
            const game::CinematicCommand& command) -> Result {
            if (command.name != "Save") {
                return Result::success();
            }
            const std::int32_t objectId =
                levelCinematicRuntime_.lastCheckpointId();
            Result saveResult = checkPointRuntime_.save(
                objectId,
                gameplayCamera_.currentAreaId(), gameplayPlayer_.position(),
                gameplayPlayer_.facing());
            if (saveResult) {
                captureRuntimeCheckPoint(objectId);
            }
            return saveResult;
        };
    gameplayCinematics_.bind(levelOne_.cinematics());
    quickTimeEvent_.bind(levelOne_.buttonConfigs());
    cinematicUi_.bind(levelOne_.textCatalog());
    result = deathConfirmationRuntime_.bind(levelOne_.textCatalog());
    if (!result) {
        return fail(result.message());
    }
    result = exitMenuRuntime_.bind(levelOne_.textCatalog());
    if (!result) {
        return fail(result.message());
    }
    game::CinematicPlayer introStartCommands;
    if (hasIntroCinematic) {
        result = introStartCommands.start(levelOne_.introStartScript());
        if (!result) {
            return fail(result.message());
        }
        if (autoplay) {
            autoplay->notifyCinematicStarted(1264);
            autoplay->recordEvent(0, "cinematic_start",
                                  "cinematic=1264;phase=intro_start");
        }
        Result introStartCommandResult = Result::success();
        result = introStartCommands.advanceTo(
            introStartCommands.durationMilliseconds(),
            [this, &introStartCommandResult, &autoplay,
             &saveCinematicCheckPoint](
                const game::CinematicThread& thread,
                const game::CinematicCommand& command) {
                if (autoplay) {
                    autoplay->recordCommand(0, 1264, thread.objectId,
                                            command);
                }
                if (introStartCommandResult) {
                    introStartCommandResult =
                        objectRuntime_.applyCinematicCommand(
                            levelOne_, thread, command);
                }
                if (introStartCommandResult) {
                    introStartCommandResult =
                        hintRuntime_.applyCinematicCommand(thread, command);
                }
                if (introStartCommandResult) {
                    introStartCommandResult =
                        levelCinematicRuntime_.applyCommand(thread, command);
                }
                if (introStartCommandResult) {
                    introStartCommandResult =
                        saveCinematicCheckPoint(command);
                }
            });
        if (!result || !introStartCommandResult) {
            return fail(!result ? result.message()
                                : introStartCommandResult.message());
        }
        // Cinematic 1265 is already the explicitly selected intro playback.
        (void)levelCinematicRuntime_.consumeCinematicStartRequests();
        if (autoplay) {
            autoplay->notifyCinematicStarted(1265);
            autoplay->recordEvent(0, "cinematic_start",
                                  "cinematic=1265;phase=intro");
        }
    }

    levelMusicRuntime_.reset();
    constexpr game::LevelMusicTrack calmMusic =
        game::LevelMusicTrack::DowntownCalm;
    constexpr game::LevelMusicTrack mixedMusic =
        game::LevelMusicTrack::DowntownMixed;
    result = playNamedAudio(
        game::LevelMusicRuntime::eventName(calmMusic),
        levelMusicBank_.track(calmMusic), true,
        audio::GameAudioMix::DefaultMusicOutputVolume);
    if (result) {
        // The two downtown files are sample-aligned. Keeping the mixed voice
        // running silently matches the native cursor-preserving transition
        // without restarting the score when combat begins.
        result = playNamedAudio(
            game::LevelMusicRuntime::eventName(mixedMusic),
            levelMusicBank_.track(mixedMusic), true, 0.0F);
    }
    if (!result) {
        return fail(result.message());
    }

    // CLevel::RestartLevel (0x00382b10) calls Application::Load and then
    // queues the player node's linked cinematic. Capture the authored state
    // after cinematic 1264's setup commands but before 1265 advances; that is
    // the native Level 1 reload boundary, not a hand-authored reset.
    const RuntimeCheckPointSnapshot initialLevelSnapshot =
        makeRuntimeSnapshot(-1);
    std::uint64_t introEpochMilliseconds = 0;

    const auto applyMusicTransition =
        [this, &playNamedAudio, &stopNamedAudio, &setNamedAudioVolume](
            const game::LevelMusicTransition& transition) -> Result {
        if (transition.from == transition.to) {
            return Result::success();
        }
        const auto setVolume =
            [&transition, &setNamedAudioVolume](
                game::LevelMusicTrack track, float volume) {
                return setNamedAudioVolume(
                    game::LevelMusicRuntime::eventName(track), volume,
                    transition.fadeMilliseconds);
            };
        Result transitionResult = setVolume(
            game::LevelMusicTrack::DowntownCalm,
            transition.to == game::LevelMusicTrack::DowntownCalm
                ? audio::GameAudioMix::DefaultMusicOutputVolume
                : 0.0F);
        if (transitionResult) {
            transitionResult = setVolume(
                game::LevelMusicTrack::DowntownMixed,
                transition.to == game::LevelMusicTrack::DowntownMixed
                    ? audio::GameAudioMix::DefaultMusicOutputVolume
                    : 0.0F);
        }
        if (!transitionResult) {
            return transitionResult;
        }
        if (transition.from == game::LevelMusicTrack::BossSandman) {
            transitionResult = stopNamedAudio(
                game::LevelMusicRuntime::eventName(transition.from),
                transition.fadeMilliseconds);
        } else if (transition.from == game::LevelMusicTrack::Lose) {
            transitionResult = stopNamedAudio(
                game::LevelMusicRuntime::eventName(transition.from),
                transition.fadeMilliseconds);
        }
        if (!transitionResult ||
            transition.to == game::LevelMusicTrack::DowntownCalm ||
            transition.to == game::LevelMusicTrack::DowntownMixed) {
            return transitionResult;
        }
        return playNamedAudio(
            game::LevelMusicRuntime::eventName(transition.to),
            levelMusicBank_.track(transition.to),
            game::LevelMusicRuntime::loops(transition.to),
            audio::GameAudioMix::DefaultMusicOutputVolume,
            transition.fadeMilliseconds);
    };

    const auto restoreRuntimeSnapshot =
        [this, &stopNamedAudio](const RuntimeCheckPointSnapshot& saved)
            -> Result {
            if (wallWebLoopSoundId_ >= 0) {
                const auto stopped = stopNamedAudio(voxSounds_.find(
                    static_cast<std::uint16_t>(wallWebLoopSoundId_))->eventName);
                wallWebLoopSoundId_ = -1;
                if (!stopped) { return stopped; }
            }
            for (const game::LevelHostageState& hostage :
                 hostageRuntime_.states()) {
                if (hostage.phase == game::HostageRescuePhase::QuickTime) {
                    const audio::VoxSoundRecord* record =
                        voxSounds_.find(static_cast<std::uint16_t>(0x18b));
                    if (record != nullptr) {
                        const Result stopResult =
                            stopNamedAudio(record->eventName);
                        if (!stopResult) {
                            return stopResult;
                        }
                    }
                }
            }
            gameplayPlayer_ = saved.player;
            playerHudHealth_ = saved.playerHud;
            gameplayCamera_ = saved.camera;
            triggerRuntime_ = saved.triggers;
            triggerSoundRuntime_ = saved.triggerSounds;
            levelCinematicRuntime_ = saved.levelCinematic;
            enemyRuntime_ = saved.enemies;
            objectRuntime_ = saved.objects;
            dropRuntime_ = saved.drops;
            hintRuntime_ = saved.hints;
            levelDamageRuntime_ = saved.damage;
            levelMusicRuntime_ = saved.music;
            hostageRuntime_ = saved.hostages;
            hostageRuntime_.clearTransientCues();

            Result loadResult =
                levelBonusRuntime_.loadCheckPointState(saved.bonuses);
            if (loadResult) {
                loadResult =
                    effectRuntime_.loadCheckPointState(saved.effects);
            }
            if (loadResult) {
                loadResult = restoreRuntime_.bind(
                    levelOne_.restoreTriggers(), levelOne_.restorePoints());
            }
            if (!loadResult) {
                return loadResult;
            }

            checkPointRuntime_.resetForLevelRestart();
            levelDeathRuntime_.reset();
            deathConfirmationRuntime_.reset();
            exitMenuRuntime_.reset();
            gameplayCinematics_.bind(levelOne_.cinematics());
            quickTimeEvent_.bind(levelOne_.buttonConfigs());
            cinematicUi_.bind(levelOne_.textCatalog());
            levelCinematicRuntime_.resetTransientForCheckPointLoad();
            enemyRuntime_.resetTransientForCheckPointLoad();
            objectRuntime_.resetTransientForCheckPointLoad();
            dropRuntime_.resetTransientForCheckPointLoad();
            levelDamageRuntime_.resetTransientForCheckPointLoad();
            return Result::success();
        };

    const auto loadRuntimeCheckPoint =
        [this, &runtimeCheckPointSnapshot, &applyMusicTransition,
         &playNamedAudio, &stopNamedAudio,
         &restoreRuntimeSnapshot]() -> Result {
            const std::optional<game::CheckPointRestartPlacement> placement =
                checkPointRuntime_.restartPlacement();
            if (!placement || !runtimeCheckPointSnapshot ||
                runtimeCheckPointSnapshot->objectId != placement->objectId) {
                return Result::failure(
                    "Retry has no serialized last-checkpoint state");
            }

            // CLevel::RestartAtCheckPoint (0x0038344c) performs ResetLevel,
            // Application::LoadCheckPoint, then applies the linked-waypoint /
            // saved-transform / authored-node placement priority.
            const RuntimeCheckPointSnapshot& saved =
                *runtimeCheckPointSnapshot;
            Result loadResult = restoreRuntimeSnapshot(saved);
            if (!loadResult) {
                return loadResult;
            }

            if (placement->cameraAreaId >= 0 &&
                !gameplayCamera_.setCurrentArea(placement->cameraAreaId)) {
                return Result::failure(
                    "Checkpoint references an unknown camera area");
            }
            assets::Vector3 facing = placement->playerFacing;
            const game::CameraPose pose =
                gameplayCamera_.sample(placement->playerPosition);
            if (placement->faceCameraAfterPlacement) {
                const float x = pose.target.x - pose.position.x;
                const float y = pose.target.y - pose.position.y;
                const float length = std::sqrt(x * x + y * y);
                if (length > std::numeric_limits<float>::epsilon()) {
                    facing = {x / length, y / length, 0.0F};
                }
            }
            gameplayPlayer_.loadCheckPointAt(placement->playerPosition,
                                             facing);

            for (const game::LevelTriggerSoundAsset& trigger :
                 levelOne_.triggerSounds()) {
                loadResult = stopNamedAudio(
                    "TriggerSound:" + std::to_string(trigger.objectId), 0,
                    trigger.eventName);
                if (!loadResult) {
                    return loadResult;
                }
            }
            for (const game::TriggerSoundEvent& event :
                 triggerSoundRuntime_.checkPointLoadEvents()) {
                const auto clip = triggerSoundClips_.find(event.eventName);
                if (clip == triggerSoundClips_.end()) {
                    return Result::failure(
                        "Checkpoint TriggerSound clip was not preloaded");
                }
                const audio::VoxSoundRecord* record =
                    voxSounds_.find(event.eventName);
                const float volume =
                    audio::GameAudioMix::DefaultSoundEffectsGroupVolume *
                    (record != nullptr && record->id == 0xa1 ? 0.5F : 1.0F);
                loadResult = playNamedAudio(
                    "TriggerSound:" + std::to_string(event.triggerId),
                    clip->second, true, volume, 0, event.eventName);
                if (!loadResult) {
                    return loadResult;
                }
            }

            const game::LevelMusicTrack restoredMusic =
                levelMusicRuntime_.currentTrack();
            loadResult = applyMusicTransition(
                {game::LevelMusicTrack::Lose, restoredMusic, 500});
            if (!loadResult) {
                return loadResult;
            }
            renderer_.setCameraAreaRoomVisibility(
                gameplayCamera_.mustInvisibleRooms(),
                gameplayCamera_.mustVisibleRooms());
            renderer_.setCinematicVisibleRooms(
                levelCinematicRuntime_.forcedVisibleRooms());
            return renderer_.setCamera(
                gameplayCamera_.sample(gameplayPlayer_.position()));
        };

    const auto restartRuntimeLevel =
        [this, &autoplay, &traceTimeMilliseconds, &hasIntroCinematic,
         &initialLevelSnapshot, &restoreRuntimeSnapshot,
         &applyMusicTransition, &stopNamedAudio, &introEndCommands,
         &introEpochMilliseconds](std::uint64_t restartTimeMilliseconds)
        -> Result {
            // CLevel::RestartLevel (0x00382b10): reset checkpoints, reload the
            // authored level, initialize its camera, then enqueue the player
            // node's linked cinematic. Level 1 links directly to 1265, so the
            // 1264 setup wrapper is not replayed on this branch.
            const game::LevelMusicTrack departingMusic =
                levelMusicRuntime_.currentTrack();
            Result restartResult =
                restoreRuntimeSnapshot(initialLevelSnapshot);
            if (!restartResult) {
                return restartResult;
            }

            for (const game::LevelTriggerSoundAsset& trigger :
                 levelOne_.triggerSounds()) {
                restartResult = stopNamedAudio(
                    "TriggerSound:" + std::to_string(trigger.objectId), 0,
                    trigger.eventName);
                if (!restartResult) {
                    return restartResult;
                }
            }
            restartResult = applyMusicTransition(
                {departingMusic, levelMusicRuntime_.currentTrack(), 500});
            if (!restartResult) {
                return restartResult;
            }

            introEpochMilliseconds = restartTimeMilliseconds;
            if (hasIntroCinematic) {
                restartResult = introPlayer_.start(levelOne_.introScript());
                if (restartResult) {
                    restartResult =
                        introEndCommands.start(levelOne_.introEndScript());
                }
                if (!restartResult) {
                    return restartResult;
                }
                if (autoplay) {
                    autoplay->notifyCinematicStarted(
                        levelOne_.player().linkedCinematicId);
                    autoplay->recordEvent(
                        traceTimeMilliseconds, "cinematic_start",
                        "cinematic=" +
                            std::to_string(
                                levelOne_.player().linkedCinematicId) +
                            ";phase=level_restart");
                }
            } else if (levelOne_.player().linkedCinematicId >= 0) {
                restartResult = gameplayCinematics_.start(
                    levelOne_.player().linkedCinematicId);
                if (!restartResult) {
                    return restartResult;
                }
                if (autoplay) {
                    autoplay->notifyCinematicStarted(
                        levelOne_.player().linkedCinematicId);
                    autoplay->recordEvent(
                        traceTimeMilliseconds, "cinematic_start",
                        "cinematic=" +
                            std::to_string(
                                levelOne_.player().linkedCinematicId) +
                            ";phase=level_restart");
                }
            }

            renderer_.setCameraAreaRoomVisibility(
                gameplayCamera_.mustInvisibleRooms(),
                gameplayCamera_.mustVisibleRooms());
            renderer_.setCinematicVisibleRooms(
                levelCinematicRuntime_.forcedVisibleRooms());
            return renderer_.setCamera(
                hasIntroCinematic
                    ? levelOne_.introCamera().sample(0)
                    : gameplayCamera_.sample(gameplayPlayer_.position()));
        };

    const auto releaseGameplayCollada =
        [this](const game::LevelCinematicAsset& cinematic,
               std::uint32_t elapsedMilliseconds) {
            if (!cinematic.hasColladaPlayback()) {
                return;
            }
            // CCinematicThread::UnUseDAECamera/UnUseDAEAnim restore the
            // ordinary UI and original actors on completion and QTE handoff.
            levelCinematicRuntime_.setColladaMovieUi(false);
            cinematicUi_.setColladaMovieUi(false);
            for (const game::CinematicActorAsset& actor : cinematic.actors) {
                if (actor.animationStartMilliseconds <= elapsedMilliseconds) {
                    enemyRuntime_.endColladaAnimation(actor.objectId);
                    objectRuntime_.endColladaAnimation(actor.objectId);
                }
            }
        };

    const auto startGameplayCinematic =
        [this, &autoplay, &traceTimeMilliseconds](
            std::int32_t cinematicId) -> Result {
        const bool alreadyActive = gameplayCinematics_.active(cinematicId);
        Result startResult = gameplayCinematics_.start(cinematicId);
        if (startResult && autoplay && !alreadyActive) {
            autoplay->notifyCinematicStarted(cinematicId);
            autoplay->recordEvent(traceTimeMilliseconds, "cinematic_start",
                                  "cinematic=" +
                                      std::to_string(cinematicId));
        }
        return startResult;
    };
    if (!hasIntroCinematic && levelOne_.player().linkedCinematicId >= 0) {
        // Later levels author their opening as an ordinary CFF cinematic
        // graph (camera-thread ChangeCamera commands) rather than Level 1's
        // dedicated Collada-camera 1264/1265/1266 wrapper. The SpiderMan
        // scene node is still the source of the opening ID; seed that asset
        // through the normal scheduler so its authored StartCinematic
        // handoffs, UI lock, sounds, and object commands all remain live.
        result = startGameplayCinematic(
            levelOne_.player().linkedCinematicId);
        if (!result) {
            return fail(result.message());
        }
    }
    const auto diagnosticCinematicId = [this]() -> std::int32_t {
        if (const game::GameplayCinematicPlayback* presentation =
                gameplayCinematics_.presentation()) {
            return presentation->asset->objectId;
        }
        const auto activeIds = gameplayCinematics_.activeIds();
        return activeIds.empty() ? -1 : activeIds.front();
    };

    const auto introStart = std::chrono::steady_clock::now();
    const auto nativeTickBucket = [](std::chrono::steady_clock::time_point time) {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                time.time_since_epoch())
                .count()) /
            kNativeGameplayTickMilliseconds;
    };
    std::uint64_t previousNativeTickBucket = nativeTickBucket(introStart);
    std::uint64_t pendingNativeTickBucket = previousNativeTickBucket;
    std::uint32_t pendingNativeUpdates = 0;
    const auto playGameplaySound =
        [&playAudio](std::int16_t, std::string_view eventName,
                     const audio::PcmAudio& clip, bool loop) {
            return playAudio(clip, loop, eventName);
        };
    const auto stopPlayerStateSound =
        [&stopNamedAudio](std::int16_t, std::string_view eventName) {
            return stopNamedAudio(eventName);
        };
    const auto cinematicSoundPosition =
        [this](std::int32_t objectId)
        -> std::optional<assets::Vector3> {
        if (objectId == levelOne_.player().objectId) {
            return gameplayPlayer_.position();
        }
        if (const game::LevelEnemyState* enemy =
                enemyRuntime_.find(objectId)) {
            return enemy->position;
        }
        if (const game::LevelObjectState* object =
                objectRuntime_.find(objectId)) {
            return object->position;
        }
        return std::nullopt;
    };
    const auto playSpatialSound =
        [this, &cinematicSoundPosition, &playSpatialAudio](
            std::int32_t objectId, std::string_view eventName,
            const audio::PcmAudio& clip, bool loop) -> Result {
        const auto position = cinematicSoundPosition(objectId);
        if (!position) {
            // SoundControlCmd skips Play3D when its cinematic thread has no
            // scene object. Enemy cues always provide a live source ID.
            return Result::success();
        }
        const audio::VoxSoundRecord* record = voxSounds_.find(eventName);
        if (record == nullptr) {
            return Result::failure(
                "Spatial sound has no VoxSound record");
        }
        return playSpatialAudio(
            eventName, clip,
            {*position, record->minimumDistance, record->maximumDistance,
             record->distanceCullingEnabled},
            loop);
    };
    const auto dispatchObjectEvents =
        [this, &autoplay, &traceTimeMilliseconds, &playSpatialSound,
         &playNamedAudio,
         &startGameplayCinematic]() -> Result {
        for (const game::LevelObjectEvent& event :
             objectRuntime_.consumeEvents()) {
            if (autoplay) {
                autoplay->recordEvent(
                    traceTimeMilliseconds, "object_event",
                    "object=" + std::to_string(event.objectId) +
                        ";kind=" +
                        std::to_string(static_cast<std::int32_t>(event.kind)) +
                        ";vox=" + std::to_string(event.voxSoundId) +
                        ";effect=" + event.effectType +
                        ";cinematic=" +
                        std::to_string(event.cinematicId) +
                        ";collectible_index=" +
                        std::to_string(event.collectibleIndex) +
                        ";level_string=" + event.levelStringId +
                        ";bridge_state=" + std::to_string(event.bridgeState));
            }
            if (event.kind == game::LevelObjectEventKind::BridgeStateChanged) {
                // CBrokenBridge::SetState (0x00300dec), literal pools at
                // 0x00301188/0x0030118c/0x00301190 and 0x00301274.
                switch (event.bridgeState) {
                case 4: case 7:
                    levelCinematicRuntime_.startCameraShake(3.0F, 12, {5.0F, 5.0F, 5.0F});
                    break;
                case 5: case 8:
                    levelCinematicRuntime_.startCameraShake(3.0F, 2000, {0.3F, 0.3F, 0.3F});
                    break;
                case 6: case 9:
                    levelCinematicRuntime_.startCameraShake(3.0F, 2000, {1.0F, 1.0F, 1.0F});
                    break;
                case 10:
                    levelCinematicRuntime_.stopCameraShake();
                    break;
                default: break;
                }
            }
            if ((event.kind == game::LevelObjectEventKind::Hit ||
                 event.kind == game::LevelObjectEventKind::BridgeDrop) &&
                event.voxSoundId >= 0) {
                const auto clip = destroyableHitSounds_.find(event.voxSoundId);
                if (clip == destroyableHitSounds_.end() ||
                    static_cast<std::size_t>(event.voxSoundId) >=
                        voxSounds_.records().size()) {
                    return Result::failure(
                        "Destroyable hit event has no preloaded sound");
                }
                const audio::VoxSoundRecord& record =
                    voxSounds_.records()[static_cast<std::size_t>(
                        event.voxSoundId)];
                Result eventResult = playSpatialSound(
                    event.objectId, record.eventName, clip->second, false);
                if (!eventResult) {
                    return eventResult;
                }
            }
            if ((event.kind == game::LevelObjectEventKind::Destroyed ||
                 event.kind == game::LevelObjectEventKind::BridgeDrop) &&
                !event.effectType.empty()) {
                Result eventResult = effectRuntime_.playEffect(
                    event.effectType, event.position, event.roomId);
                if (!eventResult) {
                    return eventResult;
                }
            }
            if (event.kind == game::LevelObjectEventKind::Destroyed &&
                event.cinematicId >= 0) {
                Result eventResult =
                    startGameplayCinematic(event.cinematicId);
                if (!eventResult) {
                    return eventResult;
                }
            }
            if (event.kind ==
                game::LevelObjectEventKind::ComicCollected) {
                if (event.voxSoundId < 0 ||
                    static_cast<std::size_t>(event.voxSoundId) >=
                        voxSounds_.records().size()) {
                    return Result::failure(
                        "Comic collection has no VoxSound record");
                }
                const audio::VoxSoundRecord& record =
                    voxSounds_.records()[static_cast<std::size_t>(
                        event.voxSoundId)];
                Result eventResult = playNamedAudio(
                    record.eventName, comicCollectSound_, false);
                if (eventResult) {
                    eventResult =
                        cinematicUi_.showComicCover(event.collectibleIndex);
                }
                if (!eventResult) {
                    return eventResult;
                }
            }
        }
        return Result::success();
    };
    float scaledDeltaRemainderMilliseconds = 0.0F;
    std::uint64_t syntheticElapsedMilliseconds =
        autoplay ? autoplay->startTimeMilliseconds() : 0;
    std::uint64_t accumulatedGameMilliseconds = 0;
    std::uint64_t frameIndex = 0;
    bool gameplayVisibilityInitialized = false;
    bool exitAfterPresent = false;
    const auto shouldRunFrame = [&] {
        return autoplay ? !autoplay->complete() && !autoplay->failed()
                        : window_.pumpMessages();
    };
    while (shouldRunFrame()) {
        std::uint32_t realDeltaMilliseconds = 0;
        bool presentThisFrame = true;
        if (autoplay) {
            realDeltaMilliseconds = autoplay->fixedStepMilliseconds();
            syntheticElapsedMilliseconds += realDeltaMilliseconds;
        } else {
            if (pendingNativeUpdates == 0) {
                auto frameTime = std::chrono::steady_clock::now();
                std::uint64_t currentTickBucket = nativeTickBucket(frameTime);
                if (currentTickBucket == previousNativeTickBucket) {
                    const auto nextNativeTick =
                        std::chrono::steady_clock::time_point(
                            std::chrono::milliseconds(
                                (previousNativeTickBucket + 1U) *
                                kNativeGameplayTickMilliseconds));
                    std::this_thread::sleep_until(nextNativeTick);
                    frameTime = std::chrono::steady_clock::now();
                    currentTickBucket = nativeTickBucket(frameTime);
                }

                // Application::Update (0x003e1624) computes the number of
                // crossed target-frame buckets, caps it at two, performs that
                // many distinct 50 ms updates, draws once, and then records
                // the current real-time bucket. Keep the updates distinct so
                // keypad press lifetimes, hit frames, and combo transitions
                // are never collapsed into one oversized simulation step.
                pendingNativeUpdates = static_cast<std::uint32_t>(
                    std::min<std::uint64_t>(
                        currentTickBucket - previousNativeTickBucket, 2U));
                pendingNativeTickBucket = currentTickBucket;
            }

            --pendingNativeUpdates;
            realDeltaMilliseconds = kNativeGameplayTickMilliseconds;
            syntheticElapsedMilliseconds += realDeltaMilliseconds;
            presentThisFrame = pendingNativeUpdates == 0;
            if (presentThisFrame) {
                // The native stores the sampled real time after its draw,
                // intentionally dropping backlog beyond its two-update cap.
                previousNativeTickBucket = pendingNativeTickBucket;
            }
        }
        traceTimeMilliseconds = syntheticElapsedMilliseconds;
        scaledDeltaRemainderMilliseconds +=
            levelCinematicRuntime_.updateSlowMotion(
                static_cast<float>(realDeltaMilliseconds));
        const auto unpausedGameDeltaMilliseconds = static_cast<std::uint32_t>(
            std::max(0.0F, std::floor(scaledDeltaRemainderMilliseconds)));
        scaledDeltaRemainderMilliseconds -=
            static_cast<float>(unpausedGameDeltaMilliseconds);
        // CTutorial::AddInfo marks Timer < 1 tutorials as modal. The native
        // CLevel::Update updates that tutorial, then returns before camera,
        // player, object, cinematic, enemy, and physics simulation while it
        // remains visible. Preserve that stopped-world handoff here; this is
        // especially important when the red-orb tutorial is entered mid-jump.
        const bool modalTutorialActive =
            cinematicUi_.modalTutorialVisible();
        const std::uint32_t gameDeltaMilliseconds =
            modalTutorialActive ? 0U : unpausedGameDeltaMilliseconds;
        accumulatedGameMilliseconds += gameDeltaMilliseconds;
        // CGameCamera::UpdateShake counts fixed Application update calls and
        // is therefore paced by real 50 ms ticks, not the scaled game delta.
        levelCinematicRuntime_.advanceCameraShake(realDeltaMilliseconds);
        std::int32_t supportingObjectId = -1;
        float supportingHeight = 0.0F;
        std::optional<game::LevelObjectSupportPose> supportingPose;
        if (levelCollision_.groundHeight(
                gameplayPlayer_.position(), 10.0F, 10.0F,
                supportingHeight, 0U, &supportingObjectId) &&
            supportingObjectId >= 0) {
            supportingPose = objectRuntime_.supportPose(supportingObjectId);
        }
        objectRuntime_.advanceAnimations(gameDeltaMilliseconds);
        objectRuntime_.updateBrokenBridges(gameplayPlayer_.position(),
                                          gameDeltaMilliseconds);
        levelCinematicRuntime_.advanceRoomMotion(gameDeltaMilliseconds);
        levelCinematicRuntime_.advanceTransport(gameDeltaMilliseconds);
        result = levelCollision_.updateRoomPositions(
            levelCinematicRuntime_.roomMotionStates());
        if (!result) {
            return fail(result.message());
        }
        result = levelCollision_.updateObjectColliders(
            objectRuntime_.states());
        if (!result) {
            return fail(result.message());
        }
        if (supportingPose.has_value()) {
            const assets::Vector3 carry = objectRuntime_.supportMotionDelta(
                *supportingPose, gameplayPlayer_.position());
            if (carry.x != 0.0F || carry.y != 0.0F || carry.z != 0.0F) {
                gameplayPlayer_.applySupportingBodyMotion(carry);
                if (autoplay) {
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds, "support_motion",
                        "object=" +
                            std::to_string(supportingPose->objectId) +
                            ";dx=" + std::to_string(carry.x) +
                            ";dy=" + std::to_string(carry.y) +
                            ";dz=" + std::to_string(carry.z));
                }
            }
        }
        if (audioEnabled) {
            audio_.update();
        }
        const std::uint32_t previousExitMenuState =
            exitMenuRuntime_.state();
        const bool mainMenuWasRequested =
            exitMenuRuntime_.mainMenuRequested();
        exitMenuRuntime_.update(realDeltaMilliseconds);
        if (exitMenuRuntime_.active() && previousExitMenuState < 16 &&
            exitMenuRuntime_.state() >= 16) {
            // GS_ExitMenu state 16 calls ClearStateWithoutCurrent before it
            // creates GS_MainMenu. The death/confirmation parents therefore
            // stop updating and rendering at this exact transition.
            levelDeathRuntime_.reset();
            deathConfirmationRuntime_.reset();
        }
        if (!mainMenuWasRequested &&
            exitMenuRuntime_.mainMenuRequested()) {
            if (autoplay) {
                autoplay->recordEvent(
                    syntheticElapsedMilliseconds, "main_menu_request",
                    "source=GS_ExitMenu;mode=3;state=20");
            }
            exitAfterPresent = true;
        }
        keyRouter_.beginFrame();
        if (!autoplay) {
            controller_.poll(
                [this](const reconstructed::XperiaKeyEvent& event) {
                    keyRouter_.route(
                        event,
                        deathConfirmationRuntime_.active()
                            ? reconstructed::InputContext::Menu
                            : reconstructed::InputContext::Gameplay);
                });
        }
        const auto introDuration =
            hasIntroCinematic
                ? levelOne_.introColladaDurationMilliseconds()
                : 0U;
        const std::uint64_t introElapsedMilliseconds =
            syntheticElapsedMilliseconds >= introEpochMilliseconds
                ? syntheticElapsedMilliseconds - introEpochMilliseconds
                : 0;
        auto timestamp = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(introElapsedMilliseconds,
                                    introDuration));
        bool gameplayActive = introElapsedMilliseconds >= introDuration;
        if (gameplayActive && !gameplayVisibilityInitialized) {
            renderer_.setCameraAreaRoomVisibility(
                gameplayCamera_.mustInvisibleRooms(),
                gameplayCamera_.mustVisibleRooms());
            result = renderer_.setCamera(
                gameplayCamera_.sample(gameplayPlayer_.position()));
            if (!result) {
                return fail(result.message());
            }
            gameplayVisibilityInitialized = true;
        }
        const bool harnessControlsEnabled =
            gameplayActive && levelCinematicRuntime_.controlsEnabled() &&
            !cinematicUi_.modalTutorialVisible() &&
            !gameplayPlayer_.cinematicDriven() &&
            !hostageRuntime_.ownsPlayerControl() &&
            !levelDeathRuntime_.active() && !exitMenuRuntime_.active() &&
            !restoreRuntime_.active() &&
            !quickTimeEvent_.active() &&
            !enemyRuntime_.rhinoQuickTimeAction().active() &&
            !gameplayCinematics_.hasActiveColladaPlayback();
        // Native Player::PreUpdate advances animation and snapshots the new
        // state frame before UpdateKeyTrigger reads this tick's buttons.
        // GameplayPlayer uses this pending delta for those predicates while
        // leaving a newly selected attack at frame zero until the next tick.
        gameplayPlayer_.prepareInputFrame(gameDeltaMilliseconds);
        diagnostics::AutoplayFrameInput autoplayInput;
        if (autoplay) {
            const game::CameraPose harnessCamera = gameplayActive
                ? gameplayCamera_.sample(gameplayPlayer_.position())
                : levelOne_.introCamera().sample(timestamp);
            const float harnessAspect =
                static_cast<float>(autoplay->renderWidth()) /
                autoplay->renderHeight();
            if (gameplayActive) {
                gameplayPlayer_.setWebGrabViewContext(
                    harnessCamera, harnessAspect,
                    renderer_.roomVisibility());
            }
            const std::int32_t harnessWebGrabPointId = gameplayActive
                ? gameplayPlayer_.availableWebGrabPointObjectId()
                : -1;
            autoplayInput = autoplay->update(
                {frameIndex,
                 syntheticElapsedMilliseconds,
                  accumulatedGameMilliseconds,
                  levelCinematicRuntime_.slowMotionDenominator(),
                  gameplayActive,
                  harnessControlsEnabled,
                  levelCinematicRuntime_.attributionEnabled(),
                  quickTimeEvent_.active() || gameplayPlayer_.wallWeb().promptActive() ||
                     hostageRuntime_.quickTimeActive() ||
                     enemyRuntime_.rhinoQuickTimeAction()
                         .buttonPromptActive(),
                 cinematicUi_.tutorialVisible(),
                 levelDeathRuntime_.active(),
                 levelDeathRuntime_.blackOverlayAlpha(),
                 deathConfirmationRuntime_.active(),
                 deathConfirmationRuntime_.selection(),
                 exitMenuRuntime_.active(),
                 exitMenuRuntime_.state(),
                 exitMenuRuntime_.mainMenuRequested(),
                  restoreRuntime_.active(),
                  restoreRuntime_.blackOverlayAlpha(),
                  gameplayCamera_.currentAreaId(),
                  checkPointRuntime_.lastCheckPointId(),
                  diagnosticCinematicId(),
                 gameplayCinematics_.activeIds(),
                 gameplayPlayer_.position(),
                 gameplayPlayer_.renderPosition(),
                 gameplayPlayer_.attackRootTranslation(),
                 gameplayPlayer_.facing(),
                 gameplayPlayer_.health(),
                 gameplayPlayer_.webPower(),
                 gameplayPlayer_.skillPoints(),
                 gameplayPlayer_.comboScore(),
                 gameplayPlayer_.activeAnimation(),
                 gameplayPlayer_.animationTimeMilliseconds(),
                 gameplayPlayer_.activeStateId(),
                 gameplayPlayer_.activeStateName(),
                 gameplayPlayer_.punchTransitionReadyAfterImpact(),
                 gameplayPlayer_.punchAttackTransitionReady(),
                 gameplayPlayer_.jumpAttackTransitionReady(),
                 gameplayPlayer_.jumpReleaseAttackTransitionReady(),
                 gameplayPlayer_.webAttackTransitionReady(),
                 gameplayPlayer_.webHeldAttackTransitionReady(),
                 gameplayPlayer_.hitEffects(),
                 gameplayPlayer_.onWall(),
                 gameplayPlayer_.cinematicMotionActive(),
                 gameplayPlayer_.cinematicMotionElapsedMilliseconds(),
                 gameplayPlayer_.cinematicMotionDurationMilliseconds(),
                 harnessCamera,
                 renderer_.roomVisibility(),
                 levelCinematicRuntime_.roomMotionStates(),
                 enemyRuntime_.states(),
                 enemyRuntime_.meleeEngagerObjectId(),
                 enemyRuntime_.meleeEngagementCooldownMilliseconds(),
                 enemyRuntime_.nativeRandomState(),
                 enemyRuntime_.gunLines(),
                 enemyRuntime_.rockets(),
                 enemyRuntime_.molotovs(),
                 enemyRuntime_.boomerangs(),
                 enemyRuntime_.thunderclaps(),
                 enemyRuntime_.electricPosts(),
                 enemyRuntime_.electroBursts(),
                 objectRuntime_.states(),
                 levelBonusRuntime_.states(),
                 hostageRuntime_.states(),
                 dropRuntime_.states(),
                 hostageRuntime_.quickTimeActive(),
                 enemyRuntime_.rhinoQuickTimeAction().active(),
                 enemyRuntime_.rhinoQuickTimeAction().actionStateId(),
                 enemyRuntime_.rhinoQuickTimeAction()
                     .stateElapsedMilliseconds(),
                 enemyRuntime_.rhinoQuickTimeAction()
                     .buttonElapsedMilliseconds(),
                 enemyRuntime_.rhinoQuickTimeAction()
                     .buttonDurationMilliseconds(),
                 enemyRuntime_.rhinoQuickTimeAction()
                     .completedActionCount(),
                 enemyRuntime_.rhinoQuickTimeAction().requiredActionCount(),
                 enemyRuntime_.rhinoQuickTimeAction().buttonProgress(),
                 levelCinematicRuntime_.bossProgress().visible,
                 levelCinematicRuntime_.bossProgress().closing,
                 levelCinematicRuntime_.bossProgress().failed,
                 levelCinematicRuntime_.bossProgress().bossObjectId,
                 levelCinematicRuntime_.bossProgress().currentDistance,
                 levelCinematicRuntime_.bossProgress().failureDistance,
                 levelCinematicRuntime_.bossProgressRatio(),
                 levelCinematicRuntime_.transport().state,
                 levelCinematicRuntime_.transport().elapsedMilliseconds,
                 levelCinematicRuntime_.transport().scale,
                 levelCinematicRuntime_.levelEnded(),
                 levelCinematicRuntime_.gameEnded(),
                 cinematicUi_.letterboxVisible(),
                 gameplayPlayer_.webLineActive(),
                 gameplayPlayer_.webLineCount(),
                 gameplayPlayer_.webLineTargetObjectId(),
                 gameplayPlayer_.wallWeb().phase(), gameplayPlayer_.wallWeb().targetObjectId(),
                 gameplayPlayer_.wallWeb().angle(), gameplayPlayer_.wallWeb().completedActionCount(),
                 gameplayPlayer_.wallWeb().lineActive(),
                 harnessWebGrabPointId,
                 hintRuntime_.combatSenseCueVisible()});
            if (autoplayInput.teleport) {
                gameplayPlayer_.restoreAt(autoplayInput.teleport->position,
                                          autoplayInput.teleport->facing);
                const bool cameraRelocated =
                    gameplayCamera_.relocateToContainingArea(
                        autoplayInput.teleport->position);
                autoplay->recordEvent(
                    syntheticElapsedMilliseconds, "teleport",
                    "x=" + std::to_string(autoplayInput.teleport->position.x) +
                        ";y=" +
                        std::to_string(autoplayInput.teleport->position.y) +
                        ";z=" +
                        std::to_string(autoplayInput.teleport->position.z) +
                        ";camera_area=" +
                        std::to_string(gameplayCamera_.currentAreaId()) +
                        ";camera_relocated=" +
                        (cameraRelocated ? "1" : "0"));
            }
            for (const std::int32_t skillId :
                 autoplayInput.skillUnlockRequests) {
                game::CinematicCommand unlock;
                unlock.id = 96;
                unlock.name = "Unlock";
                unlock.attributes.push_back(
                    {"string", "$SkillID", std::to_string(skillId)});
                result = levelCinematicRuntime_.applyCommand(unlock);
                if (!result) {
                    return fail(result.message());
                }
                autoplay->recordEvent(
                    syntheticElapsedMilliseconds,
                    "diagnostic_skill_unlock",
                    "skill=" + std::to_string(skillId) +
                        ";source=CCinematicThread::OnUnlock");
            }
            for (const std::int32_t cinematicId :
                 autoplayInput.cinematicStartRequests) {
                result = startGameplayCinematic(cinematicId);
                if (!result) {
                    return fail(result.message());
                }
            }
            for (const diagnostics::AutoplayEnemyAiOverride& overrideState :
                 autoplayInput.enemyAiOverrides) {
                const bool applied = enemyRuntime_.setDiagnosticAiEnabled(
                    overrideState.objectId, overrideState.enabled,
                    overrideState.forcePlayerDetected);
                autoplay->recordEvent(
                    syntheticElapsedMilliseconds, "diagnostic_enemy_ai",
                    "enemy=" + std::to_string(overrideState.objectId) +
                        ";enabled=" +
                        std::to_string(overrideState.enabled) +
                        ";force_detect=" +
                        std::to_string(
                            overrideState.forcePlayerDetected) +
                        ";applied=" + std::to_string(applied));
                if (!applied) {
                    return fail(
                        "Autoplay could not apply diagnostic enemy AI state");
                }
            }
            for (const diagnostics::AutoplayEnemyPhysicsOverride& overrideState :
                 autoplayInput.enemyPhysicsOverrides) {
                const bool applied = enemyRuntime_.setDiagnosticPhysicsActive(
                    overrideState.objectId, overrideState.enabled);
                autoplay->recordEvent(
                    syntheticElapsedMilliseconds,
                    "diagnostic_enemy_physics",
                    "enemy=" + std::to_string(overrideState.objectId) +
                        ";enabled=" +
                        std::to_string(overrideState.enabled) +
                        ";applied=" + std::to_string(applied));
                if (!applied) {
                    return fail(
                        "Autoplay could not apply diagnostic enemy physics state");
                }
            }
            for (const diagnostics::AutoplayEnemyDamage& damage :
                 autoplayInput.enemyDamage) {
                const bool applied = enemyRuntime_.applyDiagnosticDamage(
                    damage.objectId, damage.damage);
                autoplay->recordEvent(
                    syntheticElapsedMilliseconds,
                    "diagnostic_enemy_damage",
                    "enemy=" + std::to_string(damage.objectId) +
                        ";damage=" + std::to_string(damage.damage) +
                        ";applied=" + std::to_string(applied));
                if (!applied) {
                    return fail(
                        "Autoplay could not apply diagnostic enemy damage");
                }
            }
        }
        Result soundResult = Result::success();
        Result uiResult = Result::success();
        const bool fastForwardingIntro =
            autoplay && frameIndex == 0 &&
            autoplay->startTimeMilliseconds() != 0;
        if (hasIntroCinematic) {
            result = introPlayer_.advanceTo(
                timestamp,
            [this, &soundResult, &uiResult, &autoplay,
             &traceTimeMilliseconds, &playNamedAudio,
             &stopNamedAudio, &saveCinematicCheckPoint,
             fastForwardingIntro, timestamp](
                        const game::CinematicThread& thread,
                        const game::CinematicCommand& command) {
                if (!soundResult || !uiResult) {
                    return;
                }
                if (autoplay) {
                    autoplay->recordCommand(traceTimeMilliseconds, 1265,
                                            thread.objectId, command);
                }
                uiResult = objectRuntime_.applyCinematicCommand(
                    levelOne_, thread, command);
                if (!uiResult) {
                    return;
                }
                uiResult = hintRuntime_.applyCinematicCommand(thread, command);
                if (!uiResult) {
                    return;
                }
                uiResult =
                    levelCinematicRuntime_.applyCommand(thread, command);
                if (!uiResult) {
                    return;
                }
                uiResult = saveCinematicCheckPoint(command);
                if (!uiResult) {
                    return;
                }
                uiResult = fastForwardingIntro
                               ? restoreFastForwardedCinematicUi(
                                     cinematicUi_, command, timestamp)
                               : cinematicUi_.applyCommand(command);
                if (!fastForwardingIntro) {
                    soundResult = introSounds_.dispatch(
                        command,
                        [&playNamedAudio](std::string_view eventName,
                                          const audio::PcmAudio& clip,
                                          bool loop) {
                            return playNamedAudio(eventName, clip, loop);
                        },
                        [&stopNamedAudio](std::string_view eventName) {
                            return stopNamedAudio(eventName);
                        });
                }
                });
            if (!result) {
                return fail(result.message());
            }
        }
        if (!soundResult || !uiResult) {
            return fail(!soundResult ? soundResult.message()
                                     : uiResult.message());
        }
        if (hasIntroCinematic && gameplayActive &&
            !introEndCommands.finished()) {
            // The dedicated intro player has the same AnimCamera::UnUse ->
            // OnOffDaeMovieUI(false) teardown as scheduler-owned movies.
            // This is also required after start_time_ms fast-forward: the
            // retained PlayDAECamera command must not leave controls/HUD off.
            levelCinematicRuntime_.setColladaMovieUi(false);
            cinematicUi_.setColladaMovieUi(false);
            if (autoplay) {
                autoplay->recordEvent(traceTimeMilliseconds,
                                      "intro_movie_ui_released",
                                      "cinematic=1265;successor=1266");
            }
            // PlayDAECamera names cinematic 1266 as its successor. Execute the
            // authored wrapper instead of hard-coding its room-visibility
            // side effect, preserving the same graph used by gameplay
            // cinematics and making its provenance observable.
            if (autoplay) {
                autoplay->notifyCinematicStarted(1266);
                autoplay->recordEvent(traceTimeMilliseconds,
                                      "cinematic_start",
                                      "cinematic=1266;phase=intro_end");
            }
            Result introEndCommandResult = Result::success();
            result = introEndCommands.advanceTo(
                introEndCommands.durationMilliseconds(),
                [this, &introEndCommandResult, &autoplay,
                 &traceTimeMilliseconds, &saveCinematicCheckPoint](
                    const game::CinematicThread& thread,
                    const game::CinematicCommand& command) {
                    if (autoplay) {
                        autoplay->recordCommand(traceTimeMilliseconds, 1266,
                                                thread.objectId, command);
                    }
                    if (introEndCommandResult) {
                        introEndCommandResult =
                            objectRuntime_.applyCinematicCommand(
                                levelOne_, thread, command);
                    }
                    if (introEndCommandResult) {
                        introEndCommandResult =
                            hintRuntime_.applyCinematicCommand(thread,
                                                                command);
                    }
                    if (introEndCommandResult) {
                        introEndCommandResult =
                            levelCinematicRuntime_.applyCommand(thread,
                                                                command);
                    }
                    if (introEndCommandResult) {
                        introEndCommandResult =
                            saveCinematicCheckPoint(command);
                    }
                    if (introEndCommandResult) {
                        introEndCommandResult =
                            cinematicUi_.applyCommand(command);
                    }
                });
            if (!result || !introEndCommandResult) {
                return fail(!result ? result.message()
                                    : introEndCommandResult.message());
            }
            renderer_.setCinematicVisibleRooms(
                levelCinematicRuntime_.forcedVisibleRooms());
        }
        result = renderer_.updateLevelRooms(levelOne_,
                                            levelCinematicRuntime_);
        if (result) {
            result = renderer_.updateLevelOneActors(levelOne_, timestamp);
        }
        if (!result) {
            return fail(result.message());
        }
        game::CameraPose presentedCameraPose;
        if (!gameplayActive) {
            renderer_.setCameraAreaRoomVisibility(
                gameplayCamera_.mustInvisibleRooms(),
                gameplayCamera_.mustVisibleRooms());
            renderer_.setCinematicVisibleRooms(
                levelCinematicRuntime_.forcedVisibleRooms());
            const game::CameraPose cameraPose =
                levelCinematicRuntime_.applyCameraShake(
                    levelOne_.introCamera().sample(timestamp));
            presentedCameraPose = cameraPose;
            if (audioEnabled) {
                const assets::Vector3 listenerPosition =
                    levelCinematicRuntime_.listenerOnMainCharacter()
                        ? gameplayPlayer_.position()
                        : cameraPose.position;
                const assets::Vector3 listenerTarget{
                    listenerPosition.x +
                        (cameraPose.target.x - cameraPose.position.x),
                    listenerPosition.y +
                        (cameraPose.target.y - cameraPose.position.y),
                    listenerPosition.z +
                        (cameraPose.target.z - cameraPose.position.z)};
                audio_.setListener(listenerPosition, listenerTarget,
                                   cameraPose.up);
            }
            result = renderer_.setCamera(cameraPose);
        } else {
            const auto stick = controller_.leftStick();
            game::PlayerMotionInput motion =
                autoplay ? autoplayInput.motion
                         : game::PlayerMotionInput{stick.x, stick.y};
            if (!autoplay && motion.right == 0.0F && motion.forward == 0.0F) {
                const auto& input = keyRouter_.state();
                motion.right = static_cast<float>(input.moveRight.held) -
                               static_cast<float>(input.moveLeft.held);
                motion.forward = static_cast<float>(input.moveUp.held) -
                                 static_cast<float>(input.moveDown.held);
            }
            const bool controlsEnabled =
                levelCinematicRuntime_.controlsEnabled() &&
                !cinematicUi_.modalTutorialVisible() &&
                !gameplayPlayer_.cinematicDriven() &&
                !hostageRuntime_.ownsPlayerControl() &&
                !levelDeathRuntime_.active() &&
                !restoreRuntime_.active() &&
                !quickTimeEvent_.active() &&
                !enemyRuntime_.rhinoQuickTimeAction().active() &&
                !gameplayCinematics_.hasActiveColladaPlayback();
            if (!controlsEnabled) {
                motion = {};
            }
            const bool jumpPressed = autoplay
                ? autoplayInput.jumpPressed
                : keyRouter_.state().jump.pressed;
            const bool jumpHeld = autoplay
                ? autoplayInput.jumpHeld
                : keyRouter_.state().jump.held;
            const bool jumpReleased = autoplay
                ? autoplayInput.jumpReleased
                : keyRouter_.state().jump.released;
            const bool webPressed = autoplay
                ? autoplayInput.webPressed
                : keyRouter_.state().web.pressed;
            const bool webHeld = autoplay
                ? autoplayInput.webHeld
                : keyRouter_.state().web.held;
            const bool webReleased = autoplay
                ? autoplayInput.webReleased
                : keyRouter_.state().web.released;
            const bool punchPressed = autoplay
                ? autoplayInput.punchPressed
                : keyRouter_.state().punch.pressed;
            const bool punchHeld = !punchPressed && !autoplay &&
                keyRouter_.state().punch.held;
            const bool spiderSensePressed = autoplay
                ? autoplayInput.spiderSensePressed
                : keyRouter_.state().spiderSense.pressed;
            const bool superAttackPressed = autoplay
                ? autoplayInput.superAttackPressed
                : keyRouter_.state().superAttack.pressed;
            const bool rescuePressed =
                punchPressed &&
                (controlsEnabled || hostageRuntime_.quickTimeActive()) &&
                (hostageRuntime_.quickTimeActive() ||
                 hostageRuntime_.canStartRescue(gameplayPlayer_));
            const auto cameraBeforeMovement =
                gameplayCamera_.sample(gameplayPlayer_.position());
            const auto attackDirection =
                gameplayPlayer_.attackDirection(motion, cameraBeforeMovement);
            struct PlayerTargetSelection {
                const game::LevelEnemyState* enemy{};
                const game::LevelObjectState* object{};
                game::PlayerAttackTarget target;
            };
            const auto selectEnemyTarget =
                [this](const game::LevelEnemyState* enemy)
                    -> std::optional<PlayerTargetSelection> {
                if (enemy == nullptr || enemy->asset == nullptr) {
                    return std::nullopt;
                }
                return PlayerTargetSelection{
                    enemy,
                    nullptr,
                    {enemy->position,
                     enemy->collisionRadius,
                     enemy->asset->objectId,
                     enemyRuntime_.isInAir(enemy->asset->objectId),
                     enemy->collisionHeight,
                     enemy->canBeTiedUp,
                     enemy->canBeDraggedTo,
                     enemy->onWall,
                     enemyRuntime_.canEnterWallWeb(enemy->asset->objectId),
                     enemyRuntime_.nodeWorldPosition(enemy->asset->objectId,
                                                     "Bip01_Head")}};
            };
            const auto selectObjectTarget =
                [](const game::LevelObjectState* object)
                    -> std::optional<PlayerTargetSelection> {
                if (object == nullptr || object->asset == nullptr) {
                    return std::nullopt;
                }
                return PlayerTargetSelection{
                    nullptr,
                    object,
                    {object->position,
                     object->asset->collisionRadius,
                     object->asset->objectId,
                     false,
                     object->asset->collisionHeight}};
            };
            const auto acquireAttackTarget =
                [this, &attackDirection, &selectEnemyTarget,
                 &selectObjectTarget](float directionalRange,
                                      float neutralRange)
                    -> std::optional<PlayerTargetSelection> {
                    if (gameplayPlayer_.onWall()) {
                        return selectEnemyTarget(
                            enemyRuntime_.findPlayerWallAttackTarget(
                                gameplayPlayer_.position()));
                    }
                    const float maximumRange = attackDirection.has_value()
                        ? directionalRange
                        : neutralRange;
                    const game::LevelEnemyState* enemy =
                        enemyRuntime_.findPlayerAttackTarget(
                        gameplayPlayer_.position(),
                        attackDirection.value_or(gameplayPlayer_.facing()),
                        attackDirection.has_value(),
                        maximumRange,
                        &levelCollision_);
                    const game::LevelObjectState* object =
                        attackDirection.has_value()
                        ? objectRuntime_.findPlayerEyeAttackTarget(
                              gameplayPlayer_.position(), *attackDirection,
                              maximumRange, &levelCollision_)
                        : objectRuntime_.findPlayerAttackRangeTarget(
                              gameplayPlayer_.position(), maximumRange);
                    if (enemy == nullptr) {
                        return selectObjectTarget(object);
                    }
                    if (object == nullptr) {
                        return selectEnemyTarget(enemy);
                    }
                    const auto squaredDistance = [this](
                        const assets::Vector3& position) {
                        const float x =
                            position.x - gameplayPlayer_.position().x;
                        const float y =
                            position.y - gameplayPlayer_.position().y;
                        const float z =
                            position.z - gameplayPlayer_.position().z;
                        return x * x + y * y + z * z;
                    };
                    if (!attackDirection.has_value()) {
                        // SearchTargetByAttackRange (0x003430c8) keeps the
                        // helper enemy only when it is strictly nearer. A tie
                        // therefore resolves to the targeted destroyable.
                        return squaredDistance(enemy->position) <
                                       squaredDistance(object->position)
                            ? selectEnemyTarget(enemy)
                            : selectObjectTarget(object);
                    }
                    const auto facingDot = [this, &attackDirection](
                        const assets::Vector3& position) {
                        const float x =
                            position.x - gameplayPlayer_.position().x;
                        const float y =
                            position.y - gameplayPlayer_.position().y;
                        const float length = std::hypot(x, y);
                        return length > std::numeric_limits<float>::epsilon()
                            ? (x * attackDirection->x +
                               y * attackDirection->y) /
                                  length
                            : -1.0F;
                    };
                    // SearchTargetByEyeHorizon (0x00343b70) evaluates the
                    // appended destroyables first during its reverse walk;
                    // an enemy replaces one only for a strictly better dot.
                    return facingDot(enemy->position) >
                                   facingDot(object->position)
                        ? selectEnemyTarget(enemy)
                        : selectObjectTarget(object);
                };
            const auto acquireCombatPointerEnemy =
                [this, &attackDirection]()
                    -> const game::LevelEnemyState* {
                // Player::UpdateTarget (0x00343058) forces the
                // SearchTargetByEyeHorizon decision to weight 1.0. Unlike a
                // button-triggered search, neutral input therefore uses the
                // current facing and never falls back to nearest range.
                const assets::Vector3 direction =
                    attackDirection.value_or(gameplayPlayer_.facing());
                const game::LevelEnemyState* enemy =
                    enemyRuntime_.findPlayerAttackTarget(
                        gameplayPlayer_.position(), direction, true,
                        1000.0F, &levelCollision_);
                const game::LevelObjectState* object =
                    objectRuntime_.findPlayerEyeAttackTarget(
                        gameplayPlayer_.position(), direction, 1000.0F,
                        &levelCollision_);
                if (enemy == nullptr || object == nullptr) {
                    return object == nullptr ? enemy : nullptr;
                }
                const auto facingDot = [this, &direction](
                    const assets::Vector3& position) {
                    const float x = position.x - gameplayPlayer_.position().x;
                    const float y = position.y - gameplayPlayer_.position().y;
                    const float length = std::hypot(x, y);
                    return length > std::numeric_limits<float>::epsilon()
                        ? (x * direction.x + y * direction.y) / length
                        : -1.0F;
                };
                // SearchTargetByEyeHorizon (0x00343b70) reverse-walks the
                // combined list. Destroyables are appended last and retain
                // a dot tie; UpdateTargetPointer ignores that non-Unit.
                return facingDot(enemy->position) > facingDot(object->position)
                    ? enemy
                    : nullptr;
            };
            if (controlsEnabled && spiderSensePressed) {
                // Player::CanEnableSpiderSense (0x00341c98) consults profile
                // bit 1 only in Level 1 (CLevel+0x44 == 0). Cinematic 974's
                // shipped Unlock command supplies that bit at 4000 ms.
                const bool skillUnlocked = levelOne_.levelNumber() != 1 ||
                    levelCinematicRuntime_.skillUnlocked(1);
                const std::optional<game::EnemySpiderSenseThreat> attacker =
                    skillUnlocked
                        ? enemyRuntime_.findSpiderSenseThreat()
                        : std::nullopt;
                const bool accepted = skillUnlocked && attacker.has_value() &&
                    gameplayPlayer_.requestSpiderSense({
                        attacker->position, attacker->collisionRadius,
                        attacker->targetObjectId, attacker->airborne,
                        attacker->collisionHeight, attacker->canBeTiedUp,
                        attacker->canBeDraggedTo, attacker->onWall,
                        attacker->canEnterWallWeb, attacker->headPosition,
                        attacker->nearAttackKeyFrame,
                        attacker->senseReactionType,
                        attacker->canBeCounterHit,
                        attacker->enemySubType});
                if (accepted) {
                    // DoNormalSenseAction (0x0034f650) starts the native
                    // interface.bsprite frame-13 flash at alpha 160 before
                    // selecting the evade/counter state.
                    cinematicUi_.startInterfaceEffect(160, 0, -1);
                    // UpdateSpiderSense pops the selected CTargetHelper
                    // record before DoNormalSenseAction (0x0034fc40-
                    // 0x0034fc4e). Keep that one-shot warning lifetime even
                    // though the source enemy's attack animation continues.
                    (void)enemyRuntime_.consumeSpiderSenseThreat(*attacker);
                    const float denominator =
                        attacker->slowMotionDenominator;
                    // UpdateSpiderSense (0x0034fb70, 0x0034fc62-0x0034fc76)
                    // forces the attack's EnemyAttackInfo+0x50 denominator
                    // for 1000 ms, with the native enter/exit SFX enabled.
                    levelCinematicRuntime_.setSlowMotion(
                        denominator, 1000.0F, 0.0F, true);
                    if (autoplay) {
                        autoplay->recordEvent(
                            syntheticElapsedMilliseconds,
                            "interface_effect",
                            "frame=13;alpha=160;fade_ms=800;"
                            "source=spider_sense");
                        autoplay->recordEvent(
                            syntheticElapsedMilliseconds,
                            "player_slow_motion",
                            "denominator=" + std::to_string(denominator) +
                                ";hold_ms=1000.000000;ramp_ms=0.000000;"
                                "sfx=1;source=spider_sense");
                    }
                }
                if (autoplay) {
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds, "player_action",
                        "spider_sense_" +
                            std::string(accepted ? "accepted" : "rejected") +
                            ";attacker=" +
                            (!attacker.has_value()
                                 ? std::string{"none"}
                                 : std::to_string(attacker->targetObjectId)) +
                            ";skill_unlocked=" +
                            std::to_string(skillUnlocked) +
                            ";state=" +
                            std::to_string(gameplayPlayer_.activeStateId()));
                }
            }
            if (controlsEnabled && superAttackPressed) {
                // Player::CanEnableUltimate (0x00345e98) applies the same
                // Level-1-only profile gate to bit 0. Cinematic 969 unlocks
                // it through CCinematicThread::OnUnlock (0x0037240c).
                const bool skillUnlocked = levelOne_.levelNumber() != 1 ||
                    levelCinematicRuntime_.skillUnlocked(0);
                const bool accepted = skillUnlocked &&
                    gameplayPlayer_.requestUltimate();
                if (accepted) {
                    // Player::DoUltimate (0x0034def4-0x0034df3a) calls
                    // Application::SetSlowMotion(3.0, animLength * 3, 0,
                    // true, false). The first bool forces the request past
                    // the probabilistic ordinary-combat gate; the second
                    // suppresses the generic slow-motion SFX.
                    const float holdMilliseconds = static_cast<float>(
                        gameplayPlayer_.activeAnimationDurationMilliseconds()) *
                        3.0F;
                    levelCinematicRuntime_.setSlowMotion(
                        3.0F, holdMilliseconds, 0.0F, false);
                    if (autoplay) {
                        autoplay->recordEvent(
                            syntheticElapsedMilliseconds,
                            "player_slow_motion",
                            "denominator=3.;hold_ms=" +
                                std::to_string(holdMilliseconds) +
                                ";ramp_ms=0.000000;sfx=0;source=ultimate");
                    }
                }
                if (autoplay) {
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds, "player_action",
                        "super_attack_" +
                            std::string(accepted ? "accepted" : "rejected") +
                            ";skill_unlocked=" +
                            std::to_string(skillUnlocked) +
                            ";state=" +
                            std::to_string(gameplayPlayer_.activeStateId()));
                }
            }
            std::optional<PlayerTargetSelection> attackTarget;
            std::optional<game::PlayerAttackTarget> playerAttackTarget;
            if (controlsEnabled && (punchPressed || punchHeld) &&
                !rescuePressed) {
                attackTarget = acquireAttackTarget(1000.0F, 1000.0F);
                if (attackTarget.has_value()) {
                    playerAttackTarget = attackTarget->target;
                }
            }
            const std::optional<game::PlayerButtonPhase> jumpPhase =
                !(jumpReleased || jumpPressed || jumpHeld)
                    ? std::nullopt
                    : std::optional<game::PlayerButtonPhase>{
                          jumpReleased
                              ? game::PlayerButtonPhase::Released
                              : jumpPressed ? game::PlayerButtonPhase::Pressed
                                            : game::PlayerButtonPhase::Held};
            const std::optional<game::PlayerButtonPhase> punchPhase =
                !(punchPressed || punchHeld) || rescuePressed
                    ? std::nullopt
                    : std::optional<game::PlayerButtonPhase>{
                          punchPressed ? game::PlayerButtonPhase::Pressed
                                       : game::PlayerButtonPhase::Held};
            const std::optional<game::PlayerButtonPhase> webPhase =
                !(webPressed || webHeld)
                    ? std::nullopt
                    : std::optional<game::PlayerButtonPhase>{
                          webPressed ? game::PlayerButtonPhase::Pressed
                                     : game::PlayerButtonPhase::Held};
            const std::size_t simultaneousActionCount =
                static_cast<std::size_t>(jumpPhase.has_value()) +
                static_cast<std::size_t>(punchPhase.has_value()) +
                static_cast<std::size_t>(webPhase.has_value());
            const game::PlayerInputAction preferredAction =
                simultaneousActionCount > 1
                    ? gameplayPlayer_.preferredInputAction(
                          jumpPhase, punchPhase, webPhase, playerAttackTarget)
                    : game::PlayerInputAction::None;
            const bool dispatchJump = simultaneousActionCount <= 1 ||
                preferredAction == game::PlayerInputAction::Jump;
            const bool dispatchPunch = simultaneousActionCount <= 1 ||
                preferredAction == game::PlayerInputAction::Punch;
            const bool dispatchWeb = simultaneousActionCount <= 1 ||
                preferredAction == game::PlayerInputAction::Web;
            if (controlsEnabled && dispatchJump &&
                (jumpReleased || jumpPressed || jumpHeld)) {
                const game::PlayerButtonPhase phase = jumpReleased
                    ? game::PlayerButtonPhase::Released
                    : jumpPressed ? game::PlayerButtonPhase::Pressed
                                  : game::PlayerButtonPhase::Held;
                const bool accepted = gameplayPlayer_.requestJump(
                    motion, cameraBeforeMovement, phase);
                if (autoplay && (jumpReleased || jumpPressed)) {
                    const std::string_view phaseName = jumpReleased
                        ? "released"
                        : "pressed";
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds, "player_action",
                        "jump_" + std::string(phaseName) + "_" +
                            (accepted ? "accepted" : "rejected") +
                            ";state=" +
                            std::to_string(gameplayPlayer_.activeStateId()) +
                            ";name=" +
                            std::string(gameplayPlayer_.activeStateName()));
                }
            }
            if (controlsEnabled && dispatchWeb && webPressed) {
                const bool traversalRequest = gameplayPlayer_.airborne();
                const float gameplayAspect = autoplay
                    ? static_cast<float>(autoplay->renderWidth()) /
                          autoplay->renderHeight()
                    : static_cast<float>(window_.clientWidth()) /
                          std::max(1U, window_.clientHeight());
                gameplayPlayer_.setWebGrabViewContext(
                    cameraBeforeMovement, gameplayAspect,
                    renderer_.roomVisibility());
                const auto candidates = autoplay && traversalRequest
                    ? gameplayPlayer_.webGrabCandidateDiagnostics()
                    : std::vector<game::WebGrabCandidateDiagnostics>{};
                std::optional<PlayerTargetSelection> groundWebSpecialTarget;
                if (!traversalRequest) {
                    // UpdateKeyTrigger resolves state 57 through
                    // GetGroundWebSpecialState (0x00343f48) before state 58
                    // performs its wider target search. The selector is a
                    // strict 1000 cm eye-horizon search; if an appended
                    // destroyable wins, its non-CEnemy type rejects the
                    // special entirely.
                    groundWebSpecialTarget = selectEnemyTarget(
                        acquireCombatPointerEnemy());
                }
                std::optional<PlayerTargetSelection> webTarget;
                const std::int32_t retainedWebTargetId =
                    gameplayPlayer_.webAttackTransitionReady()
                        ? gameplayPlayer_.trackedAttackTargetObjectId()
                        : -1;
                if (retainedWebTargetId >= 0) {
                    // NeedRelocateTarget (0x003412d0) decides at state entry
                    // whether to use this fresh search result or preserve
                    // Player+0x594. State 92 relocates; the later motion
                    // 103..119/124..130 web states retain their old target.
                    webTarget = acquireAttackTarget(1000.0F, 1000.0F);
                } else if (traversalRequest) {
                    // GetAirWebSpecialState first asks CTargetHelper for mask
                    // 2: its nearest airborne enemy from the 2000 cm
                    // helper census. Only an empty helper list falls through
                    // to the ordinary 1000 cm eye/range search.
                    webTarget = selectEnemyTarget(
                        enemyRuntime_.findNearestAirbornePlayerTarget(
                            gameplayPlayer_.position()));
                    if (!webTarget.has_value()) {
                        webTarget = acquireAttackTarget(1000.0F, 1000.0F);
                    }
                } else {
                    webTarget = acquireAttackTarget(3000.0F, 2000.0F);
                }
                const std::optional<game::PlayerAttackTarget>
                    playerWebTarget = !webTarget.has_value()
                        ? std::nullopt
                        : std::optional<game::PlayerAttackTarget>{[&] {
                              game::PlayerAttackTarget selected =
                                  webTarget->target;
                              if (webTarget->enemy != nullptr) {
                                  selected.canEnterWallWeb =
                                      selected.canEnterWallWeb &&
                                   [&] {
                                       const auto spine = enemyRuntime_.nodeWorldPosition(
                                           webTarget->enemy->asset->objectId,
                                           "Bip01_Spine1");
                                       const float aspect = autoplay
                                           ? static_cast<float>(autoplay->renderWidth()) /
                                                 autoplay->renderHeight()
                                           : static_cast<float>(window_.clientWidth()) /
                                                 std::max(1U, window_.clientHeight());
                                       return spine && game::isPointInScreen(
                                           cameraBeforeMovement, aspect, *spine);
                                   }();
                              }
                              return selected;
                          }()};
                const game::PlayerGroundWebSpecialSearch groundSpecialSearch{
                    !groundWebSpecialTarget.has_value()
                        ? std::nullopt
                        : std::optional<game::PlayerAttackTarget>{
                              groundWebSpecialTarget->target}};
                const bool accepted = gameplayPlayer_.requestWeb(
                    playerWebTarget, attackDirection,
                    game::PlayerButtonPhase::Pressed,
                    &cameraBeforeMovement,
                    traversalRequest ? nullptr : &groundSpecialSearch);
                if (autoplay) {
                    const bool traversalAccepted =
                        accepted && gameplayPlayer_.activeStateId() == 17;
                    std::string detail =
                        "accepted=" + std::to_string(accepted) +
                        ";mode=" +
                        std::string(
                            traversalAccepted
                                ? "traversal"
                                : traversalRequest && webTarget.has_value() &&
                                    (webTarget->target.canBeTiedUp ||
                                     webTarget->target.canBeDraggedTo)
                                ? "air_combat"
                                : traversalRequest ? "traversal"
                                                   : "combat") +
                        ";target=" +
                        (!webTarget.has_value()
                             ? std::string{"none"}
                             : std::to_string(webTarget->target.objectId)) +
                        ";special_target=" +
                        (!groundWebSpecialTarget.has_value()
                             ? std::string{"none"}
                             : std::to_string(
                                   groundWebSpecialTarget->target.objectId)) +
                        ";state=" +
                        std::to_string(gameplayPlayer_.activeStateId()) +
                        ";rejection=" +
                        std::string(accepted
                                        ? std::string_view{"none"}
                                        : gameplayPlayer_
                                              .lastActionRejectionReason()) +
                        ";x=" +
                        std::to_string(gameplayPlayer_.position().x) +
                        ";y=" +
                        std::to_string(gameplayPlayer_.position().y) +
                        ";z=" +
                        std::to_string(gameplayPlayer_.position().z);
                    for (const game::WebGrabCandidateDiagnostics& candidate :
                         candidates) {
                        detail += ";point=" +
                                  std::to_string(candidate.objectId) +
                                  ",distance=" +
                                  std::to_string(candidate.distance) +
                                  ",facing_dot=" +
                                  std::to_string(candidate.facingDot) +
                                  ",los=" +
                                  std::to_string(candidate.lineOfSight) +
                                  ",facing=" +
                                  std::to_string(candidate.facingAccepted) +
                                  ",range=" +
                                  std::to_string(
                                      candidate.withinVisibleLength) +
                                  ",best_radius=" +
                                  std::to_string(
                                      candidate.withinBestSearchRadius) +
                                  ",hint_radius=" +
                                  std::to_string(
                                      candidate.withinHintSearchRadius) +
                                  ",room_visible=" +
                                  std::to_string(candidate.roomVisible) +
                                  ",on_screen=" +
                                  std::to_string(candidate.onScreen) +
                                  ",visible_length=" +
                                  std::to_string(candidate.visibleLength) +
                                  ",blocking_room=" +
                                  std::to_string(candidate.blockingRoomId) +
                                  ",blocking_geometry=" +
                                  std::string(candidate.blockingGeometry) +
                                  ",blocking_material=" +
                                  std::string(candidate.blockingMaterial) +
                                  ",blocking_flags=" +
                                  std::to_string(
                                      candidate.blockingPhysicsFlags) +
                                  ",blocking_fraction=" +
                                  std::to_string(candidate.blockingFraction) +
                                  ",blocking_x=" +
                                  std::to_string(
                                      candidate.blockingPosition.x) +
                                  ",blocking_y=" +
                                  std::to_string(
                                      candidate.blockingPosition.y) +
                                  ",blocking_z=" +
                                  std::to_string(
                                      candidate.blockingPosition.z);
                    }
                    autoplay->recordEvent(traceTimeMilliseconds,
                                          "web_grab_search", detail);
                }
            }
            if (controlsEnabled && dispatchWeb && !webPressed && webHeld &&
                gameplayPlayer_.webHeldAttackTransitionReady()) {
                const std::int32_t retainedWebTargetId =
                    gameplayPlayer_.trackedAttackTargetObjectId();
                const game::LevelEnemyState* heldTarget =
                    retainedWebTargetId >= 0
                        ? enemyRuntime_.find(retainedWebTargetId)
                        : nullptr;
                std::optional<game::PlayerAttackTarget> playerHeldWebTarget;
                if (heldTarget != nullptr) {
                    playerHeldWebTarget = game::PlayerAttackTarget{
                        heldTarget->position, heldTarget->collisionRadius,
                        heldTarget->asset->objectId,
                        enemyRuntime_.isInAir(heldTarget->asset->objectId),
                        heldTarget->collisionHeight, heldTarget->canBeTiedUp,
                        heldTarget->canBeDraggedTo};
                }
                const bool accepted = gameplayPlayer_.requestWeb(
                    playerHeldWebTarget, attackDirection,
                    game::PlayerButtonPhase::Held,
                    &cameraBeforeMovement);
                if (autoplay) {
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds, "player_action",
                        "web_held_" +
                            std::string(accepted ? "accepted" : "rejected") +
                            ";state=" +
                            std::to_string(gameplayPlayer_.activeStateId()) +
                            ";name=" +
                            std::string(gameplayPlayer_.activeStateName()));
                }
            }
            if (controlsEnabled && webReleased) {
                (void)gameplayPlayer_.releaseWeb();
            }
            if (controlsEnabled && dispatchPunch &&
                (punchPressed || punchHeld) &&
                !rescuePressed) {
                const bool accepted = gameplayPlayer_.requestPunch(
                    playerAttackTarget,
                    attackDirection,
                    punchPressed ? game::PlayerButtonPhase::Pressed
                                 : game::PlayerButtonPhase::Held);
                if (autoplay && accepted) {
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds, "player_action",
                        "punch_accepted;state=" +
                            std::to_string(gameplayPlayer_.activeStateId()) +
                            ";name=" +
                            std::string(gameplayPlayer_.activeStateName()) +
                            ";target=" +
                            (!attackTarget.has_value()
                                 ? std::string{"none"}
                                 : std::to_string(
                                       attackTarget->target.objectId)));
                } else if (autoplay) {
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds, "player_action",
                        "punch_rejected;reason=" +
                            std::string(gameplayPlayer_
                                            .lastActionRejectionReason()) +
                            ";state=" +
                            std::to_string(gameplayPlayer_.activeStateId()) +
                            ";name=" +
                            std::string(gameplayPlayer_.activeStateName()) +
                            ";hurt_ms=" +
                            std::to_string(gameplayPlayer_
                                               .hurtReactionRemainingMilliseconds()) +
                            ";target=" +
                            (!attackTarget.has_value()
                                 ? std::string{"none"}
                                 : std::to_string(
                                       attackTarget->target.objectId)));
                }
            }
            const auto previousWallWebPhase = gameplayPlayer_.wallWeb().phase();
            const auto* wallWebTarget = enemyRuntime_.find(
                gameplayPlayer_.wallWeb().targetObjectId());
            gameplayPlayer_.setWallWebInput(
                autoplay ? autoplayInput.quickTimeEventPressed
                         : keyRouter_.state().quickTimeEvent.pressed,
                wallWebTarget != nullptr && wallWebTarget->visible &&
                    wallWebTarget->health > 0.0F);
            const std::int32_t trackedAttackTargetId =
                gameplayPlayer_.trackedAttackTargetObjectId();
            const game::LevelEnemyState* trackedAttackTarget =
                enemyRuntime_.find(trackedAttackTargetId);
            if (trackedAttackTarget != nullptr &&
                trackedAttackTarget->asset != nullptr &&
                trackedAttackTarget->visible &&
                trackedAttackTarget->health > 0.0F) {
                gameplayPlayer_.refreshTrackedAttackTarget(
                    game::PlayerAttackTarget{
                        trackedAttackTarget->position,
                        trackedAttackTarget->collisionRadius,
                        trackedAttackTarget->asset->objectId,
                        enemyRuntime_.isInAir(
                            trackedAttackTarget->asset->objectId),
                        trackedAttackTarget->collisionHeight,
                        trackedAttackTarget->canBeTiedUp,
                        trackedAttackTarget->canBeDraggedTo,
                        trackedAttackTarget->onWall,
                        enemyRuntime_.canEnterWallWeb(
                            trackedAttackTarget->asset->objectId),
                        enemyRuntime_.nodeWorldPosition(
                            trackedAttackTarget->asset->objectId,
                            "Bip01_Head")});
            } else if (trackedAttackTargetId >= 0) {
                const game::LevelObjectState* trackedObjectTarget =
                    objectRuntime_.find(trackedAttackTargetId);
                if (trackedObjectTarget != nullptr &&
                    trackedObjectTarget->asset != nullptr &&
                    trackedObjectTarget->visible &&
                    trackedObjectTarget->health > 0.0F &&
                    trackedObjectTarget->destructionPhase ==
                        game::LevelObjectDestructionPhase::Intact) {
                    gameplayPlayer_.refreshTrackedAttackTarget(
                        selectObjectTarget(trackedObjectTarget)->target);
                } else {
                    gameplayPlayer_.refreshTrackedAttackTarget(std::nullopt);
                }
            }
            const bool attackRelocationPending =
                gameplayPlayer_.queuedAttackNeedsTargetRelocation();
            const std::uint16_t attackRelocationFromState =
                gameplayPlayer_.activeStateId();
            const std::int32_t attackRelocationFromTarget =
                gameplayPlayer_.trackedAttackTargetObjectId();
            std::int32_t attackRelocationCandidate = -1;
            if (attackRelocationPending) {
                // Player::SetNextStateId (0x003491d0) invokes
                // NeedRelocateTarget (0x003412d0) at the linked-animation
                // boundary, not when the button was buffered. Repeat the
                // native 1000 cm search with the current actor transforms.
                const auto relocationTarget =
                    acquireAttackTarget(1000.0F, 1000.0F);
                attackRelocationCandidate = relocationTarget.has_value()
                    ? relocationTarget->target.objectId
                    : -1;
                gameplayPlayer_.setQueuedAttackRelocationTarget(
                    relocationTarget.has_value()
                        ? std::optional<game::PlayerAttackTarget>{
                              relocationTarget->target}
                        : std::nullopt,
                    attackDirection);
            }
            if (levelCinematicRuntime_.attributionEnabled() &&
                gameplayPlayer_.canUpdateCombatTarget()) {
                const game::LevelEnemyState* pointerTarget =
                    acquireCombatPointerEnemy();
                if (pointerTarget != nullptr &&
                    pointerTarget->asset != nullptr) {
                    assets::Vector3 pointerPosition;
                    const auto head = enemyRuntime_.nodeWorldPosition(
                        pointerTarget->asset->objectId, "Bip01_Head");
                    if (head.has_value()) {
                        pointerPosition = *head;
                        pointerPosition.z += 60.0F;
                    } else {
                        pointerPosition = pointerTarget->position;
                        pointerPosition.z +=
                            pointerTarget->collisionHeight + 25.0F;
                    }
                    if (hintRuntime_.setCombatTargetCue(
                            pointerTarget->asset->objectId, pointerPosition,
                            pointerTarget->health,
                            pointerTarget->maximumHealth) && autoplay) {
                        const game::LevelHintState* cue =
                            hintRuntime_.combatTargetCue();
                        autoplay->recordEvent(
                            traceTimeMilliseconds, "player_target_hint",
                            "visible=1;target=" +
                                std::to_string(
                                    pointerTarget->asset->objectId) +
                                ";animation=" +
                                std::to_string(cue == nullptr
                                    ? -1
                                    : cue->animationIndex) +
                                ";health=" +
                                std::to_string(pointerTarget->health) +
                                ";maximum_health=" +
                                std::to_string(
                                    pointerTarget->maximumHealth));
                    }
                } else if (const game::LevelHintState* cue =
                               hintRuntime_.combatTargetCue();
                           cue != nullptr && cue->visible) {
                    const std::int32_t previousTarget =
                        cue->combatTargetObjectId;
                    const game::LevelEnemyState* retainedTarget =
                        enemyRuntime_.find(previousTarget);
                    // SearchTarget leaves Player+0x6e0 untouched when its
                    // forced eye search returns null. UpdateTarget then
                    // keeps the marker while CurTargetAlive succeeds.
                    if (retainedTarget == nullptr ||
                        retainedTarget->asset == nullptr ||
                        !retainedTarget->visible ||
                        retainedTarget->health <= 0.0F) {
                        if (hintRuntime_.clearCombatTargetCue() && autoplay) {
                            autoplay->recordEvent(
                                traceTimeMilliseconds,
                                "player_target_hint",
                                "visible=0;target=" +
                                    std::to_string(previousTarget));
                        }
                    }
                }
            }
            gameplayPlayer_.update(motion, cameraBeforeMovement,
                                   gameDeltaMilliseconds);
            if (autoplay && attackRelocationPending &&
                !gameplayPlayer_.queuedAttackNeedsTargetRelocation() &&
                gameplayPlayer_.activeStateId() !=
                    attackRelocationFromState) {
                autoplay->recordEvent(
                    traceTimeMilliseconds, "player_target_relocated",
                    "from_state=" +
                        std::to_string(attackRelocationFromState) +
                        ";to_state=" +
                        std::to_string(gameplayPlayer_.activeStateId()) +
                        ";from_target=" +
                        std::to_string(attackRelocationFromTarget) +
                        ";candidate=" +
                        std::to_string(attackRelocationCandidate) +
                        ";selected=" +
                        std::to_string(gameplayPlayer_
                                           .trackedAttackTargetObjectId()) +
                        ";facing_x=" +
                        std::to_string(gameplayPlayer_.facing().x) +
                        ";facing_y=" +
                        std::to_string(gameplayPlayer_.facing().y));
            }
            for (const auto& event : gameplayPlayer_.consumeWallWebEvents()) {
                const bool applied = enemyRuntime_.applyWallWebEvent(event);
                const auto* target = enemyRuntime_.find(event.targetObjectId);
                const bool armored = target != nullptr &&
                    (target->asset->enemyTypeId == 8 || target->asset->enemyTypeId == 10);
                const std::uint16_t loopId = armored ? 0xd9 : 0xe6;
                const std::uint16_t releaseId = armored ? 0xda : 0xe7;
                if (event.kind == game::WallWebEventKind::Hold && applied) {
                    result = playSpatialSound(event.targetObjectId, voxSounds_.find(loopId)->eventName,
                                             wallWebSounds_.at(loopId), true);
                    wallWebLoopSoundId_ = loopId;
                } else if (event.kind == game::WallWebEventKind::Release &&
                           event.success && applied) {
                    result = playSpatialSound(event.targetObjectId,
                        voxSounds_.find(releaseId)->eventName,
                        wallWebSounds_.at(releaseId), false);
                } else if (event.kind == game::WallWebEventKind::Finish && wallWebLoopSoundId_ >= 0) {
                    result = stopNamedAudio(voxSounds_.find(
                        static_cast<std::uint16_t>(wallWebLoopSoundId_))->eventName, 1000);
                    wallWebLoopSoundId_ = -1;
                }
                if (autoplay) {
                    autoplay->recordEvent(traceTimeMilliseconds, "wall_web_event",
                        "kind=" + std::to_string(static_cast<int>(event.kind)) +
                        ";target=" + std::to_string(event.targetObjectId) +
                        ";success=" + std::to_string(event.success) +
                        ";applied=" + std::to_string(applied) +
                        ";angle=" + std::to_string(gameplayPlayer_.wallWeb().angle()) +
                        ";target_bone=" + std::string(gameplayPlayer_.wallWeb().targetBone()));
                }
                if (!result) {
                    return fail(result.message());
                }
            }
            if (autoplay && previousWallWebPhase != gameplayPlayer_.wallWeb().phase()) {
                autoplay->recordEvent(traceTimeMilliseconds, "wall_web_phase",
                    "phase=" + std::to_string(static_cast<int>(gameplayPlayer_.wallWeb().phase())) +
                    ";progress=" + std::to_string(gameplayPlayer_.wallWeb().progress()));
            }
            if (autoplay) {
                const game::SlideCatch candidate =
                    gameplayPlayer_.slideCatchCandidate();
                if (candidate.slide != nullptr) {
                    autoplay->recordEvent(
                        traceTimeMilliseconds, "slide_catch_candidate",
                        "slide=" + std::to_string(candidate.slide->objectId) +
                            ";distance_sq=" +
                            std::to_string(candidate.distanceSquared) +
                            ";projected=" +
                            std::to_string(candidate.projectedPosition.x) + "|" +
                            std::to_string(candidate.projectedPosition.y) + "|" +
                            std::to_string(candidate.projectedPosition.z) +
                            ";player_state=" +
                            std::to_string(gameplayPlayer_.activeStateId()) +
                            ";slide_active=" +
                            std::to_string(gameplayPlayer_.slideActive()));
                }
            }
            // UpdateQTE starts its 1000 ms loop fade on the success/failure
            // animation, not after that animation finishes.
            if (wallWebLoopSoundId_ >= 0 &&
                (gameplayPlayer_.wallWeb().phase() == game::WallWebPhase::Success ||
                 gameplayPlayer_.wallWeb().phase() == game::WallWebPhase::Failure)) {
                result = stopNamedAudio(voxSounds_.find(
                    static_cast<std::uint16_t>(wallWebLoopSoundId_))->eventName, 1000);
                wallWebLoopSoundId_ = -1;
                if (!result) { return fail(result.message()); }
            }
            result = hostageRuntime_.update(
                gameplayPlayer_, objectRuntime_, levelBonusRuntime_,
                gameDeltaMilliseconds, rescuePressed);
            if (!result) {
                return fail(result.message());
            }
            for (const game::HostageSoundCue& cue :
                 hostageRuntime_.consumeSoundCues()) {
                const audio::VoxSoundRecord* record =
                    voxSounds_.find(cue.voxSoundId);
                const auto clip = hostageSounds_.find(cue.voxSoundId);
                if (record == nullptr || clip == hostageSounds_.end()) {
                    return fail("Hostage cue has no preloaded sound");
                }
                if (cue.action == game::HostageSoundAction::StopLoop) {
                    result = stopNamedAudio(record->eventName);
                } else {
                    result = playSpatialSound(
                        cue.hostageObjectId, record->eventName, clip->second,
                        cue.action == game::HostageSoundAction::StartLoop);
                }
                if (!result) {
                    return fail(result.message());
                }
            }
            objectRuntime_.updateComicCollections(
                gameplayPlayer_.position());
            result = dispatchObjectEvents();
            if (!result) {
                return fail(result.message());
            }
            restoreRuntime_.update(gameplayPlayer_.position(),
                                   gameDeltaMilliseconds,
                                   !gameplayPlayer_.dead() &&
                                       !levelDeathRuntime_.active(),
                                   gameplayPlayer_.canEnableTriggerRestore());
            for (const game::LevelRestoreEvent& event :
                 restoreRuntime_.consumeEvents()) {
                if (event.trigger == nullptr || event.restorePoint == nullptr) {
                    return fail("TriggerRestore emitted an invalid event");
                }
                (void)gameplayPlayer_.applyDamage(event.trigger->damage);
                if (autoplay) {
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds, "restore",
                        "trigger=" +
                            std::to_string(event.trigger->objectId) +
                            ";point=" +
                            std::to_string(event.restorePoint->objectId) +
                            ";damage=" +
                            std::to_string(event.trigger->damage) +
                            ";cinematic=" +
                            std::to_string(event.trigger->cinematicId) +
                            ";fall_after_restore=" +
                            std::to_string(
                                event.trigger->fallAfterRestore) +
                            ";use_last_checkpoint=" +
                            std::to_string(
                                event.trigger->useLastCheckpoint));
                }
                if (!gameplayPlayer_.dead()) {
                    // CTriggerRestore::Restoreplayer (0x0036bc24) starts the
                    // linked cinematic before CRestorePoint moves Player.
                    if (event.trigger->cinematicId > 0) {
                        result = startGameplayCinematic(
                            event.trigger->cinematicId);
                        if (!result) {
                            return fail(result.message());
                        }
                    }
                    gameplayPlayer_.restoreAt(event.restorePoint->position,
                                              event.restorePoint->facing);
                }
            }
            for (std::string_view enteredState =
                     gameplayPlayer_.consumeEnteredState();
                 !enteredState.empty();
                 enteredState = gameplayPlayer_.consumeEnteredState()) {
                result = playerSounds_.dispatchStateEnter(
                    enteredState, playGameplaySound, stopPlayerStateSound);
                if (!result) {
                    return fail(result.message());
                }
            }
            for (std::optional<game::PlayerAttackSoundTrigger> trigger =
                     gameplayPlayer_.consumeAttackSoundTrigger();
                 trigger.has_value();
                 trigger = gameplayPlayer_.consumeAttackSoundTrigger()) {
                result = playerSounds_.dispatchEmitter(
                    trigger->soundConfigId, trigger->emitterIndex,
                    playGameplaySound);
                if (!result) {
                    return fail(result.message());
                }
                if (autoplay) {
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds,
                        "player_sound_emitter",
                        "state=" + std::string(trigger->stateName) +
                            ";config=" +
                            std::to_string(trigger->soundConfigId) +
                            ";emitter_index=" +
                            std::to_string(trigger->emitterIndex));
                }
            }
            for (std::optional<game::PlayerHitEffectSpawnEvent> effect =
                     gameplayPlayer_.consumeHitEffectSpawn();
                 effect.has_value();
                 effect = gameplayPlayer_.consumeHitEffectSpawn()) {
                if (autoplay) {
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds,
                        "player_hit_mesh_effect",
                        "id=" + std::to_string(effect->effectId) +
                            ";name=" + std::string(effect->effectName) +
                            ";bone=" + std::string(effect->boneName) +
                            ";lifetime_ms=" +
                            std::to_string(effect->lifetimeMilliseconds) +
                            ";fade_ms=" +
                            std::to_string(effect->fadeDurationMilliseconds) +
                            ";scale=" +
                            std::to_string(effect->uniformScale) +
                            ";follows_bone=" +
                            std::to_string(effect->followsPlayerBone) +
                            ";material=" +
                            (effect->additiveModulateMaterial ? "0x1d"
                                                              : "0x1e") +
                            ";velocity_x=" +
                            std::to_string(
                                effect->capturedPhysicsVelocity.x) +
                            ";velocity_y=" +
                            std::to_string(
                                effect->capturedPhysicsVelocity.y) +
                            ";velocity_z=" +
                            std::to_string(
                                effect->capturedPhysicsVelocity.z));
                }
            }
            for (std::optional<game::PlayerVoxStopEvent> stop =
                     gameplayPlayer_.consumeVoxStopEvent();
                 stop.has_value();
                 stop = gameplayPlayer_.consumeVoxStopEvent()) {
                const audio::VoxSoundRecord* record = voxSounds_.find(
                    static_cast<std::uint16_t>(stop->voxSoundId));
                if (record == nullptr || stop->voxSoundId != 0x53) {
                    return fail("Player combat state has an invalid direct "
                                "VoxSound stop ID");
                }
                result = stopNamedAudio(record->eventName);
                if (!result) {
                    return fail(result.message());
                }
                if (autoplay) {
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds,
                        "player_vox_stop",
                        "state=" + std::to_string(stop->stateId) +
                            ";name=" + std::string(stop->stateName) +
                            ";vox=" + std::to_string(stop->voxSoundId) +
                            ";event=" + record->eventName);
                }
            }
            for (std::optional<game::PlayerCombatEffectEvent> effect =
                     gameplayPlayer_.consumeCombatEffect();
                 effect.has_value();
                 effect = gameplayPlayer_.consumeCombatEffect()) {
                result = effectRuntime_.playEffect(effect->effectType,
                                                   effect->origin);
                if (!result) {
                    return fail(result.message());
                }
                if (effect->voxSoundId >= 0) {
                    const audio::VoxSoundRecord* record = voxSounds_.find(
                        static_cast<std::uint16_t>(effect->voxSoundId));
                    if (record == nullptr || effect->voxSoundId != 0x54) {
                        return fail("Player combat effect has an invalid "
                                    "direct VoxSound ID");
                    }
                    result = playGameplaySound(
                        effect->voxSoundId, record->eventName,
                        ultimateSplashSound_, false);
                    if (!result) {
                        return fail(result.message());
                    }
                }
                if (autoplay) {
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds,
                        "player_combat_effect",
                        "type=" + std::string(effect->effectType) +
                            ";vox=" +
                            std::to_string(effect->voxSoundId) +
                            ";x=" + std::to_string(effect->origin.x) +
                            ";y=" + std::to_string(effect->origin.y) +
                            ";z=" + std::to_string(effect->origin.z));
                }
            }
            for (std::optional<game::PlayerEnemyNotifyEvent> notify =
                     gameplayPlayer_.consumeEnemyNotifyEvent();
                 notify.has_value();
                 notify = gameplayPlayer_.consumeEnemyNotifyEvent()) {
                const game::LevelEnemyState* notifyTarget =
                    enemyRuntime_.find(notify->targetedEnemyObjectId);
                const bool accepted = notify->hitType == 0x71
                    ? enemyRuntime_.applyPlayerAirKnockdownBinding(
                          notify->targetedEnemyObjectId)
                    : notifyTarget != nullptr && notifyTarget->visible &&
                          notifyTarget->health > 0.0F;
                if (autoplay) {
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds,
                        "player_enemy_notify",
                        "state=" + std::to_string(notify->stateId) +
                            ";name=" + std::string(notify->stateName) +
                            ";target=" +
                            std::to_string(notify->targetedEnemyObjectId) +
                            ";message=0x12d;hit_type=" +
                            std::to_string(notify->hitType) +
                            ";behavior_message=" +
                            std::to_string(notify->behaviorMessage) +
                            ";behavior_state=" +
                            std::to_string(notify->behaviorState) +
                            ";accepted=" +
                            std::to_string(accepted ? 1 : 0));
                }
            }
            const auto playPlayerHitEffect =
                [&](std::int32_t enemyObjectId,
                    const assets::Vector3& origin, std::int16_t hitType,
                    float incomingDamage,
                    std::string_view explicitEffectType = {}) -> Result {
                // CEnemy::ProcessHitInfo (0x00330fe4) tests the incoming
                // AIHitTargetInfo damage before it creates a contact splash.
                // Retained web-bind messages deliberately carry zero damage
                // and therefore keep their gameplay contact without FX.
                if (!(incomingDamage > 0.0F) ||
                    !std::isfinite(incomingDamage)) {
                    return Result::success();
                }
                const game::LevelEnemyState* enemy =
                    enemyRuntime_.find(enemyObjectId);
                if (enemy == nullptr || enemy->asset == nullptr) {
                    return Result::success();
                }
                // ProcessHitInfo requests the ordinary cartoon splash for
                // accepted hits and the big variant for native hit types
                // 0x79/0x6a (0x89 is first normalized to 0x79).
                const std::string_view effectType =
                    !explicitEffectType.empty()
                        ? explicitEffectType
                        : (hitType == 121 || hitType == 106 ||
                                   hitType == 137
                               ? "cartoon_hit_splash_big"
                               : "cartoon_hit_splash");
                if (autoplay) {
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds,
                        "player_hit_effect",
                        "enemy=" + std::to_string(enemyObjectId) +
                            ";type=" + std::string(effectType) + ";x=" +
                            std::to_string(origin.x) + ";y=" +
                            std::to_string(origin.y) + ";z=" +
                            std::to_string(origin.z));
                }
                return effectRuntime_.playEffect(effectType, origin,
                                                 enemy->asset->roomId);
            };
            for (std::optional<game::PlayerMeleeImpact> impact =
                     gameplayPlayer_.consumeMeleeImpact();
                 impact.has_value();
                 impact = gameplayPlayer_.consumeMeleeImpact()) {
                if (impact->senseAttack) {
                    // CheckAttackTarget resets the forced Spider-Sense slow
                    // motion on the counter's authored contact frame.
                    levelCinematicRuntime_.resetSlowMotion();
                }
                if (impact->senseAttack) {
                    const float sectorHalfAngle = std::max(
                        std::abs(impact->minimumAngleDegrees),
                        std::abs(impact->maximumAngleDegrees));
                    const float minimumForwardDot = std::cos(
                        sectorHalfAngle * 0.017453292519943295F);
                    const auto hits = enemyRuntime_.applyPlayerSenseMeleeHits(
                        impact->attackPosition, impact->attackDirection,
                        impact->maximumReach, impact->damage,
                        minimumForwardDot, impact->targetedEnemyObjectId,
                        impact->hitType, impact->horizontalForce,
                        impact->verticalForce);
                    const auto objectHits = objectRuntime_.applyPlayerMeleeHits(
                        impact->attackPosition, impact->attackDirection,
                        impact->maximumReach, impact->damage,
                        minimumForwardDot);
                    for (const auto& hit : hits) {
                        gameplayPlayer_.addCombo(
                            hit.actualDamage, impact->ultimateAttack,
                            syntheticElapsedMilliseconds,
                            !impact->powerRestoreBlocked);
                        result = playPlayerHitEffect(
                            hit.objectId, hit.hitEffectOrigin,
                            impact->hitType, impact->damage);
                        if (!result) {
                            return fail(result.message());
                        }
                    }
                    for (const auto& hit : objectHits) {
                        gameplayPlayer_.addCombo(
                            hit.actualDamage, impact->ultimateAttack,
                            syntheticElapsedMilliseconds,
                            !impact->powerRestoreBlocked);
                    }
                    if (!hits.empty() || !objectHits.empty()) {
                        result = playerSounds_.dispatchStateFrame(
                            impact->stateName, playGameplaySound);
                        if (!result) {
                            return fail(result.message());
                        }
                    }
                    if (autoplay) {
                        std::string detail =
                            "state=" + std::to_string(impact->stateId) +
                            ";name=" + std::string(impact->stateName) +
                            ";delivery=sense_sector_guaranteed;damage=" +
                            std::to_string(impact->damage) + ";reach=" +
                            std::to_string(impact->maximumReach) +
                            ";target=" +
                            std::to_string(impact->targetedEnemyObjectId) +
                            ";hits=" + std::to_string(hits.size() +
                                                       objectHits.size());
                        for (const auto& hit : hits) {
                            detail += ";enemy=" +
                                std::to_string(hit.objectId) +
                                ";actual_damage=" +
                                std::to_string(hit.actualDamage);
                        }
                        for (const auto& hit : objectHits) {
                            detail += ";object=" +
                                std::to_string(hit.objectId) +
                                ";actual_damage=" +
                                std::to_string(hit.actualDamage);
                        }
                        autoplay->recordEvent(
                            syntheticElapsedMilliseconds,
                            "player_melee_impact", detail);
                    }
                    continue;
                }
                if (impact->radialAttack) {
                    const auto hits = enemyRuntime_.applyPlayerRadialMeleeHits(
                        impact->attackPosition, impact->maximumReach,
                        impact->damage, impact->hitType,
                        impact->horizontalForce, impact->verticalForce);
                    for (const auto& hit : hits) {
                        gameplayPlayer_.addCombo(
                            hit.actualDamage, impact->ultimateAttack,
                            syntheticElapsedMilliseconds,
                            !impact->powerRestoreBlocked);
                        result = playPlayerHitEffect(
                            hit.objectId, hit.hitEffectOrigin,
                            impact->hitType, impact->damage);
                        if (!result) {
                            return fail(result.message());
                        }
                    }
                    if (!hits.empty()) {
                        result = playerSounds_.dispatchStateFrame(
                            impact->stateName, playGameplaySound);
                        if (!result) {
                            return fail(result.message());
                        }
                    }
                    if (autoplay) {
                        std::string detail =
                            "state=" + std::to_string(impact->stateId) +
                            ";name=" + std::string(impact->stateName) +
                            ";delivery=radial;damage=" +
                            std::to_string(impact->damage) +
                            ";reach=" +
                            std::to_string(impact->maximumReach) +
                            ";hit_type=" +
                            std::to_string(impact->hitType) +
                            ";horizontal_force=" +
                            std::to_string(impact->horizontalForce) +
                            ";vertical_force=" +
                            std::to_string(impact->verticalForce) +
                            ";hits=" + std::to_string(hits.size());
                        for (const auto& hit : hits) {
                            detail += ";enemy=" +
                                std::to_string(hit.objectId) +
                                ";actual_damage=" +
                                std::to_string(hit.actualDamage);
                        }
                        autoplay->recordEvent(
                            syntheticElapsedMilliseconds,
                            "player_melee_impact", detail);
                    }
                    continue;
                }
                if (impact->wallAttack) {
                    const auto hits = enemyRuntime_.applyPlayerWallMeleeHits(
                        impact->attackPosition, impact->wallNormal,
                        impact->attackDirection, impact->maximumReach,
                        impact->damage, impact->minimumAngleDegrees,
                        impact->maximumAngleDegrees, impact->hitType,
                        impact->horizontalForce, impact->verticalForce);
                    for (const auto& hit : hits) {
                        gameplayPlayer_.addCombo(
                            hit.actualDamage, false,
                            syntheticElapsedMilliseconds,
                            !impact->powerRestoreBlocked);
                        result = playPlayerHitEffect(
                            hit.objectId, hit.hitEffectOrigin,
                            impact->hitType, impact->damage);
                        if (!result) {
                            return fail(result.message());
                        }
                    }
                    if (!hits.empty()) {
                        result = playerSounds_.dispatchStateFrame(
                            impact->stateName, playGameplaySound);
                        if (!result) {
                            return fail(result.message());
                        }
                    }
                    if (autoplay) {
                        std::string detail = "state=" + std::to_string(impact->stateId) +
                            ";name=" + std::string(impact->stateName) +
                            ";delivery=wall_sector;damage=" + std::to_string(impact->damage) +
                            ";reach=" + std::to_string(impact->maximumReach) +
                            ";center=" + std::to_string(impact->attackPosition.x) + "|" +
                                std::to_string(impact->attackPosition.y) + "|" +
                                std::to_string(impact->attackPosition.z) +
                            ";normal=" + std::to_string(impact->wallNormal.x) + "|" +
                                std::to_string(impact->wallNormal.y) + "|" +
                                std::to_string(impact->wallNormal.z) +
                            ";direction=" + std::to_string(impact->attackDirection.x) + "|" +
                                std::to_string(impact->attackDirection.y) + "|" +
                                std::to_string(impact->attackDirection.z) +
                            ";angles=" + std::to_string(impact->minimumAngleDegrees) + "|" +
                                std::to_string(impact->maximumAngleDegrees) +
                            ";hits=" + std::to_string(hits.size());
                        for (const auto& hit : hits) {
                            detail += ";enemy=" + std::to_string(hit.objectId) +
                                ";actual_damage=" + std::to_string(hit.actualDamage);
                        }
                        autoplay->recordEvent(syntheticElapsedMilliseconds,
                                              "player_melee_impact", detail);
                    }
                    continue;
                }
                const float sectorHalfAngle = std::max(
                    std::abs(impact->minimumAngleDegrees),
                    std::abs(impact->maximumAngleDegrees));
                const float minimumForwardDot =
                    std::cos(sectorHalfAngle * 0.017453292519943295F);
                std::vector<game::PlayerMeleeHitResult> hitEnemies;
                if (impact->bindsEnemy &&
                    impact->targetedEnemyObjectId >= 0) {
                    if (const auto hit =
                            enemyRuntime_.applyPlayerWebBindingDetailed(
                                impact->targetedEnemyObjectId,
                                impact->damage)) {
                        hitEnemies.push_back(*hit);
                    }
                } else if (impact->targetedDelivery &&
                           impact->targetedEnemyObjectId >= 0) {
                    if (const auto hit =
                            enemyRuntime_.applyPlayerTargetedHitDetailed(
                                impact->targetedEnemyObjectId, impact->damage,
                                impact->hitType, &impact->attackPosition,
                                impact->horizontalForce,
                                impact->verticalForce)) {
                        hitEnemies.push_back(*hit);
                    }
                } else if (impact->airKickDownSplit &&
                           impact->targetedEnemyObjectId >= 0) {
                    hitEnemies =
                        enemyRuntime_.applyPlayerAirKickDownSectorMeleeHits(
                            impact->attackPosition, impact->attackDirection,
                            impact->maximumReach, impact->damage,
                            minimumForwardDot,
                            impact->targetedEnemyObjectId,
                            &impact->attackPosition,
                            impact->horizontalForce,
                            impact->verticalForce);
                } else {
                    hitEnemies = enemyRuntime_.applyPlayerSectorMeleeHits(
                        impact->attackPosition, impact->attackDirection,
                        impact->maximumReach, impact->damage,
                        minimumForwardDot, impact->hitType,
                        &impact->attackPosition, impact->horizontalForce,
                        impact->verticalForce);
                }
                // Player::CheckAttackTarget (0x0034fca0) enumerates the
                // intersecting destroyable list independently of Units. A
                // Unit contact therefore must not suppress an object contact.
                const auto hitObjects = objectRuntime_.applyPlayerMeleeHits(
                    impact->attackPosition, impact->attackDirection,
                    impact->maximumReach, impact->damage,
                    minimumForwardDot);
                for (const auto& hitEnemy : hitEnemies) {
                    // Player::SendHitMessage (0x00345fe8) adds the target's
                    // measured health delta, not the authored attack damage.
                    gameplayPlayer_.addCombo(
                        hitEnemy.actualDamage, impact->ultimateAttack,
                        syntheticElapsedMilliseconds,
                        !impact->powerRestoreBlocked);
                    result = playPlayerHitEffect(
                        hitEnemy.objectId, hitEnemy.hitEffectOrigin,
                        impact->hitType, impact->damage);
                    if (!result) {
                        return fail(result.message());
                    }
                }
                for (const auto& hitObject : hitObjects) {
                    gameplayPlayer_.addCombo(
                        hitObject.actualDamage, impact->ultimateAttack,
                        syntheticElapsedMilliseconds,
                        !impact->powerRestoreBlocked);
                }
                if (!hitEnemies.empty() || !hitObjects.empty()) {
                    gameplayPlayer_.notifyMeleeImpactAccepted(
                        impact->stateId);
                    result = playerSounds_.dispatchStateFrame(
                        impact->stateName, playGameplaySound);
                    if (!result) {
                        return fail(result.message());
                    }
                }
                if (autoplay) {
                    std::string detail =
                        "state=" + std::to_string(impact->stateId) +
                        ";name=" + std::string(impact->stateName) +
                        ";damage=" + std::to_string(impact->damage) +
                        ";reach=" + std::to_string(impact->maximumReach) +
                        ";hit_type=" + std::to_string(impact->hitType) +
                        ";horizontal_force=" +
                            std::to_string(impact->horizontalForce) +
                            ";vertical_force=" +
                            std::to_string(impact->verticalForce) +
                        ";origin=" +
                            std::to_string(impact->attackPosition.x) + "|" +
                            std::to_string(impact->attackPosition.y) + "|" +
                            std::to_string(impact->attackPosition.z) +
                        ";delivery=" +
                        std::string(impact->targetedDelivery
                                        ? "targeted_web"
                                        : impact->airKickDownSplit
                                            ? "air_kick_down_split"
                                        : impact->webAttack ? "web_sector"
                                                            : "sector") +
                        ";target=" +
                        std::to_string(impact->targetedEnemyObjectId) +
                        ";bind=" +
                        std::to_string(impact->bindsEnemy ? 1 : 0) +
                        ";hits=" + std::to_string(hitEnemies.size() +
                                                  hitObjects.size());
                    if (impact->airKickDownSplit) {
                        detail +=
                            ";collateral_hit_type=121"
                            ";collateral_horizontal_force=200.000000"
                            ";collateral_vertical_force=500.000000";
                    }
                    detail += std::string(";result=") +
                        (hitEnemies.empty() && hitObjects.empty()
                             ? "miss"
                             : "contact");
                    const std::int32_t trackedTargetObjectId =
                        gameplayPlayer_.trackedAttackTargetObjectId();
                    detail += ";tracked_target=" +
                        std::to_string(trackedTargetObjectId);
                    if (const auto* trackedTarget =
                            enemyRuntime_.find(trackedTargetObjectId);
                        trackedTarget != nullptr) {
                        detail +=
                            ";tracked_base=" +
                            std::to_string(trackedTarget->position.x) + "|" +
                            std::to_string(trackedTarget->position.y) + "|" +
                            std::to_string(trackedTarget->position.z) +
                            ";tracked_size=" +
                            std::to_string(trackedTarget->collisionRadius) +
                            "|" +
                            std::to_string(trackedTarget->collisionHeight) +
                            ";tracked_anim=" + trackedTarget->activeAnimation +
                            ";tracked_anim_ms=" +
                            std::to_string(
                                trackedTarget->animationTimeMilliseconds);
                        if (const auto bip = enemyRuntime_.nodeWorldPosition(
                                trackedTargetObjectId, "Bip01")) {
                            detail += ";tracked_bip=" +
                                std::to_string(bip->x) + "|" +
                                std::to_string(bip->y) + "|" +
                                std::to_string(bip->z);
                        }
                        if (const auto spine = enemyRuntime_.nodeWorldPosition(
                                trackedTargetObjectId, "Bip01_Spine1")) {
                            detail += ";tracked_spine=" +
                                std::to_string(spine->x) + "|" +
                                std::to_string(spine->y) + "|" +
                                std::to_string(spine->z);
                        }
                    } else if (const auto* trackedObject =
                                   objectRuntime_.find(
                                       trackedTargetObjectId);
                               trackedObject != nullptr &&
                               trackedObject->asset != nullptr) {
                        detail +=
                            ";tracked_base=" +
                            std::to_string(trackedObject->position.x) + "|" +
                            std::to_string(trackedObject->position.y) + "|" +
                            std::to_string(trackedObject->position.z) +
                            ";tracked_size=" +
                            std::to_string(
                                trackedObject->asset->collisionRadius) +
                            "|" +
                            std::to_string(
                                trackedObject->asset->collisionHeight) +
                            ";tracked_kind=destroyable";
                    }
                    if (!hitEnemies.empty() || !hitObjects.empty()) {
                        for (const auto& hit : hitEnemies) {
                            detail += ";enemy=" +
                                std::to_string(hit.objectId) +
                                ";actual_damage=" +
                                std::to_string(hit.actualDamage);
                        }
                        for (const auto& hit : hitObjects) {
                            detail += ";object=" +
                                std::to_string(hit.objectId) +
                                ";actual_damage=" +
                                std::to_string(hit.actualDamage);
                        }
                    }
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds,
                        "player_melee_impact", detail);
                }
            }
            for (std::optional<game::PlayerWebPelletLaunch> launch =
                     gameplayPlayer_.consumeWebPelletLaunch();
                 launch.has_value();
                 launch = gameplayPlayer_.consumeWebPelletLaunch()) {
                const bool launched = enemyRuntime_.launchPlayerWebPellet(
                    launch->origin, launch->targetPosition,
                    launch->targetedEnemyObjectId, launch->damage);
                if (autoplay) {
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds,
                        "player_web_pellet_launch",
                        "state=" + std::to_string(launch->stateId) +
                            ";name=" + std::string(launch->stateName) +
                            ";accepted=" + std::to_string(launched) +
                            ";target=" +
                            std::to_string(
                                launch->targetedEnemyObjectId) +
                            ";damage=" + std::to_string(launch->damage) +
                            ";origin_x=" +
                            std::to_string(launch->origin.x) +
                            ";origin_y=" +
                            std::to_string(launch->origin.y) +
                            ";origin_z=" +
                            std::to_string(launch->origin.z) +
                            ";target_x=" +
                            std::to_string(launch->targetPosition.x) +
                            ";target_y=" +
                            std::to_string(launch->targetPosition.y) +
                            ";target_z=" +
                            std::to_string(launch->targetPosition.z));
                }
            }
            // CEnemy::ProcessHitInfo creates the contact emitter during the
            // player/enemy update, before the native scene is rendered. The
            // main effect tick runs earlier in this portable frame, so flush
            // newly created zero-delay emitters without advancing time. This
            // keeps the authored hit splash on the contact frame instead of
            // presenting it one 25/50 ms frame late.
            gameplayPlayer_.updateComboState(syntheticElapsedMilliseconds);
            (void)gameplayCamera_.updateArea(gameplayPlayer_.position(),
                                             gameDeltaMilliseconds);
            if (const std::optional<game::CheckPointActivation> checkPoint =
                    checkPointRuntime_.update(
                        gameplayPlayer_.position(), gameplayPlayer_.facing(),
                        gameplayPlayer_.health(),
                        gameplayCamera_.currentAreaId())) {
                captureRuntimeCheckPoint(checkPoint->objectId);
                if (autoplay) {
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds, "checkpoint",
                        "object=" + std::to_string(checkPoint->objectId) +
                            ";camera_area=" +
                            std::to_string(checkPoint->cameraAreaId) +
                            ";x=" + std::to_string(
                                         checkPoint->playerPosition.x) +
                            ";y=" + std::to_string(
                                         checkPoint->playerPosition.y) +
                            ";z=" + std::to_string(
                                         checkPoint->playerPosition.z) +
                            ";automatic=1");
                }
            }
            const auto triggerEvents =
                triggerRuntime_.update(gameplayPlayer_.position(),
                                       renderer_.roomVisibility());
            if (autoplay) {
                for (const game::TriggerEvent& event : triggerEvents) {
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds, "trigger",
                        "trigger=" + std::to_string(event.triggerId) +
                            ";cinematic=" +
                            std::to_string(event.cinematicId));
                }
            }
            for (const game::TriggerSoundEvent& event :
                 triggerSoundRuntime_.update(gameplayPlayer_.position())) {
                const auto clip = triggerSoundClips_.find(event.eventName);
                if (clip == triggerSoundClips_.end()) {
                    return fail("TriggerSound clip was not preloaded");
                }
                const std::string voiceName =
                    "TriggerSound:" + std::to_string(event.triggerId);
                if (event.kind == game::TriggerSoundEventKind::Started) {
                    const audio::VoxSoundRecord* record =
                        voxSounds_.find(event.eventName);
                    const float volume =
                        audio::GameAudioMix::DefaultSoundEffectsGroupVolume *
                        (record != nullptr && record->id == 0xa1 ? 0.5F
                                                                 : 1.0F);
                    result = playNamedAudio(voiceName, clip->second, true,
                                            volume, 500, event.eventName);
                } else {
                    result = stopNamedAudio(voiceName, 500,
                                            event.eventName);
                }
                if (!result) {
                    return fail(result.message());
                }
            }
            if (!quickTimeEvent_.active()) {
                for (const game::TriggerEvent& event : triggerEvents) {
                    result = startGameplayCinematic(event.cinematicId);
                    if (!result) {
                        break;
                    }
                }
            }
            if (result && !gameplayPlayer_.dead()) {
                Result commandResult = Result::success();
                bool cinematicDamageApplied = false;
                result = gameplayCinematics_.update(
                    gameDeltaMilliseconds,
                    [this, &commandResult, &cinematicDamageApplied,
                     &playSpatialSound, &autoplay, &traceTimeMilliseconds,
                     &playNamedAudio, &stopNamedAudio,
                     &saveCinematicCheckPoint](
                        const game::LevelCinematicAsset& cinematic,
                        const game::CinematicThread& thread,
                        const game::CinematicCommand& command) {
                        if (autoplay) {
                            autoplay->recordCommand(traceTimeMilliseconds,
                                                    cinematic.objectId,
                                                    thread.objectId, command);
                        }
                        if (command.name == "IfEnemyDead") {
                            return enemyRuntime_.cinematicEnemyDead(thread,
                                                                    command);
                        }
                        if (command.name == "IfObjectDestroyed") {
                            std::int32_t objectId = thread.objectId;
                            (void)commandInteger(command, "ObjectID", objectId);
                            const game::LevelEnemyState* enemy =
                                enemyRuntime_.find(objectId);
                            return (enemy != nullptr &&
                                    enemy->health <= 0.0F) ||
                                   objectRuntime_.isDestroyed(objectId);
                        }
                        if (commandResult) {
                            if (autoplay && command.name == "SetAnim" &&
                                thread.type == 3) {
                                const game::CinematicAttribute* animation =
                                    command.findAttribute("$Anim");
                                if (animation != nullptr &&
                                    levelOne_.player().animationBank.findClip(
                                        animation->value) == nullptr) {
                                    autoplay->recordEvent(
                                        traceTimeMilliseconds,
                                        "cinematic_animation_unresolved",
                                        "target=player;cinematic=" +
                                            std::to_string(
                                                cinematic.objectId) +
                                            ";thread=" +
                                            std::to_string(thread.objectId) +
                                            ";name=" + animation->value +
                                            ";native_result=no_op");
                                }
                            }
                            const float healthBefore = gameplayPlayer_.health();
                            commandResult =
                                gameplayPlayer_.applyCinematicCommand(thread,
                                                                        command);
                            if (!commandResult && autoplay) {
                                autoplay->recordEvent(
                                    traceTimeMilliseconds,
                                    "cinematic_command_failure",
                                    "stage=player;cinematic=" +
                                        std::to_string(cinematic.objectId) +
                                        ";thread=" +
                                        std::to_string(thread.objectId) +
                                        ";command=" + command.name +
                                        ";error=" + commandResult.message());
                            }
                            cinematicDamageApplied =
                                cinematicDamageApplied ||
                                gameplayPlayer_.health() < healthBefore;
                            if (commandResult && autoplay &&
                                command.name == "MoveObject" &&
                                thread.type == 3) {
                                autoplay->recordEvent(
                                    traceTimeMilliseconds,
                                    "player_move_object",
                                    "command_ms=" +
                                        std::to_string(
                                            command.timestampMilliseconds) +
                                        ";x=" +
                                        std::to_string(
                                            gameplayPlayer_.position().x) +
                                        ";y=" +
                                        std::to_string(
                                            gameplayPlayer_.position().y) +
                                        ";z=" +
                                        std::to_string(
                                            gameplayPlayer_.position().z) +
                                        ";motion=" +
                                        std::to_string(gameplayPlayer_
                                                           .cinematicMotionActive()) +
                                        ";motion_ms=" +
                                        std::to_string(gameplayPlayer_
                                                           .cinematicMotionElapsedMilliseconds()) +
                                        ";motion_duration_ms=" +
                                        std::to_string(gameplayPlayer_
                                                           .cinematicMotionDurationMilliseconds()));
                            }
                            if (commandResult && autoplay &&
                                command.name == "Enable_Slide") {
                                std::int32_t slideId = -1;
                                if (commandInteger(command, "^ID^Slide",
                                                   slideId)) {
                                    const std::optional<bool> enabled =
                                        gameplayPlayer_.slideEnabled(slideId);
                                    autoplay->recordEvent(
                                        traceTimeMilliseconds,
                                        "slide_state",
                                        "object=" + std::to_string(slideId) +
                                            ";found=" +
                                            (enabled.has_value() ? "1" : "0") +
                                            ";enabled=" +
                                            (enabled.value_or(false) ? "1"
                                                                     : "0"));
                                }
                            }
                        }
                        if (commandResult) {
                            commandResult = objectRuntime_.applyCinematicCommand(
                                levelOne_, thread, command);
                        }
                        if (commandResult) {
                            commandResult =
                                restoreRuntime_.applyCinematicCommand(thread,
                                                                      command);
                            if (commandResult && autoplay &&
                                command.name == "EnableTriggerRestore") {
                                std::int32_t triggerId = -1;
                                if (commandInteger(command,
                                                   "^ID^TriggerRestore",
                                                   triggerId)) {
                                    const std::optional<bool> enabled =
                                        restoreRuntime_.enabled(triggerId);
                                    autoplay->recordEvent(
                                        traceTimeMilliseconds,
                                        "restore_trigger_state",
                                        "object=" +
                                            std::to_string(triggerId) +
                                            ";found=" +
                                            (enabled.has_value() ? "1" : "0") +
                                            ";enabled=" +
                                            (enabled.value_or(false) ? "1"
                                                                     : "0"));
                                }
                            }
                        }
                        if (commandResult) {
                            commandResult =
                                hintRuntime_.applyCinematicCommand(thread,
                                                                    command);
                        }
                        if (commandResult) {
                            commandResult = enemyRuntime_.applyCinematicCommand(
                                levelOne_, thread, command);
                        }
                        if (commandResult) {
                            commandResult =
                                effectRuntime_.applyCinematicCommand(command);
                        }
                        if (commandResult) {
                            commandResult =
                                quickTimeEvent_.applyCommand(
                                    command, cinematic.objectId);
                        }
                        if (commandResult) {
                            commandResult = levelCinematicRuntime_.applyCommand(
                                thread, command);
                            if (commandResult && autoplay &&
                                command.name == "ListenerPosition") {
                                autoplay->recordEvent(
                                    traceTimeMilliseconds,
                                    "listener_position_state",
                                    "on_main_character=" +
                                        std::to_string(
                                            levelCinematicRuntime_
                                                .listenerOnMainCharacter()));
                            }
                        }
                        if (commandResult) {
                            commandResult = saveCinematicCheckPoint(command);
                        }
                        if (commandResult) {
                            commandResult = cinematicUi_.applyCommand(command);
                        }
                        if (commandResult) {
                            commandResult = gameplaySounds_.dispatch(
                                command,
                                [&playNamedAudio](
                                    std::string_view eventName,
                                    const audio::PcmAudio& clip, bool loop) {
                                    return playNamedAudio(eventName, clip,
                                                          loop);
                                },
                                [&stopNamedAudio](std::string_view eventName) {
                                    return stopNamedAudio(eventName);
                                },
                                [&thread, &playSpatialSound](
                                    std::string_view eventName,
                                    const audio::PcmAudio& clip, bool loop) {
                                    return playSpatialSound(
                                        thread.objectId, eventName, clip, loop);
                                });
                        }
                        return true;
                    });
                if (result && !commandResult) {
                    result = commandResult;
                }
                if (result && cinematicDamageApplied) {
                    result = playerSounds_.dispatchStateEnter(
                        "k_state_hurt_light", playGameplaySound,
                        stopPlayerStateSound);
                }
                for (const game::LevelCinematicAsset* completedCinematic :
                     gameplayCinematics_.consumeCompletions()) {
                    if (completedCinematic->hasColladaPlayback()) {
                        releaseGameplayCollada(
                            *completedCinematic,
                            std::numeric_limits<std::uint32_t>::max());
                        levelCinematicRuntime_.completeColladaPlayback(
                            completedCinematic->levelEndAfterPlayback,
                            completedCinematic->gameEndAfterPlayback);
                    }
                    const bool terminalCinematic =
                        completedCinematic->levelEndAfterPlayback ||
                        completedCinematic->gameEndAfterPlayback ||
                        levelCinematicRuntime_.levelEnded() ||
                        levelCinematicRuntime_.gameEnded();
                    if (terminalCinematic) {
                        // Preserve the terminal Collada pose through the last
                        // present. CLevel::End/GameEnd then leave the original
                        // level state; this first-level port exits cleanly.
                        exitAfterPresent = true;
                    } else {
                        gameplayCinematics_.remove(
                            completedCinematic->objectId);
                    }
                    if (!terminalCinematic &&
                        completedCinematic->nextCinematicId >= 0) {
                        result = startGameplayCinematic(
                            completedCinematic->nextCinematicId);
                        if (!result) {
                            break;
                        }
                    }
                }
                for (const std::int32_t requestedCinematic :
                     levelCinematicRuntime_.consumeCinematicStartRequests()) {
                    if (result) {
                        result = startGameplayCinematic(requestedCinematic);
                    }
                }
            }
            if (result) {
                result = dispatchObjectEvents();
            }
            quickTimeEvent_.update(
                gameDeltaMilliseconds,
                autoplay ? autoplayInput.quickTimeEventPressed
                         : keyRouter_.state().quickTimeEvent.pressed);
            if (const auto qteCinematic =
                    quickTimeEvent_.consumeCinematicRequest()) {
                levelCinematicRuntime_.endQuickTimeEvent();
                const std::int32_t sourceId =
                    quickTimeEvent_.sourceCinematicId();
                if (const auto* source = gameplayCinematics_.playback(sourceId)) {
                    releaseGameplayCollada(*source->asset,
                                           source->elapsedMilliseconds);
                    gameplayCinematics_.remove(sourceId);
                    if (autoplay) {
                        autoplay->recordEvent(
                            traceTimeMilliseconds, "qte_cinematic_handoff",
                            "source=" + std::to_string(sourceId) +
                                ";outcome=" + std::to_string(*qteCinematic));
                    }
                }
                result = startGameplayCinematic(*qteCinematic);
            }
            if (!gameplayPlayer_.dead() &&
                !gameplayCinematics_.hasActiveColladaPlayback()) {
                enemyRuntime_.updateGameplay(gameDeltaMilliseconds,
                                             gameplayPlayer_.position(),
                                             &levelCollision_,
                                             autoplay
                                                 ? autoplayInput
                                                       .quickTimeEventPressed
                                                 : keyRouter_.state()
                                                       .quickTimeEvent.pressed,
                                              gameplayPlayer_.facing(),
                                              gameplayPlayer_.onWall(),
                                              gameplayPlayer_.senseReactState(),
                                              gameplayPlayer_.nodeWorldPosition(
                                                  "Bip01_Spine2"));
            }
            const game::BossProgressState& bossProgress =
                levelCinematicRuntime_.bossProgress();
            const game::LevelEnemyState* progressBoss =
                enemyRuntime_.find(bossProgress.bossObjectId);
            levelCinematicRuntime_.advanceBossProgress(
                gameDeltaMilliseconds, gameplayPlayer_.position(),
                progressBoss == nullptr ? nullptr : &progressBoss->position);
            const game::QuickTimeActionRuntime& rhinoQuickTimeAction =
                enemyRuntime_.rhinoQuickTimeAction();
            if (rhinoQuickTimeAction.active()) {
                const auto attachedPlayerTransform =
                    enemyRuntime_.rhinoQuickTimePlayerWorldTransform();
                const auto attachedPlayerFacing =
                    enemyRuntime_.rhinoQuickTimePlayerFacing();
                const auto attachedPlayerDetachPosition =
                    enemyRuntime_.rhinoQuickTimePlayerDetachPosition();
                gameplayPlayer_.setQuickTimeActionPose(
                    rhinoQuickTimeAction.playerAnimation(),
                    rhinoQuickTimeAction.playerAnimationMilliseconds(),
                    rhinoQuickTimeAction.animationLoops(),
                    attachedPlayerTransform
                        ? &attachedPlayerTransform.value()
                        : nullptr,
                    attachedPlayerFacing ? &attachedPlayerFacing.value()
                                         : nullptr,
                    attachedPlayerDetachPosition
                        ? &attachedPlayerDetachPosition.value()
                        : nullptr);
            }
            for (const game::EnemyProjectileEvent& event :
                 enemyRuntime_.consumeProjectileEvents()) {
                if (!autoplay) {
                    continue;
                }
                std::string_view kind = "spawned";
                switch (event.kind) {
                case game::EnemyProjectileEventKind::PlayerContact:
                    kind = "player_contact";
                    break;
                case game::EnemyProjectileEventKind::EnemyContact:
                    kind = "enemy_contact";
                    break;
                case game::EnemyProjectileEventKind::Grounded:
                    kind = "grounded";
                    break;
                case game::EnemyProjectileEventKind::Exploded:
                    kind = "exploded";
                    break;
                case game::EnemyProjectileEventKind::StaticContact:
                    kind = "static_contact";
                    break;
                case game::EnemyProjectileEventKind::Returned:
                    kind = "returned";
                    break;
                case game::EnemyProjectileEventKind::Spawned:
                    break;
                }
                autoplay->recordEvent(
                    syntheticElapsedMilliseconds, "enemy_projectile",
                    "kind=" + std::string(kind) +
                        ";enemy=" +
                        std::to_string(event.sourceObjectId) +
                        ";x=" + std::to_string(event.position.x) +
                        ";y=" + std::to_string(event.position.y) +
                        ";z=" + std::to_string(event.position.z) +
                        ";vx=" + std::to_string(event.velocity.x) +
                        ";vy=" + std::to_string(event.velocity.y) +
                        ";vz=" + std::to_string(event.velocity.z) +
                        ";gravity=" + std::to_string(
                            event.gravityCentimetersPerSecondSquared));
            }
            for (const game::PlayerWebPelletEvent& event :
                 enemyRuntime_.consumePlayerWebPelletEvents()) {
                if (event.kind ==
                        game::PlayerWebPelletEventKind::EnemyContact &&
                    event.actualDamage > 0.0F) {
                    gameplayPlayer_.addCombo(
                        event.actualDamage, false,
                        syntheticElapsedMilliseconds);
                }
                if (event.kind ==
                    game::PlayerWebPelletEventKind::EnemyContact) {
                    // SendLocalAiMessage(message 0x12d) reaches
                    // CEnemy::ProcessHitInfo through the CEnemy vtable slot
                    // at object offset +0x1b4 (table entry +0x1c4,
                    // 0x00330fe4). A damaging pellet therefore creates the
                    // ordinary target splash before CBullet adds its own
                    // target-attached web effect below.
                    result = playPlayerHitEffect(
                        event.hitEnemyObjectId, event.hitEffectOrigin,
                        123, event.requestedDamage);
                    if (!result) {
                        return fail(result.message());
                    }
                    // CBullet::CheckCollisions (0x0035cba0, call at
                    // 0x0035cdfe) constructs the literal `web_splash` at
                    // 0x0035cae8 and asks Unit::AddPlayerHitEffect to sample
                    // the struck Unit after the 0x12d damage message.
                    result = playPlayerHitEffect(
                        event.hitEnemyObjectId, event.webSplashOrigin,
                        123, event.requestedDamage, "web_splash");
                    if (!result) {
                        return fail(result.message());
                    }
                }
                if (!autoplay) {
                    continue;
                }
                std::string_view kind = "spawned";
                switch (event.kind) {
                case game::PlayerWebPelletEventKind::EnemyContact:
                    kind = "enemy_contact";
                    break;
                case game::PlayerWebPelletEventKind::StaticContact:
                    kind = "static_contact";
                    break;
                case game::PlayerWebPelletEventKind::Expired:
                    kind = "expired";
                    break;
                case game::PlayerWebPelletEventKind::Spawned:
                    break;
                }
                autoplay->recordEvent(
                    syntheticElapsedMilliseconds, "player_web_pellet",
                    "kind=" + std::string(kind) +
                        ";target=" +
                        std::to_string(event.targetedEnemyObjectId) +
                        ";hit_enemy=" +
                        std::to_string(event.hitEnemyObjectId) +
                        ";requested_damage=" +
                        std::to_string(event.requestedDamage) +
                        ";actual_damage=" +
                        std::to_string(event.actualDamage) +
                        ";x=" + std::to_string(event.position.x) +
                        ";y=" + std::to_string(event.position.y) +
                        ";z=" + std::to_string(event.position.z));
            }
            for (const game::EnemySeparationEvent& event :
                 enemyRuntime_.consumeEnemySeparationEvents()) {
                if (autoplay) {
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds, "enemy_separation",
                        "first=" + std::to_string(event.firstObjectId) +
                            ";second=" +
                            std::to_string(event.secondObjectId) +
                            ";before=" +
                            std::to_string(event.distanceBefore) +
                            ";after=" +
                            std::to_string(event.distanceAfter) +
                            ";required=" +
                            std::to_string(event.requiredDistance));
                }
            }
            for (const game::EnemyLandingAnimatedEffectSpawnEvent& event :
                 enemyRuntime_.consumeLandingAnimatedEffectSpawnEvents()) {
                if (autoplay) {
                    const std::string_view kind =
                        event.kind == game::EnemyLandingAnimatedEffectKind::Shockwave
                            ? "shockwave"
                            : "crashwall";
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds,
                        "enemy_landing_animated_effect",
                        "enemy=" + std::to_string(event.sourceObjectId) +
                            ";type=" + std::string(kind) +
                            ";room=" + std::to_string(event.roomId) +
                            ";scale=" + std::to_string(event.scale) +
                            ";lifetime_ms=" +
                            std::to_string(event.lifetimeMilliseconds) +
                            ";material=" +
                            std::string(event.additiveModulateMaterial
                                            ? "0x1d_additive_modulate"
                                            : "0x1e_texture_vertex_alpha") +
                            ";x=" + std::to_string(event.position.x) +
                            ";y=" + std::to_string(event.position.y) +
                            ";z=" + std::to_string(event.position.z));
                }
            }
            for (const game::EnemyEffectCue& cue :
                 enemyRuntime_.consumeEffectCues()) {
                if (autoplay) {
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds, "enemy_effect",
                        "enemy=" + std::to_string(cue.sourceObjectId) +
                            ";type=" + cue.effectType +
                            ";room=" + std::to_string(cue.roomId) +
                            ";x=" + std::to_string(cue.position.x) +
                            ";y=" + std::to_string(cue.position.y) +
                            ";z=" + std::to_string(cue.position.z));
                }
                result = effectRuntime_.playEffect(
                    cue.effectType, cue.position, cue.roomId);
                if (!result) {
                    return fail(result.message());
                }
            }
            for (const game::EnemyCameraShakeCue& cue :
                 enemyRuntime_.consumeCameraShakeCues()) {
                if (autoplay) {
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds,
                        "enemy_camera_shake",
                        "max_offset=" +
                            std::to_string(cue.maximumOffset) +
                            ";frames=" + std::to_string(cue.frameCount) +
                            ";x_rate=" +
                            std::to_string(cue.axisRates.x) +
                            ";y_rate=" +
                            std::to_string(cue.axisRates.y) +
                            ";z_rate=" +
                            std::to_string(cue.axisRates.z));
                }
                levelCinematicRuntime_.startCameraShake(
                    cue.maximumOffset,
                    static_cast<std::int32_t>(cue.frameCount),
                    cue.axisRates);
            }
            for (const game::EnemySoundCue& cue :
                 enemyRuntime_.consumeSoundCues()) {
                if (cue.voxSoundId < 0 ||
                    static_cast<std::size_t>(cue.voxSoundId) >=
                        voxSounds_.records().size()) {
                    return fail("Enemy sound cue has an invalid VoxSound ID");
                }
                const audio::VoxSoundRecord& record =
                    voxSounds_.records()[
                        static_cast<std::size_t>(cue.voxSoundId)];
                result = enemySounds_.dispatch(
                    cue.voxSoundId,
                    [&cue, &record,
                     &playSpatialSound](const audio::PcmAudio& clip,
                                        bool loop) {
                        return playSpatialSound(
                            cue.sourceObjectId, record.eventName, clip, loop);
                    });
                if (!result) {
                    return fail(result.message());
                }
            }
            for (const game::EnemyPlayerHit& hit :
                 enemyRuntime_.consumePlayerHits()) {
                const bool accepted = !restoreRuntime_.active() &&
                    gameplayPlayer_.applyDamage(
                        hit.damage, 0, 0, hit.hitType,
                        hit.hitProtectionMilliseconds, hit.hitPriority);
                if (autoplay) {
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds, "enemy_hit_player",
                        "enemy=" + std::to_string(hit.sourceObjectId) +
                            ";attack=" + std::to_string(hit.attackId) +
                            ";hit_type=" + std::to_string(hit.hitType) +
                            ";damage=" + std::to_string(hit.damage) +
                            ";hit_protection_ms=" +
                            std::to_string(hit.hitProtectionMilliseconds) +
                            ";hit_priority=" +
                            std::to_string(hit.hitPriority) +
                            ";accepted=" + (accepted ? "1" : "0"));
                }
                if (accepted) {
                    const std::uint16_t hurtStateId =
                        gameplayPlayer_.activeStateId();
                    if (hurtStateId == 46 || hurtStateId == 48 ||
                        hurtStateId == 49) {
                        // Player::OnHit (0x0034dbc0-0x0034dbf2) starts the
                        // frame-13 interface sprite at the global 255 alpha
                        // and CSprite RGB 0xff0000 for knockback reactions.
                        cinematicUi_.startInterfaceEffect(255, 0xff0000, -1);
                        if (autoplay) {
                            autoplay->recordEvent(
                                syntheticElapsedMilliseconds,
                                "interface_effect",
                                "frame=13;alpha=255;color=ff0000;fade_ms=800;"
                                "source=player_hit;state=" +
                                    std::to_string(hurtStateId));
                        }
                    }
                    // QTE action 8 supplies grab_to_knockbackflying itself;
                    // CBehaviorThrow/QTEActionManager must not be replaced by
                    // the generic player hurt reaction or sound dispatch.
                    if (!gameplayPlayer_.quickTimeActionDriven()) {
                        result = playerSounds_.dispatchStateEnter(
                            gameplayPlayer_.activeStateName(), playGameplaySound,
                             stopPlayerStateSound);
                        if (!result) {
                            return fail(result.message());
                        }
                    }
                }
            }
            if (!rhinoQuickTimeAction.active() &&
                gameplayPlayer_.quickTimeActionDriven()) {
                gameplayPlayer_.clearQuickTimeActionPose();
            }
            if (!restoreRuntime_.active()) {
                objectRuntime_.updateElectricPlatformContacts(
                    gameplayPlayer_.position(), gameDeltaMilliseconds);
            }
            for (const game::ElectricPlatformDamageEvent& event :
                 objectRuntime_.consumeElectricPlatformDamageEvents()) {
                if (autoplay) {
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds,
                        "electric_platform_damage",
                        "object=" + std::to_string(event.objectId) +
                            ";room=" + std::to_string(event.roomId) +
                            ";damage=" + std::to_string(event.damage) +
                            ";type=" + std::to_string(event.damageType) +
                            ";effect=" + std::to_string(event.effectId));
                }
                if (gameplayPlayer_.applyDamage(event.damage,
                                                event.damageType, 2000)) {
                    result = playerSounds_.dispatchStateEnter(
                        "k_state_hurt_light", playGameplaySound,
                        stopPlayerStateSound);
                    if (!result) {
                        return fail(result.message());
                    }
                }
            }
            if (!restoreRuntime_.active()) {
                objectRuntime_.updateAreaDamage(
                    gameplayPlayer_.position(), gameDeltaMilliseconds);
                levelDamageRuntime_.update(gameplayPlayer_.position(),
                                           gameDeltaMilliseconds);
            }
            for (const game::AreaDamageEvent& event :
                 objectRuntime_.consumeAreaDamageEvents()) {
                if (autoplay) {
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds, "area_damage",
                        "object=" + std::to_string(event.objectId) +
                            ";room=" + std::to_string(event.roomId) +
                            ";damage=" + std::to_string(event.damage) +
                            ";type=" +
                            std::to_string(event.damageType) +
                            ";hit_type=" +
                            std::to_string(event.hitType));
                }
                if (gameplayPlayer_.applyDamage(
                        event.damage, event.damageType, 1000,
                        event.hitType)) {
                    result = playerSounds_.dispatchStateEnter(
                        event.damageType == 1 ? "k_state_hurt_heavy"
                                              : "k_state_hurt_light",
                        playGameplaySound, stopPlayerStateSound);
                    if (!result) {
                        return fail(result.message());
                    }
                }
            }
            for (const game::LevelDamageEvent& event :
                 levelDamageRuntime_.consumeEvents()) {
                if (autoplay) {
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds, "damage_volume",
                        "object=" + std::to_string(event.objectId) +
                            ";damage=" + std::to_string(event.damage) +
                            ";type=" + std::to_string(event.damageType));
                }
                if (gameplayPlayer_.applyDamage(event.damage,
                                                event.damageType, 1000,
                                                event.damageType == 1
                                                    ? 0x86 : 0x85)) {
                    result = playerSounds_.dispatchStateEnter(
                        event.damageType == 1 ? "k_state_hurt_heavy"
                                              : "k_state_hurt_light",
                        playGameplaySound, stopPlayerStateSound);
                    if (!result) {
                        return fail(result.message());
                    }
                }
            }
            dropRuntime_.update(gameplayPlayer_.position(),
                                gameDeltaMilliseconds);
            for (const game::LevelDropObjectState& drop :
                 dropRuntime_.states()) {
                result = objectRuntime_.setRuntimeState(
                    drop.asset->objectId, drop.position, drop.visible,
                    drop.physicsEnabled);
                if (!result) {
                    return fail(result.message());
                }
            }
            for (const game::LevelDropEvent& event :
                 dropRuntime_.consumeEvents()) {
                if (autoplay) {
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds, "drop_event",
                        "object=" + std::to_string(event.objectId) +
                            ";kind=" + std::to_string(
                                static_cast<std::int32_t>(event.kind)) +
                            ";damage=" + std::to_string(event.damage));
                }
                if (!event.effectType.empty()) {
                    result = effectRuntime_.playEffect(
                        event.effectType, event.position, event.roomId);
                    if (!result) {
                        return fail(result.message());
                    }
                }
                if (event.kind == game::LevelDropEventKind::Activated) {
                    const audio::VoxSoundRecord* record =
                        voxSounds_.find("SFX_BATTERY_CELL_EXPLOSION");
                    if (record == nullptr) {
                        return fail("Drop-object sound has no VoxSound record");
                    }
                    result = playSpatialAudio(
                        "DropObject:" + std::to_string(event.objectId),
                        dropObjectSound_,
                        {event.position, record->minimumDistance,
                         record->maximumDistance,
                         record->distanceCullingEnabled},
                        false, record->eventName);
                    if (!result) {
                        return fail(result.message());
                    }
                } else if (event.kind ==
                           game::LevelDropEventKind::HitPlayer) {
                    if (!restoreRuntime_.active() &&
                        gameplayPlayer_.applyDamage(event.damage)) {
                        result = playerSounds_.dispatchStateEnter(
                            "k_state_hurt_light", playGameplaySound,
                            stopPlayerStateSound);
                        if (!result) {
                            return fail(result.message());
                        }
                    }
                }
            }
            levelBonusRuntime_.update(gameplayPlayer_.position(),
                                      gameDeltaMilliseconds);
            for (const std::int32_t objectId :
                 levelBonusRuntime_.consumeCollectedBonusIds()) {
                result = effectRuntime_.setPersistentEffectVisible(objectId,
                                                                   false);
                if (!result) {
                    return fail(result.message());
                }
            }
            for (const game::LevelBonusGrant& grant :
                 levelBonusRuntime_.consumeGrants()) {
                if (autoplay) {
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds, "bonus",
                        "type=" + std::to_string(
                            static_cast<std::int32_t>(grant.type)) +
                            ";amount=" + std::to_string(grant.amount));
                }
                if (grant.type == game::LevelBonusType::Health) {
                    gameplayPlayer_.addHealth(
                        static_cast<float>(grant.amount));
                } else if (grant.type == game::LevelBonusType::SkillPoint) {
                    gameplayPlayer_.addSkillPoints(grant.amount);
                }
                result = playNamedAudio("SFX_ORBS_COLLECT",
                                        bonusCollectSound_, false);
                if (!result) {
                    return fail(result.message());
                }
            }
            const bool deathScreenWasActive = levelDeathRuntime_.active();
            const bool deathConfirmationWasReady =
                levelDeathRuntime_.confirmationReady();
            levelDeathRuntime_.update(
                (gameplayPlayer_.dead() &&
                 gameplayPlayer_.activeStateId() == 0x80) ||
                    levelCinematicRuntime_.bossProgress().failed,
                gameDeltaMilliseconds,
                levelCinematicRuntime_.bossProgress().failed ? 10U : 2000U);
            if (autoplay && !deathScreenWasActive &&
                levelDeathRuntime_.active()) {
                autoplay->recordEvent(
                    syntheticElapsedMilliseconds, "death_screen",
                    "state=fade_in;duration_ms=2000;player_state=128");
            }
            if (autoplay && !deathConfirmationWasReady &&
                levelDeathRuntime_.confirmationReady()) {
                autoplay->recordEvent(
                    syntheticElapsedMilliseconds, "death_screen",
                    "state=confirmation_ready;message=48");
            }
            if (!exitMenuRuntime_.active()) {
                deathConfirmationRuntime_.update(
                    levelDeathRuntime_.confirmationReady(),
                    autoplay ? autoplayInput.menuUpReleased
                             : keyRouter_.state().moveUp.released,
                    autoplay ? autoplayInput.menuDownReleased
                             : keyRouter_.state().moveDown.released,
                    autoplay ? autoplayInput.menuSelectedReleased
                             : keyRouter_.state().menuSelected.released);
            }
            const game::DeathConfirmationOutcome deathOutcome =
                deathConfirmationRuntime_.consumeOutcome();
            if (deathOutcome == game::DeathConfirmationOutcome::Retry) {
                const std::int32_t checkPointId =
                    checkPointRuntime_.lastCheckPointId();
                const bool restartingAtCheckPoint = checkPointId >= 0;
                // CLevel::DoConfirmation (0x003836f8) saves
                // Player::GetComboScore before RestartAtLastCheckPoint and
                // writes it back to Player+0x580 after either reload branch.
                const std::int32_t liveComboScore =
                    gameplayPlayer_.comboScore();
                result = restartingAtCheckPoint
                             ? loadRuntimeCheckPoint()
                             : restartRuntimeLevel(
                                   syntheticElapsedMilliseconds);
                if (result) {
                    gameplayPlayer_.setComboScore(liveComboScore);
                }
                if (autoplay) {
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds, "checkpoint_restart",
                        "object=" + std::to_string(checkPointId) +
                            ";result=" +
                            (result ? (restartingAtCheckPoint ? "loaded"
                                                              : "reloaded")
                                    : "failed") +
                            ";camera_area=" +
                            std::to_string(gameplayCamera_.currentAreaId()) +
                            ";x=" +
                            std::to_string(gameplayPlayer_.position().x) +
                            ";y=" +
                            std::to_string(gameplayPlayer_.position().y) +
                            ";z=" +
                            std::to_string(gameplayPlayer_.position().z) +
                            ";health=" +
                            std::to_string(gameplayPlayer_.health()) +
                            ";combo_score=" +
                            std::to_string(gameplayPlayer_.comboScore()));
                }
                if (!result) {
                    return fail(result.message());
                }
                if (!restartingAtCheckPoint && hasIntroCinematic) {
                    // The restart was selected after this frame's gameplay
                    // branch began. Present the first 1265 frame immediately;
                    // subsequent frames derive their time from the new epoch.
                    timestamp = 0;
                    gameplayActive = false;
                    gameplayVisibilityInitialized = false;
                }
            } else if (deathOutcome ==
                       game::DeathConfirmationOutcome::Exit) {
                // CLevel::DoConfirmation (0x003836f8) pushes GS_ExitMenu in
                // mode 3. It is a timed transition, not a second menu;
                // GS_ExitMenu::Update (0x002c0150) replaces the failed level
                // with GS_MainMenu across states 16-20.
                exitMenuRuntime_.beginAfterDeath();
                if (autoplay) {
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds,
                        "death_confirmation_exit",
                        "state=exit_menu;mode=3");
                }
            }
            if (exitMenuRuntime_.active() &&
                exitMenuRuntime_.state() >= 16) {
                // The failed CLevel and GS_Confirmation are no longer on the
                // native state stack. Do not let the dead player recreate
                // their black-screen lifecycle later in this same frame.
                levelDeathRuntime_.reset();
                deathConfirmationRuntime_.reset();
            }
            result = applyMusicTransition(levelMusicRuntime_.update(
                enemyRuntime_.states(), gameplayPlayer_.dead()));
            if (!result) {
                return fail(result.message());
            }
            playerHudHealth_.update(gameplayPlayer_.health(),
                                    gameDeltaMilliseconds);
            const float webPowerRatio = gameplayPlayer_.maximumWebPower() > 0.0F
                ? gameplayPlayer_.webPower() /
                      gameplayPlayer_.maximumWebPower()
                : 0.0F;
            result = renderer_.updatePlayerHud(
                levelOne_.hud(), playerHudHealth_.currentRatio(),
                playerHudHealth_.delayedRatio(), webPowerRatio,
                enemyRuntime_.shownHealthBarEnemy(),
                gameplayPlayer_.skillPoints(),
                levelBonusRuntime_.showSkillPointTotal(),
                &levelBonusRuntime_.skillPointPopup(),
                levelCinematicRuntime_.attributionEnabled(),
                levelCinematicRuntime_.bossProgressVisible(),
                levelCinematicRuntime_.bossProgressRatio());
            const assets::ColladaAnimationClip* activeClip =
                levelOne_.player().animationBank.findClip(
                    gameplayPlayer_.activeAnimation());
            if (activeClip == nullptr) {
                return fail("The active player animation is missing");
            }
            if (result) {
                result = renderer_.updateLevelOnePlayer(
                    levelOne_, *activeClip,
                    gameplayPlayer_.animationTimeMilliseconds(),
                    gameplayPlayer_.worldTransform());
            }
            if (result) {
                result = renderer_.updatePlayerHitEffects(levelOne_,
                                                          gameplayPlayer_);
            }
            if (result) {
                assets::Vector3 webLineAnchor =
                    gameplayPlayer_.webLineAnchor();
                const std::int32_t webLineTargetId =
                    gameplayPlayer_.webLineTargetObjectId();
                if (webLineTargetId >= 0) {
                    const game::LevelEnemyState* liveTarget =
                        enemyRuntime_.find(webLineTargetId);
                    if (liveTarget != nullptr) {
                        webLineAnchor = liveTarget->position;
                        webLineAnchor.z +=
                            liveTarget->collisionHeight * 0.5F;
                        if (gameplayPlayer_.wallWeb().lineActive()) {
                            const auto bone = enemyRuntime_.nodeWorldPosition(
                                webLineTargetId, gameplayPlayer_.wallWeb().targetBone());
                            if (bone) {
                                webLineAnchor = *bone;
                            }
                        }
                    } else if (const game::LevelObjectState* liveObject =
                                   objectRuntime_.find(webLineTargetId);
                               liveObject != nullptr &&
                               liveObject->asset != nullptr) {
                        webLineAnchor = liveObject->position;
                        webLineAnchor.z +=
                            liveObject->asset->collisionHeight * 0.5F;
                    }
                }
                const assets::Vector3 firstWebAttach =
                    gameplayPlayer_.webLineAttachPosition(0);
                std::optional<assets::Vector3> secondWebAttach;
                if (gameplayPlayer_.webLineCount() > 1) {
                    secondWebAttach = gameplayPlayer_.webLineAttachPosition(1);
                }
                result = renderer_.updateWebLine(
                    gameplayPlayer_.webLineActive(), webLineAnchor,
                    firstWebAttach,
                    secondWebAttach.has_value() ? &*secondWebAttach : nullptr,
                    gameplayPlayer_.webLineOrientation());
            }
            if (result) {
                result = renderer_.updateLevelOneEnemies(levelOne_,
                                                         enemyRuntime_);
            }
            if (result) {
                const game::GameplayCinematicPlayback* presentation =
                    gameplayCinematics_.presentation();
                result = renderer_.updateGameplayCinematicActors(
                    levelOne_,
                    presentation != nullptr ? presentation->asset : nullptr,
                    presentation != nullptr
                        ? presentation->elapsedMilliseconds
                        : 0);
            }
            if (result) {
                result = renderer_.updatePlayerWebPellets(
                    levelOne_, enemyRuntime_.playerWebPellets());
            }
            if (result) {
                result = renderer_.updateEnemyGunLines(
                    enemyRuntime_.gunLines());
            }
            if (result) {
                result = renderer_.updateEnemyMolotovs(
                    levelOne_, enemyRuntime_.molotovs());
            }
            if (result) {
                result = renderer_.updateEnemyRockets(
                    levelOne_, enemyRuntime_.rockets());
            }
            if (result) {
                result = renderer_.updateEnemyBoomerangs(
                    levelOne_, enemyRuntime_.boomerangs());
            }
            if (result) {
                result = renderer_.updateEnemyElectroEffects(
                    levelOne_, enemyRuntime_);
            }
            if (result) {
                game::CameraPose cameraPose;
                const game::GameplayCinematicPlayback* presentation =
                    gameplayCinematics_.presentation();
                if (presentation != nullptr &&
                    presentation->asset->hasColladaPlayback()) {
                    cameraPose =
                        presentation->asset->animatedCamera.sample(
                            presentation->asset->cameraAnimationTimestamp(
                                presentation->elapsedMilliseconds));
                } else if (presentation != nullptr &&
                           presentation->asset->cameraTrack.valid()) {
                    cameraPose = presentation->asset->cameraTrack.sample(
                        presentation->elapsedMilliseconds);
                } else {
                    cameraPose =
                        gameplayCamera_.sample(gameplayPlayer_.position());
                }
                renderer_.setCameraAreaRoomVisibility(
                    gameplayCamera_.mustInvisibleRooms(),
                    gameplayCamera_.mustVisibleRooms());
                renderer_.setCinematicVisibleRooms(
                    levelCinematicRuntime_.forcedVisibleRooms());
                const game::CameraPose finalCameraPose =
                    levelCinematicRuntime_.applyCameraShake(cameraPose);
                presentedCameraPose = finalCameraPose;
                if (audioEnabled) {
                    const assets::Vector3 listenerPosition =
                        levelCinematicRuntime_.listenerOnMainCharacter()
                            ? gameplayPlayer_.position()
                            : finalCameraPose.position;
                    const assets::Vector3 listenerTarget{
                        listenerPosition.x +
                            (finalCameraPose.target.x -
                             finalCameraPose.position.x),
                        listenerPosition.y +
                            (finalCameraPose.target.y -
                             finalCameraPose.position.y),
                        listenerPosition.z +
                            (finalCameraPose.target.z -
                             finalCameraPose.position.z)};
                    audio_.setListener(listenerPosition, listenerTarget,
                                       finalCameraPose.up);
                }
                result = renderer_.setCamera(finalCameraPose);
            }
        }
        if (result) {
            result = renderer_.updateLevelOneObjects(levelOne_,
                                                     objectRuntime_);
        }
        // UpdateSpiderSense (0x0034fba0-0x0034fbea) shows the dedicated
        // hintbb animation only while state 34 is affordable/enabled and a
        // CTargetHelper attack warning remains queued. popAttack removes it
        // in the accepted-input frame, independently of tutorial visibility.
        const bool spiderSenseSkillUnlocked =
            levelOne_.levelNumber() != 1 ||
            levelCinematicRuntime_.skillUnlocked(1);
        hintRuntime_.setCombatSenseCueVisible(
            gameplayActive && spiderSenseSkillUnlocked &&
            gameplayPlayer_.canDisplaySpiderSense() &&
            enemyRuntime_.findSpiderSenseThreat().has_value());
        hintRuntime_.update(
            gameDeltaMilliseconds,
            [this](std::int32_t objectId)
                -> std::optional<assets::Vector3> {
                if (objectId == levelOne_.player().objectId) {
                    return gameplayPlayer_.position();
                }
                if (const game::LevelEnemyState* enemy =
                        enemyRuntime_.find(objectId)) {
                    return enemy->position;
                }
                if (const game::LevelObjectState* object =
                        objectRuntime_.find(objectId)) {
                    return object->position;
                }
                return std::nullopt;
            });
        if (result) {
            result = renderer_.updateLevelOneHints(hintRuntime_);
        }
        if (result) {
            for (std::int32_t poolIndex = 0;
                 poolIndex < kRocketPoolSize; ++poolIndex) {
                const auto rocket = std::find_if(
                    enemyRuntime_.rockets().begin(),
                    enemyRuntime_.rockets().end(),
                    [poolIndex](const game::EnemyRocketState& state) {
                        return state.active && state.poolIndex == poolIndex;
                    });
                const std::int32_t sourceId =
                    kRocketSmokeEffectSourceBase + poolIndex;
                if (rocket != enemyRuntime_.rockets().end()) {
                    result = effectRuntime_.setPersistentEffectPosition(
                        sourceId, rocket->position);
                    if (result) {
                        result = effectRuntime_.setPersistentEffectVisible(
                            sourceId, true, true);
                    }
                } else {
                    result = effectRuntime_.setPersistentEffectVisible(
                        sourceId, false);
                }
                if (!result) {
                    break;
                }
            }
        }
        if (result) {
            // CFpsParticleSystemSceneNode::OnAnimate (0x0039f7c8) runs from
            // the visible scene graph after CLevel::Update has completed its
            // player, room, AI, EffectManager, and bonus stages. A newly
            // thrown emitter initializes its native timestamp here and sees
            // a zero delta; existing visible emitters receive this frame's
            // elapsed time.
            effectRuntime_.update(gameDeltaMilliseconds,
                                  renderer_.roomVisibility());
            result = renderer_.updateLevelOneEffects(
                levelOne_.effects(), effectRuntime_, levelBonusRuntime_);
        }
        cinematicUi_.update(
            gameDeltaMilliseconds,
            autoplay ? autoplayInput.quickTimeEventPressed
                     : keyRouter_.state().quickTimeEvent.pressed);
        const game::QuickTimeActionRuntime& rhinoQuickTimeAction =
            enemyRuntime_.rhinoQuickTimeAction();
        const bool rhinoButtonPromptActive =
            rhinoQuickTimeAction.buttonPromptActive();
        const bool qteVisible =
            quickTimeEvent_.active() || hostageRuntime_.quickTimeActive() ||
            rhinoButtonPromptActive || gameplayPlayer_.wallWeb().promptActive();
        const float qteProgress =
            quickTimeEvent_.active() &&
                    quickTimeEvent_.durationMilliseconds() != 0
                ? static_cast<float>(quickTimeEvent_.elapsedMilliseconds()) /
                      static_cast<float>(
                          quickTimeEvent_.durationMilliseconds())
                : hostageRuntime_.quickTimeActive()
                      ? hostageRuntime_.quickTimeProgress()
                : rhinoButtonPromptActive
                      ? rhinoQuickTimeAction.buttonProgress()
                      : gameplayPlayer_.wallWeb().promptActive()
                          ? gameplayPlayer_.wallWeb().progress() : 0.0F;
        if (result) {
            game::CinematicUiFrame uiFrame = cinematicUi_.frame(
                qteVisible, qteProgress);
            if (hostageRuntime_.quickTimeActive() &&
                !uiFrame.messagePanelVisible) {
                uiFrame.text = u"Press [X]";
                uiFrame.textVisible = true;
                uiFrame.tutorialPanelVisible = true;
                uiFrame.tutorialButton = -1;
                uiFrame.dimBackground = false;
            } else if (hostageRuntime_.contextPromptVisible() &&
                       !uiFrame.textVisible) {
                uiFrame.text = u"Press [X] to rescue";
                uiFrame.textVisible = true;
                uiFrame.tutorialPanelVisible = true;
                uiFrame.tutorialButton = -1;
                uiFrame.quickTimeEventVisible = false;
            }
            uiFrame.blackOverlayAlpha = std::max(
                uiFrame.blackOverlayAlpha,
                std::max(restoreRuntime_.blackOverlayAlpha(),
                         std::max(levelDeathRuntime_.blackOverlayAlpha(),
                                  exitMenuRuntime_.blackOverlayAlpha())));
            result = renderer_.updateCinematicUi(levelOne_.hud(), uiFrame);
        }
        if (result) {
            game::DeathConfirmationFrame confirmationFrame =
                !exitMenuRuntime_.active() ||
                        exitMenuRuntime_.parentVisible()
                    ? deathConfirmationRuntime_.frame()
                    : game::DeathConfirmationFrame{};
            confirmationFrame.loadingVisible =
                exitMenuRuntime_.loadingTextVisible();
            if (confirmationFrame.loadingVisible) {
                confirmationFrame.loadingLabel =
                    exitMenuRuntime_.loadingLabel();
                confirmationFrame.loadingSuffix =
                    exitMenuRuntime_.loadingSuffix();
            }
            result = renderer_.updateDeathConfirmation(
                levelOne_.hud(), confirmationFrame);
        }
        if (result) {
            result = renderer_.updateTransport(
                levelOne_.hud(), levelCinematicRuntime_.transport());
        }
        if (result) {
            for (const game::SlowMotionSoundCue cue :
                 levelCinematicRuntime_.consumeSlowMotionSoundCues()) {
                result = playAudio(
                    cue == game::SlowMotionSoundCue::Enter
                        ? slowMotionEnterSound_
                        : slowMotionExitSound_,
                    false,
                    cue == game::SlowMotionSoundCue::Enter
                        ? "SFX_SPIDER_SENSE_IN"
                        : "SFX_SPIDER_SENSE_OUT");
                if (!result) {
                    break;
                }
            }
        }
        if (result) {
            for (const game::TransportSoundCue cue :
                 levelCinematicRuntime_.consumeTransportSoundCues()) {
                const bool closing = cue == game::TransportSoundCue::In;
                result = playAudio(
                    closing ? transportInSound_ : transportOutSound_, false,
                    closing ? "SFX_SPIDER_LOGO_IN"
                            : "SFX_SPIDER_LOGO_OUT");
                if (!result) {
                    break;
                }
            }
        }
        if (!result) {
            return fail(result.message());
        }
        if (exitAfterPresent) {
            presentThisFrame = true;
        }
        if (presentThisFrame) {
            renderer_.renderFrame();
        }
        if (autoplay) {
            const bool finalControlsEnabled =
                gameplayActive && levelCinematicRuntime_.controlsEnabled() &&
                !cinematicUi_.modalTutorialVisible() &&
                !gameplayPlayer_.cinematicDriven() &&
                !hostageRuntime_.ownsPlayerControl() &&
                !levelDeathRuntime_.active() &&
                !exitMenuRuntime_.active() &&
                !restoreRuntime_.active() && !quickTimeEvent_.active() &&
                !enemyRuntime_.rhinoQuickTimeAction().active() &&
                !gameplayCinematics_.hasActiveColladaPlayback();
            autoplay->recordFrame(
                {frameIndex,
                 syntheticElapsedMilliseconds,
                  accumulatedGameMilliseconds,
                  levelCinematicRuntime_.slowMotionDenominator(),
                  gameplayActive,
                  finalControlsEnabled,
                  levelCinematicRuntime_.attributionEnabled(),
                  quickTimeEvent_.active() || gameplayPlayer_.wallWeb().promptActive() ||
                     hostageRuntime_.quickTimeActive() ||
                     enemyRuntime_.rhinoQuickTimeAction()
                         .buttonPromptActive(),
                 cinematicUi_.tutorialVisible(),
                 levelDeathRuntime_.active(),
                 levelDeathRuntime_.blackOverlayAlpha(),
                 deathConfirmationRuntime_.active(),
                 deathConfirmationRuntime_.selection(),
                 exitMenuRuntime_.active(),
                 exitMenuRuntime_.state(),
                 exitMenuRuntime_.mainMenuRequested(),
                  restoreRuntime_.active(),
                  restoreRuntime_.blackOverlayAlpha(),
                  gameplayCamera_.currentAreaId(),
                  checkPointRuntime_.lastCheckPointId(),
                  diagnosticCinematicId(),
                 gameplayCinematics_.activeIds(),
                 gameplayPlayer_.position(),
                 gameplayPlayer_.renderPosition(),
                 gameplayPlayer_.attackRootTranslation(),
                 gameplayPlayer_.facing(),
                 gameplayPlayer_.health(),
                 gameplayPlayer_.webPower(),
                 gameplayPlayer_.skillPoints(),
                 gameplayPlayer_.comboScore(),
                 gameplayPlayer_.activeAnimation(),
                 gameplayPlayer_.animationTimeMilliseconds(),
                 gameplayPlayer_.activeStateId(),
                 gameplayPlayer_.activeStateName(),
                 gameplayPlayer_.punchTransitionReadyAfterImpact(),
                 gameplayPlayer_.punchAttackTransitionReady(),
                 gameplayPlayer_.jumpAttackTransitionReady(),
                 gameplayPlayer_.jumpReleaseAttackTransitionReady(),
                 gameplayPlayer_.webAttackTransitionReady(),
                 gameplayPlayer_.webHeldAttackTransitionReady(),
                 gameplayPlayer_.hitEffects(),
                 gameplayPlayer_.onWall(),
                 gameplayPlayer_.cinematicMotionActive(),
                 gameplayPlayer_.cinematicMotionElapsedMilliseconds(),
                 gameplayPlayer_.cinematicMotionDurationMilliseconds(),
                 presentedCameraPose,
                  renderer_.roomVisibility(),
                  levelCinematicRuntime_.roomMotionStates(),
                  enemyRuntime_.states(),
                  enemyRuntime_.meleeEngagerObjectId(),
                  enemyRuntime_.meleeEngagementCooldownMilliseconds(),
                  enemyRuntime_.nativeRandomState(),
                  enemyRuntime_.gunLines(),
                  enemyRuntime_.rockets(),
                  enemyRuntime_.molotovs(),
                   enemyRuntime_.boomerangs(),
                   enemyRuntime_.thunderclaps(),
                   enemyRuntime_.electricPosts(),
                   enemyRuntime_.electroBursts(),
                   objectRuntime_.states(),
                 levelBonusRuntime_.states(),
                 hostageRuntime_.states(),
                 dropRuntime_.states(),
                 hostageRuntime_.quickTimeActive(),
                 enemyRuntime_.rhinoQuickTimeAction().active(),
                 enemyRuntime_.rhinoQuickTimeAction().actionStateId(),
                 enemyRuntime_.rhinoQuickTimeAction()
                     .stateElapsedMilliseconds(),
                 enemyRuntime_.rhinoQuickTimeAction()
                     .buttonElapsedMilliseconds(),
                 enemyRuntime_.rhinoQuickTimeAction()
                     .buttonDurationMilliseconds(),
                  enemyRuntime_.rhinoQuickTimeAction()
                      .completedActionCount(),
                  enemyRuntime_.rhinoQuickTimeAction().requiredActionCount(),
                  enemyRuntime_.rhinoQuickTimeAction().buttonProgress(),
                  levelCinematicRuntime_.bossProgress().visible,
                  levelCinematicRuntime_.bossProgress().closing,
                  levelCinematicRuntime_.bossProgress().failed,
                  levelCinematicRuntime_.bossProgress().bossObjectId,
                  levelCinematicRuntime_.bossProgress().currentDistance,
                  levelCinematicRuntime_.bossProgress().failureDistance,
                  levelCinematicRuntime_.bossProgressRatio(),
                  levelCinematicRuntime_.transport().state,
                  levelCinematicRuntime_.transport().elapsedMilliseconds,
                  levelCinematicRuntime_.transport().scale,
                  levelCinematicRuntime_.levelEnded(),
                  levelCinematicRuntime_.gameEnded(),
                  cinematicUi_.letterboxVisible(),
                  gameplayPlayer_.webLineActive(),
                  gameplayPlayer_.webLineCount(),
                  gameplayPlayer_.webLineTargetObjectId(),
                  gameplayPlayer_.wallWeb().phase(), gameplayPlayer_.wallWeb().targetObjectId(),
                  gameplayPlayer_.wallWeb().angle(), gameplayPlayer_.wallWeb().completedActionCount(),
                  gameplayPlayer_.wallWeb().lineActive(),
                  gameplayActive
                      ? gameplayPlayer_.availableWebGrabPointObjectId()
                      : -1,
                  hintRuntime_.combatSenseCueVisible()});
            const bool periodicCapture = autoplay->periodicCaptureDue(
                syntheticElapsedMilliseconds);
            if (periodicCapture || !autoplayInput.captureLabels.empty()) {
                assets::RgbaImage capture;
                result = renderer_.readBackImage(capture);
                if (result && periodicCapture) {
                    result = autoplay->writeCapture(
                        capture, gameplayActive ? "gameplay" : "intro",
                        syntheticElapsedMilliseconds);
                }
                if (result) {
                    for (const std::string& label :
                         autoplayInput.captureLabels) {
                        result = autoplay->writeCapture(
                            capture, label, syntheticElapsedMilliseconds);
                        if (!result) {
                            break;
                        }
                    }
                }
                if (!result) {
                    autoplay->finish(false, result.message());
                    return fail(result.message());
                }
            }
        }
        ++frameIndex;
        if (exitAfterPresent &&
            (!autoplay || autoplay->complete() || autoplay->failed())) {
            // Normal play exits immediately after presenting the terminal
            // frame. Autoplay may keep that immutable final state alive long
            // enough to assert the last-timestamp commands and capture it;
            // its own finish/max-time condition still bounds the loop.
            break;
        }
    }
    if (autoplay) {
        const bool succeeded = autoplay->complete() && !autoplay->failed();
        const std::string detail = autoplay->failed()
            ? std::string(autoplay->failureMessage())
            : autoplay->complete() ? "script completed"
                                   : "application exited before script completion";
        autoplay->finish(succeeded, detail);
        if (!succeeded) {
            return fail(detail);
        }
    }
    gAutoplayDiagnostics = nullptr;
    return EXIT_SUCCESS;
}

} // namespace usm
