#pragma once

#include "core/Result.hpp"
#include "game/LevelOneBootstrap.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace usm::game {

enum class LevelDropPhase {
    Dormant,
    Delay,
    Falling,
    Hidden,
};

struct LevelDropObjectState {
    const LevelDropObjectAsset* asset{};
    assets::Vector3 position;
    float downwardVelocity{};
    std::int32_t delayRemainingMilliseconds{};
    LevelDropPhase phase{LevelDropPhase::Dormant};
    bool visible{};
    bool physicsEnabled{};
    bool hitPlayer{};
};

enum class LevelDropEventKind {
    Activated,
    BeganFalling,
    HitPlayer,
};

struct LevelDropEvent {
    LevelDropEventKind kind{LevelDropEventKind::Activated};
    std::int32_t objectId{-1};
    std::int32_t roomId{-1};
    assets::Vector3 position;
    std::string effectType;
    float damage{};
};

// Portable state behind CDropArea::Update (0x00309dc0) and
// CDropObject::Update (0x00309bb4). Areas activate once; their linked props
// become visible, wait the authored delay, then fall and can hit the player.
class LevelDropRuntime final {
public:
    [[nodiscard]] Result initialize(
        std::span<const LevelDropAreaAsset> areas,
        std::span<const LevelDropObjectAsset> objects);
    void update(const assets::Vector3& playerPosition,
                std::uint32_t elapsedMilliseconds) noexcept;

    [[nodiscard]] std::span<const LevelDropObjectState> states() const
        noexcept {
        return states_;
    }
    [[nodiscard]] std::vector<LevelDropEvent> consumeEvents();

private:
    struct AreaState {
        const LevelDropAreaAsset* asset{};
        bool activated{};
    };

    static bool areaContainsPlayer(const LevelDropAreaAsset& area,
                                   const assets::Vector3& player) noexcept;
    static bool objectHitsPlayer(const LevelDropObjectAsset& object,
                                 const assets::Vector3& objectPosition,
                                 const assets::Vector3& player) noexcept;

    std::vector<AreaState> areas_;
    std::vector<LevelDropObjectState> states_;
    std::vector<LevelDropEvent> events_;
};

} // namespace usm::game
