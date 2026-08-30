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
    constexpr std::array<std::string_view, 8> gameplaySoundStates{
        "k_state_idle_to_punch_right", "k_state_hurt_light",
        "k_state_jump_start", "k_state_jump_land",
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
    result = introPlayer_.start(levelOne_.introScript());
    if (!result) {
        return fail(result.message());
    }
    result = renderer_.uploadLevelOneScene(levelOne_);
    if (!result) {
        return fail(result.message());
    }
    result = renderer_.setCamera(levelOne_.introCamera().sample(0));
    if (!result) {
        return fail(result.message());
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
    result = enemyRuntime_.initialize(levelOne_);
    if (!result) {
        return fail(result.message());
    }
    result = objectRuntime_.initialize(levelOne_);
    if (!result) {
        return fail(result.message());
    }
    result = effectRuntime_.initialize(levelOne_.effects().presets);
    if (!result) {
        return fail(result.message());
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
                    levelCinematicRuntime_.applyCommand(command);
            }
        });
    if (!result || !introStartCommandResult) {
        return fail(!result ? result.message()
                            : introStartCommandResult.message());
    }
    // Cinematic 1265 is already the explicitly selected intro playback.
    (void)levelCinematicRuntime_.consumeCinematicStartRequests();

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
            result = renderer_.setCamera(
                levelCinematicRuntime_.applyCameraShake(
                    levelOne_.introCamera().sample(timestamp)));
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
                    [this, &commandResult, &cinematicDamageApplied](
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
                result = enemySounds_.dispatch(cue.voxSoundId,
                                               playGameplaySound);
                if (!result) {
                    return fail(result.message());
                }
            }
            for (const game::EnemyPlayerHit& hit :
                 enemyRuntime_.consumePlayerHits()) {
                if (gameplayPlayer_.applyDamage(hit.damage)) {
                    result = playerSounds_.dispatchStateEnter(
                        "k_state_hurt_light", playGameplaySound);
                    if (!result) {
                        return fail(result.message());
                    }
                }
            }
            playerHudHealth_.update(gameplayPlayer_.health(),
                                    gameDeltaMilliseconds);
            result = renderer_.updatePlayerHud(
                levelOne_.hud(), playerHudHealth_.currentRatio(),
                playerHudHealth_.delayedRatio(), 1.0F,
                enemyRuntime_.shownHealthBarEnemy());
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
                result = renderer_.setCamera(
                    levelCinematicRuntime_.applyCameraShake(cameraPose));
            }
        }
        if (result) {
            result = renderer_.updateLevelOneObjects(levelOne_,
                                                     objectRuntime_);
        }
        if (result) {
            result = renderer_.updateLevelOneEffects(
                levelOne_.effects(), effectRuntime_);
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
            result = renderer_.updateCinematicUi(cinematicUi_.frame(
                quickTimeEvent_.active(), qteProgress));
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
