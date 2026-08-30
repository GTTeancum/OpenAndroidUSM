#pragma once

#include "assets/IrrScene.hpp"
#include "core/Result.hpp"
#include "game/LevelOneBootstrap.hpp"

#include <cstddef>
#include <span>

namespace usm::game {

struct SlideCatch {
    const LevelSlideAsset* slide{};
    std::size_t segmentIndex{};
    assets::Vector3 projectedPosition;
    float distanceSquared{};
};

struct SlideExit {
    assets::Vector3 velocityCentimetersPerSecond;
    bool useGravity{true};
    bool electricShock{};
};

// Portable CSlider segment graph. CSlider::Update (0x0031d920) chooses the
// nearest enabled segment inside the recovered 640000 cm^2 catch threshold,
// preserves velocity magnitude while switching segments, and follows the
// first WayPoint link until the chain ends.
class LevelSlideRuntime final {
public:
    void bind(std::span<const LevelSlideAsset> slides,
              std::span<const LevelWayPointAsset> waypoints) noexcept;
    [[nodiscard]] SlideCatch findCatch(
        const assets::Vector3& playerPosition) const noexcept;
    [[nodiscard]] Result start(const SlideCatch& caught,
                               float speedCentimetersPerSecond) noexcept;
    [[nodiscard]] Result start(std::int32_t slideId,
                               float speedCentimetersPerSecond) noexcept;
    void update(std::uint32_t elapsedMilliseconds) noexcept;
    [[nodiscard]] SlideExit finish() noexcept;

    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] const assets::Vector3& position() const noexcept {
        return position_;
    }
    [[nodiscard]] const assets::Vector3& direction() const noexcept {
        return direction_;
    }
    [[nodiscard]] const LevelSlideAsset* slide() const noexcept {
        return activeSlide_;
    }

private:
    [[nodiscard]] const LevelWayPointAsset* waypoint(
        std::int32_t id) const noexcept;
    [[nodiscard]] Result enterSegment(std::size_t index,
                                      float offset) noexcept;

    std::span<const LevelSlideAsset> slides_;
    std::span<const LevelWayPointAsset> waypoints_;
    const LevelSlideAsset* activeSlide_{};
    bool active_{};
    std::size_t segmentIndex_{};
    float distanceAlongSegment_{};
    float segmentLength_{};
    float speedCentimetersPerSecond_{};
    assets::Vector3 position_;
    assets::Vector3 direction_{1.0F, 0.0F, 0.0F};
};

} // namespace usm::game
