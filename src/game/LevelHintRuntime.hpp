#pragma once

#include "core/Result.hpp"
#include "game/CinematicScript.hpp"
#include "game/LevelOneBootstrap.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace usm::game {

struct LevelHintState {
    const LevelHintAsset* asset{};
    assets::Vector3 position;
    std::int32_t animationIndex{};
    std::uint32_t animationTimeMilliseconds{};
    std::int32_t animationFrameIndex{-1};
    std::int32_t frameIndex{-1};
    bool visible{};
    // HintManager::GetSenseHint owns a second, runtime-only Hint using the
    // same hintbb animation as the authored tutorial node. Keep it distinct
    // so cinematic SetVisible commands cannot hide an active attack warning.
    bool combatSenseCue{};
    // Player::SpawnPlayer creates a second runtime-only hintbb instance for
    // the current enemy target. Player::UpdateTargetPointer switches this
    // instance among animations 4, 5, and 6 as target health falls.
    bool combatTargetCue{};
    std::int32_t combatTargetObjectId{-1};
};

// Portable state behind Hint::Update (0x0033db20),
// HintBase::UpdatePosition (0x0033e414), and CSpriteInstance's fixed 50 ms
// animation tick (0x002e9c10). Rendering remains backend-owned.
class LevelHintRuntime final {
public:
    using PositionResolver =
        std::function<std::optional<assets::Vector3>(std::int32_t)>;

    [[nodiscard]] Result initialize(std::span<const LevelHintAsset> hints);
    [[nodiscard]] Result applyCinematicCommand(
        const CinematicThread& thread, const CinematicCommand& command);
    void update(std::uint32_t elapsedMilliseconds,
                const PositionResolver& resolvePosition) noexcept;
    void setCombatSenseCueVisible(bool visible) noexcept;
    [[nodiscard]] bool setCombatTargetCue(
        std::int32_t objectId, const assets::Vector3& position,
        float health, float maximumHealth) noexcept;
    [[nodiscard]] bool clearCombatTargetCue() noexcept;

    [[nodiscard]] bool combatSenseCueVisible() const noexcept;
    [[nodiscard]] const LevelHintState* combatTargetCue() const noexcept;

    [[nodiscard]] const std::vector<LevelHintState>& states() const noexcept {
        return states_;
    }
    [[nodiscard]] const LevelHintState* find(std::int32_t objectId) const
        noexcept;

private:
    [[nodiscard]] LevelHintState* findMutable(std::int32_t objectId) noexcept;
    [[nodiscard]] LevelHintState* combatTargetCueMutable() noexcept;
    static void updateFrame(LevelHintState& state) noexcept;

    std::vector<LevelHintState> states_;
};

} // namespace usm::game
