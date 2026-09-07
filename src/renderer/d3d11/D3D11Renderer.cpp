#include "renderer/d3d11/D3D11Renderer.hpp"

#include "assets/ColladaSkinning.hpp"
#include "game/WebLineGeometry.hpp"

#include <d3dcompiler.h>
#include <wincodec.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace usm::renderer {
namespace {

using Microsoft::WRL::ComPtr;

bool renderTraceEnabled() noexcept {
    return GetEnvironmentVariableA("OPENANDROIDUSM_RENDER_TRACE", nullptr,
                                   0) != 0;
}

struct GpuVertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT2 textureCoordinate;
    std::uint32_t color;
};

struct EffectMaterialConstants {
    DirectX::XMFLOAT4 ambientColor;
    DirectX::XMFLOAT4 parameters;
};

constexpr std::string_view kVertexShader = R"hlsl(
cbuffer TransformBuffer : register(b0) {
    float4x4 WorldViewProjection;
};
cbuffer ViewRotationBuffer : register(b1) {
    float4x4 ViewRotation;
};
cbuffer TextureTransformBuffer : register(b2) {
    float4 TextureTransform;
};

struct VertexInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 textureCoordinate : TEXCOORD0;
    float4 color : COLOR0;
};

struct PixelInput {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 textureCoordinate : TEXCOORD0;
    float4 color : COLOR0;
};

PixelInput main(VertexInput input) {
    PixelInput output;
    output.position = mul(float4(input.position, 1.0), WorldViewProjection);
    output.normal = mul(float4(input.normal, 0.0), ViewRotation).xyz;
    output.textureCoordinate = input.textureCoordinate + TextureTransform.xy;
    output.color = input.color;
    return output;
}
)hlsl";

constexpr std::string_view kLightmapVertexShader = R"hlsl(
cbuffer TransformBuffer : register(b0) {
    float4x4 WorldViewProjection;
};
cbuffer ViewRotationBuffer : register(b1) {
    float4x4 ViewRotation;
};
cbuffer TextureTransformBuffer : register(b2) {
    float4 TextureTransform;
};

struct VertexInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 textureCoordinate : TEXCOORD0;
    float4 color : COLOR0;
    float2 secondaryTextureCoordinate : TEXCOORD1;
};

struct PixelInput {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 textureCoordinate : TEXCOORD0;
    float4 color : COLOR0;
    float2 secondaryTextureCoordinate : TEXCOORD1;
};

PixelInput main(VertexInput input) {
    PixelInput output;
    output.position = mul(float4(input.position, 1.0), WorldViewProjection);
    output.normal = mul(float4(input.normal, 0.0), ViewRotation).xyz;
    output.textureCoordinate = input.textureCoordinate + TextureTransform.xy;
    output.color = input.color;
    output.secondaryTextureCoordinate = input.secondaryTextureCoordinate;
    return output;
}
)hlsl";

constexpr std::string_view kLightmapPixelShader = R"hlsl(
Texture2D DiffuseTexture : register(t0);
Texture2D LightmapTexture : register(t1);
SamplerState DiffuseSampler : register(s0);

struct PixelInput {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 textureCoordinate : TEXCOORD0;
    float4 color : COLOR0;
    float2 secondaryTextureCoordinate : TEXCOORD1;
};

float4 main(PixelInput input) : SV_TARGET {
    float4 diffuse = DiffuseTexture.Sample(DiffuseSampler,
                                            input.textureCoordinate) *
                     input.color;
    float3 bakedLight = LightmapTexture.Sample(
        DiffuseSampler, input.secondaryTextureCoordinate).rgb;
    // CCommonGLMaterialRenderer_LIGHTMAP::onSetMaterial (0x00455fe8)
    // modulates texture unit 1 with the previous, vertex-colored diffuse.
    return float4(diffuse.rgb * bakedLight, diffuse.a);
}
)hlsl";

constexpr std::string_view kLightmapAlphaTestPixelShader = R"hlsl(
Texture2D DiffuseTexture : register(t0);
Texture2D LightmapTexture : register(t1);
SamplerState DiffuseSampler : register(s0);

struct PixelInput {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 textureCoordinate : TEXCOORD0;
    float4 color : COLOR0;
    float2 secondaryTextureCoordinate : TEXCOORD1;
};

float4 main(PixelInput input) : SV_TARGET {
    float4 diffuse = DiffuseTexture.Sample(DiffuseSampler,
                                            input.textureCoordinate) *
                     input.color;
    clip(diffuse.a - 0.5);
    float3 bakedLight = LightmapTexture.Sample(
        DiffuseSampler, input.secondaryTextureCoordinate).rgb;
    return float4(diffuse.rgb * bakedLight, diffuse.a);
}
)hlsl";

constexpr std::string_view kReflectionPixelShader = R"hlsl(
Texture2D DiffuseTexture : register(t0);
Texture2D ReflectionTexture : register(t1);
SamplerState DiffuseSampler : register(s0);

struct PixelInput {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 textureCoordinate : TEXCOORD0;
    float4 color : COLOR0;
};

float4 main(PixelInput input) : SV_TARGET {
    float3 normal = normalize(input.normal);
    float2 sphereCoordinate = normal.xy * float2(0.5, -0.5) + 0.5;
    float4 diffuse = DiffuseTexture.Sample(DiffuseSampler,
                                            input.textureCoordinate) *
                     input.color;
    float3 reflection = ReflectionTexture.Sample(DiffuseSampler,
                                                   sphereCoordinate).rgb;
    // GL_COMBINE_RGB = GL_ADD in
    // CCommonGLMaterialRenderer_REFLECTION_2_LAYER::onSetMaterial
    // (original 0x00455be0).
    return float4(saturate(diffuse.rgb + reflection), diffuse.a);
}
)hlsl";

constexpr std::string_view kPixelShader = R"hlsl(
Texture2D DiffuseTexture : register(t0);
SamplerState DiffuseSampler : register(s0);

struct PixelInput {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 textureCoordinate : TEXCOORD0;
    float4 color : COLOR0;
};

float4 main(PixelInput input) : SV_TARGET {
    // Room irradiance and static occlusion are baked into COLOR0. The native
    // SOLID material selects GL_MODULATE at image 0x00455a08.
    return DiffuseTexture.Sample(DiffuseSampler, input.textureCoordinate) *
           input.color;
}
)hlsl";

// CAnimObjEffect::Init selects material 0x1d or 0x1e. Their native GLES
// renderers at 0x00397360/0x00397028 select GL_SUBTRACT/GL_ADD for RGB with
// the material ambient color as GL_CONSTANT. Both select the texture as the
// alpha source. Diffuse alpha is animated by CAnimObjEffect::Update and is
// carried in COLOR0 here. RGB-only PVRTC maps need their authored intensity
// as coverage because the native loader supplies a synthetic alpha of one.
constexpr std::string_view kEffectColorMaskPixelShader = R"hlsl(
Texture2D DiffuseTexture : register(t0);
SamplerState DiffuseSampler : register(s0);
cbuffer EffectMaterialBuffer : register(b0) {
    float4 AmbientColor;
    float4 EffectMaterialParameters;
};

struct PixelInput {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 textureCoordinate : TEXCOORD0;
    float4 color : COLOR0;
};

float4 main(PixelInput input) : SV_TARGET {
    float4 textureColor =
        DiffuseTexture.Sample(DiffuseSampler, input.textureCoordinate);
    float mode = EffectMaterialParameters.x;
    float syntheticAlpha = EffectMaterialParameters.y;
    float3 combinedRgb = mode < 0.0
        ? saturate(textureColor.rgb - AmbientColor.rgb)
        : saturate(textureColor.rgb + AmbientColor.rgb);
    float rgbCoverage = max(textureColor.r,
                            max(textureColor.g, textureColor.b));
    float coverage = lerp(textureColor.a, rgbCoverage, syntheticAlpha);
    return float4(combinedRgb, coverage * input.color.a);
}
)hlsl";

constexpr std::string_view kColorPixelShader = R"hlsl(
struct PixelInput {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 textureCoordinate : TEXCOORD0;
    float4 color : COLOR0;
};

float4 main(PixelInput input) : SV_TARGET {
    return input.color;
}
)hlsl";

// The screen-space HUD vertex shader has a compact interpolant signature.
// Keeping its color shader separate prevents COLOR0 from being read from the
// register assigned to NORMAL by the world-space vertex shader.
constexpr std::string_view kHudColorPixelShader = R"hlsl(
struct PixelInput {
    float4 position : SV_POSITION;
    float2 textureCoordinate : TEXCOORD0;
    float4 color : COLOR0;
};

float4 main(PixelInput input) : SV_TARGET {
    return input.color;
}
)hlsl";

constexpr std::string_view kEffectPixelShader = R"hlsl(
Texture2D EffectTexture : register(t0);
SamplerState EffectSampler : register(s0);

struct PixelInput {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 textureCoordinate : TEXCOORD0;
    float4 color : COLOR0;
};

float4 main(PixelInput input) : SV_TARGET {
    return EffectTexture.Sample(EffectSampler, input.textureCoordinate) *
           input.color;
}
)hlsl";

constexpr std::string_view kAlphaTestPixelShader = R"hlsl(
Texture2D DiffuseTexture : register(t0);
SamplerState DiffuseSampler : register(s0);

struct PixelInput {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 textureCoordinate : TEXCOORD0;
    float4 color : COLOR0;
};

float4 main(PixelInput input) : SV_TARGET {
    float4 diffuse = DiffuseTexture.Sample(DiffuseSampler, input.textureCoordinate) *
                     input.color;
    // Original CCommonGLMaterialRenderer_ALPHA_TEST_NONTRANSPARENT uses
    // GL_GREATER with a 0.5 alpha reference (Ghidra image 0x00397864).
    clip(diffuse.a - 0.5);
    return diffuse;
}
)hlsl";

constexpr std::string_view kHudVertexShader = R"hlsl(
struct VertexInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 textureCoordinate : TEXCOORD0;
    float4 color : COLOR0;
};

struct PixelInput {
    float4 position : SV_POSITION;
    float2 textureCoordinate : TEXCOORD0;
    float4 color : COLOR0;
};

PixelInput main(VertexInput input) {
    PixelInput output;
    output.position = float4(input.position, 1.0);
    output.textureCoordinate = input.textureCoordinate;
    output.color = input.color;
    return output;
}
)hlsl";

constexpr std::string_view kHudPixelShader = R"hlsl(
Texture2D InterfaceTexture : register(t0);
SamplerState InterfaceSampler : register(s0);

struct PixelInput {
    float4 position : SV_POSITION;
    float2 textureCoordinate : TEXCOORD0;
    float4 color : COLOR0;
};

float4 main(PixelInput input) : SV_TARGET {
    return InterfaceTexture.Sample(InterfaceSampler,
                                   input.textureCoordinate) * input.color;
}
)hlsl";

Result hresultFailure(std::string_view operation, HRESULT value) {
    std::ostringstream message;
    message << operation << " failed: 0x" << std::hex
            << static_cast<unsigned long>(value);
    return Result::failure(message.str());
}

DirectX::XMMATRIX buildOriginalLookAtMatrix(
    DirectX::FXMVECTOR position, DirectX::FXMVECTOR target,
    DirectX::FXMVECTOR up) noexcept {
    // Irrlicht's preserved buildCameraLookAtMatrix (image 0x003f4a18)
    // constructs screen-right as forward x up. XMMatrixLookAtLH uses the
    // opposite cross-product order, which mirrors every sign and character.
    const DirectX::XMVECTOR forward = DirectX::XMVector3Normalize(
        DirectX::XMVectorSubtract(target, position));
    const DirectX::XMVECTOR right = DirectX::XMVector3Normalize(
        DirectX::XMVector3Cross(forward, up));
    const DirectX::XMVECTOR adjustedUp =
        DirectX::XMVector3Cross(right, forward);
    return DirectX::XMMATRIX(
        DirectX::XMVectorGetX(right), DirectX::XMVectorGetX(adjustedUp),
        DirectX::XMVectorGetX(forward), 0.0F,
        DirectX::XMVectorGetY(right), DirectX::XMVectorGetY(adjustedUp),
        DirectX::XMVectorGetY(forward), 0.0F,
        DirectX::XMVectorGetZ(right), DirectX::XMVectorGetZ(adjustedUp),
        DirectX::XMVectorGetZ(forward), 0.0F,
        -DirectX::XMVectorGetX(DirectX::XMVector3Dot(right, position)),
        -DirectX::XMVectorGetX(DirectX::XMVector3Dot(adjustedUp, position)),
        -DirectX::XMVectorGetX(DirectX::XMVector3Dot(forward, position)),
        1.0F);
}

Result compileShader(std::string_view source, const char* target,
                     ComPtr<ID3DBlob>& bytecode) {
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ComPtr<ID3DBlob> errors;
    const HRESULT result = D3DCompile(
        source.data(), source.size(), nullptr, nullptr, nullptr, "main", target,
        flags, 0, &bytecode, &errors);
    if (FAILED(result)) {
        std::string message = "D3DCompile failed";
        if (errors) {
            message += ": ";
            message.append(static_cast<const char*>(errors->GetBufferPointer()),
                           errors->GetBufferSize());
        }
        return Result::failure(std::move(message));
    }
    return Result::success();
}

D3D11_PRIMITIVE_TOPOLOGY topologyFor(assets::ColladaPrimitive primitive) {
    switch (primitive) {
    case assets::ColladaPrimitive::Triangles:
        return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    case assets::ColladaPrimitive::TriangleStrip:
        return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    case assets::ColladaPrimitive::Lines:
        return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
    case assets::ColladaPrimitive::LineStrip:
    case assets::ColladaPrimitive::LineLoop:
        return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
    }
    return D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
}

std::uint32_t rgbaVertexColor(std::uint32_t argb) noexcept {
    const std::uint32_t red = (argb >> 16) & 0xff;
    const std::uint32_t green = (argb >> 8) & 0xff;
    const std::uint32_t blue = argb & 0xff;
    const std::uint32_t alpha = (argb >> 24) & 0xff;
    return red | (green << 8) | (blue << 16) | (alpha << 24);
}

bool textureHasTransparency(const assets::BtexTexture& texture) noexcept {
    if (texture.mipLevels().empty()) {
        return false;
    }
    const std::vector<std::uint8_t>& pixels =
        texture.mipLevels().front().pixels;
    for (std::size_t alpha = 3; alpha < pixels.size(); alpha += 4) {
        if (pixels[alpha] != 255) {
            return true;
        }
    }
    return false;
}

struct XboxButtonGlyph {
    std::size_t characterCount{};
    std::uint32_t sourceX{};
    std::uint32_t sourceY{};
    std::uint32_t sourceWidth{};
    std::uint32_t sourceHeight{};
};

std::optional<XboxButtonGlyph> xboxButtonGlyphAt(
    std::u16string_view text, std::size_t index) noexcept {
    if (index >= text.size() || text[index] != u'[') {
        return std::nullopt;
    }
    // The shoulder/trigger crops are square transparent cells around their
    // naturally wider artwork. This preserves the atlas aspect ratio while
    // keeping the existing inline-icon layout and baseline.
    if (index + 4U <= text.size() && text[index + 3U] == u']') {
        const std::u16string_view token = text.substr(index, 4U);
        if (token == u"[LB]") {
            return XboxButtonGlyph{4U, 72U, 132U, 72U, 72U};
        }
        if (token == u"[RB]") {
            return XboxButtonGlyph{4U, 168U, 132U, 80U, 80U};
        }
        if (token == u"[LT]") {
            return XboxButtonGlyph{4U, 290U, 136U, 72U, 72U};
        }
        if (token == u"[RT]") {
            return XboxButtonGlyph{4U, 386U, 136U, 72U, 72U};
        }
    }
    if (index + 3U > text.size() || text[index + 2U] != u']') {
        return std::nullopt;
    }
    // Colored A/B/X/Y cells in the supplied 1280x640 atlas. The rectangles
    // include a few keyed-background pixels so antialiased edges are kept.
    switch (text[index + 1U]) {
    case u'A': return XboxButtonGlyph{3U, 81U, 49U, 53U, 53U};
    case u'B': return XboxButtonGlyph{3U, 177U, 49U, 53U, 53U};
    case u'X': return XboxButtonGlyph{3U, 273U, 49U, 53U, 53U};
    case u'Y': return XboxButtonGlyph{3U, 368U, 48U, 54U, 54U};
    default: return std::nullopt;
    }
}

bool containsXboxButtonGlyph(std::u16string_view text) noexcept {
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (xboxButtonGlyphAt(text, index).has_value()) {
            return true;
        }
    }
    return false;
}

class ScopedComInitialization final {
public:
    ScopedComInitialization() noexcept
        : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}

    ~ScopedComInitialization() {
        if (SUCCEEDED(result_)) {
            CoUninitialize();
        }
    }

    [[nodiscard]] bool usable() const noexcept {
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }

private:
    HRESULT result_{};
};

Result loadXboxButtonAtlas(assets::RgbaImage& image) {
    image = {};
    std::array<wchar_t, 32768> executablePath{};
    const DWORD pathLength = GetModuleFileNameW(
        nullptr, executablePath.data(),
        static_cast<DWORD>(executablePath.size()));
    if (pathLength == 0 || pathLength >= executablePath.size()) {
        return Result::failure("Could not locate xboxButtons.png");
    }
    const std::filesystem::path atlasPath =
        std::filesystem::path(executablePath.data()).parent_path() /
        L"xboxButtons.png";
    if (!std::filesystem::is_regular_file(atlasPath)) {
        return Result::failure("xboxButtons.png is not beside the executable");
    }

    ScopedComInitialization com;
    if (!com.usable()) {
        return Result::failure("Could not initialize COM for Xbox button atlas");
    }
    ComPtr<IWICImagingFactory> factory;
    HRESULT result = CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&factory));
    if (FAILED(result)) {
        return hresultFailure("CoCreateInstance(WICImagingFactory)", result);
    }
    ComPtr<IWICBitmapDecoder> decoder;
    result = factory->CreateDecoderFromFilename(
        atlasPath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
        &decoder);
    if (FAILED(result)) {
        return hresultFailure("IWICImagingFactory::CreateDecoderFromFilename",
                              result);
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    result = decoder->GetFrame(0, &frame);
    if (FAILED(result)) {
        return hresultFailure("IWICBitmapDecoder::GetFrame", result);
    }
    ComPtr<IWICFormatConverter> converter;
    result = factory->CreateFormatConverter(&converter);
    if (FAILED(result)) {
        return hresultFailure("IWICImagingFactory::CreateFormatConverter",
                              result);
    }
    result = converter->Initialize(
        frame.Get(), GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone, nullptr, 0.0,
        WICBitmapPaletteTypeCustom);
    if (FAILED(result)) {
        return hresultFailure("IWICFormatConverter::Initialize", result);
    }
    result = converter->GetSize(&image.width, &image.height);
    if (FAILED(result) || image.width != 1280U || image.height != 640U) {
        image = {};
        return Result::failure(
            "xboxButtons.png must retain its 1280x640 atlas layout");
    }
    const std::size_t byteCount =
        static_cast<std::size_t>(image.width) * image.height * 4U;
    if (byteCount > std::numeric_limits<UINT>::max()) {
        image = {};
        return Result::failure("xboxButtons.png is too large");
    }
    image.pixels.resize(byteCount);
    result = converter->CopyPixels(
        nullptr, image.width * 4U, static_cast<UINT>(byteCount),
        image.pixels.data());
    if (FAILED(result)) {
        image = {};
        return hresultFailure("IWICBitmapSource::CopyPixels", result);
    }

    const std::array<std::uint8_t, 3> background{
        image.pixels[0], image.pixels[1], image.pixels[2]};
    for (std::size_t offset = 0; offset < image.pixels.size(); offset += 4U) {
        const bool backgroundPixel =
            std::abs(static_cast<int>(image.pixels[offset]) - background[0]) <=
                2 &&
            std::abs(static_cast<int>(image.pixels[offset + 1U]) -
                     background[1]) <= 2 &&
            std::abs(static_cast<int>(image.pixels[offset + 2U]) -
                     background[2]) <= 2;
        if (backgroundPixel) {
            image.pixels[offset + 3U] = 0;
        }
    }
    return Result::success();
}

std::array<float, 16> translationMatrix(
    const assets::Vector3& position) noexcept {
    return {1.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            position.x, position.y, position.z, 1.0F};
}

std::array<float, 16> multiplyTransform(
    const std::array<float, 16>& left,
    const std::array<float, 16>& right) noexcept {
    // BDAE/Irrlicht transforms are column-major. The attached trail's local
    // bone transform is therefore applied first, then the player's world
    // transform, matching ISceneNode parenting in Player::AddHitEffect.
    std::array<float, 16> result{};
    for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
            for (std::size_t component = 0; component < 4; ++component) {
                result[column * 4 + row] +=
                    left[component * 4 + row] *
                    right[column * 4 + component];
            }
        }
    }
    return result;
}

Result renderWindowsText(std::u16string_view text,
                         const assets::RgbaImage* xboxButtonAtlas,
                         assets::RgbaImage& image) {
    constexpr std::uint32_t textureWidth = 1024;
    constexpr std::uint32_t textureHeight = 256;
    image = {};
    image.width = textureWidth;
    image.height = textureHeight;
    image.pixels.resize(static_cast<std::size_t>(textureWidth) *
                        textureHeight * 4U);

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = static_cast<LONG>(textureWidth);
    bitmapInfo.bmiHeader.biHeight = -static_cast<LONG>(textureHeight);
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    void* bitmapPixels = nullptr;
    HDC deviceContext = CreateCompatibleDC(nullptr);
    if (deviceContext == nullptr) {
        return Result::failure("Could not create cinematic text DC");
    }
    HBITMAP bitmap = CreateDIBSection(deviceContext, &bitmapInfo,
                                      DIB_RGB_COLORS, &bitmapPixels, nullptr,
                                      0);
    if (bitmap == nullptr || bitmapPixels == nullptr) {
        DeleteDC(deviceContext);
        return Result::failure("Could not create cinematic text bitmap");
    }
    HGDIOBJ previousBitmap = SelectObject(deviceContext, bitmap);
    std::memset(bitmapPixels, 0,
                static_cast<std::size_t>(textureWidth) * textureHeight * 4U);
    SetBkMode(deviceContext, TRANSPARENT);
    SetTextColor(deviceContext, RGB(255, 255, 255));
    HFONT font = CreateFontW(-42, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (font == nullptr) {
        SelectObject(deviceContext, previousBitmap);
        DeleteObject(bitmap);
        DeleteDC(deviceContext);
        return Result::failure("Could not create cinematic text font");
    }
    HGDIOBJ previousFont = SelectObject(deviceContext, font);
    std::wstring wideText;
    wideText.reserve(text.size());
    for (char16_t character : text) {
        wideText.push_back(static_cast<wchar_t>(character));
    }
    struct PlacedGlyph {
        XboxButtonGlyph glyph;
        std::uint32_t x{};
        std::uint32_t y{};
        std::uint32_t size{};
    };
    std::vector<PlacedGlyph> placedGlyphs;
    const bool renderGlyphs = xboxButtonAtlas != nullptr &&
        xboxButtonAtlas->width == 1280U &&
        xboxButtonAtlas->height == 640U &&
        containsXboxButtonGlyph(text);
    if (renderGlyphs) {
        constexpr std::uint32_t iconSize = 48U;
        TEXTMETRICW metrics{};
        GetTextMetricsW(deviceContext, &metrics);
        const auto measure = [&](std::u16string_view run) {
            SIZE extent{};
            std::wstring wideRun;
            wideRun.reserve(run.size());
            for (const char16_t character : run) {
                wideRun.push_back(static_cast<wchar_t>(character));
            }
            if (!wideRun.empty()) {
                GetTextExtentPoint32W(deviceContext, wideRun.data(),
                                      static_cast<int>(wideRun.size()),
                                      &extent);
            }
            return extent;
        };
        std::int32_t totalWidth = 0;
        for (std::size_t index = 0; index < text.size();) {
            const std::optional<XboxButtonGlyph> glyph =
                xboxButtonGlyphAt(text, index);
            if (glyph) {
                totalWidth += static_cast<std::int32_t>(iconSize);
                index += glyph->characterCount;
                continue;
            }
            const std::size_t runStart = index;
            while (index < text.size() &&
                   !xboxButtonGlyphAt(text, index).has_value()) {
                ++index;
            }
            totalWidth += measure(text.substr(runStart, index - runStart)).cx;
        }
        std::int32_t cursorX = std::max<std::int32_t>(
            16, (static_cast<std::int32_t>(textureWidth) - totalWidth) / 2);
        const std::int32_t lineHeight =
            std::max<std::int32_t>(metrics.tmHeight, iconSize);
        const std::int32_t lineY =
            (static_cast<std::int32_t>(textureHeight) - lineHeight) / 2;
        for (std::size_t index = 0; index < text.size();) {
            const std::optional<XboxButtonGlyph> glyph =
                xboxButtonGlyphAt(text, index);
            if (glyph) {
                placedGlyphs.push_back(
                    {*glyph, static_cast<std::uint32_t>(cursorX),
                     static_cast<std::uint32_t>(lineY), iconSize});
                cursorX += static_cast<std::int32_t>(iconSize);
                index += glyph->characterCount;
                continue;
            }
            const std::size_t runStart = index;
            while (index < text.size() &&
                   !xboxButtonGlyphAt(text, index).has_value()) {
                ++index;
            }
            const std::u16string_view run =
                text.substr(runStart, index - runStart);
            std::wstring wideRun;
            wideRun.reserve(run.size());
            for (const char16_t character : run) {
                wideRun.push_back(static_cast<wchar_t>(character));
            }
            const SIZE extent = measure(run);
            if (!wideRun.empty()) {
                TextOutW(deviceContext, cursorX,
                         lineY + (lineHeight - metrics.tmHeight) / 2,
                         wideRun.data(), static_cast<int>(wideRun.size()));
            }
            cursorX += extent.cx;
        }
    } else {
        RECT bounds{32, 16, static_cast<LONG>(textureWidth - 32),
                    static_cast<LONG>(textureHeight - 16)};
        DrawTextW(deviceContext, wideText.c_str(),
                  static_cast<int>(wideText.size()), &bounds,
                  DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);
    }

    const auto* bgra = static_cast<const std::uint8_t*>(bitmapPixels);
    for (std::size_t pixel = 0;
         pixel < static_cast<std::size_t>(textureWidth) * textureHeight;
         ++pixel) {
        const std::uint8_t blue = bgra[pixel * 4U];
        const std::uint8_t green = bgra[pixel * 4U + 1U];
        const std::uint8_t red = bgra[pixel * 4U + 2U];
        const std::uint8_t alpha = std::max({red, green, blue});
        image.pixels[pixel * 4U] = 255;
        image.pixels[pixel * 4U + 1U] = 255;
        image.pixels[pixel * 4U + 2U] = 255;
        image.pixels[pixel * 4U + 3U] = alpha;
    }
    if (renderGlyphs) {
        for (const PlacedGlyph& placed : placedGlyphs) {
            for (std::uint32_t y = 0; y < placed.size; ++y) {
                for (std::uint32_t x = 0; x < placed.size; ++x) {
                    const std::uint32_t sourceX =
                        placed.glyph.sourceX +
                        x * placed.glyph.sourceWidth / placed.size;
                    const std::uint32_t sourceY =
                        placed.glyph.sourceY +
                        y * placed.glyph.sourceHeight / placed.size;
                    const std::size_t sourceOffset =
                        (static_cast<std::size_t>(sourceY) *
                             xboxButtonAtlas->width +
                         sourceX) * 4U;
                    const std::uint8_t sourceAlpha =
                        xboxButtonAtlas->pixels[sourceOffset + 3U];
                    if (sourceAlpha == 0) {
                        continue;
                    }
                    const std::size_t destinationOffset =
                        (static_cast<std::size_t>(placed.y + y) *
                             textureWidth +
                         placed.x + x) * 4U;
                    const std::uint32_t inverseAlpha = 255U - sourceAlpha;
                    for (std::size_t channel = 0; channel < 3U; ++channel) {
                        image.pixels[destinationOffset + channel] =
                            static_cast<std::uint8_t>(
                                (static_cast<std::uint32_t>(
                                     xboxButtonAtlas->pixels[
                                         sourceOffset + channel]) *
                                     sourceAlpha +
                                 static_cast<std::uint32_t>(
                                     image.pixels[
                                         destinationOffset + channel]) *
                                     inverseAlpha) /
                                255U);
                    }
                    image.pixels[destinationOffset + 3U] =
                        static_cast<std::uint8_t>(
                            sourceAlpha +
                            static_cast<std::uint32_t>(
                                image.pixels[destinationOffset + 3U]) *
                                inverseAlpha /
                                255U);
                }
            }
        }
    }
    SelectObject(deviceContext, previousFont);
    SelectObject(deviceContext, previousBitmap);
    DeleteObject(font);
    DeleteObject(bitmap);
    DeleteDC(deviceContext);
    return Result::success();
}

} // namespace

std::array<float, 16> resolvePlayerHitEffectWorldTransform(
    const std::array<float, 16>& playerWorld,
    const std::array<float, 16>& boneTransform,
    bool followsPlayerBone,
    const assets::Vector3& driftOffset) noexcept {
    const std::array<float, 16> boneWorld =
        multiplyTransform(playerWorld, boneTransform);
    if (followsPlayerBone) {
        // CAnimObjEffect::Update (0x00390a88) calls getPosition/setPosition
        // on the effect root after CAnimObjEffect::Init (0x00390bb8) has
        // parented that root to the live bone. Irrlicht node position is
        // parent-relative, so the copied PhysicsEntity velocity accumulates
        // in bone-local coordinates and is transformed by the animated bone.
        // Adding it after boneWorld incorrectly treated it as world-space and
        // visibly detached moving trails from their authored limb axes.
        return multiplyTransform(boneWorld, translationMatrix(driftOffset));
    }

    // Player::AddHitEffect passes the player's quaternion to ThrowAnimEffect.
    // In CAnimObjEffect::Init's snapshot branch, the effect root receives that
    // quaternion plus getAbsolutePosition() from the bone. It does not copy
    // the bone's absolute rotation matrix.
    std::array<float, 16> snapshotWorld = playerWorld;
    snapshotWorld[12] = boneWorld[12];
    snapshotWorld[13] = boneWorld[13];
    snapshotWorld[14] = boneWorld[14];
    return snapshotWorld;
}

Result D3D11Renderer::initialize(HWND window, std::uint32_t width,
                                 std::uint32_t height) {
    if (window == nullptr || width == 0 || height == 0) {
        return Result::failure("D3D11 window target has invalid dimensions");
    }

    UINT flags = 0;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    Result result = createWindowRenderTarget(window, width, height, flags);
    if (!result && (flags & D3D11_CREATE_DEVICE_DEBUG) != 0) {
        result = createWindowRenderTarget(
            window, width, height, flags & ~D3D11_CREATE_DEVICE_DEBUG);
    }
    if (!result) {
        return result;
    }
    result = createDepthTarget(width, height);
    if (!result) {
        return result;
    }
    result = createPipeline();
    if (!result) {
        return result;
    }
    bindRenderTarget(width, height);
    return Result::success();
}

Result D3D11Renderer::initializeOffscreen(std::uint32_t width,
                                          std::uint32_t height) {
    if (width == 0 || height == 0) {
        return Result::failure("D3D11 offscreen target has invalid dimensions");
    }

    UINT flags = 0;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    Result result = createDevice(D3D_DRIVER_TYPE_WARP, flags);
    if (!result && (flags & D3D11_CREATE_DEVICE_DEBUG) != 0) {
        result = createDevice(D3D_DRIVER_TYPE_WARP,
                              flags & ~D3D11_CREATE_DEVICE_DEBUG);
    }
    if (!result) {
        return result;
    }
    result = createOffscreenRenderTarget(width, height);
    if (!result) {
        return result;
    }
    result = createDepthTarget(width, height);
    if (!result) {
        return result;
    }
    result = createPipeline();
    if (!result) {
        return result;
    }
    bindRenderTarget(width, height);
    return Result::success();
}

