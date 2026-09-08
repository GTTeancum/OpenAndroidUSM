#include "game/AttackConfig.hpp"

#include "filesystem/GbmpArchive.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace usm::game {
namespace {

class ConfigReader final {
public:
    explicit ConfigReader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool readS16(std::int16_t& value) noexcept {
        std::uint16_t bits{};
        if (!readLittleEndian(bits)) {
            return false;
        }
        value = static_cast<std::int16_t>(bits);
        return true;
    }

    [[nodiscard]] bool readS32(std::int32_t& value) noexcept {
        std::uint32_t bits{};
        if (!readLittleEndian(bits)) {
            return false;
        }
        value = static_cast<std::int32_t>(bits);
        return true;
    }

    [[nodiscard]] bool readF32(float& value) noexcept {
        std::uint32_t bits{};
        if (!readLittleEndian(bits)) {
            return false;
        }
        value = std::bit_cast<float>(bits);
        return true;
    }

    [[nodiscard]] bool readString(std::string& value) {
        std::int16_t length{};
        if (!readS16(length) || length < 0 ||
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
    template <typename Integer>
    [[nodiscard]] bool readLittleEndian(Integer& value) noexcept {
        if (sizeof(Integer) > bytes_.size() - offset_) {
            return false;
        }
        value = 0;
        for (std::size_t index = 0; index < sizeof(Integer); ++index) {
            value |= static_cast<Integer>(std::to_integer<unsigned char>(
                         bytes_[offset_ + index]))
                     << (index * 8);
        }
        offset_ += sizeof(Integer);
        return true;
    }

    std::span<const std::byte> bytes_;
    std::size_t offset_{};
};

bool skipS32(ConfigReader& reader, std::size_t count) noexcept {
    std::int32_t ignored{};
    for (std::size_t index = 0; index < count; ++index) {
        if (!reader.readS32(ignored)) {
            return false;
        }
    }
    return true;
}

bool skipF32(ConfigReader& reader, std::size_t count) noexcept {
    float ignored{};
    for (std::size_t index = 0; index < count; ++index) {
        if (!reader.readF32(ignored)) {
            return false;
        }
    }
    return true;
}

} // namespace

float AttackDefinition::maximumReach() const noexcept {
    return *std::max_element(hitBoxExtents.begin(), hitBoxExtents.end());
}

Result AttackConfigDatabase::load(
    const std::filesystem::path& gameDataRoot) {
    filesystem::GbmpArchive configs;
    Result result = configs.open(gameDataRoot / "configs.pack");
    if (!result) {
        return result;
    }
    std::vector<std::byte> bytes;
    result = configs.read("EnemysAttackConfigs.bin", bytes);
    return result ? load(bytes) : result;
}

Result AttackConfigDatabase::load(std::span<const std::byte> bytes) {
    attacks_.clear();
    ConfigReader reader(bytes);
    std::int16_t count{};
    if (!reader.readS16(count) || count < 0 || count > 4096) {
        return Result::failure("Enemy attack config has an invalid count");
    }
    attacks_.reserve(static_cast<std::size_t>(count));
    for (std::int16_t index = 0; index < count; ++index) {
        AttackDefinition attack;
        std::string exportedName;
        std::int32_t ignoredInteger{};
        std::int32_t turnTowardTargetDuringStartup{};
        std::int32_t quickTimeEnabled{};
        std::int32_t interruptibleDuringExecution{};
        std::int32_t usesTargetRelativeMovement{};
        std::int32_t movementEndsAtSpecialAction{};
        std::int32_t permitsSpecialAnimationSuccessor{};
        std::int32_t ignoredBoolean{};
        float ignoredFloat{};
        if (!reader.readS16(attack.id) || !reader.readString(exportedName) ||
            !reader.readS32(ignoredInteger) ||
            !reader.readS32(attack.hitType) ||
            !reader.readF32(attack.startupMilliseconds) ||
            !reader.readS32(turnTowardTargetDuringStartup) ||
            !reader.readS32(quickTimeEnabled) ||
            !reader.readS32(attack.quickTimeActionId) ||
            !reader.readF32(attack.damage) ||
            !reader.readF32(attack.hitProtectionMilliseconds) ||
            !reader.readF32(attack.horizontalForce) ||
            !reader.readF32(attack.verticalForce)) {
            attacks_.clear();
            return Result::failure("Enemy attack config is truncated");
        }
        attack.name = std::move(exportedName);
        for (float& extent : attack.hitBoxExtents) {
            if (!reader.readF32(extent)) {
                attacks_.clear();
                return Result::failure("Enemy attack hit box is truncated");
            }
        }
        if (!reader.readF32(attack.minimumAngleDegrees) ||
            !reader.readF32(attack.maximumAngleDegrees) ||
            !reader.readS32(ignoredInteger) ||
            !reader.readS32(interruptibleDuringExecution) ||
            !reader.readS32(ignoredBoolean) ||
            !reader.readS32(ignoredInteger) ||
            !reader.readS32(usesTargetRelativeMovement) ||
            !reader.readS32(movementEndsAtSpecialAction) ||
            !reader.readS32(permitsSpecialAnimationSuccessor) ||
            !reader.readS32(attack.senseReactionType) ||
            !reader.readS32(ignoredBoolean) ||
            !reader.readF32(attack.senseSlowMotionDenominator) ||
            !reader.readS32(attack.forceSenseActionId) ||
            !reader.readS32(attack.sensePhotoTargetId)) {
            attacks_.clear();
            return Result::failure("Enemy attack metadata is truncated");
        }
        attack.interruptibleDuringExecution =
            interruptibleDuringExecution > 0;
        attack.turnTowardTargetDuringStartup =
            turnTowardTargetDuringStartup > 0;
        attack.quickTimeEnabled = quickTimeEnabled > 0;
        attack.usesTargetRelativeMovement =
            usesTargetRelativeMovement > 0;
        attack.movementEndsAtSpecialAction =
            movementEndsAtSpecialAction > 0;
        attack.permitsSpecialAnimationSuccessor =
            permitsSpecialAnimationSuccessor > 0;
        std::string ignoredString;
        if (!reader.readString(ignoredString) ||
            !reader.readString(ignoredString) || !skipS32(reader, 1) ||
            !reader.readF32(ignoredFloat) || !skipS32(reader, 4)) {
            attacks_.clear();
            return Result::failure("Enemy attack effects are truncated");
        }
        attacks_.push_back(std::move(attack));
    }
    if (!reader.finished()) {
        attacks_.clear();
        return Result::failure("Enemy attack config has trailing bytes");
    }
    return Result::success();
}

const AttackDefinition* AttackConfigDatabase::find(
    std::int16_t id) const noexcept {
    const auto match = std::find_if(
        attacks_.begin(), attacks_.end(), [id](const AttackDefinition& attack) {
            return attack.id == id;
        });
    return match == attacks_.end() ? nullptr : &*match;
}

} // namespace usm::game
