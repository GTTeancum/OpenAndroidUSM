#pragma once

namespace usm::game {
// Synchronous native calls made by CQTEManager, not a deferred release flag.
// The level host must outlive the bound manager and any checkpoint copies.
// Implementations must not throw. Detached fixtures may omit the host.
class QteControlHost {
public:
    virtual ~QteControlHost() = default;
    // BeginQTE ELF 0x37ab54: EnableControls(false, true), before its guard.
    virtual void beginQuickTimeEvent() noexcept = 0;
    // EnableQTEControl ELF 0x2ea990: reset QTE button, then enable/disable.
    virtual void setQuickTimeControlEnabled(bool enabled) noexcept = 0;
    // EndQTE ELF 0x37a5b0: conditional slow-motion reset, then
    // EnableControls(true, false), before SetState commits the result state.
    virtual void endQuickTimeEvent() noexcept = 0;
};
} // namespace usm::game
