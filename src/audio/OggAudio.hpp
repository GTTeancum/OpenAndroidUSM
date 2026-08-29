#pragma once

#include "core/Result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace usm::audio {

struct PcmAudio final {
    std::uint32_t sampleRate{};
    std::uint16_t channelCount{};
    std::vector<std::int16_t> interleavedSamples;

    [[nodiscard]] std::size_t frameCount() const noexcept {
        return channelCount == 0 ? 0 : interleavedSamples.size() / channelCount;
    }
};

// Reconstructs the game's Ogg/Vorbis assets as signed, interleaved 16-bit PCM.
[[nodiscard]] Result decodeOggVorbis(std::span<const std::byte> encoded,
                                     PcmAudio& decoded);

} // namespace usm::audio
