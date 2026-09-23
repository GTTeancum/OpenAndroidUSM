#include "game/HostageSound.hpp"

namespace usm::game {
Result HostageSoundCallbacks::dispatch(const HostageSoundCue& cue) const {
    switch (cue.action) {
    case HostageSoundAction::PlayOnceIfStopped:
        if (!isPlaying || !playOnce) {
            return Result::failure("Hostage sound needs live query and playback callbacks");
        }
        if (isPlaying(cue.voxSoundId)) { return Result::success(); }
        return playOnce(cue.hostageObjectId, cue.voxSoundId);
    case HostageSoundAction::PlayOnce:
        if (!playOnce) { return Result::failure("Hostage playback callback is missing"); }
        return playOnce(cue.hostageObjectId, cue.voxSoundId);
    case HostageSoundAction::Stop:
        if (!stop) { return Result::failure("Hostage stop callback is missing"); }
        return stop(cue.voxSoundId);
    }
    return Result::failure("Invalid hostage sound action");
}
} // namespace usm::game
