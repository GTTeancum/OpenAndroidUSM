#include "game/NativeRandomizer.hpp"

#include <cstdint>

namespace usm::game {

std::int32_t NativeRandomizer::next() noexcept {
    // irr::os::Randomizer::rand (0x0043ae48) uses Schrage's overflow-safe
    // form with modulus 2147483399, multiplier 40692, quotient 52774, and
    // remainder 3791. The native code divides by -52774, so the quotient
    // term below is already negative for the positive generator state.
    constexpr std::int32_t kDivisor = 52774;
    constexpr std::int32_t kMultiplier = 40692;
    constexpr std::int32_t kRemainderMultiplier = 3791;
    constexpr std::int32_t kModulus = 2147483399;

    const std::int32_t quotient = state_ / -kDivisor;
    const std::int32_t remainder = state_ % kDivisor;
    std::int64_t nextState =
        static_cast<std::int64_t>(remainder) * kMultiplier +
        static_cast<std::int64_t>(quotient) * kRemainderMultiplier;
    if (nextState < 0) {
        nextState += kModulus;
    }
    state_ = static_cast<std::int32_t>(nextState);
    return state_;
}

std::int32_t NativeRandomizer::bounded(
    std::int32_t maximumExclusive) noexcept {
    // global random(int) at 0x003730b0 returns zero for a non-positive bound.
    return maximumExclusive < 1 ? 0 : next() % maximumExclusive;
}

std::int32_t NativeRandomizer::range(
    std::int32_t minimumInclusive,
    std::int32_t maximumExclusive) noexcept {
    // global random(int,int) at 0x003730cc uses [minimum, maximum). Equal
    // endpoints preserve the endpoint; a reversed interval returns zero.
    const std::int32_t width = maximumExclusive - minimumInclusive;
    if (width < 1) {
        return width == 0 ? minimumInclusive : 0;
    }
    return minimumInclusive + next() % width;
}

} // namespace usm::game
