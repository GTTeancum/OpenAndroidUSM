#pragma once

#include "core/Result.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace usm::game {

// Typed form of the 0x28-byte ButtonConfig payload read by
// ButtonConfigFile::ReadBasic (0x002fb8d4). The exported record ID precedes
// this payload in BCONFIG.bin.
struct ButtonConfigDefinition {
    std::int16_t id{-1};
    std::string name;
    std::int16_t interactionType{};
    std::int16_t interactionValue{};
    float screenX{};
    float screenY{};
    float durationMilliseconds{};
    std::int32_t normalAnimationId{-1};
    std::int32_t activeAnimationId{-1};
    std::int16_t requiredActionCount{};
    std::vector<std::int16_t> sequence;
};

class ButtonConfigDatabase final {
public:
    [[nodiscard]] Result load(const std::filesystem::path& gameDataRoot);
    [[nodiscard]] Result load(std::span<const std::byte> bytes);
    [[nodiscard]] const ButtonConfigDefinition* find(
        std::int32_t id) const noexcept;
    [[nodiscard]] const std::vector<ButtonConfigDefinition>& definitions()
        const noexcept {
        return definitions_;
    }

private:
    std::vector<ButtonConfigDefinition> definitions_;
};

} // namespace usm::game
