#pragma once

#include "core/Result.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace usm::game {

// Parser for the original newline-delimited *.map key tables and offset-based
// UTF-16LE *.data language payloads consumed by CStrings.
class LocalizedStringTable final {
public:
    [[nodiscard]] Result load(std::span<const std::byte> mapBytes,
                              std::span<const std::byte> dataBytes);
    [[nodiscard]] const std::u16string* find(
        std::string_view key) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return strings_.size(); }

private:
    std::unordered_map<std::string, std::u16string> strings_;
};

class LevelTextCatalog final {
public:
    [[nodiscard]] Result load(const std::filesystem::path& gameDataRoot,
                              std::string_view language = "EN");
    [[nodiscard]] const std::u16string* findLevelString(
        std::string_view key) const noexcept {
        return level_.find(key);
    }
    [[nodiscard]] const std::u16string* findTutorialString(
        std::string_view key) const noexcept {
        return tutorial_.find(key);
    }
    [[nodiscard]] const LocalizedStringTable& level() const noexcept {
        return level_;
    }
    [[nodiscard]] const LocalizedStringTable& tutorial() const noexcept {
        return tutorial_;
    }

private:
    LocalizedStringTable level_;
    LocalizedStringTable tutorial_;
};

} // namespace usm::game
