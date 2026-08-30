#include "assets/DdsAtcTexture.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

// The ATC color reconstruction below is derived from AMD Compressonator's
// compressonatori_tc.c.
// Copyright (c) 2007-2024 Advanced Micro Devices, Inc.
// Copyright (c) 2004-2006 ATI Technologies Inc.
// Licensed under the MIT License; see THIRD_PARTY_NOTICES.md.

namespace usm::assets {
namespace {

constexpr std::uint32_t kDdsHeaderSize = 124;
constexpr std::uint32_t kDdsPixelFormatSize = 32;
constexpr std::array<char, 4> kDdsMagic{'D', 'D', 'S', ' '};
constexpr std::array<char, 4> kAtcaFourCc{'A', 'T', 'C', 'A'};

struct Rgb final {
    std::uint8_t red{};
    std::uint8_t green{};
    std::uint8_t blue{};
};

std::uint32_t readU32(std::span<const std::byte> bytes,
                      std::size_t offset) noexcept {
    std::uint32_t value{};
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(
                     std::to_integer<std::uint8_t>(bytes[offset + index]))
                 << (index * 8);
    }
    return value;
}

bool matches(std::span<const std::byte> bytes, std::size_t offset,
             std::span<const char> expected) noexcept {
    if (offset > bytes.size() || expected.size() > bytes.size() - offset) {
        return false;
    }
    return std::equal(expected.begin(), expected.end(), bytes.begin() + offset,
                      [](char left, std::byte right) {
                          return static_cast<std::uint8_t>(left) ==
                                 std::to_integer<std::uint8_t>(right);
                      });
}

Rgb color565(std::uint16_t encoded) noexcept {
    return {
        static_cast<std::uint8_t>(((encoded & 0xf800U) >> 8U) |
                                  ((encoded & 0xe000U) >> 13U)),
        static_cast<std::uint8_t>(((encoded & 0x07e0U) >> 3U) |
                                  ((encoded & 0x0600U) >> 9U)),
        static_cast<std::uint8_t>(((encoded & 0x001fU) << 3U) |
                                  ((encoded & 0x001cU) >> 2U)),
    };
}

bool color1555(std::uint16_t encoded, Rgb& output) noexcept {
    output = {
        static_cast<std::uint8_t>(((encoded & 0x7c00U) >> 7U) |
                                  ((encoded & 0x7000U) >> 12U)),
        static_cast<std::uint8_t>(((encoded & 0x03e0U) >> 2U) |
                                  ((encoded & 0x0380U) >> 7U)),
        static_cast<std::uint8_t>(((encoded & 0x001fU) << 3U) |
                                  ((encoded & 0x001cU) >> 2U)),
    };
    return (encoded & 0x8000U) != 0;
}

std::array<Rgb, 4> decoderColors(std::uint16_t lowEncoded,
                                 std::uint16_t highEncoded) noexcept {
    std::array<Rgb, 4> colors;
    const bool blackTrick = color1555(lowEncoded, colors[0]);
    colors[3] = color565(highEncoded);
    if (blackTrick) {
        colors[2] = colors[0];
        colors[1] = {
            static_cast<std::uint8_t>(std::max(
                static_cast<int>(colors[2].red) - (colors[3].red >> 2), 0)),
            static_cast<std::uint8_t>(std::max(
                static_cast<int>(colors[2].green) - (colors[3].green >> 2),
                0)),
            static_cast<std::uint8_t>(std::max(
                static_cast<int>(colors[2].blue) - (colors[3].blue >> 2), 0)),
        };
        colors[0] = {};
    } else {
        colors[2] = {
            static_cast<std::uint8_t>((colors[3].red * 5U +
                                       colors[0].red * 3U) >>
                                      3U),
            static_cast<std::uint8_t>((colors[3].green * 5U +
                                       colors[0].green * 3U) >>
                                      3U),
            static_cast<std::uint8_t>((colors[3].blue * 5U +
                                       colors[0].blue * 3U) >>
                                      3U),
        };
        colors[1] = {
            static_cast<std::uint8_t>((colors[3].red * 3U +
                                       colors[0].red * 5U) >>
                                      3U),
            static_cast<std::uint8_t>((colors[3].green * 3U +
                                       colors[0].green * 5U) >>
                                      3U),
            static_cast<std::uint8_t>((colors[3].blue * 3U +
                                       colors[0].blue * 5U) >>
                                      3U),
        };
    }
    return colors;
}

void decodeBlock(std::span<const std::byte, 16> source,
                 std::uint32_t blockX, std::uint32_t blockY,
                 RgbaImage& output) noexcept {
    std::uint64_t alphaBits{};
    for (std::size_t index = 0; index < 8; ++index) {
        alphaBits |= static_cast<std::uint64_t>(
                         std::to_integer<std::uint8_t>(source[index]))
                     << (index * 8);
    }
    const std::uint16_t low = static_cast<std::uint16_t>(
        std::to_integer<std::uint8_t>(source[8]) |
        (std::to_integer<std::uint8_t>(source[9]) << 8U));
    const std::uint16_t high = static_cast<std::uint16_t>(
        std::to_integer<std::uint8_t>(source[10]) |
        (std::to_integer<std::uint8_t>(source[11]) << 8U));
    std::uint32_t colorIndices{};
    for (std::size_t index = 0; index < 4; ++index) {
        colorIndices |= static_cast<std::uint32_t>(
                            std::to_integer<std::uint8_t>(source[12 + index]))
                        << (index * 8);
    }
    const auto colors = decoderColors(low, high);

    for (std::uint32_t pixel = 0; pixel < 16; ++pixel) {
        const std::uint32_t x = blockX * 4 + pixel % 4;
        const std::uint32_t y = blockY * 4 + pixel / 4;
        if (x < output.width && y < output.height) {
            const Rgb color = colors[(colorIndices >> (pixel * 2)) & 3U];
            const std::uint8_t alpha = static_cast<std::uint8_t>(
                (alphaBits >> (pixel * 4)) & 0xfU);
            const std::size_t destination =
                (static_cast<std::size_t>(y) * output.width + x) * 4;
            output.pixels[destination] = color.red;
            output.pixels[destination + 1] = color.green;
            output.pixels[destination + 2] = color.blue;
            output.pixels[destination + 3] =
                static_cast<std::uint8_t>((alpha << 4U) | alpha);
        }
    }
}

} // namespace

