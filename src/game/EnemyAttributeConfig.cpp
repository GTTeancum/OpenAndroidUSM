#include "game/EnemyAttributeConfig.hpp"

#include "filesystem/GbmpArchive.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace usm::game {
namespace {

class AttributeReader final {
public:
    explicit AttributeReader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool s16(std::int16_t& value) noexcept {
        if (bytes_.size() - offset_ < sizeof(std::uint16_t)) {
            return false;
        }
        const std::uint16_t bits =
            static_cast<std::uint16_t>(
                std::to_integer<unsigned char>(bytes_[offset_])) |
            static_cast<std::uint16_t>(
                std::to_integer<unsigned char>(bytes_[offset_ + 1]))
                << 8;
        offset_ += sizeof(std::uint16_t);
        value = static_cast<std::int16_t>(bits);
        return true;
    }

    [[nodiscard]] bool s32(std::int32_t& value) noexcept {
        if (bytes_.size() - offset_ < sizeof(std::uint32_t)) {
            return false;
        }
        std::uint32_t bits{};
        for (std::size_t index = 0; index < sizeof(bits); ++index) {
            bits |= static_cast<std::uint32_t>(
                        std::to_integer<unsigned char>(bytes_[offset_ + index]))
                    << (index * 8);
        }
        offset_ += sizeof(bits);
        value = static_cast<std::int32_t>(bits);
        return true;
    }

    [[nodiscard]] bool skip(std::size_t count) noexcept {
        if (count > bytes_.size() - offset_) {
            return false;
        }
        offset_ += count;
        return true;
    }

    [[nodiscard]] bool f32(float& value) noexcept {
        std::int32_t bits{};
        if (!s32(bits)) {
            return false;
        }
        value = std::bit_cast<float>(bits);
        return std::isfinite(value);
    }

    [[nodiscard]] bool string(std::string& value) {
        std::int16_t length{};
        if (!s16(length) || length < 0 ||
            static_cast<std::size_t>(length) > bytes_.size() - offset_) {
            return false;
        }
        value.assign(reinterpret_cast<const char*>(bytes_.data() + offset_),
                     static_cast<std::size_t>(length));
        offset_ += static_cast<std::size_t>(length);
        return true;
    }

