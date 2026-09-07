#include "game/EnemyAttackIntervalConfig.hpp"

#include "filesystem/GbmpArchive.hpp"

#include <algorithm>
#include <bit>
#include <type_traits>

namespace usm::game {
namespace {

// Ghidra data address 0x00510818. GetRangeAttackTypeMapId (0x003bffdc)
// performs the inverse lookup over this exact 26-entry table.
constexpr std::array<std::int32_t, 26> kRangeWeaponTypes{
    0, 2, 3, 12, 13, 16, 17, 19, 20, 26, 5, 28, 18,
    15, 14, 29, 21, 30, 22, 23, 35, 38, 39, 37, 41, 4};

class IntervalReader final {
public:
    explicit IntervalReader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    template <typename Integer>
    [[nodiscard]] bool integer(Integer& value) noexcept {
        if (sizeof(Integer) > bytes_.size() - offset_) {
            return false;
        }
        using Unsigned = std::make_unsigned_t<Integer>;
        Unsigned bits{};
        for (std::size_t index = 0; index < sizeof(Integer); ++index) {
            bits |= static_cast<Unsigned>(std::to_integer<unsigned char>(
                        bytes_[offset_ + index]))
                    << (index * 8);
        }
        offset_ += sizeof(Integer);
        value = static_cast<Integer>(bits);
        return true;
    }

    [[nodiscard]] bool f32(float& value) noexcept {
        std::uint32_t bits{};
        if (!integer(bits)) {
            return false;
        }
        value = std::bit_cast<float>(bits);
        return true;
    }

    [[nodiscard]] bool string(std::string& value) {
        std::int16_t length{};
        if (!integer(length) || length < 0 ||
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

} // namespace

std::optional<std::int32_t> resolveEnemyRangeWeaponType(
    std::int32_t authoredMapIndex) noexcept {
    if (authoredMapIndex < 0 ||
        static_cast<std::size_t>(authoredMapIndex) >= kRangeWeaponTypes.size()) {
        return std::nullopt;
    }
    return kRangeWeaponTypes[static_cast<std::size_t>(authoredMapIndex)];
}

Result EnemyAttackIntervalConfigDatabase::load(
    const std::filesystem::path& gameDataRoot) {
    filesystem::GbmpArchive configs;
    Result result = configs.open(gameDataRoot / "configs.pack");
    if (!result) {
        return result;
    }
    std::vector<std::byte> bytes;
    result = configs.read("AttackIntervalTimeConfigs.bin", bytes);
    return result ? load(bytes) : result;
}

Result EnemyAttackIntervalConfigDatabase::load(
    std::span<const std::byte> bytes) {
    definitions_.clear();
    IntervalReader reader(bytes);
    std::int16_t count{};
    if (!reader.integer(count) || count < 0 || count > 4096) {
        return Result::failure("Enemy attack-interval config has an invalid count");
    }
    definitions_.reserve(static_cast<std::size_t>(count));
    for (std::int16_t index = 0; index < count; ++index) {
        EnemyAttackIntervalDefinition definition;
        if (!reader.integer(definition.id) || !reader.string(definition.name) ||
            !reader.integer(definition.weaponTypeMapIndex)) {
            definitions_.clear();
            return Result::failure("Enemy attack-interval config is truncated");
        }
        for (float& interval : definition.intervalMilliseconds) {
            if (!reader.f32(interval)) {
                definitions_.clear();
                return Result::failure(
                    "Enemy attack-interval config is truncated");
            }
        }
        definitions_.push_back(std::move(definition));
    }
    if (!reader.finished()) {
        definitions_.clear();
        return Result::failure("Enemy attack-interval config has trailing bytes");
    }
    return Result::success();
}

const EnemyAttackIntervalDefinition*
EnemyAttackIntervalConfigDatabase::findForWeaponType(
    std::int32_t weaponType) const noexcept {
    const auto map = std::find(kRangeWeaponTypes.begin(),
                               kRangeWeaponTypes.end(), weaponType);
    if (map == kRangeWeaponTypes.end()) {
        return nullptr;
    }
    const auto mapIndex = static_cast<std::int32_t>(
        std::distance(kRangeWeaponTypes.begin(), map));
    return findByWeaponTypeMapIndex(mapIndex);
}

const EnemyAttackIntervalDefinition*
EnemyAttackIntervalConfigDatabase::findByWeaponTypeMapIndex(
    std::int32_t weaponTypeMapIndex) const noexcept {
    const auto definition = std::find_if(
        definitions_.begin(), definitions_.end(),
        [weaponTypeMapIndex](const EnemyAttackIntervalDefinition& candidate) {
            return candidate.weaponTypeMapIndex == weaponTypeMapIndex;
        });
    return definition == definitions_.end() ? nullptr : &*definition;
}

} // namespace usm::game
