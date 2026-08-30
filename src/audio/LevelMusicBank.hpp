#pragma once

#include "audio/OggAudio.hpp"
#include "core/Result.hpp"
#include "game/LevelMusicRuntime.hpp"

#include <array>

namespace usm::audio {

class SoundEventCatalog;

class LevelMusicBank final {
public:
    [[nodiscard]] Result preload(const SoundEventCatalog& catalog);
    [[nodiscard]] const PcmAudio& track(game::LevelMusicTrack track) const
        noexcept;

private:
    static constexpr std::size_t kTrackCount = 4;
    std::array<PcmAudio, kTrackCount> tracks_;
};

} // namespace usm::audio
