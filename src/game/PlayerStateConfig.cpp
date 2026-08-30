#include "game/PlayerStateConfig.hpp"

#include "filesystem/GbmpArchive.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>

namespace usm::game {
namespace {

class Reader final {
public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    bool u16(std::uint16_t& output) noexcept {
        if (remaining() < 2) {
            return false;
        }
        output = static_cast<std::uint16_t>(
            std::to_integer<std::uint8_t>(bytes_[offset_]) |
            (std::to_integer<std::uint8_t>(bytes_[offset_ + 1]) << 8U));
        offset_ += 2;
        return true;
    }

    bool s16(std::int16_t& output) noexcept {
        std::uint16_t encoded{};
        if (!u16(encoded)) {
            return false;
        }
        output = static_cast<std::int16_t>(encoded);
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
                       << (index * 8);
        }
        offset_ += 4;
        output = std::bit_cast<float>(encoded);
        return true;
    }

    bool i32(std::int32_t& output) noexcept {
        if (remaining() < 4) {
            return false;
        }
        std::uint32_t encoded{};
        for (std::size_t index = 0; index < 4; ++index) {
            encoded |= static_cast<std::uint32_t>(
                           std::to_integer<std::uint8_t>(
                               bytes_[offset_ + index]))
                       << (index * 8);
        }
        offset_ += 4;
        output = static_cast<std::int32_t>(encoded);
        return true;
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

    bool int16Vector(std::vector<std::int16_t>& output) {
        std::int16_t count{};
        if (!s16(count) || count < 0) {
            return false;
        }
        output.clear();
        output.reserve(static_cast<std::size_t>(count));
        for (std::int16_t index = 0; index < count; ++index) {
            std::int16_t value{};
            if (!s16(value)) {
                return false;
            }
            output.push_back(value);
        }
        return true;
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - offset_;
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t offset_{};
};

template <std::size_t Size>
bool readS16Array(Reader& reader,
                  std::array<std::int16_t, Size>& output) noexcept {
    for (auto& value : output) {
        if (!reader.s16(value)) {
            return false;
        }
    }
    return true;
}

template <std::size_t Size>
bool readF32Array(Reader& reader,
                  std::array<float, Size>& output) noexcept {
    for (auto& value : output) {
        if (!reader.f32(value)) {
            return false;
        }
    }
    return true;
}

} // namespace

Result PlayerStateConfigDatabase::load(
    const std::filesystem::path& gameDataRoot) {
    filesystem::GbmpArchive configs;
    Result result = configs.open(gameDataRoot / "configs.pack");
    if (!result) {
        return result;
    }
    std::vector<std::byte> bytes;
    result = configs.read("MC_STATE.bin", bytes);
    if (!result || !(result = loadStates(bytes))) {
        return !result ? Result::failure("Could not load MC_STATE: " +
                                         result.message())
                       : result;
    }
    result = configs.read("MC_SOUND.bin", bytes);
    if (!result || !(result = loadSounds(bytes))) {
        return !result ? Result::failure("Could not load MC_SOUND: " +
                                         result.message())
                       : result;
    }
    return Result::success();
}

Result PlayerStateConfigDatabase::loadStates(
    std::span<const std::byte> bytes) {
    states_.clear();
    Reader reader(bytes);
    std::uint16_t count{};
    if (!reader.u16(count)) {
        return Result::failure("MC_STATE count is truncated");
    }
    states_.reserve(count);
    for (std::uint16_t index = 0; index < count; ++index) {
        PlayerStateDefinition state;
        std::int16_t serializedId{};
        std::int16_t boolValue{};
        if (!reader.s16(serializedId) || serializedId < 0 ||
            !reader.string(state.name) || !reader.s16(state.stateClass) ||
            !reader.s16(state.motionType) ||
            !readF32Array(reader, state.motionParameters) ||
            !reader.s16(state.soundTriggerFrame) ||
            !readS16Array(reader, state.auxiliaryParameters) ||
            !reader.i32(state.primaryAnimationId) ||
            !reader.int16Vector(state.animationIds) ||
            !reader.int16Vector(state.auxiliaryIdLists[0]) ||
            !reader.int16Vector(state.auxiliaryIdLists[1]) ||
            !reader.int16Vector(state.enterSoundConfigIds) ||
            !reader.int16Vector(state.frameSoundConfigIds) ||
            !readF32Array(reader, state.timingParameters) ||
            !reader.s16(boolValue) || !reader.s16(state.nextStateId)) {
            states_.clear();
            return Result::failure("MC_STATE record is truncated");
        }
        std::int16_t transitionCount{};
        if ((boolValue != 0 && boolValue != 1) ||
            !reader.s16(transitionCount) || transitionCount < 0) {
            states_.clear();
            return Result::failure("MC_STATE record contains invalid fields");
        }
        state.animationListFlag = boolValue != 0;
        for (auto& field : state.transitionFields) {
            field.reserve(static_cast<std::size_t>(transitionCount));
        }
        for (std::int16_t transition = 0; transition < transitionCount;
             ++transition) {
            for (auto& field : state.transitionFields) {
                std::int16_t value{};
                if (!reader.s16(value)) {
                    states_.clear();
                    return Result::failure(
                        "MC_STATE transition record is truncated");
                }
                field.push_back(value);
            }
        }
        state.id = static_cast<std::uint16_t>(serializedId);
        if (state.id != index || state.name.empty()) {
            states_.clear();
            return Result::failure("MC_STATE ID or name is invalid");
        }
        states_.push_back(std::move(state));
    }
    if (reader.remaining() != 0) {
        states_.clear();
        return Result::failure("MC_STATE contains unexpected trailing data");
    }
    return Result::success();
}

Result PlayerStateConfigDatabase::loadSounds(
    std::span<const std::byte> bytes) {
    soundConfigs_.clear();
    Reader reader(bytes);
    std::uint16_t count{};
    if (!reader.u16(count)) {
        return Result::failure("MC_SOUND count is truncated");
    }
    soundConfigs_.reserve(count);
    for (std::uint16_t index = 0; index < count; ++index) {
        PlayerSoundConfig config;
        std::int16_t serializedId{};
        if (!reader.s16(serializedId) || serializedId < 0 ||
            !reader.string(config.name) || !reader.s16(config.playbackType) ||
            !reader.s16(config.selectionMode) ||
            !reader.s16(config.targetMode) || !reader.s16(config.parameter) ||
            !reader.int16Vector(config.voxSoundIds) ||
            !reader.int16Vector(config.activeEmitterIds)) {
            soundConfigs_.clear();
            return Result::failure("MC_SOUND record is truncated");
        }
        config.id = static_cast<std::uint16_t>(serializedId);
        if (config.id != index || config.name.empty()) {
            soundConfigs_.clear();
            return Result::failure("MC_SOUND ID or name is invalid");
        }
        soundConfigs_.push_back(std::move(config));
    }
    if (reader.remaining() != 0) {
        soundConfigs_.clear();
        return Result::failure("MC_SOUND contains unexpected trailing data");
    }
    return Result::success();
}

const PlayerStateDefinition* PlayerStateConfigDatabase::findState(
    std::string_view name) const noexcept {
    const auto match = std::find_if(
        states_.begin(), states_.end(), [name](const auto& state) {
            return state.name == name;
        });
    return match == states_.end() ? nullptr : &*match;
}

const PlayerSoundConfig* PlayerStateConfigDatabase::findSoundConfig(
    std::int16_t id) const noexcept {
    if (id < 0 || static_cast<std::size_t>(id) >= soundConfigs_.size() ||
        soundConfigs_[static_cast<std::size_t>(id)].id != id) {
        return nullptr;
    }
    return &soundConfigs_[static_cast<std::size_t>(id)];
}

const PlayerSoundConfig* PlayerStateConfigDatabase::findSoundConfig(
    std::string_view name) const noexcept {
    const auto match = std::find_if(
        soundConfigs_.begin(), soundConfigs_.end(), [name](const auto& config) {
            return config.name == name;
        });
    return match == soundConfigs_.end() ? nullptr : &*match;
}

} // namespace usm::game
