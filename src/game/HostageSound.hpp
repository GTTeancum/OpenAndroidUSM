#pragma once

#include "core/Result.hpp"
#include <cstdint>
#include <functional>

namespace usm::game {
// CHostage::Update ELF 0x328454..0x32847e issues IsPlaying(0x18b,true)
// followed, only when false, by Audible::PlayAudio(0x18b,true,false).
// The latter's transient branch ignores its loop argument and calls Play3D
// with loop=false (ELF 0x3bbf6c..0x3bbf84). This is NOT a looping voice.
enum class HostageSoundAction { PlayOnce, PlayOnceIfStopped, Stop };
struct HostageSoundCue {
    std::int32_t hostageObjectId{-1};
    std::uint16_t voxSoundId{};
    HostageSoundAction action{HostageSoundAction::PlayOnce};
};
using HostageSoundHandler = std::function<Result(const HostageSoundCue&)>;

// Scoped transient Audible path used by hostage 30018, not a reconstruction
// of every persistent/attached emitter. The Android VoxSoundManager fallback
// queries/stops media by sound ID; it ignores the supplied scene-object ID.
// A request is not proof of playback: failed, culled and completed voices
// must leave isPlaying false. No synthetic duration or gameplay-clock latch.
struct HostageSoundCallbacks {
    std::function<bool(std::uint16_t)> isPlaying;
    std::function<Result(std::int32_t, std::uint16_t)> playOnce;
    std::function<Result(std::uint16_t)> stop;
    [[nodiscard]] Result dispatch(const HostageSoundCue& cue) const;
};

// Call-scoped, never stored in checkpoint copies. Empty hooks are detached
// diagnostics: sound commands are queued, with no claimed playback status.
struct HostageUpdateHooks {
    HostageSoundHandler sound;
    std::function<void(bool enabled, bool preservePauseButton)> enableControls;
};
} // namespace usm::game
