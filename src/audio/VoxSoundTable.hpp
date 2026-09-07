#pragma once

#include "core/Result.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace usm::audio {

struct VoxSoundRecord {
    std::uint16_t id{};
    std::string eventName;
    std::string resourcePath;
    std::int16_t groupId{};
    std::int16_t maximumInstances{};
    float volume{};
    bool flag18{};
    float minimumDistance{};
    float maximumDistance{};
    bool distanceCullingEnabled{};
    std::int16_t parameter28{};
    std::int16_t parameter2c{};
};

// Typed reconstruction of VoxSoundFile::LoadRecordFromFile (0x003da650) and
// VoxSoundFile::ReadBasicRecord (0x003da570).
class VoxSoundTable final {
public:
    [[nodiscard]] Result load(const std::filesystem::path& gameDataRoot);
    [[nodiscard]] Result load(std::span<const std::byte> bytes);

    [[nodiscard]] const VoxSoundRecord* find(
        std::string_view eventName) const noexcept;
    [[nodiscard]] const VoxSoundRecord* find(std::uint16_t id) const noexcept;
    [[nodiscard]] const std::vector<VoxSoundRecord>& records() const noexcept {
        return records_;
    }

private:
    std::vector<VoxSoundRecord> records_;
};

} // namespace usm::audio
