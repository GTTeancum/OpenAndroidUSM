#pragma once

#include "core/Result.hpp"

#include <cstdint>
#include <functional>

namespace usm::game {
class GameplayCinematicScheduler;
struct GameplayCinematicPlayback;

struct QteCinematicHandoff {
    std::int32_t sourceCinematicId{-1};
    std::int32_t outcomeCinematicId{-1};
};
using QteHandoffHandler = std::function<void(const QteCinematicHandoff&)>;

// SuccessHandle/FailHandle, ELF 0x37a5e4/0x37a630: remove source,
// find/add outcome, then update the WHOLE cinematic manager with float 50.0.
// The caller clears m_pIGMPressed BEFORE this and commits handled state AFTER.
// Callbacks execute synchronously and must not throw. Their lifetime need only
// span this call; no callbacks or world pointers are retained by the manager.
[[nodiscard]] Result dispatchQteCinematicHandoff(
    const QteCinematicHandoff& handoff,
    GameplayCinematicScheduler& scheduler,
    const std::function<void(const GameplayCinematicPlayback&)>& releaseSource,
    const std::function<Result(std::int32_t)>& startOutcome,
    const std::function<Result(std::uint32_t)>& advanceCinematics);
} // namespace usm::game
