#pragma once

#include "game/LevelOneBootstrap.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace usm::game {

struct LevelRestoreEvent {
    const LevelRestoreTriggerAsset* trigger{};
    const LevelRestorePointAsset* restorePoint{};
};

// Portable state behind CTriggerRestore::Update (0x0036bf50) and Draw2D
// (0x0036bb84). A fall volume fades to black for 1280 ms, restores once,
// holds the opaque frame for 512 ms, then returns control.
class LevelRestoreRuntime final {
public:
    [[nodiscard]] Result bind(
        std::span<const LevelRestoreTriggerAsset> triggers,
        std::span<const LevelRestorePointAsset> restorePoints);
    void update(const assets::Vector3& playerPosition,
                std::uint32_t elapsedMilliseconds) noexcept;

    [[nodiscard]] std::vector<LevelRestoreEvent> consumeEvents();
    [[nodiscard]] float blackOverlayAlpha() const noexcept { return alpha_; }
    [[nodiscard]] bool active() const noexcept { return active_ != nullptr; }

private:
    static bool containsPlayer(const LevelRestoreTriggerAsset& trigger,
                               const assets::Vector3& player) noexcept;

    std::span<const LevelRestoreTriggerAsset> triggers_;
    std::span<const LevelRestorePointAsset> restorePoints_;
    const LevelRestoreTriggerAsset* active_{};
    std::uint32_t elapsedMilliseconds_{};
    float alpha_{};
    bool restored_{};
    std::vector<LevelRestoreEvent> events_;
};

} // namespace usm::game
