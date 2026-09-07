#pragma once

namespace usm::game {

// Player::ResetObject (0x0034e258) loads Unit radius and height from image
// addresses 0x0056ec90 and 0x0056ec94. The preserved little-endian values are
// 50.0f (00 00 48 42) and 185.0f (00 00 39 43). The native Spider-Man
// PhysicsCapsuleShape constructor at 0x003d8a34 independently installs the
// same 50 cm radius on all three shape axes; Unit::GetHeight remains the
// separate 185 cm character extent used by combat and gameplay overlap tests.
inline constexpr float kPlayerCollisionRadiusCentimeters = 50.0F;
inline constexpr float kPlayerCollisionHeightCentimeters = 185.0F;
inline constexpr float kPlayerCollisionHalfHeightCentimeters =
    kPlayerCollisionHeightCentimeters * 0.5F;

} // namespace usm::game
