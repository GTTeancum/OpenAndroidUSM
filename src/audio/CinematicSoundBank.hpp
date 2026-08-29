#pragma once

#include "audio/OggAudio.hpp"
#include "audio/SoundEventCatalog.hpp"
#include "core/Result.hpp"
#include "game/CinematicScript.hpp"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace usm::audio {

using PlayCinematicSound =
    std::function<Result(const PcmAudio& audio, bool loop)>;

// Predecoded sound events referenced by a CFF command stream. This keeps Ogg
// decoding off the frame that dispatches the timestamped SoundControl command.
class CinematicSoundBank final {
public:
    [[nodiscard]] Result preload(const game::CinematicScript& script,
                                 const SoundEventCatalog& catalog);
    [[nodiscard]] Result dispatch(
        const game::CinematicCommand& command,
        const PlayCinematicSound& play) const;

    [[nodiscard]] std::size_t loadedEventCount() const noexcept {
        return decodedByEvent_.size();
    }
    [[nodiscard]] const std::vector<std::string>& unresolvedEvents() const
        noexcept {
        return unresolvedEvents_;
    }

private:
    std::map<std::string, PcmAudio, std::less<>> decodedByEvent_;
    std::vector<std::string> unresolvedEvents_;
};

} // namespace usm::audio
