#pragma once

#include "assets/ColladaMesh.hpp"
#include "assets/BtexTexture.hpp"
#include "game/CinematicCamera.hpp"
#include "game/CinematicUiRuntime.hpp"
#include "game/DeathConfirmationRuntime.hpp"
#include "game/GameplayPlayer.hpp"
#include "game/LevelBonusRuntime.hpp"
#include "game/LevelCinematicRuntime.hpp"
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
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

namespace usm::renderer {

// CAnimObjEffect::Init (0x00390bb8) uses two distinct attachment modes.
// Live effects are parented to the animated bone. Snapshot effects copy the
// bone's absolute position once but use the player's quaternion, deliberately
// discarding the bone rotation.
[[nodiscard]] std::array<float, 16> resolvePlayerHitEffectWorldTransform(
    const std::array<float, 16>& playerWorld,
    const std::array<float, 16>& boneTransform,
    bool followsPlayerBone,
    const assets::Vector3& driftOffset = {}) noexcept;

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
    [[nodiscard]] Result updateLevelRooms(
        const game::LevelOneBootstrap& levelOne,
        const game::LevelCinematicRuntime& cinematics);
    [[nodiscard]] Result updateGameplayCinematicActors(
        const game::LevelOneBootstrap& levelOne,
        const game::LevelCinematicAsset* cinematic,
        std::uint32_t timestampMilliseconds);
    [[nodiscard]] Result updateLevelOnePlayer(
        const game::LevelOneBootstrap& levelOne,
        const assets::ColladaAnimationClip& clip,
        std::uint32_t clipTimeMilliseconds,
        const std::array<float, 16>& worldTransform);
    [[nodiscard]] Result updatePlayerHitEffects(
        const game::LevelOneBootstrap& levelOne,
        const game::GameplayPlayer& player);
    [[nodiscard]] Result updateLevelOneEnemies(
        const game::LevelOneBootstrap& levelOne,
        const game::LevelEnemyRuntime& enemies);
    [[nodiscard]] Result updateLevelOneObjects(
        const game::LevelOneBootstrap& levelOne,
        const game::LevelObjectRuntime& objects);
    [[nodiscard]] Result updateWebLine(
        bool visible, const assets::Vector3& anchor = {},
        const assets::Vector3& attachPosition = {},
        const assets::Vector3* secondAttachPosition = nullptr,
        const assets::Vector3& orientation = {0.0F, 0.0F, -1.0F});
    [[nodiscard]] Result updateEnemyGunLines(
        std::span<const game::EnemyGunLineState> gunLines);
    [[nodiscard]] Result updatePlayerWebPellets(
        const game::LevelOneBootstrap& levelOne,
        std::span<const game::PlayerWebPelletState> pellets);
    [[nodiscard]] Result updateEnemyMolotovs(
        const game::LevelOneBootstrap& levelOne,
        std::span<const game::EnemyMolotovState> molotovs);
    [[nodiscard]] Result updateEnemyBoomerangs(
        const game::LevelOneBootstrap& levelOne,
        std::span<const game::EnemyBoomerangState> boomerangs);
    [[nodiscard]] Result updateEnemyElectroEffects(
        const game::LevelOneBootstrap& levelOne,
        const game::LevelEnemyRuntime& enemies);
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
                                              skillPointPopup = nullptr,
                                          bool visible = true,
                                          bool bossProgressVisible = false,
                                          float bossProgressRatio = 0.0F);
    [[nodiscard]] Result updateCinematicUi(
        const game::LevelHudAsset& hud,
        const game::CinematicUiFrame& frame);
    [[nodiscard]] Result updateTransport(
        const game::LevelHudAsset& hud,
        const game::TransportFrame& frame);
    [[nodiscard]] Result updateDeathConfirmation(
        const game::LevelHudAsset& hud,
        const game::DeathConfirmationFrame& frame);
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
        bool additiveBlend{};
        // Native material 0x1e performs GL_GREATER against the source
        // material's SMaterial::MaterialTypeParam. This is separate from the
        // fixed 0.5 alpha-test material used by ordinary scene geometry.
        bool effectAlphaTest{};
        float effectAlphaReference{};
        std::uint32_t renderingLayer{};
        bool backFaceCulling{true};
        bool frontFaceCulling{};
        bool reflectionTwoLayer{};
        bool lightmapTwoLayer{};
        assets::Vector3 transparentSortPosition;
        bool hasTransparentSortPosition{};
        std::string materialAnimationTarget;
        std::array<float, 6> baseTextureTransform{1.0F, 0.0F, 0.0F,
                                                  1.0F, 0.0F, 0.0F};
        std::array<float, 6> textureTransform{1.0F, 0.0F, 0.0F,
                                              1.0F, 0.0F, 0.0F};
    };

    struct GpuMesh {
        Microsoft::WRL::ComPtr<ID3D11Buffer> vertexBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> lightmapCoordinateBuffer;
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

    struct EffectParticleDrawBatch {
        std::uint32_t startVertex{};
        std::uint32_t vertexCount{};
        std::uint64_t emitterId{};
        assets::Vector3 emitterPosition;
        bool additive{};
    };

    struct WebLineDrawBatch {
        std::uint32_t startVertex{};
        std::uint32_t vertexCount{};
        assets::Vector3 nodePosition;
    };

    enum class ConfirmationTexture : std::uint8_t {
        BackgroundSuit,
        MainMenu,
        NormalWhiteFont,
        OutlineSmallFont,
        OutlineBigFont,
    };

    struct ConfirmationDrawBatch {
        std::uint32_t startVertex{};
        std::uint32_t vertexCount{};
        ConfirmationTexture texture{};
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
        bool omitUntexturedMaterials = false,
        std::string_view additiveTextureNameFragment = {});
    [[nodiscard]] Result createTextureView(
        std::span<const assets::RgbaImage> mipLevels,
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& view);
    [[nodiscard]] Result updateDynamicMesh(
        GpuMesh& gpuMesh,
        std::span<const assets::ColladaGeometry> animatedGeometry,
        const std::array<float, 16>* worldTransform,
        float vertexAlphaScale = 1.0F);
    void updateMaterialAnimation(
        GpuMesh& gpuMesh,
        const assets::ColladaAnimationFile& animation,
        std::uint32_t timestampMilliseconds) noexcept;
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
    Microsoft::WRL::ComPtr<ID3D11VertexShader> lightmapVertexShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixelShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> alphaTestPixelShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> effectAlphaTestPixelShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> reflectionPixelShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> lightmapPixelShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> lightmapAlphaTestPixelShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> colorPixelShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> hudColorPixelShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> effectPixelShader_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> hudVertexShader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> hudPixelShader_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> inputLayout_;
    Microsoft::WRL::ComPtr<ID3D11InputLayout> lightmapInputLayout_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> transformBuffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> viewRotationBuffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> textureTransformBuffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> effectAlphaTestBuffer_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizerState_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> frontCullRasterizerState_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> noCullRasterizerState_;
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
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> webLineTexture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> effectTexture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> hintTexture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> whiteTexture_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> transportSpriteVertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> transportColorVertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> transportTexture_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> cinematicUiColorVertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> cinematicUiTextVertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> cinematicUiTextTexture_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> cinematicUiMessagePanelVertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> cinematicUiMessageIconVertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer>
        cinematicUiControllerIconVertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> cinematicUiMessageTextVertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
        cinematicUiTutorialTexture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
        cinematicUiControllerTexture_;
    assets::RgbaImage cinematicUiControllerImage_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> deathConfirmationVertexBuffer_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
        deathConfirmationBackgroundTexture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
        deathConfirmationMainMenuTexture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
        deathConfirmationNormalFontTexture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
        deathConfirmationOutlineFontTexture_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>
        deathConfirmationOutlineBigFontTexture_;
    std::vector<ConfirmationDrawBatch> deathConfirmationBatches_;
    std::vector<GpuMesh> gpuMeshes_;
    std::unordered_map<const assets::BtexTexture*,
                       Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>>
        sharedTextureViews_;
    std::size_t environmentMeshCount_{};
    std::size_t roomMeshCount_{};
    std::size_t levelObjectMeshStart_{};
    std::size_t introActorMeshStart_{};
    std::optional<std::size_t> playerMeshIndex_;
    bool standalonePlayerMesh_{};
    std::size_t gameplayCinematicMeshStart_{};
    std::size_t enemyMeshStart_{};
    std::size_t playerHitEffectMeshStart_{};
    std::size_t playerHitEffectMeshCount_{};
    std::size_t webPelletProjectileMeshStart_{};
    std::size_t webPelletProjectileMeshCount_{};
    std::size_t molotovProjectileMeshStart_{};
    std::size_t molotovProjectileMeshCount_{};
    std::size_t boomerangProjectileMeshStart_{};
    std::size_t boomerangProjectileMeshCount_{};
    std::size_t thunderclapWaveMeshStart_{};
    std::size_t thunderclapWaveMeshCount_{};
    std::size_t thunderclapBeamMeshStart_{};
    std::size_t thunderclapBeamMeshCount_{};
    std::size_t electroRotateWaveMeshStart_{};
    std::size_t electroRotateWaveMeshCount_{};
    std::size_t electroPostBeamMeshStart_{};
    std::size_t electroPostBeamMeshCount_{};
    std::size_t electroBurstWaveMeshStart_{};
    std::size_t electroBurstWaveMeshCount_{};
    std::size_t electroBurstBillboardMeshStart_{};
    std::size_t electroBurstBillboardMeshCount_{};
    std::size_t enemyLandingShockwaveMeshStart_{};
    std::size_t enemyLandingShockwaveMeshCount_{};
    std::size_t enemyLandingCrashWallMeshStart_{};
    std::size_t enemyLandingCrashWallMeshCount_{};
    std::uint32_t hudVertexCount_{};
    std::uint32_t cinematicUiColorVertexCount_{};
    std::uint32_t transportSpriteVertexCount_{};
    std::uint32_t transportColorVertexCount_{};
    std::uint32_t cinematicUiTextVertexCount_{};
    std::uint32_t cinematicUiMessagePanelVertexCount_{};
    std::uint32_t cinematicUiMessageIconVertexCount_{};
    std::uint32_t cinematicUiControllerIconVertexCount_{};
    std::uint32_t cinematicUiMessageTextVertexCount_{};
    std::uint32_t deathConfirmationVertexCount_{};
    std::uint32_t deathConfirmationVertexCapacity_{};
    std::uint32_t hudVertexCapacity_{};
    std::uint32_t webLineVertexCount_{};
    std::vector<WebLineDrawBatch> webLineDrawBatches_;
    std::uint32_t enemyGunLineVertexCount_{};
    std::vector<EffectParticleDrawBatch> effectParticleDrawBatches_;
    std::uint32_t effectOrbVertexStart_{};
    std::uint32_t effectOrbVertexCount_{};
    std::uint32_t effectVertexCapacity_{};
    std::uint32_t hintVertexCount_{};
    std::uint32_t hintVertexCapacity_{};
    std::u16string cinematicUiText_;
    bool cinematicUiTextCentered_{};
    std::u16string cinematicUiMessageText_;
    std::int32_t cinematicUiMessageFace_{-1};
    std::int32_t cinematicUiMessagePage_{-1};
    bool cinematicUiTutorialPanel_{};
    game::InformationPanel cinematicUiInformationPanel_{
        game::InformationPanel::None};
    std::int32_t cinematicUiTutorialButton_{-1};
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
