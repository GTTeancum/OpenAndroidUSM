#include "app/Application.hpp"

#include "platform/windows/GameDataLocator.hpp"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <filesystem>

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
    result = renderer_.uploadSceneGeometry(levelOne_.roomGeometry(),
                                           levelOne_.roomTextures());
    if (!result) {
        return fail(result.message());
    }
    result = renderer_.setCamera(levelOne_.introCamera().sample(0));
    if (!result) {
        return fail(result.message());
    }

    const auto introStart = std::chrono::steady_clock::now();
    while (window_.pumpMessages()) {
        audio_.update();
        keyRouter_.beginFrame();
        controller_.poll([this](const reconstructed::XperiaKeyEvent& event) {
            keyRouter_.route(event, reconstructed::InputContext::Gameplay);
        });
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - introStart);
        const auto timestamp = static_cast<std::uint32_t>(std::min<std::int64_t>(
            elapsed.count(),
            levelOne_.introCameraAnimation().durationMilliseconds()));
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
        result = renderer_.setCamera(levelOne_.introCamera().sample(timestamp));
        if (!result) {
            return fail(result.message());
        }
        renderer_.renderFrame();
    }
    return EXIT_SUCCESS;
}

} // namespace usm
