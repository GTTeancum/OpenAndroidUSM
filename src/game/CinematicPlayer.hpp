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

// Deterministic native scheduler for CCinematicThread commands. Time is
// supplied by the caller, keeping playback testable and independent of Win32.
class CinematicPlayer final {
public:
    [[nodiscard]] Result start(const CinematicScript& script);
    [[nodiscard]] Result advanceTo(std::uint32_t elapsedMilliseconds,
                                   const CinematicCommandHandler& handler);
    void reset() noexcept;

    [[nodiscard]] bool finished() const noexcept {
        return nextCommand_ == schedule_.size();
    }
    [[nodiscard]] std::uint32_t durationMilliseconds() const noexcept {
        return durationMilliseconds_;
    }
    [[nodiscard]] std::size_t dispatchedCommandCount() const noexcept {
        return nextCommand_;
    }

private:
    struct ScheduledCommand {
        const CinematicThread* thread{};
        const CinematicCommand* command{};
    };

    std::vector<ScheduledCommand> schedule_;
    std::size_t nextCommand_{};
    std::uint32_t elapsedMilliseconds_{};
    std::uint32_t durationMilliseconds_{};
    bool started_{};
};

} // namespace usm::game
