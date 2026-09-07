#include "game/LevelHostageRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace usm::game {
namespace {

constexpr std::uint16_t kRescueCuttingLoopSound = 0x18b;
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

Result LevelHostageRuntime::initialize(const LevelOneBootstrap& level,
                                       LevelObjectRuntime* objects) {
    states_.clear();
    soundCues_.clear();
    quickTimeConfig_ = level.buttonConfigs().find(11);
    if (quickTimeConfig_ == nullptr ||
        quickTimeConfig_->durationMilliseconds <= 0.0F ||
        !std::isfinite(quickTimeConfig_->durationMilliseconds)) {
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
            static_cast<std::uint32_t>(std::lround(
                quickTimeConfig_->durationMilliseconds));
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
                                   std::uint32_t elapsedMilliseconds,
                                   bool rescuePressed) {
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
        hostage.promptVisible =
            hostage.phase == HostageRescuePhase::Tied && visible &&
            !player.airborne() && playerInsideEnableRadius(hostage, player);

        switch (hostage.phase) {
        case HostageRescuePhase::Tied:
            if (hostage.promptVisible && rescuePressed) {
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
            }
            break;
        case HostageRescuePhase::RescueStart:
            if (player.activeStateId() != kRescueStartPlayerState) {
                Result result = setPhase(hostage, HostageRescuePhase::Tied,
                                         player, objects, bonuses);
                if (!result) {
                    return result;
                }
            } else if (player.scriptedStateAnimationFinished()) {
                Result result = player.enterScriptedState(
                    kRescueQuickTimePlayerState, true);
                if (!result) {
                    return result;
                }
                hostage.phase = HostageRescuePhase::QuickTime;
                hostage.phaseElapsedMilliseconds = 0;
                hostage.quickTimeElapsedMilliseconds = 0;
                hostage.completedActions = 0;
                soundCues_.push_back({hostage.asset->objectId,
                                      kRescueCuttingLoopSound,
                                      HostageSoundAction::StartLoop});
            }
            break;
        case HostageRescuePhase::QuickTime:
            hostage.quickTimeElapsedMilliseconds =
                static_cast<std::uint32_t>(std::min<std::uint64_t>(
                    static_cast<std::uint64_t>(
                        hostage.quickTimeElapsedMilliseconds) +
                        elapsedMilliseconds,
                    hostage.quickTimeDurationMilliseconds));
            if (rescuePressed) {
                hostage.completedActions = std::min<std::int16_t>(
                    hostage.requiredActions,
                    static_cast<std::int16_t>(
                        hostage.completedActions + 1));
            }
            if (hostage.completedActions >= hostage.requiredActions) {
                soundCues_.push_back({hostage.asset->objectId,
                                      kRescueCuttingLoopSound,
                                      HostageSoundAction::StopLoop});
                Result result = player.enterScriptedState(
                    kRescueEndPlayerState, false);
                if (!result) {
                    return result;
                }
                hostage.phase = HostageRescuePhase::RescueEnd;
                hostage.phaseElapsedMilliseconds = 0;
            } else if (hostage.quickTimeElapsedMilliseconds >=
                       hostage.quickTimeDurationMilliseconds) {
                soundCues_.push_back({hostage.asset->objectId,
                                      kRescueCuttingLoopSound,
                                      HostageSoundAction::StopLoop});
                Result result = setPhase(hostage, HostageRescuePhase::Tied,
                                         player, objects, bonuses);
                if (!result) {
                    return result;
                }
            }
            break;
        case HostageRescuePhase::RescueEnd:
            if (player.activeStateId() != kRescueEndPlayerState ||
                player.scriptedStateAnimationFinished()) {
                player.clearScriptedState();
                Result result = setPhase(hostage, HostageRescuePhase::Release,
                                         player, objects, bonuses);
                if (!result) {
                    return result;
                }
            }
            break;
        case HostageRescuePhase::Release:
            if (objects.animationFinished(hostage.asset->objectId)) {
                Result result = setPhase(hostage, HostageRescuePhase::Thank,
                                         player, objects, bonuses);
                if (!result) {
                    return result;
                }
            }
            break;
        case HostageRescuePhase::Thank:
            if (objects.animationFinished(hostage.asset->objectId)) {
                Result result = setPhase(hostage, HostageRescuePhase::Freed,
                                         player, objects, bonuses);
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

bool LevelHostageRuntime::canStartRescue(
    const GameplayPlayer& player) const noexcept {
    return std::any_of(states_.begin(), states_.end(),
                       [&player, this](const LevelHostageState& hostage) {
                           return hostage.phase == HostageRescuePhase::Tied &&
                                  hostage.promptVisible &&
                                  !player.airborne() &&
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
                                     LevelBonusRuntime& bonuses) {
    if (hostage.asset == nullptr) {
        return Result::failure("Hostage phase target is invalid");
    }
    hostage.phase = phase;
    hostage.phaseElapsedMilliseconds = 0;
    hostage.promptVisible = false;
    switch (phase) {
    case HostageRescuePhase::Tied:
        player.clearScriptedState();
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
        soundCues_.push_back(
            {hostage.asset->objectId,
             hostage.asset->hostageIsWoman ? kWomanRescuedSound
                                            : kManRescuedSound,
             HostageSoundAction::PlayOnce});
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

bool LevelHostageRuntime::playerInsideEnableRadius(
    const LevelHostageState& hostage,
    const GameplayPlayer& player) const noexcept {
    return hostage.asset != nullptr &&
           distanceSquared(hostage.asset->position, player.position()) <=
               hostage.asset->hostageEnableRadius *
                   hostage.asset->hostageEnableRadius;
}

} // namespace usm::game
