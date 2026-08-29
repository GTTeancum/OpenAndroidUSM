#pragma once

#include "assets/ColladaMesh.hpp"
#include "assets/BtexTexture.hpp"
#include "renderer/IRenderer.hpp"

#include <DirectXMath.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace usm::renderer {

class D3D11Renderer final : public IRenderer {
public:
    [[nodiscard]] Result initialize(HWND window, std::uint32_t width,
                                    std::uint32_t height) override;
    [[nodiscard]] Result initializeOffscreen(std::uint32_t width,
                                             std::uint32_t height);
    [[nodiscard]] Result uploadPreviewGeometry(
        const assets::ColladaGeometry& geometry,
        std::span<const assets::RgbaImage> mipLevels);
    [[nodiscard]] Result uploadSceneGeometry(
        const assets::ColladaMeshFile& mesh,
        std::span<const assets::BtexTexture> textures);
    [[nodiscard]] Result readBackPixel(
        std::uint32_t x, std::uint32_t y,
        std::array<std::uint8_t, 4>& rgba) const;
    [[nodiscard]] Result readBackImage(assets::RgbaImage& image) const;
    void renderFrame() override;

private:
    struct DrawBatch {
        D3D11_PRIMITIVE_TOPOLOGY topology{};
        std::uint32_t indexCount{};
        std::uint32_t startIndex{};
        std::int32_t baseVertex{};
        std::uint32_t textureIndex{};
    };

    [[nodiscard]] Result createDevice(D3D_DRIVER_TYPE driverType, UINT flags);
    [[nodiscard]] Result createWindowRenderTarget(HWND window,
                                                   std::uint32_t width,
                                                   std::uint32_t height,
                                                   UINT flags);
    [[nodiscard]] Result createOffscreenRenderTarget(std::uint32_t width,
                                                      std::uint32_t height);
    [[nodiscard]] Result createDepthTarget(std::uint32_t width,
                                            std::uint32_t height);
    [[nodiscard]] Result createPipeline();
    [[nodiscard]] Result uploadGeometrySet(
        std::span<const assets::ColladaGeometry> geometries,
        const assets::ColladaMeshFile* materialLibrary,
        std::span<const assets::BtexTexture> textures,
        std::span<const assets::RgbaImage> previewTexture);
    [[nodiscard]] Result createTextureView(
        std::span<const assets::RgbaImage> mipLevels,
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& view);
    void bindRenderTarget(std::uint32_t width, std::uint32_t height);

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> colorTarget_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> depthTarget_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthView_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> inputLayout_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> transformBuffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> indexBuffer_;
    std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> textureViews_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizerState_;
    std::vector<DrawBatch> drawBatches_;
    DirectX::XMFLOAT4X4 worldViewProjection_{};
    std::uint32_t width_{};
    std::uint32_t height_{};
};

} // namespace usm::renderer
