#include "game/EnemyRangeAttackConfig.hpp"

#include "filesystem/GbmpArchive.hpp"

#include <algorithm>
#include <bit>

namespace usm::game {
namespace {

class ConfigReader final {
public:
    explicit ConfigReader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool s16(std::int16_t& value) noexcept {
        std::uint16_t bits{};
        if (!littleEndian(bits)) {
            return false;
        }
        value = static_cast<std::int16_t>(bits);
        return true;
    }

    [[nodiscard]] bool s32(std::int32_t& value) noexcept {
        std::uint32_t bits{};
        if (!littleEndian(bits)) {
            return false;
        }
        value = static_cast<std::int32_t>(bits);
        return true;
    }

    [[nodiscard]] bool f32(float& value) noexcept {
        std::uint32_t bits{};
        if (!littleEndian(bits)) {
            return false;
        }
        value = std::bit_cast<float>(bits);
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
    template <typename Integer>
    [[nodiscard]] bool littleEndian(Integer& value) noexcept {
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

} // namespace

Result EnemyRangeAttackConfigDatabase::load(
    const std::filesystem::path& gameDataRoot) {
    filesystem::GbmpArchive configs;
    Result result = configs.open(gameDataRoot / "configs.pack");
    if (!result) {
        return result;
    }
    std::vector<std::byte> bytes;
    result = configs.read("EnemysRangeAttackConfigs.bin", bytes);
    return result ? load(bytes) : result;
}

Result EnemyRangeAttackConfigDatabase::load(std::span<const std::byte> bytes) {
    definitions_.clear();
    ConfigReader reader(bytes);
    std::int16_t count{};
    if (!reader.s16(count) || count < 0 || count > 4096) {
        return Result::failure("Enemy range-attack config has an invalid count");
    }
    definitions_.reserve(static_cast<std::size_t>(count));
    for (std::int16_t index = 0; index < count; ++index) {
        EnemyRangeAttackDefinition definition;
        if (!reader.s16(definition.id) || !reader.string(definition.name) ||
            !reader.s32(definition.attackTypeMapId) ||
            !reader.f32(definition.animationDurationMilliseconds) ||
            !reader.f32(definition.projectileSpeedCentimetersPerSecond) ||
            !reader.f32(definition.damage)) {
            definitions_.clear();
            return Result::failure("Enemy range-attack config is truncated");
        }
        definitions_.push_back(std::move(definition));
    }
    if (!reader.finished()) {
        definitions_.clear();
        return Result::failure("Enemy range-attack config has trailing bytes");
    }
    return Result::success();
}

const EnemyRangeAttackDefinition*
EnemyRangeAttackConfigDatabase::findByMapId(std::int32_t mapId) const noexcept {
    const auto match = std::find_if(
        definitions_.begin(), definitions_.end(),
        [mapId](const EnemyRangeAttackDefinition& definition) {
            return definition.attackTypeMapId == mapId;
        });
    return match == definitions_.end() ? nullptr : &*match;
}

} // namespace usm::game
