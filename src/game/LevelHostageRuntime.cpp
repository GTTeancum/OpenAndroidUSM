#include "game/LevelHostageRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace usm::game {
namespace {

constexpr std::uint16_t kRescueCuttingSound = 0x18b;
constexpr std::uint16_t kWomanRescuedSound = 0xa3;
constexpr std::uint16_t kManRescuedSound = 0xa5;
constexpr std::uint16_t kRescueStartPlayerState = 27;
constexpr std::uint16_t kRescueQuickTimePlayerState = 28;
constexpr std::uint16_t kRescueEndPlayerState = 29;

float distanceSquared(const assets::Vector3& first,
                      const assets::Vector3& second) noexcept {
    const float x = first.x - second.x;
    const float y = first.y - second.y;
    const float z = first.z - second.z;
    return x * x + y * y + z * z;
}

} // namespace

bool hostageUntieSoundEligible(
    HostageRescuePhase phase, std::uint16_t playerStateId,
    std::int16_t remainingActions) noexcept {
    return phase == HostageRescuePhase::QuickTime &&
           playerStateId == kRescueQuickTimePlayerState &&
           remainingActions == 7;
}

HostageQteOutcome hostageQteOutcomeForManagerState(QteState state) noexcept {
    if (state == QteState::SuccessDisplay ||
        state == QteState::SuccessHandled) {
        return HostageQteOutcome::Success;
    }
    if (state == QteState::FailureDisplay ||
        state == QteState::FailureHandled) {
        return HostageQteOutcome::Failure;
    }
    return HostageQteOutcome::Running;
}

Result LevelHostageRuntime::initialize(const LevelOneBootstrap& level,
                                       QuickTimeEventRuntime& sharedQte,
                                       LevelObjectRuntime* objects) {
    states_.clear();
    soundCues_.clear();
    sharedQte_ = &sharedQte;
    quickTimeConfig_ = level.buttonConfigs().find(11);
    if (quickTimeConfig_ == nullptr ||
        quickTimeConfig_->durationMilliseconds <= 0.0F ||
        !std::isfinite(quickTimeConfig_->durationMilliseconds) ||
        static_cast<double>(quickTimeConfig_->durationMilliseconds) >
            static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
        return Result::failure(
            "Hostage rescue QTE button config 11 is invalid");
    }
    for (const LevelObjectAsset& object : level.objects()) {
        if (object.kind != LevelObjectKind::Hostage) {
            continue;
        }
        if (object.hostageEnableRadius <= 0.0F ||
            !std::isfinite(object.hostageEnableRadius) ||
            std::any_of(object.hostageAnimations.begin(),
                        object.hostageAnimations.end(),
                        [](const std::string& animation) {
                            return animation.empty();
                        })) {
            states_.clear();
            return Result::failure(
                "Hostage has invalid authored rescue attributes");
        }
        LevelHostageState state;
        state.asset = &object;
        state.quickTimeDurationMilliseconds =
            static_cast<std::uint32_t>(std::round(
                static_cast<double>(quickTimeConfig_->durationMilliseconds)));
        state.requiredActions = std::max<std::int16_t>(
            1, quickTimeConfig_->requiredActionCount);
        states_.push_back(state);
        if (objects != nullptr) {
            Result result = objects->setAnimation(
                object.objectId, object.hostageAnimations[0], true);
            if (!result) {
                states_.clear();
                return result;
            }
        }
    }
    return Result::success();
}

Result LevelHostageRuntime::update(GameplayPlayer& player,
                                   LevelObjectRuntime& objects,
                                   LevelBonusRuntime& bonuses,
                                   HostageTimeStep time,
                                   HostageInput input,
                                   const HostageUpdateHooks& hooks) {
    const Result result = updateObjects(player, objects, bonuses, time,
                                        input.rescueRequested, hooks);
    if (result) {
        updateQuickTime({time.timerMilliseconds, time.realMilliseconds},
                        input.quickTimeActionPressed);
    }
    return result;
}

