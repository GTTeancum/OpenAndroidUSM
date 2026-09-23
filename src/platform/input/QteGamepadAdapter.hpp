#pragma once

#include "game/QuickTimeEventRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace usm::platform {
// User-authorized PC adaptation: move the left stick in the requested general
// direction. NO A hold, virtual touch coordinates, path tracing or touch-up.
//
// These are explicit host-input policies, not constants recovered from ARM:
// - the production XInput translator has already applied its radial dead zone;
// - engage at 0.55 of the remaining range, rearm at/below 0.25;
// - accept a +/-60 degree cone, including diagonal movement;
// - a held stick at entry/reconnection needs neutral before it is a new gesture;
// - an incorrect direction is ignored (the original deadline continues), and
//   may be corrected without releasing A or tracing anything;
// - an accepted gesture cannot complete another child until a fresh movement.
class QteGamepadAdapter final {
public:
    static constexpr float EngageMagnitude = 0.55F;
    static constexpr float NeutralMagnitude = 0.25F;

    [[nodiscard]] static bool matchesDirection(game::QteDirection direction,
                                               float x, float y) noexcept {
        if (!std::isfinite(x) || !std::isfinite(y)) { return false; }
        x = std::clamp(x, -1.0F, 1.0F);
        y = std::clamp(y, -1.0F, 1.0F);
        const float squared = x * x + y * y;
        if (squared < EngageMagnitude * EngageMagnitude) { return false; }
        float along = 0.0F;
        switch (direction) {
        case game::QteDirection::Left: along = -x; break;
        case game::QteDirection::Right: along = x; break;
        case game::QteDirection::Up: along = y; break; // XInput up is positive
        case game::QteDirection::Down: along = -y; break;
        case game::QteDirection::None: return false;
        }
        // cos(60 degrees) = 1/2; no atan2 or platform-dependent angle wrap.
        return along > 0.0F && 4.0F * along * along >= squared;
    }

    [[nodiscard]] game::QteInput translate(
        const game::QuickTimeEventRuntime& runtime, bool connected,
        bool actionPressed, float stickX, float stickY) noexcept {
        game::QteInput input{connected && actionPressed &&
            runtime.state() != game::QteState::Drag, std::nullopt, false};
        if (generation_ != runtime.generation()) {
            generation_ = runtime.generation();
            // Observing the previous neutral frame lets a fresh movement on
            // the first active frame count, but not gameplay carry-in.
            armed_ = wasConnected_ && previousNeutral_;
        }
        const bool valid = connected && std::isfinite(stickX) && std::isfinite(stickY);
        if (!valid) {
            armed_ = false; wasConnected_ = false; previousNeutral_ = false;
            return input;
        }
        const float x = std::clamp(stickX, -1.0F, 1.0F);
        const float y = std::clamp(stickY, -1.0F, 1.0F);
        const bool neutral = x * x + y * y <= NeutralMagnitude * NeutralMagnitude;
        wasConnected_ = true; previousNeutral_ = neutral;
        if (runtime.state() != game::QteState::Drag) {
            armed_ = false;
            return input; // Original A/Cross tap/mash route stays separate.
        }
        input.actionPressed = false; // A cannot also act on a compound child.
        if (neutral) { armed_ = true; }
        const auto direction = runtime.gesturePath().direction();
        if (armed_ && matchesDirection(direction, x, y)) {
            input.dragDirection = direction;
            armed_ = false;
        }
        return input;
    }
    [[nodiscard]] bool armed() const noexcept { return armed_; }
    void reset() noexcept { *this = {}; }

    [[nodiscard]] static constexpr std::u16string_view prompt(
        game::QteDirection direction) noexcept {
        switch (direction) {
        case game::QteDirection::Left: return u"Move left stick LEFT";
        case game::QteDirection::Right: return u"Move left stick RIGHT";
        case game::QteDirection::Up: return u"Move left stick UP";
        case game::QteDirection::Down: return u"Move left stick DOWN";
        case game::QteDirection::None: return u"";
        }
        return u"";
    }
private:
    std::uint64_t generation_{};
    bool armed_{};
    bool wasConnected_{};
    bool previousNeutral_{};
};
} // namespace usm::platform
