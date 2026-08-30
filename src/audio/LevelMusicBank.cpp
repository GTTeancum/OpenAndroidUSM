#include "audio/LevelMusicBank.hpp"

#include "audio/SoundEventCatalog.hpp"

#include <array>
#include <string>

namespace usm::audio {
namespace {

constexpr std::size_t trackIndex(game::LevelMusicTrack track) noexcept {
    return static_cast<std::size_t>(track);
}

} // namespace

Result LevelMusicBank::preload(const SoundEventCatalog& catalog) {
    tracks_ = {};
    constexpr std::array tracks{
        game::LevelMusicTrack::DowntownCalm,
        game::LevelMusicTrack::DowntownMixed,
        game::LevelMusicTrack::BossSandman,
        game::LevelMusicTrack::Lose,
    };
    for (const game::LevelMusicTrack track : tracks) {
        Result result = catalog.decode(game::LevelMusicRuntime::eventName(track),
                                       tracks_[trackIndex(track)]);
        if (!result) {
            tracks_ = {};
            return Result::failure("Could not preload level music " +
                                   std::string(
                                       game::LevelMusicRuntime::eventName(track)) +
                                   ": " + result.message());
        }
    }
    const PcmAudio& calm = track(game::LevelMusicTrack::DowntownCalm);
    const PcmAudio& mixed = track(game::LevelMusicTrack::DowntownMixed);
    if (calm.sampleRate != mixed.sampleRate ||
        calm.channelCount != mixed.channelCount ||
        calm.frameCount() != mixed.frameCount()) {
        tracks_ = {};
        return Result::failure(
            "Downtown adaptive music tracks are not sample-aligned");
    }
    return Result::success();
}

const PcmAudio& LevelMusicBank::track(game::LevelMusicTrack track) const
    noexcept {
    return tracks_[trackIndex(track)];
}

} // namespace usm::audio