Result LevelHostageRuntime::updateObjects(GameplayPlayer& player,
                                          LevelObjectRuntime& objects,
                                          LevelBonusRuntime& bonuses,
                                          HostageTimeStep time,
                                          bool rescueRequested,
                                          const HostageUpdateHooks& hooks) {
    if (sharedQte_ == nullptr) {
        return Result::failure("Hostage runtime has no shared level QTE manager");
    }
    // CHostage reads the level manager BEFORE its later CLevel update.
    observeQuickTime();
    const std::uint32_t elapsedMilliseconds = time.simulationMilliseconds;
    for (LevelHostageState& hostage : states_) {
        if (hostage.asset == nullptr) {
            return Result::failure("Hostage runtime lost its authored asset");
        }
        hostage.phaseElapsedMilliseconds =
            static_cast<std::uint32_t>(std::min<std::uint64_t>(
                static_cast<std::uint64_t>(hostage.phaseElapsedMilliseconds) +
                    elapsedMilliseconds,
                std::numeric_limits<std::uint32_t>::max()));
        const LevelObjectState* object =
            objects.find(hostage.asset->objectId);
        const bool visible = object != nullptr && object->visible;
        if (!visible) {
            hostage.promptVisible = false;
            continue;
        }
        hostage.promptVisible =
            hostage.phase == HostageRescuePhase::Tied && visible &&
            !player.airborne() && !player.isUltimateState() &&
            playerInsideEnableRadius(hostage, player);

        switch (hostage.phase) {
        case HostageRescuePhase::Tied:
            if (hostage.promptVisible && rescueRequested) {
                const assets::Vector3 direction{
                    hostage.asset->position.x - player.position().x,
                    hostage.asset->position.y - player.position().y, 0.0F};
                Result result = player.enterScriptedState(
                    kRescueStartPlayerState, false, &direction);
                if (!result) {
                    return result;
                }
                hostage.phase = HostageRescuePhase::RescueStart;
                hostage.phaseElapsedMilliseconds = 0;
                hostage.promptVisible = false;
                rescueRequested = false; // The native global is consumed once.
                if (hooks.enableControls) { hooks.enableControls(false, true); }
            }
            break;
        case HostageRescuePhase::RescueStart:
            if (player.activeStateId() != kRescueStartPlayerState) {
                if (hooks.enableControls) { hooks.enableControls(true, false); }
                Result result = setPhase(hostage, HostageRescuePhase::Tied,
                                         player, objects, bonuses, hooks);
                if (!result) {
                    return result;
                }
            } else if (hooks.enableControls) {
                hooks.enableControls(false, true);
            }
            // Native checks the animation even after resetting an interrupted
            // hostage. Do not collapse these independent checks into else-if.
            if (player.scriptedStateAnimationFinished()) {
                Result result = player.enterScriptedState(
                    kRescueQuickTimePlayerState, true);
                if (!result) {
                    return result;
                }
                result = sharedQte_->begin(11,
                    {time.timerMilliseconds, time.realMilliseconds}, -1, -1, -1);
                if (!result) { return result; }
                hostage.phase = HostageRescuePhase::QuickTime;
                hostage.phaseElapsedMilliseconds = 0;
                observeQuickTime();
            }
            break;
        case HostageRescuePhase::QuickTime: {
            // CHostage::Update reads the previous CQTEManager result/count.
            // A final tap or timeout in this update is observed on the next
            // object update, not eagerly inside the button-input operation.
            const bool succeeded =
                hostage.quickTimeOutcome == HostageQteOutcome::Success;
            const bool failed =
                hostage.quickTimeOutcome == HostageQteOutcome::Failure;
            if (succeeded || failed) {
                Result soundResult = emitSound({hostage.asset->objectId,
                    kRescueCuttingSound, HostageSoundAction::Stop}, hooks);
                if (!soundResult) { return soundResult; }
                if (succeeded) {
                    Result result = player.enterScriptedState(
                        kRescueEndPlayerState, false);
                    if (!result) { return result; }
                    hostage.phase = HostageRescuePhase::RescueEnd;
                    hostage.phaseElapsedMilliseconds = 0;
                } else {
                    player.clearScriptedState(); // Native requests idle here.
                    if (hooks.enableControls) { hooks.enableControls(true, false); }
                    Result result = setPhase(hostage, HostageRescuePhase::Tied,
                                             player, objects, bonuses, hooks);
                    if (!result) { return result; }
                }
            } else if (player.activeStateId() != kRescueQuickTimePlayerState) {
                // An interrupted rescue fails; it must not replace the
                // interrupting player state with idle or grant rescue rewards.
                sharedQte_->forceFail(); // CHostage ELF 0x32840e -> manager state 4
                if (hooks.enableControls) { hooks.enableControls(true, false); }
                Result soundResult = emitSound({hostage.asset->objectId,
                    kRescueCuttingSound, HostageSoundAction::Stop}, hooks);
                if (!soundResult) { return soundResult; }
                Result result = setPhase(hostage, HostageRescuePhase::Tied,
                                         player, objects, bonuses, hooks);
                if (!result) { return result; }
            } else {
                // Native repeats EnableControls(false,true) before querying.
                if (hooks.enableControls) { hooks.enableControls(false, true); }
                if (hostageUntieSoundEligible(
                        hostage.phase, player.activeStateId(),
                        sharedQte_->remainingActionCount())) {
                    Result soundResult = emitSound({hostage.asset->objectId,
                        kRescueCuttingSound, HostageSoundAction::PlayOnceIfStopped}, hooks);
                    if (!soundResult) { return soundResult; }
                }
            }
            break;
        }
        case HostageRescuePhase::RescueEnd:
            // Native calls Stop(0x18b) each state-3 object update. Repeated
            // stop requests are intentional and have no new gameplay effect.
            {
                Result soundResult = emitSound({hostage.asset->objectId,
                    kRescueCuttingSound, HostageSoundAction::Stop}, hooks);
                if (!soundResult) { return soundResult; }
            }
            if (player.activeStateId() != kRescueEndPlayerState) {
                if (hooks.enableControls) { hooks.enableControls(true, false); }
                Result result = setPhase(hostage, HostageRescuePhase::Tied,
                                         player, objects, bonuses, hooks);
                if (!result) { return result; }
            }
            if (player.scriptedStateAnimationFinished()) {
                player.clearScriptedState();
                if (hooks.enableControls) { hooks.enableControls(true, false); }
                // OnExitState(0) prevents a freshly reset, still-animating
                // tied hostage from entering release after an interruption.
                if (hostage.phase != HostageRescuePhase::Tied ||
                    objects.animationFinished(hostage.asset->objectId)) {
                    Result result = setPhase(hostage, HostageRescuePhase::Release,
                                             player, objects, bonuses, hooks);
                    if (!result) { return result; }
                }
            }
            break;
        case HostageRescuePhase::Release:
            if (objects.animationFinished(hostage.asset->objectId)) {
                Result result = setPhase(hostage, HostageRescuePhase::Thank,
                                         player, objects, bonuses, hooks);
                if (!result) {
                    return result;
                }
            }
            break;
        case HostageRescuePhase::Thank:
            if (objects.animationFinished(hostage.asset->objectId)) {
                Result result = setPhase(hostage, HostageRescuePhase::Freed,
                                         player, objects, bonuses, hooks);
                if (!result) {
                    return result;
                }
            }
            break;
        case HostageRescuePhase::Freed:
            hostage.promptVisible = false;
            break;
        }
    }
    return Result::success();
}

