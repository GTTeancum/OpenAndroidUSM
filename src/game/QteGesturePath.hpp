#pragma once

#include "assets/SpriteAtlas.hpp"
#include "core/Result.hpp"
#include "game/ButtonConfig.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace usm::game {
// Screen-relative intent. XInput Y is converted by the PC adapter, not by
// generating virtual touch coordinates. None is never a successful gesture.
enum class QteDirection : std::uint8_t { None, Left, Right, Up, Down };

struct QtePoint {
    std::int16_t x{};
    std::int16_t y{};
    bool operator==(const QtePoint&) const = default;
};

// CQTEManager::CalculateEndPos / UpdateDragState / onEvent, ELF
// 0x0037a3f8 / 0x0037acd4 / 0x0037abf0. No invented curve or percentage target.
class QteGesturePath final {
public:
    [[nodiscard]] Result bind(const ButtonConfigDefinition& config,
                              const assets::SpriteAtlas& atlas);
    void clear() noexcept { samples_.clear(); horizontal_ = false; }
    [[nodiscard]] std::span<const QtePoint> samples() const noexcept { return samples_; }
    [[nodiscard]] bool horizontal() const noexcept { return horizontal_; }
    [[nodiscard]] QtePoint start() const noexcept { return samples_.empty() ? QtePoint{} : samples_.front(); }
    [[nodiscard]] QtePoint end() const noexcept { return samples_.empty() ? QtePoint{} : samples_.back(); }
    [[nodiscard]] QteDirection direction() const noexcept {
        if (samples_.empty()) { return QteDirection::None; }
        const int delta = horizontal_ ? static_cast<int>(end().x) - start().x
                                      : static_cast<int>(end().y) - start().y;
        if (delta == 0) { return QteDirection::None; }
        return horizontal_ ? (delta > 0 ? QteDirection::Right : QteDirection::Left)
                           : (delta > 0 ? QteDirection::Down : QteDirection::Up);
    }
    [[nodiscard]] std::size_t nearest(QtePoint input) const noexcept;
    [[nodiscard]] bool releaseSucceeded(QtePoint input) const noexcept;
private:
    std::vector<QtePoint> samples_;
    bool horizontal_{};
};
} // namespace usm::game
