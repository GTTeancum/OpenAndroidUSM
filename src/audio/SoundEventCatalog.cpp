#include "audio/SoundEventCatalog.hpp"

#include "audio/VoxSoundTable.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>

namespace usm::audio {
namespace {

std::string normalizedEventName(std::string_view value) {
    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::toupper(character));
                   });
    return normalized;
}

std::string normalizedResourcePath(std::filesystem::path value) {
    value.replace_extension();
    std::string normalized = value.generic_string();
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::toupper(character));
                   });
    return normalized;
}

} // namespace

Result SoundEventCatalog::index(const std::filesystem::path& soundRoot,
                                const VoxSoundTable* voxSounds) {
    pathsByEvent_.clear();
    configuredPathsByEvent_.clear();
    std::error_code error;
    if (!std::filesystem::is_directory(soundRoot, error)) {
        return Result::failure("Sound root is not a readable directory: " +
                               soundRoot.string());
    }

    std::map<std::string, std::filesystem::path, std::less<>> pathsByResource;
    for (std::filesystem::recursive_directory_iterator iterator(soundRoot, error),
         end;
         iterator != end; iterator.increment(error)) {
        if (error) {
            pathsByEvent_.clear();
            return Result::failure("Could not enumerate sound resources: " +
                                   error.message());
        }
        if (!iterator->is_regular_file() ||
            normalizedEventName(iterator->path().extension().string()) !=
                ".OGG") {
            continue;
        }
        pathsByEvent_[normalizedEventName(
                          iterator->path().stem().string())]
            .push_back(iterator->path());
        const std::filesystem::path relative =
            std::filesystem::relative(iterator->path(), soundRoot, error);
        if (error) {
            pathsByEvent_.clear();
            return Result::failure("Could not index sound resource path: " +
                                   error.message());
        }
        pathsByResource.emplace(normalizedResourcePath(relative),
                                iterator->path());
    }
    if (pathsByEvent_.empty()) {
        return Result::failure("Sound root contains no Ogg/Vorbis resources");
    }
    if (voxSounds != nullptr) {
        for (const VoxSoundRecord& record : voxSounds->records()) {
            const auto resource = pathsByResource.find(
                normalizedResourcePath(record.resourcePath));
            if (resource != pathsByResource.end()) {
                configuredPathsByEvent_.emplace(
                    normalizedEventName(record.eventName), resource->second);
            }
        }
    }
    return Result::success();
}

const std::filesystem::path* SoundEventCatalog::resolve(
    std::string_view eventName) const noexcept {
    std::string normalized = normalizedEventName(eventName);
    const auto configured = configuredPathsByEvent_.find(normalized);
    if (configured != configuredPathsByEvent_.end()) {
        return &configured->second;
    }
    auto iterator = pathsByEvent_.find(normalized);
    if (iterator == pathsByEvent_.end() || iterator->second.size() != 1) {
        return nullptr;
    }
    return &iterator->second.front();
}

Result SoundEventCatalog::decode(std::string_view eventName,
                                 PcmAudio& decoded) const {
    const std::filesystem::path* path = resolve(eventName);
    if (path == nullptr) {
        return Result::failure("Sound event is missing or ambiguous: " +
                               std::string(eventName));
    }
    std::ifstream stream(*path, std::ios::binary);
    if (!stream) {
        return Result::failure("Could not open sound resource: " +
                               path->string());
    }
    const std::vector<char> encoded(
        (std::istreambuf_iterator<char>(stream)),
        std::istreambuf_iterator<char>());
    const auto bytes = std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(encoded.data()), encoded.size());
    return decodeOggVorbis(bytes, decoded);
}

std::size_t SoundEventCatalog::ambiguousEventCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(
        pathsByEvent_.begin(), pathsByEvent_.end(),
        [](const auto& entry) { return entry.second.size() > 1; }));
}

} // namespace usm::audio
