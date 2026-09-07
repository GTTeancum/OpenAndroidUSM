#include "app/Application.hpp"

#include <Windows.h>
#include <shellapi.h>

#include <cstdlib>
#include <string_view>

namespace {

bool parseOptions(usm::ApplicationOptions& options, std::wstring& error) {
    int argumentCount = 0;
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (arguments == nullptr) {
        error = L"CommandLineToArgvW failed";
        return false;
    }
    const auto releaseArguments = [&] { LocalFree(arguments); };
    for (int index = 1; index < argumentCount; ++index) {
        const std::wstring_view argument(arguments[index]);
        if (argument == L"--autoplay" || argument == L"--output" ||
            argument == L"--level") {
            if (index + 1 >= argumentCount) {
                error = std::wstring(argument) + L" requires a value";
                releaseArguments();
                return false;
            }
            const std::wstring_view value(arguments[++index]);
            if (argument == L"--autoplay") {
                options.autoplayScript = std::filesystem::path(value);
            } else if (argument == L"--output") {
                options.autoplayOutput = std::filesystem::path(value);
            } else {
                wchar_t* end = nullptr;
                const unsigned long parsed =
                    std::wcstoul(value.data(), &end, 10);
                if (end != value.data() + value.size() || parsed < 1 ||
                    parsed > 12) {
                    error = L"--level requires a number from 1 through 12";
                    releaseArguments();
                    return false;
                }
                options.levelNumber = static_cast<std::uint32_t>(parsed);
            }
        } else if (argument == L"--autoplay-audio") {
            options.autoplayAudio = true;
        } else {
            error = L"Unknown option: " + std::wstring(argument);
            releaseArguments();
            return false;
        }
    }
    releaseArguments();
    if (options.autoplayScript && !options.autoplayOutput) {
        options.autoplayOutput = L"autoplay-output";
    }
    if (!options.autoplayScript && options.autoplayOutput) {
        error = L"--output requires --autoplay";
        return false;
    }
    return true;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    usm::ApplicationOptions options;
    std::wstring error;
    if (!parseOptions(options, error)) {
        MessageBoxW(nullptr, error.c_str(), L"OpenAndroidUSM command line",
                    MB_OK | MB_ICONERROR);
        return EXIT_FAILURE;
    }
    usm::Application application;
    return application.run(instance, options);
}
