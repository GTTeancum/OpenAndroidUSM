#pragma once

#include "core/Result.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace usm::game {

// Typed form of the 0x4c-byte QTEActionConfig payload reconstructed from
// QTEActionConfigFile::ReadBasic (0x002fc50c).  The exported record ID
// precedes this payload in QTE_ACTIONS.bin.
struct QuickTimeActionDefinition {
    std::int16_t id{-1};
    std::string name;
    std::int16_t attackDirection{};
    std::int16_t buttonConfigId{-1};
    std::string playerAnimation;
    std::int16_t playerSoundId{-1};
    std::string npcAnimation;
    std::int16_t npcSoundId{-1};
    bool loop{};
    std::int16_t successStateId{-1};
    std::int16_t failureStateId{-1};
    std::vector<std::int16_t> attackFrames;
    std::vector<std::int16_t> attackDamage;
    std::int16_t playerStartRotationDegrees{};
    std::int16_t npcStartRotationDegrees{};
    std::int16_t playerEndRotationDegrees{};
    std::int16_t npcEndRotationDegrees{};
    std::int16_t playerEndAction{-1};
    std::int16_t npcEndAction{-1};
    std::int16_t slowMotion{};
    std::int16_t slowMotionDuration{};
    std::int16_t bossHealthStealMultiplier{};
};

class QuickTimeActionConfigDatabase final {
public:
    [[nodiscard]] Result load(const std::filesystem::path& gameDataRoot);
    [[nodiscard]] Result load(std::span<const std::byte> bytes);
    [[nodiscard]] const QuickTimeActionDefinition* find(
        std::int32_t id) const noexcept;
    [[nodiscard]] const std::vector<QuickTimeActionDefinition>& definitions()
        const noexcept {
        return definitions_;
    }

private:
    std::vector<QuickTimeActionDefinition> definitions_;
};

} // namespace usm::game
