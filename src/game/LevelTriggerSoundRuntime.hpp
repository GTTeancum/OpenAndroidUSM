#pragma once

#include "assets/IrrScene.hpp"
#include "game/LevelOneBootstrap.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace usm::game {

enum class TriggerSoundEventKind {
    Started,
    Stopped,
};

struct TriggerSoundEvent {
    std::int32_t triggerId{-1};
    std::string_view eventName;
    TriggerSoundEventKind kind{TriggerSoundEventKind::Started};
};

// Source-level reconstruction of CTriggerSound::Init/Update/SetState at
// 0x0036c928, 0x0036cb14, and 0x0036ca34.
class LevelTriggerSoundRuntime final {
public:
    void bind(std::span<const LevelTriggerSoundAsset> triggers);
    [[nodiscard]] std::vector<TriggerSoundEvent> update(
        const assets::Vector3& playerPosition);

private:
    struct State {
        const LevelTriggerSoundAsset* asset{};
        bool playing{};
    };

    [[nodiscard]] static bool containsPlayer(
        const LevelTriggerSoundAsset& trigger,
        const assets::Vector3& playerPosition) noexcept;

    std::vector<State> states_;
};

} // namespace usm::game
