#include "game/CinematicPlayer.hpp"

#include <algorithm>

namespace usm::game {

Result CinematicPlayer::start(const CinematicScript& script) {
    reset();
    for (const CinematicThread& thread : script.threads()) {
        for (const CinematicCommand& command : thread.commands) {
            schedule_.push_back({&thread, &command});
        }
    }
    if (schedule_.empty()) {
        return Result::failure("Cannot start an empty cinematic script");
    }
    std::stable_sort(
        schedule_.begin(), schedule_.end(),
        [](const ScheduledCommand& left, const ScheduledCommand& right) {
            return left.command->timestampMilliseconds <
                   right.command->timestampMilliseconds;
        });
    durationMilliseconds_ = schedule_.back().command->timestampMilliseconds;
    started_ = true;
    return Result::success();
}

Result CinematicPlayer::advanceTo(
    std::uint32_t elapsedMilliseconds,
    const CinematicCommandHandler& handler) {
    if (!started_) {
        return Result::failure("Cinematic playback has not been started");
    }
    if (elapsedMilliseconds < elapsedMilliseconds_) {
        return Result::failure("Cinematic playback time cannot move backwards");
    }
    elapsedMilliseconds_ = elapsedMilliseconds;
    while (nextCommand_ < schedule_.size() &&
           schedule_[nextCommand_].command->timestampMilliseconds <=
               elapsedMilliseconds) {
        const ScheduledCommand& scheduled = schedule_[nextCommand_];
        handler(*scheduled.thread, *scheduled.command);
        ++nextCommand_;
    }
    return Result::success();
}

void CinematicPlayer::reset() noexcept {
    schedule_.clear();
    nextCommand_ = 0;
    elapsedMilliseconds_ = 0;
    durationMilliseconds_ = 0;
    started_ = false;
}

} // namespace usm::game
