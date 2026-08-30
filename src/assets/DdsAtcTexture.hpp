#pragma once

#include "assets/BtexTexture.hpp"
#include "core/Result.hpp"

#include <cstddef>
#include <span>

namespace usm::assets {

// Decoder for DDS textures containing ATC_RGBA_EXPLICIT_ALPHA blocks. The
// Android package gives these resources a .tga suffix, but their contents are
// standard DDS files with the ATCA FourCC.
class DdsAtcTexture final {
public:
    [[nodiscard]] Result load(std::span<const std::byte> bytes);

    [[nodiscard]] const RgbaImage& image() const noexcept { return image_; }

private:
    RgbaImage image_;
};

} // namespace usm::assets