void LevelHostageRuntime::updateQuickTime(QteTimeStep time,
                                          bool actionPressed) noexcept {
    if (sharedQte_ == nullptr) { return; }
    sharedQte_->update(time, actionPressed);
    observeQuickTime();
}

void LevelHostageRuntime::observeQuickTime() noexcept {
    if (sharedQte_ == nullptr) { return; }
    for (auto& hostage : states_) {
        if (hostage.phase != HostageRescuePhase::QuickTime) { continue; }
        hostage.quickTimeElapsedMilliseconds = sharedQte_->elapsedMilliseconds();
        hostage.quickTimeDurationMilliseconds = sharedQte_->durationMilliseconds();
        hostage.completedActions = sharedQte_->completedActionCount();
        // CHostage::Update (ELF 0x3283ba onward) explicitly accepts both
        // displayed and handled result states. No owner/config-ID guard.
        hostage.quickTimeOutcome =
            hostageQteOutcomeForManagerState(sharedQte_->state());
    }
}

std::vector<std::uint16_t> LevelHostageRuntime::consumeQuickTimeSoundCues() noexcept {
    return sharedQte_ ? sharedQte_->consumeSoundCues() : std::vector<std::uint16_t>{};
}
bool LevelHostageRuntime::consumeControlRelease() noexcept {
    return sharedQte_ && sharedQte_->consumeControlRelease();
}

void LevelHostageRuntime::setPause(bool paused,
                                  std::uint32_t timerMilliseconds) noexcept {
    if (sharedQte_) { sharedQte_->setPause(paused, timerMilliseconds); }
}

bool LevelHostageRuntime::canStartRescue(
    const GameplayPlayer& player) const noexcept {
    return std::any_of(states_.begin(), states_.end(),
                       [&player, this](const LevelHostageState& hostage) {
                           return hostage.phase == HostageRescuePhase::Tied &&
                                  hostage.promptVisible &&
                                  !player.airborne() && !player.isUltimateState() &&
                                  playerInsideEnableRadius(hostage, player);
                       });
}

bool LevelHostageRuntime::ownsPlayerControl() const noexcept {
    return std::any_of(states_.begin(), states_.end(),
                       [](const LevelHostageState& hostage) {
                           return hostage.phase >=
                                      HostageRescuePhase::RescueStart &&
                                  hostage.phase <=
                                      HostageRescuePhase::RescueEnd;
                       });
}

