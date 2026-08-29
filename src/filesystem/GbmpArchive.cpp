#include "filesystem/GbmpArchive.hpp"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <span>

namespace usm::filesystem {
namespace {

constexpr std::uint32_t kGbmpLocalHeaderSignature = 0x504d4247;
constexpr std::uint32_t kZipLocalHeaderSignature = 0x04034b50;
constexpr std::uint32_t kZipCentralDirectorySignature = 0x02014b50;
constexpr std::uint32_t kZipEndOfDirectorySignature = 0x06054b50;
constexpr std::size_t kLocalHeaderSize = 30;
constexpr std::uint16_t kStoredCompression = 0;
constexpr std::uint16_t kDeflateCompression = 8;
constexpr std::uint16_t kEncryptedFlag = 1;
constexpr std::uint16_t kDataDescriptorFlag = 8;

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

Result streamFailure(std::string_view operation) {
    return Result::failure(std::string(operation) +
                           " failed while reading GBMP archive");
}

} // namespace

Result GbmpArchive::open(const std::filesystem::path& archivePath) {
    entries_.clear();
    stream_.close();
    stream_.clear();
    archivePath_ = archivePath;
    stream_.open(archivePath_, std::ios::binary);
    if (!stream_) {
        return Result::failure("Could not open GBMP archive: " +
                               archivePath_.string());
    }

    stream_.seekg(0, std::ios::end);
    const auto endPosition = stream_.tellg();
    if (endPosition < 0) {
        return streamFailure("Determining archive size");
    }
    const auto archiveSize = static_cast<std::uint64_t>(endPosition);
    stream_.seekg(0, std::ios::beg);

    while (static_cast<std::uint64_t>(stream_.tellg()) + kLocalHeaderSize <=
           archiveSize) {
        const auto headerOffset = static_cast<std::uint64_t>(stream_.tellg());
        std::array<std::byte, kLocalHeaderSize> header{};
        stream_.read(reinterpret_cast<char*>(header.data()),
                     static_cast<std::streamsize>(header.size()));
        if (!stream_) {
            return streamFailure("Reading local header");
        }

        const std::uint32_t signature =
            readLittleEndian<std::uint32_t>(header, 0);
        if (signature == kZipCentralDirectorySignature ||
            signature == kZipEndOfDirectorySignature) {
            break;
        }
        if (signature != kGbmpLocalHeaderSignature &&
            signature != kZipLocalHeaderSignature) {
            return Result::failure("Invalid GBMP local header at byte " +
                                   std::to_string(headerOffset));
        }

        const std::uint16_t flags = readLittleEndian<std::uint16_t>(header, 6);
        const std::uint16_t method = readLittleEndian<std::uint16_t>(header, 8);
        const std::uint32_t checksum =
            readLittleEndian<std::uint32_t>(header, 14);
        const std::uint32_t compressedSize =
            readLittleEndian<std::uint32_t>(header, 18);
        const std::uint32_t uncompressedSize =
            readLittleEndian<std::uint32_t>(header, 22);
        const std::uint16_t pathLength =
            readLittleEndian<std::uint16_t>(header, 26);
        const std::uint16_t extraLength =
            readLittleEndian<std::uint16_t>(header, 28);

        if ((flags & kEncryptedFlag) != 0) {
            return Result::failure("Encrypted GBMP entries are unsupported");
        }
        if ((flags & kDataDescriptorFlag) != 0) {
            return Result::failure("GBMP data descriptors are unsupported");
        }
        if (method != kStoredCompression && method != kDeflateCompression) {
            return Result::failure("Unsupported GBMP compression method: " +
                                   std::to_string(method));
        }

        const std::uint64_t dataOffset =
            headerOffset + kLocalHeaderSize + pathLength + extraLength;
        if (dataOffset > archiveSize || compressedSize > archiveSize - dataOffset) {
            return Result::failure("GBMP entry exceeds archive bounds");
        }

        std::string path(pathLength, '\0');
        stream_.read(path.data(), static_cast<std::streamsize>(path.size()));
        if (!stream_) {
            return streamFailure("Reading entry path");
        }
        stream_.seekg(extraLength, std::ios::cur);
        if (!stream_) {
            return streamFailure("Skipping entry metadata");
        }

        entries_.push_back({normalizePath(path), dataOffset, compressedSize,
                            uncompressedSize, checksum, method});
        stream_.seekg(static_cast<std::streamoff>(dataOffset + compressedSize),
                      std::ios::beg);
    }

    if (entries_.empty()) {
        return Result::failure("GBMP archive contains no local entries");
    }
    return Result::success();
}

Result GbmpArchive::read(std::string_view resourcePath,
                         std::vector<std::byte>& output) {
    const GbmpArchiveEntry* entry = find(resourcePath);
    if (entry == nullptr) {
        return Result::failure("Resource not found in GBMP archive: " +
                               std::string(resourcePath));
    }
    return readEntry(*entry, output);
}

const GbmpArchiveEntry* GbmpArchive::find(
    std::string_view resourcePath) const noexcept {
    const std::string normalized = normalizePath(resourcePath);
    const auto match = std::find_if(
        entries_.begin(), entries_.end(), [&normalized](const auto& entry) {
            return entry.path == normalized;
        });
    return match == entries_.end() ? nullptr : &*match;
}

std::string GbmpArchive::normalizePath(std::string_view path) {
    std::string normalized(path);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char character) {
                       if (character == '\\') {
                           return '/';
                       }
                       return static_cast<char>(std::tolower(character));
                   });
    return normalized;
}

Result GbmpArchive::readEntry(const GbmpArchiveEntry& entry,
                              std::vector<std::byte>& output) {
    if (!stream_.is_open()) {
        return Result::failure("GBMP archive is not open");
    }

    std::vector<std::byte> compressed(entry.compressedSize);
    stream_.clear();
    stream_.seekg(static_cast<std::streamoff>(entry.dataOffset), std::ios::beg);
    stream_.read(reinterpret_cast<char*>(compressed.data()),
                 static_cast<std::streamsize>(compressed.size()));
    if (!stream_) {
        return streamFailure("Reading entry payload");
    }

    if (entry.compressionMethod == kStoredCompression) {
        output = std::move(compressed);
    } else {
        output.assign(entry.uncompressedSize, std::byte{});
        z_stream inflater{};
        inflater.next_in = reinterpret_cast<Bytef*>(compressed.data());
        inflater.avail_in = static_cast<uInt>(compressed.size());
        inflater.next_out = reinterpret_cast<Bytef*>(output.data());
        inflater.avail_out = static_cast<uInt>(output.size());

        if (inflateInit2(&inflater, -MAX_WBITS) != Z_OK) {
            return Result::failure("Could not initialize raw Deflate decoder");
        }
        const int inflateResult = inflate(&inflater, Z_FINISH);
        inflateEnd(&inflater);
        if (inflateResult != Z_STREAM_END ||
            inflater.total_out != entry.uncompressedSize) {
            return Result::failure("Deflate failed for GBMP resource: " +
                                   entry.path);
        }
    }

    const auto checksum = ::crc32(
        0, reinterpret_cast<const Bytef*>(output.data()),
        static_cast<uInt>(output.size()));
    if (checksum != entry.crc32) {
        output.clear();
        return Result::failure("CRC mismatch for GBMP resource: " + entry.path);
    }
    return Result::success();
}

} // namespace usm::filesystem
