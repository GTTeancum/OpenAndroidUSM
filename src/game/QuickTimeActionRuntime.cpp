#include "game/QuickTimeActionRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace usm::game {
namespace {

constexpr std::uint32_t kAuthoredAnimationFramesPerSecond = 30;

std::uint32_t saturatedAdd(std::uint32_t left,
                           std::uint32_t right) noexcept {
    return std::min<std::uint32_t>(
               std::numeric_limits<std::uint32_t>::max() - right, left) +
           right;
}

} // namespace

void QuickTimeActionRuntime::bind(
    const QuickTimeActionConfigDatabase& actions,
    const ButtonConfigDatabase& buttons,
    const assets::ColladaAnimationFile& playerAnimations,
    const assets::ColladaAnimationFile& npcAnimations) noexcept {
    cancel();
    actions_ = &actions;
    buttons_ = &buttons;
    playerAnimations_ = &playerAnimations;
    npcAnimations_ = &npcAnimations;
}

Result QuickTimeActionRuntime::begin(std::int32_t actionStateId) {
    pendingHits_.clear();
    pendingOutcome_.reset();
    completion_.reset();
    return enter(actionStateId);
}

Result QuickTimeActionRuntime::enter(std::int32_t actionStateId) {
    if (actions_ == nullptr || buttons_ == nullptr ||
        playerAnimations_ == nullptr || npcAnimations_ == nullptr) {
        return Result::failure("Quick-time action runtime is not bound");
    }
    const QuickTimeActionDefinition* definition = actions_->find(actionStateId);
    if (definition == nullptr) {
        return Result::failure("Quick-time action references an invalid state");
    }
    const std::uint32_t playerDuration = animationDuration(
        playerAnimations_, definition->playerAnimation);
    const std::uint32_t npcDuration = animationDuration(
        npcAnimations_, definition->npcAnimation);
    if ((definition->playerAnimation != "NONE" && playerDuration == 0) ||
        (definition->npcAnimation != "NONE" && npcDuration == 0)) {
        return Result::failure(
            "Quick-time action references an unavailable animation");
    }
    const ButtonConfigDefinition* button = nullptr;
    if (definition->buttonConfigId >= 0) {
        button = buttons_->find(definition->buttonConfigId);
        if (button == nullptr || button->durationMilliseconds <= 0.0F ||
            button->durationMilliseconds >
                static_cast<float>(std::numeric_limits<std::uint32_t>::max())) {
            return Result::failure(
                "Quick-time action references an invalid button config");
        }
    }

    definition_ = definition;
    buttonDefinition_ = button;
    stateElapsedMilliseconds_ = 0;
    playerAnimationMilliseconds_ = 0;
    npcAnimationMilliseconds_ = 0;
    playerAnimationDurationMilliseconds_ = playerDuration;
    npcAnimationDurationMilliseconds_ = npcDuration;
    buttonElapsedMilliseconds_ = 0;
    buttonProgress_.reset();
    nextAttackFrameIndex_ = 0;
    enteredActionState_ = definition->id;
    active_ = true;
    return Result::success();
}

