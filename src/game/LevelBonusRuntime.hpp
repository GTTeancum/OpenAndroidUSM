#pragma once

#include "game/LevelOneBootstrap.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <span>
#include <vector>

namespace usm::game {

struct LevelBonusGrant {
    std::int32_t objectId{-1};
    LevelBonusType type{LevelBonusType::Health};
    std::int32_t amount{};
};

struct LevelBonusOrbRenderState {
    static constexpr std::size_t kTrailPointCount = 9;

    LevelBonusType type{LevelBonusType::Health};
    std::int32_t roomId{-1};
    std::array<assets::Vector3, kTrailPointCount> trailPositions{};
    assets::Vector3 headPosition;
    float trailHalfWidth{};
    float headHalfWidth{};
};

struct LevelBonusPopupState {
    assets::Vector3 playerPosition;
    std::int32_t amount{};
    float progress{};
    bool visible{};
};

// Observable state for one authored CBonus. Keeping collection and orb-flight
// state together lets deterministic diagnostics verify the complete pickup
// lifecycle without duplicating CBonus's rules.
struct LevelBonusState {
    const LevelBonusAsset* asset{};
    assets::Vector3 startTangent;
    assets::Vector3 endTangent;
    float progress{};
    bool visible{};
    bool orbActive{};
};

// CLevel::Save/Load (0x00388558/0x00388450) serializes the level-owned
// bonus objects as part of the checkpoint stream. Render-orb vertices and
// output queues are derived/transient and are intentionally not stored.
struct LevelBonusCheckPointState {
    std::vector<LevelBonusState> bonuses;
    std::uint32_t difficulty{};
    std::uint32_t randomState{};
    std::int32_t pendingSkillPointAmount{};
    std::uint32_t pendingSkillPointMilliseconds{};
    std::uint32_t skillPointPopupMilliseconds{};
    std::uint32_t skillPointTotalRemainingMilliseconds{};
    LevelBonusPopupState skillPointPopup;
};

// Portable CBonus/CHealthOrbs state reconstructed from CBonus::Update
// (0x00393680), CHealthOrbs::Init (0x003a190c), and OnAnimate
// (0x003a17c0). Rendering consumes only the sampled Hermite ribbon state.
class LevelBonusRuntime final {
public:
    LevelBonusRuntime();
    ~LevelBonusRuntime();
    LevelBonusRuntime(const LevelBonusRuntime&) = delete;
    LevelBonusRuntime& operator=(const LevelBonusRuntime&) = delete;

    [[nodiscard]] Result initialize(std::span<const LevelBonusAsset> assets,
                                    std::uint32_t difficulty = 0);
    void update(const assets::Vector3& playerPosition,
                std::uint32_t elapsedMilliseconds) noexcept;
    void spawnOrbs(LevelBonusType type, const assets::Vector3& position,
                   std::int32_t roomId, std::int32_t count) noexcept;

    [[nodiscard]] std::vector<std::int32_t> consumeCollectedBonusIds();
    [[nodiscard]] std::vector<LevelBonusGrant> consumeGrants();
    [[nodiscard]] std::span<const LevelBonusOrbRenderState> orbs() const
        noexcept {
        return renderOrbs_;
    }
    [[nodiscard]] std::size_t visibleBonusCount() const noexcept;
    [[nodiscard]] LevelBonusCheckPointState saveCheckPointState() const;
    [[nodiscard]] Result loadCheckPointState(
        const LevelBonusCheckPointState& state);
    [[nodiscard]] std::span<const LevelBonusState> states() const noexcept {
        return states_;
    }
    [[nodiscard]] bool showSkillPointTotal() const noexcept {
        return skillPointTotalRemainingMilliseconds_ > 0;
    }
    [[nodiscard]] const LevelBonusPopupState& skillPointPopup() const noexcept {
        return skillPointPopup_;
    }

private:
    [[nodiscard]] std::uint32_t randomBounded(
        std::uint32_t maximumExclusive) noexcept;
    [[nodiscard]] assets::Vector3 randomTangent(
        float horizontalScale, float verticalScale,
        std::int32_t elevationCenterDegrees,
        std::uint32_t elevationRangeDegrees) noexcept;
    [[nodiscard]] static assets::Vector3 hermite(
        float progress, const assets::Vector3& start,
        const assets::Vector3& end, const assets::Vector3& startTangent,
        const assets::Vector3& endTangent) noexcept;

    std::vector<LevelBonusState> states_;
    std::deque<LevelBonusAsset> spawnedAssets_;
    std::vector<LevelBonusOrbRenderState> renderOrbs_;
    std::vector<std::int32_t> collectedBonusIds_;
    std::vector<LevelBonusGrant> grants_;
    std::uint32_t difficulty_{};
    std::uint32_t randomState_{0x2f6e2b1U};
    std::int32_t pendingSkillPointAmount_{};
    std::uint32_t pendingSkillPointMilliseconds_{};
    std::uint32_t skillPointPopupMilliseconds_{};
    std::uint32_t skillPointTotalRemainingMilliseconds_{};
    LevelBonusPopupState skillPointPopup_;
    std::int32_t nextSpawnedObjectId_{-200000};
};

} // namespace usm::game
