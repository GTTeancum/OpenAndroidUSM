#pragma once

#include "assets/SpriteAtlas.hpp"
#include "core/Result.hpp"
#include "game/ButtonConfig.hpp"
#include "game/CinematicScript.hpp"
#include "game/NativeRandomizer.hpp"
#include "game/QteClock.hpp"
#include "game/QteControlHost.hpp"
#include "game/QteCinematicHandoff.hpp"
#include "game/QteGesturePath.hpp"
#include "game/QteFeedbackFrame.hpp"
#include "game/QteSpriteAnimation.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace usm::game {
enum class QteState : std::int8_t {
    Inactive = -1, Tap = 0, Drag = 1, Mash = 2,
    SuccessDisplay = 3, FailureDisplay = 4, SuccessHandled = 5, FailureHandled = 6
};
struct QteInput {
    bool actionPressed{}; // Original Cross flag; does NOT complete a drag.
    std::optional<QtePoint> dragPosition;
    bool dragReleased{};  // Original event 0x1f, before the manager update.
    // User-authorized PC control adaptation, NOT an Android input field.
    // The adapter supplies intent only; this manager still owns timing,
    // compound sequencing, result audio, control release and outcome dispatch.
    QteDirection dragDirection{QteDirection::None};
};

// Writes performed by the native manager to the shared input globals in THIS
// update. Carry these to later consumers; do not fake key-up events or erase
// the independent CKeyPad two-update press history.
struct QteInputConsumption {
    bool quickTimePress{};
    bool jumpPress{};
    bool rescuePress{};
};

// Native reconstruction of CQTEManager, original ELF 0x0037a3f8..0x0037b40c.
// Draw has gameplay side effects; drawStep is called once per presented frame,
// not once per simulation catch-up update. Cinematics and hostages share this
// one level-owned instance (CLevel+0x54); there is no per-owner priority rule.
// Wall-web/QTEAction paths have not yet been migrated to the shared manager.
class QuickTimeEventRuntime final {
public:
    void bind(const ButtonConfigDatabase& configs, const assets::SpriteAtlas& atlas,
              NativeRandomizer* randomizer = nullptr,
              QteControlHost* controls = nullptr) noexcept;
    [[nodiscard]] Result applyCommand(const CinematicCommand& command,
                                      QteTimeStep time,
                                      std::int32_t sourceCinematicId = -1);
    // CQTEManager::BeginQTE (ELF 0x37ab40), also called directly by
    // CHostage::Update with (11,-1,-1,-1). A refused begin leaves the current
    // QTE intact. No fake cinematic command or owner-specific queue is used.
    [[nodiscard]] Result begin(std::int32_t configId, QteTimeStep time,
                               std::int32_t successCinematicId = -1,
                               std::int32_t failureCinematicId = -1,
                               std::int32_t sourceCinematicId = -1);
    void forceFail() noexcept; // ELF 0x37a9fc: unconditional SetState(4)
    QteInputConsumption update(QteTimeStep time, const QteInput& input,
                               const QteHandoffHandler& handoff = {}) noexcept;
    QteInputConsumption update(QteTimeStep time, bool actionPressed,
                               const QteHandoffHandler& handoff = {}) noexcept {
        return update(time, QteInput{actionPressed, std::nullopt, false}, handoff);
    }
    void drawStep(const QteHandoffHandler& handoff = {}) noexcept;
    [[nodiscard]] QteFeedbackFrame feedbackFrame() const noexcept;
    [[nodiscard]] std::uint8_t failureAlpha() const noexcept { return failureAlpha_; }
    void setPause(bool paused, std::uint32_t timerMilliseconds) noexcept {
        clock_.setPause(paused, timerMilliseconds);
    }
    [[nodiscard]] bool active() const noexcept { return state_ >= QteState::Tap && state_ <= QteState::Mash; }
    [[nodiscard]] bool visible() const noexcept { return state_ >= QteState::Tap && state_ <= QteState::FailureDisplay; }
    [[nodiscard]] QteState state() const noexcept { return state_; }
    [[nodiscard]] std::int32_t configId() const noexcept { return configId_; }
    [[nodiscard]] std::int32_t sourceCinematicId() const noexcept { return sourceCinematicId_; }
    [[nodiscard]] std::uint32_t elapsedMilliseconds() const noexcept { return elapsedMilliseconds_; }
    [[nodiscard]] std::uint32_t durationMilliseconds() const noexcept { return durationMilliseconds_; }
    [[nodiscard]] std::int16_t completedActionCount() const noexcept;
    [[nodiscard]] std::int16_t remainingActionCount() const noexcept { return remaining_; }
    [[nodiscard]] const QteGesturePath& gesturePath() const noexcept { return path_; }
    [[nodiscard]] QtePoint buttonPosition() const noexcept { return position_; }
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }
    [[nodiscard]] const QteSpriteAnimation& resultSprite() const noexcept { return resultSprite_; }
    [[nodiscard]] const QteSpriteAnimation& promptSprite() const noexcept { return promptSprite_; }
    [[nodiscard]] std::uint32_t successDrawCount() const noexcept { return successDrawCount_; }
    // A live host supplies handoff to update/drawStep: it runs BEFORE the
    // handled-state write, preserving reentrant BeginQTE's original guard.
    // These queues are for detached fixtures/observation only when no handler
    // is supplied; they do not emulate synchronous reentrant world execution.
    // Handled state may remove a source cinematic even with no outcome ID.
    [[nodiscard]] std::optional<std::int32_t> consumeSourceRemoval() noexcept;
    [[nodiscard]] bool consumeControlRelease() noexcept;
    [[nodiscard]] std::vector<std::uint16_t> consumeSoundCues() noexcept;
    [[nodiscard]] std::optional<std::int32_t> consumeCinematicRequest() noexcept;
private:
    [[nodiscard]] Result validate(const ButtonConfigDefinition& definition) const;
    void beginNow(std::int32_t id, QteTimeStep time) noexcept;
    void checkSuccess(QteTimeStep time) noexcept;
    void setState(QteState state, const QteHandoffHandler& handoff = {}) noexcept;
    [[nodiscard]] const ButtonConfigDefinition* definition() const noexcept;
    const ButtonConfigDatabase* configs_{};
    const assets::SpriteAtlas* atlas_{};
    NativeRandomizer* randomizer_{};
    QteControlHost* controls_{}; // non-owning, synchronous; never a frame closure
    std::int32_t configId_{-1};
    std::int32_t sourceCinematicId_{-1};
    std::int32_t successCinematicId_{-1};
    std::int32_t failureCinematicId_{-1};
    std::uint32_t elapsedMilliseconds_{};
    std::uint32_t durationMilliseconds_{};
    std::optional<std::int32_t> cinematicRequest_;
    std::optional<std::int32_t> sourceRemoval_;
    QteClock clock_;
    QteGesturePath path_;
    QteSpriteAnimation resultSprite_, promptSprite_;
    QteState state_{QteState::Inactive};
    QtePoint position_;
    QtePoint origin_, resultPosition_;
    std::uint8_t failureAlpha_{255};
    std::uint32_t failureFadeStep_{};
    QtePoint releaseEnd_;
    std::int16_t remaining_{};
    float idle_{};
    const ButtonConfigDefinition* compound_{};
    std::size_t sequenceIndex_{};
    std::uint32_t successDrawLimit_{};
    std::uint32_t successDrawCount_{};
    std::uint64_t generation_{};
    bool releaseControl_{};
    std::vector<std::uint16_t> sounds_;
};
} // namespace usm::game
