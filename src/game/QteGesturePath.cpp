#include "game/QteGesturePath.hpp"

#include <bit>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace usm::game {
namespace {
bool coordinate(float base, std::int16_t offset, std::int16_t& out) {
    const float sum = base + static_cast<float>(offset);
    // Reject malformed metadata before the float->integer conversion. All
    // shipped values are representable; native VCVT then SXTH retains low 16.
    if (!std::isfinite(sum) || static_cast<double>(sum) < -2147483648.0 ||
        static_cast<double>(sum) >= 2147483648.0) { return false; }
    const auto word = static_cast<std::int32_t>(sum);
    out = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(word));
    return true;
}
}
Result QteGesturePath::bind(const ButtonConfigDefinition& config,
                            const assets::SpriteAtlas& atlas) {
    clear();
    if (config.interactionType != 1 || config.interactionValue < 11 ||
        config.interactionValue > 14) {
        return Result::failure("QTE drag references an unsupported interaction value");
    }
    const auto id = static_cast<std::size_t>(config.interactionValue - 9);
    if (id >= atlas.animations().size()) {
        return Result::failure("QTE drag animation is missing");
    }
    const auto& animation = atlas.animations()[id];
    const std::size_t first = animation.firstFrameIndex;
    const std::size_t count = animation.frameCount;
    if (count == 0 || first + count > atlas.animationFrames().size()) {
        return Result::failure("QTE drag animation has invalid frame metadata");
    }
    samples_.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto& frame = atlas.animationFrames()[first + i];
        QtePoint point;
        if (!coordinate(config.screenX, frame.x, point.x) ||
            !coordinate(config.screenY, frame.y, point.y)) {
            clear();
            return Result::failure("QTE drag contains an invalid coordinate");
        }
        samples_.push_back(point);
    }
    horizontal_ = config.interactionValue == 11 || config.interactionValue == 12;
    return Result::success();
}
std::size_t QteGesturePath::nearest(QtePoint input) const noexcept {
    std::size_t best = 0;
    if (samples_.empty()) { return best; }
    const int axis = horizontal_ ? input.x : input.y;
    int bestDistance = std::abs(axis - (horizontal_ ? samples_[0].x : samples_[0].y));
    for (std::size_t i = 1; i < samples_.size(); ++i) {
        const int distance = std::abs(axis - (horizontal_ ? samples_[i].x : samples_[i].y));
        if (distance < bestDistance) { best = i; bestDistance = distance; }
    }
    return best;
}
bool QteGesturePath::releaseSucceeded(QtePoint input) const noexcept {
    if (samples_.empty()) { return false; }
    return std::abs(static_cast<int>(input.x) - end().x) <= 10 &&
           std::abs(static_cast<int>(input.y) - end().y) <= 10;
}
} // namespace usm::game
