#pragma once

#include "core/Result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace usm::assets {

struct SpriteModule {
    std::uint8_t imageIndex{};
    std::uint16_t x{};
    std::uint16_t y{};
    std::uint16_t width{};
    std::uint16_t height{};
};

struct SpriteFrameModule {
    std::uint16_t moduleIndex{};
    std::uint8_t flags{};
    std::int16_t x{};
    std::int16_t y{};
};

struct SpriteFrame {
    std::uint8_t moduleCount{};
    std::uint16_t firstModuleIndex{};
};

struct SpriteAnimationFrame {
    std::uint16_t frameIndex{};
    std::uint8_t duration{};
    std::uint8_t flags{};
    std::int16_t x{};
    std::int16_t y{};
};

struct SpriteAnimation {
    std::uint8_t frameCount{};
    std::uint16_t firstFrameIndex{};
};

// Native representation of the compact sprite metadata consumed by
// CSprite::LoadSpriteData at original address 0x002e88d4.
class SpriteAtlas final {
public:
    [[nodiscard]] Result load(std::span<const std::byte> bytes);

    [[nodiscard]] std::uint16_t flags() const noexcept { return flags_; }
    [[nodiscard]] const std::vector<SpriteModule>& modules() const noexcept {
        return modules_;
    }
    [[nodiscard]] const std::vector<SpriteFrameModule>& frameModules() const
        noexcept {
        return frameModules_;
    }
    [[nodiscard]] const std::vector<SpriteFrame>& frames() const noexcept {
        return frames_;
    }
    [[nodiscard]] const std::vector<SpriteAnimationFrame>& animationFrames()
        const noexcept {
        return animationFrames_;
    }
    [[nodiscard]] const std::vector<SpriteAnimation>& animations() const
        noexcept {
        return animations_;
    }
    [[nodiscard]] std::span<const SpriteFrameModule> modulesForFrame(
        std::size_t frameIndex) const noexcept;

private:
    std::uint16_t flags_{};
    std::vector<SpriteModule> modules_;
    std::vector<SpriteFrameModule> frameModules_;
    std::vector<SpriteFrame> frames_;
    std::vector<SpriteAnimationFrame> animationFrames_;
    std::vector<SpriteAnimation> animations_;
};

} // namespace usm::assets
