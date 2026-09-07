#include "game/PlayerHitEffectConfig.hpp"

#include "filesystem/GbmpArchive.hpp"

#include <bit>
#include <cmath>
#include <utility>

namespace usm::game {
namespace {

class Reader final {
public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    bool s16(std::int16_t& output) noexcept {
        if (remaining() < 2) {
            return false;
        }
        const std::uint16_t encoded =
            static_cast<std::uint16_t>(
                std::to_integer<std::uint8_t>(bytes_[offset_])) |
            static_cast<std::uint16_t>(
                std::to_integer<std::uint8_t>(bytes_[offset_ + 1]) << 8U);
        output = static_cast<std::int16_t>(encoded);
        offset_ += 2;
        return true;
    }

    bool f32(float& output) noexcept {
        if (remaining() < 4) {
            return false;
        }
        std::uint32_t encoded{};
        for (std::size_t index = 0; index < 4; ++index) {
            encoded |= static_cast<std::uint32_t>(
                           std::to_integer<std::uint8_t>(
                               bytes_[offset_ + index]))
                       << (index * 8U);
        }
        offset_ += 4;
        output = std::bit_cast<float>(encoded);
        return std::isfinite(output);
    }

    bool string(std::string& output) {
        std::int16_t length{};
        if (!s16(length) || length < 0 ||
            static_cast<std::size_t>(length) > remaining()) {
            return false;
        }
        output.assign(reinterpret_cast<const char*>(bytes_.data() + offset_),
                      static_cast<std::size_t>(length));
        offset_ += static_cast<std::size_t>(length);
        return true;
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - offset_;
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t offset_{};
};

} // namespace

Result PlayerHitEffectConfigDatabase::load(
    const std::filesystem::path& gameDataRoot) {
    filesystem::GbmpArchive configs;
    Result result = configs.open(gameDataRoot / "configs.pack");
    if (!result) {
        return result;
    }
    std::vector<std::byte> bytes;
    result = configs.read("MCHitEffects.bin", bytes);
    return result ? load(bytes)
                  : Result::failure("Could not load MCHitEffects: " +
                                    result.message());
}

Result PlayerHitEffectConfigDatabase::load(
    std::span<const std::byte> bytes) {
    definitions_.clear();
    Reader reader(bytes);
    std::int16_t count{};
    if (!reader.s16(count) || count < 0) {
        return Result::failure("MCHitEffects count is invalid");
    }
    definitions_.reserve(static_cast<std::size_t>(count));
    for (std::int16_t index = 0; index < count; ++index) {
        PlayerHitEffectDefinition definition;
        std::int16_t serializedId{};
        std::int16_t attached{};
        if (!reader.s16(serializedId) || serializedId != index ||
            !reader.string(definition.name) || definition.name.empty() ||
            !reader.string(definition.meshFile) ||
            definition.meshFile.empty() ||
            !reader.string(definition.boneName) ||
            definition.boneName.empty() || !reader.s16(attached) ||
            (attached != 0 && attached != 1) ||
            !reader.s16(definition.renderingParameter) ||
            !reader.f32(definition.lifetimeMilliseconds)) {
            definitions_.clear();
            return Result::failure("MCHitEffects record is invalid");
        }
        definition.id = static_cast<std::uint16_t>(serializedId);
        definition.snapshotBoneTransform = attached != 0;
        definitions_.push_back(std::move(definition));
    }
    if (reader.remaining() != 0) {
        definitions_.clear();
        return Result::failure("MCHitEffects contains trailing data");
    }
    return Result::success();
}

const PlayerHitEffectDefinition* PlayerHitEffectConfigDatabase::find(
    std::int16_t id) const noexcept {
    if (id < 0 || static_cast<std::size_t>(id) >= definitions_.size() ||
        definitions_[static_cast<std::size_t>(id)].id !=
            static_cast<std::uint16_t>(id)) {
        return nullptr;
    }
    return &definitions_[static_cast<std::size_t>(id)];
}

} // namespace usm::game