    [[nodiscard]] bool finished() const noexcept {
        return offset_ == bytes_.size();
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t offset_{};
};

// CEnemy::InitEntityAttribute (0x003373e8) consumes the first two signed
// 32-bit fields after the exported name as the collision radius and height.
// ReadAttributeInfo (0x0033bf00) converts them and four later signed fields to
// float. The third and fourth values in that four-field range block are used
// by CEnemy::MoveToRangeAttackPlayerRange (0x003316b0) as the minimum and
// maximum ranged engagement distances. The fields on either side remain
// opaque until their own consumers provide defensible names. The preceding
// three F32 speeds and two S32 melee distances are consumed by
// CEnemy::InitEntityAttribute (0x003373e8), ResetBehavior (0x00332cc0), and
// CBehaviorMoveOnWall::SetMoveLineSpeed (0x003bdddc).
constexpr std::size_t kOpaqueFieldsAfterRangeAttackDistances = 12;
constexpr std::size_t kFieldsAfterHitForceFlagsBeforeTrailingString = 28;
constexpr std::size_t kTrailingFlags = 8;

} // namespace

Result EnemyAttributeConfigDatabase::load(
    const std::filesystem::path& gameDataRoot) {
    filesystem::GbmpArchive configs;
    Result result = configs.open(gameDataRoot / "configs.pack");
    if (!result) {
        return result;
    }
    std::vector<std::byte> bytes;
    result = configs.read("EnemysAttributeConfigs.bin", bytes);
    if (!result) {
        return result;
    }
    result = load(bytes);
    if (!result) {
        return result;
    }
    result = configs.read("EnemysBehaviorConfigs.bin", bytes);
    return result ? loadBehaviors(bytes) : result;
}

Result EnemyAttributeConfigDatabase::load(std::span<const std::byte> bytes) {
    definitions_.clear();
    AttributeReader reader(bytes);
    std::int16_t count{};
    if (!reader.s16(count) || count < 0 || count > 4096) {
        return Result::failure("Enemy attribute config has an invalid count");
    }
    definitions_.reserve(static_cast<std::size_t>(count));
    for (std::int16_t enemyTypeId = 0; enemyTypeId < count; ++enemyTypeId) {
        EnemyAttributeDefinition definition;
        definition.enemyTypeId = enemyTypeId;
        std::int32_t collisionRadius{};
        std::int32_t collisionHeight{};
        std::int32_t minimumMeleeAttackDistance{};
        std::int32_t maximumMeleeAttackDistance{};
        std::int32_t minimumRangeAttackDistance{};
        std::int32_t maximumRangeAttackDistance{};
        std::int16_t rangedAttackCount{};
        float opaqueFieldBeforeHitForceFlags{};
        std::int32_t allowsHorizontalHitForce{};
        std::int32_t allowsVerticalHitForce{};
        std::int32_t allowsLaunchHitType{};
        std::int32_t canBeCounterHit{};
        std::string trailingResourceName;
        if (!reader.s16(definition.exportedId) ||
            !reader.string(definition.name) ||
            !reader.s32(collisionRadius) || !reader.s32(collisionHeight) ||
            collisionRadius <= 0 || collisionHeight <= 0 ||
            !reader.skip(4) ||
            !reader.f32(definition.walkSpeedCentimetersPerMillisecond) ||
            !reader.f32(definition.runSpeedCentimetersPerMillisecond) ||
            !reader.f32(definition.wallSpeedCentimetersPerMillisecond) ||
            definition.walkSpeedCentimetersPerMillisecond < 0.0F ||
            definition.runSpeedCentimetersPerMillisecond < 0.0F ||
            definition.wallSpeedCentimetersPerMillisecond < 0.0F ||
            !reader.s32(minimumMeleeAttackDistance) ||
            !reader.s32(maximumMeleeAttackDistance) ||
            minimumMeleeAttackDistance < 0 ||
            maximumMeleeAttackDistance < minimumMeleeAttackDistance ||
            !reader.s32(minimumRangeAttackDistance) ||
            !reader.s32(maximumRangeAttackDistance) ||
            minimumRangeAttackDistance < 0 ||
            maximumRangeAttackDistance < minimumRangeAttackDistance ||
            !reader.skip(kOpaqueFieldsAfterRangeAttackDistances) ||
            !reader.s16(rangedAttackCount) || rangedAttackCount < 0 ||
            rangedAttackCount > 1024) {
            definitions_.clear();
            return Result::failure("Enemy attribute config is truncated");
        }
        definition.collisionRadius = static_cast<float>(collisionRadius);
        definition.collisionHeight = static_cast<float>(collisionHeight);
        definition.minimumMeleeAttackDistance =
            static_cast<float>(minimumMeleeAttackDistance);
        definition.maximumMeleeAttackDistance =
            static_cast<float>(maximumMeleeAttackDistance);
        definition.minimumRangeAttackDistance =
            static_cast<float>(minimumRangeAttackDistance);
        definition.maximumRangeAttackDistance =
            static_cast<float>(maximumRangeAttackDistance);
        definition.rangedAttackTypeMapIndices.reserve(
            static_cast<std::size_t>(rangedAttackCount));
        for (std::int16_t index = 0; index < rangedAttackCount; ++index) {
            std::int16_t attackType{};
            if (!reader.s16(attackType)) {
                definitions_.clear();
                return Result::failure("Enemy ranged-attack list is truncated");
            }
            definition.rangedAttackTypeMapIndices.push_back(attackType);
        }
        // ReadAttributeInfo (0x0033bf00) reads one F32 followed by four
        // S32 booleans after the ranged-weapon vector. The first three are
        // copied to CEnemy+0x452..0x454 by InitEntityAttribute.
        if (!reader.f32(opaqueFieldBeforeHitForceFlags) ||
            !reader.s32(allowsHorizontalHitForce) ||
            !reader.s32(allowsVerticalHitForce) ||
            !reader.s32(allowsLaunchHitType) ||
            !reader.s32(canBeCounterHit) ||
            !reader.skip(kFieldsAfterHitForceFlagsBeforeTrailingString) ||
            !reader.string(trailingResourceName) ||
            !reader.skip(kTrailingFlags)) {
            definitions_.clear();
            return Result::failure("Enemy attribute config is truncated");
        }
        definition.allowsHorizontalHitForce =
            allowsHorizontalHitForce > 0;
        definition.allowsVerticalHitForce = allowsVerticalHitForce > 0;
        definition.allowsLaunchHitType = allowsLaunchHitType > 0;
        definition.canBeCounterHit = canBeCounterHit > 0;
        definitions_.push_back(std::move(definition));
    }
    if (!reader.finished()) {
        definitions_.clear();
        return Result::failure("Enemy attribute config has trailing bytes");
    }
    return Result::success();
}

Result EnemyAttributeConfigDatabase::loadBehaviors(
    std::span<const std::byte> bytes) {
    for (auto& definition : definitions_) {
        definition.behaviorTypeMapIndices.clear();
    }
    AttributeReader reader(bytes);
    std::int16_t count{};
    if (!reader.s16(count) || count < 0 ||
        static_cast<std::size_t>(count) != definitions_.size()) {
        return Result::failure("Enemy behavior config has an invalid count");
    }
    for (std::int16_t enemyTypeId = 0; enemyTypeId < count; ++enemyTypeId) {
        std::int16_t exportedId{};
        std::string name;
        std::int16_t behaviorCount{};
        if (!reader.s16(exportedId) || !reader.string(name) ||
            !reader.s16(behaviorCount) || behaviorCount < 0 ||
            behaviorCount > 1024 || exportedId != enemyTypeId ||
            name != definitions_[static_cast<std::size_t>(enemyTypeId)].name) {
            return Result::failure("Enemy behavior config is truncated");
        }
        auto& behaviorIndices =
            definitions_[static_cast<std::size_t>(enemyTypeId)]
                .behaviorTypeMapIndices;
        behaviorIndices.clear();
        behaviorIndices.reserve(static_cast<std::size_t>(behaviorCount));
        for (std::int16_t index = 0; index < behaviorCount; ++index) {
            std::int32_t behaviorType{};
            if (!reader.s32(behaviorType) || behaviorType < 0 ||
                behaviorType >= 34) {
                return Result::failure(
                    "Enemy behavior list contains an invalid type");
            }
            behaviorIndices.push_back(behaviorType);
        }
    }
    if (!reader.finished()) {
        return Result::failure("Enemy behavior config has trailing bytes");
    }
    return Result::success();
}

bool EnemyAttributeDefinition::canBeTiedUp() const noexcept {
    constexpr std::int32_t kTiedUpBehaviorMapIndex = 4;
    return std::find(behaviorTypeMapIndices.begin(),
                     behaviorTypeMapIndices.end(),
                     kTiedUpBehaviorMapIndex) != behaviorTypeMapIndices.end();
}

bool EnemyAttributeDefinition::canBeDraggedTo() const noexcept {
    // CEnemy::InitEntityAttribute (0x003373e8) clears CEnemy+0x216 only for
    // AIRCRAFT_BLUE (20) and AIRCRAFT_RED (21).
    return enemyTypeId != 20 && enemyTypeId != 21;
}

bool EnemyAttributeDefinition::canMoveOnWall() const noexcept {
    // k_enemy_behavior_map_table at 0x004c0848: slot 13 -> interface 0x13b.
    return std::find(behaviorTypeMapIndices.begin(), behaviorTypeMapIndices.end(),
                     13) != behaviorTypeMapIndices.end();
}

const EnemyAttributeDefinition* EnemyAttributeConfigDatabase::find(
    std::int16_t enemyTypeId) const noexcept {
    const auto match = std::find_if(
        definitions_.begin(), definitions_.end(),
        [enemyTypeId](const EnemyAttributeDefinition& definition) {
            return definition.enemyTypeId == enemyTypeId;
        });
    return match == definitions_.end() ? nullptr : &*match;
}

} // namespace usm::game
