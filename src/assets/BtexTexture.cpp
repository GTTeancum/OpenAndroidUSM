#include "assets/BtexTexture.hpp"

#include <PVRTDecompress.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace usm::assets {
namespace {

constexpr std::array<char, 8> kBtexPvrSignature{
    'B', 'T', 'E', 'X', 'p', 'v', 'r', '\0'};
constexpr std::array<char, 4> kPvrTag{'P', 'V', 'R', '!'};
constexpr std::uint32_t kPvrV2HeaderSize = 52;
constexpr std::uint32_t kPvrtc2 = 0x18;
constexpr std::uint32_t kPvrtc4 = 0x19;
constexpr std::uint32_t kPixelTypeMask = 0xff;
constexpr std::uint32_t kAlphaFlag = 0x8000;

std::uint32_t readU32(std::span<const std::byte> bytes,
                      std::size_t offset) noexcept {
    std::uint32_t result{};
    for (std::size_t index = 0; index < sizeof(result); ++index) {
        result |= static_cast<std::uint32_t>(
                      std::to_integer<unsigned char>(bytes[offset + index]))
                  << (index * 8);
    }
    return result;
}

bool matches(std::span<const std::byte> bytes, std::size_t offset,
             std::span<const char> expected) {
    if (offset > bytes.size() || expected.size() > bytes.size() - offset) {
        return false;
    }
    return std::equal(expected.begin(), expected.end(), bytes.begin() + offset,
                      [](char left, std::byte right) {
                          return static_cast<unsigned char>(left) ==
                                 std::to_integer<unsigned char>(right);
                      });
}

std::uint64_t compressedLevelSize(std::uint32_t width, std::uint32_t height,
                                  std::uint32_t bitsPerPixel) {
    const std::uint32_t blockWidth = bitsPerPixel == 2 ? 8 : 4;
    const std::uint32_t minimumBlocks = 2;
    const std::uint64_t blocksX = std::max(width / blockWidth, minimumBlocks);
    const std::uint64_t blocksY = std::max(height / 4, minimumBlocks);
    return blocksX * blocksY * bitsPerPixel * blockWidth * 4 / 8;
}

} // namespace

Result BtexTexture::load(std::span<const std::byte> bytes) {
    mipLevels_.clear();
    containsAlpha_ = false;

    constexpr std::size_t wrapperSize = kBtexPvrSignature.size();
    if (bytes.size() < wrapperSize + kPvrV2HeaderSize ||
        !matches(bytes, 0, kBtexPvrSignature)) {
        return Result::failure("BTEX/PVR wrapper signature is missing");
    }

    const std::span<const std::byte> pvr = bytes.subspan(wrapperSize);
    const std::uint32_t headerSize = readU32(pvr, 0);
    const std::uint32_t height = readU32(pvr, 4);
    const std::uint32_t width = readU32(pvr, 8);
    const std::uint32_t additionalMipCount = readU32(pvr, 12);
    const std::uint32_t flags = readU32(pvr, 16);
    const std::uint32_t dataSize = readU32(pvr, 20);
    const std::uint32_t bitsPerPixel = readU32(pvr, 24);
    const std::uint32_t surfaceCount = readU32(pvr, 48);

    if (headerSize != kPvrV2HeaderSize || !matches(pvr, 44, kPvrTag)) {
        return Result::failure("BTEX contains an invalid PVR v2 header");
    }
    if (width == 0 || height == 0 || surfaceCount != 1) {
        return Result::failure("Unsupported BTEX dimensions or surface count");
    }
    const std::uint32_t pixelType = flags & kPixelTypeMask;
    if ((pixelType != kPvrtc2 && pixelType != kPvrtc4) ||
        (bitsPerPixel != 2 && bitsPerPixel != 4)) {
        return Result::failure("Unsupported BTEX/PVR pixel format");
    }
    if (dataSize != pvr.size() - kPvrV2HeaderSize) {
        return Result::failure("BTEX/PVR data-size field does not match payload");
    }

    containsAlpha_ = (flags & kAlphaFlag) != 0;
    std::size_t sourceOffset = kPvrV2HeaderSize;
    std::uint32_t mipWidth = width;
    std::uint32_t mipHeight = height;
    mipLevels_.reserve(static_cast<std::size_t>(additionalMipCount) + 1);
    for (std::uint32_t level = 0; level <= additionalMipCount; ++level) {
        const std::uint64_t compressedSize =
            compressedLevelSize(mipWidth, mipHeight, bitsPerPixel);
        const std::uint64_t decodedSize =
            static_cast<std::uint64_t>(mipWidth) * mipHeight * 4;
        if (compressedSize > pvr.size() - sourceOffset ||
            decodedSize > std::numeric_limits<std::size_t>::max()) {
            mipLevels_.clear();
            return Result::failure("BTEX mip level exceeds payload bounds");
        }

        RgbaImage image;
        image.width = mipWidth;
        image.height = mipHeight;
        image.pixels.resize(static_cast<std::size_t>(decodedSize));
        pvr::PVRTDecompressPVRTC(
            pvr.data() + sourceOffset, bitsPerPixel == 2 ? 1U : 0U, mipWidth,
            mipHeight, image.pixels.data());
        if (!containsAlpha_) {
            for (std::size_t alpha = 3; alpha < image.pixels.size(); alpha += 4) {
                image.pixels[alpha] = 0xff;
            }
        }
        mipLevels_.push_back(std::move(image));

        sourceOffset += static_cast<std::size_t>(compressedSize);
        mipWidth = std::max(1U, mipWidth / 2);
        mipHeight = std::max(1U, mipHeight / 2);
    }

    if (sourceOffset != pvr.size()) {
        return Result::failure("BTEX/PVR contains unexpected trailing image data");
    }
    return Result::success();
}

} // namespace usm::assets
