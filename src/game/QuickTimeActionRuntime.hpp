#pragma once

#include "game/ButtonMashProgress.hpp"

#include "assets/ColladaAnimation.hpp"
#include "core/Result.hpp"
#include "game/ButtonConfig.hpp"
#include "game/QuickTimeActionConfig.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace usm::game {

enum class QuickTimeActionTarget {
    Player,
    Npc,
};

enum class QuickTimeActionOutcome {
    Success,
    Failure,
};

struct QuickTimeActionHit {
    QuickTimeActionTarget target{QuickTimeActionTarget::Player};
    std::int16_t actionStateId{-1};
    std::int16_t animationFrame{};
    float damage{};
};

// Renderer-independent reconstruction of QTEActionManager's synchronized
// animation/config path (0x00389e1c, 0x0038a0e0, 0x0038a30c). This is
// separate from cinematic StartQTE: boss action 9 owns player and NPC clips,
// then drives BCONFIG entry 11 before selecting its authored success/failure
// action records.
class QuickTimeActionRuntime final {
public:
    void bind(const QuickTimeActionConfigDatabase& actions,
              const ButtonConfigDatabase& buttons,
              const assets::ColladaAnimationFile& playerAnimations,
              const assets::ColladaAnimationFile& npcAnimations) noexcept;
    [[nodiscard]] Result begin(std::int32_t actionStateId);
    [[nodiscard]] Result update(std::uint32_t elapsedMilliseconds,
                                bool actionPressed);
    void cancel() noexcept;

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] bool buttonPromptActive() const noexcept {
        return active_ && buttonDefinition_ != nullptr;
    }
    [[nodiscard]] std::int16_t actionStateId() const noexcept {
        return definition_ == nullptr ? -1 : definition_->id;
    }
    [[nodiscard]] std::string_view playerAnimation() const noexcept;
    [[nodiscard]] std::string_view npcAnimation() const noexcept;
    [[nodiscard]] bool animationLoops() const noexcept;
    [[nodiscard]] std::uint32_t stateElapsedMilliseconds() const noexcept {
        return stateElapsedMilliseconds_;
    }
    [[nodiscard]] std::uint32_t playerAnimationMilliseconds() const noexcept {
        return playerAnimationMilliseconds_;
    }
    [[nodiscard]] std::uint32_t npcAnimationMilliseconds() const noexcept {
        return npcAnimationMilliseconds_;
    }
    [[nodiscard]] std::uint32_t buttonElapsedMilliseconds() const noexcept {
        return buttonElapsedMilliseconds_;
    }
    [[nodiscard]] std::uint32_t buttonDurationMilliseconds() const noexcept;
    [[nodiscard]] std::int16_t completedActionCount() const noexcept {
        return buttonProgress_.completed();
    }
    [[nodiscard]] std::int16_t requiredActionCount() const noexcept;
    [[nodiscard]] float buttonProgress() const noexcept;
    [[nodiscard]] std::optional<std::int16_t>
    consumeEnteredActionState() noexcept;
    [[nodiscard]] std::vector<QuickTimeActionHit> consumeHits() noexcept;
    [[nodiscard]] std::optional<QuickTimeActionOutcome>
    consumeCompletion() noexcept;

private:
    [[nodiscard]] Result enter(std::int32_t actionStateId);
    [[nodiscard]] std::uint32_t animationDuration(
        const assets::ColladaAnimationFile* bank,
        std::string_view animation) const noexcept;
    [[nodiscard]] bool animationEnded(std::string_view animation,
                                      std::uint32_t time,
                                      std::uint32_t duration) const noexcept;
    void advanceAnimation(std::string_view animation, std::uint32_t duration,
                          std::uint32_t elapsedMilliseconds,
                          std::uint32_t& time) const noexcept;
    void queueCrossedAttackFrames(std::uint32_t previousPlayerMilliseconds,
                                  std::uint32_t previousNpcMilliseconds);

    const QuickTimeActionConfigDatabase* actions_{};
    const ButtonConfigDatabase* buttons_{};
    const assets::ColladaAnimationFile* playerAnimations_{};
    const assets::ColladaAnimationFile* npcAnimations_{};
    const QuickTimeActionDefinition* definition_{};
    const ButtonConfigDefinition* buttonDefinition_{};
    std::uint32_t stateElapsedMilliseconds_{};
    std::uint32_t playerAnimationMilliseconds_{};
    std::uint32_t npcAnimationMilliseconds_{};
    std::uint32_t playerAnimationDurationMilliseconds_{};
    std::uint32_t npcAnimationDurationMilliseconds_{};
    std::uint32_t buttonElapsedMilliseconds_{};
    std::size_t nextAttackFrameIndex_{};
    ButtonMashProgress buttonProgress_;
    std::optional<std::int16_t> enteredActionState_;
    std::vector<QuickTimeActionHit> pendingHits_;
    std::optional<QuickTimeActionOutcome> pendingOutcome_;
    std::optional<QuickTimeActionOutcome> completion_;
    bool active_{};
};

} // namespace usm::game
