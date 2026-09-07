#include "assets/TgaTexture.hpp"

#include <cstdint>
#include <limits>

namespace usm::assets {
namespace {

std::uint16_t readU16(std::span<const std::byte> bytes,
                      std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(
        std::to_integer<std::uint8_t>(bytes[offset]) |
        (std::to_integer<std::uint8_t>(bytes[offset + 1]) << 8U));
}

} // namespace

Result TgaTexture::load(std::span<const std::byte> bytes) {
    image_ = {};
    constexpr std::size_t headerSize = 18;
    if (bytes.size() < headerSize) {
        return Result::failure("TGA header is truncated");
    }
    const std::uint8_t idLength =
        std::to_integer<std::uint8_t>(bytes[0]);
    const std::uint8_t colorMapType =
        std::to_integer<std::uint8_t>(bytes[1]);
    const std::uint8_t imageType =
        std::to_integer<std::uint8_t>(bytes[2]);
    const std::uint16_t width = readU16(bytes, 12);
    const std::uint16_t height = readU16(bytes, 14);
    const std::uint8_t pixelDepth =
        std::to_integer<std::uint8_t>(bytes[16]);
    const std::uint8_t descriptor =
        std::to_integer<std::uint8_t>(bytes[17]);
    if (colorMapType != 0 || imageType != 2 || width == 0 || height == 0 ||
        (pixelDepth != 24 && pixelDepth != 32)) {
        return Result::failure(
            "TGA is not uncompressed 24/32-bit true color");
    }
    const std::size_t bytesPerPixel = pixelDepth / 8U;
    const std::size_t dataOffset = headerSize + idLength;
    const std::uint64_t pixelCount =
        static_cast<std::uint64_t>(width) * height;
    const std::uint64_t sourceSize = pixelCount * bytesPerPixel;
    const std::uint64_t decodedSize = pixelCount * 4U;
    const std::uint64_t payloadEnd = dataOffset + sourceSize;
    const std::uint64_t trailingSize =
        payloadEnd <= bytes.size() ? bytes.size() - payloadEnd : 0;
    // TGA 2.0 appends a 26-byte footer after the pixel payload. The shipped
    // bg_suit.tga has that exact footer and no extension/developer blocks.
    constexpr std::size_t footerSize = 26;
    constexpr char footerSignature[] = "TRUEVISION-XFILE.";
    bool validFooter = trailingSize == 0;
    if (trailingSize == footerSize) {
        validFooter = true;
        constexpr std::size_t signatureOffset = 8;
        for (std::size_t index = 0;
             index + 1 < sizeof(footerSignature); ++index) {
            validFooter =
                validFooter &&
                std::to_integer<std::uint8_t>(
                    bytes[static_cast<std::size_t>(payloadEnd) +
                          signatureOffset + index]) ==
                    static_cast<std::uint8_t>(footerSignature[index]);
        }
    }
    if (dataOffset > bytes.size() || sourceSize > bytes.size() - dataOffset ||
        !validFooter ||
        decodedSize > std::numeric_limits<std::size_t>::max()) {
        return Result::failure("TGA payload size does not match header");
    }

    image_.width = width;
    image_.height = height;
    image_.pixels.resize(static_cast<std::size_t>(decodedSize));
    const bool rightOrigin = (descriptor & 0x10U) != 0;
    const bool topOrigin = (descriptor & 0x20U) != 0;
    for (std::uint32_t sourceY = 0; sourceY < height; ++sourceY) {
        for (std::uint32_t sourceX = 0; sourceX < width; ++sourceX) {
            const std::uint32_t destinationX =
                rightOrigin ? width - 1U - sourceX : sourceX;
            const std::uint32_t destinationY =
                topOrigin ? sourceY : height - 1U - sourceY;
            const std::size_t source =
                dataOffset +
                (static_cast<std::size_t>(sourceY) * width + sourceX) *
                    bytesPerPixel;
            const std::size_t destination =
                (static_cast<std::size_t>(destinationY) * width +
                 destinationX) *
                4U;
            image_.pixels[destination] =
                std::to_integer<std::uint8_t>(bytes[source + 2]);
            image_.pixels[destination + 1] =
                std::to_integer<std::uint8_t>(bytes[source + 1]);
            image_.pixels[destination + 2] =
                std::to_integer<std::uint8_t>(bytes[source]);
            image_.pixels[destination + 3] =
                bytesPerPixel == 4
                    ? std::to_integer<std::uint8_t>(bytes[source + 3])
                    : 0xffU;
        }
    }
    return Result::success();
}

} // namespace usm::assets