Result QuickTimeActionRuntime::update(std::uint32_t elapsedMilliseconds,
                                      bool actionPressed) {
    if (!active_ || definition_ == nullptr) {
        return Result::success();
    }

    const std::uint32_t previousState = stateElapsedMilliseconds_;
    const std::uint32_t previousPlayer = playerAnimationMilliseconds_;
    const std::uint32_t previousNpc = npcAnimationMilliseconds_;
    stateElapsedMilliseconds_ =
        saturatedAdd(stateElapsedMilliseconds_, elapsedMilliseconds);
    advanceAnimation(definition_->playerAnimation,
                     playerAnimationDurationMilliseconds_,
                     elapsedMilliseconds, playerAnimationMilliseconds_);
    advanceAnimation(definition_->npcAnimation,
                     npcAnimationDurationMilliseconds_, elapsedMilliseconds,
                     npcAnimationMilliseconds_);
    queueCrossedAttackFrames(animationLoops() ? previousState
                                              : previousPlayer,
                             animationLoops() ? previousState : previousNpc);

    if (buttonDefinition_ != nullptr) {
        buttonElapsedMilliseconds_ = std::min(
            buttonDurationMilliseconds(),
            saturatedAdd(buttonElapsedMilliseconds_, elapsedMilliseconds));
        const std::int16_t required = requiredActionCount();
        const bool succeeded = buttonProgress_.update(
            elapsedMilliseconds, actionPressed, required,
            buttonDefinition_->interactionType == 3);
        const bool failed =
            buttonElapsedMilliseconds_ >= buttonDurationMilliseconds();
        if (succeeded || failed) {
            const std::int16_t nextState =
                succeeded ? definition_->successStateId
                          : definition_->failureStateId;
            pendingOutcome_ = succeeded ? QuickTimeActionOutcome::Success
                                        : QuickTimeActionOutcome::Failure;
            if (nextState < 0) {
                completion_ = pendingOutcome_;
                active_ = false;
                return Result::success();
            }
            return enter(nextState);
        }
        return Result::success();
    }

    const bool playerEnded = animationEnded(
        definition_->playerAnimation, playerAnimationMilliseconds_,
        playerAnimationDurationMilliseconds_);
    const bool npcEnded = animationEnded(
        definition_->npcAnimation, npcAnimationMilliseconds_,
        npcAnimationDurationMilliseconds_);
    if (!playerEnded || !npcEnded) {
        return Result::success();
    }

    if (definition_->playerEndAction >= 0 &&
        definition_->npcEndAction >= 0) {
        completion_ = pendingOutcome_.value_or(QuickTimeActionOutcome::Success);
        active_ = false;
        return Result::success();
    }
    if (definition_->successStateId < 0) {
        completion_ = pendingOutcome_.value_or(QuickTimeActionOutcome::Success);
        active_ = false;
        return Result::success();
    }
    return enter(definition_->successStateId);
}

void QuickTimeActionRuntime::cancel() noexcept {
    definition_ = nullptr;
    buttonDefinition_ = nullptr;
    stateElapsedMilliseconds_ = 0;
    playerAnimationMilliseconds_ = 0;
    npcAnimationMilliseconds_ = 0;
    playerAnimationDurationMilliseconds_ = 0;
    npcAnimationDurationMilliseconds_ = 0;
    buttonElapsedMilliseconds_ = 0;
    nextAttackFrameIndex_ = 0;
    buttonProgress_.reset();
    enteredActionState_.reset();
    pendingHits_.clear();
    pendingOutcome_.reset();
    completion_.reset();
    active_ = false;
}

std::string_view QuickTimeActionRuntime::playerAnimation() const noexcept {
    return definition_ == nullptr ? std::string_view{}
                                  : definition_->playerAnimation;
}

std::string_view QuickTimeActionRuntime::npcAnimation() const noexcept {
    return definition_ == nullptr ? std::string_view{}
                                  : definition_->npcAnimation;
}

bool QuickTimeActionRuntime::animationLoops() const noexcept {
    return definition_ != nullptr && definition_->loop;
}

std::uint32_t QuickTimeActionRuntime::buttonDurationMilliseconds() const
    noexcept {
    return buttonDefinition_ == nullptr
               ? 0
               : static_cast<std::uint32_t>(
                     std::lround(buttonDefinition_->durationMilliseconds));
}

std::int16_t QuickTimeActionRuntime::requiredActionCount() const noexcept {
    return buttonDefinition_ == nullptr ? 0
                                        : buttonDefinition_->requiredActionCount;
}

