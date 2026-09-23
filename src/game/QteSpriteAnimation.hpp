#pragma once

#include "assets/SpriteAtlas.hpp"

#include <cstdint>

namespace usm::game {
// The zero-flags CSpriteInstance timing used by CQTEManager. Original ELF:
// InitInstance 0x002d9af8, Restart 0x002d9cc4, SetAnim 0x002d9ce8,
// SetAnimSafe 0x002d9d04, UpdateSpriteAnim 0x002d9c10, IsAnimEnded 0x002d9d1c.
// This is not a conventional duration-summing animation player.
class QteSpriteAnimation final {
public:
    void bind(const assets::SpriteAtlas& atlas) noexcept;
    void setAnimation(std::int16_t id, bool restartEvenIfSame = false) noexcept;
    void update(std::uint32_t realMilliseconds) noexcept;
    [[nodiscard]] bool ended() const noexcept;
    [[nodiscard]] std::int16_t animationId() const noexcept { return animationId_; }
    [[nodiscard]] std::int16_t frameIndex() const noexcept { return frameIndex_; }
    [[nodiscard]] std::int16_t ticks() const noexcept { return ticks_; }
    [[nodiscard]] float residualMilliseconds() const noexcept { return residual_; }
    [[nodiscard]] bool looped() const noexcept { return looped_; }
    [[nodiscard]] const assets::SpriteAnimationFrame* frame() const noexcept;
private:
    void restart() noexcept;
    const assets::SpriteAtlas* atlas_{};
    std::int16_t animationId_{};
    std::int16_t frameIndex_{};
    std::int16_t ticks_{};
    float residual_{};
    bool looped_{};
};
} // namespace usm::game
