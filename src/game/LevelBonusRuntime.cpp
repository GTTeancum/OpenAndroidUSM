#include "game/LevelBonusRuntime.hpp"

#include <algorithm>
#include <cmath>

namespace usm::game {
namespace {

constexpr float kCollectionRadiusSquared = 22500.0F;
constexpr float kPlayerTargetHeight = 100.0F;
constexpr float kProgressPerMillisecond = 0.0006F / 1.3F;
constexpr float kTrailProgressLength = 0.077F;
constexpr float kCompletionProgress = 1.02F;
constexpr float kTrailWidth = 110.0F;
constexpr float kHeadWidthScale = 0.4F;
constexpr float kDegreesToRadians = 0.017453292519943295F;
constexpr std::array<std::int32_t, 4> kHealthByDifficulty{80, 50, 35, 20};

float distanceSquared(const assets::Vector3& first,
                      const assets::Vector3& second) noexcept {
    const float x = first.x - second.x;
    const float y = first.y - second.y;
    const float z = first.z - second.z;
    return x * x + y * y + z * z;
}

} // namespace

LevelBonusRuntime::LevelBonusRuntime() = default;
LevelBonusRuntime::~LevelBonusRuntime() = default;

Result LevelBonusRuntime::initialize(std::span<const LevelBonusAsset> assets,
                                     std::uint32_t difficulty) {
    if (difficulty >= kHealthByDifficulty.size()) {
        return Result::failure("Level bonus difficulty is invalid");
    }
    states_.clear();
    spawnedAssets_.clear();
    states_.reserve(assets.size());
    for (const LevelBonusAsset& asset : assets) {
        if (asset.objectId < 0 || asset.roomId < 1) {
            states_.clear();
            return Result::failure("Level bonus asset is invalid");
        }
        states_.push_back({&asset, {}, {}, 0.0F, asset.visible, false});
    }
    renderOrbs_.clear();
    collectedBonusIds_.clear();
    grants_.clear();
    difficulty_ = difficulty;
    randomState_ = 0x2f6e2b1U;
    pendingSkillPointAmount_ = 0;
    pendingSkillPointMilliseconds_ = 0;
    skillPointPopupMilliseconds_ = 0;
    skillPointTotalRemainingMilliseconds_ = 0;
    skillPointPopup_ = {};
    nextSpawnedObjectId_ = -200000;
    // CLevel permits levels without Bonus nodes (notably Levels 9 and 10).
    // An empty manager is a valid inert runtime, not a bootstrap failure.
    return Result::success();
}

void LevelBonusRuntime::spawnOrbs(LevelBonusType type,
                                  const assets::Vector3& position,
                                  std::int32_t roomId,
                                  std::int32_t count) noexcept {
    for (std::int32_t index = 0; index < std::max(0, count); ++index) {
        LevelBonusAsset asset;
        asset.objectId = nextSpawnedObjectId_--;
        asset.type = type;
        asset.roomId = roomId;
        asset.position = position;
        asset.visible = false;
        spawnedAssets_.push_back(std::move(asset));
        LevelBonusState state;
        state.asset = &spawnedAssets_.back();
        state.startTangent = randomTangent(3000.0F, 2000.0F, 30, 60);
        state.endTangent = randomTangent(-4000.0F, -3000.0F, 120, 240);
        state.visible = false;
        state.orbActive = true;
        states_.push_back(state);
    }
}

void LevelBonusRuntime::update(const assets::Vector3& playerPosition,
                               std::uint32_t elapsedMilliseconds) noexcept {
    renderOrbs_.clear();
    if (skillPointTotalRemainingMilliseconds_ > elapsedMilliseconds) {
        skillPointTotalRemainingMilliseconds_ -= elapsedMilliseconds;
    } else {
        skillPointTotalRemainingMilliseconds_ = 0;
    }
    if (skillPointPopup_.visible) {
        skillPointPopupMilliseconds_ = std::min(
            2000U, skillPointPopupMilliseconds_ + elapsedMilliseconds);
        skillPointPopup_.playerPosition = playerPosition;
        skillPointPopup_.progress =
            static_cast<float>(skillPointPopupMilliseconds_) / 2000.0F;
        if (skillPointPopupMilliseconds_ >= 2000U) {
            skillPointPopup_ = {};
        }
    } else if (pendingSkillPointAmount_ > 0) {
        pendingSkillPointMilliseconds_ = std::min(
            1000U, pendingSkillPointMilliseconds_ + elapsedMilliseconds);
        if (pendingSkillPointMilliseconds_ >= 1000U) {
            skillPointPopup_ =
                {playerPosition, pendingSkillPointAmount_, 0.0F, true};
            pendingSkillPointAmount_ = 0;
            pendingSkillPointMilliseconds_ = 0;
            skillPointPopupMilliseconds_ = 0;
        }
    }
    const assets::Vector3 target{playerPosition.x, playerPosition.y,
                                 playerPosition.z + kPlayerTargetHeight};
    for (LevelBonusState& state : states_) {
        if (state.visible &&
            distanceSquared(state.asset->position, target) <
                kCollectionRadiusSquared) {
            state.visible = false;
            state.orbActive = true;
            state.progress = 0.0F;
            state.startTangent = randomTangent(3000.0F, 2000.0F, 30, 60);
            state.endTangent = randomTangent(-4000.0F, -3000.0F, 120, 240);
            collectedBonusIds_.push_back(state.asset->objectId);
        }
        if (!state.orbActive) {
            continue;
        }

        state.progress += static_cast<float>(elapsedMilliseconds) *
                          kProgressPerMillisecond;
        const float leadingProgress =
            state.progress + kTrailProgressLength / (state.progress + 1.0F);
        if (state.progress + kTrailProgressLength >= kCompletionProgress) {
            state.orbActive = false;
            const std::int32_t amount =
                state.asset->type == LevelBonusType::Health
                    ? kHealthByDifficulty[difficulty_]
                : state.asset->type == LevelBonusType::SkillPoint ? 5
                                                                  : 20;
            grants_.push_back(
                {state.asset->objectId, state.asset->type, amount});
            if (state.asset->type == LevelBonusType::SkillPoint) {
                pendingSkillPointAmount_ += amount;
                pendingSkillPointMilliseconds_ = 0;
                skillPointTotalRemainingMilliseconds_ = 6000;
            }
            continue;
        }

        LevelBonusOrbRenderState render;
        render.type = state.asset->type;
        render.roomId = state.asset->roomId;
        const float tailLength = leadingProgress - state.progress;
        for (std::size_t index = 0;
             index < LevelBonusOrbRenderState::kTrailPointCount; ++index) {
            const float fraction =
                static_cast<float>(index) /
                static_cast<float>(
                    LevelBonusOrbRenderState::kTrailPointCount - 1);
            render.trailPositions[index] = hermite(
                state.progress + tailLength * fraction,
                state.asset->position, target, state.startTangent,
                state.endTangent);
        }
        render.headPosition = render.trailPositions.back();
        render.trailHalfWidth = kTrailWidth / (state.progress + 1.0F);
        render.headHalfWidth = render.trailHalfWidth * kHeadWidthScale;
        renderOrbs_.push_back(render);
    }
}

std::vector<std::int32_t> LevelBonusRuntime::consumeCollectedBonusIds() {
    std::vector<std::int32_t> result;
    result.swap(collectedBonusIds_);
    return result;
}

std::vector<LevelBonusGrant> LevelBonusRuntime::consumeGrants() {
    std::vector<LevelBonusGrant> result;
    result.swap(grants_);
    return result;
}

std::size_t LevelBonusRuntime::visibleBonusCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(
        states_.begin(), states_.end(),
        [](const LevelBonusState& state) { return state.visible; }));
}

