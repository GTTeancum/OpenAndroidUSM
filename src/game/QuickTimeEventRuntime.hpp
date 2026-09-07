#pragma once

#include "core/Result.hpp"
#include "game/ButtonConfig.hpp"
#include "game/ButtonMashProgress.hpp"
#include "game/CinematicScript.hpp"

#include <cstdint>
#include <optional>

namespace usm::game {

// Renderer-independent controller reconstruction of Player::BeginQTE
// (0x0034c9b4) and CQTEManager::BeginQTE/SuccessHandle/FailHandle
// (0x0038ab40/0x0038a5e4/0x0038a630).
class QuickTimeEventRuntime final {
public:
    void bind(const ButtonConfigDatabase& configs) noexcept;
    [[nodiscard]] Result applyCommand(const CinematicCommand& command,
                                      std::int32_t sourceCinematicId = -1);
    void update(std::uint32_t elapsedMilliseconds, bool actionPressed) noexcept;

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] std::int32_t configId() const noexcept { return configId_; }
    [[nodiscard]] std::int32_t sourceCinematicId() const noexcept {
        return sourceCinematicId_;
    }
    [[nodiscard]] std::uint32_t elapsedMilliseconds() const noexcept {
        return elapsedMilliseconds_;
    }
    [[nodiscard]] std::uint32_t durationMilliseconds() const noexcept {
        return durationMilliseconds_;
    }
    [[nodiscard]] std::int16_t completedActionCount() const noexcept {
        return buttonProgress_.completed();
    }
    [[nodiscard]] std::optional<std::int32_t>
    consumeCinematicRequest() noexcept;

private:
    const ButtonConfigDatabase* configs_{};
    std::int32_t configId_{-1};
    std::int32_t sourceCinematicId_{-1};
    std::int32_t successCinematicId_{-1};
    std::int32_t failureCinematicId_{-1};
    std::uint32_t elapsedMilliseconds_{};
    std::uint32_t durationMilliseconds_{};
    std::optional<std::int32_t> cinematicRequest_;
    ButtonMashProgress buttonProgress_;
    bool active_{};
};

} // namespace usm::game