float QuickTimeActionRuntime::buttonProgress() const noexcept {
    if (buttonDefinition_ == nullptr) {
        return 0.0F;
    }
    const std::int16_t required = requiredActionCount();
    if (required > 0) {
        return std::clamp(static_cast<float>(buttonProgress_.completed()) /
                              static_cast<float>(required),
                          0.0F, 1.0F);
    }
    const std::uint32_t duration = buttonDurationMilliseconds();
    return duration == 0
               ? 0.0F
               : std::clamp(static_cast<float>(buttonElapsedMilliseconds_) /
                                static_cast<float>(duration),
                            0.0F, 1.0F);
}

std::optional<std::int16_t>
QuickTimeActionRuntime::consumeEnteredActionState() noexcept {
    const std::optional<std::int16_t> entered = enteredActionState_;
    enteredActionState_.reset();
    return entered;
}

std::vector<QuickTimeActionHit>
QuickTimeActionRuntime::consumeHits() noexcept {
    std::vector<QuickTimeActionHit> hits = std::move(pendingHits_);
    pendingHits_.clear();
    return hits;
}

std::optional<QuickTimeActionOutcome>
QuickTimeActionRuntime::consumeCompletion() noexcept {
    const std::optional<QuickTimeActionOutcome> result = completion_;
    completion_.reset();
    return result;
}

std::uint32_t QuickTimeActionRuntime::animationDuration(
    const assets::ColladaAnimationFile* bank,
    std::string_view animation) const noexcept {
    if (animation == "NONE") {
        return 0;
    }
    const assets::ColladaAnimationClip* clip =
        bank == nullptr ? nullptr : bank->findClip(animation);
    return clip == nullptr ? 0 : clip->durationMilliseconds();
}

bool QuickTimeActionRuntime::animationEnded(
    std::string_view animation, std::uint32_t time,
    std::uint32_t duration) const noexcept {
    return animation == "NONE" ||
           (!animationLoops() && duration != 0 && time >= duration);
}

void QuickTimeActionRuntime::advanceAnimation(
    std::string_view animation, std::uint32_t duration,
    std::uint32_t elapsedMilliseconds, std::uint32_t& time) const noexcept {
    if (animation == "NONE" || duration == 0) {
        time = 0;
        return;
    }
    const std::uint32_t next = saturatedAdd(time, elapsedMilliseconds);
    time = animationLoops() ? next % duration : std::min(next, duration);
}

void QuickTimeActionRuntime::queueCrossedAttackFrames(
    std::uint32_t previousPlayerMilliseconds,
    std::uint32_t previousNpcMilliseconds) {
    if (definition_ == nullptr || definition_->attackFrames.empty()) {
        return;
    }
    const bool playerAttacks = definition_->attackDirection == 2;
    const bool npcAttacks = definition_->attackDirection == 0 ||
                            definition_->attackDirection == 3;
    if (!playerAttacks && !npcAttacks) {
        return;
    }
    const std::uint32_t previous =
        animationLoops()
            ? previousPlayerMilliseconds
            : (playerAttacks ? previousPlayerMilliseconds
                             : previousNpcMilliseconds);
    const std::uint32_t current =
        animationLoops()
            ? stateElapsedMilliseconds_
            : (playerAttacks ? playerAnimationMilliseconds_
                             : npcAnimationMilliseconds_);
    while (nextAttackFrameIndex_ < definition_->attackFrames.size()) {
        const std::int16_t frame =
            definition_->attackFrames[nextAttackFrameIndex_];
        if (frame < 0) {
            ++nextAttackFrameIndex_;
            continue;
        }
        const std::uint32_t threshold =
            static_cast<std::uint32_t>(frame) * 1000U /
            kAuthoredAnimationFramesPerSecond;
        if (threshold > current) {
            break;
        }
        if (threshold >= previous) {
            pendingHits_.push_back(
                {playerAttacks ? QuickTimeActionTarget::Npc
                               : QuickTimeActionTarget::Player,
                 definition_->id, frame,
                 static_cast<float>(
                     definition_->attackDamage[nextAttackFrameIndex_])});
        }
        ++nextAttackFrameIndex_;
    }
}

} // namespace usm::game
