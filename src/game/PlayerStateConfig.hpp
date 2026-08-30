#pragma once

#include "core/Result.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace usm::game {

struct PlayerSoundConfig {
    std::uint16_t id{};
    std::string name;
    std::int16_t playbackType{};
    std::int16_t selectionMode{};
    std::int16_t targetMode{};
    std::int16_t parameter{};
    std::vector<std::int16_t> voxSoundIds;
    std::vector<std::int16_t> activeEmitterIds;
};

struct PlayerStateDefinition {
    std::uint16_t id{};
    std::string name;
    std::int16_t soundTriggerFrame{-1};
    std::vector<std::int16_t> enterSoundConfigIds;
    std::vector<std::int16_t> frameSoundConfigIds;
};

// Audio-relevant portion of the original StateFile data. The readers follow
// StateFile::ReadBasicState (0x0033d294) and ReadSoundConfig (0x0033d5f8).
class PlayerStateConfigDatabase final {
public:
    [[nodiscard]] Result load(const std::filesystem::path& gameDataRoot);
    [[nodiscard]] Result loadStates(std::span<const std::byte> bytes);
    [[nodiscard]] Result loadSounds(std::span<const std::byte> bytes);

    [[nodiscard]] const PlayerStateDefinition* findState(
        std::string_view name) const noexcept;
    [[nodiscard]] const PlayerSoundConfig* findSoundConfig(
        std::int16_t id) const noexcept;
    [[nodiscard]] const PlayerSoundConfig* findSoundConfig(
        std::string_view name) const noexcept;

    [[nodiscard]] const std::vector<PlayerStateDefinition>& states() const
        noexcept {
        return states_;
    }
    [[nodiscard]] const std::vector<PlayerSoundConfig>& soundConfigs() const
        noexcept {
        return soundConfigs_;
    }

private:
    std::vector<PlayerStateDefinition> states_;
    std::vector<PlayerSoundConfig> soundConfigs_;
};

} // namespace usm::game
