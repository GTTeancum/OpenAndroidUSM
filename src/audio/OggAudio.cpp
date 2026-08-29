#include "audio/OggAudio.hpp"

#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

#include <cstdlib>
#include <limits>

namespace usm::audio {

Result decodeOggVorbis(std::span<const std::byte> encoded, PcmAudio& decoded) {
    decoded = {};
    if (encoded.empty()) {
        return Result::failure("Ogg/Vorbis resource is empty");
    }
    if (encoded.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return Result::failure("Ogg/Vorbis resource exceeds decoder size limits");
    }

    short* samples = nullptr;
    int channels = 0;
    int sampleRate = 0;
    const int frames = stb_vorbis_decode_memory(
        reinterpret_cast<const unsigned char*>(encoded.data()),
        static_cast<int>(encoded.size()), &channels, &sampleRate, &samples);
    if (frames < 0 || samples == nullptr || channels <= 0 || sampleRate <= 0) {
        std::free(samples);
        return Result::failure("Could not decode Ogg/Vorbis resource");
    }
    if (channels > static_cast<int>(std::numeric_limits<std::uint16_t>::max())) {
        std::free(samples);
        return Result::failure("Ogg/Vorbis channel count is unsupported");
    }

    const auto sampleCount = static_cast<std::size_t>(frames) *
                             static_cast<std::size_t>(channels);
    decoded.sampleRate = static_cast<std::uint32_t>(sampleRate);
    decoded.channelCount = static_cast<std::uint16_t>(channels);
    decoded.interleavedSamples.assign(samples, samples + sampleCount);
    std::free(samples);
    return Result::success();
}

} // namespace usm::audio
