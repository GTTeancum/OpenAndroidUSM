#pragma once

#include <algorithm>
#include <cstdint>

namespace usm::game {

// CQTEManager::onEvent/Update (0x0038abf0/0x0038b240): tap events reduce
// the remaining count, and mash state 2 restores one action after 500 ms
// without a tap. This timer is separate from the whole prompt's timeout.
class ButtonMashProgress final {
public:
    void reset() noexcept {
        completed_ = 0;
        idleMilliseconds_ = 0;
        managerIdleMilliseconds_ = 0.0F;
    }

    [[nodiscard]] bool update(std::uint32_t elapsedMilliseconds,
                              bool actionPressed, std::int16_t required,
                              bool decays) noexcept {
        const auto count = std::max<std::int16_t>(required, 1);
        if (actionPressed) {
            completed_ = static_cast<std::int16_t>(
                std::min<int>(count, static_cast<int>(completed_) + 1));
            idleMilliseconds_ = 0;
        }
        if (completed_ >= count) {
            return true;
        }
        if (decays) {
            idleMilliseconds_ = static_cast<std::uint32_t>(std::min<std::uint64_t>(500,
                static_cast<std::uint64_t>(idleMilliseconds_) + elapsedMilliseconds));
            if (idleMilliseconds_ >= 500 && completed_ > 0) {
                --completed_;
                idleMilliseconds_ = 0;
            }
        }
        return false;
    }

    // CQTEManager state 2 does not return early after CheckSuccess: its
    // float idle timer still advances, can restore one action, and is then
    // followed by IsOutTime. Keep that ordering in the manager callers.
    // Legacy wall-web/QTEAction callers remain on update() until their
    // complete caller/manager split has been audited.
    [[nodiscard]] bool updateManager(std::uint32_t realMilliseconds,
                                     bool actionPressed, std::int16_t required,
                                     bool decays) noexcept {
        const auto count = std::max<std::int16_t>(required, 1);
        bool succeeded = false;
        if (actionPressed) {
            completed_ = static_cast<std::int16_t>(
                std::min<int>(count, static_cast<int>(completed_) + 1));
            managerIdleMilliseconds_ = 0.0F;
            succeeded = completed_ >= count;
        }
        if (decays) {
            managerIdleMilliseconds_ += static_cast<float>(realMilliseconds);
            if (managerIdleMilliseconds_ >= 500.0F && completed_ > 0) {
                --completed_;
                managerIdleMilliseconds_ = 0.0F;
            }
        }
        return succeeded;
    }

    [[nodiscard]] std::int16_t completed() const noexcept { return completed_; }

private:
    std::int16_t completed_{};
    std::uint32_t idleMilliseconds_{};
    float managerIdleMilliseconds_{};
};

} // namespace usm::game
