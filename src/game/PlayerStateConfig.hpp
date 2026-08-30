#pragma once

#include "core/Result.hpp"

#include <array>
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
    // StateBasic fields recovered from StateFile::ReadBasicState
    // (0x0033d294). Player::SetNextStateId (0x003491d0) dispatches on
    // motionType and selects primaryAnimationId/animationIds.
    std::int16_t stateClass{};
    std::int16_t motionType{};
    std::array<float, 4> motionParameters{};
    std::int16_t soundTriggerFrame{-1};
    std::array<std::int16_t, 4> auxiliaryParameters{};
    std::int32_t primaryAnimationId{-1};
    std::vector<std::int16_t> animationIds;
    bool animationListFlag{};
    std::array<std::vector<std::int16_t>, 2> auxiliaryIdLists;
    std::vector<std::int16_t> enterSoundConfigIds;
    std::vector<std::int16_t> frameSoundConfigIds;
    std::array<float, 2> timingParameters{};
    std::int16_t nextStateId{-1};
    std::array<std::vector<std::int16_t>, 3> transitionFields;
};

// Portable view of the original StateFile data. The readers follow
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
