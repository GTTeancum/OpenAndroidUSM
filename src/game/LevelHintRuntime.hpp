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
    std::uint32_t animationTimeMilliseconds{};
    std::int32_t animationFrameIndex{-1};
    std::int32_t frameIndex{-1};
    bool visible{};
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

    [[nodiscard]] const std::vector<LevelHintState>& states() const noexcept {
        return states_;
    }
    [[nodiscard]] const LevelHintState* find(std::int32_t objectId) const
        noexcept;

private:
    [[nodiscard]] LevelHintState* findMutable(std::int32_t objectId) noexcept;
    static void updateFrame(LevelHintState& state) noexcept;

    std::vector<LevelHintState> states_;
};

} // namespace usm::game
