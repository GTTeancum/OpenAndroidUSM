#include "app/Application.hpp"

#include "platform/windows/GameDataLocator.hpp"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <filesystem>
#include <limits>

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
    result = soundCatalog_.index(gameDataRoot / "sound");
    if (!result) {
        return fail(result.message());
    }
    result = introSounds_.preload(levelOne_.introScript(), soundCatalog_);
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
    result = levelCollision_.build(levelOne_.introRooms());
    if (!result) {
        return fail(result.message());
    }
    result = gameplayPlayer_.initialize(levelOne_.player(), &levelCollision_);
    if (!result) {
        return fail(result.message());
    }
    triggerRuntime_.bind(levelOne_.triggers());
    result = enemyRuntime_.initialize(levelOne_);
    if (!result) {
        return fail(result.message());
    }

    const auto introStart = std::chrono::steady_clock::now();
    auto previousFrame = introStart;
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
                    command, [this](const audio::PcmAudio& clip, bool loop) {
                        return audio_.play(clip, loop);
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
            if (keyRouter_.state().punch.pressed) {
                (void)gameplayPlayer_.requestPunch();
            }
            const auto cameraBeforeMovement =
                gameplayCamera_.sample(gameplayPlayer_.position());
            const auto deltaMilliseconds = static_cast<std::uint32_t>(
                std::clamp<std::int64_t>(frameElapsed.count(), 0, 100));
            gameplayPlayer_.update(motion, cameraBeforeMovement,
                                   deltaMilliseconds);
            if (gameplayPlayer_.consumePunchImpact()) {
                // Unit::DecreaseHealth (0x002fe890): ATTACK_HIT_NORMAL in
                // EnemysAttackConfigs.bin authors 35 damage, 200 cm extents,
                // and a -90..90 degree forward attack sector.
                (void)enemyRuntime_.applyPlayerMeleeHit(
                    gameplayPlayer_.position(), gameplayPlayer_.facing(),
                    200.0F, 35.0F, 0.0F);
            }
            (void)gameplayCamera_.updateArea(gameplayPlayer_.position(),
                                             deltaMilliseconds);
            const auto triggerEvents =
                triggerRuntime_.update(gameplayPlayer_.position());
            if (activeGameplayCinematic_ == nullptr) {
                for (const game::TriggerEvent& event : triggerEvents) {
                    const auto cinematic = std::find_if(
                        levelOne_.cinematics().begin(),
                        levelOne_.cinematics().end(),
                        [&event](const game::LevelCinematicAsset& candidate) {
                            return candidate.objectId == event.cinematicId;
                        });
                    if (cinematic == levelOne_.cinematics().end()) {
                        continue;
                    }
                    activeGameplayCinematic_ = &*cinematic;
                    gameplayCinematicTimeMilliseconds_ = 0;
                    result = gameplayCinematicPlayer_.start(cinematic->script);
                    break;
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
                    });
                if (result && !commandResult) {
                    result = commandResult;
                }
                if (result && gameplayCinematicPlayer_.finished()) {
                    activeGameplayCinematic_ = nullptr;
                }
            }
            enemyRuntime_.updateGameplay(deltaMilliseconds,
                                         gameplayPlayer_.position(),
                                         &levelCollision_);
            const assets::ColladaAnimationClip* activeClip =
                levelOne_.player().animationBank.findClip(
                    gameplayPlayer_.activeAnimation());
            if (activeClip == nullptr) {
                return fail("The active player animation is missing");
            }
            result = renderer_.updateLevelOnePlayer(
                levelOne_, *activeClip,
                gameplayPlayer_.animationTimeMilliseconds(),
                gameplayPlayer_.worldTransform());
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
