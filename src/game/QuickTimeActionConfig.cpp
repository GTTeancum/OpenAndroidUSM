#include "game/QuickTimeActionConfig.hpp"

#include "filesystem/GbmpArchive.hpp"

#include <algorithm>
#include <charconv>

namespace usm::game {
namespace {

class ConfigReader final {
public:
    explicit ConfigReader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool s16(std::int16_t& value) noexcept {
        if (sizeof(std::uint16_t) > bytes_.size() - offset_) {
            return false;
        }
        std::uint16_t bits{};
        for (std::size_t index = 0; index < sizeof(bits); ++index) {
            bits |= static_cast<std::uint16_t>(
                        std::to_integer<unsigned char>(bytes_[offset_ + index]))
                    << (index * 8);
        }
        offset_ += sizeof(bits);
        value = static_cast<std::int16_t>(bits);
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

bool parseShortVector(std::string_view text,
                      std::vector<std::int16_t>& output) {
    output.clear();
    if (text == "NONE") {
        return true;
    }
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t separator = text.find(',', start);
        const std::size_t end = separator == std::string_view::npos
                                    ? text.size()
                                    : separator;
        if (end == start) {
            return false;
        }
        std::int16_t value{};
        const char* beginPointer = text.data() + start;
        const char* endPointer = text.data() + end;
        const auto parsed = std::from_chars(beginPointer, endPointer, value);
        if (parsed.ec != std::errc{} || parsed.ptr != endPointer) {
            return false;
        }
        output.push_back(value);
        if (separator == std::string_view::npos) {
            break;
        }
        start = separator + 1;
        // The shipped exporter leaves a trailing comma on several one-frame
        // lists (for example action 7 stores "21,"). GetVectorFromString
        // (0x002fc428) stops at that empty tail rather than appending a value.
        if (start == text.size()) {
            break;
        }
    }
    return true;
}

} // namespace

Result QuickTimeActionConfigDatabase::load(
    const std::filesystem::path& gameDataRoot) {
    filesystem::GbmpArchive configs;
    Result result = configs.open(gameDataRoot / "configs.pack");
    if (!result) {
        return result;
    }
    std::vector<std::byte> bytes;
    result = configs.read("QTE_ACTIONS.bin", bytes);
    return result ? load(bytes) : result;
}

Result QuickTimeActionConfigDatabase::load(
    std::span<const std::byte> bytes) {
    definitions_.clear();
    ConfigReader reader(bytes);
    std::int16_t count{};
    if (!reader.s16(count) || count < 0 || count > 4096) {
        return Result::failure("Quick-time action config has an invalid count");
    }
    definitions_.reserve(static_cast<std::size_t>(count));
    for (std::int16_t index = 0; index < count; ++index) {
        QuickTimeActionDefinition definition;
        std::string attackFrames;
        std::string attackDamage;
        std::int16_t loop{};
        if (!reader.s16(definition.id) || !reader.string(definition.name) ||
            !reader.s16(definition.attackDirection) ||
            !reader.s16(definition.buttonConfigId) ||
            !reader.string(definition.playerAnimation) ||
            !reader.s16(definition.playerSoundId) ||
            !reader.string(definition.npcAnimation) ||
            !reader.s16(definition.npcSoundId) || !reader.s16(loop) ||
            !reader.s16(definition.successStateId) ||
            !reader.s16(definition.failureStateId) ||
            !reader.string(attackFrames) || !reader.string(attackDamage) ||
            !reader.s16(definition.playerStartRotationDegrees) ||
            !reader.s16(definition.npcStartRotationDegrees) ||
            !reader.s16(definition.playerEndRotationDegrees) ||
            !reader.s16(definition.npcEndRotationDegrees) ||
            !reader.s16(definition.playerEndAction) ||
            !reader.s16(definition.npcEndAction) ||
            !reader.s16(definition.slowMotion) ||
            !reader.s16(definition.slowMotionDuration) ||
            !reader.s16(definition.bossHealthStealMultiplier) ||
            !parseShortVector(attackFrames, definition.attackFrames) ||
            !parseShortVector(attackDamage, definition.attackDamage) ||
            definition.attackFrames.size() != definition.attackDamage.size()) {
            definitions_.clear();
            return Result::failure("Quick-time action config is truncated or invalid");
        }
        definition.loop = loop != 0;
        definitions_.push_back(std::move(definition));
    }
    if (!reader.finished()) {
        definitions_.clear();
        return Result::failure("Quick-time action config has trailing bytes");
    }
    return Result::success();
}

const QuickTimeActionDefinition* QuickTimeActionConfigDatabase::find(
    std::int32_t id) const noexcept {
    const auto match = std::find_if(
        definitions_.begin(), definitions_.end(),
        [id](const QuickTimeActionDefinition& definition) {
            return definition.id == id;
        });
    return match == definitions_.end() ? nullptr : &*match;
}

} // namespace usm::game
