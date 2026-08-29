#pragma once

#include "core/Result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace usm::assets {

struct RgbaImage {
    std::uint32_t width{};
    std::uint32_t height{};
    std::vector<std::uint8_t> pixels;
};

// Decoder for the BTEX/PVR v2 textures used by the level and entity archives.
// The original loadPVRTexture is at 0x003dc120 (Ghidra 0x003ec120).
class BtexTexture final {
public:
    [[nodiscard]] Result load(std::span<const std::byte> bytes);

    [[nodiscard]] const std::vector<RgbaImage>& mipLevels() const noexcept {
        return mipLevels_;
    }
    [[nodiscard]] bool containsAlpha() const noexcept { return containsAlpha_; }

private:
    std::vector<RgbaImage> mipLevels_;
    bool containsAlpha_{};
};

} // namespace usm::assets
