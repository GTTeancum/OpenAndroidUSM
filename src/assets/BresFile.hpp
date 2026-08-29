#pragma once

#include "core/Result.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace usm::assets {

struct BresHeader {
    std::uint16_t byteOrderMarker{};
    std::uint16_t runtimeFlags{};
    std::uint32_t headerSize{};
    std::uint32_t fileSize{};
    std::uint32_t relocationCount{};
    std::uint32_t relocationTableOffset{};
};

struct BresRelocation {
    std::uint32_t pointerFieldOffset{};
    std::uint32_t targetOffset{};
};

// Portable, non-mutating replacement for irr::res::File::Init at original
// address 0x0042f028 (Ghidra image address 0x0043f028). The ARM code rewrote
// 32-bit offsets as pointers in place; this view remains valid on 64-bit hosts.
class BresFile final {
public:
    [[nodiscard]] Result load(std::span<const std::byte> bytes);

    [[nodiscard]] const BresHeader& header() const noexcept { return header_; }
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
        return bytes_;
    }
    [[nodiscard]] const std::vector<BresRelocation>& relocations() const noexcept {
        return relocations_;
    }
    [[nodiscard]] std::optional<std::uint32_t> resolvePointer(
        std::uint32_t pointerFieldOffset) const noexcept;

private:
    std::vector<std::byte> bytes_;
    BresHeader header_;
    std::vector<BresRelocation> relocations_;
};

} // namespace usm::assets
