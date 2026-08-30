#include "assets/SpriteAtlas.hpp"

#include <limits>

namespace usm::assets {
namespace {

constexpr std::uint16_t kSpriteMagic = 0xa9d1;

class Reader final {
public:
    explicit Reader(std::span<const std::byte> bytes) : bytes_(bytes) {}

    bool u8(std::uint8_t& value) noexcept {
        if (remaining() < 1) {
            return false;
        }
        value = std::to_integer<std::uint8_t>(bytes_[offset_++]);
        return true;
    }

    bool u16(std::uint16_t& value) noexcept {
        if (remaining() < 2) {
            return false;
        }
        value = static_cast<std::uint16_t>(
            std::to_integer<std::uint8_t>(bytes_[offset_]) |
            (std::to_integer<std::uint8_t>(bytes_[offset_ + 1]) << 8U));
        offset_ += 2;
        return true;
    }

    bool s16(std::int16_t& value) noexcept {
        std::uint16_t encoded{};
        if (!u16(encoded)) {
            return false;
        }
        value = static_cast<std::int16_t>(encoded);
        return true;
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - offset_;
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t offset_{};
};

} // namespace

Result SpriteAtlas::load(std::span<const std::byte> bytes) {
    flags_ = 0;
    modules_.clear();
    frameModules_.clear();
    frames_.clear();
    animationFrames_.clear();
    animations_.clear();

    Reader reader(bytes);
    std::uint16_t magic{};
    std::uint16_t moduleCount{};
    std::uint16_t frameModuleCount{};
    std::uint16_t frameCount{};
    std::uint16_t animationFrameCount{};
    std::uint16_t animationCount{};
    if (!reader.u16(magic) || !reader.u16(flags_) ||
        !reader.u16(moduleCount) || !reader.u16(frameModuleCount) ||
        !reader.u16(frameCount) || !reader.u16(animationFrameCount) ||
        !reader.u16(animationCount) || magic != kSpriteMagic) {
        return Result::failure("Sprite metadata header is missing or invalid");
    }

    modules_.resize(moduleCount);
    for (auto& module : modules_) {
        if (!reader.u8(module.imageIndex)) {
            return Result::failure("Sprite module image indices are truncated");
        }
    }
    for (auto& module : modules_) {
        if (!reader.u16(module.x)) {
            return Result::failure("Sprite module X coordinates are truncated");
        }
    }
    for (auto& module : modules_) {
        if (!reader.u16(module.y)) {
            return Result::failure("Sprite module Y coordinates are truncated");
        }
    }
    for (auto& module : modules_) {
        if (!reader.u16(module.width)) {
            return Result::failure("Sprite module widths are truncated");
        }
    }
    for (auto& module : modules_) {
        if (!reader.u16(module.height)) {
            return Result::failure("Sprite module heights are truncated");
        }
    }

    frameModules_.resize(frameModuleCount);
    for (auto& module : frameModules_) {
        if (!reader.u16(module.moduleIndex)) {
            return Result::failure("Sprite frame module indices are truncated");
        }
    }
    for (auto& module : frameModules_) {
        if (!reader.u8(module.flags)) {
            return Result::failure("Sprite frame module flags are truncated");
        }
    }
    for (auto& module : frameModules_) {
        if (!reader.s16(module.x)) {
            return Result::failure("Sprite frame module X offsets are truncated");
        }
    }
    for (auto& module : frameModules_) {
        if (!reader.s16(module.y)) {
            return Result::failure("Sprite frame module Y offsets are truncated");
        }
    }

    frames_.resize(frameCount);
    for (auto& frame : frames_) {
        if (!reader.u8(frame.moduleCount)) {
            return Result::failure("Sprite frame module counts are truncated");
        }
    }
    for (auto& frame : frames_) {
        if (!reader.u16(frame.firstModuleIndex)) {
            return Result::failure("Sprite frame offsets are truncated");
        }
    }

    animationFrames_.resize(animationFrameCount);
    for (auto& frame : animationFrames_) {
        if (!reader.u16(frame.frameIndex)) {
            return Result::failure("Sprite animation frame indices are truncated");
        }
    }
    for (auto& frame : animationFrames_) {
        if (!reader.u8(frame.duration)) {
            return Result::failure("Sprite animation durations are truncated");
        }
    }
    for (auto& frame : animationFrames_) {
        if (!reader.u8(frame.flags)) {
            return Result::failure("Sprite animation flags are truncated");
        }
    }
    for (auto& frame : animationFrames_) {
        if (!reader.s16(frame.x)) {
            return Result::failure("Sprite animation X offsets are truncated");
        }
    }
    for (auto& frame : animationFrames_) {
        if (!reader.s16(frame.y)) {
            return Result::failure("Sprite animation Y offsets are truncated");
        }
    }

    animations_.resize(animationCount);
    for (auto& animation : animations_) {
        if (!reader.u8(animation.frameCount)) {
            return Result::failure("Sprite animation counts are truncated");
        }
    }
    for (auto& animation : animations_) {
        if (!reader.u16(animation.firstFrameIndex)) {
            return Result::failure("Sprite animation offsets are truncated");
        }
    }
    if (reader.remaining() != 0) {
        return Result::failure("Sprite metadata contains unexpected trailing data");
    }

    for (const SpriteFrameModule& frameModule : frameModules_) {
        if (frameModule.moduleIndex >= modules_.size()) {
            return Result::failure("Sprite frame references an invalid module");
        }
    }
    for (const SpriteFrame& frame : frames_) {
        if (frame.firstModuleIndex > frameModules_.size() ||
            frame.moduleCount >
                frameModules_.size() - frame.firstModuleIndex) {
            return Result::failure("Sprite frame module span is out of bounds");
        }
    }
    for (const SpriteAnimationFrame& frame : animationFrames_) {
        if (frame.frameIndex >= frames_.size()) {
            return Result::failure("Sprite animation references an invalid frame");
        }
    }
    for (const SpriteAnimation& animation : animations_) {
        if (animation.firstFrameIndex > animationFrames_.size() ||
            animation.frameCount >
                animationFrames_.size() - animation.firstFrameIndex) {
            return Result::failure("Sprite animation span is out of bounds");
        }
    }
    return Result::success();
}

std::span<const SpriteFrameModule> SpriteAtlas::modulesForFrame(
    std::size_t frameIndex) const noexcept {
    if (frameIndex >= frames_.size()) {
        return {};
    }
    const SpriteFrame& frame = frames_[frameIndex];
    return std::span<const SpriteFrameModule>(
        frameModules_.data() + frame.firstModuleIndex, frame.moduleCount);
}

} // namespace usm::assets
