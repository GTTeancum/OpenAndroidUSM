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

constexpr std::size_t kEnemyBehaviorTypeCount = 25;

struct EnemyBehaviorAnimationMap {
    std::int16_t id{};
    std::string name;
    std::array<std::string, kEnemyBehaviorTypeCount> animationNames;
};

struct EnemyBehaviorAnimationList {
    std::int16_t id{};
    std::string name;
    std::int32_t selectionMode{};
    std::vector<std::int32_t> animationMapIds;
};

struct EnemyBehaviorSoundMap {
    std::int16_t id{};
    std::string name;
    std::array<std::int32_t, kEnemyBehaviorTypeCount> voxSoundIds;
};

struct EnemyBehaviorStateDefinition {
    std::int16_t id{};
    std::string name;
    bool looping{};
    std::int32_t parameter0{};
    std::int32_t animationSelectionMode{};
    std::vector<std::int16_t> animationListIds;
    std::vector<std::int16_t> soundMapIds;
    bool parameter1{};
    bool stopPreviousSound{};
};

// Typed reconstruction of BehaviorStateFile's four serialized inputs. The
// layouts follow ReadAnimList (0x0033a2b0), ReadStateInfoList (0x0033a3d8),
// ReadSoundMapInfos (0x0033a868), and the 25-column animation-map loader used
// by each original enemy type.
class EnemyBehaviorConfigDatabase final {
public:
    [[nodiscard]] Result load(const std::filesystem::path& gameDataRoot);
    [[nodiscard]] Result loadAnimationMaps(std::span<const std::byte> bytes);
    [[nodiscard]] Result loadAnimationLists(std::span<const std::byte> bytes);
    [[nodiscard]] Result loadSoundMaps(std::span<const std::byte> bytes);
    [[nodiscard]] Result loadStates(std::span<const std::byte> bytes);

    [[nodiscard]] const EnemyBehaviorStateDefinition* findState(
        std::string_view name) const noexcept;
    [[nodiscard]] const EnemyBehaviorSoundMap* findSoundMap(
        std::int16_t id) const noexcept;
    [[nodiscard]] std::int32_t resolveSoundMap(
        std::int16_t soundMapId, std::int16_t enemyTypeId) const noexcept;
    [[nodiscard]] std::vector<std::int32_t> resolveStateSoundIds(
        std::string_view stateName, std::int16_t enemyTypeId) const;
    [[nodiscard]] std::vector<std::string_view> resolveStateAnimationNames(
        std::string_view stateName, std::int16_t enemyTypeId) const;

    [[nodiscard]] const std::vector<EnemyBehaviorAnimationMap>&
    animationMaps() const noexcept {
        return animationMaps_;
    }
    [[nodiscard]] const std::vector<EnemyBehaviorAnimationList>&
    animationLists() const noexcept {
        return animationLists_;
    }
    [[nodiscard]] const std::vector<EnemyBehaviorSoundMap>& soundMaps() const
        noexcept {
        return soundMaps_;
    }
    [[nodiscard]] const std::vector<EnemyBehaviorStateDefinition>& states()
        const noexcept {
        return states_;
    }

private:
    [[nodiscard]] const EnemyBehaviorAnimationMap* findAnimationMap(
        std::int32_t id) const noexcept;
    [[nodiscard]] const EnemyBehaviorAnimationList* findAnimationList(
        std::int16_t id) const noexcept;

    std::vector<EnemyBehaviorAnimationMap> animationMaps_;
    std::vector<EnemyBehaviorAnimationList> animationLists_;
    std::vector<EnemyBehaviorSoundMap> soundMaps_;
    std::vector<EnemyBehaviorStateDefinition> states_;
};

} // namespace usm::game
