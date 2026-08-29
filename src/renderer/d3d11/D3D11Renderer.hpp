#pragma once

#include "renderer/IRenderer.hpp"

#include <d3d11.h>
#include <wrl/client.h>

namespace usm::renderer {

class D3D11Renderer final : public IRenderer {
public:
    [[nodiscard]] Result initialize(HWND window, std::uint32_t width,
                                    std::uint32_t height) override;
    void renderFrame() override;

private:
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget_;
};

} // namespace usm::renderer

