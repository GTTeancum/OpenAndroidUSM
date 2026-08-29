#pragma once

#include "core/Result.hpp"

#include <Windows.h>

#include <cstdint>

namespace usm::renderer {

class IRenderer {
public:
    virtual ~IRenderer() = default;
    [[nodiscard]] virtual Result initialize(HWND window, std::uint32_t width,
                                            std::uint32_t height) = 0;
    virtual void renderFrame() = 0;
};

} // namespace usm::renderer