Result DdsAtcTexture::load(std::span<const std::byte> bytes) {
    image_ = {};
    constexpr std::size_t dataOffset = 4 + kDdsHeaderSize;
    if (bytes.size() < dataOffset || !matches(bytes, 0, kDdsMagic) ||
        readU32(bytes, 4) != kDdsHeaderSize ||
        readU32(bytes, 76) != kDdsPixelFormatSize) {
        return Result::failure("DDS header is missing or invalid");
    }
    if (!matches(bytes, 84, kAtcaFourCc)) {
        return Result::failure("DDS texture is not ATC explicit-alpha (ATCA)");
    }

    const std::uint32_t height = readU32(bytes, 12);
    const std::uint32_t width = readU32(bytes, 16);
    const std::uint32_t mipCount = readU32(bytes, 28);
    if (width == 0 || height == 0 || mipCount > 1) {
        return Result::failure("Unsupported DDS dimensions or mip count");
    }
    const std::uint64_t blocksX = (static_cast<std::uint64_t>(width) + 3) / 4;
    const std::uint64_t blocksY = (static_cast<std::uint64_t>(height) + 3) / 4;
    const std::uint64_t payloadSize = blocksX * blocksY * 16;
    const std::uint64_t decodedSize =
        static_cast<std::uint64_t>(width) * height * 4;
    if (payloadSize != bytes.size() - dataOffset ||
        decodedSize > std::numeric_limits<std::size_t>::max()) {
        return Result::failure("DDS ATCA payload size does not match header");
    }

    image_.width = width;
    image_.height = height;
    image_.pixels.resize(static_cast<std::size_t>(decodedSize));
    std::size_t sourceOffset = dataOffset;
    for (std::uint32_t blockY = 0; blockY < blocksY; ++blockY) {
        for (std::uint32_t blockX = 0; blockX < blocksX; ++blockX) {
            decodeBlock(std::span<const std::byte, 16>(
                            bytes.data() + sourceOffset, 16),
                        blockX, blockY, image_);
            sourceOffset += 16;
        }
    }
    return Result::success();
}

} // namespace usm::assets
