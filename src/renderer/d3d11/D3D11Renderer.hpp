#pragma once

#include "assets/ColladaMesh.hpp"
#include "assets/BtexTexture.hpp"
#include "game/CinematicCamera.hpp"
#include "game/CinematicUiRuntime.hpp"
#include "game/LevelBonusRuntime.hpp"
#include "game/LevelOneBootstrap.hpp"
#include "game/LevelEnemyRuntime.hpp"
#include "game/LevelEffectRuntime.hpp"
#include "game/LevelHintRuntime.hpp"
#include "game/LevelObjectRuntime.hpp"
#include "renderer/IRenderer.hpp"

#include <DirectXMath.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <unordered_map>

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
    [[nodiscard]] Result uploadLevelOneScene(
        const game::LevelOneBootstrap& levelOne);
    [[nodiscard]] Result updateLevelOneActors(
        const game::LevelOneBootstrap& levelOne,
        std::uint32_t timestampMilliseconds);
    [[nodiscard]] Result updateGameplayCinematicActors(
        const game::LevelOneBootstrap& levelOne,
        const game::LevelCinematicAsset* cinematic,
        std::uint32_t timestampMilliseconds);
    [[nodiscard]] Result updateLevelOnePlayer(
        const game::LevelOneBootstrap& levelOne,
        const assets::ColladaAnimationClip& clip,
        std::uint32_t clipTimeMilliseconds,
        const std::array<float, 16>& worldTransform);
    [[nodiscard]] Result updateLevelOneEnemies(
        const game::LevelOneBootstrap& levelOne,
        const game::LevelEnemyRuntime& enemies);
    [[nodiscard]] Result updateLevelOneObjects(
        const game::LevelOneBootstrap& levelOne,
        const game::LevelObjectRuntime& objects);
    [[nodiscard]] Result updateWebLine(
        bool visible, const assets::Vector3& anchor = {},
        const assets::Vector3& attachPosition = {});
    [[nodiscard]] Result updateEnemyGunLines(
        std::span<const game::EnemyGunLineState> gunLines);
    [[nodiscard]] Result updateLevelOneEffects(
        const game::LevelEffectAsset& assets,
        const game::LevelEffectRuntime& effects,
        const game::LevelBonusRuntime& bonuses);
    [[nodiscard]] Result updateLevelOneHints(
        const game::LevelHintRuntime& hints);
    [[nodiscard]] Result updatePlayerHud(const game::LevelHudAsset& hud,
                                         float currentHealthRatio,
                                         float delayedHealthRatio,
                                         float webPowerRatio,
                                         const game::LevelEnemyState*
                                             shownHealthBarEnemy = nullptr,
                                         std::int32_t skillPoints = 0,
                                         bool showSkillPointTotal = false,
                                         const game::LevelBonusPopupState*
                                             skillPointPopup = nullptr);
    [[nodiscard]] Result updateCinematicUi(
        const game::CinematicUiFrame& frame);
    void setCinematicVisibleRooms(std::span<const bool> rooms) noexcept;
    void setCameraAreaRoomVisibility(
        std::span<const bool> invisibleRooms,
        std::span<const bool> visibleRooms) noexcept;
    [[nodiscard]] std::span<const bool> roomVisibility() const noexcept {
        return roomVisibility_;
    }
    [[nodiscard]] Result setCamera(const game::CameraPose& camera);
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
        std::uint32_t secondaryTextureIndex{};
        bool alphaTest{};
        bool alphaBlend{};
        bool reflectionTwoLayer{};
    };

    struct GpuMesh {
        Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> indexBuffer;
        std::vector<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> textures;
        std::vector<DrawBatch> drawBatches;
        std::uint32_t vertexCount{};
        bool dynamicVertices{};
        bool baseVisible{true};
        bool visible{true};
        bool cameraRelative{};
        std::int32_t roomId{-1};
        assets::AxisAlignedBounds bounds;
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
        std::span<const assets::RgbaImage> previewTexture,
        const std::array<float, 16>* transform = nullptr,
        bool dynamicVertices = false,
        bool omitUntexturedMaterials = false);
    [[nodiscard]] Result createTextureView(
        std::span<const assets::RgbaImage> mipLevels,
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& view);
    [[nodiscard]] Result updateDynamicMesh(
        GpuMesh& gpuMesh,
        std::span<const assets::ColladaGeometry> animatedGeometry,
        const std::array<float, 16>* worldTransform);
    [[nodiscard]] Result uploadHudTexture(const game::LevelHudAsset& hud);
    void bindRenderTarget(std::uint32_t width, std::uint32_t height);
    void setMeshVisible(GpuMesh& mesh, bool visible) noexcept;
    void updateRoomVisibility(const DirectX::XMMATRIX& viewProjection) noexcept;

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> colorTarget_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> depthTarget_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthView_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertexShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> alphaTestPixelShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> reflectionPixelShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> colorPixelShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> hudColorPixelShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> effectPixelShader_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> hudVertexShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> hudPixelShader_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> inputLayout_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> transformBuffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> viewRotationBuffer_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizerState_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> alphaBlendState_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> additiveBlendState_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthWriteState_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthReadState_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthDisabledState_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> hudVertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> webLineVertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> enemyGunLineVertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> effectVertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> hintVertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> hudTexture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> effectTexture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> hintTexture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> whiteTexture_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> cinematicUiColorVertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> cinematicUiTextVertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> cinematicUiTextTexture_;
    std::vector<GpuMesh> gpuMeshes_;
    std::unordered_map<const assets::BtexTexture*,
                       Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>>
        sharedTextureViews_;
    std::size_t environmentMeshCount_{};
    std::size_t roomMeshCount_{};
    std::size_t levelObjectMeshStart_{};
    std::size_t introActorMeshStart_{};
    std::size_t gameplayCinematicMeshStart_{};
    std::size_t enemyMeshStart_{};
    std::uint32_t hudVertexCount_{};
    std::uint32_t cinematicUiColorVertexCount_{};
    std::uint32_t cinematicUiTextVertexCount_{};
    std::uint32_t hudVertexCapacity_{};
    std::uint32_t webLineVertexCount_{};
    std::uint32_t enemyGunLineVertexCount_{};
    std::uint32_t effectAlphaVertexCount_{};
    std::uint32_t effectAdditiveVertexCount_{};
    std::uint32_t effectVertexCapacity_{};
    std::uint32_t hintVertexCount_{};
    std::uint32_t hintVertexCapacity_{};
    std::u16string cinematicUiText_;
    bool cinematicUiTextCentered_{};
    DirectX::XMFLOAT4X4 worldViewProjection_{};
    DirectX::XMFLOAT4X4 skyViewProjection_{};
    DirectX::XMFLOAT4X4 viewRotation_{};
    assets::Vector3 cameraRight_{1.0F, 0.0F, 0.0F};
    assets::Vector3 cameraUp_{0.0F, 0.0F, 1.0F};
    assets::Vector3 cameraPosition_;
    std::array<bool, 16> forcedVisibleRooms_{};
    std::array<bool, 16> cameraAreaInvisibleRooms_{};
    std::array<bool, 16> cameraAreaVisibleRooms_{};
    std::array<bool, 16> roomVisibility_{};
    std::uint32_t width_{};
    std::uint32_t height_{};
};

} // namespace usm::renderer
