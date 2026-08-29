#include "renderer/d3d11/D3D11Renderer.hpp"

#include <array>
#include <sstream>

namespace usm::renderer {

Result D3D11Renderer::initialize(HWND window, std::uint32_t width,
                                 std::uint32_t height) {
    DXGI_SWAP_CHAIN_DESC swapChainDescription{};
    swapChainDescription.BufferDesc.Width = width;
    swapChainDescription.BufferDesc.Height = height;
    swapChainDescription.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDescription.SampleDesc.Count = 1;
    swapChainDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDescription.BufferCount = 2;
    swapChainDescription.OutputWindow = window;
    swapChainDescription.Windowed = TRUE;
    swapChainDescription.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL selectedFeatureLevel{};
    constexpr std::array requestedFeatureLevels{
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    UINT flags = 0;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        requestedFeatureLevels.data(),
        static_cast<UINT>(requestedFeatureLevels.size()), D3D11_SDK_VERSION,
        &swapChainDescription, &swapChain_, &device_, &selectedFeatureLevel,
        &context_);

    if (FAILED(result) && (flags & D3D11_CREATE_DEVICE_DEBUG) != 0) {
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        result = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            requestedFeatureLevels.data(),
            static_cast<UINT>(requestedFeatureLevels.size()), D3D11_SDK_VERSION,
            &swapChainDescription, &swapChain_, &device_, &selectedFeatureLevel,
            &context_);
    }
    if (FAILED(result)) {
        std::ostringstream message;
        message << "D3D11CreateDeviceAndSwapChain failed: 0x" << std::hex
                << static_cast<unsigned long>(result);
        return Result::failure(message.str());
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    result = swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(result)) {
        return Result::failure("IDXGISwapChain::GetBuffer failed");
    }

    result = device_->CreateRenderTargetView(backBuffer.Get(), nullptr,
                                             &renderTarget_);
    if (FAILED(result)) {
        return Result::failure("ID3D11Device::CreateRenderTargetView failed");
    }

    context_->OMSetRenderTargets(1, renderTarget_.GetAddressOf(), nullptr);
    const D3D11_VIEWPORT viewport{0.0F, 0.0F, static_cast<float>(width),
                                  static_cast<float>(height), 0.0F, 1.0F};
    context_->RSSetViewports(1, &viewport);
    return Result::success();
}

void D3D11Renderer::renderFrame() {
    constexpr float clearColor[]{0.025F, 0.045F, 0.085F, 1.0F};
    context_->ClearRenderTargetView(renderTarget_.Get(), clearColor);
    swapChain_->Present(1, 0);
}

} // namespace usm::renderer

