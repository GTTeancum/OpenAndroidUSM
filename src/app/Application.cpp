#include "app/Application.hpp"

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
#include <vector>

namespace usm {
namespace {

bool gShowErrorDialogs = true;

int fail(std::string_view message) {
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

} // namespace

int Application::run(HINSTANCE instance, const ApplicationOptions& options) {
    gShowErrorDialogs = !options.autoplayScript.has_value();
    std::optional<diagnostics::AutoplayHarness> autoplay;
    Result result = Result::success();
    if (options.autoplayScript) {
        autoplay.emplace();
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
                                  diagnosticName, loop, false);
        }
        return audioEnabled ? audio_.play(clip, loop) : Result::success();
    };
    const auto playNamedAudio =
        [this, &autoplay, &traceTimeMilliseconds, audioEnabled](
            std::string_view eventName, const audio::PcmAudio& clip, bool loop,
            float volume = 1.0F,
            std::uint32_t fadeMilliseconds = 0) -> Result {
        if (autoplay) {
            autoplay->recordAudio(traceTimeMilliseconds, "play", eventName,
                                  loop, false);
        }
        return audioEnabled
            ? audio_.playNamed(eventName, clip, loop, volume,
                               fadeMilliseconds)
            : Result::success();
    };
    const auto playSpatialAudio =
        [this, &autoplay, &traceTimeMilliseconds, audioEnabled](
            std::string_view eventName, const audio::PcmAudio& clip,
            const audio::SpatialSoundSource& source, bool loop = false)
            -> Result {
        if (autoplay) {
            autoplay->recordAudio(traceTimeMilliseconds, "play", eventName,
                                  loop, true);
        }
        return audioEnabled
            ? audio_.playNamed3D(eventName, clip, source, loop)
            : Result::success();
    };
    const auto stopNamedAudio =
        [this, &autoplay, &traceTimeMilliseconds, audioEnabled](
            std::string_view eventName,
            std::uint32_t fadeMilliseconds = 0) -> Result {
        if (autoplay) {
            autoplay->recordAudio(traceTimeMilliseconds, "stop", eventName);
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
    result = levelOne_.load(gameDataRoot);
    if (!result) {
        return fail(result.message());
    }
    if (autoplay) {
        autoplay->recordCinematicAssets(levelOne_.cinematics());
        autoplay->recordCollisionAssets(levelOne_.rooms());
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
                    ";wait_spawn=" + std::to_string(enemy.waitSpawn));
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
    result = soundCatalog_.decode("SFX_BATTERY_CELL_EXPLOSION",
                                  dropObjectSound_);
    if (!result) {
        return fail(result.message());
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
    result = playerStateConfigs_.load(gameDataRoot);
    if (!result) {
        return fail(result.message());
    }
    if (autoplay) {
        autoplay->recordPlayerStateAssets(playerStateConfigs_);
    }
    constexpr std::array<std::string_view, 22> gameplaySoundStates{
        "k_state_idle_to_punch_right", "k_state_hurt_light",
        "k_state_hurt_heavy", "k_state_jump_start", "k_state_jump_land",
        "k_state_swing_web_throw", "k_state_swing_hang",
        "k_state_swing_idle", "k_state_trigger_slider_move",
        "k_state_idle_onwall", "k_state_move_onwall",
        "k_state_move_climb_wall", "k_state_move_exit_wall",
        "k_state_move_jump_wall_up", "k_state_move_jump_wall_down",
        "k_state_move_jump_wall_left", "k_state_move_jump_wall_right",
        "k_state_punch_right_to_punch_left",
        "k_state_punch_left_to_kick_right",
        "k_state_kick_right_to_fast_kick",
        "k_state_kick_right_to_fast_kick_2",
        "k_state_kick_left_double_kick"};
    result = playerSounds_.preload(playerStateConfigs_, voxSounds_,
                                   soundCatalog_, gameplaySoundStates);
    if (!result) {
        return fail(result.message());
    }
    constexpr std::array<std::int16_t, 6> firstLevelEnemyTypes{
        0, 1, 3, 4, 5, 16};
    result = enemySounds_.preload(
        levelOne_.enemyBehaviorConfigs(), levelOne_.enemySpecialActions(),
        voxSounds_, soundCatalog_, firstLevelEnemyTypes);
    if (!result) {
        return fail(result.message());
    }
    result = introSounds_.preload(levelOne_.introScript(), soundCatalog_);
    if (!result) {
        return fail(result.message());
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
    result = introPlayer_.start(levelOne_.introScript());
    if (!result) {
        return fail(result.message());
    }
    result = renderer_.uploadLevelOneScene(levelOne_);
    if (!result) {
        return fail(result.message());
    }
    const game::CameraPose initialCameraPose =
        levelOne_.introCamera().sample(0);
    result = renderer_.setCamera(initialCameraPose);
    if (!result) {
        return fail(result.message());
    }
    if (audioEnabled) {
        audio_.setListener(initialCameraPose.position, initialCameraPose.target,
                           initialCameraPose.up);
    }
    result = gameplayCamera_.bind(levelOne_.cameraAreas(),
                                  levelOne_.player().initialCameraAreaId);
    if (!result) {
        return fail(result.message());
    }
    result = levelCollision_.build(levelOne_.rooms());
    if (!result) {
        return fail(result.message());
    }
    result = gameplayPlayer_.initialize(levelOne_.player(), &levelCollision_,
                                        &playerStateConfigs_,
                                        levelOne_.webGrabPoints(),
                                        levelOne_.slides(),
                                        levelOne_.waypoints());
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
    result = enemyRuntime_.initialize(levelOne_);
    if (!result) {
        return fail(result.message());
    }
    result = objectRuntime_.initialize(levelOne_);
    if (!result) {
        return fail(result.message());
    }
    result = dropRuntime_.initialize(levelOne_.dropAreas(),
                                     levelOne_.dropObjects());
    if (!result) {
        return fail(result.message());
    }
    result = effectRuntime_.initialize(levelOne_.effects().presets);
    if (!result) {
        return fail(result.message());
    }
    for (const game::LevelEnvironmentEffectAsset& effect :
         levelOne_.environmentEffects()) {
        result = effectRuntime_.addPersistentEffect(
            effect.effectType, effect.position, effect.roomId, effect.visible,
            effect.objectId);
        if (!result) {
            return fail(result.message());
        }
    }
    result = levelBonusRuntime_.initialize(levelOne_.bonuses());
    if (!result) {
        return fail(result.message());
    }
    result = hintRuntime_.initialize(levelOne_.hints());
    if (!result) {
        return fail(result.message());
    }
    for (const game::LevelBonusAsset& bonus : levelOne_.bonuses()) {
        const std::string_view effectType =
            bonus.type == game::LevelBonusType::Health
                ? "bonus_green"
            : bonus.type == game::LevelBonusType::WebPower ? "bonus_blue"
                                                            : "bonus_red";
        result = effectRuntime_.addPersistentEffect(
            effectType, bonus.position, bonus.roomId, bonus.visible,
            bonus.objectId);
        if (!result) {
            return fail(result.message());
        }
    }
    levelCinematicRuntime_.bind(triggerRuntime_, gameplayCamera_,
                                levelOne_.waypoints());
    gameplayCinematics_.bind(levelOne_.cinematics());
    quickTimeEvent_.bind(levelOne_.buttonConfigs());
    cinematicUi_.bind(levelOne_.textCatalog());
    game::CinematicPlayer introStartCommands;
    result = introStartCommands.start(levelOne_.introStartScript());
    if (!result) {
        return fail(result.message());
    }
    Result introStartCommandResult = Result::success();
    result = introStartCommands.advanceTo(
        introStartCommands.durationMilliseconds(),
        [this, &introStartCommandResult](
            const game::CinematicThread& thread,
            const game::CinematicCommand& command) {
            if (introStartCommandResult) {
                introStartCommandResult = objectRuntime_.applyCinematicCommand(
                    levelOne_, thread, command);
            }
            if (introStartCommandResult) {
                introStartCommandResult =
                    hintRuntime_.applyCinematicCommand(thread, command);
            }
            if (introStartCommandResult) {
                introStartCommandResult =
                    levelCinematicRuntime_.applyCommand(command);
            }
        });
    if (!result || !introStartCommandResult) {
        return fail(!result ? result.message()
                            : introStartCommandResult.message());
    }
    // Cinematic 1265 is already the explicitly selected intro playback.
    (void)levelCinematicRuntime_.consumeCinematicStartRequests();

    levelMusicRuntime_.reset();
    constexpr game::LevelMusicTrack calmMusic =
        game::LevelMusicTrack::DowntownCalm;
    constexpr game::LevelMusicTrack mixedMusic =
        game::LevelMusicTrack::DowntownMixed;
    result = playNamedAudio(
        game::LevelMusicRuntime::eventName(calmMusic),
        levelMusicBank_.track(calmMusic), true);
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
            transition.to == game::LevelMusicTrack::DowntownCalm ? 1.0F
                                                                  : 0.0F);
        if (transitionResult) {
            transitionResult = setVolume(
                game::LevelMusicTrack::DowntownMixed,
                transition.to == game::LevelMusicTrack::DowntownMixed ? 1.0F
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
            game::LevelMusicRuntime::loops(transition.to), 1.0F,
            transition.fadeMilliseconds);
    };

    const auto startGameplayCinematic =
        [this, &autoplay, &traceTimeMilliseconds](
            std::int32_t cinematicId) -> Result {
        const bool alreadyActive = gameplayCinematics_.active(cinematicId);
        Result startResult = gameplayCinematics_.start(cinematicId);
        if (startResult && autoplay && !alreadyActive) {
            autoplay->recordEvent(traceTimeMilliseconds, "cinematic_start",
                                  "cinematic=" +
                                      std::to_string(cinematicId));
        }
        return startResult;
    };
    const auto diagnosticCinematicId = [this]() -> std::int32_t {
        if (const game::GameplayCinematicPlayback* presentation =
                gameplayCinematics_.presentation()) {
            return presentation->asset->objectId;
        }
        const auto activeIds = gameplayCinematics_.activeIds();
        return activeIds.empty() ? -1 : activeIds.front();
    };

    const auto introStart = std::chrono::steady_clock::now();
    auto previousFrame = introStart;
    const auto playGameplaySound =
        [&playAudio](const audio::PcmAudio& clip, bool loop) {
            return playAudio(clip, loop, "player_state");
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
    float scaledDeltaRemainderMilliseconds = 0.0F;
    std::uint64_t syntheticElapsedMilliseconds =
        autoplay ? autoplay->startTimeMilliseconds() : 0;
    std::uint64_t accumulatedGameMilliseconds = 0;
    std::uint64_t frameIndex = 0;
    bool gameOverCinematicRequested = false;
    bool gameplayVisibilityInitialized = false;
    bool exitAfterPresent = false;
    const auto shouldRunFrame = [&] {
        return autoplay ? !autoplay->complete() && !autoplay->failed()
                        : window_.pumpMessages();
    };
    while (shouldRunFrame()) {
        const auto frameTime = std::chrono::steady_clock::now();
        std::uint32_t realDeltaMilliseconds = 0;
        if (autoplay) {
            realDeltaMilliseconds = autoplay->fixedStepMilliseconds();
            syntheticElapsedMilliseconds += realDeltaMilliseconds;
        } else {
            const auto frameElapsed =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    frameTime - previousFrame);
            realDeltaMilliseconds = static_cast<std::uint32_t>(
                std::clamp<std::int64_t>(frameElapsed.count(), 0, 100));
            syntheticElapsedMilliseconds = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    frameTime - introStart)
                    .count());
        }
        traceTimeMilliseconds = syntheticElapsedMilliseconds;
        previousFrame = frameTime;
        scaledDeltaRemainderMilliseconds +=
            levelCinematicRuntime_.updateSlowMotion(
                static_cast<float>(realDeltaMilliseconds));
        const auto gameDeltaMilliseconds = static_cast<std::uint32_t>(
            std::max(0.0F, std::floor(scaledDeltaRemainderMilliseconds)));
        accumulatedGameMilliseconds += gameDeltaMilliseconds;
        scaledDeltaRemainderMilliseconds -=
            static_cast<float>(gameDeltaMilliseconds);
        // CGameCamera::UpdateShake counts fixed Application update calls and
        // is therefore paced by real 50 ms ticks, not the scaled game delta.
        levelCinematicRuntime_.advanceCameraShake(realDeltaMilliseconds);
        objectRuntime_.advanceAnimations(gameDeltaMilliseconds);
        effectRuntime_.update(gameDeltaMilliseconds);
        if (audioEnabled) {
            audio_.update();
        }
        keyRouter_.beginFrame();
        if (!autoplay) {
            controller_.poll(
                [this](const reconstructed::XperiaKeyEvent& event) {
                    keyRouter_.route(event,
                                     reconstructed::InputContext::Gameplay);
                });
        }
        const auto introDuration =
            levelOne_.introCameraAnimation().durationMilliseconds();
        const auto timestamp = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(syntheticElapsedMilliseconds,
                                    introDuration));
        const bool gameplayActive =
            syntheticElapsedMilliseconds >= introDuration;
        if (gameplayActive && !gameplayVisibilityInitialized) {
            // CCinematicThread::MustBeVisibleRoom is an override owned by the
            // presenting cinematic. It expires with the intro rather than
            // becoming persistent level state.
            levelCinematicRuntime_.clearCinematicRoomOverride();
            renderer_.setCinematicVisibleRooms(
                levelCinematicRuntime_.forcedVisibleRooms());
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
            !gameplayPlayer_.cinematicDriven() && !restoreRuntime_.active() &&
            !quickTimeEvent_.active() &&
            !gameplayCinematics_.hasActiveColladaPlayback();
        diagnostics::AutoplayFrameInput autoplayInput;
        if (autoplay) {
            const game::CameraPose harnessCamera = gameplayActive
                ? gameplayCamera_.sample(gameplayPlayer_.position())
                : levelOne_.introCamera().sample(timestamp);
            autoplayInput = autoplay->update(
                {frameIndex,
                 syntheticElapsedMilliseconds,
                 accumulatedGameMilliseconds,
                 gameplayActive,
                 harnessControlsEnabled,
                 quickTimeEvent_.active(),
                 cinematicUi_.tutorialVisible(),
                 restoreRuntime_.active(),
                 restoreRuntime_.blackOverlayAlpha(),
                 gameplayCamera_.currentAreaId(),
                 diagnosticCinematicId(),
                 gameplayCinematics_.activeIds(),
                 gameplayPlayer_.position(),
                 gameplayPlayer_.facing(),
                 gameplayPlayer_.health(),
                 gameplayPlayer_.activeAnimation(),
                 gameplayPlayer_.animationTimeMilliseconds(),
                 gameplayPlayer_.activeStateId(),
                 gameplayPlayer_.activeStateName(),
                 gameplayPlayer_.punchTransitionReadyAfterImpact(),
                 gameplayPlayer_.onWall(),
                 harnessCamera,
                 renderer_.roomVisibility(),
                 enemyRuntime_.states()});
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
        }
        Result soundResult = Result::success();
        Result uiResult = Result::success();
        const bool fastForwardingIntro =
            autoplay && frameIndex == 0 &&
            autoplay->startTimeMilliseconds() != 0;
        result = introPlayer_.advanceTo(
            timestamp,
            [this, &soundResult, &uiResult, &autoplay,
             &traceTimeMilliseconds, &playNamedAudio,
             &stopNamedAudio, fastForwardingIntro](
                        const game::CinematicThread& thread,
                        const game::CinematicCommand& command) {
                if (!soundResult || !uiResult) {
                    return;
                }
                if (autoplay) {
                    autoplay->recordCommand(traceTimeMilliseconds,
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
                uiResult = levelCinematicRuntime_.applyCommand(command);
                if (!uiResult) {
                    return;
                }
                if (!fastForwardingIntro) {
                    uiResult = cinematicUi_.applyCommand(command);
                }
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
        if (!soundResult || !uiResult) {
            return fail(!soundResult ? soundResult.message()
                                     : uiResult.message());
        }
        result = renderer_.updateLevelOneActors(levelOne_, timestamp);
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
                audio_.setListener(cameraPose.position, cameraPose.target,
                                   cameraPose.up);
            }
            result = renderer_.setCamera(cameraPose);
        } else {
            if (gameplayPlayer_.dead() && !gameOverCinematicRequested) {
                result = startGameplayCinematic(
                    levelOne_.player().endGameCinematicId);
                if (!result) {
                    return fail(result.message());
                }
                gameOverCinematicRequested = true;
            }
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
                !gameplayPlayer_.cinematicDriven() &&
                !restoreRuntime_.active() &&
                !quickTimeEvent_.active() &&
                !gameplayCinematics_.hasActiveColladaPlayback();
            if (!controlsEnabled) {
                motion = {};
            }
            const bool jumpPressed = autoplay
                ? autoplayInput.jumpPressed
                : keyRouter_.state().jump.pressed;
            const bool webPressed = autoplay
                ? autoplayInput.webPressed
                : keyRouter_.state().web.pressed;
            const bool webReleased = autoplay
                ? autoplayInput.webReleased
                : keyRouter_.state().web.released;
            const bool punchPressed = autoplay
                ? autoplayInput.punchPressed
                : keyRouter_.state().punch.pressed;
            if (controlsEnabled && jumpPressed) {
                (void)gameplayPlayer_.requestJump();
            }
            if (controlsEnabled && webPressed) {
                (void)gameplayPlayer_.requestWeb();
            }
            if (controlsEnabled && webReleased) {
                (void)gameplayPlayer_.releaseWeb();
            }
            if (controlsEnabled && punchPressed &&
                gameplayPlayer_.requestPunch()) {
                if (autoplay) {
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds, "player_action",
                        "punch_accepted;state=" +
                            std::to_string(gameplayPlayer_.activeStateId()) +
                            ";name=" +
                            std::string(gameplayPlayer_.activeStateName()));
                }
            }
            const auto cameraBeforeMovement =
                gameplayCamera_.sample(gameplayPlayer_.position());
            gameplayPlayer_.update(motion, cameraBeforeMovement,
                                   gameDeltaMilliseconds);
            restoreRuntime_.update(gameplayPlayer_.position(),
                                   gameDeltaMilliseconds);
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
                            std::to_string(event.trigger->damage));
                }
                if (!gameplayPlayer_.dead()) {
                    gameplayPlayer_.restoreAt(event.restorePoint->position,
                                              event.restorePoint->facing);
                }
            }
            for (std::string_view enteredState =
                     gameplayPlayer_.consumeEnteredState();
                 !enteredState.empty();
                 enteredState = gameplayPlayer_.consumeEnteredState()) {
                result = playerSounds_.dispatchStateEnter(enteredState,
                                                          playGameplaySound);
                if (!result) {
                    return fail(result.message());
                }
            }
            for (std::string_view frameSoundState =
                     gameplayPlayer_.consumeAttackFrameSound();
                 !frameSoundState.empty();
                 frameSoundState =
                     gameplayPlayer_.consumeAttackFrameSound()) {
                result = playerSounds_.dispatchStateFrame(frameSoundState,
                                                          playGameplaySound);
                if (!result) {
                    return fail(result.message());
                }
            }
            for (std::optional<game::PlayerMeleeImpact> impact =
                     gameplayPlayer_.consumeMeleeImpact();
                 impact.has_value();
                 impact = gameplayPlayer_.consumeMeleeImpact()) {
                const float sectorHalfAngle = std::max(
                    std::abs(impact->minimumAngleDegrees),
                    std::abs(impact->maximumAngleDegrees));
                const float minimumForwardDot =
                    std::cos(sectorHalfAngle * 0.017453292519943295F);
                const auto hitEnemy = enemyRuntime_.applyPlayerMeleeHit(
                    gameplayPlayer_.position(), gameplayPlayer_.facing(),
                    impact->maximumReach, impact->damage,
                    minimumForwardDot);
                if (autoplay) {
                    std::string detail =
                        "state=" + std::to_string(impact->stateId) +
                        ";name=" + std::string(impact->stateName) +
                        ";damage=" + std::to_string(impact->damage) +
                        ";reach=" + std::to_string(impact->maximumReach) +
                        ";result=";
                    detail += hitEnemy
                        ? "enemy=" + std::to_string(*hitEnemy)
                        : "miss";
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds,
                        "player_melee_impact", detail);
                }
            }
            (void)gameplayCamera_.updateArea(gameplayPlayer_.position(),
                                             gameDeltaMilliseconds);
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
                        record != nullptr && record->id == 0xa1 ? 0.5F : 1.0F;
                    result = playNamedAudio(voiceName, clip->second, true,
                                            volume, 500);
                } else {
                    result = stopNamedAudio(voiceName, 500);
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
            if (result) {
                Result commandResult = Result::success();
                bool cinematicDamageApplied = false;
                result = gameplayCinematics_.update(
                    gameDeltaMilliseconds,
                    [this, &commandResult, &cinematicDamageApplied,
                     &playSpatialSound, &autoplay, &traceTimeMilliseconds,
                     &playNamedAudio, &stopNamedAudio](
                        const game::CinematicThread& thread,
                        const game::CinematicCommand& command) {
                        if (autoplay) {
                            autoplay->recordCommand(traceTimeMilliseconds,
                                                    thread.objectId, command);
                        }
                        if (command.name == "IfEnemyDead") {
                            std::int32_t enemyId = thread.objectId;
                            (void)commandInteger(command, "IDEnemy", enemyId);
                            const game::LevelEnemyState* enemy =
                                enemyRuntime_.find(enemyId);
                            return enemy != nullptr && enemy->health <= 0.0F;
                        }
                        if (command.name == "IfObjectDestroyed") {
                            std::int32_t objectId = thread.objectId;
                            (void)commandInteger(command, "ObjectID", objectId);
                            const game::LevelEnemyState* enemy =
                                enemyRuntime_.find(objectId);
                            return enemy != nullptr && enemy->health <= 0.0F;
                        }
                        if (commandResult) {
                            const float healthBefore = gameplayPlayer_.health();
                            commandResult =
                                gameplayPlayer_.applyCinematicCommand(thread,
                                                                        command);
                            cinematicDamageApplied =
                                cinematicDamageApplied ||
                                gameplayPlayer_.health() < healthBefore;
                        }
                        if (commandResult) {
                            commandResult = objectRuntime_.applyCinematicCommand(
                                levelOne_, thread, command);
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
                                quickTimeEvent_.applyCommand(command);
                        }
                        if (commandResult) {
                            commandResult =
                                levelCinematicRuntime_.applyCommand(command);
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
                        "k_state_hurt_light", playGameplaySound);
                }
                for (const game::LevelCinematicAsset* completedCinematic :
                     gameplayCinematics_.consumeCompletions()) {
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
            quickTimeEvent_.update(
                gameDeltaMilliseconds,
                autoplay ? autoplayInput.quickTimeEventPressed
                         : keyRouter_.state().quickTimeEvent.pressed);
            if (const auto qteCinematic =
                    quickTimeEvent_.consumeCinematicRequest()) {
                result = startGameplayCinematic(*qteCinematic);
            }
            if (!gameplayCinematics_.hasActiveColladaPlayback()) {
                enemyRuntime_.updateGameplay(gameDeltaMilliseconds,
                                             gameplayPlayer_.position(),
                                             &levelCollision_);
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
                if (autoplay) {
                    autoplay->recordEvent(
                        syntheticElapsedMilliseconds, "enemy_hit_player",
                        "enemy=" + std::to_string(hit.sourceObjectId) +
                            ";attack=" + std::to_string(hit.attackId) +
                            ";damage=" + std::to_string(hit.damage));
                }
                if (!restoreRuntime_.active() &&
                    gameplayPlayer_.applyDamage(hit.damage)) {
                    result = playerSounds_.dispatchStateEnter(
                        "k_state_hurt_light", playGameplaySound);
                    if (!result) {
                        return fail(result.message());
                    }
                }
            }
            if (!restoreRuntime_.active()) {
                levelDamageRuntime_.update(gameplayPlayer_.position(),
                                           gameDeltaMilliseconds);
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
                                                event.damageType, 1000)) {
                    result = playerSounds_.dispatchStateEnter(
                        event.damageType == 1 ? "k_state_hurt_heavy"
                                              : "k_state_hurt_light",
                        playGameplaySound);
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
                         record->distanceCullingEnabled});
                    if (!result) {
                        return fail(result.message());
                    }
                } else if (event.kind ==
                           game::LevelDropEventKind::HitPlayer) {
                    if (!restoreRuntime_.active() &&
                        gameplayPlayer_.applyDamage(event.damage)) {
                        result = playerSounds_.dispatchStateEnter(
                            "k_state_hurt_light", playGameplaySound);
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
            result = applyMusicTransition(levelMusicRuntime_.update(
                enemyRuntime_.states(), gameplayPlayer_.dead()));
            if (!result) {
                return fail(result.message());
            }
            playerHudHealth_.update(gameplayPlayer_.health(),
                                    gameDeltaMilliseconds);
            result = renderer_.updatePlayerHud(
                levelOne_.hud(), playerHudHealth_.currentRatio(),
                playerHudHealth_.delayedRatio(), 1.0F,
                enemyRuntime_.shownHealthBarEnemy(),
                gameplayPlayer_.skillPoints(),
                levelBonusRuntime_.showSkillPointTotal(),
                &levelBonusRuntime_.skillPointPopup());
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
                result = renderer_.updateWebLine(
                    gameplayPlayer_.webLineActive(),
                    gameplayPlayer_.webLineAnchor(),
                    gameplayPlayer_.webLineAttachPosition());
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
                result = renderer_.updateEnemyGunLines(
                    enemyRuntime_.gunLines());
            }
            if (result) {
                game::CameraPose cameraPose;
                const game::GameplayCinematicPlayback* presentation =
                    gameplayCinematics_.presentation();
                if (presentation != nullptr &&
                    presentation->asset->hasColladaPlayback()) {
                    cameraPose =
                        presentation->asset->animatedCamera.sample(
                            presentation->elapsedMilliseconds);
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
                    audio_.setListener(finalCameraPose.position,
                                       finalCameraPose.target,
                                       finalCameraPose.up);
                }
                result = renderer_.setCamera(finalCameraPose);
            }
        }
        if (result) {
            result = renderer_.updateLevelOneObjects(levelOne_,
                                                     objectRuntime_);
        }
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
            result = renderer_.updateLevelOneEffects(
                levelOne_.effects(), effectRuntime_, levelBonusRuntime_);
        }
        cinematicUi_.update(
            gameDeltaMilliseconds,
            autoplay ? autoplayInput.quickTimeEventPressed
                     : keyRouter_.state().quickTimeEvent.pressed);
        const float qteProgress =
            quickTimeEvent_.active() &&
                    quickTimeEvent_.durationMilliseconds() != 0
                ? static_cast<float>(quickTimeEvent_.elapsedMilliseconds()) /
                      static_cast<float>(
                          quickTimeEvent_.durationMilliseconds())
                : 0.0F;
        if (result) {
            game::CinematicUiFrame uiFrame = cinematicUi_.frame(
                quickTimeEvent_.active(), qteProgress);
            uiFrame.blackOverlayAlpha = restoreRuntime_.blackOverlayAlpha();
            result = renderer_.updateCinematicUi(uiFrame);
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
        if (!result) {
            return fail(result.message());
        }
        renderer_.renderFrame();
        if (autoplay) {
            const bool finalControlsEnabled =
                gameplayActive && levelCinematicRuntime_.controlsEnabled() &&
                !gameplayPlayer_.cinematicDriven() &&
                !restoreRuntime_.active() && !quickTimeEvent_.active() &&
                !gameplayCinematics_.hasActiveColladaPlayback();
            autoplay->recordFrame(
                {frameIndex,
                 syntheticElapsedMilliseconds,
                 accumulatedGameMilliseconds,
                 gameplayActive,
                 finalControlsEnabled,
                 quickTimeEvent_.active(),
                 cinematicUi_.tutorialVisible(),
                 restoreRuntime_.active(),
                 restoreRuntime_.blackOverlayAlpha(),
                 gameplayCamera_.currentAreaId(),
                 diagnosticCinematicId(),
                 gameplayCinematics_.activeIds(),
                 gameplayPlayer_.position(),
                 gameplayPlayer_.facing(),
                 gameplayPlayer_.health(),
                 gameplayPlayer_.activeAnimation(),
                 gameplayPlayer_.animationTimeMilliseconds(),
                 gameplayPlayer_.activeStateId(),
                 gameplayPlayer_.activeStateName(),
                 gameplayPlayer_.punchTransitionReadyAfterImpact(),
                 gameplayPlayer_.onWall(),
                 presentedCameraPose,
                 renderer_.roomVisibility(),
                 enemyRuntime_.states()});
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
        if (exitAfterPresent) {
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
    return EXIT_SUCCESS;
}

} // namespace usm
