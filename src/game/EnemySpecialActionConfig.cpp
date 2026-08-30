#include "game/EnemySpecialActionConfig.hpp"

#include "filesystem/GbmpArchive.hpp"

#include <algorithm>
#include <utility>

namespace usm::game {
namespace {

class ConfigReader final {
public:
    explicit ConfigReader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    [[nodiscard]] bool readS16(std::int16_t& value) noexcept {
        std::uint16_t bits{};
        if (!readLittleEndian(bits)) {
            return false;
        }
        value = static_cast<std::int16_t>(bits);
        return true;
    }

    [[nodiscard]] bool readS32(std::int32_t& value) noexcept {
        std::uint32_t bits{};
        if (!readLittleEndian(bits)) {
            return false;
        }
        value = static_cast<std::int32_t>(bits);
        return true;
    }

    [[nodiscard]] bool readString(std::string& value) {
        std::int16_t length{};
        if (!readS16(length) || length < 0 ||
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
    [[nodiscard]] bool readLittleEndian(Integer& value) noexcept {
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

Result EnemySpecialActionConfigDatabase::load(
    const std::filesystem::path& gameDataRoot) {
    filesystem::GbmpArchive configs;
    Result result = configs.open(gameDataRoot / "configs.pack");
    if (!result) {
        return result;
    }
    std::vector<std::byte> bytes;
    result = configs.read("EnemysSpecialAnimConfigs.bin", bytes);
    return result ? load(bytes) : result;
}

Result EnemySpecialActionConfigDatabase::load(
    std::span<const std::byte> bytes) {
    actions_.clear();
    ConfigReader reader(bytes);
    std::int16_t count{};
    if (!reader.readS16(count) || count < 0 || count > 4096) {
        return Result::failure("Enemy special-action config has an invalid count");
    }
    actions_.reserve(static_cast<std::size_t>(count));
    for (std::int16_t index = 0; index < count; ++index) {
        EnemyAnimationSpecialAction action;
        std::int16_t nextActionCount{};
        if (!reader.readS16(action.recordId) || !reader.readString(action.name) ||
            !reader.readS16(action.enemyTypeId) ||
            !reader.readString(action.animationName) ||
            !reader.readS32(action.actionType) ||
            !reader.readS32(action.keyFramePercent) ||
            !reader.readS32(action.attackId) ||
            !reader.readS16(nextActionCount) || nextActionCount < 0 ||
            nextActionCount > 4096) {
            actions_.clear();
            return Result::failure("Enemy special-action config is truncated");
        }
        action.soundMapIds.reserve(static_cast<std::size_t>(nextActionCount));
        for (std::int16_t nextIndex = 0; nextIndex < nextActionCount;
             ++nextIndex) {
            std::int16_t nextAction{};
            if (!reader.readS16(nextAction)) {
                actions_.clear();
                return Result::failure(
                    "Enemy special-action successor list is truncated");
            }
            action.soundMapIds.push_back(nextAction);
        }
        if (!reader.readString(action.effectName)) {
            actions_.clear();
            return Result::failure("Enemy special-action effect is truncated");
        }
        actions_.push_back(std::move(action));
    }
    if (!reader.finished()) {
        actions_.clear();
        return Result::failure("Enemy special-action config has trailing bytes");
    }
    return Result::success();
}

std::vector<const EnemyAnimationSpecialAction*>
EnemySpecialActionConfigDatabase::findAttackEvents(
    std::int16_t enemyTypeId, std::string_view animationName) const {
    auto matches = findEvents(enemyTypeId, animationName);
    std::erase_if(matches, [](const EnemyAnimationSpecialAction* action) {
        return action == nullptr || action->actionType != 0 ||
               action->attackId < 0;
    });
    return matches;
}

std::vector<const EnemyAnimationSpecialAction*>
EnemySpecialActionConfigDatabase::findEvents(
    std::int16_t enemyTypeId, std::string_view animationName) const {
    std::vector<const EnemyAnimationSpecialAction*> matches;
    for (const EnemyAnimationSpecialAction& action : actions_) {
        if (action.enemyTypeId == enemyTypeId &&
            action.animationName == animationName) {
            matches.push_back(&action);
        }
    }
    return matches;
}

} // namespace usm::game
