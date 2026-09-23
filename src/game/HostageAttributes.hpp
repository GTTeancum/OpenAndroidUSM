#pragma once

namespace usm::game {

// CHostage ctor (ELF 0x00328c20): default +0x36c = 85.0f.
// ProcessUserAttr (0x0032891e..0x00328938): VFP GT, subtract 100 only
// for a positive authored value. The shipped -1 sentinel keeps 85;
// it is not converted to -101 (below the character).
[[nodiscard]] constexpr float hostageButtonHeight(float authored) noexcept {
    return authored > 0.0F ? authored - 100.0F : 85.0F;
}

} // namespace usm::game
