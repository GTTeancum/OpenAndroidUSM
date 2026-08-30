#pragma once

#include "assets/IrrScene.hpp"
#include "game/LevelOneBootstrap.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace usm::game {

enum class TriggerEventKind {
    Entered,
    Exited,
    WhileInside,
    WhileOutside,
};

struct TriggerEvent {
    std::int32_t triggerId{-1};
    std::int32_t cinematicId{-1};
    TriggerEventKind kind{TriggerEventKind::Entered};
};

// Native runtime for CTrigger::Update (0x0036ac74). It evaluates the player's
// recovered 50 x 50 x 140 cm collision box against authored AABB/OBB volumes.
class LevelTriggerRuntime final {
public:
    void bind(std::span<const LevelTriggerAsset> triggers);
    [[nodiscard]] std::vector<TriggerEvent> update(
        const assets::Vector3& playerPosition,
        std::span<const bool> activeRooms = {});
    [[nodiscard]] bool setEnabled(std::int32_t triggerId,
                                  bool enabled) noexcept;
    [[nodiscard]] bool isEnabled(std::int32_t triggerId) const noexcept;

private:
    struct State {
        const LevelTriggerAsset* asset{};
        bool enabled{};
        bool initialized{};
        bool inside{};
        bool whileEventDispatched{};
    };

    [[nodiscard]] static bool containsPlayer(
        const LevelTriggerAsset& trigger,
        const assets::Vector3& playerPosition) noexcept;

    std::vector<State> states_;
};

} // namespace usm::game
