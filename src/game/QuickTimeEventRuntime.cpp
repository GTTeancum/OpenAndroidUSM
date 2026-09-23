#include "game/QuickTimeEventRuntime.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <limits>
#include <string_view>
#include <utility>

namespace usm::game {
namespace {
bool integerAttribute(const CinematicCommand& command, std::string_view name,
                      std::int32_t& value) noexcept {
    const auto* attribute = command.findAttribute(name);
    if (!attribute) { return false; }
    const char* begin = attribute->value.data();
    const char* end = begin + attribute->value.size();
    const auto parsed = std::from_chars(begin, end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}
std::int16_t shortWord(int value) noexcept {
    return std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(value));
}
}
void QuickTimeEventRuntime::bind(const ButtonConfigDatabase& configs,
                                const assets::SpriteAtlas& atlas,
                                NativeRandomizer* randomizer,
                                QteControlHost* controls) noexcept {
    configs_ = &configs; atlas_ = &atlas; randomizer_ = randomizer;
    controls_ = controls;
    configId_ = sourceCinematicId_ = successCinematicId_ = failureCinematicId_ = -1;
    elapsedMilliseconds_ = durationMilliseconds_ = 0;
    cinematicRequest_.reset(); sourceRemoval_.reset(); compound_ = nullptr; sequenceIndex_ = 0;
    state_ = QteState::Inactive; path_.clear(); remaining_ = 0; idle_ = 0;
    clock_ = {}; position_ = {}; origin_ = {}; resultPosition_ = {}; releaseEnd_ = {}; releaseControl_ = false; sounds_.clear();
    ++generation_;
    resultSprite_.bind(atlas); promptSprite_.bind(atlas);
    promptSprite_.setAnimation(6);
    successDrawCount_ = successDrawLimit_ = 0;
    failureAlpha_ = 255; failureFadeStep_ = 0;
    if (atlas.animations().size() > 1) {
        const auto& anim = atlas.animations()[1];
        std::uint32_t duration = 0;
        for (std::size_t i = 0; i < anim.frameCount &&
             anim.firstFrameIndex + i < atlas.animationFrames().size(); ++i) {
            duration += atlas.animationFrames()[anim.firstFrameIndex + i].duration;
        }
        // CQTEManager::Draw ELF 0x37a8ba..0x37a8ca: integer 255 /
        // GetAnimDuration(1), including the special value-16 result.
        failureFadeStep_ = duration == 0 ? 0 : 255 / duration;
    }
    if (atlas.animations().size() > 7) {
        const auto& anim = atlas.animations()[7];
        for (std::size_t i = 0; i < anim.frameCount &&
             anim.firstFrameIndex + i < atlas.animationFrames().size(); ++i) {
            successDrawLimit_ += atlas.animationFrames()[anim.firstFrameIndex + i].duration;
        }
    }
}
const ButtonConfigDefinition* QuickTimeEventRuntime::definition() const noexcept {
    return configs_ ? configs_->find(configId_) : nullptr;
}
Result QuickTimeEventRuntime::validate(const ButtonConfigDefinition& d) const {
    if (d.interactionType != 0 && d.interactionType != 1 && d.interactionType != 3) {
        return Result::failure("StartQTE has an invalid or nested interaction type");
    }
    if (!std::isfinite(d.durationMilliseconds) || d.durationMilliseconds <= 0 ||
        static_cast<double>(d.durationMilliseconds) > std::numeric_limits<std::uint32_t>::max()) {
        return Result::failure("StartQTE has an invalid duration");
    }
    if (!std::isfinite(d.screenX) || !std::isfinite(d.screenY) ||
        d.screenX < -32000 || d.screenX > 32000 || d.screenY < -32000 || d.screenY > 32000) {
        return Result::failure("StartQTE has invalid screen coordinates");
    }
    if ((d.interactionValue == 15 || d.interactionValue == 16) && randomizer_ == nullptr) {
        return Result::failure("Random-position QTE needs the shared native randomizer");
    }
    if (d.interactionType == 1) {
        QteGesturePath check;
        return check.bind(d, *atlas_);
    }
    return Result::success();
}
Result QuickTimeEventRuntime::applyCommand(const CinematicCommand& command,
                                         QteTimeStep time, std::int32_t sourceId) {
    if (command.name != "StartQTE") { return Result::success(); }
    if (!configs_ || !atlas_ || atlas_->animations().size() <= 29) {
        return Result::failure("Quick-time-event runtime is not bound to original sprite metadata");
    }
    std::int32_t id = -1, success = -1, failure = -1;
    if (!integerAttribute(command, "QTEID", id) ||
        !integerAttribute(command, "^ID^Cinematic^Success", success) ||
        !integerAttribute(command, "^ID^Cinematic^Fail", failure)) {
        return Result::failure("StartQTE has invalid attributes");
    }
    return begin(id, time, success, failure, sourceId);
}
Result QuickTimeEventRuntime::begin(std::int32_t id, QteTimeStep time,
                                    std::int32_t success, std::int32_t failure,
                                    std::int32_t sourceId) {
    if (!configs_ || !atlas_ || atlas_->animations().size() <= 29) {
        return Result::failure("QTE manager is not bound to original sprite metadata");
    }
    // ELF 0x37ab54: this call precedes BOTH the state guard and lookup.
    // A refused request still resets the gameplay keypad and disables the
    // ordinary controls, without disabling the separate pause button.
    if (controls_) { controls_->beginQuickTimeEvent(); }
    // The ARM guard precedes config lookup. Even a second, invalid request
    // does not replace an active QTE. There is no priority based on caller.
    if (state_ >= QteState::Tap && state_ <= QteState::SuccessDisplay) {
        return Result::success();
    }
    const auto* d = configs_->find(id);
    if (!d) { return Result::failure("StartQTE references a missing button config"); }
    if (d->interactionType == 2) {
        if (d->sequence.empty()) { return Result::failure("StartQTE sequence is empty"); }
        for (const auto childId : d->sequence) {
            const auto* child = configs_->find(childId);
            if (!child) { return Result::failure("StartQTE sequence references a missing child"); }
            const auto result = validate(*child);
            if (!result) { return result; }
        }
    } else {
        const auto result = validate(*d);
        if (!result) { return result; }
    }
    failureAlpha_ = 255; // BeginQTE ELF 0x37ab5e, NOT per compound child.
    sourceCinematicId_ = sourceId; successCinematicId_ = success; failureCinematicId_ = failure;
    // Pending host delivery represents calls that already occurred in native
    // code; a later BeginQTE must not erase those side effects.
    compound_ = d->interactionType == 2 ? d : nullptr;
    sequenceIndex_ = 0;
    beginNow(compound_ ? compound_->sequence[0] : id, time);
    // This is outside BeginNowQTE: compound children must not repeat it.
    if (controls_) { controls_->setQuickTimeControlEnabled(true); }
    return Result::success();
}
void QuickTimeEventRuntime::forceFail() noexcept {
    setState(QteState::FailureDisplay);
}
void QuickTimeEventRuntime::beginNow(std::int32_t id, QteTimeStep time) noexcept {
    configId_ = id;
    const auto& d = *definition();
    clock_.begin(time.timerMilliseconds); elapsedMilliseconds_ = 0;
    durationMilliseconds_ = static_cast<std::uint32_t>(std::round(static_cast<double>(d.durationMilliseconds)));
    remaining_ = d.requiredActionCount;
    float x = d.screenX, y = d.screenY;
    if (d.interactionValue == 15 || d.interactionValue == 16) {
        x = (x + static_cast<float>(randomizer_->range(0, 240))) - 120.0F;
        y = (y + static_cast<float>(randomizer_->range(0, 160))) - 80.0F;
    }
    position_ = {shortWord(static_cast<int>(x)), shortWord(static_cast<int>(y))};
    origin_ = position_;
    promptSprite_.setAnimation(d.interactionValue == 16 ? 28 : 6);
    successDrawCount_ = 0;
    ++generation_;
    // Preserve SetState's same-state early return, including consecutive
    // drag children. UpdateDragState reads each new config's own samples.
    const auto next = d.interactionType == 0 ? QteState::Tap :
                      d.interactionType == 1 ? QteState::Drag : QteState::Mash;
    if (d.interactionType == 1) {
        (void)path_.bind(d, *atlas_); // validated before starting the sequence
    } else { path_.clear(); }
    setState(next);
}
void QuickTimeEventRuntime::setState(QteState next, const QteHandoffHandler& handoff) noexcept {
    if (state_ == next) { return; }
    const auto* d = definition();
    switch (next) {
    case QteState::Drag:
        releaseEnd_ = path_.end();
        resultPosition_ = origin_;
        resultSprite_.setAnimation(static_cast<std::int16_t>(d->interactionValue - 9));
        break;
    case QteState::Mash: idle_ = 0; break;
    case QteState::SuccessDisplay:
    case QteState::FailureDisplay:
        sounds_.push_back(next == QteState::SuccessDisplay ? 0x188 : 0x189);
        if (controls_) { controls_->setQuickTimeControlEnabled(false); }
        resultPosition_ = position_; // Capture AFTER disabling the QTE button.
        resultSprite_.setAnimation(d && d->interactionValue == 16 ? 29 :
                                  next == QteState::SuccessDisplay ? 7 : 1, true);
        // EndQTE runs inside SetState, before the final state_ write. Do
        // not defer it until after another cinematic has changed controls.
        if (controls_) { controls_->endQuickTimeEvent(); }
        else { releaseControl_ = true; } // detached observation only
        break;
    case QteState::SuccessHandled:
    case QteState::FailureHandled: {
        // SuccessHandle/FailHandle, ELF 0x37a5e4/0x37a630: removal is
        // independent of the optional outcome. -1 is not a cinematic ID.
        const auto id = next == QteState::SuccessHandled
            ? successCinematicId_ : failureCinematicId_;
        if (handoff) {
            // SetState stores +0x7c at ELF 0x37a7b6, AFTER calling the
            // native handler. Do not publish state 5/6 early: a StartQTE
            // command executed by its immediate 50-ms cinematic update
            // must see the old state, including original nested-write order.
            handoff({sourceCinematicId_, id});
        } else {
            if (sourceCinematicId_ != -1) { sourceRemoval_ = sourceCinematicId_; }
            if (id != -1) { cinematicRequest_ = id; }
        }
        break;
    }
    default: break;
    }
    state_ = next;
}
void QuickTimeEventRuntime::checkSuccess(QteTimeStep time) noexcept {
    if (compound_ && sequenceIndex_ + 1 < compound_->sequence.size()) {
        beginNow(compound_->sequence[++sequenceIndex_], time);
    } else { setState(QteState::SuccessDisplay); }
}
QteInputConsumption QuickTimeEventRuntime::update(QteTimeStep time, const QteInput& input,
                                                  const QteHandoffHandler& handoff) noexcept {
    QteInputConsumption consumed;
    if (!visible()) { return consumed; }
    const auto* d = definition();
    if (!d) { return consumed; }
    // The CButton release event is delivered before CQTEManager::Update.
    if (state_ == QteState::Drag) {
        if (input.dragPosition) { position_ = *input.dragPosition; }
        if (input.dragReleased) {
            if (std::abs(static_cast<int>(position_.x) - releaseEnd_.x) <= 10 &&
                std::abs(static_cast<int>(position_.y) - releaseEnd_.y) <= 10) { checkSuccess(time); }
            else { setState(QteState::FailureDisplay); }
        }
    }
    d = definition();
    elapsedMilliseconds_ = clock_.displayElapsed(time.timerMilliseconds, d->durationMilliseconds);
    switch (state_) {
    case QteState::Tap:
    case QteState::Mash: {
        const bool mash = state_ == QteState::Mash;
        // ELF 0x37b2c0..0x37b2d4: mash clears rescue unconditionally,
        // then clears QTE and jump on a press. Tap clears QTE alone
        // (0x37b27e..0x37b286). Evaluate the state BEFORE CheckSuccess
        // can advance a compound child or enter result feedback.
        consumed.rescuePress = mash;
        if (input.actionPressed) {
            consumed.quickTimePress = true;
            consumed.jumpPress = mash;
            sounds_.push_back(0x18a);
            remaining_ = shortWord(static_cast<int>(remaining_) - 1);
            if (remaining_ <= 0) { checkSuccess(time); }
            if (mash) { idle_ = 0; }
        }
        promptSprite_.update(time.realMilliseconds);
        if (mash) {
            resultSprite_.update(time.realMilliseconds);
            // Lookup occurs again after CheckSuccess may have begun a child.
            d = definition();
            idle_ += static_cast<float>(time.realMilliseconds);
            if (idle_ >= 500.0F && remaining_ < d->requiredActionCount) {
                remaining_ = shortWord(static_cast<int>(remaining_) + 1); idle_ = 0;
            }
        }
        d = definition();
        if (clock_.expired(time.timerMilliseconds, d->durationMilliseconds)) { setState(QteState::FailureDisplay); }
        break;
    }
    case QteState::Drag: {
        promptSprite_.update(time.realMilliseconds);
        resultSprite_.update(time.realMilliseconds);
        // Native state 1 tests timeout FIRST, then still calls UpdateDragState.
        if (clock_.expired(time.timerMilliseconds, d->durationMilliseconds)) { setState(QteState::FailureDisplay); }
        if (input.dragDirection != QteDirection::None &&
            input.dragDirection == path_.direction()) {
            // PC adaptation: a matching stick gesture replaces touch tracing.
            // Keep the native completion point in UpdateDragState's ordering:
            // timeout first, then completion, with no timer reset or extra tap.
            // The final position is the result sprite's location, not a
            // synthesized series of touch/mouse events.
            position_ = path_.end();
            checkSuccess(time);
        } else {
            const auto nearest = path_.nearest(position_);
            const auto samples = path_.samples();
            if (!samples.empty()) {
                position_ = samples[nearest];
                if (nearest == samples.size() - 1) { checkSuccess(time); }
            }
        }
        break;
    }
    case QteState::SuccessDisplay:
        if (resultSprite_.ended()) { setState(QteState::SuccessHandled, handoff); }
        break;
    case QteState::FailureDisplay:
        if (resultSprite_.ended()) { setState(QteState::FailureHandled, handoff); }
        else { resultSprite_.update(time.realMilliseconds); }
        break;
    default: break;
    }
    return consumed;
}
void QuickTimeEventRuntime::drawStep(const QteHandoffHandler& handoff) noexcept {
    if (state_ == QteState::SuccessDisplay) {
        // CQTEManager::Draw +0xb6: compare before increment. The shipped
        // animation 7 has duration sum 8, therefore nine draws, not eight.
        if (successDrawCount_ >= successDrawLimit_) { setState(QteState::SuccessHandled, handoff); }
        ++successDrawCount_;
    } else if (state_ == QteState::FailureDisplay) {
        if (resultSprite_.ended()) { setState(QteState::FailureHandled, handoff); }
        else {
            // ELF 0x37a8d2..0x37a8e4: decrement/clamp BEFORE PaintAnim.
            failureAlpha_ = static_cast<std::uint8_t>(std::max(
                0, static_cast<int>(failureAlpha_) - static_cast<int>(failureFadeStep_)));
        }
    }
}
QteFeedbackFrame QuickTimeEventRuntime::feedbackFrame() const noexcept {
    QteFeedbackFrame output;
    if (state_ != QteState::SuccessDisplay && state_ != QteState::FailureDisplay) { return output; }
    const bool failure = state_ == QteState::FailureDisplay;
    if (failure && resultSprite_.ended()) { return output; }
    const auto* frame = resultSprite_.frame();
    if (!frame) { return output; }
    // CSprite::PaintAFrame ELF 0x2d99f4: unit-scale animation offsets,
    // then XOR animation flags into the frame's draw flags. QTE instances
    // have zero parent flags and unit scale.
    const auto alpha = failure ? static_cast<std::uint8_t>(std::max(
        0, static_cast<int>(failureAlpha_) - static_cast<int>(failureFadeStep_))) : std::uint8_t{255};
    output.sprites[output.count++] = {frame->frameIndex,
        {static_cast<int>(resultPosition_.x) + frame->x,
         static_cast<int>(resultPosition_.y) + frame->y}, frame->flags, alpha};
    if (!failure && definition() && definition()->interactionType == 3) {
        // Draw state 3: finished mash ring and full progress frame, after
        // the result sprite. No invented percentage/smooth interpolation.
        output.sprites[output.count++] = {85, {origin_.x, origin_.y}, 0, 255};
        output.sprites[output.count++] = {93, {origin_.x, origin_.y}, 0, 255};
    }
    // Explosion sprite's scaled, differently flagged draw is not silently
    // approximated here; its renderer/material reconstruction is still open.
    return output;
}
std::int16_t QuickTimeEventRuntime::completedActionCount() const noexcept {
    const auto* d = definition();
    return d ? shortWord(static_cast<int>(d->requiredActionCount) - remaining_) : 0;
}
std::optional<std::int32_t> QuickTimeEventRuntime::consumeSourceRemoval() noexcept {
    return std::exchange(sourceRemoval_, std::nullopt);
}
bool QuickTimeEventRuntime::consumeControlRelease() noexcept { return std::exchange(releaseControl_, false); }
std::vector<std::uint16_t> QuickTimeEventRuntime::consumeSoundCues() noexcept { return std::exchange(sounds_, {}); }
std::optional<std::int32_t> QuickTimeEventRuntime::consumeCinematicRequest() noexcept {
    return std::exchange(cinematicRequest_, std::nullopt);
}
} // namespace usm::game
