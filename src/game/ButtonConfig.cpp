#include "game/ButtonConfig.hpp"

#include "filesystem/GbmpArchive.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

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

Result ButtonConfigDatabase::load(
    const std::filesystem::path& gameDataRoot) {
    filesystem::GbmpArchive configs;
    Result result = configs.open(gameDataRoot / "configs.pack");
    if (!result) {
        return result;
    }
    std::vector<std::byte> bytes;
    result = configs.read("BCONFIG.bin", bytes);
    return result ? load(bytes) : result;
}

Result ButtonConfigDatabase::load(std::span<const std::byte> bytes) {
    definitions_.clear();
    ConfigReader reader(bytes);
    std::int16_t count{};
    if (!reader.s16(count) || count < 0 || count > 4096) {
        return Result::failure("Button config has an invalid count");
    }
    definitions_.reserve(static_cast<std::size_t>(count));
    for (std::int16_t index = 0; index < count; ++index) {
        ButtonConfigDefinition definition;
        std::int16_t sequenceCount{};
        if (!reader.s16(definition.id) || !reader.string(definition.name) ||
            !reader.s16(definition.interactionType) ||
            !reader.s16(definition.interactionValue) ||
            !reader.f32(definition.screenX) ||
            !reader.f32(definition.screenY) ||
            !reader.f32(definition.durationMilliseconds) ||
            !reader.s32(definition.normalAnimationId) ||
            !reader.s32(definition.activeAnimationId) ||
            !reader.s16(definition.requiredActionCount) ||
            !reader.s16(sequenceCount) || sequenceCount < 0 ||
            sequenceCount > 4096) {
            definitions_.clear();
            return Result::failure("Button config is truncated");
        }
        definition.sequence.reserve(static_cast<std::size_t>(sequenceCount));
        for (std::int16_t sequenceIndex = 0;
             sequenceIndex < sequenceCount; ++sequenceIndex) {
            std::int16_t child{};
            if (!reader.s16(child)) {
                definitions_.clear();
                return Result::failure("Button config sequence is truncated");
            }
            definition.sequence.push_back(child);
        }
        definitions_.push_back(std::move(definition));
    }
    if (!reader.finished()) {
        definitions_.clear();
        return Result::failure("Button config has trailing bytes");
    }
    return Result::success();
}

const ButtonConfigDefinition* ButtonConfigDatabase::find(
    std::int32_t id) const noexcept {
    const auto match = std::find_if(
        definitions_.begin(), definitions_.end(),
        [id](const ButtonConfigDefinition& definition) {
            return definition.id == id;
        });
    return match == definitions_.end() ? nullptr : &*match;
}

} // namespace usm::game
