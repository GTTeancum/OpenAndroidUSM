#pragma once

#include "audio/OggAudio.hpp"
#include "core/Result.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace usm::audio {

class VoxSoundTable;

// Native event-name lookup backed by the original VoxSounds record table, with
// direct file stems retained as an explicit fallback for unlisted assets.
class SoundEventCatalog final {
public:
    [[nodiscard]] Result index(const std::filesystem::path& soundRoot,
                               const VoxSoundTable* voxSounds = nullptr);
    [[nodiscard]] const std::filesystem::path* resolve(
        std::string_view eventName) const noexcept;
    [[nodiscard]] Result decode(std::string_view eventName,
                                PcmAudio& decoded) const;
    [[nodiscard]] std::size_t eventCount() const noexcept {
        return pathsByEvent_.size();
    }
    [[nodiscard]] std::size_t ambiguousEventCount() const noexcept;
    [[nodiscard]] std::size_t configuredEventCount() const noexcept {
        return configuredPathsByEvent_.size();
    }

private:
    std::map<std::string, std::vector<std::filesystem::path>, std::less<>>
        pathsByEvent_;
    std::map<std::string, std::filesystem::path, std::less<>>
        configuredPathsByEvent_;
};

} // namespace usm::audio
