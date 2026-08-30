#include "app/Application.hpp"

#include "platform/windows/GameDataLocator.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <string>
#include <filesystem>
#include <limits>
#include <optional>
#include <vector>

namespace usm {
namespace {

int fail(std::string_view message) {
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

int Application::run(HINSTANCE instance) {
    Result result = window_.create(instance, L"OpenAndroidUSM", 1280, 720);
    if (!result) {
        return fail(result.message());
    }

    result = renderer_.initialize(window_.nativeHandle(), window_.clientWidth(),
                                  window_.clientHeight());
    if (!result) {
        return fail(result.message());
    }

    result = audio_.initialize();
    if (!result) {
        return fail(result.message());
    }

    std::filesystem::path gameDataRoot;
    result = platform::locateGameData(gameDataRoot);
    if (!result) {
        return fail(result.message());
    }
    result = levelOne_.load(gameDataRoot);
    if (!result) {
        return fail(result.message());
    }
    const game::AttackDefinition* normalPunchAttack =
        levelOne_.attackConfigs().find(7);
    if (normalPunchAttack == nullptr) {
        return fail("ATTACK_HIT_NORMAL is missing from the combat config");
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
    constexpr std::array<std::string_view, 9> gameplaySoundStates{
        "k_state_idle_to_punch_right", "k_state_hurt_light",
        "k_state_hurt_heavy", "k_state_jump_start", "k_state_jump_land",
        "k_state_swing_web_throw", "k_state_swing_hang",
        "k_state_swing_idle", "k_state_trigger_slider_move"};
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
    audio_.setListener(initialCameraPose.position, initialCameraPose.target,
                       initialCameraPose.up);
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
    result = audio_.playNamed(
        game::LevelMusicRuntime::eventName(calmMusic),
        levelMusicBank_.track(calmMusic), true);
    if (result) {
        // The two downtown files are sample-aligned. Keeping the mixed voice
        // running silently matches the native cursor-preserving transition
        // without restarting the score when combat begins.
        result = audio_.playNamed(
            game::LevelMusicRuntime::eventName(mixedMusic),
            levelMusicBank_.track(mixedMusic), true, 0.0F);
    }
    if (!result) {
        return fail(result.message());
    }

    const auto applyMusicTransition =
        [this](const game::LevelMusicTransition& transition) -> Result {
        if (transition.from == transition.to) {
            return Result::success();
        }
        const auto setVolume =
            [this, &transition](game::LevelMusicTrack track, float volume) {
                return audio_.setNamedVolume(
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
            transitionResult = audio_.stopNamed(
                game::LevelMusicRuntime::eventName(transition.from),
                transition.fadeMilliseconds);
        } else if (transition.from == game::LevelMusicTrack::Lose) {
            transitionResult = audio_.stopNamed(
                game::LevelMusicRuntime::eventName(transition.from),
                transition.fadeMilliseconds);
        }
        if (!transitionResult ||
            transition.to == game::LevelMusicTrack::DowntownCalm ||
            transition.to == game::LevelMusicTrack::DowntownMixed) {
            return transitionResult;
        }
        return audio_.playNamed(
            game::LevelMusicRuntime::eventName(transition.to),
            levelMusicBank_.track(transition.to),
            game::LevelMusicRuntime::loops(transition.to), 1.0F,
            transition.fadeMilliseconds);
    };

    const auto startGameplayCinematic =
        [this](std::int32_t cinematicId) -> Result {
        const auto cinematic = std::find_if(
            levelOne_.cinematics().begin(), levelOne_.cinematics().end(),
            [cinematicId](const game::LevelCinematicAsset& candidate) {
                return candidate.objectId == cinematicId &&
                       candidate.scriptAvailable;
            });
        if (cinematic == levelOne_.cinematics().end()) {
            return Result::failure(
                "Cinematic command references an unavailable script");
        }
        activeGameplayCinematic_ = &*cinematic;
        gameplayCinematicTimeMilliseconds_ = 0;
        Result startResult = gameplayCinematicPlayer_.start(cinematic->script);
        if (!startResult) {
            activeGameplayCinematic_ = nullptr;
            return startResult;
        }
        gameplayCinematicDurationMilliseconds_ = std::max(
            gameplayCinematicPlayer_.durationMilliseconds(),
            cinematic->colladaDurationMilliseconds);
        return Result::success();
    };

    const auto introStart = std::chrono::steady_clock::now();
    auto previousFrame = introStart;
    const auto playGameplaySound =
        [this](const audio::PcmAudio& clip, bool loop) {
            return audio_.play(clip, loop);
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
        [this, &cinematicSoundPosition](
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
        return audio_.playNamed3D(
            eventName, clip,
            {*position, record->minimumDistance, record->maximumDistance,
             record->distanceCullingEnabled},
            loop);
    };
    float scaledDeltaRemainderMilliseconds = 0.0F;
    bool gameOverCinematicRequested = false;
    bool exitAfterPresent = false;
    while (window_.pumpMessages()) {
        const auto frameTime = std::chrono::steady_clock::now();
        const auto frameElapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                frameTime - previousFrame);
        const auto realDeltaMilliseconds = static_cast<std::uint32_t>(
            std::clamp<std::int64_t>(frameElapsed.count(), 0, 100));
        previousFrame = frameTime;
        scaledDeltaRemainderMilliseconds +=
            levelCinematicRuntime_.updateSlowMotion(
                static_cast<float>(realDeltaMilliseconds));
        const auto gameDeltaMilliseconds = static_cast<std::uint32_t>(
            std::max(0.0F, std::floor(scaledDeltaRemainderMilliseconds)));
        scaledDeltaRemainderMilliseconds -=
            static_cast<float>(gameDeltaMilliseconds);
        // CGameCamera::UpdateShake counts fixed Application update calls and
        // is therefore paced by real 50 ms ticks, not the scaled game delta.
        levelCinematicRuntime_.advanceCameraShake(realDeltaMilliseconds);
        objectRuntime_.advanceAnimations(gameDeltaMilliseconds);
        effectRuntime_.update(gameDeltaMilliseconds);
        audio_.update();
        keyRouter_.beginFrame();
        controller_.poll([this](const reconstructed::XperiaKeyEvent& event) {
            keyRouter_.route(event, reconstructed::InputContext::Gameplay);
        });
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            frameTime - introStart);
        const auto introDuration =
            levelOne_.introCameraAnimation().durationMilliseconds();
        const auto timestamp = static_cast<std::uint32_t>(
            std::min<std::int64_t>(elapsed.count(), introDuration));
        Result soundResult = Result::success();
        Result uiResult = Result::success();
        result = introPlayer_.advanceTo(
            timestamp,
            [this, &soundResult,
             &uiResult](const game::CinematicThread& thread,
                        const game::CinematicCommand& command) {
                if (!soundResult || !uiResult) {
                    return;
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
                uiResult = cinematicUi_.applyCommand(command);
                soundResult = introSounds_.dispatch(
                    command,
                    [this](std::string_view eventName,
                           const audio::PcmAudio& clip, bool loop) {
                        return audio_.playNamed(eventName, clip, loop);
                    },
                    [this](std::string_view eventName) {
                        return audio_.stopNamed(eventName);
                    });
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
        if (elapsed.count() < introDuration) {
            renderer_.setCameraAreaRoomVisibility(
                gameplayCamera_.mustInvisibleRooms(),
                gameplayCamera_.mustVisibleRooms());
            renderer_.setCinematicVisibleRooms(
                levelCinematicRuntime_.forcedVisibleRooms());
            const game::CameraPose cameraPose =
                levelCinematicRuntime_.applyCameraShake(
                    levelOne_.introCamera().sample(timestamp));
            audio_.setListener(cameraPose.position, cameraPose.target,
                               cameraPose.up);
            result = renderer_.setCamera(cameraPose);
        } else {
            if (gameplayPlayer_.dead() &&
                activeGameplayCinematic_ == nullptr &&
                !gameOverCinematicRequested) {
                result = startGameplayCinematic(
                    levelOne_.player().endGameCinematicId);
                if (!result) {
                    return fail(result.message());
                }
                gameOverCinematicRequested = true;
            }
            const auto stick = controller_.leftStick();
            game::PlayerMotionInput motion{stick.x, stick.y};
            if (motion.right == 0.0F && motion.forward == 0.0F) {
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
                !(activeGameplayCinematic_ != nullptr &&
                  activeGameplayCinematic_->hasColladaPlayback());
            if (!controlsEnabled) {
                motion = {};
            }
            if (controlsEnabled && keyRouter_.state().jump.pressed) {
                (void)gameplayPlayer_.requestJump();
            }
            if (controlsEnabled && keyRouter_.state().web.pressed) {
                (void)gameplayPlayer_.requestWeb();
            }
            if (controlsEnabled && keyRouter_.state().web.released) {
                (void)gameplayPlayer_.releaseWeb();
            }
            if (controlsEnabled && keyRouter_.state().punch.pressed &&
                gameplayPlayer_.requestPunch()) {
                result = playerSounds_.dispatchStateEnter(
                    "k_state_idle_to_punch_right", playGameplaySound);
                if (!result) {
                    return fail(result.message());
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
            if (gameplayPlayer_.consumePunchSoundFrame()) {
                result = playerSounds_.dispatchStateFrame(
                    "k_state_idle_to_punch_right", playGameplaySound);
                if (!result) {
                    return fail(result.message());
                }
            }
            if (gameplayPlayer_.consumePunchImpact()) {
                const float sectorHalfAngle = std::max(
                    std::abs(normalPunchAttack->minimumAngleDegrees),
                    std::abs(normalPunchAttack->maximumAngleDegrees));
                const float minimumForwardDot =
                    std::cos(sectorHalfAngle * 0.017453292519943295F);
                (void)enemyRuntime_.applyPlayerMeleeHit(
                    gameplayPlayer_.position(), gameplayPlayer_.facing(),
                    normalPunchAttack->maximumReach(),
                    normalPunchAttack->damage, minimumForwardDot);
            }
            (void)gameplayCamera_.updateArea(gameplayPlayer_.position(),
                                             gameDeltaMilliseconds);
            const auto triggerEvents =
                triggerRuntime_.update(gameplayPlayer_.position());
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
                    result = audio_.playNamed(voiceName, clip->second, true,
                                              volume, 500);
                } else {
                    result = audio_.stopNamed(voiceName, 500);
                }
                if (!result) {
                    return fail(result.message());
                }
            }
            if (activeGameplayCinematic_ == nullptr &&
                !quickTimeEvent_.active()) {
                for (const game::TriggerEvent& event : triggerEvents) {
                    result = startGameplayCinematic(event.cinematicId);
                    if (result) {
                        break;
                    }
                }
            }
            if (result && activeGameplayCinematic_ != nullptr) {
                gameplayCinematicTimeMilliseconds_ =
                    std::min<std::uint32_t>(
                        gameplayCinematicTimeMilliseconds_ +
                            gameDeltaMilliseconds,
                        gameplayCinematicDurationMilliseconds_);
                Result commandResult = Result::success();
                bool cinematicDamageApplied = false;
                result = gameplayCinematicPlayer_.advanceToConditional(
                    gameplayCinematicTimeMilliseconds_,
                    [this, &commandResult, &cinematicDamageApplied,
                     &playSpatialSound](
                        const game::CinematicThread& thread,
                        const game::CinematicCommand& command) {
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
                                [this](std::string_view eventName,
                                       const audio::PcmAudio& clip,
                                       bool loop) {
                                    return audio_.playNamed(eventName, clip,
                                                            loop);
                                },
                                [this](std::string_view eventName) {
                                    return audio_.stopNamed(eventName);
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
                if (result && gameplayCinematicPlayer_.finished() &&
                    gameplayCinematicTimeMilliseconds_ >=
                        gameplayCinematicDurationMilliseconds_) {
                    const game::LevelCinematicAsset* completedCinematic =
                        activeGameplayCinematic_;
                    auto chainedCinematics = levelCinematicRuntime_
                                                 .consumeCinematicStartRequests();
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
                        activeGameplayCinematic_ = nullptr;
                    }
                    if (!terminalCinematic && chainedCinematics.empty() &&
                        completedCinematic->nextCinematicId >= 0) {
                        chainedCinematics.push_back(
                            completedCinematic->nextCinematicId);
                    }
                    if (!terminalCinematic && chainedCinematics.size() > 1) {
                        result = Result::failure(
                            "Concurrent gameplay cinematics are not yet "
                            "reconstructed");
                    } else if (!terminalCinematic &&
                               !chainedCinematics.empty()) {
                        result = startGameplayCinematic(
                            chainedCinematics.front());
                    }
                }
            }
            quickTimeEvent_.update(
                gameDeltaMilliseconds,
                keyRouter_.state().quickTimeEvent.pressed);
            if (const auto qteCinematic =
                    quickTimeEvent_.consumeCinematicRequest()) {
                result = startGameplayCinematic(*qteCinematic);
            }
            if (activeGameplayCinematic_ == nullptr ||
                !activeGameplayCinematic_->hasColladaPlayback()) {
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
                    result = audio_.playNamed3D(
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
                if (grant.type == game::LevelBonusType::Health) {
                    gameplayPlayer_.addHealth(
                        static_cast<float>(grant.amount));
                } else if (grant.type == game::LevelBonusType::SkillPoint) {
                    gameplayPlayer_.addSkillPoints(grant.amount);
                }
                result = audio_.playNamed("SFX_ORBS_COLLECT",
                                          bonusCollectSound_);
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
                result = renderer_.updateGameplayCinematicActors(
                    levelOne_, activeGameplayCinematic_,
                    gameplayCinematicTimeMilliseconds_);
            }
            if (result) {
                result = renderer_.updateEnemyGunLines(
                    enemyRuntime_.gunLines());
            }
            if (result) {
                game::CameraPose cameraPose;
                if (activeGameplayCinematic_ != nullptr &&
                    activeGameplayCinematic_->hasColladaPlayback()) {
                    cameraPose =
                        activeGameplayCinematic_->animatedCamera.sample(
                            gameplayCinematicTimeMilliseconds_);
                } else if (activeGameplayCinematic_ != nullptr &&
                           activeGameplayCinematic_->cameraTrack.valid()) {
                    cameraPose = activeGameplayCinematic_->cameraTrack.sample(
                        gameplayCinematicTimeMilliseconds_);
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
                audio_.setListener(finalCameraPose.position,
                                   finalCameraPose.target,
                                   finalCameraPose.up);
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
            keyRouter_.state().quickTimeEvent.pressed);
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
                result = audio_.play(
                    cue == game::SlowMotionSoundCue::Enter
                        ? slowMotionEnterSound_
                        : slowMotionExitSound_,
                    false);
                if (!result) {
                    break;
                }
            }
        }
        if (!result) {
            return fail(result.message());
        }
        renderer_.renderFrame();
        if (exitAfterPresent) {
            break;
        }
    }
    return EXIT_SUCCESS;
}

} // namespace usm
