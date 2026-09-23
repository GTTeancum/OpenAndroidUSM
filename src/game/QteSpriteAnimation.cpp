#include "game/QteSpriteAnimation.hpp"
#include <bit>
#include <cmath>

namespace usm::game {
void QteSpriteAnimation::bind(const assets::SpriteAtlas& atlas) noexcept {
    atlas_ = &atlas;
    animationId_ = frameIndex_ = ticks_ = 0;
    residual_ = 0;
    looped_ = false;
}
void QteSpriteAnimation::restart() noexcept {
    frameIndex_ = ticks_ = 0;
    looped_ = false;
    // CSpriteInstance::Restart deliberately retains the fractional residue.
}
void QteSpriteAnimation::setAnimation(std::int16_t id, bool safe) noexcept {
    if (atlas_ == nullptr || id < 0 || static_cast<std::size_t>(id) >= atlas_->animations().size()) { return; }
    if (id != animationId_ || safe) { animationId_ = id; restart(); }
    looped_ = false;
}
const assets::SpriteAnimationFrame* QteSpriteAnimation::frame() const noexcept {
    if (atlas_ == nullptr || animationId_ < 0 || frameIndex_ < 0 ||
        static_cast<std::size_t>(animationId_) >= atlas_->animations().size()) { return nullptr; }
    const auto& anim = atlas_->animations()[animationId_];
    const auto index = static_cast<std::size_t>(anim.firstFrameIndex) + frameIndex_;
    if (frameIndex_ >= anim.frameCount || index >= atlas_->animationFrames().size()) { return nullptr; }
    return &atlas_->animationFrames()[index];
}
void QteSpriteAnimation::update(std::uint32_t elapsed) noexcept {
    const auto* current = frame();
    if (!current || ticks_ < 0 || current->duration == 0) { return; }
    residual_ += static_cast<float>(elapsed);
    std::uint32_t steps = 0;
    // ELF data 0x00505380 is float32 50.0. Strict >, not >=.
    while (residual_ > 50.0F) {
        const float next = residual_ - 50.0F;
        // Malformed external steps beyond float resolution must not hang the
        // host. The original Application supplies a 50 ms real step.
        if (next == residual_ || !std::isfinite(next)) { return; }
        residual_ = next;
        ticks_ = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(ticks_) + 1U));
        ++steps;
    }
    if (current->duration > ticks_) { return; }
    frameIndex_ = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(frameIndex_) + steps));
    ticks_ = 0;
    if (frameIndex_ >= atlas_->animations()[animationId_].frameCount) {
        frameIndex_ = 0;
        looped_ = true;
    }
}
bool QteSpriteAnimation::ended() const noexcept {
    if (looped_) { return true; }
    const auto* current = frame();
    if (!current || frameIndex_ != atlas_->animations()[animationId_].frameCount - 1) { return false; }
    return current->duration == 0 || ticks_ == current->duration - 1;
}
} // namespace usm::game
