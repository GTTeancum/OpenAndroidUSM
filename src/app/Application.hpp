#pragma once

#include "audio/xaudio2/XAudio2System.hpp"
#include "audio/CinematicSoundBank.hpp"
#include "audio/EnemyBehaviorSoundBank.hpp"
#include "audio/LevelMusicBank.hpp"
#include "audio/SoundEventCatalog.hpp"
#include "audio/VoxSoundTable.hpp"
#include "audio/PlayerStateSoundBank.hpp"
#include "game/CinematicPlayer.hpp"
#include "game/CinematicUiRuntime.hpp"
#include "game/GameplayCamera.hpp"
#include "game/GameplayPlayer.hpp"
#include "game/LevelCollision.hpp"
#include "game/LevelCinematicRuntime.hpp"
#include "game/LevelBonusRuntime.hpp"
#include "game/LevelEnemyRuntime.hpp"
#include "game/LevelEffectRuntime.hpp"
#include "game/LevelDropRuntime.hpp"
#include "game/LevelHintRuntime.hpp"
#include "game/LevelMusicRuntime.hpp"
#include "game/LevelObjectRuntime.hpp"
#include "game/QuickTimeEventRuntime.hpp"
#include "game/LevelTriggerRuntime.hpp"
#include "game/LevelTriggerSoundRuntime.hpp"
#include "game/LevelOneBootstrap.hpp"
#include "game/PlayerHudHealthState.hpp"
#include "game/PlayerStateConfig.hpp"
#include "platform/windows/Window.hpp"
#include "platform/windows/XInputController.hpp"
#include "reconstructed/input/XperiaKeyRouter.hpp"
#include "renderer/d3d11/D3D11Renderer.hpp"

#include <Windows.h>

#include <map>
#include <string>

namespace usm {

class Application final {
public:
    [[nodiscard]] int run(HINSTANCE instance);

private:
    platform::Window window_;
    platform::XInputController controller_;
    reconstructed::XperiaKeyRouter keyRouter_;
    game::LevelOneBootstrap levelOne_;
    renderer::D3D11Renderer renderer_;
    audio::XAudio2System audio_;
    audio::VoxSoundTable voxSounds_;
    audio::SoundEventCatalog soundCatalog_;
    audio::PlayerStateSoundBank playerSounds_;
    audio::EnemyBehaviorSoundBank enemySounds_;
    audio::LevelMusicBank levelMusicBank_;
    audio::CinematicSoundBank introSounds_;
    audio::CinematicSoundBank gameplaySounds_;
    audio::PcmAudio slowMotionEnterSound_;
    audio::PcmAudio slowMotionExitSound_;
    audio::PcmAudio bonusCollectSound_;
    audio::PcmAudio dropObjectSound_;
    std::map<std::string, audio::PcmAudio, std::less<>> triggerSoundClips_;
    game::CinematicPlayer introPlayer_;
    game::CinematicUiRuntime cinematicUi_;
    game::GameplayCamera gameplayCamera_;
    game::LevelCollision levelCollision_;
    game::GameplayPlayer gameplayPlayer_;
    game::PlayerHudHealthState playerHudHealth_;
    game::PlayerStateConfigDatabase playerStateConfigs_;
    game::LevelTriggerRuntime triggerRuntime_;
    game::LevelTriggerSoundRuntime triggerSoundRuntime_;
    game::LevelCinematicRuntime levelCinematicRuntime_;
    game::LevelBonusRuntime levelBonusRuntime_;
    game::LevelEnemyRuntime enemyRuntime_;
    game::LevelEffectRuntime effectRuntime_;
    game::LevelDropRuntime dropRuntime_;
    game::LevelHintRuntime hintRuntime_;
    game::LevelMusicRuntime levelMusicRuntime_;
    game::LevelObjectRuntime objectRuntime_;
    game::CinematicPlayer gameplayCinematicPlayer_;
    game::QuickTimeEventRuntime quickTimeEvent_;
    const game::LevelCinematicAsset* activeGameplayCinematic_{};
    std::uint32_t gameplayCinematicTimeMilliseconds_{};
    std::uint32_t gameplayCinematicDurationMilliseconds_{};
};

} // namespace usm
