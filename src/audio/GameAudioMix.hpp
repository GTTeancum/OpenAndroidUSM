#pragma once

namespace usm::audio::GameAudioMix {

// TGameSetting::Reset (0x003e268c) initializes both profile-backed group
// controls to 0.9. CGameProfile::Load applies music to VoxSound group 1 and
// sound effects to group 2 through VoxSoundManager::SetGroupVolume.
constexpr float DefaultMusicGroupVolume = 0.9F;
constexpr float DefaultSoundEffectsGroupVolume = 0.9F;

// The Android profile value above is preserved verbatim. The Windows port
// applies a separate -6 dB output trim to music after user testing found the
// decoded score overpowering gameplay audio. This is host-output calibration,
// not an inferred native VoxSound setting.
constexpr float WindowsMusicOutputTrim = 0.5011872F;
constexpr float DefaultMusicOutputVolume =
    DefaultMusicGroupVolume * WindowsMusicOutputTrim;

} // namespace usm::audio::GameAudioMix
