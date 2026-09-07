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
#include "game/DeathConfirmationRuntime.hpp"
#include "game/ExitMenuRuntime.hpp"
#include "game/GameplayCamera.hpp"
#include "game/GameplayCinematicScheduler.hpp"
#include "game/GameplayPlayer.hpp"
#include "game/LevelCollision.hpp"
#include "game/LevelCheckPointRuntime.hpp"
#include "game/LevelCinematicRuntime.hpp"
#include "game/LevelBonusRuntime.hpp"
#include "game/LevelDamageRuntime.hpp"
#include "game/LevelDeathRuntime.hpp"
#include "game/LevelEnemyRuntime.hpp"
#include "game/LevelEffectRuntime.hpp"
#include "game/LevelDropRuntime.hpp"
#include "game/LevelHintRuntime.hpp"
#include "game/LevelHostageRuntime.hpp"
#include "game/LevelMusicRuntime.hpp"
#include "game/NativeRandomizer.hpp"
#include "game/LevelObjectRuntime.hpp"
#include "game/LevelRestoreRuntime.hpp"
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

#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace usm {

struct ApplicationOptions {
    std::optional<std::filesystem::path> autoplayScript;
    std::optional<std::filesystem::path> autoplayOutput;
    std::uint32_t levelNumber{1};
    bool autoplayAudio{};
};

class Application final {
public:
    [[nodiscard]] int run(HINSTANCE instance,
                          const ApplicationOptions& options = {});

private:
    platform::Window window_;
    platform::XInputController controller_;
    reconstructed::XperiaKeyRouter keyRouter_;
    game::LevelOneBootstrap levelOne_;
    renderer::D3D11Renderer renderer_;
    audio::XAudio2System audio_;
    audio::VoxSoundTable voxSounds_;
    audio::SoundEventCatalog soundCatalog_;
    game::NativeRandomizer nativeRandomizer_;
    audio::PlayerStateSoundBank playerSounds_;
    audio::EnemyBehaviorSoundBank enemySounds_;
    audio::LevelMusicBank levelMusicBank_;
    audio::CinematicSoundBank introSounds_;
    audio::CinematicSoundBank gameplaySounds_;
    audio::PcmAudio slowMotionEnterSound_;
    audio::PcmAudio slowMotionExitSound_;
    audio::PcmAudio transportInSound_;
    audio::PcmAudio transportOutSound_;
    audio::PcmAudio bonusCollectSound_;
    audio::PcmAudio comicCollectSound_;
    audio::PcmAudio dropObjectSound_;
    audio::PcmAudio ultimateSplashSound_;
    std::map<std::int32_t, audio::PcmAudio> hostageSounds_;
    std::map<std::int32_t, audio::PcmAudio> wallWebSounds_;
    std::int32_t wallWebLoopSoundId_{-1};
    std::map<std::int32_t, audio::PcmAudio> destroyableHitSounds_;
    std::map<std::string, audio::PcmAudio, std::less<>> triggerSoundClips_;
    game::CinematicPlayer introPlayer_;
    game::CinematicUiRuntime cinematicUi_;
    game::GameplayCamera gameplayCamera_;
    game::LevelCollision levelCollision_;
    game::LevelCheckPointRuntime checkPointRuntime_;
    game::GameplayPlayer gameplayPlayer_;
    game::PlayerHudHealthState playerHudHealth_;
    game::PlayerStateConfigDatabase playerStateConfigs_;
    game::LevelTriggerRuntime triggerRuntime_;
    game::LevelTriggerSoundRuntime triggerSoundRuntime_;
    game::LevelCinematicRuntime levelCinematicRuntime_;
    game::LevelBonusRuntime levelBonusRuntime_;
    game::LevelHostageRuntime hostageRuntime_;
    game::LevelDamageRuntime levelDamageRuntime_;
    game::LevelDeathRuntime levelDeathRuntime_;
    game::DeathConfirmationRuntime deathConfirmationRuntime_;
    game::ExitMenuRuntime exitMenuRuntime_;
    game::LevelEnemyRuntime enemyRuntime_;
    game::LevelEffectRuntime effectRuntime_;
    game::LevelDropRuntime dropRuntime_;
    game::LevelHintRuntime hintRuntime_;
    game::LevelMusicRuntime levelMusicRuntime_;
    game::LevelObjectRuntime objectRuntime_;
    game::LevelRestoreRuntime restoreRuntime_;
    game::GameplayCinematicScheduler gameplayCinematics_;
    game::QuickTimeEventRuntime quickTimeEvent_;
};

} // namespace usm
