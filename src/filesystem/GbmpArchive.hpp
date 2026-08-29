#pragma once

#include "core/Result.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace usm::filesystem {

struct GbmpArchiveEntry {
    std::string path;
    std::uint64_t dataOffset{};
    std::uint32_t compressedSize{};
    std::uint32_t uncompressedSize{};
    std::uint32_t crc32{};
    std::uint16_t compressionMethod{};
};

// Native reader for Gameloft's GBMP archive variant. Reconstructed from
// irr::io::CZipReader::scanLocalHeader at original address 0x00429124
// (Ghidra image address 0x00439124).
class GbmpArchive final {
public:
    [[nodiscard]] Result open(const std::filesystem::path& archivePath);
    [[nodiscard]] Result read(std::string_view resourcePath,
                              std::vector<std::byte>& output);

    [[nodiscard]] const std::vector<GbmpArchiveEntry>& entries() const noexcept {
        return entries_;
    }
    [[nodiscard]] const GbmpArchiveEntry* find(
        std::string_view resourcePath) const noexcept;

private:
    static std::string normalizePath(std::string_view path);
    [[nodiscard]] Result readEntry(const GbmpArchiveEntry& entry,
                                   std::vector<std::byte>& output);

    std::filesystem::path archivePath_;
    std::ifstream stream_;
    std::vector<GbmpArchiveEntry> entries_;
};

} // namespace usm::filesystem