bool LevelHostageRuntime::quickTimeActive() const noexcept {
    return std::any_of(states_.begin(), states_.end(),
                       [](const LevelHostageState& hostage) {
                           return hostage.phase ==
                                  HostageRescuePhase::QuickTime;
                       });
}

float LevelHostageRuntime::quickTimeProgress() const noexcept {
    const auto active = std::find_if(
        states_.begin(), states_.end(), [](const LevelHostageState& hostage) {
            return hostage.phase == HostageRescuePhase::QuickTime;
        });
    if (active == states_.end() ||
        active->quickTimeDurationMilliseconds == 0) {
        return 0.0F;
    }
    return std::clamp(
        static_cast<float>(active->quickTimeElapsedMilliseconds) /
            static_cast<float>(active->quickTimeDurationMilliseconds),
        0.0F, 1.0F);
}

bool LevelHostageRuntime::contextPromptVisible() const noexcept {
    return std::any_of(states_.begin(), states_.end(),
                       [](const LevelHostageState& hostage) {
                           return hostage.promptVisible;
                       });
}

const LevelHostageState* LevelHostageRuntime::find(
    std::int32_t objectId) const noexcept {
    const auto match = std::find_if(
        states_.begin(), states_.end(), [objectId](const LevelHostageState& state) {
            return state.asset != nullptr &&
                   state.asset->objectId == objectId;
        });
    return match == states_.end() ? nullptr : &*match;
}

std::vector<HostageSoundCue> LevelHostageRuntime::consumeSoundCues() {
    std::vector<HostageSoundCue> result;
    result.swap(soundCues_);
    return result;
}

Result LevelHostageRuntime::setPhase(LevelHostageState& hostage,
                                     HostageRescuePhase phase,
                                     GameplayPlayer& player,
                                     LevelObjectRuntime& objects,
                                     LevelBonusRuntime& bonuses,
                                     const HostageUpdateHooks& hooks) {
    if (hostage.asset == nullptr) {
        return Result::failure("Hostage phase target is invalid");
    }
    hostage.phase = phase;
    hostage.phaseElapsedMilliseconds = 0;
    hostage.promptVisible = false;
    switch (phase) {
    case HostageRescuePhase::Tied:
        // CHostage::SetState(0) changes the hostage, not the player. Only
        // branches with an evidenced Player::SetNextStateId(0) clear it.
        hostage.quickTimeOutcome = HostageQteOutcome::Inactive;
        hostage.quickTimeElapsedMilliseconds = 0;
        hostage.completedActions = 0;
        return objects.setAnimation(hostage.asset->objectId,
                                    hostage.asset->hostageAnimations[0], true);
    case HostageRescuePhase::Release:
        bonuses.spawnOrbs(LevelBonusType::Health, hostage.asset->position,
                          hostage.asset->roomId,
                          hostage.asset->hostageHealthOrbCount);
        bonuses.spawnOrbs(LevelBonusType::SkillPoint,
                          hostage.asset->position, hostage.asset->roomId,
                          hostage.asset->hostageSkillPointOrbCount);
        return objects.setAnimation(hostage.asset->objectId,
                                    hostage.asset->hostageAnimations[1],
                                    false);
    case HostageRescuePhase::Thank:
        {
            Result soundResult = emitSound({hostage.asset->objectId,
                hostage.asset->hostageIsWoman ? kWomanRescuedSound : kManRescuedSound,
                HostageSoundAction::PlayOnce}, hooks);
            if (!soundResult) { return soundResult; }
        }
        return objects.setAnimation(hostage.asset->objectId,
                                    hostage.asset->hostageAnimations[2],
                                    false);
    case HostageRescuePhase::Freed:
        return objects.setAnimation(hostage.asset->objectId,
                                    hostage.asset->hostageAnimations[3], true);
    case HostageRescuePhase::RescueStart:
    case HostageRescuePhase::QuickTime:
    case HostageRescuePhase::RescueEnd:
        return Result::success();
    }
    return Result::success();
}

Result LevelHostageRuntime::emitSound(const HostageSoundCue& cue,
                                      const HostageUpdateHooks& hooks) {
    if (hooks.sound) { return hooks.sound(cue); }
    soundCues_.push_back(cue); // Request only; never latched as actually playing.
    return Result::success();
}

bool LevelHostageRuntime::playerInsideEnableRadius(
    const LevelHostageState& hostage,
    const GameplayPlayer& player) const noexcept {
    return hostage.asset != nullptr &&
           distanceSquared(hostage.asset->position, player.position()) <=
               hostage.asset->hostageEnableRadius *
                   hostage.asset->hostageEnableRadius;
}

} // namespace usm::game
