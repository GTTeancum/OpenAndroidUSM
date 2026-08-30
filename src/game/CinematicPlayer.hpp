#pragma once

#include "core/Result.hpp"
#include "game/CinematicScript.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace usm::game {

using CinematicCommandHandler =
    std::function<void(const CinematicThread&, const CinematicCommand&)>;
using ConditionalCinematicCommandHandler =
    std::function<bool(const CinematicThread&, const CinematicCommand&)>;

// Deterministic native scheduler for CCinematicThread commands. Time is
// supplied by the caller, keeping playback testable and independent of Win32.
class CinematicPlayer final {
public:
    [[nodiscard]] Result start(const CinematicScript& script);
    [[nodiscard]] Result advanceTo(std::uint32_t elapsedMilliseconds,
                                   const CinematicCommandHandler& handler);
    // A false result leaves that command and the rest of its original thread
    // pending. This reconstructs CCinematicThread::executeCommand
    // (0x00372aa4), whose If* commands block only their owning thread.
    [[nodiscard]] Result advanceToConditional(
        std::uint32_t elapsedMilliseconds,
        const ConditionalCinematicCommandHandler& handler);
    void reset() noexcept;

    [[nodiscard]] bool finished() const noexcept {
        return dispatchedCommandCount_ == schedule_.size();
    }
    [[nodiscard]] std::uint32_t durationMilliseconds() const noexcept {
        return durationMilliseconds_;
    }
    [[nodiscard]] std::size_t dispatchedCommandCount() const noexcept {
        return dispatchedCommandCount_;
    }

private:
    struct ScheduledCommand {
        const CinematicThread* thread{};
        const CinematicCommand* command{};
        bool dispatched{};
    };

    std::vector<ScheduledCommand> schedule_;
    std::size_t dispatchedCommandCount_{};
    std::uint32_t elapsedMilliseconds_{};
    std::uint32_t durationMilliseconds_{};
    bool started_{};
};

} // namespace usm::game
