#pragma once

#include "assets/IrrScene.hpp"
#include "core/Result.hpp"
#include "game/LevelOneBootstrap.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace usm::game {

enum class CheckPointPlacementKind {
    SavedPlayerTransform,
    AuthoredNode,
    LinkedWaypoint,
};

struct CheckPointActivation {
    std::int32_t objectId{-1};
    std::int32_t cameraAreaId{-1};
    assets::Vector3 playerPosition;
    assets::Vector3 playerFacing{1.0F, 0.0F, 0.0F};
    bool automatic{};
};

struct CheckPointRestartPlacement {
    std::int32_t objectId{-1};
    std::int32_t cameraAreaId{-1};
    assets::Vector3 playerPosition;
    assets::Vector3 playerFacing{1.0F, 0.0F, 0.0F};
    CheckPointPlacementKind kind{CheckPointPlacementKind::AuthoredNode};
    bool faceCameraAfterPlacement{};
};

// Portable CCheckPoint state reconstructed from ProcessUserAttr/SaveData/
// Update/Init (0x0036919c, 0x00369228, 0x00369280, 0x0036941c).
// Restart placement follows CLevel::RestartAtCheckPoint (0x0038344c).
class LevelCheckPointRuntime final {
public:
    [[nodiscard]] Result bind(
        std::span<const LevelCheckPointAsset> checkPoints,
        std::span<const LevelWayPointAsset> waypoints);
    [[nodiscard]] std::optional<CheckPointActivation> update(
        const assets::Vector3& playerPosition,
        const assets::Vector3& playerFacing, float playerHealth,
        std::int32_t cameraAreaId) noexcept;
    [[nodiscard]] Result save(std::int32_t checkPointId,
                              std::int32_t cameraAreaId,
                              const assets::Vector3& playerPosition,
                              const assets::Vector3& playerFacing);

    // CLevel::RestartLevel (0x00382b10) invokes CCheckPoint::Reset
    // (0x0036918c): re-arm each volume and restore its authored Enabled flag,
    // while leaving its saved transform data intact.
    void resetForLevelRestart() noexcept;

    [[nodiscard]] std::optional<CheckPointRestartPlacement>
    restartPlacement() const noexcept;
    [[nodiscard]] std::int32_t lastCheckPointId() const noexcept {
        return lastCheckPointId_;
    }

private:
    struct State {
        const LevelCheckPointAsset* asset{};
        bool enabled{};
        bool armed{true};
        bool hasSavedData{};
        std::int32_t savedCameraAreaId{-1};
        assets::Vector3 savedPosition;
        assets::Vector3 savedFacing{1.0F, 0.0F, 0.0F};
    };

    [[nodiscard]] bool containsPlayer(
        const LevelCheckPointAsset& checkPoint,
        const assets::Vector3& playerPosition) const noexcept;
    [[nodiscard]] const LevelWayPointAsset* findWayPoint(
        std::int32_t objectId) const noexcept;

    std::vector<State> states_;
    std::span<const LevelWayPointAsset> waypoints_;
    std::int32_t lastCheckPointId_{-1};
};

} // namespace usm::game
