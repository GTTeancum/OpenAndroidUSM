#pragma once

#include "audio/OggAudio.hpp"
#include "audio/SoundEventCatalog.hpp"
#include "audio/VoxSoundTable.hpp"
#include "core/Result.hpp"
#include "game/PlayerStateConfig.hpp"
#include "game/NativeRandomizer.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string_view>
#include <vector>

namespace usm::audio {

using PlayPlayerStateSound =
    std::function<Result(std::int16_t voxSoundId,
                         std::string_view eventName,
                         const PcmAudio& audio, bool loop)>;
using StopPlayerStateSound =
    std::function<Result(std::int16_t voxSoundId,
                         std::string_view eventName)>;

// Predecoded native playback for the state-triggered SoundConfig lists used by
// Player::PlayerStateSFX (0x00349154) and Player::PlaySound (0x0034905c).
class PlayerStateSoundBank final {
public:
    [[nodiscard]] Result preload(
        const game::PlayerStateConfigDatabase& states,
        const VoxSoundTable& voxSounds, const SoundEventCatalog& catalog,
        std::span<const std::string_view> stateNames,
        game::NativeRandomizer* nativeRandomizer = nullptr);
    [[nodiscard]] Result dispatchStateEnter(
        std::string_view stateName, const PlayPlayerStateSound& play,
        const StopPlayerStateSound& stop = {});
    [[nodiscard]] Result dispatchStateFrame(
        std::string_view stateName, const PlayPlayerStateSound& play);
    [[nodiscard]] Result dispatchEmitter(
        std::int16_t configId, std::size_t emitterIndex,
        const PlayPlayerStateSound& play);

    [[nodiscard]] std::size_t decodedVariantCount() const noexcept;
    [[nodiscard]] Result cleanActive(const StopPlayerStateSound& stop);

private:
    struct DecodedVariant {
        std::int16_t voxSoundId{-1};
        std::string eventName;
        PcmAudio audio;
        bool looping{};
    };

    [[nodiscard]] Result dispatchConfigs(
        std::span<const std::int16_t> configIds,
        const PlayPlayerStateSound& play);
    [[nodiscard]] Result dispatchVariant(
        std::int16_t configId, std::size_t variantIndex,
        const PlayPlayerStateSound& play);

    const game::PlayerStateConfigDatabase* states_{};
    game::NativeRandomizer ownedNativeRandomizer_;
    game::NativeRandomizer* nativeRandomizer_{&ownedNativeRandomizer_};
    std::map<std::int16_t, std::vector<DecodedVariant>> decodedByConfig_;
    std::vector<std::int16_t> activeConfigIds_;
};

} // namespace usm::audio
