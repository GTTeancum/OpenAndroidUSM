#include "renderer/d3d11/D3D11Renderer.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void checkClearPixel(const std::array<std::uint8_t, 4>& rgba) {
    constexpr std::array<int, 4> expected{6, 11, 22, 255};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const int delta = std::abs(static_cast<int>(rgba[i]) - expected[i]);
        require(delta <= 1, "D3D11 clear-color readback differs from expected UNORM value");
    }
}

} // namespace

int main() {
    try {
        usm::renderer::D3D11Renderer renderer;
        const usm::Result init = renderer.initializeOffscreen(64, 32);
        require(static_cast<bool>(init), init.message());

        renderer.renderFrame();

        std::array<std::uint8_t, 4> first{};
        std::array<std::uint8_t, 4> center{};
        const usm::Result firstRead = renderer.readBackPixel(0, 0, first);
        require(static_cast<bool>(firstRead), firstRead.message());
        const usm::Result centerRead = renderer.readBackPixel(32, 16, center);
        require(static_cast<bool>(centerRead), centerRead.message());

        checkClearPixel(first);
        checkClearPixel(center);
        require(first == center, "Empty WARP frame was not spatially uniform");

        std::cout << "PASS D3D11 WARP render/readback backend\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL D3D11 backend: " << error.what() << '\n';
        return 1;
    }
}
