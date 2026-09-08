#include "game/LevelHintRuntime.hpp"

#include <algorithm>
#include <charconv>
#include <limits>
#include <string_view>

namespace usm::game {
namespace {

constexpr std::uint32_t kSpriteTickMilliseconds = 50;

bool parseInteger(std::string_view text, std::int32_t& value) noexcept {
    const auto parsed =
        std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} &&
           parsed.ptr == text.data() + text.size();
}

bool parseBoolean(std::string_view text, bool& value) noexcept {
    if (text == "true" || text == "1") {
        value = true;
        return true;
    }
    if (text == "false" || text == "0") {
        value = false;
        return true;
    }
    return false;
}

std::int32_t commandObjectId(const CinematicThread& thread,
                             const CinematicCommand& command) noexcept {
    std::int32_t objectId = thread.objectId;
    const CinematicAttribute* explicitObject =
        command.findAttribute("ObjectID");
    std::int32_t parsed = -1;
    if (explicitObject != nullptr &&
        parseInteger(explicitObject->value, parsed) && parsed >= 0) {
        objectId = parsed;
    }
    return objectId;
}

} // namespace

Result LevelHintRuntime::initialize(std::span<const LevelHintAsset> hints) {
    states_.clear();
    states_.reserve(hints.size() + 2);
    bool runtimeCombatHintsCreated = false;
    for (const LevelHintAsset& hint : hints) {
        if (hint.animationIndex < 0 ||
            static_cast<std::size_t>(hint.animationIndex) >=
                hint.atlas.animations().size()) {
            states_.clear();
            return Result::failure("Level hint references an invalid animation");
        }
        LevelHintState state;
        state.asset = &hint;
        state.position = hint.position;
        state.animationIndex = hint.animationIndex;
        state.visible = hint.visible;
        updateFrame(state);
        states_.push_back(state);

        // Player::SpawnPlayer (0x00345260, 0x003455fe-0x0034564a) creates a
        // separate HintManager sense cue from hintbb animation 0, links it
        // to Bip01_Head, and leaves it hidden. Level 1 packages an authored
        // node with the same sprite/animation for the tutorial, so retain a
        // second runtime state backed by those decoded resources.
        if (!runtimeCombatHintsCreated &&
            hint.spriteFile == "hintbb.bsprite" &&
            hint.animationIndex == 0) {
            runtimeCombatHintsCreated = true;
            LevelHintState combatSense = state;
            combatSense.visible = false;
            combatSense.combatSenseCue = true;
            states_.push_back(combatSense);

            // Player::SpawnPlayer (0x00345260, 0x00345662-0x00345688)
            // obtains HintManager::GetPlayerTargetHint, assigns the same
            // hintbb sprite, selects animation 4, and hides it. The three
            // health states selected by UpdateTargetPointer are animations
            // 4, 5, and 6, so all must exist in the shipped atlas.
            if (hint.atlas.animations().size() <= 6) {
                states_.clear();
                return Result::failure(
                    "hintbb sprite is missing combat target animations");
            }
            LevelHintState combatTarget = state;
            combatTarget.animationIndex = 4;
            combatTarget.animationTimeMilliseconds = 0;
            combatTarget.visible = false;
            combatTarget.combatTargetCue = true;
            updateFrame(combatTarget);
            states_.push_back(combatTarget);
        }
    }
    return Result::success();
}

void LevelHintRuntime::setCombatSenseCueVisible(bool visible) noexcept {
    const auto match = std::find_if(
        states_.begin(), states_.end(), [](const LevelHintState& state) {
            return state.combatSenseCue;
        });
    if (match != states_.end()) {
        match->visible = visible;
    }
}

bool LevelHintRuntime::combatSenseCueVisible() const noexcept {
    return std::any_of(states_.begin(), states_.end(),
                       [](const LevelHintState& state) {
                           return state.combatSenseCue && state.visible;
                       });
}

bool LevelHintRuntime::setCombatTargetCue(
    std::int32_t objectId, const assets::Vector3& position,
    float health, float maximumHealth) noexcept {
    LevelHintState* state = combatTargetCueMutable();
    if (state == nullptr || objectId < 0 || maximumHealth <= 0.0F) {
        return false;
    }
    const float ratio = health / maximumHealth;
    // Player::UpdateTargetPointer (0x00342ed0, 0x00342fac-0x00342ffc)
    // uses strict greater-than comparisons. Equality at 0.6 selects 5 and
    // equality at 0.3 selects 6.
    const std::int32_t animationIndex = ratio > 0.6F ? 4
        : ratio > 0.3F ? 5
                       : 6;
    const bool changed = !state->visible ||
        state->combatTargetObjectId != objectId ||
        state->animationIndex != animationIndex;
    if (state->animationIndex != animationIndex) {
        // CSpriteInstance::SetAnim (0x002e9ce8) restarts only when the
        // animation index actually changes.
        state->animationIndex = animationIndex;
        state->animationTimeMilliseconds = 0;
    }
    state->combatTargetObjectId = objectId;
    state->position = position;
    state->visible = true;
    updateFrame(*state);
    return changed;
}

bool LevelHintRuntime::clearCombatTargetCue() noexcept {
    LevelHintState* state = combatTargetCueMutable();
    if (state == nullptr || !state->visible) {
        return false;
    }
    state->visible = false;
    state->combatTargetObjectId = -1;
    return true;
}

const LevelHintState* LevelHintRuntime::combatTargetCue() const noexcept {
    const auto match = std::find_if(
        states_.begin(), states_.end(), [](const LevelHintState& state) {
            return state.combatTargetCue;
        });
    return match == states_.end() ? nullptr : &*match;
}

