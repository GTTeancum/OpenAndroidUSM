#pragma once

#include "audio/OggAudio.hpp"
#include "core/Result.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace usm::audio {

// Native replacement for VoxSoundFile's event-name lookup. The original
// record-table binary is absent from the supplied data, so direct filename
// matches remain distinguishable from explicitly documented spelling aliases.
class SoundEventCatalog final {
public:
    [[nodiscard]] Result index(const std::filesystem::path& soundRoot);
    [[nodiscard]] const std::filesystem::path* resolve(
        std::string_view eventName) const noexcept;
    [[nodiscard]] Result decode(std::string_view eventName,
                                PcmAudio& decoded) const;
    [[nodiscard]] std::size_t eventCount() const noexcept {
        return pathsByEvent_.size();
    }
    [[nodiscard]] std::size_t ambiguousEventCount() const noexcept;

private:
    std::map<std::string, std::vector<std::filesystem::path>, std::less<>>
        pathsByEvent_;
};

} // namespace usm::audio
