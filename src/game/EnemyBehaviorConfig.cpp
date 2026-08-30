#include "game/EnemyBehaviorConfig.hpp"

#include "filesystem/GbmpArchive.hpp"

#include <algorithm>
#include <limits>

namespace usm::game {
namespace {

class Reader final {
public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    bool s16(std::int16_t& output) noexcept {
        std::uint16_t bits{};
        if (!integer(bits)) {
            return false;
        }
        output = static_cast<std::int16_t>(bits);
        return true;
    }

    bool s32(std::int32_t& output) noexcept {
        std::uint32_t bits{};
        if (!integer(bits)) {
            return false;
        }
        output = static_cast<std::int32_t>(bits);
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
    template <typename Integer>
    bool integer(Integer& output) noexcept {
        if (sizeof(Integer) > remaining()) {
            return false;
        }
        output = 0;
        for (std::size_t index = 0; index < sizeof(Integer); ++index) {
            output |= static_cast<Integer>(
                          std::to_integer<std::uint8_t>(bytes_[offset_ + index]))
                      << (index * 8);
        }
        offset_ += sizeof(Integer);
        return true;
    }

    std::span<const std::byte> bytes_;
    std::size_t offset_{};
};

bool validCount(std::int16_t count, std::int16_t maximum = 4096) noexcept {
    return count >= 0 && count <= maximum;
}

bool validBoolean(std::int32_t value) noexcept {
    return value == 0 || value == 1;
}

} // namespace

Result EnemyBehaviorConfigDatabase::load(
    const std::filesystem::path& gameDataRoot) {
    filesystem::GbmpArchive configs;
    Result result = configs.open(gameDataRoot / "configs.pack");
    if (!result) {
        return result;
    }
    std::vector<std::byte> bytes;
    const auto loadEntry = [&configs, &bytes](std::string_view name,
                                              auto&& parse) -> Result {
        Result entryResult = configs.read(name, bytes);
        if (!entryResult) {
            return entryResult;
        }
        entryResult = parse(bytes);
        return entryResult ? entryResult
                           : Result::failure("Could not decode " +
                                             std::string(name) + ": " +
                                             entryResult.message());
    };
    result = loadEntry("BehaviorAnimMapList.bin", [this](auto data) {
        return loadAnimationMaps(data);
    });
    if (!result) {
        return result;
    }
    result = loadEntry("BehaviorAnimList.bin", [this](auto data) {
        return loadAnimationLists(data);
    });
    if (!result) {
        return result;
    }
    result = loadEntry("BehaviorSoundMapList.bin", [this](auto data) {
        return loadSoundMaps(data);
    });
    if (!result) {
        return result;
    }
    return loadEntry("BehaviorState.bin", [this](auto data) {
        return loadStates(data);
    });
}

Result EnemyBehaviorConfigDatabase::loadAnimationMaps(
    std::span<const std::byte> bytes) {
    animationMaps_.clear();
    Reader reader(bytes);
    std::int16_t count{};
    if (!reader.s16(count) || !validCount(count)) {
        return Result::failure("Animation-map count is invalid");
    }
    animationMaps_.reserve(static_cast<std::size_t>(count));
    for (std::int16_t index = 0; index < count; ++index) {
        EnemyBehaviorAnimationMap map;
        if (!reader.s16(map.id) || !reader.string(map.name)) {
            animationMaps_.clear();
            return Result::failure("Animation-map record is truncated");
        }
        for (std::string& animationName : map.animationNames) {
            if (!reader.string(animationName)) {
                animationMaps_.clear();
                return Result::failure("Animation-map columns are truncated");
            }
        }
        if (map.id != index || map.name.empty()) {
            animationMaps_.clear();
            return Result::failure("Animation-map ID or name is invalid");
        }
        animationMaps_.push_back(std::move(map));
    }
    if (reader.remaining() != 0) {
        animationMaps_.clear();
        return Result::failure("Animation maps contain trailing data");
    }
    return Result::success();
}

Result EnemyBehaviorConfigDatabase::loadAnimationLists(
    std::span<const std::byte> bytes) {
    animationLists_.clear();
    Reader reader(bytes);
    std::int16_t count{};
    if (!reader.s16(count) || !validCount(count)) {
        return Result::failure("Animation-list count is invalid");
    }
    animationLists_.reserve(static_cast<std::size_t>(count));
    for (std::int16_t index = 0; index < count; ++index) {
        EnemyBehaviorAnimationList list;
        std::int16_t mapCount{};
        if (!reader.s16(list.id) || !reader.string(list.name) ||
            !reader.s32(list.selectionMode) || !reader.s16(mapCount) ||
            !validCount(mapCount)) {
            animationLists_.clear();
            return Result::failure("Animation-list record is truncated");
        }
        list.animationMapIds.reserve(static_cast<std::size_t>(mapCount));
        for (std::int16_t mapIndex = 0; mapIndex < mapCount; ++mapIndex) {
            std::int32_t mapId{};
            if (!reader.s32(mapId)) {
                animationLists_.clear();
                return Result::failure("Animation-list maps are truncated");
            }
            list.animationMapIds.push_back(mapId);
        }
        if (list.id != index || list.name.empty()) {
            animationLists_.clear();
            return Result::failure("Animation-list ID or name is invalid");
        }
        animationLists_.push_back(std::move(list));
    }
    if (reader.remaining() != 0) {
        animationLists_.clear();
        return Result::failure("Animation lists contain trailing data");
    }
    return Result::success();
}

Result EnemyBehaviorConfigDatabase::loadSoundMaps(
    std::span<const std::byte> bytes) {
    soundMaps_.clear();
    Reader reader(bytes);
    std::int16_t count{};
    if (!reader.s16(count) || !validCount(count)) {
        return Result::failure("Sound-map count is invalid");
    }
    soundMaps_.reserve(static_cast<std::size_t>(count));
    for (std::int16_t index = 0; index < count; ++index) {
        EnemyBehaviorSoundMap map;
        if (!reader.s16(map.id) || !reader.string(map.name)) {
            soundMaps_.clear();
            return Result::failure("Sound-map record is truncated");
        }
        for (std::int32_t& voxSoundId : map.voxSoundIds) {
            if (!reader.s32(voxSoundId)) {
                soundMaps_.clear();
                return Result::failure("Sound-map columns are truncated");
            }
        }
        if (map.id != index || map.name.empty()) {
            soundMaps_.clear();
            return Result::failure("Sound-map ID or name is invalid");
        }
        soundMaps_.push_back(std::move(map));
    }
    if (reader.remaining() != 0) {
        soundMaps_.clear();
        return Result::failure("Sound maps contain trailing data");
    }
    return Result::success();
}

Result EnemyBehaviorConfigDatabase::loadStates(
    std::span<const std::byte> bytes) {
    states_.clear();
    Reader reader(bytes);
    std::int16_t count{};
    if (!reader.s16(count) || !validCount(count)) {
        return Result::failure("Behavior-state count is invalid");
    }
    states_.reserve(static_cast<std::size_t>(count));
    for (std::int16_t index = 0; index < count; ++index) {
        EnemyBehaviorStateDefinition state;
        std::int32_t looping{};
        std::int32_t parameter1{};
        std::int32_t stopPreviousSound{};
        std::int16_t animationCount{};
        std::int16_t soundCount{};
        if (!reader.s16(state.id) || !reader.string(state.name) ||
            !reader.s32(looping) || !reader.s32(state.parameter0) ||
            !reader.s32(state.animationSelectionMode) ||
            !reader.s16(animationCount) || !validCount(animationCount)) {
            states_.clear();
            return Result::failure("Behavior-state record is truncated");
        }
        state.animationListIds.reserve(
            static_cast<std::size_t>(animationCount));
        for (std::int16_t animationIndex = 0;
             animationIndex < animationCount; ++animationIndex) {
            std::int16_t animationListId{};
            if (!reader.s16(animationListId)) {
                states_.clear();
                return Result::failure(
                    "Behavior-state animation lists are truncated");
            }
            state.animationListIds.push_back(animationListId);
        }
        if (!reader.s16(soundCount) || !validCount(soundCount)) {
            states_.clear();
            return Result::failure("Behavior-state sound count is invalid");
        }
        state.soundMapIds.reserve(static_cast<std::size_t>(soundCount));
        for (std::int16_t soundIndex = 0; soundIndex < soundCount;
             ++soundIndex) {
            std::int16_t soundMapId{};
            if (!reader.s16(soundMapId)) {
                states_.clear();
                return Result::failure(
                    "Behavior-state sound maps are truncated");
            }
            state.soundMapIds.push_back(soundMapId);
        }
        if (!reader.s32(parameter1) || !reader.s32(stopPreviousSound) ||
            !validBoolean(looping) || !validBoolean(parameter1) ||
            !validBoolean(stopPreviousSound) || state.id != index ||
            state.name.empty()) {
            states_.clear();
            return Result::failure("Behavior-state fields are invalid");
        }
        state.looping = looping != 0;
        state.parameter1 = parameter1 != 0;
        state.stopPreviousSound = stopPreviousSound != 0;
        states_.push_back(std::move(state));
    }
    if (reader.remaining() != 0) {
        states_.clear();
        return Result::failure("Behavior states contain trailing data");
    }
    return Result::success();
}

const EnemyBehaviorStateDefinition* EnemyBehaviorConfigDatabase::findState(
    std::string_view name) const noexcept {
    const auto match = std::find_if(
        states_.begin(), states_.end(), [name](const auto& state) {
            return state.name == name;
        });
    return match == states_.end() ? nullptr : &*match;
}

const EnemyBehaviorSoundMap* EnemyBehaviorConfigDatabase::findSoundMap(
    std::int16_t id) const noexcept {
    if (id < 0 || static_cast<std::size_t>(id) >= soundMaps_.size() ||
        soundMaps_[static_cast<std::size_t>(id)].id != id) {
        return nullptr;
    }
    return &soundMaps_[static_cast<std::size_t>(id)];
}

std::int32_t EnemyBehaviorConfigDatabase::resolveSoundMap(
    std::int16_t soundMapId, std::int16_t enemyTypeId) const noexcept {
    const EnemyBehaviorSoundMap* map = findSoundMap(soundMapId);
    if (map == nullptr || enemyTypeId < 0 ||
        static_cast<std::size_t>(enemyTypeId) >= kEnemyBehaviorTypeCount) {
        return -1;
    }
    return map->voxSoundIds[static_cast<std::size_t>(enemyTypeId)];
}

std::vector<std::int32_t> EnemyBehaviorConfigDatabase::resolveStateSoundIds(
    std::string_view stateName, std::int16_t enemyTypeId) const {
    std::vector<std::int32_t> result;
    const EnemyBehaviorStateDefinition* state = findState(stateName);
    if (state == nullptr) {
        return result;
    }
    for (const std::int16_t soundMapId : state->soundMapIds) {
        const std::int32_t voxSoundId =
            resolveSoundMap(soundMapId, enemyTypeId);
        if (voxSoundId >= 0) {
            result.push_back(voxSoundId);
        }
    }
    return result;
}

std::vector<std::string_view>
EnemyBehaviorConfigDatabase::resolveStateAnimationNames(
    std::string_view stateName, std::int16_t enemyTypeId) const {
    std::vector<std::string_view> result;
    if (enemyTypeId < 0 ||
        static_cast<std::size_t>(enemyTypeId) >= kEnemyBehaviorTypeCount) {
        return result;
    }
    const EnemyBehaviorStateDefinition* state = findState(stateName);
    if (state == nullptr) {
        return result;
    }
    for (const std::int16_t animationListId : state->animationListIds) {
        const EnemyBehaviorAnimationList* list =
            findAnimationList(animationListId);
        if (list == nullptr) {
            continue;
        }
        for (const std::int32_t mapId : list->animationMapIds) {
            const EnemyBehaviorAnimationMap* map = findAnimationMap(mapId);
            if (map == nullptr) {
                continue;
            }
            const std::string& animationName =
                map->animationNames[static_cast<std::size_t>(enemyTypeId)];
            if (!animationName.empty()) {
                result.push_back(animationName);
            }
        }
    }
    return result;
}

const EnemyBehaviorAnimationMap*
EnemyBehaviorConfigDatabase::findAnimationMap(std::int32_t id) const noexcept {
    if (id < 0 || static_cast<std::size_t>(id) >= animationMaps_.size() ||
        animationMaps_[static_cast<std::size_t>(id)].id != id) {
        return nullptr;
    }
    return &animationMaps_[static_cast<std::size_t>(id)];
}

const EnemyBehaviorAnimationList*
EnemyBehaviorConfigDatabase::findAnimationList(std::int16_t id) const noexcept {
    if (id < 0 || static_cast<std::size_t>(id) >= animationLists_.size() ||
        animationLists_[static_cast<std::size_t>(id)].id != id) {
        return nullptr;
    }
    return &animationLists_[static_cast<std::size_t>(id)];
}

} // namespace usm::game
