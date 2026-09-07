#pragma once

#include <cstdint>

namespace usm::game {

// Exact integer generator used by irr::os::Randomizer in the shipped ARM
// executable. The original starts from 0x0f0f0f0f and exposes half-open
// bounded/range helpers through global random() wrappers.
class NativeRandomizer final {
public:
    static constexpr std::int32_t kInitialSeed = 0x0f0f0f0f;

    [[nodiscard]] std::int32_t next() noexcept;
    [[nodiscard]] std::int32_t bounded(
        std::int32_t maximumExclusive) noexcept;
    [[nodiscard]] std::int32_t range(
        std::int32_t minimumInclusive,
        std::int32_t maximumExclusive) noexcept;

    [[nodiscard]] std::int32_t state() const noexcept { return state_; }

private:
    std::int32_t state_{kInitialSeed};
};

} // namespace usm::game
