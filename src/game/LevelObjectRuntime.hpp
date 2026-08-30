#pragma once

#include "core/Result.hpp"
#include "game/CinematicScript.hpp"
#include "game/LevelOneBootstrap.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace usm::game {

struct LevelObjectState {
    const LevelObjectAsset* asset{};
    assets::Vector3 position;
    std::array<float, 16> worldTransform{};
    std::string activeAnimation;
    std::uint32_t animationTimeMilliseconds{};
    float animationSpeed{1.0F};
    bool animationLoops{true};
    bool visible{true};
    bool physicsEnabled{};
};

// Portable state for the concrete CAnimatedObject, CDestroyableObject,
// CStaticObject and CDestroyableStreamPiping instances authored in room IRR
// files. It owns only gameplay state; mesh resources remain in the bootstrap.
class LevelObjectRuntime final {
public:
    [[nodiscard]] Result initialize(const LevelOneBootstrap& level);
    void advanceAnimations(std::uint32_t elapsedMilliseconds) noexcept;
    [[nodiscard]] Result applyCinematicCommand(
        const LevelOneBootstrap& level, const CinematicThread& thread,
        const CinematicCommand& command);

    [[nodiscard]] std::span<const LevelObjectState> states() const noexcept {
        return states_;
    }
    [[nodiscard]] const LevelObjectState* find(std::int32_t objectId) const
        noexcept;

private:
    [[nodiscard]] LevelObjectState* findMutable(std::int32_t objectId) noexcept;

    std::vector<LevelObjectState> states_;
};

} // namespace usm::game
