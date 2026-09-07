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

// Exact StateMCHitEffect payload read by MCHitEffectFile::ReadBasicState
// (0x0033cacc). MC_STATE auxiliaryIdLists[1] indexes this table.
struct PlayerHitEffectDefinition {
    std::uint16_t id{};
    std::string name;
    std::string meshFile;
    std::string boneName;
    // CAnimObjEffect::Init (0x00390bb8): true copies the bone's absolute
    // transform once; false parents the effect node to the live bone.
    bool snapshotBoneTransform{};
    std::int16_t renderingParameter{-1};
    float lifetimeMilliseconds{};
};

class PlayerHitEffectConfigDatabase final {
public:
    [[nodiscard]] Result load(const std::filesystem::path& gameDataRoot);
    [[nodiscard]] Result load(std::span<const std::byte> bytes);

    [[nodiscard]] const PlayerHitEffectDefinition* find(
        std::int16_t id) const noexcept;
    [[nodiscard]] const std::vector<PlayerHitEffectDefinition>& definitions()
        const noexcept {
        return definitions_;
    }

private:
    std::vector<PlayerHitEffectDefinition> definitions_;
};

} // namespace usm::game
