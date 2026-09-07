#pragma once

#include "assets/BtexTexture.hpp"
#include "core/Result.hpp"

#include <cstddef>
#include <span>

namespace usm::assets {

// Decoder for the uncompressed true-color TGA used by bg_suit.tga. The
// original resource is a real bottom-origin TGA, unlike the ATCA DDS payloads
// shipped under most other .tga names.
class TgaTexture final {
public:
    [[nodiscard]] Result load(std::span<const std::byte> bytes);

    [[nodiscard]] const RgbaImage& image() const noexcept { return image_; }

private:
    RgbaImage image_;
};

} // namespace usm::assets
