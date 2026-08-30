#pragma once

#include "game/LevelOneBootstrap.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace usm::game {

struct LevelDamageEvent {
    std::int32_t objectId{-1};
    std::int32_t damageType{};
    float damage{};
};

// Portable state behind CEffectDamage::Update (0x00368a80). These authored
// nodes are invisible oriented volumes that repeatedly hurt a hittable player
// after the native 1000 ms reaction window expires.
class LevelDamageRuntime final {
public:
    void bind(std::span<const LevelDamageAsset> assets) noexcept;
    void update(const assets::Vector3& playerPosition,
                std::uint32_t elapsedMilliseconds) noexcept;

    [[nodiscard]] std::vector<LevelDamageEvent> consumeEvents();
    [[nodiscard]] std::uint32_t cooldownRemainingMilliseconds() const
        noexcept {
        return cooldownRemainingMilliseconds_;
    }

private:
    static bool containsPlayer(const LevelDamageAsset& asset,
                               const assets::Vector3& player) noexcept;

    std::span<const LevelDamageAsset> assets_;
    std::vector<LevelDamageEvent> events_;
    std::uint32_t cooldownRemainingMilliseconds_{};
};

} // namespace usm::game
