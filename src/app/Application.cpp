#include "app/Application.hpp"

#include "platform/windows/GameDataLocator.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
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
    result = playerStateConfigs_.load(gameDataRoot);
    if (!result) {
        return fail(result.message());
    }
    constexpr std::array<std::string_view, 7> gameplaySoundStates{
        "k_state_idle_to_punch_right", "k_state_hurt_light",
        "k_state_jump_start", "k_state_jump_land",
        "k_state_swing_web_throw", "k_state_swing_hang",
        "k_state_swing_idle"};
    result = playerSounds_.preload(playerStateConfigs_, voxSounds_,
                                   soundCatalog_, gameplaySoundStates);
    if (!result) {
        return fail(result.message());
    }
    constexpr std::array<std::int16_t, 2> firstLevelEnemyTypes{0, 1};
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
                                        levelOne_.webGrabPoints());
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
    levelCinematicRuntime_.bind(triggerRuntime_, gameplayCamera_);
    game::CinematicPlayer introStartCommands;
    result = introStartCommands.start(levelOne_.introStartScript());
    if (!result) {
        return fail(result.message());
    }
    Result introStartCommandResult = Result::success();
    result = introStartCommands.advanceTo(
        introStartCommands.durationMilliseconds(),
        [this, &introStartCommandResult](
            const game::CinematicThread&,
            const game::CinematicCommand& command) {
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
        return gameplayCinematicPlayer_.start(cinematic->script);
    };

    const auto introStart = std::chrono::steady_clock::now();
    auto previousFrame = introStart;
    const auto playGameplaySound =
        [this](const audio::PcmAudio& clip, bool loop) {
            return audio_.play(clip, loop);
        };
    while (window_.pumpMessages()) {
        const auto frameTime = std::chrono::steady_clock::now();
        const auto frameElapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                frameTime - previousFrame);
        previousFrame = frameTime;
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
        result = introPlayer_.advanceTo(
            timestamp,
            [this, &soundResult](const game::CinematicThread&,
                                 const game::CinematicCommand& command) {
                if (!soundResult) {
                    return;
                }
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
        if (!soundResult) {
            return fail(soundResult.message());
        }
        result = renderer_.updateLevelOneActors(levelOne_, timestamp);
        if (!result) {
            return fail(result.message());
        }
        if (elapsed.count() < introDuration) {
            result =
                renderer_.setCamera(levelOne_.introCamera().sample(timestamp));
        } else {
            const auto stick = controller_.leftStick();
            game::PlayerMotionInput motion{stick.x, stick.y};
            if (motion.right == 0.0F && motion.forward == 0.0F) {
                const auto& input = keyRouter_.state();
                motion.right = static_cast<float>(input.moveRight.held) -
                               static_cast<float>(input.moveLeft.held);
                motion.forward = static_cast<float>(input.moveUp.held) -
                                 static_cast<float>(input.moveDown.held);
            }
            if (keyRouter_.state().jump.pressed) {
                (void)gameplayPlayer_.requestJump();
            }
            if (keyRouter_.state().web.pressed) {
                (void)gameplayPlayer_.requestWeb();
            }
            if (keyRouter_.state().web.released) {
                (void)gameplayPlayer_.releaseWeb();
            }
            if (keyRouter_.state().punch.pressed &&
                gameplayPlayer_.requestPunch()) {
                result = playerSounds_.dispatchStateEnter(
                    "k_state_idle_to_punch_right", playGameplaySound);
                if (!result) {
                    return fail(result.message());
                }
            }
            const auto cameraBeforeMovement =
                gameplayCamera_.sample(gameplayPlayer_.position());
            const auto deltaMilliseconds = static_cast<std::uint32_t>(
                std::clamp<std::int64_t>(frameElapsed.count(), 0, 100));
            gameplayPlayer_.update(motion, cameraBeforeMovement,
                                   deltaMilliseconds);
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
                                             deltaMilliseconds);
            const auto triggerEvents =
                triggerRuntime_.update(gameplayPlayer_.position());
            if (activeGameplayCinematic_ == nullptr) {
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
                            deltaMilliseconds,
                        gameplayCinematicPlayer_.durationMilliseconds());
                Result commandResult = Result::success();
                result = gameplayCinematicPlayer_.advanceTo(
                    gameplayCinematicTimeMilliseconds_,
                    [this, &commandResult](
                        const game::CinematicThread& thread,
                        const game::CinematicCommand& command) {
                        if (commandResult) {
                            commandResult = enemyRuntime_.applyCinematicCommand(
                                levelOne_, thread, command);
                        }
                        if (commandResult) {
                            commandResult =
                                levelCinematicRuntime_.applyCommand(command);
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
                    });
                if (result && !commandResult) {
                    result = commandResult;
                }
                if (result && gameplayCinematicPlayer_.finished()) {
                    activeGameplayCinematic_ = nullptr;
                    auto chainedCinematics = levelCinematicRuntime_
                                                 .consumeCinematicStartRequests();
                    if (chainedCinematics.size() > 1) {
                        result = Result::failure(
                            "Concurrent gameplay cinematics are not yet "
                            "reconstructed");
                    } else if (!chainedCinematics.empty()) {
                        result = startGameplayCinematic(
                            chainedCinematics.front());
                    }
                }
            }
            enemyRuntime_.updateGameplay(deltaMilliseconds,
                                         gameplayPlayer_.position(),
                                         &levelCollision_);
            for (const game::EnemySoundCue& cue :
                 enemyRuntime_.consumeSoundCues()) {
                result = enemySounds_.dispatch(cue.voxSoundId,
                                               playGameplaySound);
                if (!result) {
                    return fail(result.message());
                }
            }
            for (const game::EnemyMeleeHit& hit :
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
                                    deltaMilliseconds);
            result = renderer_.updatePlayerHud(
                levelOne_.hud(), playerHudHealth_.currentRatio(),
                playerHudHealth_.delayedRatio(), 1.0F);
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
                result = renderer_.setCamera(
                    gameplayCamera_.sample(gameplayPlayer_.position()));
            }
        }
        if (!result) {
            return fail(result.message());
        }
        renderer_.renderFrame();
    }
    return EXIT_SUCCESS;
}

} // namespace usm
