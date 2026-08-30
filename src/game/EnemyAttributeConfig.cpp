#include "game/EnemyAttributeConfig.hpp"

#include "filesystem/GbmpArchive.hpp"

#include <algorithm>

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

    [[nodiscard]] bool skip(std::size_t count) noexcept {
        if (count > bytes_.size() - offset_) {
            return false;
        }
        offset_ += count;
        return true;
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

// EnemyAttributeFile::ReadAttributeInfo (0x0033bf00) reads 52 bytes between
// the exported name and ranged-attack vector, then 48 bytes, a string, and
// two final 32-bit flags. Those unrelated fields remain deliberately opaque
// until their consumers provide defensible names.
constexpr std::size_t kFieldsBeforeRangedAttacks = 52;
constexpr std::size_t kFieldsBeforeTrailingString = 48;
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
    return result ? load(bytes) : result;
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
        std::int16_t rangedAttackCount{};
        std::string trailingResourceName;
        if (!reader.s16(definition.exportedId) ||
            !reader.string(definition.name) ||
            !reader.skip(kFieldsBeforeRangedAttacks) ||
            !reader.s16(rangedAttackCount) || rangedAttackCount < 0 ||
            rangedAttackCount > 1024) {
            definitions_.clear();
            return Result::failure("Enemy attribute config is truncated");
        }
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
        if (!reader.skip(kFieldsBeforeTrailingString) ||
            !reader.string(trailingResourceName) ||
            !reader.skip(kTrailingFlags)) {
            definitions_.clear();
            return Result::failure("Enemy attribute config is truncated");
        }
        definitions_.push_back(std::move(definition));
    }
    if (!reader.finished()) {
        definitions_.clear();
        return Result::failure("Enemy attribute config has trailing bytes");
    }
    return Result::success();
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
