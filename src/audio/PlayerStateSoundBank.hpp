#pragma once

#include "audio/OggAudio.hpp"
#include "audio/SoundEventCatalog.hpp"
#include "audio/VoxSoundTable.hpp"
#include "core/Result.hpp"
#include "game/PlayerStateConfig.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string_view>
#include <vector>

namespace usm::audio {

using PlayPlayerStateSound =
    std::function<Result(const PcmAudio& audio, bool loop)>;

// Predecoded native playback for the state-triggered SoundConfig lists used by
// Player::PlayerStateSFX (0x00349154) and Player::PlaySound (0x0034905c).
class PlayerStateSoundBank final {
public:
    [[nodiscard]] Result preload(
        const game::PlayerStateConfigDatabase& states,
        const VoxSoundTable& voxSounds, const SoundEventCatalog& catalog,
        std::span<const std::string_view> stateNames);
    [[nodiscard]] Result dispatchStateEnter(
        std::string_view stateName, const PlayPlayerStateSound& play);
    [[nodiscard]] Result dispatchStateFrame(
        std::string_view stateName, const PlayPlayerStateSound& play);

    [[nodiscard]] std::size_t decodedVariantCount() const noexcept;

private:
    struct DecodedVariant {
        PcmAudio audio;
        bool looping{};
    };

    [[nodiscard]] Result dispatchConfigs(
        std::span<const std::int16_t> configIds,
        const PlayPlayerStateSound& play);

    const game::PlayerStateConfigDatabase* states_{};
    std::map<std::int16_t, std::vector<DecodedVariant>> decodedByConfig_;
    std::map<std::int16_t, std::size_t> nextVariantByConfig_;
};

} // namespace usm::audio