Result D3D11Renderer::createDevice(D3D_DRIVER_TYPE driverType, UINT flags) {
    device_.Reset();
    context_.Reset();
    constexpr std::array featureLevels{D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL selectedFeatureLevel{};
    const HRESULT result = D3D11CreateDevice(
        nullptr, driverType, nullptr, flags, featureLevels.data(),
        static_cast<UINT>(featureLevels.size()), D3D11_SDK_VERSION, &device_,
        &selectedFeatureLevel, &context_);
    return FAILED(result) ? hresultFailure("D3D11CreateDevice", result)
                          : Result::success();
}

Result D3D11Renderer::createWindowRenderTarget(HWND window,
                                                std::uint32_t width,
                                                std::uint32_t height,
                                                UINT flags) {
    device_.Reset();
    context_.Reset();
    swapChain_.Reset();
    colorTarget_.Reset();
    renderTarget_.Reset();

    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferDesc.Width = width;
    description.BufferDesc.Height = height;
    description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2;
    description.OutputWindow = window;
    description.Windowed = TRUE;
    description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    constexpr std::array featureLevels{D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL selectedFeatureLevel{};
    HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, featureLevels.data(),
        static_cast<UINT>(featureLevels.size()), D3D11_SDK_VERSION, &description,
        &swapChain_, &device_, &selectedFeatureLevel, &context_);
    if (FAILED(result)) {
        return hresultFailure("D3D11CreateDeviceAndSwapChain", result);
    }

    result = swapChain_->GetBuffer(0, IID_PPV_ARGS(&colorTarget_));
    if (FAILED(result)) {
        return hresultFailure("IDXGISwapChain::GetBuffer", result);
    }
    result = device_->CreateRenderTargetView(colorTarget_.Get(), nullptr,
                                              &renderTarget_);
    return FAILED(result)
               ? hresultFailure("ID3D11Device::CreateRenderTargetView", result)
               : Result::success();
}

Result D3D11Renderer::createOffscreenRenderTarget(std::uint32_t width,
                                                   std::uint32_t height) {
    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET;

    HRESULT result =
        device_->CreateTexture2D(&description, nullptr, &colorTarget_);
    if (FAILED(result)) {
        return hresultFailure("ID3D11Device::CreateTexture2D(color)", result);
    }
    result = device_->CreateRenderTargetView(colorTarget_.Get(), nullptr,
                                              &renderTarget_);
    return FAILED(result)
               ? hresultFailure("ID3D11Device::CreateRenderTargetView", result)
               : Result::success();
}

Result D3D11Renderer::createDepthTarget(std::uint32_t width,
                                        std::uint32_t height) {
    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    HRESULT result =
        device_->CreateTexture2D(&description, nullptr, &depthTarget_);
    if (FAILED(result)) {
        return hresultFailure("ID3D11Device::CreateTexture2D(depth)", result);
    }
    result = device_->CreateDepthStencilView(depthTarget_.Get(), nullptr,
                                              &depthView_);
    return FAILED(result)
               ? hresultFailure("ID3D11Device::CreateDepthStencilView", result)
               : Result::success();
}

Result D3D11Renderer::createPipeline() {
    ComPtr<ID3DBlob> vertexBytecode;
    Result result = compileShader(kVertexShader, "vs_5_0", vertexBytecode);
    if (!result) {
        return result;
    }
    ComPtr<ID3DBlob> pixelBytecode;
    result = compileShader(kPixelShader, "ps_5_0", pixelBytecode);
    if (!result) {
        return result;
    }

    HRESULT callResult = device_->CreateVertexShader(
        vertexBytecode->GetBufferPointer(), vertexBytecode->GetBufferSize(),
        nullptr, &vertexShader_);
    if (FAILED(callResult)) {
        return hresultFailure("ID3D11Device::CreateVertexShader", callResult);
    }
    ComPtr<ID3DBlob> lightmapVertexBytecode;
    result = compileShader(kLightmapVertexShader, "vs_5_0",
                           lightmapVertexBytecode);
    if (!result) {
        return result;
    }
    callResult = device_->CreateVertexShader(
        lightmapVertexBytecode->GetBufferPointer(),
        lightmapVertexBytecode->GetBufferSize(), nullptr,
        &lightmapVertexShader_);
    if (FAILED(callResult)) {
        return hresultFailure(
            "ID3D11Device::CreateVertexShader(lightmap)", callResult);
    }
    callResult = device_->CreatePixelShader(
        pixelBytecode->GetBufferPointer(), pixelBytecode->GetBufferSize(),
        nullptr, &pixelShader_);
    if (FAILED(callResult)) {
        return hresultFailure("ID3D11Device::CreatePixelShader", callResult);
    }
    ComPtr<ID3DBlob> effectColorMaskPixelBytecode;
    result = compileShader(kEffectColorMaskPixelShader, "ps_5_0",
                           effectColorMaskPixelBytecode);
    if (!result) {
        return result;
    }
    callResult = device_->CreatePixelShader(
        effectColorMaskPixelBytecode->GetBufferPointer(),
        effectColorMaskPixelBytecode->GetBufferSize(), nullptr,
        &effectColorMaskPixelShader_);
    if (FAILED(callResult)) {
        return hresultFailure(
            "ID3D11Device::CreatePixelShader(effect color mask)",
            callResult);
    }
    ComPtr<ID3DBlob> lightmapPixelBytecode;
    result = compileShader(kLightmapPixelShader, "ps_5_0",
                           lightmapPixelBytecode);
    if (!result) {
        return result;
    }
    callResult = device_->CreatePixelShader(
        lightmapPixelBytecode->GetBufferPointer(),
        lightmapPixelBytecode->GetBufferSize(), nullptr,
        &lightmapPixelShader_);
    if (FAILED(callResult)) {
        return hresultFailure(
            "ID3D11Device::CreatePixelShader(lightmap)", callResult);
    }
    ComPtr<ID3DBlob> lightmapAlphaTestPixelBytecode;
    result = compileShader(kLightmapAlphaTestPixelShader, "ps_5_0",
                           lightmapAlphaTestPixelBytecode);
    if (!result) {
        return result;
    }
    callResult = device_->CreatePixelShader(
        lightmapAlphaTestPixelBytecode->GetBufferPointer(),
        lightmapAlphaTestPixelBytecode->GetBufferSize(), nullptr,
        &lightmapAlphaTestPixelShader_);
    if (FAILED(callResult)) {
        return hresultFailure(
            "ID3D11Device::CreatePixelShader(lightmap alpha test)",
            callResult);
    }
    ComPtr<ID3DBlob> colorPixelBytecode;
    result = compileShader(kColorPixelShader, "ps_5_0", colorPixelBytecode);
    if (!result) {
        return result;
    }
    callResult = device_->CreatePixelShader(
        colorPixelBytecode->GetBufferPointer(),
        colorPixelBytecode->GetBufferSize(), nullptr, &colorPixelShader_);
    if (FAILED(callResult)) {
        return hresultFailure("ID3D11Device::CreatePixelShader(color)",
                              callResult);
    }
    ComPtr<ID3DBlob> hudColorPixelBytecode;
    result = compileShader(kHudColorPixelShader, "ps_5_0",
                           hudColorPixelBytecode);
    if (!result) {
        return result;
    }
    callResult = device_->CreatePixelShader(
        hudColorPixelBytecode->GetBufferPointer(),
        hudColorPixelBytecode->GetBufferSize(), nullptr,
        &hudColorPixelShader_);
    if (FAILED(callResult)) {
        return hresultFailure("ID3D11Device::CreatePixelShader(HUD color)",
                              callResult);
    }
    ComPtr<ID3DBlob> effectPixelBytecode;
    result = compileShader(kEffectPixelShader, "ps_5_0", effectPixelBytecode);
    if (!result) {
        return result;
    }
    callResult = device_->CreatePixelShader(
        effectPixelBytecode->GetBufferPointer(),
        effectPixelBytecode->GetBufferSize(), nullptr, &effectPixelShader_);
    if (FAILED(callResult)) {
        return hresultFailure("ID3D11Device::CreatePixelShader(effect)",
                              callResult);
    }
    ComPtr<ID3DBlob> alphaTestPixelBytecode;
    result = compileShader(kAlphaTestPixelShader, "ps_5_0",
                           alphaTestPixelBytecode);
    if (!result) {
        return result;
    }
    callResult = device_->CreatePixelShader(
        alphaTestPixelBytecode->GetBufferPointer(),
        alphaTestPixelBytecode->GetBufferSize(), nullptr,
        &alphaTestPixelShader_);
    if (FAILED(callResult)) {
        return hresultFailure(
            "ID3D11Device::CreatePixelShader(alpha test)", callResult);
    }
    ComPtr<ID3DBlob> reflectionPixelBytecode;
    result = compileShader(kReflectionPixelShader, "ps_5_0",
                           reflectionPixelBytecode);
    if (!result) {
        return result;
    }
    callResult = device_->CreatePixelShader(
        reflectionPixelBytecode->GetBufferPointer(),
        reflectionPixelBytecode->GetBufferSize(), nullptr,
        &reflectionPixelShader_);
    if (FAILED(callResult)) {
        return hresultFailure(
            "ID3D11Device::CreatePixelShader(reflection)", callResult);
    }
    ComPtr<ID3DBlob> hudVertexBytecode;
    result = compileShader(kHudVertexShader, "vs_5_0", hudVertexBytecode);
    if (!result) {
        return result;
    }
    callResult = device_->CreateVertexShader(
        hudVertexBytecode->GetBufferPointer(),
        hudVertexBytecode->GetBufferSize(), nullptr, &hudVertexShader_);
    if (FAILED(callResult)) {
        return hresultFailure("ID3D11Device::CreateVertexShader(HUD)",
                              callResult);
    }
    ComPtr<ID3DBlob> hudPixelBytecode;
    result = compileShader(kHudPixelShader, "ps_5_0", hudPixelBytecode);
    if (!result) {
        return result;
    }
    callResult = device_->CreatePixelShader(
        hudPixelBytecode->GetBufferPointer(),
        hudPixelBytecode->GetBufferSize(), nullptr, &hudPixelShader_);
    if (FAILED(callResult)) {
        return hresultFailure("ID3D11Device::CreatePixelShader(HUD)",
                              callResult);
    }

    constexpr std::array inputElements{
        D3D11_INPUT_ELEMENT_DESC{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
                                 offsetof(GpuVertex, position),
                                 D3D11_INPUT_PER_VERTEX_DATA, 0},
        D3D11_INPUT_ELEMENT_DESC{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
                                 offsetof(GpuVertex, normal),
                                 D3D11_INPUT_PER_VERTEX_DATA, 0},
        D3D11_INPUT_ELEMENT_DESC{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
                                 offsetof(GpuVertex, textureCoordinate),
                                 D3D11_INPUT_PER_VERTEX_DATA, 0},
        D3D11_INPUT_ELEMENT_DESC{"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0,
                                 offsetof(GpuVertex, color),
                                 D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    callResult = device_->CreateInputLayout(
        inputElements.data(), static_cast<UINT>(inputElements.size()),
        vertexBytecode->GetBufferPointer(), vertexBytecode->GetBufferSize(),
        &inputLayout_);
    if (FAILED(callResult)) {
        return hresultFailure("ID3D11Device::CreateInputLayout", callResult);
    }
    constexpr std::array lightmapInputElements{
        inputElements[0], inputElements[1], inputElements[2], inputElements[3],
        D3D11_INPUT_ELEMENT_DESC{"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 1,
                                 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    callResult = device_->CreateInputLayout(
        lightmapInputElements.data(),
        static_cast<UINT>(lightmapInputElements.size()),
        lightmapVertexBytecode->GetBufferPointer(),
        lightmapVertexBytecode->GetBufferSize(), &lightmapInputLayout_);
    if (FAILED(callResult)) {
        return hresultFailure(
            "ID3D11Device::CreateInputLayout(lightmap)", callResult);
    }

    D3D11_BUFFER_DESC transformDescription{};
    transformDescription.ByteWidth = sizeof(worldViewProjection_);
    transformDescription.Usage = D3D11_USAGE_DEFAULT;
    transformDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    callResult = device_->CreateBuffer(&transformDescription, nullptr,
                                       &transformBuffer_);
    if (FAILED(callResult)) {
        return hresultFailure("ID3D11Device::CreateBuffer(transform)", callResult);
    }
    D3D11_BUFFER_DESC viewRotationDescription{};
    viewRotationDescription.ByteWidth = sizeof(viewRotation_);
    viewRotationDescription.Usage = D3D11_USAGE_DEFAULT;
    viewRotationDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    callResult = device_->CreateBuffer(&viewRotationDescription, nullptr,
                                       &viewRotationBuffer_);
    if (FAILED(callResult)) {
        return hresultFailure("ID3D11Device::CreateBuffer(view rotation)",
                              callResult);
    }
    D3D11_BUFFER_DESC textureTransformDescription{};
    textureTransformDescription.ByteWidth = sizeof(DirectX::XMFLOAT4);
    textureTransformDescription.Usage = D3D11_USAGE_DEFAULT;
    textureTransformDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    callResult = device_->CreateBuffer(&textureTransformDescription, nullptr,
                                       &textureTransformBuffer_);
    if (FAILED(callResult)) {
        return hresultFailure(
            "ID3D11Device::CreateBuffer(texture transform)", callResult);
    }
    D3D11_BUFFER_DESC effectMaterialDescription{};
    effectMaterialDescription.ByteWidth = sizeof(EffectMaterialConstants);
    effectMaterialDescription.Usage = D3D11_USAGE_DEFAULT;
    effectMaterialDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    callResult = device_->CreateBuffer(&effectMaterialDescription, nullptr,
                                       &effectMaterialBuffer_);
    if (FAILED(callResult)) {
        return hresultFailure(
            "ID3D11Device::CreateBuffer(effect material)", callResult);
    }
    D3D11_BUFFER_DESC webLineDescription{};
    // Player owns at most two simultaneous CobWebs in the reconstructed
    // motion set. Each native line has 30 indexed quads; expand their indices
    // to a dynamic triangle list so the buffer carries 2 * 30 * 6 vertices.
    constexpr std::uint32_t kMaximumWebLineVertices = 2U *
        game::kWebLineMaximumSegments * 6U;
    webLineDescription.ByteWidth =
        kMaximumWebLineVertices * sizeof(GpuVertex);
    webLineDescription.Usage = D3D11_USAGE_DYNAMIC;
    webLineDescription.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    webLineDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    callResult = device_->CreateBuffer(&webLineDescription, nullptr,
                                       &webLineVertexBuffer_);
    if (FAILED(callResult)) {
        return hresultFailure("ID3D11Device::CreateBuffer(web line)",
                              callResult);
    }
    D3D11_BUFFER_DESC gunLineDescription = webLineDescription;
    constexpr std::uint32_t kMaximumGunLineVertices = 64;
    gunLineDescription.ByteWidth =
        kMaximumGunLineVertices * sizeof(GpuVertex);
    callResult = device_->CreateBuffer(&gunLineDescription, nullptr,
                                       &enemyGunLineVertexBuffer_);
    if (FAILED(callResult)) {
        return hresultFailure("ID3D11Device::CreateBuffer(enemy gun lines)",
                              callResult);
    }

    D3D11_SAMPLER_DESC samplerDescription{};
    samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
    callResult = device_->CreateSamplerState(&samplerDescription, &sampler_);
    if (FAILED(callResult)) {
        return hresultFailure("ID3D11Device::CreateSamplerState", callResult);
    }

    D3D11_BLEND_DESC blendDescription{};
    blendDescription.RenderTarget[0].BlendEnable = TRUE;
    blendDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDescription.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDescription.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDescription.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDescription.RenderTarget[0].DestBlendAlpha =
        D3D11_BLEND_INV_SRC_ALPHA;
    blendDescription.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDescription.RenderTarget[0].RenderTargetWriteMask =
        D3D11_COLOR_WRITE_ENABLE_ALL;
    callResult = device_->CreateBlendState(&blendDescription,
                                            &alphaBlendState_);
    if (FAILED(callResult)) {
        return hresultFailure("ID3D11Device::CreateBlendState(alpha)",
                              callResult);
    }
    blendDescription.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
    blendDescription.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
    callResult = device_->CreateBlendState(&blendDescription,
                                            &additiveBlendState_);
    if (FAILED(callResult)) {
        return hresultFailure("ID3D11Device::CreateBlendState(additive)",
                              callResult);
    }

    D3D11_DEPTH_STENCIL_DESC depthDescription{};
    depthDescription.DepthEnable = TRUE;
    depthDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthDescription.DepthFunc = D3D11_COMPARISON_LESS;
    callResult = device_->CreateDepthStencilState(&depthDescription,
                                                   &depthWriteState_);
    if (FAILED(callResult)) {
        return hresultFailure("ID3D11Device::CreateDepthStencilState(write)",
                              callResult);
    }
    depthDescription.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    callResult = device_->CreateDepthStencilState(&depthDescription,
                                                   &depthReadState_);
    if (FAILED(callResult)) {
        return hresultFailure("ID3D11Device::CreateDepthStencilState(read)",
                              callResult);
    }
    depthDescription.DepthEnable = FALSE;
    callResult = device_->CreateDepthStencilState(&depthDescription,
                                                   &depthDisabledState_);
    if (FAILED(callResult)) {
        return hresultFailure(
            "ID3D11Device::CreateDepthStencilState(disabled)", callResult);
    }

    D3D11_RASTERIZER_DESC rasterizerDescription{};
    rasterizerDescription.FillMode = D3D11_FILL_SOLID;
    // The shipped Irrlicht/OpenGL path culls back-facing triangles for the
    // ordinary level and actor materials. Leaving culling disabled makes the
    // underside of the one-sided street mesh occlude the authored below-grade
    // wall-climb camera in Room 8.
    rasterizerDescription.CullMode = D3D11_CULL_BACK;
    rasterizerDescription.FrontCounterClockwise = TRUE;
    rasterizerDescription.DepthClipEnable = TRUE;
    callResult = device_->CreateRasterizerState(&rasterizerDescription,
                                                 &rasterizerState_);
    if (FAILED(callResult)) {
        return hresultFailure("ID3D11Device::CreateRasterizerState(culled)",
                              callResult);
    }
    rasterizerDescription.CullMode = D3D11_CULL_FRONT;
    callResult = device_->CreateRasterizerState(
        &rasterizerDescription, &frontCullRasterizerState_);
    if (FAILED(callResult)) {
        return hresultFailure(
            "ID3D11Device::CreateRasterizerState(front-culled)", callResult);
    }
    // Billboards and screen-space quads are generated by the portable
    // renderer and remain two-sided, matching the original sprite path.
    rasterizerDescription.CullMode = D3D11_CULL_NONE;
    callResult = device_->CreateRasterizerState(
        &rasterizerDescription, &noCullRasterizerState_);
    return FAILED(callResult)
               ? hresultFailure("ID3D11Device::CreateRasterizerState(two-sided)",
                                callResult)
               : Result::success();
}

void D3D11Renderer::bindRenderTarget(std::uint32_t width,
                                     std::uint32_t height) {
    width_ = width;
    height_ = height;
    context_->OMSetRenderTargets(1, renderTarget_.GetAddressOf(), depthView_.Get());
    const D3D11_VIEWPORT viewport{0.0F, 0.0F, static_cast<float>(width),
                                  static_cast<float>(height), 0.0F, 1.0F};
    context_->RSSetViewports(1, &viewport);
}

void D3D11Renderer::setMeshVisible(GpuMesh& mesh, bool visible) noexcept {
    mesh.baseVisible = visible;
    mesh.visible = visible;
    if (mesh.roomId >= 1 &&
        mesh.roomId <= static_cast<std::int32_t>(roomVisibility_.size())) {
        mesh.visible = visible &&
                       roomVisibility_[static_cast<std::size_t>(mesh.roomId - 1)];
    }
}

void D3D11Renderer::setCinematicVisibleRooms(
    std::span<const bool> rooms) noexcept {
    forcedVisibleRooms_.fill(false);
    const std::size_t count = std::min(rooms.size(), forcedVisibleRooms_.size());
    std::copy_n(rooms.begin(), count, forcedVisibleRooms_.begin());
}

void D3D11Renderer::setCameraAreaRoomVisibility(
    std::span<const bool> invisibleRooms,
    std::span<const bool> visibleRooms) noexcept {
    cameraAreaInvisibleRooms_.fill(false);
    cameraAreaVisibleRooms_.fill(false);
    std::copy_n(invisibleRooms.begin(),
                std::min(invisibleRooms.size(),
                         cameraAreaInvisibleRooms_.size()),
                cameraAreaInvisibleRooms_.begin());
    std::copy_n(visibleRooms.begin(),
                std::min(visibleRooms.size(), cameraAreaVisibleRooms_.size()),
                cameraAreaVisibleRooms_.begin());
}

void D3D11Renderer::updateRoomVisibility(
    const DirectX::XMMATRIX& viewProjection) noexcept {
    roomVisibility_.fill(false);
    const std::size_t roomCount =
        std::min(roomMeshCount_, roomVisibility_.size());
    for (std::size_t roomIndex = 0; roomIndex < roomCount; ++roomIndex) {
        const assets::AxisAlignedBounds& bounds = gpuMeshes_[roomIndex].bounds;
        bool outsideLeft = true;
        bool outsideRight = true;
        bool outsideBottom = true;
        bool outsideTop = true;
        bool outsideNear = true;
        bool outsideFar = true;
        for (unsigned corner = 0; corner < 8; ++corner) {
            const float x = (corner & 1U) != 0U ? bounds.maximum.x
                                                : bounds.minimum.x;
            const float y = (corner & 2U) != 0U ? bounds.maximum.y
                                                : bounds.minimum.y;
            const float z = (corner & 4U) != 0U ? bounds.maximum.z
                                                : bounds.minimum.z;
            const DirectX::XMVECTOR clip = DirectX::XMVector4Transform(
                DirectX::XMVectorSet(x, y, z, 1.0F), viewProjection);
            const float clipX = DirectX::XMVectorGetX(clip);
            const float clipY = DirectX::XMVectorGetY(clip);
            const float clipZ = DirectX::XMVectorGetZ(clip);
            const float clipW = DirectX::XMVectorGetW(clip);
            outsideLeft &= clipX < -clipW;
            outsideRight &= clipX > clipW;
            outsideBottom &= clipY < -clipW;
            outsideTop &= clipY > clipW;
            outsideNear &= clipZ < 0.0F;
            outsideFar &= clipZ > clipW;
        }
        const bool intersectsFrustum =
            !(outsideLeft || outsideRight || outsideBottom || outsideTop ||
              outsideNear || outsideFar);
        bool visible = intersectsFrustum;
        if (cameraAreaInvisibleRooms_[roomIndex]) {
            visible = false;
        }
        if (cameraAreaVisibleRooms_[roomIndex]) {
            visible = true;
        }
        roomVisibility_[roomIndex] =
            visible || forcedVisibleRooms_[roomIndex];
    }
    for (GpuMesh& mesh : gpuMeshes_) {
        mesh.visible = mesh.baseVisible;
        if (mesh.roomId >= 1 &&
            mesh.roomId <= static_cast<std::int32_t>(roomVisibility_.size())) {
            mesh.visible =
                mesh.baseVisible &&
                roomVisibility_[static_cast<std::size_t>(mesh.roomId - 1)];
        }
    }
}

Result D3D11Renderer::uploadPreviewGeometry(
    const assets::ColladaGeometry& geometry,
    std::span<const assets::RgbaImage> mipLevels) {
    gpuMeshes_.clear();
    sharedTextureViews_.clear();
    whiteTexture_.Reset();
    environmentMeshCount_ = 0;
    roomMeshCount_ = 0;
    forcedVisibleRooms_.fill(false);
    cameraAreaInvisibleRooms_.fill(false);
    cameraAreaVisibleRooms_.fill(false);
    roomVisibility_.fill(true);
    return uploadGeometrySet({&geometry, 1}, nullptr, {}, mipLevels);
}

Result D3D11Renderer::uploadSceneGeometry(
    const assets::ColladaMeshFile& mesh,
    std::span<const assets::BtexTexture> textures) {
    if (mesh.images().size() != textures.size()) {
        return Result::failure(
            "Scene texture count does not match the BDAE image library");
    }
    gpuMeshes_.clear();
    sharedTextureViews_.clear();
    whiteTexture_.Reset();
    environmentMeshCount_ = 0;
    roomMeshCount_ = 0;
    forcedVisibleRooms_.fill(false);
    cameraAreaInvisibleRooms_.fill(false);
    cameraAreaVisibleRooms_.fill(false);
    roomVisibility_.fill(true);
    return uploadGeometrySet(mesh.sceneGeometries(), &mesh, textures, {});
}

Result D3D11Renderer::uploadLevelOneScene(
    const game::LevelOneBootstrap& levelOne) {
    gpuMeshes_.clear();
    sharedTextureViews_.clear();
    whiteTexture_.Reset();
    environmentMeshCount_ = 0;
    roomMeshCount_ = 0;
    playerMeshIndex_.reset();
    standalonePlayerMesh_ = false;
    playerHitEffectMeshStart_ = 0;
    playerHitEffectMeshCount_ = 0;
    webPelletProjectileMeshStart_ = 0;
    webPelletProjectileMeshCount_ = 0;
    molotovProjectileMeshStart_ = 0;
    molotovProjectileMeshCount_ = 0;
    boomerangProjectileMeshStart_ = 0;
    boomerangProjectileMeshCount_ = 0;
    thunderclapWaveMeshStart_ = 0;
    thunderclapWaveMeshCount_ = 0;
    thunderclapBeamMeshStart_ = 0;
    thunderclapBeamMeshCount_ = 0;
    electroRotateWaveMeshStart_ = 0;
    electroRotateWaveMeshCount_ = 0;
    electroPostBeamMeshStart_ = 0;
    electroPostBeamMeshCount_ = 0;
    electroBurstWaveMeshStart_ = 0;
    electroBurstWaveMeshCount_ = 0;
    electroBurstBillboardMeshStart_ = 0;
    electroBurstBillboardMeshCount_ = 0;
    enemyLandingShockwaveMeshStart_ = 0;
    enemyLandingShockwaveMeshCount_ = 0;
    enemyLandingCrashWallMeshStart_ = 0;
    enemyLandingCrashWallMeshCount_ = 0;
    forcedVisibleRooms_.fill(false);
    cameraAreaInvisibleRooms_.fill(false);
    cameraAreaVisibleRooms_.fill(false);
    roomVisibility_.fill(true);
    Result result = Result::success();
    for (std::size_t roomIndex = 0; roomIndex < levelOne.rooms().size();
         ++roomIndex) {
        const game::LevelRoomAsset& room = levelOne.rooms()[roomIndex];
        const std::array<float, 16> roomTransform =
            translationMatrix(room.position);
        result = uploadGeometrySet(room.geometry.sceneGeometries(),
                                   &room.geometry, room.textures, {},
                                   &roomTransform,
                                   room.linkedWaypointId > 0);
        if (!result) {
            gpuMeshes_.clear();
            return Result::failure("Could not upload " + room.name + ": " +
                                   result.message());
        }
        gpuMeshes_.back().roomId = static_cast<std::int32_t>(roomIndex + 1);
    }
    roomMeshCount_ = levelOne.rooms().size();
    const game::LevelStaticMeshAsset& sky = levelOne.introSky();
    std::vector<assets::ColladaGeometry> skyRenderGeometry(
        sky.geometry.sceneGeometries().begin(),
        sky.geometry.sceneGeometries().end());
    if (skyRenderGeometry.size() > 1) {
        // FpsSkyBoxSceneNode::collectSkyboxNodes (0x0039a4cc) gathers the
        // Collada mesh nodes, then heapsort<SkyboxNodeEntry> (0x0039a684)
        // orders them by ISceneNode::getRenderingLayer. Collada mesh nodes
        // retain ISceneNode's default layer zero here, so the recovered heap
        // sort moves the first authored entry to the end. In Level 3 this
        // draws sky_001 before Cylinder01's transparent shadows overlay.
        std::rotate(skyRenderGeometry.begin(),
                    std::next(skyRenderGeometry.begin()),
                    skyRenderGeometry.end());
    }
    result = uploadGeometrySet(skyRenderGeometry, &sky.geometry, sky.textures,
                               {});
    if (!result) {
        gpuMeshes_.clear();
        return Result::failure("Could not upload " + sky.name + ": " +
                               result.message());
    }
    gpuMeshes_.back().cameraRelative = sky.cameraRelative;
    environmentMeshCount_ = gpuMeshes_.size();
    levelObjectMeshStart_ = gpuMeshes_.size();
    for (const game::LevelObjectAsset& object : levelOne.objects()) {
        if (object.archetypeIndex >= levelOne.objectArchetypes().size()) {
            gpuMeshes_.clear();
            return Result::failure("Level object archetype index is invalid");
        }
        const game::LevelObjectArchetypeAsset& archetype =
            levelOne.objectArchetypes()[object.archetypeIndex];
        std::vector<assets::ColladaGeometry> animatedGeometry;
        std::span<const assets::ColladaGeometry> geometry =
            archetype.mesh.sceneGeometries();
        if (!object.initialAnimation.empty()) {
            const assets::ColladaAnimationClip* clip =
                archetype.animationBank.findClip(object.initialAnimation);
            if (clip == nullptr) {
                gpuMeshes_.clear();
                return Result::failure(
                    "Level object " + std::to_string(object.objectId) +
                    " initial animation " + object.initialAnimation +
                    " is missing");
            }
            result = assets::evaluateColladaPose(
                archetype.mesh, archetype.animationBank,
                clip->startMilliseconds, animatedGeometry);
            if (!result) {
                gpuMeshes_.clear();
                return Result::failure("Could not evaluate level object " +
                                       object.name + ": " +
                                       result.message());
            }
            geometry = animatedGeometry;
        } else if (!archetype.mesh.skins().empty() &&
                   !archetype.animationBank.tracks().empty()) {
            // A skinned BDAE can also contain non-rendered helper geometry.
            // web_obstacle_mesh.bdae, for example, instantiates a bbox beside
            // its skinned visual. Keep the bind pose on the same geometry
            // topology that evaluateColladaPose emits after SetAnim.
            result = assets::evaluateColladaPose(
                archetype.mesh, archetype.animationBank, 0,
                animatedGeometry);
            if (!result) {
                gpuMeshes_.clear();
                return Result::failure("Could not evaluate level object " +
                                       object.name + " bind pose: " +
                                       result.message());
            }
            geometry = animatedGeometry;
        }
        result = uploadGeometrySet(geometry, &archetype.mesh,
                                   archetype.textures, {},
                                   &object.worldTransform, true, true,
                                   archetype.additiveTextureNameFragment);
        if (!result) {
            gpuMeshes_.clear();
            return Result::failure("Could not upload level object " +
                                   object.name + ": " + result.message());
        }
        gpuMeshes_.back().roomId = object.roomId;
        if (object.additiveBlend) {
            // CAnimatedObject::ProcessUserAttr (0x002fd560) calls
            // SetMaterialType(node, 0x0d) when AddColor is authored. Native
            // 0x0d is TRANSPARENT_ADD_COLOR and uses texture layer zero with
            // GL_SRC_ALPHA / GL_ONE (0x004559b0).
            for (DrawBatch& batch : gpuMeshes_.back().drawBatches) {
                batch.alphaTest = false;
                batch.alphaBlend = false;
                batch.additiveBlend = true;
                batch.reflectionTwoLayer = false;
                batch.lightmapTwoLayer = false;
            }
        }
        setMeshVisible(gpuMeshes_.back(), object.visible);
    }
    introActorMeshStart_ = gpuMeshes_.size();
    for (const game::CinematicActorAsset& actor : levelOne.introActors()) {
        if (actor.mesh.images().size() != actor.textures.size()) {
            gpuMeshes_.clear();
            return Result::failure(
                "Actor texture count does not match its BDAE image library");
        }
        std::vector<assets::ColladaGeometry> animatedGeometry;
        result = assets::evaluateColladaPose(
            actor.mesh, actor.animation,
            actor.animationClipStartMilliseconds, animatedGeometry);
        if (!result) {
            gpuMeshes_.clear();
            return Result::failure("Could not evaluate actor " +
                                   actor.sceneNodeName + ": " +
                                   result.message());
        }
        result = uploadGeometrySet(animatedGeometry, &actor.mesh,
                                   actor.textures, {}, nullptr, true, true);
        if (!result) {
            gpuMeshes_.clear();
            return Result::failure("Could not upload actor " +
                                   actor.sceneNodeName + ": " +
                                   result.message());
        }
        setMeshVisible(gpuMeshes_.back(),
                       actor.animationStartMilliseconds == 0);
        if (actor.objectId == levelOne.player().objectId) {
            playerMeshIndex_ = gpuMeshes_.size() - 1;
        }
    }
    if (!playerMeshIndex_) {
        const assets::ColladaAnimationClip* initialClip =
            levelOne.player().animationBank.findClip(
                levelOne.player().initialAnimation);
        if (initialClip == nullptr) {
            gpuMeshes_.clear();
            return Result::failure(
                "Player initial animation is missing during scene upload");
        }
        std::vector<assets::ColladaGeometry> animatedGeometry;
        result = assets::evaluateColladaPose(
            levelOne.player().mesh, levelOne.player().animationBank,
            initialClip->startMilliseconds, animatedGeometry);
        if (!result) {
            gpuMeshes_.clear();
            return Result::failure("Could not evaluate standalone player: " +
                                   result.message());
        }
        result = uploadGeometrySet(
            animatedGeometry, &levelOne.player().mesh,
            levelOne.player().textures, {}, &levelOne.player().worldTransform,
            true, true);
        if (!result) {
            gpuMeshes_.clear();
            return Result::failure("Could not upload standalone player: " +
                                   result.message());
        }
        playerMeshIndex_ = gpuMeshes_.size() - 1;
        standalonePlayerMesh_ = true;
    }
    gameplayCinematicMeshStart_ = gpuMeshes_.size();
    for (const game::LevelCinematicAsset& cinematic :
         levelOne.cinematics()) {
        for (const game::CinematicActorAsset& actor : cinematic.actors) {
            if (actor.mesh.images().size() != actor.textures.size()) {
                gpuMeshes_.clear();
                return Result::failure(
                    "Cinematic actor texture count does not match its BDAE "
                    "image library");
            }
            std::vector<assets::ColladaGeometry> animatedGeometry;
            result = assets::evaluateColladaPose(
                actor.mesh, actor.animation,
                actor.animationClipStartMilliseconds, animatedGeometry);
            if (!result) {
                gpuMeshes_.clear();
                return Result::failure("Could not evaluate cinematic actor " +
                                       actor.sceneNodeName + ": " +
                                       result.message());
            }
            result = uploadGeometrySet(animatedGeometry, &actor.mesh,
                                       actor.textures, {}, nullptr, true,
                                       true);
            if (!result) {
                gpuMeshes_.clear();
                return Result::failure("Could not upload cinematic actor " +
                                       actor.sceneNodeName + ": " +
                                       result.message());
            }
            setMeshVisible(gpuMeshes_.back(), false);
        }
    }
    enemyMeshStart_ = gpuMeshes_.size();
    for (const game::LevelEnemyAsset& enemy : levelOne.enemies()) {
        if (enemy.archetypeIndex >= levelOne.enemyArchetypes().size()) {
            gpuMeshes_.clear();
            return Result::failure("Enemy archetype index is invalid");
        }
        const game::EnemyArchetypeAsset& archetype =
            levelOne.enemyArchetypes()[enemy.archetypeIndex];
        std::vector<assets::ColladaGeometry> animatedGeometry;
        std::span<const assets::ColladaGeometry> geometry =
            archetype.mesh.sceneGeometries();
        if (!enemy.initialAnimation.empty()) {
            const assets::ColladaAnimationClip* clip =
                archetype.animationBank.findClip(enemy.initialAnimation);
            if (clip == nullptr) {
                gpuMeshes_.clear();
                return Result::failure("Enemy initial animation is missing");
            }
            result = assets::evaluateColladaPose(
                archetype.mesh, archetype.animationBank,
                clip->startMilliseconds, animatedGeometry);
            if (!result) {
                gpuMeshes_.clear();
                return Result::failure("Could not evaluate enemy " +
                                       enemy.name + ": " + result.message());
            }
            geometry = animatedGeometry;
        }
        result = uploadGeometrySet(geometry, &archetype.mesh,
                                   archetype.textures, {},
                                   &enemy.worldTransform, true);
        if (!result) {
            gpuMeshes_.clear();
            return Result::failure("Could not upload enemy " + enemy.name +
                                   ": " + result.message());
        }
        gpuMeshes_.back().roomId = enemy.roomId;
        setMeshVisible(gpuMeshes_.back(), enemy.visible);
    }
    playerHitEffectMeshStart_ = gpuMeshes_.size();
    constexpr std::size_t hitEffectSlotsPerDefinition = 4;
    playerHitEffectMeshCount_ =
        levelOne.playerHitEffects().size() * hitEffectSlotsPerDefinition;
    for (const game::PlayerHitEffectAsset& effect :
         levelOne.playerHitEffects()) {
        if (effect.mesh.images().size() != effect.textures.size()) {
            gpuMeshes_.clear();
            return Result::failure(
                "Player hit-effect texture count does not match its BDAE "
                "image library");
        }
        if (renderTraceEnabled()) {
            std::cerr << "player_hit_effect_asset id="
                      << effect.definition.id << " name="
                      << effect.definition.name << " bone="
                      << effect.definition.boneName << " snapshot="
                      << effect.definition.snapshotBoneTransform << " layer="
                      << effect.definition.renderingParameter << " lifetime="
                      << effect.definition.lifetimeMilliseconds
                      << " animation_clips="
                      << effect.animation.clips().size();
            for (std::size_t clipIndex = 0;
                 clipIndex < effect.animation.clips().size(); ++clipIndex) {
                const auto& clip = effect.animation.clips()[clipIndex];
                std::cerr << " clip[" << clipIndex << "]=" << clip.name
                          << ':' << clip.durationMilliseconds();
            }
            for (const assets::ColladaGeometry& geometry :
                 effect.mesh.sceneGeometries()) {
                std::cerr << " geometry=" << geometry.name << " bounds=["
                          << geometry.bounds.minimum.x << ','
                          << geometry.bounds.minimum.y << ','
                          << geometry.bounds.minimum.z << ';'
                          << geometry.bounds.maximum.x << ','
                          << geometry.bounds.maximum.y << ','
                          << geometry.bounds.maximum.z << ']';
            }
            for (const assets::ColladaMaterial& material :
                 effect.mesh.materials()) {
                std::cerr << " material=" << material.name << " ambient=["
                          << material.ambientColor[0] << ','
                          << material.ambientColor[1] << ','
                          << material.ambientColor[2] << ','
                          << material.ambientColor[3] << ']';
            }
            for (std::size_t textureIndex = 0;
                 textureIndex < effect.textures.size(); ++textureIndex) {
                std::cerr << " texture[" << textureIndex << "]_alpha="
                          << effect.textures[textureIndex].containsAlpha()
                          << "/transparent="
                          << textureHasTransparency(
                                 effect.textures[textureIndex]);
            }
            std::cerr << '\n';
        }
        for (std::size_t slot = 0; slot < hitEffectSlotsPerDefinition;
             ++slot) {
            result = uploadGeometrySet(
                effect.mesh.sceneGeometries(), &effect.mesh,
                effect.textures, {}, nullptr, true, true);
            if (!result) {
                gpuMeshes_.clear();
                return Result::failure("Could not upload player hit effect " +
                                       effect.definition.name + ": " +
                                       result.message());
            }
            // CAnimObjEffect::Init (0x00390bb8) replaces every source
            // material with native type 0x1d or 0x1e (selected per spawn),
            // disables both culling modes, and puts the complete effect node
            // on rendering layer 7.
            // CAnimObjEffect::Update then fades that material's diffuse alpha.
            for (DrawBatch& batch : gpuMeshes_.back().drawBatches) {
                batch.alphaTest = false;
                batch.alphaBlend = true;
                batch.additiveBlend = false;
                batch.effectColorMask =
                    batch.textureIndex < effect.textures.size() &&
                    !textureHasTransparency(
                        effect.textures[batch.textureIndex]);
                batch.reflectionTwoLayer = false;
                batch.lightmapTwoLayer = false;
                batch.backFaceCulling = false;
                batch.frontFaceCulling = false;
                batch.renderingLayer = 7;
            }
            setMeshVisible(gpuMeshes_.back(), false);
        }
    }
    webPelletProjectileMeshStart_ = gpuMeshes_.size();
    // Player::ShootWebPellet uses CLevel's bounded CBullet pool. Eight slots
    // cover the native input cadence while keeping renderer allocation fixed.
    webPelletProjectileMeshCount_ = 8;
    {
        const game::WebPelletProjectileAsset& projectile =
            levelOne.webPelletProjectile();
        if (projectile.mesh.images().size() != projectile.textures.size()) {
            gpuMeshes_.clear();
            return Result::failure(
                "Web pellet texture count does not match its BDAE image library");
        }
        for (std::size_t index = 0;
             index < webPelletProjectileMeshCount_; ++index) {
            result = uploadGeometrySet(
                projectile.mesh.sceneGeometries(), &projectile.mesh,
                projectile.textures, {}, nullptr, true, true);
            if (!result) {
                gpuMeshes_.clear();
                return Result::failure(
                    "Could not upload player web pellet: " +
                    result.message());
            }
            setMeshVisible(gpuMeshes_.back(), false);
        }
    }
    molotovProjectileMeshStart_ = gpuMeshes_.size();
    molotovProjectileMeshCount_ = static_cast<std::size_t>(std::count_if(
        levelOne.enemies().begin(), levelOne.enemies().end(),
        [](const game::LevelEnemyAsset& enemy) {
            return enemy.gameType == "RangeThug_molotov";
        }));
    if (molotovProjectileMeshCount_ != 0) {
        const game::MolotovProjectileAsset& projectile =
            levelOne.molotovProjectile();
        if (projectile.mesh.images().size() != projectile.textures.size()) {
            gpuMeshes_.clear();
            return Result::failure(
                "Molotov texture count does not match its BDAE image library");
        }
        const assets::ColladaAnimationClip* fly =
            projectile.animationBank.findClip("fly");
        if (fly == nullptr) {
            gpuMeshes_.clear();
            return Result::failure("Molotov fly animation is missing");
        }
        std::vector<assets::ColladaGeometry> geometry;
        result = assets::evaluateColladaPose(
            projectile.mesh, projectile.animationBank, fly->startMilliseconds,
            geometry);
        if (!result) {
            gpuMeshes_.clear();
            return Result::failure("Could not evaluate molotov projectile: " +
                                   result.message());
        }
        for (std::size_t index = 0; index < molotovProjectileMeshCount_;
             ++index) {
            result = uploadGeometrySet(geometry, &projectile.mesh,
                                       projectile.textures, {}, nullptr, true);
            if (!result) {
                gpuMeshes_.clear();
                return Result::failure(
                    "Could not upload molotov projectile: " +
                    result.message());
            }
            setMeshVisible(gpuMeshes_.back(), false);
        }
    }
    boomerangProjectileMeshStart_ = gpuMeshes_.size();
    boomerangProjectileMeshCount_ = static_cast<std::size_t>(std::count_if(
        levelOne.enemies().begin(), levelOne.enemies().end(),
        [](const game::LevelEnemyAsset& enemy) {
            return enemy.gameType == "Robot_Phantom";
        }));
    if (boomerangProjectileMeshCount_ != 0) {
        const game::BoomerangProjectileAsset& projectile =
            levelOne.boomerangProjectile();
        if (projectile.mesh.images().size() != projectile.textures.size()) {
            gpuMeshes_.clear();
            return Result::failure(
                "Boomerang texture count does not match its BDAE image library");
        }
        // At 0x0035b568/0x0035b728 both constructors load r1 from
        // 0x004e538a (`weapons`), set r2=true and r3=0, then call
        // IAnimatedObject::SetAnim at 0x00311150.
        const assets::ColladaAnimationClip* animation =
            projectile.animationBank.findClip("weapons");
        if (animation == nullptr) {
            gpuMeshes_.clear();
            return Result::failure(
                "Boomerang weapons animation is missing");
        }
        std::vector<assets::ColladaGeometry> geometry;
        result = assets::evaluateColladaPose(
            projectile.mesh, projectile.animationBank,
            animation->startMilliseconds, geometry);
        if (!result) {
            gpuMeshes_.clear();
            return Result::failure("Could not evaluate boomerang projectile: " +
                                   result.message());
        }
        for (std::size_t index = 0;
             index < boomerangProjectileMeshCount_; ++index) {
            result = uploadGeometrySet(geometry, &projectile.mesh,
                                       projectile.textures, {}, nullptr,
                                       true);
            if (!result) {
                gpuMeshes_.clear();
                return Result::failure(
                    "Could not upload boomerang projectile: " +
                    result.message());
            }
            setMeshVisible(gpuMeshes_.back(), false);
        }
    }
    const std::size_t electroBossCount =
        static_cast<std::size_t>(std::count_if(
            levelOne.enemies().begin(), levelOne.enemies().end(),
            [](const game::LevelEnemyAsset& enemy) {
                return enemy.gameType == "Boss_Electro" &&
                       enemy.enemyTypeId == 7;
            }));
    const game::ElectroEffectAsset& electroEffects =
        levelOne.electroEffects();
    const auto uploadElectroPool =
        [this, &result](const game::ElectroEffectModelAsset& effect,
                        std::string_view clipName, std::size_t count,
                        std::size_t& start, std::size_t& storedCount) {
            start = gpuMeshes_.size();
            storedCount = count;
            if (count == 0) {
                return Result::success();
            }
            if (effect.mesh.images().size() != effect.textures.size()) {
                return Result::failure(
                    "Electro effect texture count does not match its BDAE "
                    "image library");
            }
            const assets::ColladaAnimationClip* clip =
                effect.animationBank.findClip(clipName);
            if (clip == nullptr) {
                return Result::failure("Electro effect animation is missing");
            }
            std::vector<assets::ColladaGeometry> geometry;
            result = assets::evaluateColladaPose(
                effect.mesh, effect.animationBank, clip->startMilliseconds,
                geometry);
            if (!result) {
                return Result::failure("Could not evaluate Electro effect: " +
                                       result.message());
            }
            for (std::size_t index = 0; index < count; ++index) {
                result = uploadGeometrySet(geometry, &effect.mesh,
                                           effect.textures, {}, nullptr, true,
                                           true);
                if (!result) {
                    return Result::failure(
                        "Could not upload Electro effect: " +
                        result.message());
                }
                // CElectricPost (0x00354e94) and the summon objects select
                // native material type 0x0d (TRANSPARENT_ADD_COLOR). Keeping
                // this explicit prevents the authored lightning planes from
                // appearing as opaque black pillars.
                for (DrawBatch& batch : gpuMeshes_.back().drawBatches) {
                    batch.alphaTest = false;
                    batch.alphaBlend = false;
                    batch.additiveBlend = true;
                    batch.reflectionTwoLayer = false;
                    batch.lightmapTwoLayer = false;
                    batch.renderingLayer = 7;
                }
                setMeshVisible(gpuMeshes_.back(), false);
            }
            return Result::success();
        };

    result = uploadElectroPool(
        electroEffects.wave, "wave", electroBossCount == 0 ? 0U : 5U,
        thunderclapWaveMeshStart_, thunderclapWaveMeshCount_);
    if (!result) {
        gpuMeshes_.clear();
        return result;
    }
    result = uploadElectroPool(
        electroEffects.beam, "keep", electroBossCount == 0 ? 0U : 5U,
        thunderclapBeamMeshStart_, thunderclapBeamMeshCount_);
    if (!result) {
        gpuMeshes_.clear();
        return result;
    }
    result = uploadElectroPool(
        electroEffects.wave, "wave", electroBossCount,
        electroRotateWaveMeshStart_, electroRotateWaveMeshCount_);
    if (!result) {
        gpuMeshes_.clear();
        return result;
    }
    result = uploadElectroPool(
        electroEffects.beam, "keep", electroBossCount * 3U,
        electroPostBeamMeshStart_, electroPostBeamMeshCount_);
    if (!result) {
        gpuMeshes_.clear();
        return result;
    }
    result = uploadElectroPool(
        electroEffects.wave, "wave", electroBossCount * 8U,
        electroBurstWaveMeshStart_, electroBurstWaveMeshCount_);
    if (!result) {
        gpuMeshes_.clear();
        return result;
    }
    result = uploadElectroPool(
        electroEffects.waveBillboard, "wave", electroBossCount * 8U,
        electroBurstBillboardMeshStart_, electroBurstBillboardMeshCount_);
    if (!result) {
        gpuMeshes_.clear();
        return result;
    }

    // EffectManager's animated-object pool is shared. Four slots per landing
    // model cover all three simultaneously active Room-1 thugs plus one
    // overlapping prior effect without allocating while rendering.
    const auto uploadLandingEffectPool =
        [this, &result](const game::ElectroEffectModelAsset& effect,
                        std::size_t count, std::size_t& start,
                        std::size_t& poolCount) -> Result {
            start = gpuMeshes_.size();
            poolCount = count;
            for (std::size_t index = 0; index < count; ++index) {
                result = uploadGeometrySet(
                    effect.mesh.sceneGeometries(), &effect.mesh,
                    effect.textures, {}, nullptr, true, true);
                if (!result) {
                    return Result::failure(
                        "Could not upload enemy landing effect: " +
                        result.message());
                }
                for (DrawBatch& batch : gpuMeshes_.back().drawBatches) {
                    batch.alphaTest = false;
                    batch.alphaBlend = true;
                    batch.additiveBlend = false;
                    batch.effectColorMask =
                        batch.textureIndex < effect.textures.size() &&
                        !textureHasTransparency(
                            effect.textures[batch.textureIndex]);
                    batch.reflectionTwoLayer = false;
                    batch.lightmapTwoLayer = false;
                    batch.backFaceCulling = false;
                    batch.frontFaceCulling = false;
                    batch.renderingLayer = 7;
                }
                setMeshVisible(gpuMeshes_.back(), false);
            }
            return Result::success();
        };
    constexpr std::size_t landingEffectSlots = 4;
    result = uploadLandingEffectPool(
        levelOne.enemyLandingEffects().shockwave, landingEffectSlots,
        enemyLandingShockwaveMeshStart_, enemyLandingShockwaveMeshCount_);
    if (!result) {
        gpuMeshes_.clear();
        return result;
    }
    result = uploadLandingEffectPool(
        levelOne.enemyLandingEffects().crashWall, landingEffectSlots,
        enemyLandingCrashWallMeshStart_, enemyLandingCrashWallMeshCount_);
    if (!result) {
        gpuMeshes_.clear();
        return result;
    }
    result = uploadHudTexture(levelOne.hud());
    if (!result) {
        return result;
    }
    effectTexture_.Reset();
    effectVertexBuffer_.Reset();
    effectAlphaVertexCount_ = 0;
    effectAdditiveVertexCount_ = 0;
    effectVertexCapacity_ = 0;
    const assets::RgbaImage& effectImage = levelOne.effects().texture.image();
    if (effectImage.width == 0 || effectImage.height == 0 ||
        effectImage.pixels.size() !=
            static_cast<std::size_t>(effectImage.width) *
                effectImage.height * 4) {
        return Result::failure("Effect texture image is invalid");
    }
    result = createTextureView(
        std::span<const assets::RgbaImage>(&effectImage, 1), effectTexture_);
    if (!result) {
        return result;
    }
    webLineTexture_.Reset();
    if (levelOne.webLine().texture.mipLevels().empty()) {
        return Result::failure("Native web-line texture image is invalid");
    }
    result = createTextureView(levelOne.webLine().texture.mipLevels(),
                               webLineTexture_);
    if (!result) {
        return Result::failure("Could not upload native web-line texture: " +
                               result.message());
    }
    hintTexture_.Reset();
    hintVertexBuffer_.Reset();
    hintVertexCount_ = 0;
    hintVertexCapacity_ = 0;
    if (levelOne.hints().empty()) {
        return Result::success();
    }
    const std::string& hintSpriteFile = levelOne.hints().front().spriteFile;
    if (std::any_of(levelOne.hints().begin(), levelOne.hints().end(),
                    [&hintSpriteFile](const game::LevelHintAsset& hint) {
                        return hint.spriteFile != hintSpriteFile;
                    })) {
        return Result::failure(
            "Level hints reference more than one sprite texture");
    }
    const assets::RgbaImage& hintImage =
        levelOne.hints().front().texture.image();
    if (hintImage.width == 0 || hintImage.height == 0 ||
        hintImage.pixels.size() !=
            static_cast<std::size_t>(hintImage.width) * hintImage.height * 4) {
        return Result::failure("Hint texture image is invalid");
    }
    return createTextureView(
        std::span<const assets::RgbaImage>(&hintImage, 1), hintTexture_);
}

Result D3D11Renderer::updateLevelRooms(
    const game::LevelOneBootstrap& levelOne,
    const game::LevelCinematicRuntime& cinematics) {
    const std::span<const game::RoomMotionState> states =
        cinematics.roomMotionStates();
    if (states.size() != levelOne.rooms().size() ||
        roomMeshCount_ != levelOne.rooms().size() ||
        roomMeshCount_ > gpuMeshes_.size()) {
        return Result::failure("Level room GPU resources are incomplete");
    }
    for (std::size_t index = 0; index < states.size(); ++index) {
        const game::LevelRoomAsset& asset = levelOne.rooms()[index];
        const game::RoomMotionState& state = states[index];
        if (state.objectId != asset.objectId ||
            state.roomId != static_cast<std::int32_t>(index + 1)) {
            return Result::failure("Level room runtime order is invalid");
        }
        GpuMesh& gpuMesh = gpuMeshes_[index];
        if (!state.movingRoom) {
            continue;
        }
        const std::array<float, 16> world =
            translationMatrix(state.position);
        Result result = updateDynamicMesh(
            gpuMesh, asset.geometry.sceneGeometries(), &world);
        if (!result) {
            return Result::failure("Could not move " + asset.name + ": " +
                                   result.message());
        }
        const std::span<const assets::ColladaGeometry> geometries =
            asset.geometry.sceneGeometries();
        if (!geometries.empty()) {
            assets::AxisAlignedBounds bounds = geometries.front().bounds;
            for (const assets::ColladaGeometry& geometry :
                 geometries.subspan(1)) {
                bounds.minimum.x =
                    std::min(bounds.minimum.x, geometry.bounds.minimum.x);
                bounds.minimum.y =
                    std::min(bounds.minimum.y, geometry.bounds.minimum.y);
                bounds.minimum.z =
                    std::min(bounds.minimum.z, geometry.bounds.minimum.z);
                bounds.maximum.x =
                    std::max(bounds.maximum.x, geometry.bounds.maximum.x);
                bounds.maximum.y =
                    std::max(bounds.maximum.y, geometry.bounds.maximum.y);
                bounds.maximum.z =
                    std::max(bounds.maximum.z, geometry.bounds.maximum.z);
            }
            bounds.minimum.x += state.position.x;
            bounds.minimum.y += state.position.y;
            bounds.minimum.z += state.position.z;
            bounds.maximum.x += state.position.x;
            bounds.maximum.y += state.position.y;
            bounds.maximum.z += state.position.z;
            gpuMesh.bounds = bounds;
        }
    }
    return Result::success();
}

Result D3D11Renderer::updateLevelOneActors(
    const game::LevelOneBootstrap& levelOne,
    std::uint32_t timestampMilliseconds) {
    std::size_t gameplayCinematicActorCount = 0;
    for (const game::LevelCinematicAsset& cinematic :
         levelOne.cinematics()) {
        gameplayCinematicActorCount += cinematic.actors.size();
    }
    if (gpuMeshes_.size() != levelOne.introActors().size() +
                                 (standalonePlayerMesh_ ? 1U : 0U) +
                                 gameplayCinematicActorCount +
                                  levelOne.enemies().size() +
                                  playerHitEffectMeshCount_ +
                                  webPelletProjectileMeshCount_ +
                                  molotovProjectileMeshCount_ +
                                  boomerangProjectileMeshCount_ +
                                  thunderclapWaveMeshCount_ +
                                  thunderclapBeamMeshCount_ +
                                  electroRotateWaveMeshCount_ +
                                  electroPostBeamMeshCount_ +
                                  electroBurstWaveMeshCount_ +
                                  electroBurstBillboardMeshCount_ +
                                  enemyLandingShockwaveMeshCount_ +
                                  enemyLandingCrashWallMeshCount_ +
                                  levelOne.objects().size() +
                                 environmentMeshCount_) {
        return Result::failure("Level-one actor GPU resources are incomplete");
    }
    for (std::size_t actorIndex = 0;
         actorIndex < levelOne.introActors().size(); ++actorIndex) {
        const game::CinematicActorAsset& actor =
            levelOne.introActors()[actorIndex];
        GpuMesh& gpuMesh = gpuMeshes_[actorIndex + introActorMeshStart_];
        if (!gpuMesh.dynamicVertices) {
            return Result::failure("Actor vertex buffer is not dynamic");
        }
        setMeshVisible(gpuMesh,
                       timestampMilliseconds >=
                           actor.animationStartMilliseconds);
        const std::uint32_t animationTime =
            actor.animationTimestamp(timestampMilliseconds);
        std::vector<assets::ColladaGeometry> animatedGeometry;
        Result result = assets::evaluateColladaPose(
            actor.mesh, actor.animation, animationTime, animatedGeometry);
        if (!result) {
            return Result::failure("Could not animate actor " +
                                   actor.sceneNodeName + ": " +
                                   result.message());
        }
        if (renderTraceEnabled()) {
            std::cerr << "cinematic_actor object=" << actor.objectId
                      << " node=" << actor.sceneNodeName
                      << " timeline_ms=" << timestampMilliseconds
                      << " animation_ms=" << animationTime
                      << " scene_translation=[" << actor.worldTransform[12]
                      << ',' << actor.worldTransform[13] << ','
                      << actor.worldTransform[14] << ']';
            for (const assets::ColladaGeometry& geometry : animatedGeometry) {
                std::cerr << " bounds=[" << geometry.bounds.minimum.x << ','
                          << geometry.bounds.minimum.y << ','
                          << geometry.bounds.minimum.z << ";"
                          << geometry.bounds.maximum.x << ','
                          << geometry.bounds.maximum.y << ','
                          << geometry.bounds.maximum.z << ']';
            }
            std::cerr << '\n';
            if (actor.objectId == 1262 && timestampMilliseconds == 0) {
                for (const assets::ColladaSceneNode& node :
                     actor.mesh.sceneNodes()) {
                    std::cerr << "cinematic_actor_node object=1262 id="
                              << node.id << " scope=" << node.scopeId
                              << " position=[" << node.position.x << ','
                              << node.position.y << ',' << node.position.z
                              << "] geometries=" << node.geometryIndices.size()
                              << '\n';
                }
                for (const assets::ColladaAnimationTrack& track :
                     actor.animation.tracks()) {
                    const auto first = track.sample(0);
                    const auto middle = track.sample(9700);
                    const auto last = track.sample(
                        actor.animation.durationMilliseconds());
                    std::cerr << "cinematic_actor_track object=1262 target="
                              << track.targetNode << " property="
                              << static_cast<int>(track.property)
                              << " components=" << track.componentCount
                              << " samples="
                              << track.timestampsMilliseconds.size()
                              << " first=[" << first.value[0] << ','
                              << first.value[1] << ',' << first.value[2]
                              << ',' << first.value[3] << "] middle=["
                              << middle.value[0] << ',' << middle.value[1]
                              << ',' << middle.value[2] << ','
                              << middle.value[3] << "] last=["
                              << last.value[0] << ',' << last.value[1] << ','
                              << last.value[2] << ',' << last.value[3] << ']'
                              << '\n';
                }
                for (std::size_t imageIndex = 0;
                     imageIndex < actor.mesh.images().size(); ++imageIndex) {
                    std::cerr << "cinematic_actor_image object=1262 index="
                              << imageIndex << " path="
                              << actor.mesh.images()[imageIndex].sourcePath
                              << '\n';
                }
                for (const assets::ColladaMaterial& material :
                     actor.mesh.materials()) {
                    std::cerr << "cinematic_actor_material object=1262 id="
                              << material.id << " diffuse=";
                    if (material.diffuseImageIndex) {
                        std::cerr << *material.diffuseImageIndex;
                    } else {
                        std::cerr << "none";
                    }
                    std::cerr << " secondary=";
                    if (material.secondaryImageIndex) {
                        std::cerr << *material.secondaryImageIndex;
                    } else {
                        std::cerr << "none";
                    }
                    std::cerr << '\n';
                }
                for (const assets::ColladaGeometry& geometry :
                     actor.mesh.geometries()) {
                    std::cerr << "cinematic_actor_geometry object=1262 id="
                              << geometry.id << " name=" << geometry.name
                              << " vertices=" << geometry.vertices.size()
                              << " buffers=" << geometry.meshBuffers.size()
                              << " bounds=[" << geometry.bounds.minimum.x
                              << ',' << geometry.bounds.minimum.y << ','
                              << geometry.bounds.minimum.z << ';'
                              << geometry.bounds.maximum.x << ','
                              << geometry.bounds.maximum.y << ','
                              << geometry.bounds.maximum.z << ']';
                    if (!geometry.vertices.empty()) {
                        const assets::ColladaVertex& vertex =
                            geometry.vertices.front();
                        std::cerr << " first_position=[" << vertex.position.x
                                  << ',' << vertex.position.y << ','
                                  << vertex.position.z << "] first_uv=["
                                  << vertex.textureCoordinate[0] << ','
                                  << vertex.textureCoordinate[1] << ']';
                    }
                    std::cerr << '\n';
                    for (const assets::ColladaMeshBuffer& buffer :
                         geometry.meshBuffers) {
                        std::cerr
                            << "cinematic_actor_buffer object=1262 geometry="
                            << geometry.name << " material="
                            << buffer.materialName << " indices="
                            << buffer.indices.size() << '\n';
                    }
                }
            }
        }
        // Cinematic animation tracks are exported in scene/world space for
        // both skinned actors and rigid props. Applying the linked scene-node
        // transform here a second time displaces rigid actors such as CI_Car.
        result = updateDynamicMesh(gpuMesh, animatedGeometry, nullptr);
        if (!result) {
            return Result::failure("Could not update actor " +
                                   actor.sceneNodeName + ": " +
                                   result.message());
        }
        updateMaterialAnimation(gpuMesh, actor.animation, animationTime);
    }
    return Result::success();
}

Result D3D11Renderer::updateGameplayCinematicActors(
    const game::LevelOneBootstrap& levelOne,
    const game::LevelCinematicAsset* cinematic,
    std::uint32_t timestampMilliseconds) {
    std::size_t totalActorCount = 0;
    std::size_t activeActorOffset = 0;
    bool foundActive = cinematic == nullptr;
    for (const game::LevelCinematicAsset& candidate : levelOne.cinematics()) {
        if (&candidate == cinematic) {
            activeActorOffset = totalActorCount;
            foundActive = true;
        }
        totalActorCount += candidate.actors.size();
    }
    if (!foundActive || gameplayCinematicMeshStart_ + totalActorCount !=
                            enemyMeshStart_) {
        return Result::failure(
            "Gameplay cinematic actor GPU resources are incomplete");
    }
    for (std::size_t index = 0; index < totalActorCount; ++index) {
        setMeshVisible(gpuMeshes_[gameplayCinematicMeshStart_ + index], false);
    }
    if (cinematic == nullptr || cinematic->actors.empty()) {
        return Result::success();
    }

    const bool replacesPlayer = std::any_of(
        cinematic->actors.begin(), cinematic->actors.end(),
        [&levelOne](const game::CinematicActorAsset& actor) {
            return actor.objectId == levelOne.player().objectId;
        });
    if (replacesPlayer) {
        // Native PlayDAEAnim pushes an animator onto the existing Spider-Man
        // object (CCinematicThread::PlayDAEAnim, 0x003709ec). Our cinematic
        // GPU stream is separate, so hide whichever persistent player mesh
        // was uploaded: Level 1's intro actor or a later level's standalone
        // gameplay player.
        if (!playerMeshIndex_ || *playerMeshIndex_ >= gpuMeshes_.size()) {
            return Result::failure(
                "Persistent player actor is missing during cinematic");
        }
        setMeshVisible(gpuMeshes_[*playerMeshIndex_], false);
    }

    for (std::size_t enemyIndex = 0;
         enemyIndex < levelOne.enemies().size(); ++enemyIndex) {
        const std::int32_t enemyId = levelOne.enemies()[enemyIndex].objectId;
        if (std::any_of(cinematic->actors.begin(), cinematic->actors.end(),
                        [enemyId](const game::CinematicActorAsset& actor) {
                            return actor.objectId == enemyId;
                        })) {
            setMeshVisible(gpuMeshes_[enemyMeshStart_ + enemyIndex], false);
        }
    }

    for (std::size_t actorIndex = 0;
         actorIndex < cinematic->actors.size(); ++actorIndex) {
        const game::CinematicActorAsset& actor =
            cinematic->actors[actorIndex];
        GpuMesh& gpuMesh =
            gpuMeshes_[gameplayCinematicMeshStart_ + activeActorOffset +
                       actorIndex];
        setMeshVisible(gpuMesh,
                       timestampMilliseconds >=
                           actor.animationStartMilliseconds);
        const std::uint32_t animationTime =
            actor.animationTimestamp(timestampMilliseconds);
        std::vector<assets::ColladaGeometry> animatedGeometry;
        Result result = assets::evaluateColladaPose(
            actor.mesh, actor.animation, animationTime, animatedGeometry);
        if (!result) {
            return Result::failure("Could not animate cinematic actor " +
                                   actor.sceneNodeName + ": " +
                                   result.message());
        }
        result = updateDynamicMesh(gpuMesh, animatedGeometry, nullptr);
        if (!result) {
            return Result::failure("Could not update cinematic actor " +
                                   actor.sceneNodeName + ": " +
                                   result.message());
        }
        updateMaterialAnimation(gpuMesh, actor.animation, animationTime);
    }
    return Result::success();
}

Result D3D11Renderer::updateLevelOneEnemies(
    const game::LevelOneBootstrap& levelOne,
    const game::LevelEnemyRuntime& enemies) {
    if (enemies.states().size() != levelOne.enemies().size() ||
        enemyMeshStart_ + enemies.states().size() > gpuMeshes_.size()) {
        return Result::failure("Level-one enemy GPU resources are incomplete");
    }
    for (std::size_t index = 0; index < enemies.states().size(); ++index) {
        const game::LevelEnemyState& enemy = enemies.states()[index];
        if (enemy.asset == nullptr ||
            enemy.asset->archetypeIndex >= levelOne.enemyArchetypes().size()) {
            return Result::failure("Enemy runtime archetype is invalid");
        }
        const game::EnemyArchetypeAsset& archetype =
            levelOne.enemyArchetypes()[enemy.asset->archetypeIndex];
        std::vector<assets::ColladaGeometry> animatedGeometry;
        std::span<const assets::ColladaGeometry> geometry =
            archetype.mesh.sceneGeometries();
        const assets::ColladaAnimationClip* clip =
            archetype.animationBank.findClip(enemy.activeAnimation);
        std::optional<std::uint32_t> animationTimestamp;
        if (clip != nullptr) {
            const std::uint32_t localTime =
                clip->durationMilliseconds() == 0
                    ? 0
                    : enemy.animationLoops
                          ? enemy.animationTimeMilliseconds %
                                clip->durationMilliseconds()
                          : std::min(enemy.animationTimeMilliseconds,
                                     clip->durationMilliseconds());
            animationTimestamp = clip->startMilliseconds + localTime;
            Result result = assets::evaluateColladaPose(
                archetype.mesh, archetype.animationBank,
                *animationTimestamp, animatedGeometry);
            if (!result) {
                return Result::failure("Could not animate enemy " +
                                       enemy.asset->name + ": " +
                                       result.message());
            }
            geometry = animatedGeometry;
        } else if (!archetype.animationBank.clips().empty()) {
            return Result::failure(
                "Enemy " + std::to_string(enemy.asset->objectId) + " (" +
                enemy.asset->gameType + ") runtime animation '" +
                enemy.activeAnimation + "' is missing from " +
                archetype.animationFile);
        }
        GpuMesh& gpuMesh = gpuMeshes_[enemyMeshStart_ + index];
        setMeshVisible(gpuMesh, enemy.visible);
        Result result = updateDynamicMesh(gpuMesh, geometry,
                                          &enemy.worldTransform);
        if (!result) {
            return Result::failure("Could not update enemy " +
                                   enemy.asset->name + ": " +
                                   result.message());
        }
        if (animationTimestamp.has_value()) {
            updateMaterialAnimation(gpuMesh, archetype.animationBank,
                                    *animationTimestamp);
        }
    }
    return Result::success();
}

Result D3D11Renderer::updateLevelOneObjects(
    const game::LevelOneBootstrap& levelOne,
    const game::LevelObjectRuntime& objects) {
    if (objects.states().size() != levelOne.objects().size() ||
        levelObjectMeshStart_ + objects.states().size() > gpuMeshes_.size() ||
        levelObjectMeshStart_ + objects.states().size() !=
            introActorMeshStart_) {
        return Result::failure("Level object GPU resources are incomplete");
    }
    for (std::size_t index = 0; index < objects.states().size(); ++index) {
        const game::LevelObjectState& object = objects.states()[index];
        if (object.asset == nullptr ||
            object.asset->archetypeIndex >= levelOne.objectArchetypes().size()) {
            return Result::failure("Level object runtime archetype is invalid");
        }
        const game::LevelObjectArchetypeAsset& archetype =
            levelOne.objectArchetypes()[object.asset->archetypeIndex];
        std::vector<assets::ColladaGeometry> animatedGeometry;
        std::span<const assets::ColladaGeometry> geometry =
            archetype.mesh.sceneGeometries();
        std::optional<std::uint32_t> animationTimestamp;
        if (!object.activeAnimation.empty()) {
            const assets::ColladaAnimationClip* clip =
                archetype.animationBank.findClip(object.activeAnimation);
            if (clip == nullptr) {
                return Result::failure("Level object animation is missing");
            }
            const std::uint32_t localTime =
                clip->durationMilliseconds() == 0
                    ? 0
                    : object.animationLoops
                          ? object.animationTimeMilliseconds %
                                clip->durationMilliseconds()
                          : std::min(object.animationTimeMilliseconds,
                                     clip->durationMilliseconds());
            Result result = assets::evaluateColladaPose(
                archetype.mesh, archetype.animationBank,
                clip->startMilliseconds + localTime, animatedGeometry);
            if (!result) {
                return Result::failure("Could not animate level object " +
                                       object.asset->name + ": " +
                                       result.message());
            }
            geometry = animatedGeometry;
            animationTimestamp = clip->startMilliseconds + localTime;
        } else if (!archetype.mesh.skins().empty() &&
                   !archetype.animationBank.tracks().empty()) {
            Result result = assets::evaluateColladaPose(
                archetype.mesh, archetype.animationBank, 0,
                animatedGeometry);
            if (!result) {
                return Result::failure(
                    "Could not evaluate level object " +
                    object.asset->name + " bind pose: " + result.message());
            }
            geometry = animatedGeometry;
        }
        GpuMesh& gpuMesh = gpuMeshes_[levelObjectMeshStart_ + index];
        setMeshVisible(gpuMesh, object.visible);
        Result result =
            updateDynamicMesh(gpuMesh, geometry, &object.worldTransform);
        if (!result) {
            return Result::failure("Could not update level object " +
                                   object.asset->name + ": " +
                                   result.message());
        }
        if (animationTimestamp) {
            updateMaterialAnimation(gpuMesh, archetype.animationBank,
                                    *animationTimestamp);
        } else {
            for (DrawBatch& batch : gpuMesh.drawBatches) {
                batch.textureOffset = {};
            }
        }
    }
    return Result::success();
}

Result D3D11Renderer::updateLevelOnePlayer(
    const game::LevelOneBootstrap& levelOne,
    const assets::ColladaAnimationClip& clip,
    std::uint32_t clipTimeMilliseconds,
    const std::array<float, 16>& worldTransform) {
    if (!playerMeshIndex_ || *playerMeshIndex_ >= gpuMeshes_.size()) {
        return Result::failure("Player GPU resources are incomplete");
    }

    const std::uint32_t localTime =
        clip.durationMilliseconds() == 0
            ? 0
            : clipTimeMilliseconds % clip.durationMilliseconds();
    std::vector<assets::ColladaGeometry> animatedGeometry;
    Result result = assets::evaluateColladaPose(
        levelOne.player().mesh, levelOne.player().animationBank,
        clip.startMilliseconds + localTime, animatedGeometry);
    if (!result) {
        return Result::failure("Could not evaluate player clip " + clip.name +
                               ": " + result.message());
    }
    GpuMesh& gpuMesh = gpuMeshes_[*playerMeshIndex_];
    setMeshVisible(gpuMesh, true);
    result = updateDynamicMesh(gpuMesh, animatedGeometry, &worldTransform);
    if (result) {
        updateMaterialAnimation(gpuMesh, levelOne.player().animationBank,
                                clip.startMilliseconds + localTime);
    }
    return !result ? Result::failure("Could not update player clip " +
                                     clip.name + ": " + result.message())
                   : Result::success();
}

Result D3D11Renderer::updatePlayerHitEffects(
    const game::LevelOneBootstrap& levelOne,
    const game::GameplayPlayer& player) {
    constexpr std::size_t hitEffectSlotsPerDefinition = 4;
    const auto& assets = levelOne.playerHitEffects();
    if (playerHitEffectMeshCount_ !=
            assets.size() * hitEffectSlotsPerDefinition ||
        playerHitEffectMeshStart_ + playerHitEffectMeshCount_ >
            gpuMeshes_.size()) {
        return Result::failure(
            "Player hit-effect GPU resources are incomplete");
    }
    for (std::size_t index = 0; index < playerHitEffectMeshCount_; ++index) {
        setMeshVisible(gpuMeshes_[playerHitEffectMeshStart_ + index], false);
    }

    // CAnimObjEffect chooses one of two native color/alpha combiners (0x1d
    // or 0x1e) per spawn. Both treat the source RGB as coverage when the PVR
    // has no alpha; preserve that authored cutout instead of drawing its black
    // texels as opaque geometry.
    std::vector<std::size_t> usedSlots(assets.size());
    for (const game::PlayerHitEffectState& state : player.hitEffects()) {
        if (state.effectId < 0 ||
            static_cast<std::size_t>(state.effectId) >= assets.size()) {
            return Result::failure("Active player hit effect is invalid");
        }
        const std::size_t effectIndex =
            static_cast<std::size_t>(state.effectId);
        const game::PlayerHitEffectAsset& effect = assets[effectIndex];
        if (effect.definition.id != effectIndex ||
            effect.definition.boneName.empty()) {
            return Result::failure(
                "Player hit-effect asset table is inconsistent");
        }
        std::size_t& slot = usedSlots[effectIndex];
        if (slot >= hitEffectSlotsPerDefinition) {
            continue;
        }

        const std::string_view animation = state.followsPlayerBone
                                               ? player.activeAnimation()
                                               : state.spawnAnimation;
        const std::uint32_t animationMilliseconds =
            state.followsPlayerBone
                ? player.animationTimeMilliseconds()
                : state.spawnAnimationMilliseconds;
        const std::array<float, 16>& playerWorld =
            state.followsPlayerBone ? player.worldTransform()
                                   : state.spawnPlayerWorldTransform;
        const assets::ColladaAnimationClip* clip =
            levelOne.player().animationBank.findClip(animation);
        if (clip == nullptr) {
            return Result::failure("Player hit-effect animation is missing: " +
                                   std::string(animation));
        }
        const std::uint32_t localTime =
            clip->durationMilliseconds() == 0
                ? 0
                : animationMilliseconds % clip->durationMilliseconds();
        const std::string_view boneName = state.boneNameOverride.empty()
                                              ? std::string_view(
                                                    effect.definition.boneName)
                                              : state.boneNameOverride;
        std::array<float, 16> boneTransform{};
        Result result = assets::evaluateColladaSceneNodeTransform(
            levelOne.player().mesh, levelOne.player().animationBank,
            clip->startMilliseconds + localTime,
            boneName, boneTransform);
        if (!result) {
            return Result::failure("Could not attach player hit effect " +
                                   effect.definition.name + ": " +
                                   result.message());
        }
        std::array<float, 16> worldTransform =
            resolvePlayerHitEffectWorldTransform(
                playerWorld, boneTransform, state.followsPlayerBone,
                state.driftOffset);
        for (const std::size_t index :
             {0U, 1U, 2U, 4U, 5U, 6U, 8U, 9U, 10U}) {
            worldTransform[index] *= state.uniformScale;
        }
        GpuMesh& gpuMesh =
            gpuMeshes_[playerHitEffectMeshStart_ +
                       effectIndex * hitEffectSlotsPerDefinition + slot++];
        for (DrawBatch& batch : gpuMesh.drawBatches) {
            batch.effectMaterialMode =
                state.subtractAmbientMaterial ? -1 : 1;
        }
        std::vector<assets::ColladaGeometry> animatedGeometry;
        std::span<const assets::ColladaGeometry> geometry =
            effect.mesh.sceneGeometries();
        std::optional<std::uint32_t> effectAnimationTimestamp;
        if (effect.definition.renderingParameter >= 0) {
            const std::size_t effectClipIndex = static_cast<std::size_t>(
                effect.definition.renderingParameter);
            if (effectClipIndex >= effect.animation.clips().size()) {
                return Result::failure(
                    "Player hit effect references a missing animation");
            }
            const assets::ColladaAnimationClip& effectClip =
                effect.animation.clips()[effectClipIndex];
            const std::uint32_t effectLocalTime =
                std::min(state.elapsedMilliseconds,
                         effectClip.durationMilliseconds());
            effectAnimationTimestamp =
                effectClip.startMilliseconds + effectLocalTime;
            result = assets::evaluateColladaPose(
                effect.mesh, effect.animation, *effectAnimationTimestamp,
                animatedGeometry);
            if (!result) {
                return Result::failure("Could not animate player hit effect " +
                                       effect.definition.name + ": " +
                                       result.message());
            }
            geometry = animatedGeometry;
        }
        const float alpha = !state.fadeWithLifetime
            ? 1.0F
            : state.fadeDurationMilliseconds == 0
                  ? 0.0F
                  : 1.0F - static_cast<float>(state.elapsedMilliseconds) /
                               static_cast<float>(state.fadeDurationMilliseconds);
        result = updateDynamicMesh(gpuMesh, geometry, &worldTransform,
                                   std::clamp(alpha, 0.0F, 1.0F));
        if (!result) {
            return Result::failure("Could not update player hit effect " +
                                   effect.definition.name + ": " +
                                   result.message());
        }
        if (effectAnimationTimestamp.has_value()) {
            updateMaterialAnimation(gpuMesh, effect.animation,
                                    *effectAnimationTimestamp);
        }
        setMeshVisible(gpuMesh, true);
    }
    return Result::success();
}

Result D3D11Renderer::updateWebLine(
    bool visible, const assets::Vector3& anchor,
    const assets::Vector3& attachPosition,
    const assets::Vector3* secondAttachPosition,
    const assets::Vector3& orientation) {
    webLineVertexCount_ = 0;
    if (!visible) {
        return Result::success();
    }
    if (!context_ || !webLineVertexBuffer_ || !webLineTexture_) {
        return Result::failure("Web line GPU resources are incomplete");
    }

    std::vector<GpuVertex> vertices;
    vertices.reserve((secondAttachPosition == nullptr ? 1U : 2U) *
                     game::kWebLineMaximumSegments * 6U);
    const auto appendLine = [&vertices, &anchor, &orientation](
                                const assets::Vector3& attach) -> Result {
        game::WebLineGeometry geometry;
        Result result = game::buildWebLineGeometry(
            attach, anchor, orientation, geometry);
        if (!result) {
            return result;
        }
        for (const std::uint16_t index : geometry.indices) {
            const game::WebLineVertex& source = geometry.vertices[index];
            vertices.push_back({
                {source.position.x, source.position.y, source.position.z},
                {0.0F, 0.0F, 0.0F},
                {source.textureU, source.textureV},
                0xffffffffU,
            });
        }
        return Result::success();
    };
    Result result = appendLine(attachPosition);
    if (result && secondAttachPosition != nullptr) {
        result = appendLine(*secondAttachPosition);
    }
    if (!result) {
        return Result::failure("Could not build native web-line ribbon: " +
                               result.message());
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT mapResult = context_->Map(
        webLineVertexBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(mapResult)) {
        return hresultFailure("ID3D11DeviceContext::Map(web line)",
                              mapResult);
    }
    std::memcpy(mapped.pData, vertices.data(),
                vertices.size() * sizeof(GpuVertex));
    context_->Unmap(webLineVertexBuffer_.Get(), 0);
    webLineVertexCount_ = static_cast<std::uint32_t>(vertices.size());
    return Result::success();
}

Result D3D11Renderer::updatePlayerWebPellets(
    const game::LevelOneBootstrap& levelOne,
    std::span<const game::PlayerWebPelletState> pellets) {
    if (webPelletProjectileMeshStart_ + webPelletProjectileMeshCount_ >
            gpuMeshes_.size() ||
        pellets.size() > webPelletProjectileMeshCount_) {
        return Result::failure(
            "Player web pellet GPU resources are incomplete");
    }
    for (std::size_t index = 0; index < webPelletProjectileMeshCount_;
         ++index) {
        setMeshVisible(
            gpuMeshes_[webPelletProjectileMeshStart_ + index], false);
    }
    const game::WebPelletProjectileAsset& projectile =
        levelOne.webPelletProjectile();
    for (std::size_t index = 0; index < pellets.size(); ++index) {
        const game::PlayerWebPelletState& pellet = pellets[index];
        if (!pellet.active) {
            continue;
        }
        const float horizontalLength =
            std::hypot(pellet.velocity.x, pellet.velocity.y);
        const assets::Vector3 facing =
            horizontalLength > std::numeric_limits<float>::epsilon()
                ? assets::Vector3{pellet.velocity.x / horizontalLength,
                                  pellet.velocity.y / horizontalLength,
                                  0.0F}
                : assets::Vector3{1.0F, 0.0F, 0.0F};
        const std::array<float, 16> worldTransform{
            -facing.y, facing.x, 0.0F, 0.0F,
            -facing.x, -facing.y, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            pellet.position.x, pellet.position.y, pellet.position.z, 1.0F};
        GpuMesh& gpuMesh =
            gpuMeshes_[webPelletProjectileMeshStart_ + index];
        setMeshVisible(gpuMesh, true);
        const Result result = updateDynamicMesh(
            gpuMesh, projectile.mesh.sceneGeometries(), &worldTransform);
        if (!result) {
            return Result::failure(
                "Could not update player web pellet: " + result.message());
        }
    }
    return Result::success();
}

Result D3D11Renderer::updateEnemyMolotovs(
    const game::LevelOneBootstrap& levelOne,
    std::span<const game::EnemyMolotovState> molotovs) {
    if (molotovProjectileMeshStart_ + molotovProjectileMeshCount_ >
            gpuMeshes_.size() ||
        molotovs.size() > molotovProjectileMeshCount_) {
        return Result::failure("Molotov projectile GPU resources are incomplete");
    }
    for (std::size_t index = 0; index < molotovProjectileMeshCount_; ++index) {
        setMeshVisible(gpuMeshes_[molotovProjectileMeshStart_ + index], false);
    }
    if (molotovs.empty()) {
        return Result::success();
    }

    const game::MolotovProjectileAsset& projectile =
        levelOne.molotovProjectile();
    for (std::size_t index = 0; index < molotovs.size(); ++index) {
        const game::EnemyMolotovState& state = molotovs[index];
        const std::string_view animation =
            state.phase == game::EnemyMolotovPhase::Flying
                ? std::string_view{"fly"}
                : std::string_view{"explode_ready"};
        const assets::ColladaAnimationClip* clip =
            projectile.animationBank.findClip(animation);
        if (clip == nullptr) {
            return Result::failure("Molotov projectile animation is missing");
        }
        const std::uint32_t localTime =
            clip->durationMilliseconds() == 0
                ? 0
                : state.phase == game::EnemyMolotovPhase::Flying
                      ? state.phaseElapsedMilliseconds %
                            clip->durationMilliseconds()
                      : std::min(state.phaseElapsedMilliseconds,
                                 clip->durationMilliseconds());
        std::vector<assets::ColladaGeometry> geometry;
        Result result = assets::evaluateColladaPose(
            projectile.mesh, projectile.animationBank,
            clip->startMilliseconds + localTime, geometry);
        if (!result) {
            return Result::failure("Could not animate molotov projectile: " +
                                   result.message());
        }
        const std::array<float, 16> worldTransform{
            -state.facing.y,
            state.facing.x,
            0.0F,
            0.0F,
            -state.facing.x,
            -state.facing.y,
            0.0F,
            0.0F,
            0.0F,
            0.0F,
            1.0F,
            0.0F,
            state.position.x,
            state.position.y,
            state.position.z,
            1.0F};
        GpuMesh& gpuMesh =
            gpuMeshes_[molotovProjectileMeshStart_ + index];
        setMeshVisible(gpuMesh, true);
        result = updateDynamicMesh(gpuMesh, geometry, &worldTransform);
        if (!result) {
            return Result::failure("Could not update molotov projectile: " +
                                   result.message());
        }
        updateMaterialAnimation(gpuMesh, projectile.animationBank,
                                clip->startMilliseconds + localTime);
    }
    return Result::success();
}

Result D3D11Renderer::updateEnemyBoomerangs(
    const game::LevelOneBootstrap& levelOne,
    std::span<const game::EnemyBoomerangState> boomerangs) {
    if (boomerangProjectileMeshStart_ +
                boomerangProjectileMeshCount_ >
            gpuMeshes_.size() ||
        boomerangs.size() > boomerangProjectileMeshCount_) {
        return Result::failure(
            "Boomerang projectile GPU resources are incomplete");
    }
    for (std::size_t index = 0;
         index < boomerangProjectileMeshCount_; ++index) {
        setMeshVisible(
            gpuMeshes_[boomerangProjectileMeshStart_ + index], false);
    }
    if (boomerangs.empty()) {
        return Result::success();
    }
    const game::BoomerangProjectileAsset& projectile =
        levelOne.boomerangProjectile();
    const assets::ColladaAnimationClip* animation =
        projectile.animationBank.findClip("weapons");
    if (animation == nullptr) {
        return Result::failure("Boomerang weapons animation is missing");
    }
    for (std::size_t index = 0; index < boomerangs.size(); ++index) {
        const game::EnemyBoomerangState& state = boomerangs[index];
        if (!state.active ||
            state.phase == game::EnemyBoomerangPhase::Ready) {
            continue;
        }
        const std::uint32_t localTime =
            animation->durationMilliseconds() == 0
                ? 0
                : state.phaseElapsedMilliseconds %
                      animation->durationMilliseconds();
        std::vector<assets::ColladaGeometry> geometry;
        Result result = assets::evaluateColladaPose(
            projectile.mesh, projectile.animationBank,
            animation->startMilliseconds + localTime, geometry);
        if (!result) {
            return Result::failure("Could not animate boomerang projectile: " +
                                   result.message());
        }

        assets::Vector3 forward = state.facing;
        const float forwardLength = std::sqrt(
            forward.x * forward.x + forward.y * forward.y +
            forward.z * forward.z);
        if (forwardLength > std::numeric_limits<float>::epsilon()) {
            forward.x /= forwardLength;
            forward.y /= forwardLength;
            forward.z /= forwardLength;
        } else {
            forward = {1.0F, 0.0F, 0.0F};
        }
        assets::Vector3 right{-forward.y, forward.x, 0.0F};
        float rightLength = std::hypot(right.x, right.y);
        if (rightLength <= std::numeric_limits<float>::epsilon()) {
            right = {1.0F, 0.0F, 0.0F};
            rightLength = 1.0F;
        }
        right.x /= rightLength;
        right.y /= rightLength;
        const assets::Vector3 up{
            -right.y * forward.z,
            right.x * forward.z,
            right.y * forward.x - right.x * forward.y};
        const std::array<float, 16> worldTransform{
            right.x,
            right.y,
            right.z,
            0.0F,
            -forward.x,
            -forward.y,
            -forward.z,
            0.0F,
            up.x,
            up.y,
            up.z,
            0.0F,
            state.position.x,
            state.position.y,
            state.position.z,
            1.0F};
        GpuMesh& gpuMesh =
            gpuMeshes_[boomerangProjectileMeshStart_ + index];
        setMeshVisible(gpuMesh, true);
        result = updateDynamicMesh(gpuMesh, geometry, &worldTransform);
        if (!result) {
            return Result::failure("Could not update boomerang projectile: " +
                                   result.message());
        }
        updateMaterialAnimation(
            gpuMesh, projectile.animationBank,
            animation->startMilliseconds + localTime);
    }
    return Result::success();
}

Result D3D11Renderer::updateEnemyElectroEffects(
    const game::LevelOneBootstrap& levelOne,
    const game::LevelEnemyRuntime& enemies) {
    const auto validPool = [this](std::size_t start, std::size_t count) {
        return start + count <= gpuMeshes_.size();
    };
    if (!validPool(thunderclapWaveMeshStart_, thunderclapWaveMeshCount_) ||
        !validPool(thunderclapBeamMeshStart_, thunderclapBeamMeshCount_) ||
        !validPool(electroRotateWaveMeshStart_,
                   electroRotateWaveMeshCount_) ||
        !validPool(electroPostBeamMeshStart_, electroPostBeamMeshCount_) ||
        !validPool(electroBurstWaveMeshStart_, electroBurstWaveMeshCount_) ||
        !validPool(electroBurstBillboardMeshStart_,
                   electroBurstBillboardMeshCount_) ||
        !validPool(enemyLandingShockwaveMeshStart_,
                   enemyLandingShockwaveMeshCount_) ||
        !validPool(enemyLandingCrashWallMeshStart_,
                   enemyLandingCrashWallMeshCount_)) {
        return Result::failure("Electro effect GPU resources are incomplete");
    }
    const auto hidePool = [this](std::size_t start, std::size_t count) {
        for (std::size_t index = 0; index < count; ++index) {
            setMeshVisible(gpuMeshes_[start + index], false);
        }
    };
    hidePool(thunderclapWaveMeshStart_, thunderclapWaveMeshCount_);
    hidePool(thunderclapBeamMeshStart_, thunderclapBeamMeshCount_);
    hidePool(electroRotateWaveMeshStart_, electroRotateWaveMeshCount_);
    hidePool(electroPostBeamMeshStart_, electroPostBeamMeshCount_);
    hidePool(electroBurstWaveMeshStart_, electroBurstWaveMeshCount_);
    hidePool(electroBurstBillboardMeshStart_,
             electroBurstBillboardMeshCount_);
    hidePool(enemyLandingShockwaveMeshStart_,
             enemyLandingShockwaveMeshCount_);
    hidePool(enemyLandingCrashWallMeshStart_,
             enemyLandingCrashWallMeshCount_);

    const game::ElectroEffectAsset& effects = levelOne.electroEffects();
    const auto translationTransform = [](const assets::Vector3& position) {
        return std::array<float, 16>{
            1.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            position.x, position.y, position.z, 1.0F};
    };
    const auto rayTransform = [](const assets::Vector3& position,
                                 assets::Vector3 facing) {
        const float length = std::hypot(facing.x, facing.y);
        if (length > std::numeric_limits<float>::epsilon()) {
            facing.x /= length;
            facing.y /= length;
        } else {
            facing = {1.0F, 0.0F, 0.0F};
        }
        // electro_beam's authored dummy_start1 -> dummy_end1 ray lies on
        // local -Y. This basis maps that axis to the native post facing.
        return std::array<float, 16>{
            -facing.y, facing.x, 0.0F, 0.0F,
            -facing.x, -facing.y, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.0F,
            position.x, position.y, position.z, 1.0F};
    };
    const auto scaledTransform = [](const assets::Vector3& position,
                                    float scale) {
        return std::array<float, 16>{
            scale, 0.0F, 0.0F, 0.0F,
            0.0F, scale, 0.0F, 0.0F,
            0.0F, 0.0F, scale, 0.0F,
            position.x, position.y, position.z, 1.0F};
    };
    const auto updateEffect =
        [this](GpuMesh& gpuMesh,
               const game::ElectroEffectModelAsset& effect,
               std::string_view clipName, std::uint32_t localTime,
               const std::array<float, 16>& worldTransform,
               std::int32_t roomId) -> Result {
            const assets::ColladaAnimationClip* clip =
                effect.animationBank.findClip(clipName);
            if (clip == nullptr) {
                return Result::failure("Electro effect animation is missing");
            }
            const std::uint32_t sampleTime =
                clip->durationMilliseconds() == 0
                    ? 0
                    : localTime % clip->durationMilliseconds();
            std::vector<assets::ColladaGeometry> geometry;
            Result result = assets::evaluateColladaPose(
                effect.mesh, effect.animationBank,
                clip->startMilliseconds + sampleTime, geometry);
            if (!result) {
                return Result::failure("Could not animate Electro effect: " +
                                       result.message());
            }
            result = updateDynamicMesh(gpuMesh, geometry, &worldTransform);
            if (!result) {
                return Result::failure("Could not update Electro effect: " +
                                       result.message());
            }
            gpuMesh.roomId = roomId;
            setMeshVisible(gpuMesh, true);
            updateMaterialAnimation(gpuMesh, effect.animationBank,
                                    clip->startMilliseconds + sampleTime);
            return Result::success();
        };

    std::size_t thunderclapIndex = 0;
    for (const game::EnemyThunderclapState& thunderclap :
         enemies.thunderclaps()) {
        if (!thunderclap.active ||
            thunderclap.phase == game::EnemyThunderclapPhase::Ready) {
            continue;
        }
        if (thunderclapIndex >= thunderclapWaveMeshCount_) {
            return Result::failure(
                "Active Thunderclap count exceeds the native five-object pool");
        }
        const auto world = translationTransform(thunderclap.position);
        Result result = updateEffect(
            gpuMeshes_[thunderclapWaveMeshStart_ + thunderclapIndex],
            effects.wave, "wave", thunderclap.phaseElapsedMilliseconds,
            world, thunderclap.roomId);
        if (!result) {
            return result;
        }
        if (thunderclap.phase != game::EnemyThunderclapPhase::Converging) {
            result = updateEffect(
                gpuMeshes_[thunderclapBeamMeshStart_ + thunderclapIndex],
                effects.beam, "keep",
                thunderclap.phaseElapsedMilliseconds, world,
                thunderclap.roomId);
            if (!result) {
                return result;
            }
        }
        ++thunderclapIndex;
    }

    std::size_t rotateWaveIndex = 0;
    for (const game::LevelEnemyState& enemy : enemies.states()) {
        if (enemy.asset == nullptr ||
            enemy.asset->gameType != "Boss_Electro" ||
            enemy.asset->enemyTypeId != 7 ||
            (enemy.electroTask != game::ElectroBossTaskState::RotateReady &&
             enemy.electroTask != game::ElectroBossTaskState::Rotate)) {
            continue;
        }
        if (rotateWaveIndex >= electroRotateWaveMeshCount_) {
            return Result::failure(
                "Active Electro rotate count exceeds its authored pool");
        }
        const assets::Vector3 center{
            enemy.position.x, enemy.position.y,
            enemy.position.z + enemy.collisionHeight * 0.4F};
        Result result = updateEffect(
            gpuMeshes_[electroRotateWaveMeshStart_ + rotateWaveIndex],
            effects.wave, "wave", enemy.electroTaskElapsedMilliseconds,
            translationTransform(center), enemy.asset->roomId);
        if (!result) {
            return result;
        }
        ++rotateWaveIndex;
    }

    std::size_t postIndex = 0;
    for (const game::EnemyElectricPostState& post : enemies.electricPosts()) {
        if (!post.active) {
            continue;
        }
        if (postIndex >= electroPostBeamMeshCount_) {
            return Result::failure(
                "Active ElectricPost count exceeds its authored pool");
        }
        Result result = updateEffect(
            gpuMeshes_[electroPostBeamMeshStart_ + postIndex], effects.beam,
            "keep", post.animationTimeMilliseconds,
            rayTransform(post.position, post.facing), post.roomId);
        if (!result) {
            return result;
        }
        ++postIndex;
    }
    std::size_t burstIndex = 0;
    for (const game::EnemyElectroBurstState& burst :
         enemies.electroBursts()) {
        if (!burst.active) {
            continue;
        }
        if (burstIndex >= electroBurstWaveMeshCount_ ||
            burstIndex >= electroBurstBillboardMeshCount_) {
            return Result::failure(
                "Active Electro burst count exceeds its effect pool");
        }
        const auto world = scaledTransform(burst.position, burst.scale);
        Result result = updateEffect(
            gpuMeshes_[electroBurstWaveMeshStart_ + burstIndex],
            effects.wave, "wave", burst.elapsedMilliseconds, world,
            burst.roomId);
        if (!result) {
            return result;
        }
        result = updateEffect(
            gpuMeshes_[electroBurstBillboardMeshStart_ + burstIndex],
            effects.waveBillboard, "wave", burst.elapsedMilliseconds, world,
            burst.roomId);
        if (!result) {
            return result;
        }
        ++burstIndex;
    }

    std::size_t shockwaveIndex = 0;
    std::size_t crashWallIndex = 0;
    const game::EnemyLandingEffectAsset& landingAssets =
        levelOne.enemyLandingEffects();
    for (const game::EnemyLandingAnimatedEffectState& state :
         enemies.landingAnimatedEffects()) {
        if (!state.active) {
            continue;
        }
        const game::ElectroEffectModelAsset* asset = nullptr;
        GpuMesh* gpuMesh = nullptr;
        if (state.kind ==
            game::EnemyLandingAnimatedEffectKind::Shockwave) {
            if (shockwaveIndex >= enemyLandingShockwaveMeshCount_) {
                return Result::failure(
                    "Active landing shockwave count exceeds its effect pool");
            }
            asset = &landingAssets.shockwave;
            gpuMesh = &gpuMeshes_[enemyLandingShockwaveMeshStart_ +
                                  shockwaveIndex++];
        } else {
            if (crashWallIndex >= enemyLandingCrashWallMeshCount_) {
                return Result::failure(
                    "Active landing crash-wall count exceeds its effect pool");
            }
            asset = &landingAssets.crashWall;
            gpuMesh = &gpuMeshes_[enemyLandingCrashWallMeshStart_ +
                                  crashWallIndex++];
        }

        // rotationFromTo((0,-1,0),(0,0,-1)) at 0x003b8d98 produces a
        // +90-degree X rotation for both models before their authored scale.
        const float scale = state.scale;
        const std::array<float, 16> world{
            scale, 0.0F, 0.0F, 0.0F,
            0.0F, 0.0F, scale, 0.0F,
            0.0F, -scale, 0.0F, 0.0F,
            state.position.x, state.position.y, state.position.z, 1.0F};
        const float alpha = state.lifetimeMilliseconds == 0
            ? 0.0F
            : 1.0F -
                  static_cast<float>(state.elapsedMilliseconds) /
                      static_cast<float>(state.lifetimeMilliseconds);
        Result result = updateDynamicMesh(
            *gpuMesh, asset->mesh.sceneGeometries(), &world,
            std::clamp(alpha, 0.0F, 1.0F));
        if (!result) {
            return Result::failure(
                "Could not update enemy landing effect: " +
                result.message());
        }
        for (DrawBatch& batch : gpuMesh->drawBatches) {
            batch.effectMaterialMode =
                state.subtractAmbientMaterial ? -1 : 1;
        }
        gpuMesh->roomId = state.roomId;
        setMeshVisible(*gpuMesh, true);
        updateMaterialAnimation(*gpuMesh, asset->animationBank,
                                state.elapsedMilliseconds);
    }
    return Result::success();
}

Result D3D11Renderer::updateEnemyGunLines(
    std::span<const game::EnemyGunLineState> gunLines) {
    enemyGunLineVertexCount_ = 0;
    if (gunLines.empty()) {
        return Result::success();
    }
    if (!context_ || !enemyGunLineVertexBuffer_) {
        return Result::failure("Enemy gun-line buffer is unavailable");
    }
    constexpr std::size_t kMaximumGunLines = 32;
    constexpr float kTracerLengthCentimeters = 300.0F;
    constexpr std::uint32_t kGunLineColor = 0xf0ffd060U;
    std::vector<GpuVertex> vertices;
    vertices.reserve(std::min(gunLines.size(), kMaximumGunLines) * 2U);
    for (const game::EnemyGunLineState& line :
         gunLines.first(std::min(gunLines.size(), kMaximumGunLines))) {
        if (!line.active) {
            continue;
        }
        const float tracerLength = std::clamp(
            static_cast<float>(line.ageMilliseconds) * 1.5F, 3.0F,
            kTracerLengthCentimeters);
        const assets::Vector3 tail{
            line.position.x - line.direction.x * tracerLength,
            line.position.y - line.direction.y * tracerLength,
            line.position.z - line.direction.z * tracerLength};
        vertices.push_back({{tail.x, tail.y, tail.z},
                            {0.0F, 0.0F, 1.0F},
                            {},
                            kGunLineColor});
        vertices.push_back({{line.position.x, line.position.y, line.position.z},
                            {0.0F, 0.0F, 1.0F},
                            {},
                            kGunLineColor});
    }
    if (vertices.empty()) {
        return Result::success();
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT mapResult = context_->Map(enemyGunLineVertexBuffer_.Get(), 0,
                                            D3D11_MAP_WRITE_DISCARD, 0,
                                            &mapped);
    if (FAILED(mapResult)) {
        return hresultFailure("ID3D11DeviceContext::Map(enemy gun lines)",
                              mapResult);
    }
    std::memcpy(mapped.pData, vertices.data(),
                vertices.size() * sizeof(GpuVertex));
    context_->Unmap(enemyGunLineVertexBuffer_.Get(), 0);
    enemyGunLineVertexCount_ = static_cast<std::uint32_t>(vertices.size());
    return Result::success();
}

Result D3D11Renderer::updateLevelOneEffects(
    const game::LevelEffectAsset& assets,
    const game::LevelEffectRuntime& effects,
    const game::LevelBonusRuntime& bonuses) {
    effectAlphaVertexCount_ = 0;
    effectAdditiveVertexCount_ = 0;
    if (!device_ || !context_ || !effectTexture_) {
        return Result::failure("Effect GPU resources are incomplete");
    }
    const assets::SpriteAtlas& atlas = assets.atlas;
    const assets::RgbaImage& texture = assets.texture.image();
    if (texture.width < 2 || texture.height < 2) {
        return Result::failure("Effect atlas dimensions are invalid");
    }

    std::vector<GpuVertex> alphaVertices;
    std::vector<GpuVertex> additiveVertices;
    const auto appendParticle = [&](const game::EffectParticleState& particle,
                                    std::vector<GpuVertex>& vertices,
                                    bool& valid) {
        const auto modules = atlas.modulesForFrame(
            static_cast<std::size_t>(particle.frameId));
        if (particle.frameId < 0 || modules.empty()) {
            valid = false;
            return;
        }
        const float radians =
            particle.rotationDegrees * 0.017453292519943295F;
        const float cosine = std::cos(radians);
        const float sine = std::sin(radians);
        const auto point = [&](float localX, float localY) {
            const float rotatedX = localX * cosine - localY * sine;
            const float rotatedY = localX * sine + localY * cosine;
            return DirectX::XMFLOAT3{
                particle.position.x + cameraRight_.x * rotatedX +
                    cameraUp_.x * rotatedY,
                particle.position.y + cameraRight_.y * rotatedX +
                    cameraUp_.y * rotatedY,
                particle.position.z + cameraRight_.z * rotatedX +
                    cameraUp_.z * rotatedY};
        };
        for (const assets::SpriteFrameModule& frameModule : modules) {
            if (frameModule.moduleIndex >= atlas.modules().size()) {
                valid = false;
                continue;
            }
            const assets::SpriteModule& module =
                atlas.modules()[frameModule.moduleIndex];
            constexpr std::uint8_t horizontalFlip = 0x01;
            constexpr std::uint8_t verticalFlip = 0x02;
            if (module.imageIndex != 0 ||
                (frameModule.flags & ~(horizontalFlip | verticalFlip)) != 0) {
                valid = false;
                continue;
            }
            float u0 = static_cast<float>(module.x) /
                       static_cast<float>(texture.width);
            float v0 = static_cast<float>(module.y) /
                       static_cast<float>(texture.height);
            float u1 = static_cast<float>(module.x + module.width) /
                       static_cast<float>(texture.width);
            float v1 = static_cast<float>(module.y + module.height) /
                       static_cast<float>(texture.height);
            if ((frameModule.flags & horizontalFlip) != 0) {
                std::swap(u0, u1);
            }
            if ((frameModule.flags & verticalFlip) != 0) {
                std::swap(v0, v1);
            }
            const float halfWidth = particle.width * 0.5F;
            const float halfHeight = particle.height * 0.5F;
            const std::uint32_t color = rgbaVertexColor(particle.color);
            const GpuVertex topLeft{point(-halfWidth, halfHeight), {},
                                    {u0, v0}, color};
            const GpuVertex topRight{point(halfWidth, halfHeight), {},
                                     {u1, v0}, color};
            const GpuVertex bottomLeft{point(-halfWidth, -halfHeight), {},
                                       {u0, v1}, color};
            const GpuVertex bottomRight{point(halfWidth, -halfHeight), {},
                                        {u1, v1}, color};
            vertices.insert(vertices.end(),
                            {topLeft, topRight, bottomLeft, topRight,
                             bottomRight, bottomLeft});
        }
    };

    bool valid = true;
    for (const game::EffectParticleState& particle : effects.particles()) {
        if (particle.roomId >= 1 &&
            particle.roomId <=
                static_cast<std::int32_t>(roomVisibility_.size()) &&
            !roomVisibility_[static_cast<std::size_t>(particle.roomId - 1)]) {
            continue;
        }
        appendParticle(particle,
                       particle.additive ? additiveVertices : alphaVertices,
                       valid);
    }
    const auto frameModule = [&](std::int32_t frameId,
                                 assets::SpriteModule& module) {
        const auto modules =
            atlas.modulesForFrame(static_cast<std::size_t>(frameId));
        if (frameId < 0 || modules.size() != 1 ||
            modules.front().moduleIndex >= atlas.modules().size() ||
            modules.front().flags != 0) {
            return false;
        }
        module = atlas.modules()[modules.front().moduleIndex];
        return module.imageIndex == 0;
    };
    const auto textureCoordinates = [&](const assets::SpriteModule& module) {
        return std::array<float, 4>{
            static_cast<float>(module.x) /
                static_cast<float>(texture.width),
            static_cast<float>(module.y) /
                static_cast<float>(texture.height),
            static_cast<float>(module.x + module.width) /
                static_cast<float>(texture.width),
            static_cast<float>(module.y + module.height) /
                static_cast<float>(texture.height)};
    };
    const auto appendOrbHead = [&](const game::LevelBonusOrbRenderState& orb,
                                   const assets::SpriteModule& module) {
        const auto uv = textureCoordinates(module);
        const float half = orb.headHalfWidth;
        const auto point = [&](float right, float up) {
            return DirectX::XMFLOAT3{
                orb.headPosition.x + cameraRight_.x * right + cameraUp_.x * up,
                orb.headPosition.y + cameraRight_.y * right + cameraUp_.y * up,
                orb.headPosition.z + cameraRight_.z * right + cameraUp_.z * up};
        };
        constexpr std::uint32_t white = 0xffffffffU;
        const GpuVertex topLeft{point(-half, half), {}, {uv[0], uv[1]}, white};
        const GpuVertex topRight{point(half, half), {}, {uv[2], uv[1]}, white};
        const GpuVertex bottomLeft{point(-half, -half), {}, {uv[0], uv[3]},
                                   white};
        const GpuVertex bottomRight{point(half, -half), {}, {uv[2], uv[3]},
                                    white};
        additiveVertices.insert(additiveVertices.end(),
                                {topLeft, topRight, bottomLeft, topRight,
                                 bottomRight, bottomLeft});
    };
    const auto appendOrbRibbon = [&](const game::LevelBonusOrbRenderState& orb,
                                     const assets::SpriteModule& module) {
        const auto uv = textureCoordinates(module);
        const std::uint32_t color = rgbaVertexColor(
            orb.type == game::LevelBonusType::SkillPoint ? 0x64ff0000U
                                                          : 0x6400ff00U);
        for (std::size_t index = 0;
             index + 1 < orb.trailPositions.size(); ++index) {
            const assets::Vector3& first = orb.trailPositions[index];
            const assets::Vector3& second = orb.trailPositions[index + 1];
            const DirectX::XMVECTOR tangent = DirectX::XMVectorSet(
                second.x - first.x, second.y - first.y,
                second.z - first.z, 0.0F);
            const DirectX::XMVECTOR toCamera = DirectX::XMVectorSet(
                cameraPosition_.x - first.x,
                cameraPosition_.y - first.y,
                cameraPosition_.z - first.z, 0.0F);
            DirectX::XMVECTOR widthDirection =
                DirectX::XMVector3Cross(tangent, toCamera);
            if (DirectX::XMVectorGetX(
                    DirectX::XMVector3LengthSq(widthDirection)) < 0.0001F) {
                widthDirection = DirectX::XMVectorSet(
                    cameraRight_.x, cameraRight_.y, cameraRight_.z, 0.0F);
            } else {
                widthDirection = DirectX::XMVector3Normalize(widthDirection);
            }
            const float widthX =
                DirectX::XMVectorGetX(widthDirection) * orb.trailHalfWidth;
            const float widthY =
                DirectX::XMVectorGetY(widthDirection) * orb.trailHalfWidth;
            const float widthZ =
                DirectX::XMVectorGetZ(widthDirection) * orb.trailHalfWidth;
            const DirectX::XMFLOAT3 firstLeft{first.x - widthX,
                                               first.y - widthY,
                                               first.z - widthZ};
            const DirectX::XMFLOAT3 firstRight{first.x + widthX,
                                                first.y + widthY,
                                                first.z + widthZ};
            const DirectX::XMFLOAT3 secondLeft{second.x - widthX,
                                                second.y - widthY,
                                                second.z - widthZ};
            const DirectX::XMFLOAT3 secondRight{second.x + widthX,
                                                 second.y + widthY,
                                                 second.z + widthZ};
            const float firstFraction =
                static_cast<float>(index) /
                static_cast<float>(orb.trailPositions.size() - 1);
            const float secondFraction =
                static_cast<float>(index + 1) /
                static_cast<float>(orb.trailPositions.size() - 1);
            const float firstV = uv[3] + (uv[1] - uv[3]) * firstFraction;
            const float secondV = uv[3] + (uv[1] - uv[3]) * secondFraction;
            additiveVertices.insert(
                additiveVertices.end(),
                {GpuVertex{firstLeft, {}, {uv[0], firstV}, color},
                 GpuVertex{firstRight, {}, {uv[2], firstV}, color},
                 GpuVertex{secondLeft, {}, {uv[0], secondV}, color},
                 GpuVertex{firstRight, {}, {uv[2], firstV}, color},
                 GpuVertex{secondRight, {}, {uv[2], secondV}, color},
                 GpuVertex{secondLeft, {}, {uv[0], secondV}, color}});
        }
    };
    for (const game::LevelBonusOrbRenderState& orb : bonuses.orbs()) {
        if (orb.roomId >= 1 &&
            orb.roomId <= static_cast<std::int32_t>(roomVisibility_.size()) &&
            !roomVisibility_[static_cast<std::size_t>(orb.roomId - 1)]) {
            continue;
        }
        assets::SpriteModule trailModule;
        assets::SpriteModule headModule;
        const std::int32_t headFrame =
            orb.type == game::LevelBonusType::SkillPoint ? 14 : 15;
        if (!frameModule(5, trailModule) ||
            !frameModule(headFrame, headModule)) {
            valid = false;
            continue;
        }
        appendOrbRibbon(orb, trailModule);
        appendOrbHead(orb, headModule);
    }
    if (!valid) {
        return Result::failure(
            "Effect particle references an unsupported sprite frame");
    }
    const std::size_t totalVertexCount =
        alphaVertices.size() + additiveVertices.size();
    if (totalVertexCount > std::numeric_limits<std::uint32_t>::max()) {
        return Result::failure("Effect vertex count exceeds D3D11 limits");
    }
    if (totalVertexCount > effectVertexCapacity_) {
        effectVertexBuffer_.Reset();
        effectVertexCapacity_ = static_cast<std::uint32_t>(
            std::max<std::size_t>(totalVertexCount, 256));
        D3D11_BUFFER_DESC description{};
        description.ByteWidth =
            effectVertexCapacity_ * static_cast<UINT>(sizeof(GpuVertex));
        description.Usage = D3D11_USAGE_DYNAMIC;
        description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        const HRESULT createResult = device_->CreateBuffer(
            &description, nullptr, &effectVertexBuffer_);
        if (FAILED(createResult)) {
            effectVertexCapacity_ = 0;
            return hresultFailure("ID3D11Device::CreateBuffer(effects)",
                                  createResult);
        }
    }
    effectAlphaVertexCount_ =
        static_cast<std::uint32_t>(alphaVertices.size());
    effectAdditiveVertexCount_ =
        static_cast<std::uint32_t>(additiveVertices.size());
    if (totalVertexCount == 0) {
        return Result::success();
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT mapResult = context_->Map(
        effectVertexBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(mapResult)) {
        return hresultFailure("ID3D11DeviceContext::Map(effects)", mapResult);
    }
    auto* destination = static_cast<GpuVertex*>(mapped.pData);
    std::copy(alphaVertices.begin(), alphaVertices.end(), destination);
    std::copy(additiveVertices.begin(), additiveVertices.end(),
              destination + alphaVertices.size());
    context_->Unmap(effectVertexBuffer_.Get(), 0);
    return Result::success();
}

Result D3D11Renderer::updateLevelOneHints(
    const game::LevelHintRuntime& hints) {
    hintVertexCount_ = 0;
    if (hints.states().empty()) {
        return Result::success();
    }
    if (!device_ || !context_ || !hintTexture_) {
        return Result::failure("Hint GPU resources are incomplete");
    }

    std::vector<GpuVertex> vertices;
    bool valid = true;
    for (const game::LevelHintState& state : hints.states()) {
        if (!state.visible || state.asset == nullptr) {
            continue;
        }
        const game::LevelHintAsset& hint = *state.asset;
        if (hint.roomId >= 1 &&
            hint.roomId <= static_cast<std::int32_t>(roomVisibility_.size()) &&
            !roomVisibility_[static_cast<std::size_t>(hint.roomId - 1)]) {
            continue;
        }
        const assets::SpriteAtlas& atlas = hint.atlas;
        const assets::RgbaImage& texture = hint.texture.image();
        if (state.animationFrameIndex < 0 || state.frameIndex < 0 ||
            static_cast<std::size_t>(state.animationFrameIndex) >=
                atlas.animationFrames().size() ||
            texture.width < 2 || texture.height < 2) {
            valid = false;
            continue;
        }
        const assets::SpriteAnimationFrame& animationFrame =
            atlas.animationFrames()[
                static_cast<std::size_t>(state.animationFrameIndex)];
        const auto modules = atlas.modulesForFrame(
            static_cast<std::size_t>(state.frameIndex));
        if (modules.empty()) {
            valid = false;
            continue;
        }
        const auto point = [&state, this](float right, float up) {
            return DirectX::XMFLOAT3{
                state.position.x + cameraRight_.x * right + cameraUp_.x * up,
                state.position.y + cameraRight_.y * right + cameraUp_.y * up,
                state.position.z + cameraRight_.z * right + cameraUp_.z * up};
        };
        for (const assets::SpriteFrameModule& frameModule : modules) {
            if (frameModule.moduleIndex >= atlas.modules().size()) {
                valid = false;
                continue;
            }
            const assets::SpriteModule& module =
                atlas.modules()[frameModule.moduleIndex];
            constexpr std::uint8_t horizontalFlip = 0x01;
            constexpr std::uint8_t verticalFlip = 0x02;
            const std::uint8_t flags =
                frameModule.flags ^ animationFrame.flags;
            if (module.imageIndex != 0 ||
                (flags & ~(horizontalFlip | verticalFlip)) != 0) {
                valid = false;
                continue;
            }
            float u0 = static_cast<float>(module.x) /
                       static_cast<float>(texture.width);
            float v0 = static_cast<float>(module.y) /
                       static_cast<float>(texture.height);
            float u1 = static_cast<float>(module.x + module.width) /
                       static_cast<float>(texture.width);
            float v1 = static_cast<float>(module.y + module.height) /
                       static_cast<float>(texture.height);
            if ((flags & horizontalFlip) != 0) {
                std::swap(u0, u1);
            }
            if ((flags & verticalFlip) != 0) {
                std::swap(v0, v1);
            }

            const float left = static_cast<float>(animationFrame.x +
                                                  frameModule.x);
            const float top = -static_cast<float>(animationFrame.y +
                                                  frameModule.y);
            const float right = left + static_cast<float>(module.width);
            const float bottom = top - static_cast<float>(module.height);
            constexpr std::uint32_t white = 0xffffffffU;
            const GpuVertex topLeft{point(left, top), {}, {u0, v0}, white};
            const GpuVertex topRight{point(right, top), {}, {u1, v0}, white};
            const GpuVertex bottomLeft{point(left, bottom), {}, {u0, v1},
                                       white};
            const GpuVertex bottomRight{point(right, bottom), {}, {u1, v1},
                                        white};
            vertices.insert(vertices.end(),
                            {topLeft, topRight, bottomLeft, topRight,
                             bottomRight, bottomLeft});
        }
    }
    if (!valid) {
        return Result::failure(
            "Hint references an unsupported sprite animation frame");
    }
    if (vertices.size() > std::numeric_limits<std::uint32_t>::max()) {
        return Result::failure("Hint vertex count exceeds D3D11 limits");
    }
    if (vertices.size() > hintVertexCapacity_) {
        hintVertexBuffer_.Reset();
        hintVertexCapacity_ = static_cast<std::uint32_t>(
            std::max<std::size_t>(vertices.size(), 32));
        D3D11_BUFFER_DESC description{};
        description.ByteWidth =
            hintVertexCapacity_ * static_cast<UINT>(sizeof(GpuVertex));
        description.Usage = D3D11_USAGE_DYNAMIC;
        description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        const HRESULT createResult = device_->CreateBuffer(
            &description, nullptr, &hintVertexBuffer_);
        if (FAILED(createResult)) {
            hintVertexCapacity_ = 0;
            return hresultFailure("ID3D11Device::CreateBuffer(hints)",
                                  createResult);
        }
    }
    hintVertexCount_ = static_cast<std::uint32_t>(vertices.size());
    if (vertices.empty()) {
        return Result::success();
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT mapResult = context_->Map(
        hintVertexBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(mapResult)) {
        return hresultFailure("ID3D11DeviceContext::Map(hints)", mapResult);
    }
    std::memcpy(mapped.pData, vertices.data(),
                vertices.size() * sizeof(GpuVertex));
    context_->Unmap(hintVertexBuffer_.Get(), 0);
    return Result::success();
}

Result D3D11Renderer::updateDynamicMesh(
    GpuMesh& gpuMesh,
    std::span<const assets::ColladaGeometry> animatedGeometry,
    const std::array<float, 16>* worldTransform,
    float vertexAlphaScale) {
    if (!gpuMesh.dynamicVertices) {
        return Result::failure("Vertex buffer is not dynamic");
    }
    std::size_t vertexCount = 0;
    for (const assets::ColladaGeometry& geometry : animatedGeometry) {
        vertexCount += geometry.vertices.size();
    }
    if (vertexCount != gpuMesh.vertexCount) {
        return Result::failure("Animated vertex count changed");
    }

    DirectX::XMMATRIX meshTransform = DirectX::XMMatrixIdentity();
    if (worldTransform != nullptr) {
        DirectX::XMFLOAT4X4 worldStorage;
        std::copy(worldTransform->begin(), worldTransform->end(),
                  &worldStorage.m[0][0]);
        meshTransform = DirectX::XMLoadFloat4x4(&worldStorage);
    }
    DirectX::XMVECTOR determinant;
    const DirectX::XMMATRIX normalTransform = DirectX::XMMatrixTranspose(
        DirectX::XMMatrixInverse(&determinant, meshTransform));

    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT mapResult = context_->Map(
        gpuMesh.vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(mapResult)) {
        return hresultFailure("ID3D11DeviceContext::Map(animated mesh)",
                              mapResult);
    }
    auto* destination = static_cast<GpuVertex*>(mapped.pData);
    for (const assets::ColladaGeometry& geometry : animatedGeometry) {
        for (const assets::ColladaVertex& source : geometry.vertices) {
            DirectX::XMVECTOR position = DirectX::XMVectorSet(
                source.position.x, source.position.y, source.position.z, 1.0F);
            DirectX::XMVECTOR normal = DirectX::XMVectorSet(
                source.normal.x, source.normal.y, source.normal.z, 0.0F);
            position =
                DirectX::XMVector3TransformCoord(position, meshTransform);
            normal = DirectX::XMVector3Normalize(
                DirectX::XMVector3TransformNormal(normal, normalTransform));
            std::uint32_t color = source.color;
            const std::uint32_t sourceAlpha = color >> 24U;
            const auto scaledAlpha = static_cast<std::uint32_t>(std::clamp(
                std::lround(static_cast<float>(sourceAlpha) *
                            vertexAlphaScale),
                0L, 255L));
            color = (color & 0x00ffffffU) | (scaledAlpha << 24U);
            *destination++ = {
                {DirectX::XMVectorGetX(position), DirectX::XMVectorGetY(position),
                 DirectX::XMVectorGetZ(position)},
                {DirectX::XMVectorGetX(normal), DirectX::XMVectorGetY(normal),
                 DirectX::XMVectorGetZ(normal)},
                {source.textureCoordinate[0], source.textureCoordinate[1]},
                rgbaVertexColor(color),
            };
        }
    }
    context_->Unmap(gpuMesh.vertexBuffer.Get(), 0);
    return Result::success();
}

void D3D11Renderer::updateMaterialAnimation(
    GpuMesh& gpuMesh, const assets::ColladaAnimationFile& animation,
    std::uint32_t timestampMilliseconds) noexcept {
    for (DrawBatch& batch : gpuMesh.drawBatches) {
        batch.textureOffset = {};
    }
    for (const assets::ColladaAnimationTrack& track : animation.tracks()) {
        const bool offsetU =
            track.property ==
            assets::ColladaAnimationProperty::TextureOffsetU;
        const bool offsetV =
            track.property ==
            assets::ColladaAnimationProperty::TextureOffsetV;
        if (!offsetU && !offsetV) {
            continue;
        }
        const assets::ColladaAnimationSample sample =
            track.sample(timestampMilliseconds);
        if (sample.componentCount != 1) {
            continue;
        }
        for (DrawBatch& batch : gpuMesh.drawBatches) {
            if (batch.materialAnimationTarget != track.targetNode) {
                continue;
            }
            batch.textureOffset[offsetV ? 1U : 0U] = sample.value[0];
        }
    }
}

Result D3D11Renderer::uploadHudTexture(const game::LevelHudAsset& hud) {
    hudTexture_.Reset();
    hudVertexBuffer_.Reset();
    hudVertexCount_ = 0;
    hudVertexCapacity_ = 0;
    deathConfirmationVertexBuffer_.Reset();
    deathConfirmationBackgroundTexture_.Reset();
    deathConfirmationMainMenuTexture_.Reset();
    deathConfirmationNormalFontTexture_.Reset();
    deathConfirmationOutlineFontTexture_.Reset();
    deathConfirmationOutlineBigFontTexture_.Reset();
    cinematicUiTutorialTexture_.Reset();
    transportTexture_.Reset();
    transportSpriteVertexBuffer_.Reset();
    transportColorVertexBuffer_.Reset();
    transportSpriteVertexCount_ = 0;
    transportColorVertexCount_ = 0;
    cinematicUiControllerTexture_.Reset();
    cinematicUiControllerImage_ = {};
    cinematicUiMessagePanelVertexBuffer_.Reset();
    cinematicUiMessageIconVertexBuffer_.Reset();
    cinematicUiControllerIconVertexBuffer_.Reset();
    cinematicUiMessageTextVertexBuffer_.Reset();
    cinematicUiMessagePanelVertexCount_ = 0;
    cinematicUiMessageIconVertexCount_ = 0;
    cinematicUiControllerIconVertexCount_ = 0;
    cinematicUiMessageTextVertexCount_ = 0;
    cinematicUiMessageText_.clear();
    cinematicUiMessageFace_ = -1;
    cinematicUiMessagePage_ = -1;
    cinematicUiTutorialPanel_ = false;
    cinematicUiInformationPanel_ = game::InformationPanel::None;
    cinematicUiTutorialButton_ = -1;
    deathConfirmationBatches_.clear();
    deathConfirmationVertexCount_ = 0;
    deathConfirmationVertexCapacity_ = 0;
    const assets::RgbaImage& image = hud.interfaceTexture.image();
    if (image.width == 0 || image.height == 0 ||
        image.pixels.size() !=
            static_cast<std::size_t>(image.width) * image.height * 4) {
        return Result::failure("Interface texture image is invalid");
    }
    Result result = createTextureView(
        std::span<const assets::RgbaImage>(&image, 1), hudTexture_);
    if (!result) {
        return result;
    }
    const auto uploadConfirmationTexture = [this](
        const assets::RgbaImage& source,
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& destination,
        std::string_view name) -> Result {
        if (source.width == 0 || source.height == 0 ||
            source.pixels.size() !=
                static_cast<std::size_t>(source.width) * source.height * 4U) {
            return Result::failure(std::string(name) +
                                   " texture image is invalid");
        }
        return createTextureView({&source, 1}, destination);
    };
    result = uploadConfirmationTexture(
        hud.backgroundSuitTexture.image(),
        deathConfirmationBackgroundTexture_, "Background suit");
    if (!result) {
        return result;
    }
    result = uploadConfirmationTexture(
        hud.mainMenuTexture.image(), deathConfirmationMainMenuTexture_,
        "Main menu");
    if (!result) {
        return result;
    }
    result = uploadConfirmationTexture(
        hud.normalWhiteFontTexture.image(),
        deathConfirmationNormalFontTexture_, "Normal white font");
    if (!result) {
        return result;
    }
    result = uploadConfirmationTexture(
        hud.tutorialTexture.image(), cinematicUiTutorialTexture_,
        "Tutorial");
    if (!result) {
        return result;
    }
    result = uploadConfirmationTexture(
        hud.transportTexture.image(), transportTexture_, "Transport");
    if (!result) {
        return result;
    }
    result = uploadConfirmationTexture(
        hud.outlineSmallFontTexture.image(),
        deathConfirmationOutlineFontTexture_, "Outline small font");
    if (!result) {
        return result;
    }
    result = uploadConfirmationTexture(
        hud.outlineBigFontTexture.image(),
        deathConfirmationOutlineBigFontTexture_, "Outline big font");
    if (!result) {
        return result;
    }
    const Result atlasResult = loadXboxButtonAtlas(
        cinematicUiControllerImage_);
    if (!atlasResult) {
        std::clog << "Xbox button atlas unavailable; using bracket labels: "
                  << atlasResult.message() << '\n';
        cinematicUiControllerImage_ = {};
        return Result::success();
    }
    result = createTextureView(
        {&cinematicUiControllerImage_, 1}, cinematicUiControllerTexture_);
    if (!result) {
        std::clog << "Xbox button atlas upload failed; using bracket labels: "
                  << result.message() << '\n';
        cinematicUiControllerImage_ = {};
        cinematicUiControllerTexture_.Reset();
    }
    return Result::success();
}

Result D3D11Renderer::updatePlayerHud(const game::LevelHudAsset& hud,
                                      float currentHealthRatio,
                                      float delayedHealthRatio,
                                      float webPowerRatio,
                                      const game::LevelEnemyState*
                                          shownHealthBarEnemy,
                                       std::int32_t skillPoints,
                                       bool showSkillPointTotal,
                                       const game::LevelBonusPopupState*
                                           skillPointPopup,
                                       bool visible,
                                       bool bossProgressVisible,
                                       float bossProgressRatio) {
    if (!device_ || !context_ || !hudTexture_ || width_ == 0 || height_ == 0) {
        return Result::failure("HUD GPU resources are incomplete");
    }
    if (!visible) {
        // AttributionEnable feeds CLevel+0x2d. The original
        // CLevel::Render2DInterface skips the health/web/enemy HUD, combo,
        // bonus total, and objective arrow while this flag is false.
        hudVertexCount_ = 0;
        return Result::success();
    }

    const assets::SpriteAtlas& atlas = hud.interfaceAtlas;
    const assets::RgbaImage& texture = hud.interfaceTexture.image();
    constexpr float virtualWidth = 480.0F;
    constexpr float virtualHeight = 320.0F;
    // CreateAllItems_3x2 (0x002eed48) authors UI items 0x14 and 0x15 at
    // (46, 32) and (435, 32) on the original 480x320 canvas.
    constexpr float playerHudX = 46.0F;
    constexpr float playerHudY = 32.0F;
    constexpr float enemyHudX = 435.0F;
    constexpr float enemyHudY = 32.0F;
    const float screenScale =
        std::min(static_cast<float>(width_) / virtualWidth,
                 static_cast<float>(height_) / virtualHeight);
    const float screenOffsetX =
        (static_cast<float>(width_) - virtualWidth * screenScale) * 0.5F;
    const float screenOffsetY =
        (static_cast<float>(height_) - virtualHeight * screenScale) * 0.5F;

    std::vector<GpuVertex> vertices;
    vertices.reserve(48);
    bool valid = true;
    const auto appendFrame = [&](std::size_t frameIndex, float originX,
                                 float originY, float fillRatio,
                                 float rightClipPixels,
                                 bool fillFromRight = false,
                                 std::uint8_t alpha = 0xff) {
        const float clampedRatio = std::clamp(fillRatio, 0.0F, 1.0F);
        for (const assets::SpriteFrameModule& frameModule :
             atlas.modulesForFrame(frameIndex)) {
            if (frameModule.moduleIndex >= atlas.modules().size()) {
                valid = false;
                continue;
            }
            const assets::SpriteModule& module =
                atlas.modules()[frameModule.moduleIndex];
            // CSprite frame-module flag bit 0 mirrors the module in X. The
            // enemy bars reuse left-facing atlas modules on the right side of
            // the HUD through this flag; the level-one HUD does not use the
            // remaining transform bits.
            constexpr std::uint8_t horizontalFlip = 0x01;
            if (module.imageIndex != 0 ||
                (frameModule.flags & ~horizontalFlip) != 0) {
                valid = false;
                continue;
            }
            const bool flipX =
                (frameModule.flags & horizontalFlip) != 0;
            const float unclippedWidth =
                std::max(0.0F, static_cast<float>(module.width) -
                                   rightClipPixels);
            const float visibleWidth = unclippedWidth * clampedRatio;
            if (visibleWidth <= 0.0F || module.height == 0) {
                continue;
            }

            const float leftTrim =
                fillFromRight ? unclippedWidth - visibleWidth : 0.0F;
            const float left = screenOffsetX +
                               (originX + static_cast<float>(frameModule.x) +
                                leftTrim) *
                                   screenScale;
            const float top = screenOffsetY +
                              (originY + static_cast<float>(frameModule.y)) *
                                  screenScale;
            const float right = left + visibleWidth * screenScale;
            const float bottom =
                top + static_cast<float>(module.height) * screenScale;
            const float x0 = left / static_cast<float>(width_) * 2.0F - 1.0F;
            const float x1 = right / static_cast<float>(width_) * 2.0F - 1.0F;
            const float y0 = 1.0F - top / static_cast<float>(height_) * 2.0F;
            const float y1 =
                1.0F - bottom / static_cast<float>(height_) * 2.0F;

            const float sourceLeftTrim =
                fillFromRight && !flipX ? leftTrim : 0.0F;
            float u0 =
                (static_cast<float>(module.x) + sourceLeftTrim) /
                texture.width;
            const float v0 =
                static_cast<float>(module.y) / texture.height;
            float u1 =
                (static_cast<float>(module.x) + sourceLeftTrim +
                 visibleWidth) /
                texture.width;
            const float v1 =
                (static_cast<float>(module.y) + module.height) /
                texture.height;
            if (flipX) {
                std::swap(u0, u1);
            }
            const std::uint32_t white =
                rgbaVertexColor((static_cast<std::uint32_t>(alpha) << 24U) |
                                0x00ffffffU);
            const GpuVertex topLeft{{x0, y0, 0.0F}, {}, {u0, v0}, white};
            const GpuVertex topRight{{x1, y0, 0.0F}, {}, {u1, v0}, white};
            const GpuVertex bottomLeft{{x0, y1, 0.0F}, {}, {u0, v1}, white};
            const GpuVertex bottomRight{{x1, y1, 0.0F}, {}, {u1, v1}, white};
            vertices.insert(vertices.end(),
                            {topLeft, topRight, bottomLeft, topRight,
                             bottomRight, bottomLeft});
        }
    };
    const auto frameAdvance = [&](std::size_t frameIndex) {
        float advance = 0.0F;
        for (const assets::SpriteFrameModule& frameModule :
             atlas.modulesForFrame(frameIndex)) {
            if (frameModule.moduleIndex >= atlas.modules().size()) {
                continue;
            }
            const assets::SpriteModule& module =
                atlas.modules()[frameModule.moduleIndex];
            advance = std::max(
                advance, static_cast<float>(frameModule.x) + module.width);
        }
        return std::max(advance, 1.0F);
    };
    const auto appendNumber = [&](std::int32_t value, float originX,
                                  float originY, std::uint8_t alpha) {
        constexpr std::array<std::size_t, 10> digitFrames{
            0x42, 0x39, 0x3a, 0x3b, 0x3c,
            0x3d, 0x3e, 0x3f, 0x40, 0x41};
        const std::string digits = std::to_string(std::max(value, 0));
        float cursor = originX;
        for (const char digit : digits) {
            const std::size_t frame =
                digitFrames[static_cast<std::size_t>(digit - '0')];
            appendFrame(frame, cursor, originY, 1.0F, 0.0F, false, alpha);
            cursor += frameAdvance(frame);
        }
    };

    currentHealthRatio = std::clamp(currentHealthRatio, 0.0F, 1.0F);
    delayedHealthRatio =
        std::clamp(delayedHealthRatio, currentHealthRatio, 1.0F);
    appendFrame(0x1b, playerHudX, playerHudY, 1.0F, 0.0F);
    if (delayedHealthRatio > currentHealthRatio) {
        appendFrame(0x1c, playerHudX, playerHudY, delayedHealthRatio, 0.0F);
    }
    appendFrame(0x1d, playerHudX, playerHudY, currentHealthRatio, 0.0F);
    appendFrame(0x18, playerHudX, playerHudY, 1.0F, 0.0F);
    appendFrame(0x19, playerHudX, playerHudY, webPowerRatio, 5.0F);
    appendFrame(0x1f, playerHudX, playerHudY, 1.0F, 0.0F);

    if (showSkillPointTotal) {
        // CLevel::RenderSkillPoint (0x0038728c) uses the original 480x320
        // positions (80,90) and keeps the total visible for six seconds.
        // SetShowSkillPointFrame (0x0037f358) also creates frame 0x21 at
        // (40,90) for the duration of that display.
        appendFrame(0x21, 40.0F, 90.0F, 1.0F, 0.0F);
        appendFrame(0x6f, 80.0F, 90.0F, 1.0F, 0.0F);
        appendNumber(skillPoints, 98.0F, 94.0F, 0xff);
    }
    if (skillPointPopup != nullptr && skillPointPopup->visible) {
        const DirectX::XMMATRIX viewProjection = DirectX::XMMatrixTranspose(
            DirectX::XMLoadFloat4x4(&worldViewProjection_));
        const DirectX::XMVECTOR world = DirectX::XMVectorSet(
            skillPointPopup->playerPosition.x,
            skillPointPopup->playerPosition.y,
            skillPointPopup->playerPosition.z, 1.0F);
        const DirectX::XMVECTOR projected =
            DirectX::XMVector3TransformCoord(world, viewProjection);
        const float screenX =
            (DirectX::XMVectorGetX(projected) + 1.0F) * 0.5F * width_;
        const float screenY =
            (1.0F - DirectX::XMVectorGetY(projected)) * 0.5F * height_;
        const float popupX = (screenX - screenOffsetX) / screenScale;
        const float popupY = (screenY - screenOffsetY) / screenScale -
                             80.0F - 40.0F * skillPointPopup->progress;
        const std::uint8_t popupAlpha = static_cast<std::uint8_t>(
            std::clamp(std::lround(
                           (1.0F - skillPointPopup->progress) * 255.0F),
                       0L, 255L));
        appendFrame(0x43, popupX, popupY, 1.0F, 0.0F, false, popupAlpha);
        appendNumber(skillPointPopup->amount, popupX + 17.0F, popupY,
                     popupAlpha);
    }

    if (shownHealthBarEnemy != nullptr && shownHealthBarEnemy->asset != nullptr) {
        struct EnemyHealthFrames {
            std::size_t surround;
            std::size_t fill;
            std::size_t icon;
        };
        std::optional<EnemyHealthFrames> frames;
        switch (shownHealthBarEnemy->asset->enemyTypeId) {
        case 6:
            frames = EnemyHealthFrames{0x4e, 0x4f, 0x50};
            break;
        case 5:
            frames = EnemyHealthFrames{0x4e, 0x4f, 0x53};
            break;
        case 16:
            frames = EnemyHealthFrames{0x68, 0x69, 0x6a};
            break;
        case 7:
            frames = EnemyHealthFrames{0x6b, 0x6c, 0x6d};
            break;
        case 13:
        case 17:
            frames = EnemyHealthFrames{0x65, 0x66, 0x67};
            break;
        case 18:
            frames = EnemyHealthFrames{0x75, 0x76, 0x77};
            break;
        case 15:
            frames = EnemyHealthFrames{0x81, 0x82, 0x83};
            break;
        case 23:
            frames = EnemyHealthFrames{0x84, 0x85, 0x86};
            break;
        default:
            break;
        }
        if (frames) {
            const float maximumHealth =
                std::max(shownHealthBarEnemy->asset->health, 1.0F);
            const float enemyHealthRatio =
                shownHealthBarEnemy->health / maximumHealth;
            // CLevel::ShowHealthBarOfEnemy (0x00387548) paints the surround,
            // clips the fill from its right edge, then overlays the icon.
            appendFrame(frames->surround, enemyHudX, enemyHudY, 1.0F, 0.0F);
            appendFrame(frames->fill, enemyHudX, enemyHudY,
                        enemyHealthRatio, 0.0F, true);
            appendFrame(frames->icon, enemyHudX, enemyHudY, 1.0F, 0.0F);
        }
    }
    if (bossProgressVisible) {
        // CProgressBar::Draw2D (0x0031a960) paints interface frames 0x44 and
        // 0x45 at y=25 on the native 480x320 canvas. The moving frame's X is
        // 300 + 44 - trunc(88 * currentDistance / failureDistance).
        const float ratio = std::clamp(bossProgressRatio, 0.0F, 1.0F);
        appendFrame(0x44, 300.0F, 25.0F, 1.0F, 0.0F);
        appendFrame(0x45,
                    344.0F -
                        static_cast<float>(static_cast<std::int32_t>(
                            88.0F * ratio)),
                    25.0F, 1.0F, 0.0F);
    }
    if (!valid) {
        return Result::failure(
            "HUD frame uses an unsupported image or sprite transform");
    }
    if (vertices.size() > std::numeric_limits<std::uint32_t>::max()) {
        return Result::failure("HUD vertex count exceeds D3D11 limits");
    }

    if (vertices.size() > hudVertexCapacity_) {
        hudVertexBuffer_.Reset();
        hudVertexCapacity_ = static_cast<std::uint32_t>(
            std::max<std::size_t>(vertices.size(), 64));
        D3D11_BUFFER_DESC description{};
        description.ByteWidth =
            hudVertexCapacity_ * static_cast<UINT>(sizeof(GpuVertex));
        description.Usage = D3D11_USAGE_DYNAMIC;
        description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        const HRESULT createResult = device_->CreateBuffer(
            &description, nullptr, &hudVertexBuffer_);
        if (FAILED(createResult)) {
            hudVertexCapacity_ = 0;
            return hresultFailure("ID3D11Device::CreateBuffer(HUD)",
                                  createResult);
        }
    }

    hudVertexCount_ = static_cast<std::uint32_t>(vertices.size());
    if (vertices.empty()) {
        return Result::success();
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT mapResult = context_->Map(
        hudVertexBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(mapResult)) {
        return hresultFailure("ID3D11DeviceContext::Map(HUD)", mapResult);
    }
    std::memcpy(mapped.pData, vertices.data(),
                vertices.size() * sizeof(GpuVertex));
    context_->Unmap(hudVertexBuffer_.Get(), 0);
    return Result::success();
}

Result D3D11Renderer::updateCinematicUi(
    const game::LevelHudAsset& hud,
    const game::CinematicUiFrame& frame) {
    cinematicUiColorVertexBuffer_.Reset();
    cinematicUiColorVertexCount_ = 0;
    if (!device_) {
        return Result::failure("Cinematic UI has no D3D11 device");
    }

    const bool informationPanelVisible =
        frame.informationPanel != game::InformationPanel::None;
    const bool centeredPanel =
        frame.tutorialPanelVisible || informationPanelVisible;

    std::vector<GpuVertex> colorVertices;
    const auto appendColorQuad = [&colorVertices](float left, float top,
                                                   float right, float bottom,
                                                   std::uint32_t argb) {
        const std::uint32_t color = rgbaVertexColor(argb);
        const GpuVertex topLeft{{left, top, 0.0F}, {}, {}, color};
        const GpuVertex topRight{{right, top, 0.0F}, {}, {}, color};
        const GpuVertex bottomLeft{{left, bottom, 0.0F}, {}, {}, color};
        const GpuVertex bottomRight{{right, bottom, 0.0F}, {}, {}, color};
        colorVertices.insert(colorVertices.end(),
                             {topLeft, topRight, bottomLeft, topRight,
                              bottomRight, bottomLeft});
    };
    if (frame.dimBackground) {
        appendColorQuad(-1.0F, 1.0F, 1.0F, -1.0F,
                        frame.tutorialPanelVisible ? 0x78000000U
                                                   : 0xb0000000U);
    }
    if (frame.letterboxVisible) {
        appendColorQuad(-1.0F, 1.0F, 1.0F, 0.78F, 0xe0000000U);
        appendColorQuad(-1.0F, -0.78F, 1.0F, -1.0F, 0xe0000000U);
    }
    if (frame.textVisible && !frame.messagePanelVisible &&
        !centeredPanel) {
        const bool centered =
            frame.dimBackground || frame.quickTimeEventVisible;
        appendColorQuad(-0.9F, centered ? 0.35F : -0.48F, 0.9F,
                        centered ? -0.35F : -0.96F, 0xb0000000U);
    }
    if (frame.quickTimeEventVisible) {
        appendColorQuad(-0.42F, -0.39F, 0.42F, -0.45F, 0xff303030U);
        appendColorQuad(-0.42F, -0.39F,
                        -0.42F + 0.84F *
                                     (1.0F - frame.quickTimeEventProgress),
                        -0.45F, 0xfff0c030U);
    }
    if (frame.blackOverlayAlpha > 0.0F) {
        const auto alpha = static_cast<std::uint32_t>(std::lround(
            std::clamp(frame.blackOverlayAlpha, 0.0F, 1.0F) * 255.0F));
        appendColorQuad(-1.0F, 1.0F, 1.0F, -1.0F, alpha << 24U);
    }
    if (!colorVertices.empty()) {
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = static_cast<UINT>(colorVertices.size() *
                                                   sizeof(GpuVertex));
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data{colorVertices.data(), 0, 0};
        const HRESULT createResult = device_->CreateBuffer(
            &description, &data, &cinematicUiColorVertexBuffer_);
        if (FAILED(createResult)) {
            return hresultFailure(
                "ID3D11Device::CreateBuffer(cinematic UI colors)",
                createResult);
        }
        cinematicUiColorVertexCount_ =
            static_cast<std::uint32_t>(colorVertices.size());
    }

    if ((frame.messagePanelVisible || centeredPanel) &&
        frame.textVisible && !frame.text.empty()) {
        cinematicUiTextVertexBuffer_.Reset();
        cinematicUiTextTexture_.Reset();
        cinematicUiTextVertexCount_ = 0;
        cinematicUiText_.clear();
        if (!cinematicUiTutorialTexture_ ||
            !deathConfirmationNormalFontTexture_ || width_ == 0 ||
            height_ == 0) {
            return Result::failure(
                "Cinematic message GPU resources are incomplete");
        }

        const assets::SpriteAtlas& tutorial = hud.tutorialAtlas;
        const assets::RgbaImage& tutorialTexture =
            hud.tutorialTexture.image();
        const assets::SpriteAtlas& font = hud.normalWhiteFontAtlas;
        const assets::RgbaImage& fontTexture =
            hud.normalWhiteFontTexture.image();
        const bool useXboxButtonGlyphs =
            cinematicUiControllerTexture_ &&
            cinematicUiControllerImage_.width == 1280U &&
            cinematicUiControllerImage_.height == 640U &&
            containsXboxButtonGlyph(frame.text);
        bool valid = true;
        std::string invalidDetail;

        const auto mapCharacter = [](char16_t character) -> std::size_t {
            // English __CHARACTERS_MAP at 0x0056e4f0, consumed by
            // CFont::GetMap at 0x002e6164.
            if (character >= 0x20 && character <= 0x3f) {
                return static_cast<std::size_t>(character - 0x20);
            }
            if (character == u'@') {
                return 0xa5;
            }
            if (character >= u'A' && character <= u'Z') {
                return 0x20 +
                       static_cast<std::size_t>(character - u'A');
            }
            if (character >= u'[' && character <= u'_') {
                return 0x3b +
                       static_cast<std::size_t>(character - u'[');
            }
            if (character == u'`') {
                return 0x5e;
            }
            if (character >= u'a' && character <= u'z') {
                return 0x3f +
                       static_cast<std::size_t>(character - u'a');
            }
            if (character >= u'{' && character <= u'~') {
                return 0x5b +
                       static_cast<std::size_t>(character - u'{');
            }
            return 0x1f;
        };
        const auto glyphAdvance = [&](char16_t character) -> float {
            const std::size_t mapped = mapCharacter(character);
            if (font.frameModules().empty() ||
                mapped >= font.frameModules().size()) {
                valid = false;
                return 0.0F;
            }
            const assets::SpriteFrameModule& glyph =
                font.frameModules()[mapped];
            if (glyph.moduleIndex >= font.modules().size()) {
                valid = false;
                return 0.0F;
            }
            // CFont::GetStringSize (0x002e6238) uses the base fmodule X,
            // module width, and glyph fmodule X as its character advance.
            return static_cast<float>(font.frameModules()[0].x) +
                   static_cast<float>(
                       font.modules()[glyph.moduleIndex].width) +
                   static_cast<float>(glyph.x);
        };
        const auto measureLine = [&](std::u16string_view text) -> float {
            if (text.empty() || font.frameModules().empty()) {
                return 0.0F;
            }
            float result = 0.0F;
            float segmentWidth = 0.0F;
            bool segmentHasText = false;
            const float baseSpacing =
                static_cast<float>(font.frameModules()[0].x);
            const auto flushSegment = [&] {
                if (segmentHasText) {
                    result += std::max(0.0F,
                                       segmentWidth - baseSpacing);
                }
                segmentWidth = 0.0F;
                segmentHasText = false;
            };
            for (std::size_t index = 0; index < text.size(); ++index) {
                if (useXboxButtonGlyphs) {
                    const std::optional<XboxButtonGlyph> glyph =
                        xboxButtonGlyphAt(text, index);
                    if (glyph) {
                        segmentWidth += 20.0F;
                        segmentHasText = true;
                        index += glyph->characterCount - 1U;
                        continue;
                    }
                }
                if (centeredPanel && text[index] == u'^' &&
                    index + 1U < text.size()) {
                    flushSegment();
                    ++index;
                    continue;
                }
                segmentWidth += text[index] == u'[' || text[index] == u']'
                    ? 6.0F
                    : glyphAdvance(text[index]);
                segmentHasText = true;
            }
            flushSegment();
            return result;
        };
        const auto fontHeight = [&]() -> float {
            if (font.frameModules().empty() ||
                font.frameModules()[0].moduleIndex >= font.modules().size()) {
                valid = false;
                return 0.0F;
            }
            return static_cast<float>(
                font.modules()[font.frameModules()[0].moduleIndex].height);
        };
        const auto wrapText = [&]() {
            std::vector<std::u16string> lines;
            std::u16string line;
            std::size_t cursor = 0;
            while (cursor < frame.text.size()) {
                if (frame.text[cursor] == u'\n') {
                    lines.push_back(line);
                    line.clear();
                    ++cursor;
                    continue;
                }
                while (cursor < frame.text.size() &&
                       frame.text[cursor] == u' ') {
                    ++cursor;
                }
                if (cursor >= frame.text.size()) {
                    break;
                }
                const std::size_t wordStart = cursor;
                while (cursor < frame.text.size() &&
                       frame.text[cursor] != u' ' &&
                       frame.text[cursor] != u'\n') {
                    ++cursor;
                }
                const std::u16string word(
                    frame.text.substr(wordStart, cursor - wordStart));
                std::u16string candidate = line;
                if (!candidate.empty()) {
                    candidate.push_back(u' ');
                }
                candidate += word;
                // AddInfo (0x0038dea0) and AddMessage (0x0038d7b4) use
                // native CFont::SplitText widths 0x186 and 0x15e.
                const float maximumWidth =
                    centeredPanel ? 390.0F : 350.0F;
                if (!line.empty() &&
                    measureLine(candidate) >= maximumWidth) {
                    lines.push_back(line);
                    line = word;
                } else {
                    line = std::move(candidate);
                }
            }
            if (!line.empty() || lines.empty()) {
                lines.push_back(std::move(line));
            }
            return lines;
        };
        const std::vector<std::u16string> lines = wrapText();
        const std::size_t pageCount = (lines.size() + 1U) / 2U;
        std::vector<std::int32_t> pageDurations(pageCount, 0);
        std::size_t characterCount = 0;
        for (const std::u16string& line : lines) {
            characterCount += line.size();
        }
        if (frame.messagePanelVisible && pageCount != 0 &&
            characterCount != 0 &&
            frame.messageDurationMilliseconds > 0) {
            for (std::size_t page = 0; page < pageCount; ++page) {
                std::size_t pageCharacters = lines[page * 2U].size();
                if (page * 2U + 1U < lines.size()) {
                    pageCharacters += lines[page * 2U + 1U].size();
                }
                pageDurations[page] = static_cast<std::int32_t>(
                    static_cast<std::int64_t>(
                        frame.messageDurationMilliseconds) *
                    static_cast<std::int64_t>(pageCharacters) /
                    static_cast<std::int64_t>(characterCount));
            }
            // ComputeTimeForEachPageOfMessage (0x0038ba6c) guarantees the
            // final page 999 ms and removes any added time evenly from the
            // preceding pages.
            if (pageCount > 1U && pageDurations.back() < 999) {
                const std::int32_t adjustment =
                    (999 - pageDurations.back()) /
                    static_cast<std::int32_t>(pageCount - 1U);
                pageDurations.back() = 999;
                for (std::size_t page = 0; page + 1U < pageCount; ++page) {
                    pageDurations[page] -= adjustment;
                }
            }
        }
        std::size_t pageIndex = 0;
        std::int32_t elapsed = frame.messageElapsedMilliseconds;
        while (pageIndex + 1U < pageDurations.size() &&
               elapsed >= pageDurations[pageIndex]) {
            elapsed -= pageDurations[pageIndex];
            ++pageIndex;
        }
        if (frame.text == cinematicUiMessageText_ &&
            frame.messageFace == cinematicUiMessageFace_ &&
            static_cast<std::int32_t>(pageIndex) ==
                cinematicUiMessagePage_ &&
            frame.tutorialPanelVisible == cinematicUiTutorialPanel_ &&
            frame.informationPanel == cinematicUiInformationPanel_ &&
            frame.tutorialButton == cinematicUiTutorialButton_ &&
            cinematicUiMessagePanelVertexBuffer_ &&
            cinematicUiMessageTextVertexBuffer_ &&
            (!useXboxButtonGlyphs ||
             cinematicUiControllerIconVertexBuffer_)) {
            return Result::success();
        }

        constexpr float virtualWidth = 480.0F;
        constexpr float virtualHeight = 320.0F;
        const float screenScale =
            std::min(static_cast<float>(width_) / virtualWidth,
                     static_cast<float>(height_) / virtualHeight);
        const float screenOffsetX =
            (static_cast<float>(width_) - virtualWidth * screenScale) *
            0.5F;
        const float screenOffsetY =
            (static_cast<float>(height_) - virtualHeight * screenScale) *
            0.5F;
        std::vector<GpuVertex> panelVertices;
        std::vector<GpuVertex> iconVertices;
        std::vector<GpuVertex> controllerVertices;
        std::vector<GpuVertex> textVertices;
        const auto appendVirtualSolidQuad =
            [&](float left, float top, float right, float bottom) {
                const float screenLeft =
                    screenOffsetX + left * screenScale;
                const float screenTop = screenOffsetY + top * screenScale;
                const float screenRight =
                    screenOffsetX + right * screenScale;
                const float screenBottom =
                    screenOffsetY + bottom * screenScale;
                const float x0 = screenLeft /
                                     static_cast<float>(width_) * 2.0F -
                                 1.0F;
                const float x1 = screenRight /
                                     static_cast<float>(width_) * 2.0F -
                                 1.0F;
                const float y0 = 1.0F - screenTop /
                                            static_cast<float>(height_) *
                                            2.0F;
                const float y1 = 1.0F - screenBottom /
                                            static_cast<float>(height_) *
                                            2.0F;
                constexpr std::uint32_t white = 0xffffffffU;
                const GpuVertex topLeft{{x0, y0, 0.0F}, {}, {}, white};
                const GpuVertex topRight{{x1, y0, 0.0F}, {}, {}, white};
                const GpuVertex bottomLeft{{x0, y1, 0.0F}, {}, {}, white};
                const GpuVertex bottomRight{{x1, y1, 0.0F}, {}, {}, white};
                iconVertices.insert(iconVertices.end(),
                                    {topLeft, topRight, bottomLeft, topRight,
                                     bottomRight, bottomLeft});
            };
        const auto appendXboxButtonGlyph =
            [&](const XboxButtonGlyph& glyph, float left, float top,
                float size) {
                const float screenLeft =
                    screenOffsetX + left * screenScale;
                const float screenTop = screenOffsetY + top * screenScale;
                const float screenRight =
                    screenOffsetX + (left + size) * screenScale;
                const float screenBottom =
                    screenOffsetY + (top + size) * screenScale;
                const float x0 = screenLeft /
                                     static_cast<float>(width_) * 2.0F -
                                 1.0F;
                const float x1 = screenRight /
                                     static_cast<float>(width_) * 2.0F -
                                 1.0F;
                const float y0 = 1.0F - screenTop /
                                            static_cast<float>(height_) *
                                            2.0F;
                const float y1 = 1.0F - screenBottom /
                                            static_cast<float>(height_) *
                                            2.0F;
                const float u0 = static_cast<float>(glyph.sourceX) /
                                 cinematicUiControllerImage_.width;
                const float v0 = static_cast<float>(glyph.sourceY) /
                                 cinematicUiControllerImage_.height;
                const float u1 =
                    static_cast<float>(glyph.sourceX + glyph.sourceWidth) /
                    cinematicUiControllerImage_.width;
                const float v1 =
                    static_cast<float>(glyph.sourceY + glyph.sourceHeight) /
                    cinematicUiControllerImage_.height;
                constexpr std::uint32_t white = 0xffffffffU;
                const GpuVertex topLeft{
                    {x0, y0, 0.0F}, {}, {u0, v0}, white};
                const GpuVertex topRight{
                    {x1, y0, 0.0F}, {}, {u1, v0}, white};
                const GpuVertex bottomLeft{
                    {x0, y1, 0.0F}, {}, {u0, v1}, white};
                const GpuVertex bottomRight{
                    {x1, y1, 0.0F}, {}, {u1, v1}, white};
                controllerVertices.insert(
                    controllerVertices.end(),
                    {topLeft, topRight, bottomLeft, topRight, bottomRight,
                     bottomLeft});
            };
        const auto appendFrameModule =
            [&](std::vector<GpuVertex>& vertices,
                const assets::SpriteAtlas& atlas,
                const assets::RgbaImage& texture,
                std::size_t frameModuleIndex, float originX,
                float originY) {
                if (frameModuleIndex >= atlas.frameModules().size()) {
                    valid = false;
                    invalidDetail =
                        "frame-module index " +
                        std::to_string(frameModuleIndex) +
                        " exceeds atlas count " +
                        std::to_string(atlas.frameModules().size());
                    return;
                }
                const assets::SpriteFrameModule& frameModule =
                    atlas.frameModules()[frameModuleIndex];
                if (frameModule.moduleIndex >= atlas.modules().size()) {
                    valid = false;
                    invalidDetail =
                        "module index " +
                        std::to_string(frameModule.moduleIndex) +
                        " exceeds atlas count " +
                        std::to_string(atlas.modules().size());
                    return;
                }
                const assets::SpriteModule& module =
                    atlas.modules()[frameModule.moduleIndex];
                constexpr std::uint8_t horizontalFlip = 0x01;
                constexpr std::uint8_t verticalFlip = 0x02;
                if (module.imageIndex != 0 || module.width == 0 ||
                    module.height == 0 ||
                    (frameModule.flags &
                     ~(horizontalFlip | verticalFlip)) != 0) {
                    valid = false;
                    invalidDetail =
                        "module " +
                        std::to_string(frameModule.moduleIndex) +
                        " has image=" +
                        std::to_string(module.imageIndex) + ", size=" +
                        std::to_string(module.width) + "x" +
                        std::to_string(module.height) + ", flags=" +
                        std::to_string(frameModule.flags);
                    return;
                }
                const float left =
                    screenOffsetX +
                    (originX + static_cast<float>(frameModule.x)) *
                        screenScale;
                const float top =
                    screenOffsetY +
                    (originY + static_cast<float>(frameModule.y)) *
                        screenScale;
                const float right =
                    left + static_cast<float>(module.width) * screenScale;
                const float bottom =
                    top + static_cast<float>(module.height) * screenScale;
                const float x0 =
                    left / static_cast<float>(width_) * 2.0F - 1.0F;
                const float x1 =
                    right / static_cast<float>(width_) * 2.0F - 1.0F;
                const float y0 =
                    1.0F - top / static_cast<float>(height_) * 2.0F;
                const float y1 =
                    1.0F - bottom / static_cast<float>(height_) * 2.0F;
                float u0 = static_cast<float>(module.x) / texture.width;
                float v0 = static_cast<float>(module.y) / texture.height;
                float u1 = static_cast<float>(module.x + module.width) /
                           texture.width;
                float v1 = static_cast<float>(module.y + module.height) /
                           texture.height;
                if ((frameModule.flags & horizontalFlip) != 0) {
                    std::swap(u0, u1);
                }
                if ((frameModule.flags & verticalFlip) != 0) {
                    std::swap(v0, v1);
                }
                constexpr std::uint32_t white = 0xffffffffU;
                const GpuVertex topLeft{
                    {x0, y0, 0.0F}, {}, {u0, v0}, white};
                const GpuVertex topRight{
                    {x1, y0, 0.0F}, {}, {u1, v0}, white};
                const GpuVertex bottomLeft{
                    {x0, y1, 0.0F}, {}, {u0, v1}, white};
                const GpuVertex bottomRight{
                    {x1, y1, 0.0F}, {}, {u1, v1}, white};
                vertices.insert(vertices.end(),
                                {topLeft, topRight, bottomLeft, topRight,
                                 bottomRight, bottomLeft});
            };
        const auto appendFrame = [&](std::size_t frameIndex, float originX,
                                     float originY) {
            if (frameIndex >= tutorial.frames().size()) {
                valid = false;
                return;
            }
            const assets::SpriteFrame& spriteFrame =
                tutorial.frames()[frameIndex];
            for (std::size_t index = 0; index < spriteFrame.moduleCount;
                 ++index) {
                appendFrameModule(
                    panelVertices, tutorial, tutorialTexture,
                    spriteFrame.firstModuleIndex + index, originX,
                    originY);
            }
        };
        const auto frameExtent = [&](std::size_t frameIndex) {
            std::array<float, 2> extent{};
            if (frameIndex >= tutorial.frames().size()) {
                valid = false;
                return extent;
            }
            float minimumX = std::numeric_limits<float>::max();
            float minimumY = std::numeric_limits<float>::max();
            float maximumX = std::numeric_limits<float>::lowest();
            float maximumY = std::numeric_limits<float>::lowest();
            for (const assets::SpriteFrameModule& frameModule :
                 tutorial.modulesForFrame(frameIndex)) {
                if (frameModule.moduleIndex >= tutorial.modules().size()) {
                    valid = false;
                    continue;
                }
                const assets::SpriteModule& module =
                    tutorial.modules()[frameModule.moduleIndex];
                minimumX = std::min(
                    minimumX, static_cast<float>(frameModule.x));
                minimumY = std::min(
                    minimumY, static_cast<float>(frameModule.y));
                maximumX = std::max(
                    maximumX, static_cast<float>(frameModule.x) +
                                  static_cast<float>(module.width));
                maximumY = std::max(
                    maximumY, static_cast<float>(frameModule.y) +
                                  static_cast<float>(module.height));
            }
            if (minimumX <= maximumX && minimumY <= maximumY) {
                extent = {maximumX - minimumX, maximumY - minimumY};
            }
            return extent;
        };
        const auto frameVerticalBounds = [&](std::size_t frameIndex) {
            std::array<float, 2> bounds{};
            if (frameIndex >= tutorial.frames().size()) {
                valid = false;
                return bounds;
            }
            float minimumY = std::numeric_limits<float>::max();
            float maximumY = std::numeric_limits<float>::lowest();
            for (const assets::SpriteFrameModule& frameModule :
                 tutorial.modulesForFrame(frameIndex)) {
                if (frameModule.moduleIndex >= tutorial.modules().size()) {
                    valid = false;
                    continue;
                }
                const assets::SpriteModule& module =
                    tutorial.modules()[frameModule.moduleIndex];
                minimumY = std::min(
                    minimumY, static_cast<float>(frameModule.y));
                maximumY = std::max(
                    maximumY, static_cast<float>(frameModule.y) +
                                  static_cast<float>(module.height));
            }
            if (minimumY <= maximumY) {
                bounds = {minimumY, maximumY};
            }
            return bounds;
        };
        const auto faceFrameForMessage = [](std::int32_t face) {
            // CTutorial::GetFrameIdByFace (0x0038b98c).
            switch (face) {
            case 1: return std::size_t{2};
            case 2:
            case 3: return std::size_t{3};
            case 4: return std::size_t{5};
            case 5: return std::size_t{6};
            case 6:
            case 7:
            case 8: return std::size_t{4};
            case 9: return std::size_t{8};
            case 10: return std::size_t{7};
            case 11: return std::size_t{10};
            case 12: return std::size_t{9};
            case 13: return std::size_t{12};
            case 14: return std::size_t{13};
            case 15: return std::size_t{14};
            default: return std::size_t{0};
            }
        };

        if (centeredPanel) {
            // CTutorial::Draw (0x0038e648) selects the compact frame 0xf
            // for one line and frame 0 for multiple lines. The original
            // Android UI anchored these items at the top. The Windows
            // controller presentation deliberately moves the same shipped
            // panel to the bottom of the 480x320 canvas.
            // RenderComicCoverInfo (0x0038c434) selects the frame by notice
            // kind, independently of the localized line count. Both no-face
            // information panels follow the requested bottom placement.
            const bool singleLine = informationPanelVisible
                ? frame.informationPanel == game::InformationPanel::Compact
                : lines.size() == 1U;
            const std::size_t tutorialFrame = singleLine ? 0xfU : 0U;
            const float nativeItemY = singleLine ? 24.0F : 44.0F;
            const std::array<float, 2> panelExtent =
                frameExtent(tutorialFrame);
            const std::array<float, 2> verticalBounds =
                frameVerticalBounds(tutorialFrame);
            constexpr float bottomMargin = 8.0F;
            const float itemY = virtualHeight - bottomMargin -
                                verticalBounds[1];
            const float verticalTranslation = itemY - nativeItemY;
            appendFrame(tutorialFrame, 240.0F, itemY);

            const std::size_t visibleLineCount =
                std::min<std::size_t>(lines.size(), 3U);
            const float lineHeight = fontHeight();
            const float totalHeight =
                static_cast<float>(visibleLineCount) * lineHeight;
            const float verticalAllowance =
                visibleLineCount == 1U ? 42.0F : 68.0F;
            float textY = nativeItemY - panelExtent[1] * 0.5F +
                          lineHeight * 0.5F +
                          (verticalAllowance - totalHeight) * 0.5F +
                          verticalTranslation;
            for (std::size_t line = 0; line < visibleLineCount; ++line) {
                float cursorX = 240.0F - measureLine(lines[line]) * 0.5F;
                bool segmentHasText = false;
                for (std::size_t characterIndex = 0;
                     characterIndex < lines[line].size();
                     ++characterIndex) {
                    const char16_t character =
                        lines[line][characterIndex];
                    if (useXboxButtonGlyphs) {
                        const std::optional<XboxButtonGlyph> glyph =
                            xboxButtonGlyphAt(lines[line], characterIndex);
                        if (glyph) {
                            constexpr float glyphSize = 18.0F;
                            appendXboxButtonGlyph(
                                *glyph, cursorX + 1.0F,
                                textY + (fontHeight() - glyphSize) * 0.5F,
                                glyphSize);
                            cursorX += 20.0F;
                            characterIndex += glyph->characterCount - 1U;
                            segmentHasText = true;
                            continue;
                        }
                    }
                    if (character == u'^' &&
                        characterIndex + 1U < lines[line].size()) {
                        if (segmentHasText) {
                            cursorX -= static_cast<float>(
                                font.frameModules()[0].x);
                            segmentHasText = false;
                        }
                        ++characterIndex;
                        continue;
                    }
                    if (character == u'[' || character == u']') {
                        constexpr float bracketAdvance = 6.0F;
                        constexpr float bracketThickness = 1.0F;
                        constexpr float bracketCapWidth = 4.0F;
                        const float bracketTop = textY + 1.0F;
                        const float bracketBottom =
                            textY + fontHeight() - 1.0F;
                        const float verticalLeft = character == u'['
                            ? cursorX + 1.0F
                            : cursorX + bracketAdvance - 2.0F;
                        appendVirtualSolidQuad(
                            verticalLeft, bracketTop,
                            verticalLeft + bracketThickness, bracketBottom);
                        const float capLeft = character == u'['
                            ? verticalLeft
                            : verticalLeft - bracketCapWidth +
                                  bracketThickness;
                        appendVirtualSolidQuad(
                            capLeft, bracketTop,
                            capLeft + bracketCapWidth,
                            bracketTop + bracketThickness);
                        appendVirtualSolidQuad(
                            capLeft, bracketBottom - bracketThickness,
                            capLeft + bracketCapWidth, bracketBottom);
                        cursorX += bracketAdvance;
                        segmentHasText = true;
                        continue;
                    }
                    appendFrameModule(textVertices, font, fontTexture,
                                      mapCharacter(character), cursorX,
                                      textY);
                    cursorX += glyphAdvance(character);
                    segmentHasText = true;
                }
                textY += lineHeight + 4.0F;
            }
        } else {
            const std::size_t faceFrame =
                faceFrameForMessage(frame.messageFace);
            const std::array<float, 2> faceExtent =
                frameExtent(faceFrame);
            const std::array<float, 2> panelExtent = frameExtent(1);
            constexpr float messageItemY = 25.0F;
            appendFrame(faceFrame, 0.0F,
                        virtualHeight - messageItemY - faceExtent[1]);

            float panelX = faceExtent[0] - 10.0F;
            const float panelY =
                327.0F - messageItemY - panelExtent[1];
            if (tutorial.frames().size() <= 1 ||
                tutorial.frames()[1].moduleCount < 5) {
                valid = false;
            } else {
                const std::size_t firstPanelModule =
                    tutorial.frames()[1].firstModuleIndex;
                for (std::size_t index = 0; index < 3; ++index) {
                    appendFrameModule(
                        panelVertices, tutorial, tutorialTexture,
                        firstPanelModule + index, panelX, panelY);
                }

                const std::size_t firstLine = pageIndex * 2U;
                const std::size_t endLine =
                    std::min(firstLine + 2U, lines.size());
                float maximumLineWidth = 0.0F;
                for (std::size_t line = firstLine; line < endLine;
                     ++line) {
                    maximumLineWidth = std::max(
                        maximumLineWidth, measureLine(lines[line]));
                }
                if (maximumLineWidth > 40.0F) {
                    const std::int32_t repeats =
                        static_cast<std::int32_t>(
                            (maximumLineWidth - 40.0F) / 20.0F);
                    for (std::int32_t index = 0; index < repeats;
                         ++index) {
                        panelX += 20.0F;
                        appendFrameModule(
                            panelVertices, tutorial, tutorialTexture,
                            firstPanelModule + 2U, panelX, panelY);
                    }
                }
                appendFrameModule(panelVertices, tutorial, tutorialTexture,
                                  firstPanelModule + 3U, panelX, panelY);
                appendFrameModule(panelVertices, tutorial, tutorialTexture,
                                  firstPanelModule + 4U, panelX, panelY);

                const float lineHeight = fontHeight();
                float textY = 0.0F;
                const std::size_t visibleLineCount = endLine - firstLine;
                if (visibleLineCount == 1U) {
                    textY = virtualHeight - messageItemY -
                            panelExtent[1] * 0.5F -
                            lineHeight * 0.5F;
                } else {
                    const float totalHeight =
                        static_cast<float>(visibleLineCount) *
                        (lineHeight + 6.0F);
                    textY = 314.0F - messageItemY - totalHeight;
                }
                const float textX = faceExtent[0] + 46.0F;
                for (std::size_t line = firstLine; line < endLine;
                     ++line) {
                    float cursorX = textX;
                    for (const char16_t character : lines[line]) {
                        appendFrameModule(textVertices, font, fontTexture,
                                          mapCharacter(character), cursorX,
                                          textY);
                        cursorX += glyphAdvance(character);
                    }
                    textY += lineHeight + 6.0F;
                }
            }
        }

        if (!valid) {
            return Result::failure(
                "Cinematic message references invalid shipped sprite data: " +
                invalidDetail);
        }
        const auto createUiBuffer =
            [this](std::span<const GpuVertex> vertices,
                   Microsoft::WRL::ComPtr<ID3D11Buffer>& destination,
                   std::string_view name) -> Result {
                destination.Reset();
                if (vertices.empty()) {
                    return Result::success();
                }
                if (vertices.size() >
                    std::numeric_limits<UINT>::max() /
                        sizeof(GpuVertex)) {
                    return Result::failure(std::string(name) +
                                           " vertex buffer is too large");
                }
                D3D11_BUFFER_DESC description{};
                description.ByteWidth = static_cast<UINT>(
                    vertices.size() * sizeof(GpuVertex));
                description.Usage = D3D11_USAGE_IMMUTABLE;
                description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
                D3D11_SUBRESOURCE_DATA data{vertices.data(), 0, 0};
                const HRESULT createResult = device_->CreateBuffer(
                    &description, &data, &destination);
                return FAILED(createResult)
                           ? hresultFailure(
                                 std::string("ID3D11Device::CreateBuffer(") +
                                     std::string(name) + ")",
                                 createResult)
                           : Result::success();
            };
        Result result = createUiBuffer(
            panelVertices, cinematicUiMessagePanelVertexBuffer_,
            "cinematic message panel");
        if (!result) {
            return result;
        }
        result = createUiBuffer(iconVertices,
                                cinematicUiMessageIconVertexBuffer_,
                                "cinematic tutorial icons");
        if (!result) {
            return result;
        }
        result = createUiBuffer(
            controllerVertices, cinematicUiControllerIconVertexBuffer_,
            "cinematic controller icons");
        if (!result) {
            return result;
        }
        result = createUiBuffer(textVertices,
                                cinematicUiMessageTextVertexBuffer_,
                                "cinematic message text");
        if (!result) {
            return result;
        }
        cinematicUiMessagePanelVertexCount_ =
            static_cast<std::uint32_t>(panelVertices.size());
        cinematicUiMessageIconVertexCount_ =
            static_cast<std::uint32_t>(iconVertices.size());
        cinematicUiControllerIconVertexCount_ =
            static_cast<std::uint32_t>(controllerVertices.size());
        cinematicUiMessageTextVertexCount_ =
            static_cast<std::uint32_t>(textVertices.size());
        cinematicUiMessageText_ = frame.text;
        cinematicUiMessageFace_ = frame.messageFace;
        cinematicUiMessagePage_ =
            static_cast<std::int32_t>(pageIndex);
        cinematicUiTutorialPanel_ = frame.tutorialPanelVisible;
        cinematicUiInformationPanel_ = frame.informationPanel;
        cinematicUiTutorialButton_ = frame.tutorialButton;
        return Result::success();
    }

    cinematicUiMessagePanelVertexBuffer_.Reset();
    cinematicUiMessageIconVertexBuffer_.Reset();
    cinematicUiControllerIconVertexBuffer_.Reset();
    cinematicUiMessageTextVertexBuffer_.Reset();
    cinematicUiMessagePanelVertexCount_ = 0;
    cinematicUiMessageIconVertexCount_ = 0;
    cinematicUiControllerIconVertexCount_ = 0;
    cinematicUiMessageTextVertexCount_ = 0;
    cinematicUiMessageText_.clear();
    cinematicUiMessageFace_ = -1;
    cinematicUiMessagePage_ = -1;
    cinematicUiTutorialPanel_ = false;
    cinematicUiInformationPanel_ = game::InformationPanel::None;
    cinematicUiTutorialButton_ = -1;

    if (!frame.textVisible || frame.text.empty()) {
        cinematicUiTextVertexBuffer_.Reset();
        cinematicUiTextTexture_.Reset();
        cinematicUiTextVertexCount_ = 0;
        cinematicUiText_.clear();
        return Result::success();
    }
    const bool centered = frame.dimBackground || frame.quickTimeEventVisible;
    if (frame.text == cinematicUiText_ &&
        centered == cinematicUiTextCentered_ &&
        cinematicUiTextVertexBuffer_ && cinematicUiTextTexture_) {
        return Result::success();
    }
    cinematicUiTextVertexBuffer_.Reset();
    cinematicUiTextTexture_.Reset();
    cinematicUiTextVertexCount_ = 0;
    assets::RgbaImage textImage;
    const assets::RgbaImage* xboxButtonAtlas =
        cinematicUiControllerTexture_ ? &cinematicUiControllerImage_
                                      : nullptr;
    Result result = renderWindowsText(frame.text, xboxButtonAtlas, textImage);
    if (!result) {
        return result;
    }
    result = createTextureView({&textImage, 1}, cinematicUiTextTexture_);
    if (!result) {
        return result;
    }
    const float top = centered ? 0.34F : -0.5F;
    const float bottom = centered ? -0.34F : -0.94F;
    constexpr std::uint32_t white = 0xffffffffU;
    const std::array<GpuVertex, 6> textVertices{
        GpuVertex{{-0.88F, top, 0.0F}, {}, {0.0F, 0.0F}, white},
        GpuVertex{{0.88F, top, 0.0F}, {}, {1.0F, 0.0F}, white},
        GpuVertex{{-0.88F, bottom, 0.0F}, {}, {0.0F, 1.0F}, white},
        GpuVertex{{0.88F, top, 0.0F}, {}, {1.0F, 0.0F}, white},
        GpuVertex{{0.88F, bottom, 0.0F}, {}, {1.0F, 1.0F}, white},
        GpuVertex{{-0.88F, bottom, 0.0F}, {}, {0.0F, 1.0F}, white},
    };
    D3D11_BUFFER_DESC description{};
    description.ByteWidth = sizeof(textVertices);
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA data{textVertices.data(), 0, 0};
    const HRESULT createResult = device_->CreateBuffer(
        &description, &data, &cinematicUiTextVertexBuffer_);
    if (FAILED(createResult)) {
        return hresultFailure(
            "ID3D11Device::CreateBuffer(cinematic UI text)", createResult);
    }
    cinematicUiTextVertexCount_ =
        static_cast<std::uint32_t>(textVertices.size());
    cinematicUiText_ = frame.text;
    cinematicUiTextCentered_ = centered;
    return Result::success();
}

Result D3D11Renderer::updateTransport(
    const game::LevelHudAsset& hud,
    const game::TransportFrame& frame) {
    transportSpriteVertexBuffer_.Reset();
    transportColorVertexBuffer_.Reset();
    transportSpriteVertexCount_ = 0;
    transportColorVertexCount_ = 0;
    if (!device_) {
        return Result::failure("Transport has no D3D11 device");
    }
    if (!frame.visible()) {
        return Result::success();
    }

    std::vector<GpuVertex> spriteVertices;
    std::vector<GpuVertex> colorVertices;
    const auto appendColorQuad = [&](float left, float top, float right,
                                     float bottom) {
        if (right <= left || bottom <= top) {
            return;
        }
        const float x0 = left / static_cast<float>(width_) * 2.0F - 1.0F;
        const float x1 = right / static_cast<float>(width_) * 2.0F - 1.0F;
        const float y0 = 1.0F - top / static_cast<float>(height_) * 2.0F;
        const float y1 = 1.0F - bottom / static_cast<float>(height_) * 2.0F;
        const std::uint32_t black = rgbaVertexColor(0xff000000U);
        const GpuVertex topLeft{{x0, y0, 0.0F}, {}, {}, black};
        const GpuVertex topRight{{x1, y0, 0.0F}, {}, {}, black};
        const GpuVertex bottomLeft{{x0, y1, 0.0F}, {}, {}, black};
        const GpuVertex bottomRight{{x1, y1, 0.0F}, {}, {}, black};
        colorVertices.insert(colorVertices.end(),
                             {topLeft, topRight, bottomLeft, topRight,
                              bottomRight, bottomLeft});
    };

    if (frame.state == game::TransportState::Covered ||
        frame.scale <= 0.0F) {
        appendColorQuad(0.0F, 0.0F, static_cast<float>(width_),
                        static_cast<float>(height_));
    } else {
        const assets::SpriteAtlas& atlas = hud.transportAtlas;
        const assets::RgbaImage& texture = hud.transportTexture.image();
        if (atlas.frames().empty() || texture.width == 0 ||
            texture.height == 0 || width_ == 0 || height_ == 0) {
            return Result::failure("Transport sprite resources are invalid");
        }
        constexpr float virtualWidth = 480.0F;
        constexpr float virtualHeight = 320.0F;
        const float scaleRateX = static_cast<float>(width_) / virtualWidth;
        const float scaleRateY = static_cast<float>(height_) / virtualHeight;

        float frameWidth = 0.0F;
        float frameHeight = 0.0F;
        for (const assets::SpriteFrameModule& frameModule :
             atlas.modulesForFrame(0)) {
            if (frameModule.moduleIndex >= atlas.modules().size()) {
                return Result::failure(
                    "Transport frame references an invalid module");
            }
            const assets::SpriteModule& module =
                atlas.modules()[frameModule.moduleIndex];
            frameWidth = std::max(
                frameWidth,
                static_cast<float>(frameModule.x) + module.width);
            frameHeight = std::max(
                frameHeight,
                static_cast<float>(frameModule.y) + module.height);
        }
        if (frameWidth <= 0.0F || frameHeight <= 0.0F) {
            return Result::failure("Transport frame zero has no extent");
        }

        // CTransport::Draw (0x0038b608) centers frame zero on the native
        // 480x320 canvas, paints it at CTransport's scalar, then covers the
        // four rectangles outside its scaled bounds. The two-pixel overlap
        // removes sampling seams at the wipe edge.
        const float nativeWidth = std::trunc(frameWidth * frame.scale);
        const float nativeHeight = std::trunc(frameHeight * frame.scale);
        const float originX = 240.0F - std::trunc(nativeWidth / 2.0F);
        const float originY = 160.0F - std::trunc(nativeHeight / 2.0F);
        constexpr std::uint8_t horizontalFlip = 0x01;
        constexpr std::uint8_t verticalFlip = 0x02;
        for (const assets::SpriteFrameModule& frameModule :
             atlas.modulesForFrame(0)) {
            const assets::SpriteModule& module =
                atlas.modules()[frameModule.moduleIndex];
            if (module.imageIndex != 0 ||
                (frameModule.flags & ~(horizontalFlip | verticalFlip)) != 0) {
                return Result::failure(
                    "Transport frame uses an unsupported transform");
            }
            const float left =
                (originX + frameModule.x * frame.scale) * scaleRateX;
            const float top =
                (originY + frameModule.y * frame.scale) * scaleRateY;
            const float right =
                left + module.width * frame.scale * scaleRateX;
            const float bottom =
                top + module.height * frame.scale * scaleRateY;
            const float x0 = left / static_cast<float>(width_) * 2.0F - 1.0F;
            const float x1 = right / static_cast<float>(width_) * 2.0F - 1.0F;
            const float y0 = 1.0F - top / static_cast<float>(height_) * 2.0F;
            const float y1 =
                1.0F - bottom / static_cast<float>(height_) * 2.0F;
            float u0 = static_cast<float>(module.x) / texture.width;
            float v0 = static_cast<float>(module.y) / texture.height;
            float u1 = static_cast<float>(module.x + module.width) /
                       texture.width;
            float v1 = static_cast<float>(module.y + module.height) /
                       texture.height;
            if ((frameModule.flags & horizontalFlip) != 0) {
                std::swap(u0, u1);
            }
            if ((frameModule.flags & verticalFlip) != 0) {
                std::swap(v0, v1);
            }
            const std::uint32_t white = rgbaVertexColor(0xffffffffU);
            const GpuVertex topLeft{{x0, y0, 0.0F}, {}, {u0, v0}, white};
            const GpuVertex topRight{{x1, y0, 0.0F}, {}, {u1, v0}, white};
            const GpuVertex bottomLeft{{x0, y1, 0.0F}, {}, {u0, v1}, white};
            const GpuVertex bottomRight{{x1, y1, 0.0F}, {}, {u1, v1}, white};
            spriteVertices.insert(spriteVertices.end(),
                                  {topLeft, topRight, bottomLeft, topRight,
                                   bottomRight, bottomLeft});
        }

        appendColorQuad(0.0F, 0.0F, static_cast<float>(width_),
                        (originY + 2.0F) * scaleRateY);
        appendColorQuad(0.0F, (originY + nativeHeight - 2.0F) * scaleRateY,
                        static_cast<float>(width_),
                        static_cast<float>(height_));
        appendColorQuad(0.0F, (originY - 2.0F) * scaleRateY,
                        (originX + 2.0F) * scaleRateX,
                        (originY + nativeHeight + 2.0F) * scaleRateY);
        appendColorQuad((originX + nativeWidth - 2.0F) * scaleRateX,
                        (originY - 2.0F) * scaleRateY,
                        static_cast<float>(width_),
                        (originY + nativeHeight + 2.0F) * scaleRateY);
    }

    const auto createVertexBuffer = [this](
        std::span<const GpuVertex> vertices,
        Microsoft::WRL::ComPtr<ID3D11Buffer>& buffer,
        std::uint32_t& count, std::string_view name) -> Result {
        if (vertices.empty()) {
            return Result::success();
        }
        D3D11_BUFFER_DESC description{};
        description.ByteWidth =
            static_cast<UINT>(vertices.size_bytes());
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA data{vertices.data(), 0, 0};
        const HRESULT createResult =
            device_->CreateBuffer(&description, &data, &buffer);
        if (FAILED(createResult)) {
            return hresultFailure(
                std::string("ID3D11Device::CreateBuffer(") +
                    std::string(name) + ")",
                createResult);
        }
        count = static_cast<std::uint32_t>(vertices.size());
        return Result::success();
    };
    Result result = createVertexBuffer(
        spriteVertices, transportSpriteVertexBuffer_,
        transportSpriteVertexCount_, "transport sprite");
    if (!result) {
        return result;
    }
    return createVertexBuffer(colorVertices, transportColorVertexBuffer_,
                              transportColorVertexCount_,
                              "transport cover");
}

Result D3D11Renderer::updateDeathConfirmation(
    const game::LevelHudAsset& hud,
    const game::DeathConfirmationFrame& frame) {
    deathConfirmationBatches_.clear();
    deathConfirmationVertexCount_ = 0;
    if (!frame.visible && !frame.loadingVisible) {
        return Result::success();
    }
    if (!device_ || !context_ || width_ == 0 || height_ == 0 ||
        !deathConfirmationBackgroundTexture_ ||
        !deathConfirmationMainMenuTexture_ ||
        !deathConfirmationNormalFontTexture_ ||
        !deathConfirmationOutlineFontTexture_ ||
        !deathConfirmationOutlineBigFontTexture_) {
        return Result::failure(
            "Death confirmation GPU resources are incomplete");
    }

    constexpr float virtualWidth = 480.0F;
    constexpr float virtualHeight = 320.0F;
    const float screenScale =
        std::min(static_cast<float>(width_) / virtualWidth,
                 static_cast<float>(height_) / virtualHeight);
    const float screenOffsetX =
        (static_cast<float>(width_) - virtualWidth * screenScale) * 0.5F;
    const float screenOffsetY =
        (static_cast<float>(height_) - virtualHeight * screenScale) * 0.5F;

    std::vector<GpuVertex> vertices;
    vertices.reserve(384);
    bool valid = true;
    const auto appendFrameModule =
        [&](const assets::SpriteAtlas& atlas,
            const assets::RgbaImage& texture,
            std::size_t frameModuleIndex, float originX, float originY,
            std::uint32_t argb, ConfirmationTexture textureKind) {
            if (frameModuleIndex >= atlas.frameModules().size()) {
                valid = false;
                return;
            }
            const assets::SpriteFrameModule& frameModule =
                atlas.frameModules()[frameModuleIndex];
            if (frameModule.moduleIndex >= atlas.modules().size()) {
                valid = false;
                return;
            }
            const assets::SpriteModule& module =
                atlas.modules()[frameModule.moduleIndex];
            // CSprite frame-module flags returned by GetFModuleFlags
            // (0x002e6114): bit 0 mirrors X and bit 1 mirrors Y. The
            // confirmation Nimbus frame uses both bits in shipped data.
            constexpr std::uint8_t horizontalFlip = 0x01;
            constexpr std::uint8_t verticalFlip = 0x02;
            if (module.imageIndex != 0 ||
                (frameModule.flags &
                 ~(horizontalFlip | verticalFlip)) != 0 ||
                module.width == 0 || module.height == 0) {
                valid = false;
                return;
            }
            const float left =
                screenOffsetX +
                (originX + static_cast<float>(frameModule.x)) * screenScale;
            const float top =
                screenOffsetY +
                (originY + static_cast<float>(frameModule.y)) * screenScale;
            const float right =
                left + static_cast<float>(module.width) * screenScale;
            const float bottom =
                top + static_cast<float>(module.height) * screenScale;
            const float x0 = left / static_cast<float>(width_) * 2.0F - 1.0F;
            const float x1 = right / static_cast<float>(width_) * 2.0F - 1.0F;
            const float y0 = 1.0F - top / static_cast<float>(height_) * 2.0F;
            const float y1 =
                1.0F - bottom / static_cast<float>(height_) * 2.0F;
            float u0 = static_cast<float>(module.x) / texture.width;
            float v0 = static_cast<float>(module.y) / texture.height;
            float u1 = static_cast<float>(module.x + module.width) /
                       texture.width;
            float v1 = static_cast<float>(module.y + module.height) /
                       texture.height;
            if ((frameModule.flags & horizontalFlip) != 0) {
                std::swap(u0, u1);
            }
            if ((frameModule.flags & verticalFlip) != 0) {
                std::swap(v0, v1);
            }
            if (deathConfirmationBatches_.empty() ||
                deathConfirmationBatches_.back().texture != textureKind) {
                deathConfirmationBatches_.push_back(
                    {static_cast<std::uint32_t>(vertices.size()), 0,
                     textureKind});
            }
            const std::uint32_t color = rgbaVertexColor(argb);
            const GpuVertex topLeft{{x0, y0, 0.0F}, {}, {u0, v0}, color};
            const GpuVertex topRight{{x1, y0, 0.0F}, {}, {u1, v0}, color};
            const GpuVertex bottomLeft{{x0, y1, 0.0F}, {}, {u0, v1}, color};
            const GpuVertex bottomRight{{x1, y1, 0.0F}, {}, {u1, v1}, color};
            vertices.insert(vertices.end(),
                            {topLeft, topRight, bottomLeft, topRight,
                             bottomRight, bottomLeft});
            deathConfirmationBatches_.back().vertexCount += 6;
        };
    const auto appendFrame =
        [&](const assets::SpriteAtlas& atlas,
            const assets::RgbaImage& texture, std::size_t frameIndex,
            float originX, float originY, std::uint32_t argb,
            ConfirmationTexture textureKind) {
            if (frameIndex >= atlas.frames().size()) {
                valid = false;
                return;
            }
            const assets::SpriteFrame& spriteFrame =
                atlas.frames()[frameIndex];
            for (std::size_t index = 0; index < spriteFrame.moduleCount;
                 ++index) {
                appendFrameModule(atlas, texture,
                                  spriteFrame.firstModuleIndex + index,
                                  originX, originY, argb, textureKind);
            }
        };

    const auto mapCharacter = [](char16_t character) -> std::size_t {
        // English __CHARACTERS_MAP at 0x0056e4f0, consumed by
        // CFont::GetMap at 0x002e6164.
        if (character >= 0x20 && character <= 0x3f) {
            return static_cast<std::size_t>(character - 0x20);
        }
        if (character == u'@') {
            return 0xa5;
        }
        if (character >= u'A' && character <= u'Z') {
            return 0x20 + static_cast<std::size_t>(character - u'A');
        }
        if (character >= u'[' && character <= u'_') {
            return 0x3b + static_cast<std::size_t>(character - u'[');
        }
        if (character == u'`') {
            return 0x5e;
        }
        if (character >= u'a' && character <= u'z') {
            return 0x3f + static_cast<std::size_t>(character - u'a');
        }
        if (character >= u'{' && character <= u'~') {
            return 0x5b + static_cast<std::size_t>(character - u'{');
        }
        return 0x1f;
    };
    const auto glyphAdvance = [&](const assets::SpriteAtlas& atlas,
                                  char16_t character) -> float {
        const std::size_t mapped = mapCharacter(character);
        if (atlas.frameModules().empty() ||
            mapped >= atlas.frameModules().size()) {
            valid = false;
            return 0.0F;
        }
        const assets::SpriteFrameModule& glyph =
            atlas.frameModules()[mapped];
        if (glyph.moduleIndex >= atlas.modules().size()) {
            valid = false;
            return 0.0F;
        }
        // CFont::GetStringSize (0x002e6238) adds the base fmodule X,
        // glyph module width and glyph fmodule X for each character.
        return static_cast<float>(atlas.frameModules()[0].x) +
               static_cast<float>(atlas.modules()[glyph.moduleIndex].width) +
               static_cast<float>(glyph.x);
    };
    const auto measureLine = [&](const assets::SpriteAtlas& atlas,
                                 std::u16string_view text) -> float {
        if (text.empty() || atlas.frameModules().empty()) {
            return 0.0F;
        }
        float width = 0.0F;
        for (const char16_t character : text) {
            width += glyphAdvance(atlas, character);
        }
        return std::max(
            0.0F,
            width - static_cast<float>(atlas.frameModules()[0].x));
    };
    const auto fontHeight = [&](const assets::SpriteAtlas& atlas) -> float {
        if (atlas.frameModules().empty() || atlas.modules().empty() ||
            atlas.frameModules()[0].moduleIndex >= atlas.modules().size()) {
            valid = false;
            return 0.0F;
        }
        // CFont::GetLineHeight (0x002e6150) gets module height for character
        // fmodule zero and then adds the configured line spacing.
        return static_cast<float>(
            atlas.modules()[atlas.frameModules()[0].moduleIndex].height);
    };
    const auto wrapText = [&](const assets::SpriteAtlas& atlas,
                              std::u16string_view text, float maximumWidth) {
        std::vector<std::u16string> lines;
        std::u16string line;
        std::size_t cursor = 0;
        while (cursor < text.size()) {
            if (text[cursor] == u'\n') {
                lines.push_back(line);
                line.clear();
                ++cursor;
                continue;
            }
            while (cursor < text.size() && text[cursor] == u' ') {
                ++cursor;
            }
            if (cursor >= text.size()) {
                break;
            }
            const std::size_t wordStart = cursor;
            while (cursor < text.size() && text[cursor] != u' ' &&
                   text[cursor] != u'\n') {
                ++cursor;
            }
            const std::u16string word(text.substr(wordStart,
                                                  cursor - wordStart));
            std::u16string candidate = line;
            if (!candidate.empty()) {
                candidate.push_back(u' ');
            }
            candidate += word;
            if (!line.empty() && measureLine(atlas, candidate) >=
                                     maximumWidth) {
                lines.push_back(line);
                line = word;
            } else {
                line = std::move(candidate);
            }
        }
        if (!line.empty() || lines.empty()) {
            lines.push_back(std::move(line));
        }
        return lines;
    };
    const auto appendText =
        [&](const assets::SpriteAtlas& atlas,
            const assets::RgbaImage& texture,
            const std::vector<std::u16string>& lines, float originX,
            float originY, float lineSpacing, std::uint32_t alignment,
            std::uint32_t argb, ConfirmationTexture textureKind) {
            const float lineHeight = fontHeight(atlas) + lineSpacing;
            const float totalHeight =
                lines.empty()
                    ? 0.0F
                    : static_cast<float>(lines.size()) * lineHeight -
                          lineSpacing;
            float top = originY;
            if ((alignment & 0x20U) != 0) {
                top -= totalHeight;
            }
            if ((alignment & 0x10U) != 0) {
                top -= totalHeight * 0.5F;
            }
            for (const std::u16string& line : lines) {
                const float lineWidth = measureLine(atlas, line);
                float left = originX;
                if ((alignment & 0x02U) != 0) {
                    left -= lineWidth;
                }
                if ((alignment & 0x01U) != 0) {
                    left -= lineWidth * 0.5F;
                }
                for (const char16_t character : line) {
                    const std::size_t mapped = mapCharacter(character);
                    appendFrameModule(atlas, texture, mapped, left, top,
                                      argb, textureKind);
                    left += glyphAdvance(atlas, character);
                }
                top += lineHeight;
            }
        };
    const auto appendSingleLineText =
        [&](const assets::SpriteAtlas& atlas,
            const assets::RgbaImage& texture, std::u16string_view text,
            float originX, float originY, std::uint32_t alignment,
            std::uint32_t argb, ConfirmationTexture textureKind) {
            appendText(atlas, texture,
                       std::vector<std::u16string>{std::u16string(text)},
                       originX, originY, 0.0F, alignment, argb, textureKind);
        };
    const auto appendButtonImage = [&](float x, float y, bool selected) {
        const std::size_t animationIndex = selected ? 0x20U : 0x1fU;
        if (animationIndex >= hud.mainMenuAtlas.animations().size()) {
            valid = false;
            return;
        }
        const assets::SpriteAnimation& animation =
            hud.mainMenuAtlas.animations()[animationIndex];
        if (animation.frameCount == 0 ||
            animation.firstFrameIndex >=
                hud.mainMenuAtlas.animationFrames().size()) {
            valid = false;
            return;
        }
        const assets::SpriteAnimationFrame& animationFrame =
            hud.mainMenuAtlas.animationFrames()[animation.firstFrameIndex];
        appendFrame(hud.mainMenuAtlas, hud.mainMenuTexture.image(),
                    animationFrame.frameIndex,
                    x + static_cast<float>(animationFrame.x),
                    y + static_cast<float>(animationFrame.y), 0xffffffffU,
                    ConfirmationTexture::MainMenu);
    };

    if (frame.visible) {
        // Exact render order recovered from GS_Confirmation::Render
        // (0x002dbfa8), gxGameState::RenderTitle (0x002bb67c),
        // RenderMarkBG (0x002bc6d8), and RenderNimbus (0x002bc740).
        appendFrame(hud.mainMenuAtlas, hud.mainMenuTexture.image(), 0x24,
                    240.0F, 160.0F, 0xffffffffU,
                    ConfirmationTexture::MainMenu);
        appendFrame(hud.backgroundSuitAtlas,
                    hud.backgroundSuitTexture.image(), 1, 0.0F, 0.0F,
                    0xffffffffU, ConfirmationTexture::BackgroundSuit);
        appendSingleLineText(
            hud.normalWhiteFontAtlas, hud.normalWhiteFontTexture.image(),
            frame.title, 76.0F, 13.0F, 0x10U, 0xfffffb00U,
            ConfirmationTexture::NormalWhiteFont);
        appendFrame(hud.mainMenuAtlas, hud.mainMenuTexture.image(), 0x44,
                    14.0F, 11.0F, 0xffffffffU,
                    ConfirmationTexture::MainMenu);
        // RenderTitle's CSWTCH_2768 entry for style 1 is frame 0x2c.
        appendFrame(hud.mainMenuAtlas, hud.mainMenuTexture.image(), 0x2c,
                    25.0F, 13.0F, 0xffffffffU,
                    ConfirmationTexture::MainMenu);
        appendFrame(hud.mainMenuAtlas, hud.mainMenuTexture.image(), 0x22,
                    240.0F, 174.0F, 0xffffffffU,
                    ConfirmationTexture::MainMenu);
        appendFrame(hud.mainMenuAtlas, hud.mainMenuTexture.image(), 0x23,
                    240.0F, 174.0F, 0xffffffffU,
                    ConfirmationTexture::MainMenu);
        appendText(hud.normalWhiteFontAtlas,
                   hud.normalWhiteFontTexture.image(),
                   wrapText(hud.normalWhiteFontAtlas, frame.message, 323.0F),
                   240.0F, 120.0F, 10.0F, 0x11U, 0xffffffffU,
                   ConfirmationTexture::NormalWhiteFont);
        appendButtonImage(240.0F, 180.0F, frame.selection == 0);
        appendSingleLineText(
            hud.outlineSmallFontAtlas, hud.outlineSmallFontTexture.image(),
            frame.yes, 240.0F, 180.0F, 0x11U, 0xffffffffU,
            ConfirmationTexture::OutlineSmallFont);
        appendButtonImage(240.0F, 240.0F, frame.selection == 1);
        appendSingleLineText(
            hud.outlineSmallFontAtlas, hud.outlineSmallFontTexture.image(),
            frame.no, 240.0F, 240.0F, 0x11U, 0xffffffffU,
            ConfirmationTexture::OutlineSmallFont);
    }
    if (frame.loadingVisible) {
        // GS_ExitMenu::Render (0x002c0020) uses 3:2 UI item 0x1b,
        // created at (385, 283), then draws Main 0x13 right-aligned and
        // Main 0x26a two virtual pixels to its right.
        appendSingleLineText(
            hud.outlineBigFontAtlas, hud.outlineBigFontTexture.image(),
            frame.loadingLabel, 385.0F, 283.0F, 0x02U, 0xffffffffU,
            ConfirmationTexture::OutlineBigFont);
        appendSingleLineText(
            hud.outlineBigFontAtlas, hud.outlineBigFontTexture.image(),
            frame.loadingSuffix, 387.0F, 283.0F, 0x00U, 0xffffffffU,
            ConfirmationTexture::OutlineBigFont);
    }

    if (!valid) {
        deathConfirmationBatches_.clear();
        return Result::failure(
            "Death confirmation references invalid shipped sprite data");
    }
    if (vertices.size() > std::numeric_limits<std::uint32_t>::max()) {
        deathConfirmationBatches_.clear();
        return Result::failure(
            "Death confirmation vertex count exceeds D3D11 limits");
    }
    if (vertices.size() > deathConfirmationVertexCapacity_) {
        deathConfirmationVertexBuffer_.Reset();
        deathConfirmationVertexCapacity_ = static_cast<std::uint32_t>(
            std::max<std::size_t>(vertices.size(), 384));
        D3D11_BUFFER_DESC description{};
        description.ByteWidth = deathConfirmationVertexCapacity_ *
                                static_cast<UINT>(sizeof(GpuVertex));
        description.Usage = D3D11_USAGE_DYNAMIC;
        description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        const HRESULT createResult = device_->CreateBuffer(
            &description, nullptr, &deathConfirmationVertexBuffer_);
        if (FAILED(createResult)) {
            deathConfirmationVertexCapacity_ = 0;
            deathConfirmationBatches_.clear();
            return hresultFailure(
                "ID3D11Device::CreateBuffer(death confirmation)",
                createResult);
        }
    }
    deathConfirmationVertexCount_ =
        static_cast<std::uint32_t>(vertices.size());
    if (vertices.empty()) {
        return Result::success();
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT mapResult = context_->Map(
        deathConfirmationVertexBuffer_.Get(), 0,
        D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(mapResult)) {
        deathConfirmationBatches_.clear();
        deathConfirmationVertexCount_ = 0;
        return hresultFailure(
            "ID3D11DeviceContext::Map(death confirmation)", mapResult);
    }
    std::memcpy(mapped.pData, vertices.data(),
                vertices.size() * sizeof(GpuVertex));
    context_->Unmap(deathConfirmationVertexBuffer_.Get(), 0);
    return Result::success();
}

Result D3D11Renderer::setCamera(const game::CameraPose& camera) {
    if (!context_ || !transformBuffer_ || !viewRotationBuffer_ || width_ == 0 ||
        height_ == 0 ||
        !std::isfinite(camera.verticalFieldOfViewDegrees) ||
        camera.verticalFieldOfViewDegrees <= 0.0F ||
        camera.verticalFieldOfViewDegrees >= 180.0F ||
        camera.nearPlane <= 0.0F || camera.farPlane <= camera.nearPlane) {
        return Result::failure("Perspective camera values are invalid");
    }
    const DirectX::XMVECTOR position = DirectX::XMVectorSet(
        camera.position.x, camera.position.y, camera.position.z, 1.0F);
    const DirectX::XMVECTOR target = DirectX::XMVectorSet(
        camera.target.x, camera.target.y, camera.target.z, 1.0F);
    const DirectX::XMVECTOR up = DirectX::XMVectorSet(
        camera.up.x, camera.up.y, camera.up.z, 0.0F);
    const DirectX::XMVECTOR direction =
        DirectX::XMVectorSubtract(target, position);
    if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(direction)) <=
            std::numeric_limits<float>::epsilon() ||
        DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(up)) <=
            std::numeric_limits<float>::epsilon()) {
        return Result::failure("Perspective camera direction is invalid");
    }
    const DirectX::XMMATRIX view =
        buildOriginalLookAtMatrix(position, target, up);
    const DirectX::XMVECTOR forward =
        DirectX::XMVector3Normalize(direction);
    const DirectX::XMVECTOR billboardRight = DirectX::XMVector3Normalize(
        DirectX::XMVector3Cross(forward, up));
    const DirectX::XMVECTOR billboardUp =
        DirectX::XMVector3Cross(billboardRight, forward);
    cameraRight_ = {DirectX::XMVectorGetX(billboardRight),
                    DirectX::XMVectorGetY(billboardRight),
                    DirectX::XMVectorGetZ(billboardRight)};
    cameraUp_ = {DirectX::XMVectorGetX(billboardUp),
                 DirectX::XMVectorGetY(billboardUp),
                 DirectX::XMVectorGetZ(billboardUp)};
    cameraPosition_ = camera.position;
    const float verticalFieldOfViewRadians =
        DirectX::XMConvertToRadians(camera.verticalFieldOfViewDegrees);
    const float aspectRatio =
        static_cast<float>(width_) / static_cast<float>(height_);
    const DirectX::XMMATRIX projection = DirectX::XMMatrixPerspectiveFovLH(
        verticalFieldOfViewRadians, aspectRatio, camera.nearPlane,
        camera.farPlane);
    // CFpsSceneManager::drawAll (0x003995dc) saves the active camera far
    // value, sets it to 16000.0f (0x467a0000) for render pass 2/"skyBox",
    // uploads that projection, renders the sky node, and restores the camera.
    // Keep this projection separate so gameplay visibility continues to use
    // the authored 10000 + CameraArea offset far plane.
    constexpr float fpsSkyBoxFarPlane = 16000.0F;
    const DirectX::XMMATRIX skyProjection =
        DirectX::XMMatrixPerspectiveFovLH(verticalFieldOfViewRadians,
                                          aspectRatio, camera.nearPlane,
                                          fpsSkyBoxFarPlane);
    // FpsSkyBoxSceneNode::render (0x0039a998) starts with each BDAE child's
    // authored absolute transform, then adds the active camera X/Y and half
    // its Z before issuing the draw.  Preserve that unusual half-height
    // translation instead of treating this asset as an ordinary centered
    // skybox.
    const DirectX::XMMATRIX skyWorld = DirectX::XMMatrixTranslation(
        camera.position.x, camera.position.y, camera.position.z * 0.5F);
    const DirectX::XMMATRIX skyView = buildOriginalLookAtMatrix(
        DirectX::XMVectorZero(), direction, up);
    updateRoomVisibility(view * projection);
    DirectX::XMStoreFloat4x4(
        &worldViewProjection_,
        DirectX::XMMatrixTranspose(view * projection));
    DirectX::XMStoreFloat4x4(
        &skyViewProjection_,
        DirectX::XMMatrixTranspose(skyWorld * view * skyProjection));
    DirectX::XMStoreFloat4x4(&viewRotation_,
                             DirectX::XMMatrixTranspose(skyView));
    context_->UpdateSubresource(transformBuffer_.Get(), 0, nullptr,
                                &worldViewProjection_, 0, 0);
    context_->UpdateSubresource(viewRotationBuffer_.Get(), 0, nullptr,
                                &viewRotation_, 0, 0);
    return Result::success();
}

Result D3D11Renderer::uploadGeometrySet(
    std::span<const assets::ColladaGeometry> geometries,
    const assets::ColladaMeshFile* materialLibrary,
    std::span<const assets::BtexTexture> textures,
    std::span<const assets::RgbaImage> previewTexture,
    const std::array<float, 16>* transform, bool dynamicVertices,
    bool omitUntexturedMaterials,
    std::string_view additiveTextureNameFragment) {
    if (!device_ || geometries.empty()) {
        return Result::failure("Geometry set is empty or D3D11 is uninitialized");
    }
    GpuMesh gpuMesh;
    DirectX::XMMATRIX meshTransform = DirectX::XMMatrixIdentity();
    DirectX::XMMATRIX normalTransform = DirectX::XMMatrixIdentity();
    if (transform != nullptr) {
        DirectX::XMFLOAT4X4 worldStorage;
        std::copy(transform->begin(), transform->end(),
                  &worldStorage.m[0][0]);
        meshTransform = DirectX::XMLoadFloat4x4(&worldStorage);
        DirectX::XMVECTOR determinant;
        normalTransform = DirectX::XMMatrixTranspose(
            DirectX::XMMatrixInverse(&determinant, meshTransform));
    }

    std::vector<GpuVertex> vertices;
    std::vector<DirectX::XMFLOAT2> lightmapCoordinates;
    std::size_t totalVertexCount = 0;
    for (const assets::ColladaGeometry& geometry : geometries) {
        totalVertexCount += geometry.vertices.size();
    }
    vertices.reserve(totalVertexCount);
    lightmapCoordinates.reserve(totalVertexCount);
    for (const assets::ColladaGeometry& geometry : geometries) {
        for (const assets::ColladaVertex& source : geometry.vertices) {
            DirectX::XMVECTOR position = DirectX::XMVectorSet(
                source.position.x, source.position.y, source.position.z, 1.0F);
            DirectX::XMVECTOR normal = DirectX::XMVectorSet(
                source.normal.x, source.normal.y, source.normal.z, 0.0F);
            if (transform != nullptr) {
                position = DirectX::XMVector3TransformCoord(position,
                                                            meshTransform);
                normal = DirectX::XMVector3Normalize(
                    DirectX::XMVector3TransformNormal(normal, normalTransform));
            }
            vertices.push_back({
                {DirectX::XMVectorGetX(position), DirectX::XMVectorGetY(position),
                 DirectX::XMVectorGetZ(position)},
                {DirectX::XMVectorGetX(normal), DirectX::XMVectorGetY(normal),
                 DirectX::XMVectorGetZ(normal)},
                {source.textureCoordinate[0], source.textureCoordinate[1]},
                rgbaVertexColor(source.color),
            });
            lightmapCoordinates.push_back(
                {source.secondaryTextureCoordinate[0],
                 source.secondaryTextureCoordinate[1]});
        }
    }
    if (vertices.empty()) {
        return Result::failure("Geometry set contains no vertices");
    }
    gpuMesh.bounds.minimum = {vertices.front().position.x,
                              vertices.front().position.y,
                              vertices.front().position.z};
    gpuMesh.bounds.maximum = gpuMesh.bounds.minimum;
    for (std::size_t vertexIndex = 1; vertexIndex < vertices.size();
         ++vertexIndex) {
        const GpuVertex& vertex = vertices[vertexIndex];
        gpuMesh.bounds.minimum.x =
            std::min(gpuMesh.bounds.minimum.x, vertex.position.x);
        gpuMesh.bounds.minimum.y =
            std::min(gpuMesh.bounds.minimum.y, vertex.position.y);
        gpuMesh.bounds.minimum.z =
            std::min(gpuMesh.bounds.minimum.z, vertex.position.z);
        gpuMesh.bounds.maximum.x =
            std::max(gpuMesh.bounds.maximum.x, vertex.position.x);
        gpuMesh.bounds.maximum.y =
            std::max(gpuMesh.bounds.maximum.y, vertex.position.y);
        gpuMesh.bounds.maximum.z =
            std::max(gpuMesh.bounds.maximum.z, vertex.position.z);
    }

    D3D11_BUFFER_DESC vertexDescription{};
    vertexDescription.ByteWidth =
        static_cast<UINT>(vertices.size() * sizeof(GpuVertex));
    vertexDescription.Usage = dynamicVertices ? D3D11_USAGE_DYNAMIC
                                               : D3D11_USAGE_IMMUTABLE;
    vertexDescription.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vertexDescription.CPUAccessFlags =
        dynamicVertices ? D3D11_CPU_ACCESS_WRITE : 0;
    D3D11_SUBRESOURCE_DATA vertexData{vertices.data(), 0, 0};
    HRESULT result = device_->CreateBuffer(&vertexDescription, &vertexData,
                                            &gpuMesh.vertexBuffer);
    if (FAILED(result)) {
        return hresultFailure("ID3D11Device::CreateBuffer(vertices)", result);
    }
    gpuMesh.vertexCount = static_cast<std::uint32_t>(vertices.size());
    gpuMesh.dynamicVertices = dynamicVertices;

    std::vector<bool> transparentTextures(textures.size());
    for (std::size_t textureIndex = 0; textureIndex < textures.size();
         ++textureIndex) {
        transparentTextures[textureIndex] =
            textureHasTransparency(textures[textureIndex]);
    }
    const bool hasLightmapMaterial =
        materialLibrary != nullptr &&
        std::any_of(materialLibrary->materials().begin(),
                    materialLibrary->materials().end(),
                    [&textures](const assets::ColladaMaterial& material) {
                        return material.lightmapImageIndex &&
                               *material.lightmapImageIndex < textures.size();
                    });
    if (materialLibrary != nullptr && renderTraceEnabled()) {
        for (const assets::ColladaSceneNode& node :
             materialLibrary->sceneNodes()) {
            std::cerr << "mesh_scene_node id=" << node.id
                      << " parent=" << node.parentIndex
                      << " position=[" << node.position.x << ','
                      << node.position.y << ',' << node.position.z
                      << "] rotation=[" << node.rotation.x << ','
                      << node.rotation.y << ',' << node.rotation.z << ','
                      << node.rotation.w << "] scale=[" << node.scale.x
                      << ',' << node.scale.y << ',' << node.scale.z
                      << "] geometries=" << node.geometryIndices.size()
                      << '\n';
        }
        for (const assets::ColladaMaterial& material :
             materialLibrary->materials()) {
            if (material.lightmapImageIndex) {
                std::cerr << "lightmap_material id=" << material.id
                          << " image=" << *material.lightmapImageIndex
                          << '\n';
            }
        }
    }
    if (hasLightmapMaterial) {
        D3D11_BUFFER_DESC coordinateDescription{};
        coordinateDescription.ByteWidth = static_cast<UINT>(
            lightmapCoordinates.size() * sizeof(DirectX::XMFLOAT2));
        coordinateDescription.Usage = D3D11_USAGE_IMMUTABLE;
        coordinateDescription.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA coordinateData{
            lightmapCoordinates.data(), 0, 0};
        result = device_->CreateBuffer(
            &coordinateDescription, &coordinateData,
            &gpuMesh.lightmapCoordinateBuffer);
        if (FAILED(result)) {
            return hresultFailure(
                "ID3D11Device::CreateBuffer(lightmap coordinates)", result);
        }
    }
    std::vector<std::uint16_t> indices;
    std::uint32_t baseVertex = 0;
    for (const assets::ColladaGeometry& geometry : geometries) {
        if (renderTraceEnabled()) {
            std::cerr << "mesh_geometry name=" << geometry.name
                      << " bounds=[" << geometry.bounds.minimum.x << ','
                      << geometry.bounds.minimum.y << ','
                      << geometry.bounds.minimum.z << ';'
                      << geometry.bounds.maximum.x << ','
                      << geometry.bounds.maximum.y << ','
                      << geometry.bounds.maximum.z << "] buffers="
                      << geometry.meshBuffers.size();
            for (const assets::ColladaMeshBuffer& source :
                 geometry.meshBuffers) {
                std::cerr << " material=" << source.materialName;
            }
            std::cerr << '\n';
        }
        for (const assets::ColladaMeshBuffer& source : geometry.meshBuffers) {
            DrawBatch batch;
            batch.topology = topologyFor(source.primitive);
            batch.indexCount = static_cast<std::uint32_t>(source.indices.size());
            batch.startIndex = static_cast<std::uint32_t>(indices.size());
            batch.baseVertex = static_cast<std::int32_t>(baseVertex);
            batch.secondaryTextureIndex =
                static_cast<std::uint32_t>(textures.size());
            indices.insert(indices.end(), source.indices.begin(),
                           source.indices.end());
            if (source.primitive == assets::ColladaPrimitive::LineLoop &&
                !source.indices.empty()) {
                indices.push_back(source.indices.front());
                ++batch.indexCount;
            }
            const assets::ColladaMaterial* material =
                materialLibrary == nullptr
                    ? nullptr
                    : materialLibrary->findMaterial(source.materialName);
            if (materialLibrary != nullptr) {
                if (material != nullptr && material->diffuseImageIndex &&
                    *material->diffuseImageIndex < textures.size()) {
                    batch.textureIndex = *material->diffuseImageIndex;
                } else {
                    batch.textureIndex = static_cast<std::uint32_t>(textures.size());
                }
                if (omitUntexturedMaterials &&
                    (material == nullptr || !material->diffuseImageIndex)) {
                    batch.indexCount = 0;
                }
                if (material != nullptr) {
                    batch.effectAmbientColor = material->ambientColor;
                    batch.materialAnimationTarget = material->effectId;
                    if (!batch.materialAnimationTarget.empty() &&
                        batch.materialAnimationTarget.front() == '#') {
                        batch.materialAnimationTarget.erase(0, 1);
                    }
                    batch.alphaTest = material->id.starts_with("alphatest") ||
                                      material->name.starts_with("alphatest");
                    batch.additiveBlend = material->additiveBlend;
                    if (!additiveTextureNameFragment.empty() &&
                        material->diffuseImageIndex &&
                        *material->diffuseImageIndex <
                            materialLibrary->images().size() &&
                        materialLibrary
                                ->images()[*material->diffuseImageIndex]
                                .sourcePath.find(additiveTextureNameFragment) !=
                            std::string::npos) {
                        // CLevel::LoadNextObject (0x003853fc) invokes
                        // SetMaterialAdditiveByTexName (0x00373838) after
                        // constructing CElectricPlatForm. The helper selects
                        // native material type 0x0d and rendering layer 7 for
                        // each scene node whose layer-zero texture name
                        // contains "electric_wr".
                        batch.additiveBlend = true;
                        batch.renderingLayer = 7;
                    }
                    // CMaterial::prepareMaterial (0x0041c750) copies the
                    // serialized material +0x38/+0x3c fields into native
                    // culling flags 0x200/0x400. CCommonGLDriver at
                    // 0x003e7824 maps them to GL_BACK and GL_FRONT.
                    batch.backFaceCulling = material->backFaceCulling;
                    batch.frontFaceCulling = material->frontFaceCulling;
                    batch.alphaBlend =
                        !batch.additiveBlend && !batch.alphaTest &&
                        material->diffuseImageIndex &&
                        *material->diffuseImageIndex <
                            transparentTextures.size() &&
                        (material->transparentAlphaChannel ||
                         transparentTextures[*material->diffuseImageIndex]);
                    if (material->secondaryImageIndex &&
                        *material->secondaryImageIndex < textures.size() &&
                        material->secondaryTextureMode == 0) {
                        batch.secondaryTextureIndex =
                            *material->secondaryImageIndex;
                        batch.reflectionTwoLayer = true;
                    }
                }
            }
            if (source.usesSecondaryTextureCoordinates &&
                material != nullptr && material->lightmapImageIndex &&
                batch.textureIndex < textures.size() &&
                !batch.alphaTest && !batch.alphaBlend &&
                !batch.reflectionTwoLayer &&
                *material->lightmapImageIndex < textures.size()) {
                batch.secondaryTextureIndex = *material->lightmapImageIndex;
                batch.lightmapTwoLayer = true;
                batch.reflectionTwoLayer = false;
            }
            if (batch.additiveBlend) {
                // CMaterial::prepareMaterial (0x0041c750) applies the
                // serialized material +0x1c override after every other
                // renderer selection and chooses native type 0x0d.
                batch.alphaTest = false;
                batch.alphaBlend = false;
                batch.reflectionTwoLayer = false;
                batch.lightmapTwoLayer = false;
            }
            gpuMesh.drawBatches.push_back(batch);
        }
        baseVertex += static_cast<std::uint32_t>(geometry.vertices.size());
    }
    if (indices.empty()) {
        return Result::failure("Geometry set contains no indices");
    }

    D3D11_BUFFER_DESC indexDescription{};
    indexDescription.ByteWidth =
        static_cast<UINT>(indices.size() * sizeof(std::uint16_t));
    indexDescription.Usage = D3D11_USAGE_IMMUTABLE;
    indexDescription.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA indexData{indices.data(), 0, 0};
    result =
        device_->CreateBuffer(&indexDescription, &indexData,
                              &gpuMesh.indexBuffer);
    if (FAILED(result)) {
        return hresultFailure("ID3D11Device::CreateBuffer(indices)", result);
    }

    if (materialLibrary == nullptr) {
        ComPtr<ID3D11ShaderResourceView> view;
        Result textureResult = createTextureView(previewTexture, view);
        if (!textureResult) {
            return textureResult;
        }
        gpuMesh.textures.push_back(std::move(view));
    } else {
        gpuMesh.textures.reserve(textures.size() + 1);
        if (!whiteTexture_) {
            assets::RgbaImage white;
            white.width = 1;
            white.height = 1;
            white.pixels = {255, 255, 255, 255};
            Result textureResult = createTextureView({&white, 1}, whiteTexture_);
            if (!textureResult) {
                return textureResult;
            }
        }
        for (const assets::BtexTexture& texture : textures) {
            if (texture.mipLevels().empty()) {
                gpuMesh.textures.push_back(whiteTexture_);
                continue;
            }
            const auto cached = sharedTextureViews_.find(&texture);
            if (cached != sharedTextureViews_.end()) {
                gpuMesh.textures.push_back(cached->second);
                continue;
            }
            ComPtr<ID3D11ShaderResourceView> view;
            Result textureResult = createTextureView(texture.mipLevels(), view);
            if (!textureResult) {
                return textureResult;
            }
            sharedTextureViews_.emplace(&texture, view);
            gpuMesh.textures.push_back(std::move(view));
        }
        gpuMesh.textures.push_back(whiteTexture_);
    }

    assets::AxisAlignedBounds bounds = geometries.front().bounds;
    for (const assets::ColladaGeometry& geometry : geometries.subspan(1)) {
        bounds.minimum.x = std::min(bounds.minimum.x, geometry.bounds.minimum.x);
        bounds.minimum.y = std::min(bounds.minimum.y, geometry.bounds.minimum.y);
        bounds.minimum.z = std::min(bounds.minimum.z, geometry.bounds.minimum.z);
        bounds.maximum.x = std::max(bounds.maximum.x, geometry.bounds.maximum.x);
        bounds.maximum.y = std::max(bounds.maximum.y, geometry.bounds.maximum.y);
        bounds.maximum.z = std::max(bounds.maximum.z, geometry.bounds.maximum.z);
    }
    const DirectX::XMVECTOR minimum = DirectX::XMVectorSet(
        bounds.minimum.x, bounds.minimum.y, bounds.minimum.z, 1.0F);
    const DirectX::XMVECTOR maximum = DirectX::XMVectorSet(
        bounds.maximum.x, bounds.maximum.y, bounds.maximum.z, 1.0F);
    const DirectX::XMVECTOR center = DirectX::XMVectorScale(
        DirectX::XMVectorAdd(minimum, maximum), 0.5F);
    const DirectX::XMVECTOR extent = DirectX::XMVectorSubtract(maximum, minimum);
    const float largestExtent = std::max(
        {DirectX::XMVectorGetX(extent), DirectX::XMVectorGetY(extent),
         DirectX::XMVectorGetZ(extent), 0.001F});
    const float scale = 2.0F / largestExtent;
    const DirectX::XMMATRIX world =
        DirectX::XMMatrixTranslation(-DirectX::XMVectorGetX(center),
                                     -DirectX::XMVectorGetY(center),
                                     -DirectX::XMVectorGetZ(center)) *
        DirectX::XMMatrixScaling(scale, scale, scale);
    const DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(
        DirectX::XMVectorSet(0.0F, 0.0F, -3.0F, 1.0F),
        DirectX::XMVectorZero(), DirectX::XMVectorSet(0.0F, 1.0F, 0.0F, 0.0F));
    const DirectX::XMMATRIX projection = DirectX::XMMatrixPerspectiveFovLH(
        DirectX::XMConvertToRadians(60.0F),
        static_cast<float>(width_) / static_cast<float>(height_), 0.1F, 100.0F);
    DirectX::XMStoreFloat4x4(
        &worldViewProjection_,
        DirectX::XMMatrixTranspose(world * view * projection));
    DirectX::XMStoreFloat4x4(&viewRotation_,
                             DirectX::XMMatrixIdentity());
    context_->UpdateSubresource(transformBuffer_.Get(), 0, nullptr,
                                &worldViewProjection_, 0, 0);
    context_->UpdateSubresource(viewRotationBuffer_.Get(), 0, nullptr,
                                &viewRotation_, 0, 0);
    gpuMeshes_.push_back(std::move(gpuMesh));
    return Result::success();
}

Result D3D11Renderer::createTextureView(
    std::span<const assets::RgbaImage> mipLevels,
    ComPtr<ID3D11ShaderResourceView>& view) {
    if (mipLevels.empty()) {
        return Result::failure("D3D11 texture has no mip levels");
    }
    const assets::RgbaImage& baseLevel = mipLevels.front();
    if (baseLevel.width == 0 || baseLevel.height == 0) {
        return Result::failure("Preview texture has invalid dimensions");
    }
    std::vector<D3D11_SUBRESOURCE_DATA> textureData;
    textureData.reserve(mipLevels.size());
    std::uint32_t expectedWidth = baseLevel.width;
    std::uint32_t expectedHeight = baseLevel.height;
    for (const assets::RgbaImage& mip : mipLevels) {
        if (mip.width != expectedWidth || mip.height != expectedHeight ||
            mip.pixels.size() !=
                static_cast<std::size_t>(mip.width) * mip.height * 4) {
            return Result::failure("Preview texture mip chain is invalid");
        }
        textureData.push_back({mip.pixels.data(), mip.width * 4, 0});
        expectedWidth = std::max(1U, expectedWidth / 2);
        expectedHeight = std::max(1U, expectedHeight / 2);
    }

    D3D11_TEXTURE2D_DESC textureDescription{};
    textureDescription.Width = baseLevel.width;
    textureDescription.Height = baseLevel.height;
    textureDescription.MipLevels = static_cast<UINT>(mipLevels.size());
    textureDescription.ArraySize = 1;
    textureDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDescription.SampleDesc.Count = 1;
    textureDescription.Usage = D3D11_USAGE_IMMUTABLE;
    textureDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> texture;
    HRESULT result = device_->CreateTexture2D(
        &textureDescription, textureData.data(), &texture);
    if (FAILED(result)) {
        return hresultFailure("ID3D11Device::CreateTexture2D(diffuse)", result);
    }
    result = device_->CreateShaderResourceView(texture.Get(), nullptr, &view);
    if (FAILED(result)) {
        return hresultFailure("ID3D11Device::CreateShaderResourceView", result);
    }
    return Result::success();
}

void D3D11Renderer::renderFrame() {
    constexpr float clearColor[]{0.025F, 0.045F, 0.085F, 1.0F};
    context_->ClearRenderTargetView(renderTarget_.Get(), clearColor);
    context_->ClearDepthStencilView(depthView_.Get(),
                                    D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                                    1.0F, 0);

    if (!gpuMeshes_.empty()) {
        constexpr UINT stride = sizeof(GpuVertex);
        constexpr UINT offset = 0;
        context_->IASetInputLayout(inputLayout_.Get());
        context_->VSSetShader(vertexShader_.Get(), nullptr, 0);
        const std::array<ID3D11Buffer*, 3> vertexBuffers{
            transformBuffer_.Get(), viewRotationBuffer_.Get(),
            textureTransformBuffer_.Get()};
        context_->VSSetConstantBuffers(
            0, static_cast<UINT>(vertexBuffers.size()), vertexBuffers.data());
        context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
        // CFpsSceneManager::drawAll (0x003995dc) renders pass 2/"skyBox"
        // before terrain and solid nodes.  The upload order is kept useful for
        // room indexing, so reproduce the recovered pass order at draw time.
        for (const bool cameraRelativePass : {true, false}) {
            // SetMaterialAdditiveByTexName (0x00373838) moves matching nodes
            // to original rendering layer 7. Draw that layer after the
            // ordinary layer-zero geometry so the electric planes read the
            // completed opaque depth buffer just as the native scene does.
            for (const std::uint32_t renderingLayer : {0U, 7U}) {
                for (const GpuMesh& gpuMesh : gpuMeshes_) {
                    if (gpuMesh.cameraRelative != cameraRelativePass ||
                        !gpuMesh.visible || !gpuMesh.vertexBuffer ||
                        !gpuMesh.indexBuffer || gpuMesh.textures.empty()) {
                        continue;
                    }
                    const DirectX::XMFLOAT4X4& transform =
                        gpuMesh.cameraRelative ? skyViewProjection_
                                               : worldViewProjection_;
                    context_->RSSetState(rasterizerState_.Get());
                    context_->UpdateSubresource(transformBuffer_.Get(), 0,
                                                nullptr, &transform, 0, 0);
                    context_->IASetVertexBuffers(
                        0, 1, gpuMesh.vertexBuffer.GetAddressOf(), &stride,
                        &offset);
                    context_->IASetIndexBuffer(gpuMesh.indexBuffer.Get(),
                                               DXGI_FORMAT_R16_UINT, 0);
                    for (const DrawBatch& batch : gpuMesh.drawBatches) {
                        if (batch.renderingLayer != renderingLayer) {
                            continue;
                        }
                        if (batch.backFaceCulling && batch.frontFaceCulling) {
                            // The original selects GL_FRONT_AND_BACK when both
                            // material flags are set, which rejects every
                            // triangle.
                            continue;
                        }
                        const DirectX::XMFLOAT4 textureTransform{
                            batch.textureOffset[0], batch.textureOffset[1],
                            0.0F, 0.0F};
                        context_->UpdateSubresource(
                            textureTransformBuffer_.Get(), 0, nullptr,
                            &textureTransform, 0, 0);
                        if (batch.lightmapTwoLayer) {
                            const std::array<ID3D11Buffer*, 2> buffers{
                                gpuMesh.vertexBuffer.Get(),
                                gpuMesh.lightmapCoordinateBuffer.Get()};
                            constexpr std::array<UINT, 2> strides{
                                sizeof(GpuVertex), sizeof(DirectX::XMFLOAT2)};
                            constexpr std::array<UINT, 2> offsets{0, 0};
                            context_->IASetInputLayout(
                                lightmapInputLayout_.Get());
                            context_->IASetVertexBuffers(
                                0, static_cast<UINT>(buffers.size()),
                                buffers.data(), strides.data(), offsets.data());
                            context_->VSSetShader(lightmapVertexShader_.Get(),
                                                  nullptr, 0);
                        } else {
                            context_->IASetInputLayout(inputLayout_.Get());
                            context_->IASetVertexBuffers(
                                0, 1, gpuMesh.vertexBuffer.GetAddressOf(),
                                &stride, &offset);
                            context_->VSSetShader(vertexShader_.Get(), nullptr,
                                                  0);
                        }
                        context_->OMSetBlendState(
                            batch.additiveBlend
                                ? additiveBlendState_.Get()
                                : batch.alphaBlend ? alphaBlendState_.Get()
                                                   : nullptr,
                            nullptr, 0xffffffffU);
                        context_->OMSetDepthStencilState(
                            (gpuMesh.cameraRelative || batch.alphaBlend ||
                             batch.additiveBlend)
                                ? depthReadState_.Get()
                                : depthWriteState_.Get(),
                            0);
                        context_->RSSetState(
                            batch.frontFaceCulling
                                ? frontCullRasterizerState_.Get()
                                : batch.backFaceCulling
                                      ? rasterizerState_.Get()
                                      : noCullRasterizerState_.Get());
                        if (batch.effectMaterialMode != 0) {
                            const EffectMaterialConstants constants{
                                DirectX::XMFLOAT4{
                                    batch.effectAmbientColor[0],
                                    batch.effectAmbientColor[1],
                                    batch.effectAmbientColor[2],
                                    batch.effectAmbientColor[3]},
                                DirectX::XMFLOAT4{
                                    static_cast<float>(
                                        batch.effectMaterialMode),
                                    batch.effectColorMask ? 1.0F : 0.0F,
                                    0.0F, 0.0F}};
                            context_->UpdateSubresource(
                                effectMaterialBuffer_.Get(), 0, nullptr,
                                &constants, 0, 0);
                            context_->PSSetConstantBuffers(
                                0, 1, effectMaterialBuffer_.GetAddressOf());
                        }
                        context_->PSSetShader(
                            batch.lightmapTwoLayer
                                ? (batch.alphaTest
                                       ? lightmapAlphaTestPixelShader_.Get()
                                       : lightmapPixelShader_.Get())
                                : batch.reflectionTwoLayer
                                      ? reflectionPixelShader_.Get()
                                      : batch.alphaTest
                                            ? alphaTestPixelShader_.Get()
                                            : batch.effectMaterialMode != 0
                                                  ? effectColorMaskPixelShader_
                                                        .Get()
                                                  : pixelShader_.Get(),
                            nullptr, 0);
                        const std::array<ID3D11ShaderResourceView*, 2>
                            textureViews{
                                gpuMesh.textures[batch.textureIndex].Get(),
                                (batch.reflectionTwoLayer ||
                                 batch.lightmapTwoLayer)
                                    ? gpuMesh
                                          .textures[batch.secondaryTextureIndex]
                                          .Get()
                                    : nullptr};
                        context_->PSSetShaderResources(
                            0, static_cast<UINT>(textureViews.size()),
                            textureViews.data());
                        context_->IASetPrimitiveTopology(batch.topology);
                        context_->DrawIndexed(batch.indexCount,
                                              batch.startIndex,
                                              batch.baseVertex);
                    }
                }
            }
        }
        const DirectX::XMFLOAT4 identityTextureTransform{};
        context_->UpdateSubresource(textureTransformBuffer_.Get(), 0, nullptr,
                                    &identityTextureTransform, 0, 0);
    }

    if (webLineVertexCount_ != 0 && webLineVertexBuffer_ && webLineTexture_) {
        constexpr UINT stride = sizeof(GpuVertex);
        constexpr UINT offset = 0;
        context_->UpdateSubresource(transformBuffer_.Get(), 0, nullptr,
                                    &worldViewProjection_, 0, 0);
        context_->IASetInputLayout(inputLayout_.Get());
        context_->IASetVertexBuffers(0, 1,
                                     webLineVertexBuffer_.GetAddressOf(),
                                     &stride, &offset);
        context_->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
        context_->IASetPrimitiveTopology(
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(vertexShader_.Get(), nullptr, 0);
        const std::array<ID3D11Buffer*, 2> vertexBuffers{
            transformBuffer_.Get(), viewRotationBuffer_.Get()};
        context_->VSSetConstantBuffers(
            0, static_cast<UINT>(vertexBuffers.size()), vertexBuffers.data());
        context_->PSSetShader(pixelShader_.Get(), nullptr, 0);
        context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
        context_->PSSetShaderResources(0, 1, webLineTexture_.GetAddressOf());
        context_->OMSetBlendState(alphaBlendState_.Get(), nullptr,
                                  0xffffffffU);
        context_->OMSetDepthStencilState(depthReadState_.Get(), 0);
        context_->RSSetState(rasterizerState_.Get());
        context_->Draw(webLineVertexCount_, 0);
    }

    if (enemyGunLineVertexCount_ != 0 && enemyGunLineVertexBuffer_) {
        constexpr UINT stride = sizeof(GpuVertex);
        constexpr UINT offset = 0;
        context_->UpdateSubresource(transformBuffer_.Get(), 0, nullptr,
                                    &worldViewProjection_, 0, 0);
        context_->IASetInputLayout(inputLayout_.Get());
        context_->IASetVertexBuffers(
            0, 1, enemyGunLineVertexBuffer_.GetAddressOf(), &stride, &offset);
        context_->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        context_->VSSetShader(vertexShader_.Get(), nullptr, 0);
        const std::array<ID3D11Buffer*, 2> vertexBuffers{
            transformBuffer_.Get(), viewRotationBuffer_.Get()};
        context_->VSSetConstantBuffers(
            0, static_cast<UINT>(vertexBuffers.size()), vertexBuffers.data());
        context_->PSSetShader(colorPixelShader_.Get(), nullptr, 0);
        context_->OMSetBlendState(alphaBlendState_.Get(), nullptr,
                                  0xffffffffU);
        context_->OMSetDepthStencilState(depthReadState_.Get(), 0);
        context_->RSSetState(noCullRasterizerState_.Get());
        context_->Draw(enemyGunLineVertexCount_, 0);
    }

    if ((effectAlphaVertexCount_ != 0 ||
         effectAdditiveVertexCount_ != 0) &&
        effectVertexBuffer_ && effectTexture_) {
        constexpr UINT stride = sizeof(GpuVertex);
        constexpr UINT offset = 0;
        context_->UpdateSubresource(transformBuffer_.Get(), 0, nullptr,
                                    &worldViewProjection_, 0, 0);
        context_->IASetInputLayout(inputLayout_.Get());
        context_->IASetVertexBuffers(0, 1,
                                     effectVertexBuffer_.GetAddressOf(),
                                     &stride, &offset);
        context_->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(vertexShader_.Get(), nullptr, 0);
        const std::array<ID3D11Buffer*, 2> vertexBuffers{
            transformBuffer_.Get(), viewRotationBuffer_.Get()};
        context_->VSSetConstantBuffers(
            0, static_cast<UINT>(vertexBuffers.size()), vertexBuffers.data());
        context_->PSSetShader(effectPixelShader_.Get(), nullptr, 0);
        context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
        context_->PSSetShaderResources(0, 1, effectTexture_.GetAddressOf());
        context_->OMSetDepthStencilState(depthReadState_.Get(), 0);
        context_->RSSetState(noCullRasterizerState_.Get());
        if (effectAlphaVertexCount_ != 0) {
            context_->OMSetBlendState(alphaBlendState_.Get(), nullptr,
                                      0xffffffffU);
            context_->Draw(effectAlphaVertexCount_, 0);
        }
        if (effectAdditiveVertexCount_ != 0) {
            context_->OMSetBlendState(additiveBlendState_.Get(), nullptr,
                                      0xffffffffU);
            context_->Draw(effectAdditiveVertexCount_,
                           effectAlphaVertexCount_);
        }
    }

    if (hintVertexCount_ != 0 && hintVertexBuffer_ && hintTexture_) {
        constexpr UINT stride = sizeof(GpuVertex);
        constexpr UINT offset = 0;
        context_->UpdateSubresource(transformBuffer_.Get(), 0, nullptr,
                                    &worldViewProjection_, 0, 0);
        context_->IASetInputLayout(inputLayout_.Get());
        context_->IASetVertexBuffers(0, 1,
                                     hintVertexBuffer_.GetAddressOf(),
                                     &stride, &offset);
        context_->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(vertexShader_.Get(), nullptr, 0);
        const std::array<ID3D11Buffer*, 2> vertexBuffers{
            transformBuffer_.Get(), viewRotationBuffer_.Get()};
        context_->VSSetConstantBuffers(
            0, static_cast<UINT>(vertexBuffers.size()), vertexBuffers.data());
        context_->PSSetShader(effectPixelShader_.Get(), nullptr, 0);
        context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
        context_->PSSetShaderResources(0, 1, hintTexture_.GetAddressOf());
        context_->OMSetBlendState(alphaBlendState_.Get(), nullptr,
                                  0xffffffffU);
        context_->OMSetDepthStencilState(depthReadState_.Get(), 0);
        context_->RSSetState(noCullRasterizerState_.Get());
        context_->Draw(hintVertexCount_, 0);
    }

    if (hudVertexCount_ != 0 && hudVertexBuffer_ && hudTexture_) {
        constexpr UINT stride = sizeof(GpuVertex);
        constexpr UINT offset = 0;
        context_->IASetInputLayout(inputLayout_.Get());
        context_->IASetVertexBuffers(0, 1, hudVertexBuffer_.GetAddressOf(),
                                     &stride, &offset);
        context_->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(hudVertexShader_.Get(), nullptr, 0);
        context_->PSSetShader(hudPixelShader_.Get(), nullptr, 0);
        context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
        context_->PSSetShaderResources(0, 1, hudTexture_.GetAddressOf());
        context_->OMSetBlendState(alphaBlendState_.Get(), nullptr,
                                  0xffffffffU);
        context_->OMSetDepthStencilState(depthDisabledState_.Get(), 0);
        context_->RSSetState(noCullRasterizerState_.Get());
        context_->Draw(hudVertexCount_, 0);
    }

    if (cinematicUiColorVertexCount_ != 0 &&
        cinematicUiColorVertexBuffer_) {
        constexpr UINT stride = sizeof(GpuVertex);
        constexpr UINT offset = 0;
        context_->IASetInputLayout(inputLayout_.Get());
        context_->IASetVertexBuffers(
            0, 1, cinematicUiColorVertexBuffer_.GetAddressOf(), &stride,
            &offset);
        context_->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(hudVertexShader_.Get(), nullptr, 0);
        context_->PSSetShader(hudColorPixelShader_.Get(), nullptr, 0);
        context_->OMSetBlendState(alphaBlendState_.Get(), nullptr,
                                  0xffffffffU);
        context_->OMSetDepthStencilState(depthDisabledState_.Get(), 0);
        context_->RSSetState(noCullRasterizerState_.Get());
        context_->Draw(cinematicUiColorVertexCount_, 0);
    }
    if (deathConfirmationVertexCount_ != 0 &&
        deathConfirmationVertexBuffer_) {
        constexpr UINT stride = sizeof(GpuVertex);
        constexpr UINT offset = 0;
        context_->IASetInputLayout(inputLayout_.Get());
        context_->IASetVertexBuffers(
            0, 1, deathConfirmationVertexBuffer_.GetAddressOf(), &stride,
            &offset);
        context_->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
        context_->IASetPrimitiveTopology(
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(hudVertexShader_.Get(), nullptr, 0);
        context_->PSSetShader(hudPixelShader_.Get(), nullptr, 0);
        context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
        context_->OMSetBlendState(alphaBlendState_.Get(), nullptr,
                                  0xffffffffU);
        context_->OMSetDepthStencilState(depthDisabledState_.Get(), 0);
        context_->RSSetState(noCullRasterizerState_.Get());
        for (const ConfirmationDrawBatch& batch :
             deathConfirmationBatches_) {
            ID3D11ShaderResourceView* texture = nullptr;
            switch (batch.texture) {
            case ConfirmationTexture::BackgroundSuit:
                texture = deathConfirmationBackgroundTexture_.Get();
                break;
            case ConfirmationTexture::MainMenu:
                texture = deathConfirmationMainMenuTexture_.Get();
                break;
            case ConfirmationTexture::NormalWhiteFont:
                texture = deathConfirmationNormalFontTexture_.Get();
                break;
            case ConfirmationTexture::OutlineSmallFont:
                texture = deathConfirmationOutlineFontTexture_.Get();
                break;
            case ConfirmationTexture::OutlineBigFont:
                texture = deathConfirmationOutlineBigFontTexture_.Get();
                break;
            }
            context_->PSSetShaderResources(0, 1, &texture);
            context_->Draw(batch.vertexCount, batch.startVertex);
        }
    }
    const auto drawCinematicSpriteBuffer =
        [this](ID3D11Buffer* vertexBuffer, std::uint32_t vertexCount,
               ID3D11ShaderResourceView* texture) {
            if (vertexBuffer == nullptr || vertexCount == 0 ||
                texture == nullptr) {
                return;
            }
            constexpr UINT stride = sizeof(GpuVertex);
            constexpr UINT offset = 0;
            context_->IASetInputLayout(inputLayout_.Get());
            context_->IASetVertexBuffers(0, 1, &vertexBuffer, &stride,
                                         &offset);
            context_->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
            context_->IASetPrimitiveTopology(
                D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            context_->VSSetShader(hudVertexShader_.Get(), nullptr, 0);
            context_->PSSetShader(hudPixelShader_.Get(), nullptr, 0);
            context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
            context_->PSSetShaderResources(0, 1, &texture);
            context_->OMSetBlendState(alphaBlendState_.Get(), nullptr,
                                      0xffffffffU);
            context_->OMSetDepthStencilState(depthDisabledState_.Get(), 0);
            context_->RSSetState(noCullRasterizerState_.Get());
            context_->Draw(vertexCount, 0);
        };
    drawCinematicSpriteBuffer(
        cinematicUiMessagePanelVertexBuffer_.Get(),
        cinematicUiMessagePanelVertexCount_,
        cinematicUiTutorialTexture_.Get());
    drawCinematicSpriteBuffer(
        cinematicUiMessageIconVertexBuffer_.Get(),
        cinematicUiMessageIconVertexCount_, whiteTexture_.Get());
    drawCinematicSpriteBuffer(
        cinematicUiControllerIconVertexBuffer_.Get(),
        cinematicUiControllerIconVertexCount_,
        cinematicUiControllerTexture_.Get());
    drawCinematicSpriteBuffer(
        cinematicUiMessageTextVertexBuffer_.Get(),
        cinematicUiMessageTextVertexCount_,
        deathConfirmationNormalFontTexture_.Get());
    if (cinematicUiTextVertexCount_ != 0 && cinematicUiTextVertexBuffer_ &&
        cinematicUiTextTexture_) {
        constexpr UINT stride = sizeof(GpuVertex);
        constexpr UINT offset = 0;
        context_->IASetInputLayout(inputLayout_.Get());
        context_->IASetVertexBuffers(
            0, 1, cinematicUiTextVertexBuffer_.GetAddressOf(), &stride,
            &offset);
        context_->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
        context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(hudVertexShader_.Get(), nullptr, 0);
        context_->PSSetShader(hudPixelShader_.Get(), nullptr, 0);
        context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
        context_->PSSetShaderResources(
            0, 1, cinematicUiTextTexture_.GetAddressOf());
        context_->OMSetBlendState(alphaBlendState_.Get(), nullptr,
                                  0xffffffffU);
        context_->OMSetDepthStencilState(depthDisabledState_.Get(), 0);
        context_->RSSetState(noCullRasterizerState_.Get());
        context_->Draw(cinematicUiTextVertexCount_, 0);
    }

    // CLevel::Draw paints CTransport after the world and normal 2D
    // interface. Paint the scaled logo first, then the native four-rectangle
    // black cover so transparent pixels inside its bounds retain the scene.
    drawCinematicSpriteBuffer(transportSpriteVertexBuffer_.Get(),
                              transportSpriteVertexCount_,
                              transportTexture_.Get());
    if (transportColorVertexCount_ != 0 && transportColorVertexBuffer_) {
        constexpr UINT stride = sizeof(GpuVertex);
        constexpr UINT offset = 0;
        context_->IASetInputLayout(inputLayout_.Get());
        context_->IASetVertexBuffers(
            0, 1, transportColorVertexBuffer_.GetAddressOf(), &stride,
            &offset);
        context_->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
        context_->IASetPrimitiveTopology(
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context_->VSSetShader(hudVertexShader_.Get(), nullptr, 0);
        context_->PSSetShader(hudColorPixelShader_.Get(), nullptr, 0);
        context_->OMSetBlendState(alphaBlendState_.Get(), nullptr,
                                  0xffffffffU);
        context_->OMSetDepthStencilState(depthDisabledState_.Get(), 0);
        context_->RSSetState(noCullRasterizerState_.Get());
        context_->Draw(transportColorVertexCount_, 0);
    }

    if (swapChain_) {
        swapChain_->Present(1, 0);
    }
}

Result D3D11Renderer::readBackPixel(
    std::uint32_t x, std::uint32_t y,
    std::array<std::uint8_t, 4>& rgba) const {
    if (x >= width_ || y >= height_) {
        return Result::failure("D3D11 readback coordinates are invalid");
    }

    assets::RgbaImage image;
    Result result = readBackImage(image);
    if (!result) {
        return result;
    }
    const std::size_t pixelOffset =
        (static_cast<std::size_t>(y) * image.width + x) * 4;
    std::copy_n(image.pixels.data() + pixelOffset, rgba.size(), rgba.begin());
    return Result::success();
}

Result D3D11Renderer::readBackImage(assets::RgbaImage& image) const {
    if (!device_ || !context_ || !colorTarget_) {
        return Result::failure("D3D11 renderer has no color target to read");
    }

    D3D11_TEXTURE2D_DESC description{};
    colorTarget_->GetDesc(&description);
    description.Usage = D3D11_USAGE_STAGING;
    description.BindFlags = 0;
    description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    description.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    HRESULT callResult = device_->CreateTexture2D(&description, nullptr, &staging);
    if (FAILED(callResult)) {
        return hresultFailure("ID3D11Device::CreateTexture2D(staging)", callResult);
    }
    context_->CopyResource(staging.Get(), colorTarget_.Get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    callResult = context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(callResult)) {
        return hresultFailure("ID3D11DeviceContext::Map", callResult);
    }

    image.width = width_;
    image.height = height_;
    image.pixels.resize(static_cast<std::size_t>(width_) * height_ * 4);
    const auto* source = static_cast<const std::uint8_t*>(mapped.pData);
    for (std::uint32_t row = 0; row < height_; ++row) {
        std::memcpy(image.pixels.data() + static_cast<std::size_t>(row) * width_ * 4,
                    source + static_cast<std::size_t>(row) * mapped.RowPitch,
                    static_cast<std::size_t>(width_) * 4);
    }
    context_->Unmap(staging.Get(), 0);
    return Result::success();
}

} // namespace usm::renderer