LevelBonusCheckPointState
LevelBonusRuntime::saveCheckPointState() const {
    return {states_,
            difficulty_,
            randomState_,
            pendingSkillPointAmount_,
            pendingSkillPointMilliseconds_,
            skillPointPopupMilliseconds_,
            skillPointTotalRemainingMilliseconds_,
            skillPointPopup_};
}

Result LevelBonusRuntime::loadCheckPointState(
    const LevelBonusCheckPointState& state) {
    if (state.bonuses.empty() != states_.empty()) {
        return Result::failure(
            "Checkpoint bonus state does not match the loaded level");
    }
    const std::size_t sharedCount =
        std::min(state.bonuses.size(), states_.size());
    for (std::size_t index = 0; index < sharedCount; ++index) {
        if (state.bonuses[index].asset == nullptr ||
            states_[index].asset == nullptr ||
            state.bonuses[index].asset->objectId !=
                states_[index].asset->objectId) {
            return Result::failure(
                "Checkpoint bonus state references a different asset");
        }
    }
    states_ = state.bonuses;
    difficulty_ = state.difficulty;
    randomState_ = state.randomState;
    pendingSkillPointAmount_ = state.pendingSkillPointAmount;
    pendingSkillPointMilliseconds_ = state.pendingSkillPointMilliseconds;
    skillPointPopupMilliseconds_ = state.skillPointPopupMilliseconds;
    skillPointTotalRemainingMilliseconds_ =
        state.skillPointTotalRemainingMilliseconds;
    skillPointPopup_ = state.skillPointPopup;
    renderOrbs_.clear();
    collectedBonusIds_.clear();
    grants_.clear();
    return Result::success();
}

std::uint32_t LevelBonusRuntime::randomBounded(
    std::uint32_t maximumExclusive) noexcept {
    randomState_ ^= randomState_ << 13U;
    randomState_ ^= randomState_ >> 17U;
    randomState_ ^= randomState_ << 5U;
    return maximumExclusive == 0 ? 0 : randomState_ % maximumExclusive;
}

assets::Vector3 LevelBonusRuntime::randomTangent(
    float horizontalScale, float verticalScale,
    std::int32_t elevationCenterDegrees,
    std::uint32_t elevationRangeDegrees) noexcept {
    const float azimuth =
        static_cast<float>(randomBounded(360)) * kDegreesToRadians;
    const float elevation =
        static_cast<float>(elevationCenterDegrees -
                           static_cast<std::int32_t>(
                               randomBounded(elevationRangeDegrees))) *
        kDegreesToRadians;
    const float horizontal = std::sin(elevation) * horizontalScale;
    return {horizontal * std::cos(azimuth),
            horizontal * std::sin(azimuth),
            std::cos(elevation) * verticalScale};
}

assets::Vector3 LevelBonusRuntime::hermite(
    float progress, const assets::Vector3& start,
    const assets::Vector3& end, const assets::Vector3& startTangent,
    const assets::Vector3& endTangent) noexcept {
    const float inverse = 1.0F - progress;
    const float startWeight = (2.0F * progress + 1.0F) * inverse * inverse;
    const float startTangentWeight = progress * inverse * inverse;
    const float endWeight = progress * progress * (3.0F - 2.0F * progress);
    const float endTangentWeight =
        progress * progress * (progress - 1.0F);
    return {
        startWeight * start.x + startTangentWeight * startTangent.x +
            endWeight * end.x + endTangentWeight * endTangent.x,
        startWeight * start.y + startTangentWeight * startTangent.y +
            endWeight * end.y + endTangentWeight * endTangent.y,
        startWeight * start.z + startTangentWeight * startTangent.z +
            endWeight * end.z + endTangentWeight * endTangent.z,
    };
}

} // namespace usm::game
