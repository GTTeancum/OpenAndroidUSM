#include "assets/BresFile.hpp"

#include <algorithm>
#include <array>

namespace usm::assets {
namespace {

constexpr std::uint32_t kBresSignature = 0x53455242;
constexpr std::uint16_t kLittleEndianMarker = 0xfffe;
constexpr std::size_t kSerializedHeaderSize = 32;
constexpr std::uint16_t kRuntimeInitializedFlag = 0x8000;

template <typename Integer>
Integer readLittleEndian(std::span<const std::byte> bytes,
                         std::size_t offset) noexcept {
    Integer result{};
    for (std::size_t index = 0; index < sizeof(Integer); ++index) {
        result |= static_cast<Integer>(std::to_integer<unsigned char>(
                      bytes[offset + index]))
                  << (index * 8);
    }
    return result;
}

} // namespace

Result BresFile::load(std::span<const std::byte> bytes) {
    bytes_.clear();
    relocations_.clear();
    header_ = {};

    if (bytes.size() < kSerializedHeaderSize) {
        return Result::failure("BRES file is smaller than its header");
    }
    if (readLittleEndian<std::uint32_t>(bytes, 0) != kBresSignature) {
        return Result::failure("BRES signature is missing");
    }

    header_.byteOrderMarker = readLittleEndian<std::uint16_t>(bytes, 4);
    header_.runtimeFlags = readLittleEndian<std::uint16_t>(bytes, 6);
    header_.headerSize = readLittleEndian<std::uint32_t>(bytes, 8);
    header_.fileSize = readLittleEndian<std::uint32_t>(bytes, 12);
    header_.relocationCount = readLittleEndian<std::uint32_t>(bytes, 16);
    header_.relocationTableOffset = readLittleEndian<std::uint32_t>(bytes, 20);

    if (header_.byteOrderMarker != kLittleEndianMarker) {
        return Result::failure("Unsupported BRES byte order");
    }
    if ((header_.runtimeFlags & kRuntimeInitializedFlag) != 0) {
        return Result::failure("BRES contains runtime-relocated pointers");
    }
    if (header_.headerSize != kSerializedHeaderSize) {
        return Result::failure("Unexpected BRES header size");
    }
    if (header_.fileSize != bytes.size()) {
        return Result::failure("BRES file-size field does not match payload");
    }

    const std::uint64_t relocationBytes =
        static_cast<std::uint64_t>(header_.relocationCount) * sizeof(std::uint32_t);
    if (header_.relocationTableOffset > bytes.size() ||
        relocationBytes > bytes.size() - header_.relocationTableOffset) {
        return Result::failure("BRES relocation table exceeds file bounds");
    }

    bytes_.assign(bytes.begin(), bytes.end());
    const std::span<const std::byte> ownedBytes(bytes_);
    relocations_.reserve(header_.relocationCount);
    for (std::uint32_t index = 0; index < header_.relocationCount; ++index) {
        const std::size_t tableEntryOffset =
            header_.relocationTableOffset + index * sizeof(std::uint32_t);
        const std::uint32_t pointerFieldOffset =
            readLittleEndian<std::uint32_t>(ownedBytes, tableEntryOffset);
        if (pointerFieldOffset > bytes_.size() - sizeof(std::uint32_t)) {
            return Result::failure("BRES pointer field exceeds file bounds");
        }
        const std::uint32_t targetOffset =
            readLittleEndian<std::uint32_t>(ownedBytes, pointerFieldOffset);
        if (targetOffset >= bytes_.size()) {
            return Result::failure("BRES pointer target exceeds file bounds");
        }
        relocations_.push_back({pointerFieldOffset, targetOffset});
    }

    std::sort(relocations_.begin(), relocations_.end(),
              [](const BresRelocation& left, const BresRelocation& right) {
                  return left.pointerFieldOffset < right.pointerFieldOffset;
              });
    return Result::success();
}

std::optional<std::uint32_t> BresFile::resolvePointer(
    std::uint32_t pointerFieldOffset) const noexcept {
    const auto match = std::lower_bound(
        relocations_.begin(), relocations_.end(), pointerFieldOffset,
        [](const BresRelocation& relocation, std::uint32_t offset) {
            return relocation.pointerFieldOffset < offset;
        });
    if (match == relocations_.end() ||
        match->pointerFieldOffset != pointerFieldOffset) {
        return std::nullopt;
    }
    return match->targetOffset;
}

} // namespace usm::assets
