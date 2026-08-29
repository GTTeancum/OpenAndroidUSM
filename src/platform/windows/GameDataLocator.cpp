#include "platform/windows/GameDataLocator.hpp"

#include <Windows.h>

#include <optional>
#include <vector>

namespace usm::platform {
namespace {

bool isGameDataRoot(const std::filesystem::path& path) {
    return std::filesystem::is_regular_file(path / "levelnew_01.pack") &&
           std::filesystem::is_regular_file(path / "entities.pack") &&
           std::filesystem::is_directory(path / "sound");
}

std::optional<std::filesystem::path> executableDirectory() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) {
        return std::nullopt;
    }
    path.resize(length);
    return std::filesystem::path(path).parent_path();
}

void appendAncestorCandidates(const std::filesystem::path& start,
                              std::vector<std::filesystem::path>& candidates) {
    std::filesystem::path directory = start;
    for (int depth = 0; depth < 8 && !directory.empty(); ++depth) {
        candidates.push_back(directory);
        candidates.push_back(directory / "game" / "data" / "gameloft" /
                             "games" / "spiderman");
        const std::filesystem::path parent = directory.parent_path();
        if (parent == directory) {
            break;
        }
        directory = parent;
    }
}

} // namespace

Result locateGameData(std::filesystem::path& output) {
    std::vector<std::filesystem::path> candidates;

    std::wstring environmentValue(32768, L'\0');
    const DWORD environmentLength = GetEnvironmentVariableW(
        L"OPENANDROIDUSM_DATA", environmentValue.data(),
        static_cast<DWORD>(environmentValue.size()));
    if (environmentLength > 0 && environmentLength < environmentValue.size()) {
        environmentValue.resize(environmentLength);
        candidates.emplace_back(environmentValue);
    }

    appendAncestorCandidates(std::filesystem::current_path(), candidates);
    if (const auto executable = executableDirectory()) {
        appendAncestorCandidates(*executable, candidates);
    }

    for (const std::filesystem::path& candidate : candidates) {
        std::error_code error;
        const std::filesystem::path normalized =
            std::filesystem::weakly_canonical(candidate, error);
        if (!error && isGameDataRoot(normalized)) {
            output = normalized;
            return Result::success();
        }
    }
    return Result::failure(
        "Game data was not found. Set OPENANDROIDUSM_DATA to the spiderman "
        "data directory containing levelnew_01.pack.");
}

} // namespace usm::platform
