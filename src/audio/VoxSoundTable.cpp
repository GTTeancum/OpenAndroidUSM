#include "audio/VoxSoundTable.hpp"

#include "filesystem/GbmpArchive.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstdint>
#include <limits>

namespace usm::audio {
namespace {

std::string normalizedEventName(std::string_view value) {
    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::toupper(character));
                   });
    return normalized;
}

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

Result VoxSoundTable::load(const std::filesystem::path& gameDataRoot) {
    filesystem::GbmpArchive configs;
    Result result = configs.open(gameDataRoot / "configs.pack");
    if (!result) {
        return result;
    }
    std::vector<std::byte> bytes;
    result = configs.read("VoxSounds.bin", bytes);
    return result ? load(bytes) : result;
}

Result VoxSoundTable::load(std::span<const std::byte> bytes) {
    records_.clear();
    Reader reader(bytes);
    std::uint16_t recordCount{};
    if (!reader.u16(recordCount)) {
        return Result::failure("VoxSounds record count is truncated");
    }
    records_.reserve(recordCount);
    for (std::uint16_t index = 0; index < recordCount; ++index) {
        VoxSoundRecord record;
        std::int16_t flag18{};
        std::int16_t distanceCullingEnabled{};
        if (!reader.u16(record.id) || !reader.string(record.eventName) ||
            !reader.string(record.resourcePath) ||
            !reader.s16(record.groupId) ||
            !reader.s16(record.maximumInstances) || !reader.f32(record.volume) ||
            !reader.s16(flag18) || !reader.f32(record.minimumDistance) ||
            !reader.f32(record.maximumDistance) ||
            !reader.s16(distanceCullingEnabled) ||
            !reader.s16(record.parameter28) ||
            !reader.s16(record.parameter2c)) {
            records_.clear();
            return Result::failure("VoxSounds record is truncated");
        }
        if (record.id != index || record.eventName.empty() ||
            record.resourcePath.empty() || (flag18 != 0 && flag18 != 1) ||
            (distanceCullingEnabled != 0 && distanceCullingEnabled != 1)) {
            records_.clear();
            return Result::failure("VoxSounds record contains invalid fields");
        }
        record.flag18 = flag18 == 1;
        record.distanceCullingEnabled = distanceCullingEnabled == 1;
        records_.push_back(std::move(record));
    }
    if (reader.remaining() != 0) {
        records_.clear();
        return Result::failure("VoxSounds contains unexpected trailing data");
    }
    return Result::success();
}

const VoxSoundRecord* VoxSoundTable::find(
    std::string_view eventName) const noexcept {
    const std::string normalized = normalizedEventName(eventName);
    const auto match = std::find_if(
        records_.begin(), records_.end(), [&normalized](const auto& record) {
            return normalizedEventName(record.eventName) == normalized;
        });
    return match == records_.end() ? nullptr : &*match;
}

} // namespace usm::audio