Result LevelHintRuntime::applyCinematicCommand(
    const CinematicThread& thread, const CinematicCommand& command) {
    if (command.name != "SetVisible") {
        return Result::success();
    }
    LevelHintState* state = findMutable(commandObjectId(thread, command));
    if (state == nullptr) {
        return Result::success();
    }
    const CinematicAttribute* visible = command.findAttribute("Visible");
    bool parsedVisible = true;
    if (visible != nullptr && !parseBoolean(visible->value, parsedVisible)) {
        return Result::failure("Hint SetVisible has an invalid Visible value");
    }
    state->visible = parsedVisible;
    return Result::success();
}

void LevelHintRuntime::update(
    std::uint32_t elapsedMilliseconds,
    const PositionResolver& resolvePosition) noexcept {
    for (LevelHintState& state : states_) {
        if (!state.visible || state.asset == nullptr) {
            continue;
        }
        if (resolvePosition && !state.combatTargetCue) {
            const auto linked =
                resolvePosition(state.asset->linkedObjectId);
            if (linked) {
                state.position = *linked;
                // HintBase::UpdatePosition uses Bip01_Head when available,
                // then adds the recovered 35 + 25 unit head clearance. The
                // gameplay root is at the feet, so retain the player's
                // reconstructed 160-unit standing height as the fallback.
                // The dynamic sense cue is linked directly to Bip01_Head
                // with a 50 cm Z offset in Player::SpawnPlayer. The authored
                // HintBase path adds 35 + 25 cm above the reconstructed
                // 160 cm player root.
                state.position.z += state.combatSenseCue ? 210.0F : 220.0F;
            }
        }
        state.animationTimeMilliseconds =
            static_cast<std::uint32_t>(std::min<std::uint64_t>(
                static_cast<std::uint64_t>(state.animationTimeMilliseconds) +
                    elapsedMilliseconds,
                std::numeric_limits<std::uint32_t>::max()));
        updateFrame(state);
    }
}

const LevelHintState* LevelHintRuntime::find(std::int32_t objectId) const
    noexcept {
    const auto match = std::find_if(
        states_.begin(), states_.end(), [objectId](const LevelHintState& state) {
            return !state.combatSenseCue && !state.combatTargetCue &&
                   state.asset != nullptr &&
                   state.asset->objectId == objectId;
        });
    return match == states_.end() ? nullptr : &*match;
}

LevelHintState* LevelHintRuntime::findMutable(std::int32_t objectId) noexcept {
    const auto match = std::find_if(
        states_.begin(), states_.end(), [objectId](const LevelHintState& state) {
            return !state.combatSenseCue && !state.combatTargetCue &&
                   state.asset != nullptr &&
                   state.asset->objectId == objectId;
        });
    return match == states_.end() ? nullptr : &*match;
}

LevelHintState* LevelHintRuntime::combatTargetCueMutable() noexcept {
    const auto match = std::find_if(
        states_.begin(), states_.end(), [](const LevelHintState& state) {
            return state.combatTargetCue;
        });
    return match == states_.end() ? nullptr : &*match;
}

void LevelHintRuntime::updateFrame(LevelHintState& state) noexcept {
    if (state.asset == nullptr || state.animationIndex < 0) {
        state.animationFrameIndex = -1;
        state.frameIndex = -1;
        return;
    }
    const assets::SpriteAtlas& atlas = state.asset->atlas;
    const auto& animations = atlas.animations();
    const auto& animationFrames = atlas.animationFrames();
    const std::size_t animationIndex =
        static_cast<std::size_t>(state.animationIndex);
    if (animationIndex >= animations.size()) {
        state.animationFrameIndex = -1;
        state.frameIndex = -1;
        return;
    }
    const assets::SpriteAnimation& animation = animations[animationIndex];
    if (animation.frameCount == 0 ||
        animation.firstFrameIndex >= animationFrames.size()) {
        state.animationFrameIndex = -1;
        state.frameIndex = -1;
        return;
    }
    std::uint32_t durationMilliseconds = 0;
    for (std::size_t index = 0; index < animation.frameCount; ++index) {
        const assets::SpriteAnimationFrame& frame =
            animationFrames[animation.firstFrameIndex + index];
        durationMilliseconds +=
            static_cast<std::uint32_t>(frame.duration) *
            kSpriteTickMilliseconds;
    }
    if (durationMilliseconds == 0) {
        state.animationFrameIndex = -1;
        state.frameIndex = -1;
        return;
    }
    const std::uint32_t localTime =
        state.animationTimeMilliseconds % durationMilliseconds;
    std::uint32_t endTime = 0;
    for (std::size_t index = 0; index < animation.frameCount; ++index) {
        const assets::SpriteAnimationFrame& frame =
            animationFrames[animation.firstFrameIndex + index];
        endTime += static_cast<std::uint32_t>(frame.duration) *
                   kSpriteTickMilliseconds;
        if (localTime < endTime) {
            state.animationFrameIndex = static_cast<std::int32_t>(
                animation.firstFrameIndex + index);
            state.frameIndex = static_cast<std::int32_t>(frame.frameIndex);
            return;
        }
    }
    state.animationFrameIndex =
        static_cast<std::int32_t>(animation.firstFrameIndex);
    state.frameIndex = static_cast<std::int32_t>(
        animationFrames[animation.firstFrameIndex].frameIndex);
}

} // namespace usm::game
