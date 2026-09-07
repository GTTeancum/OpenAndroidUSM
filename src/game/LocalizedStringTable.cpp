#include "game/LocalizedStringTable.hpp"

#include "filesystem/GbmpArchive.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace usm::game {
namespace {

std::uint32_t readU32(std::span<const std::byte> bytes,
                      std::size_t offset) noexcept {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(
                     std::to_integer<unsigned char>(bytes[offset + index]))
                 << (index * 8);
    }
    return value;
}

std::vector<std::string> parseKeys(std::span<const std::byte> bytes) {
    const std::string text(reinterpret_cast<const char*>(bytes.data()),
                           bytes.size());
    std::vector<std::string> keys;
    std::size_t begin = 0;
    while (begin < text.size()) {
        std::size_t end = text.find('\n', begin);
        if (end == std::string::npos) {
            end = text.size();
        }
        std::size_t length = end - begin;
        if (length != 0 && text[begin + length - 1] == '\r') {
            --length;
        }
        if (length != 0) {
            keys.emplace_back(text.substr(begin, length));
        }
        begin = end + 1;
    }
    return keys;
}

} // namespace

Result LocalizedStringTable::load(std::span<const std::byte> mapBytes,
                                  std::span<const std::byte> dataBytes) {
    strings_.clear();
    orderedStrings_.clear();
    if (dataBytes.size() < 4) {
        return Result::failure("Localized string data has no header");
    }
    const std::uint32_t count = readU32(dataBytes, 0);
    if (count > 65536 || dataBytes.size() < 4U + count * 4U) {
        return Result::failure("Localized string data has invalid offsets");
    }
    const std::vector<std::string> keys = parseKeys(mapBytes);
    if (keys.size() < count) {
        return Result::failure("Localized string map has too few keys");
    }
    const std::size_t payloadOffset = 4U + count * 4U;
    strings_.reserve(count);
    orderedStrings_.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        const std::uint32_t relativeOffset =
            readU32(dataBytes, 4U + index * 4U);
        if (relativeOffset > dataBytes.size() - payloadOffset) {
            strings_.clear();
            return Result::failure(
                "Localized string offset is outside the payload");
        }
        std::size_t cursor = payloadOffset + relativeOffset;
        std::u16string value;
        while (cursor + 1 < dataBytes.size()) {
            const std::uint16_t character =
                static_cast<std::uint16_t>(
                    std::to_integer<unsigned char>(dataBytes[cursor])) |
                static_cast<std::uint16_t>(
                    std::to_integer<unsigned char>(dataBytes[cursor + 1]))
                    << 8;
            cursor += 2;
            if (character == 0) {
                break;
            }
            value.push_back(static_cast<char16_t>(character));
        }
        if (cursor > dataBytes.size() ||
            (cursor == dataBytes.size() &&
             (dataBytes.size() < 2 ||
              std::to_integer<unsigned char>(dataBytes[cursor - 1]) != 0 ||
              std::to_integer<unsigned char>(dataBytes[cursor - 2]) != 0))) {
            strings_.clear();
            return Result::failure("Localized string is not terminated");
        }
        orderedStrings_.push_back(value);
        strings_.emplace(keys[index], std::move(value));
    }
    return Result::success();
}

const std::u16string* LocalizedStringTable::find(
    std::string_view key) const noexcept {
    const auto match = strings_.find(std::string(key));
    return match == strings_.end() ? nullptr : &match->second;
}

Result LevelTextCatalog::load(const std::filesystem::path& gameDataRoot,
                              std::string_view language,
                              std::string_view levelTable) {
    filesystem::GbmpArchive strings;
    Result result = strings.open(gameDataRoot / "xlsStrings.pack");
    if (!result) {
        return result;
    }
    const auto loadTable = [&strings, language](std::string_view tableName,
                                                LocalizedStringTable& table) {
        std::vector<std::byte> mapBytes;
        std::vector<std::byte> dataBytes;
        Result tableResult = strings.read(
            std::string(tableName) + ".map", mapBytes);
        if (!tableResult) {
            return tableResult;
        }
        tableResult = strings.read(std::string(tableName) + "_" +
                                       std::string(language) + ".data",
                                   dataBytes);
        return tableResult ? table.load(mapBytes, dataBytes) : tableResult;
    };
    result = loadTable(levelTable, level_);
    if (!result) {
        return Result::failure("Could not load " + std::string(levelTable) +
                               " strings: " +
                               result.message());
    }
    result = loadTable("Tutorial", tutorial_);
    if (!result) {
        return Result::failure("Could not load tutorial strings: " +
                               result.message());
    }
    result = loadTable("Main", main_);
    return result ? result
                  : Result::failure("Could not load main strings: " +
                                    result.message());
}

} // namespace usm::game
