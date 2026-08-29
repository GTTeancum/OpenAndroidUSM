#include "app/Application.hpp"

#include "platform/windows/GameDataLocator.hpp"

#include <Windows.h>

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
    result = renderer_.uploadPreviewGeometry(
        levelOne_.previewGeometry(), levelOne_.previewTexture().mipLevels());
    if (!result) {
        return fail(result.message());
    }

    while (window_.pumpMessages()) {
        audio_.update();
        keyRouter_.beginFrame();
        controller_.poll([this](const reconstructed::XperiaKeyEvent& event) {
            keyRouter_.route(event, reconstructed::InputContext::Gameplay);
        });
        renderer_.renderFrame();
    }
    return EXIT_SUCCESS;
}

} // namespace usm
