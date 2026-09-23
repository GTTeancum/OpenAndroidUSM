#include "game/QteCinematicHandoff.hpp"
#include "game/GameplayCinematicScheduler.hpp"

namespace usm::game {
Result dispatchQteCinematicHandoff(
    const QteCinematicHandoff& handoff,
    GameplayCinematicScheduler& scheduler,
    const std::function<void(const GameplayCinematicPlayback&)>& releaseSource,
    const std::function<Result(std::int32_t)>& startOutcome,
    const std::function<Result(std::uint32_t)>& advanceCinematics) {
    if (handoff.sourceCinematicId != -1) {
        if (const auto* source = scheduler.playback(handoff.sourceCinematicId)) {
            if (releaseSource) { releaseSource(*source); }
        }
        scheduler.remove(handoff.sourceCinematicId);
    }
    // Original FindCinematic miss is a no-op, not an invented fallback ID.
    if (handoff.outcomeCinematicId == -1 ||
        scheduler.findAsset(handoff.outcomeCinematicId) == nullptr) {
        return Result::success();
    }
    if (!startOutcome || !advanceCinematics) {
        return Result::failure("QTE outcome has no bound cinematic dispatch callbacks");
    }
    Result result = startOutcome(handoff.outcomeCinematicId);
    if (!result) { return result; }
    // ARM literal 0x42480000. Do not substitute scaled game delta, real delta,
    // next-frame scheduling, or an update of only the newly added outcome.
    return advanceCinematics(50);
}
} // namespace usm::game
