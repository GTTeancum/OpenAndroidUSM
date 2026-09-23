#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>

namespace usm::game {

// The two clocks read by CQTEManager::Update are not interchangeable:
//   Timer::getTime (ELF 0x0042aea0): absolute, shared by catch-up updates;
//   Application::GetRealTs (0x003ce5d4): unslowed update duration.
// Neither is the scaled simulation duration. Require both at call sites.
struct QteTimeStep {
    std::uint32_t timerMilliseconds;
    std::uint32_t realMilliseconds;
};

// Direct reconstruction of CQTEManager::BeginNowQTE / IsOutTime / SetPause
// (ELF 0x0037aa08 / 0x0037a4b8 / 0x0037a4fc). Keep each native float32
// conversion and subtraction, including the unusual pause-marker formula.
// Replacing this with integer elapsed time or conventional accumulated pause
// duration changes the executable's boundary and long-running-clock behavior.
class QteClock final {
public:
    void begin(std::uint32_t now) noexcept {
        start_ = static_cast<float>(now);
        pauseAdjustment_ = 0.0F;
    }

    void setPause(bool paused, std::uint32_t now) noexcept {
        if (paused) {
            // Both getTime calls read the same tick-cached Timer value.
            const float first = static_cast<float>(now);
            const float second = static_cast<float>(now);
            pauseMarker_ = first - (second - start_);
        } else {
            pauseAdjustment_ = static_cast<float>(now) - pauseMarker_;
            pauseMarker_ = 0.0F;
        }
    }

    [[nodiscard]] float elapsed(std::uint32_t now) const noexcept {
        const float sinceStart = static_cast<float>(now) - start_;
        return sinceStart - pauseAdjustment_;
    }

    [[nodiscard]] bool expired(std::uint32_t now, float duration) const noexcept {
        return elapsed(now) > duration; // Native VFP GT, not >=.
    }

    // Integer diagnostics/HUD only; never used to decide the timeout.
    [[nodiscard]] std::uint32_t displayElapsed(std::uint32_t now,
                                             float duration) const noexcept {
        const double value = std::clamp(static_cast<double>(elapsed(now)), 0.0,
                                       static_cast<double>(duration));
        return static_cast<std::uint32_t>(std::min(
            value, static_cast<double>(std::numeric_limits<std::uint32_t>::max())));
    }

private:
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
    float start_{};            // CQTEManager + 0x60
    float pauseMarker_{};      // + 0x64
    float pauseAdjustment_{};  // + 0x68
};

} // namespace usm::game
