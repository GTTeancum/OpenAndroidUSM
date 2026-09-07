#pragma once

#include "core/Result.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace usm::game {

// Parser for the original newline-delimited *.map key tables and offset-based
// UTF-16LE *.data language payloads consumed by CStrings.
class LocalizedStringTable final {
public:
    [[nodiscard]] Result load(std::span<const std::byte> mapBytes,
                              std::span<const std::byte> dataBytes);
    [[nodiscard]] const std::u16string* find(
        std::string_view key) const noexcept;
    [[nodiscard]] const std::u16string* at(std::size_t index) const noexcept {
        return index < orderedStrings_.size() ? &orderedStrings_[index]
                                              : nullptr;
    }
    [[nodiscard]] std::size_t size() const noexcept { return strings_.size(); }

private:
    std::unordered_map<std::string, std::u16string> strings_;
    std::vector<std::u16string> orderedStrings_;
};

class LevelTextCatalog final {
public:
    [[nodiscard]] Result load(const std::filesystem::path& gameDataRoot,
                              std::string_view language = "EN",
                              std::string_view levelTable = "levelnew_01");
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
    [[nodiscard]] const LocalizedStringTable& main() const noexcept {
        return main_;
    }

private:
    LocalizedStringTable level_;
    LocalizedStringTable tutorial_;
    LocalizedStringTable main_;
};

} // namespace usm::game
