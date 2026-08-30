#include "renderer/d3d11/D3D11Renderer.hpp"

#include "assets/ColladaSkinning.hpp"

#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>

namespace usm::renderer {
namespace {

using Microsoft::WRL::ComPtr;

struct GpuVertex {
    DirectX::XMFLOAT3 position;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT2 textureCoordinate;
    std::uint32_t color;
};

constexpr std::string_view kVertexShader = R"hlsl(
cbuffer TransformBuffer : register(b0) {
    float4x4 WorldViewProjection;
};
cbuffer ViewRotationBuffer : register(b1) {
    float4x4 ViewRotation;
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
    output.textureCoordinate = input.textureCoordinate;
    output.color = input.color;
    return output;
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
    float lighting = 0.35 + 0.65 * abs(dot(normal,
                                           normalize(float3(0.3, 0.5, -0.8))));
    // GL_COMBINE_RGB = GL_ADD in
    // CCommonGLMaterialRenderer_REFLECTION_2_LAYER::onSetMaterial
    // (original 0x00455be0).
    return float4(saturate(diffuse.rgb * lighting + reflection), diffuse.a);
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
    float3 normal = normalize(input.normal);
    float lighting = 0.35 + 0.65 * abs(dot(normal, normalize(float3(0.3, 0.5, -0.8))));
    return DiffuseTexture.Sample(DiffuseSampler, input.textureCoordinate) *
           input.color * float4(lighting, lighting, lighting, 1.0);
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
    float3 normal = normalize(input.normal);
    float lighting = 0.35 + 0.65 * abs(dot(normal, normalize(float3(0.3, 0.5, -0.8))));
    return diffuse * float4(lighting, lighting, lighting, 1.0);
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

Result renderWindowsText(std::u16string_view text,
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
    RECT bounds{32, 16, static_cast<LONG>(textureWidth - 32),
                static_cast<LONG>(textureHeight - 16)};
    DrawTextW(deviceContext, wideText.c_str(),
              static_cast<int>(wideText.size()), &bounds,
              DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_NOPREFIX);

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
    SelectObject(deviceContext, previousFont);
    SelectObject(deviceContext, previousBitmap);
    DeleteObject(font);
    DeleteObject(bitmap);
    DeleteDC(deviceContext);
    return Result::success();
}

} // namespace

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
    callResult = device_->CreatePixelShader(
        pixelBytecode->GetBufferPointer(), pixelBytecode->GetBufferSize(),
        nullptr, &pixelShader_);
    if (FAILED(callResult)) {
        return hresultFailure("ID3D11Device::CreatePixelShader", callResult);
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
    D3D11_BUFFER_DESC webLineDescription{};
    webLineDescription.ByteWidth = 2U * sizeof(GpuVertex);
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
    rasterizerDescription.CullMode = D3D11_CULL_NONE;
    rasterizerDescription.DepthClipEnable = TRUE;
    callResult = device_->CreateRasterizerState(&rasterizerDescription,
                                                 &rasterizerState_);
    return FAILED(callResult)
               ? hresultFailure("ID3D11Device::CreateRasterizerState", callResult)
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
    forcedVisibleRooms_.fill(false);
    cameraAreaInvisibleRooms_.fill(false);
    cameraAreaVisibleRooms_.fill(false);
    roomVisibility_.fill(true);
    Result result = Result::success();
    for (std::size_t roomIndex = 0; roomIndex < levelOne.rooms().size();
         ++roomIndex) {
        const game::LevelRoomAsset& room = levelOne.rooms()[roomIndex];
        result = uploadGeometrySet(room.geometry.sceneGeometries(),
                                   &room.geometry, room.textures, {});
        if (!result) {
            gpuMeshes_.clear();
            return Result::failure("Could not upload " + room.name + ": " +
                                   result.message());
        }
        gpuMeshes_.back().roomId = static_cast<std::int32_t>(roomIndex + 1);
    }
    roomMeshCount_ = levelOne.rooms().size();
    const game::LevelStaticMeshAsset& sky = levelOne.introSky();
    result = uploadGeometrySet(sky.geometry.sceneGeometries(), &sky.geometry,
                               sky.textures, {});
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
        }
        result = uploadGeometrySet(geometry, &archetype.mesh,
                                   archetype.textures, {},
                                   &object.worldTransform, true, true);
        if (!result) {
            gpuMeshes_.clear();
            return Result::failure("Could not upload level object " +
                                   object.name + ": " + result.message());
        }
        gpuMeshes_.back().roomId = object.roomId;
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
            actor.mesh, actor.animation, 0, animatedGeometry);
        if (!result) {
            gpuMeshes_.clear();
            return Result::failure("Could not evaluate actor " +
                                   actor.sceneNodeName + ": " +
                                   result.message());
        }
        const std::array<float, 16>* actorTransform =
            actor.mesh.skins().empty() ? &actor.worldTransform : nullptr;
        result = uploadGeometrySet(animatedGeometry, &actor.mesh,
                                   actor.textures, {}, actorTransform, true);
        if (!result) {
            gpuMeshes_.clear();
            return Result::failure("Could not upload actor " +
                                   actor.sceneNodeName + ": " +
                                   result.message());
        }
        setMeshVisible(gpuMeshes_.back(),
                       actor.animationStartMilliseconds == 0);
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
                actor.mesh, actor.animation, 0, animatedGeometry);
            if (!result) {
                gpuMeshes_.clear();
                return Result::failure("Could not evaluate cinematic actor " +
                                       actor.sceneNodeName + ": " +
                                       result.message());
            }
            const std::array<float, 16>* actorTransform =
                actor.mesh.skins().empty() ? &actor.worldTransform : nullptr;
            result = uploadGeometrySet(animatedGeometry, &actor.mesh,
                                       actor.textures, {}, actorTransform,
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
        const assets::ColladaAnimationClip* clip =
            archetype.animationBank.findClip(enemy.initialAnimation);
        if (clip == nullptr) {
            gpuMeshes_.clear();
            return Result::failure("Enemy initial animation is missing");
        }
        std::vector<assets::ColladaGeometry> animatedGeometry;
        result = assets::evaluateColladaPose(
            archetype.mesh, archetype.animationBank, clip->startMilliseconds,
            animatedGeometry);
        if (!result) {
            gpuMeshes_.clear();
            return Result::failure("Could not evaluate enemy " + enemy.name +
                                   ": " + result.message());
        }
        result = uploadGeometrySet(animatedGeometry, &archetype.mesh,
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
    return createTextureView(
        std::span<const assets::RgbaImage>(&effectImage, 1), effectTexture_);
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
                                 gameplayCinematicActorCount +
                                 levelOne.enemies().size() +
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
        const std::uint32_t localTime =
            timestampMilliseconds <= actor.animationStartMilliseconds
                ? 0
                : std::min(timestampMilliseconds -
                               actor.animationStartMilliseconds,
                           actor.animation.durationMilliseconds());
        std::vector<assets::ColladaGeometry> animatedGeometry;
        Result result = assets::evaluateColladaPose(
            actor.mesh, actor.animation, localTime, animatedGeometry);
        if (!result) {
            return Result::failure("Could not animate actor " +
                                   actor.sceneNodeName + ": " +
                                   result.message());
        }
        const std::array<float, 16>* actorTransform =
            actor.mesh.skins().empty() ? &actor.worldTransform : nullptr;
        result = updateDynamicMesh(gpuMesh, animatedGeometry, actorTransform);
        if (!result) {
            return Result::failure("Could not update actor " +
                                   actor.sceneNodeName + ": " +
                                   result.message());
        }
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
        const auto playerActor = std::find_if(
            levelOne.introActors().begin(), levelOne.introActors().end(),
            [&levelOne](const game::CinematicActorAsset& actor) {
                return actor.objectId == levelOne.player().objectId;
            });
        if (playerActor == levelOne.introActors().end()) {
            return Result::failure(
                "Persistent player actor is missing during cinematic");
        }
        const std::size_t playerIndex = static_cast<std::size_t>(
            playerActor - levelOne.introActors().begin());
        setMeshVisible(gpuMeshes_[introActorMeshStart_ + playerIndex], false);
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
        const std::uint32_t localTime =
            timestampMilliseconds <= actor.animationStartMilliseconds
                ? 0
                : std::min(timestampMilliseconds -
                               actor.animationStartMilliseconds,
                           actor.animation.durationMilliseconds());
        std::vector<assets::ColladaGeometry> animatedGeometry;
        Result result = assets::evaluateColladaPose(
            actor.mesh, actor.animation, localTime, animatedGeometry);
        if (!result) {
            return Result::failure("Could not animate cinematic actor " +
                                   actor.sceneNodeName + ": " +
                                   result.message());
        }
        const std::array<float, 16>* actorTransform =
            actor.mesh.skins().empty() ? &actor.worldTransform : nullptr;
        result = updateDynamicMesh(gpuMesh, animatedGeometry, actorTransform);
        if (!result) {
            return Result::failure("Could not update cinematic actor " +
                                   actor.sceneNodeName + ": " +
                                   result.message());
        }
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
        const assets::ColladaAnimationClip* clip =
            archetype.animationBank.findClip(enemy.activeAnimation);
        if (clip == nullptr) {
            return Result::failure("Enemy runtime animation is missing");
        }
        const std::uint32_t localTime =
            clip->durationMilliseconds() == 0
                ? 0
                : enemy.animationLoops
                      ? enemy.animationTimeMilliseconds %
                            clip->durationMilliseconds()
                      : std::min(enemy.animationTimeMilliseconds,
                                 clip->durationMilliseconds());
        std::vector<assets::ColladaGeometry> animatedGeometry;
        Result result = assets::evaluateColladaPose(
            archetype.mesh, archetype.animationBank,
            clip->startMilliseconds + localTime, animatedGeometry);
        if (!result) {
            return Result::failure("Could not animate enemy " +
                                   enemy.asset->name + ": " +
                                   result.message());
        }
        GpuMesh& gpuMesh = gpuMeshes_[enemyMeshStart_ + index];
        setMeshVisible(gpuMesh, enemy.visible);
        result = updateDynamicMesh(gpuMesh, animatedGeometry,
                                   &enemy.worldTransform);
        if (!result) {
            return Result::failure("Could not update enemy " +
                                   enemy.asset->name + ": " +
                                   result.message());
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
    }
    return Result::success();
}

Result D3D11Renderer::updateLevelOnePlayer(
    const game::LevelOneBootstrap& levelOne,
    const assets::ColladaAnimationClip& clip,
    std::uint32_t clipTimeMilliseconds,
    const std::array<float, 16>& worldTransform) {
    const auto playerActor = std::find_if(
        levelOne.introActors().begin(), levelOne.introActors().end(),
        [&levelOne](const game::CinematicActorAsset& actor) {
            return actor.objectId == levelOne.player().objectId;
        });
    if (playerActor == levelOne.introActors().end()) {
        return Result::failure("Cinematic player GPU actor was not found");
    }
    const std::size_t actorIndex = static_cast<std::size_t>(
        playerActor - levelOne.introActors().begin());
    if (actorIndex + introActorMeshStart_ >= gpuMeshes_.size()) {
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
    GpuMesh& gpuMesh = gpuMeshes_[actorIndex + introActorMeshStart_];
    setMeshVisible(gpuMesh, true);
    result = updateDynamicMesh(gpuMesh, animatedGeometry, &worldTransform);
    return !result ? Result::failure("Could not update player clip " +
                                     clip.name + ": " + result.message())
                   : Result::success();
}

Result D3D11Renderer::updateWebLine(
    bool visible, const assets::Vector3& anchor,
    const assets::Vector3& attachPosition) {
    webLineVertexCount_ = 0;
    if (!visible) {
        return Result::success();
    }
    const auto finite = [](const assets::Vector3& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) &&
               std::isfinite(value.z);
    };
    const float differenceX = anchor.x - attachPosition.x;
    const float differenceY = anchor.y - attachPosition.y;
    const float differenceZ = anchor.z - attachPosition.z;
    if (!context_ || !webLineVertexBuffer_ || !finite(anchor) ||
        !finite(attachPosition) ||
        differenceX * differenceX + differenceY * differenceY +
                differenceZ * differenceZ <=
            std::numeric_limits<float>::epsilon()) {
        return Result::failure("Web line endpoints are invalid");
    }
    // CobWeb/CTexLineSceneNode uses a bright translucent strand. The native
    // line retains scene depth and alpha while gameplay remains DX-agnostic.
    constexpr std::uint32_t webColor = 0xd9ffffffU;
    const std::array<GpuVertex, 2> vertices{{
        {{attachPosition.x, attachPosition.y, attachPosition.z},
         {0.0F, 0.0F, 1.0F}, {}, webColor},
        {{anchor.x, anchor.y, anchor.z},
         {0.0F, 0.0F, 1.0F}, {}, webColor},
    }};
    D3D11_MAPPED_SUBRESOURCE mapped{};
    const HRESULT mapResult = context_->Map(
        webLineVertexBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(mapResult)) {
        return hresultFailure("ID3D11DeviceContext::Map(web line)",
                              mapResult);
    }
    std::memcpy(mapped.pData, vertices.data(), sizeof(vertices));
    context_->Unmap(webLineVertexBuffer_.Get(), 0);
    webLineVertexCount_ = static_cast<std::uint32_t>(vertices.size());
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
    const game::LevelEffectRuntime& effects) {
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
        appendParticle(particle,
                       particle.additive ? additiveVertices : alphaVertices,
                       valid);
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

Result D3D11Renderer::updateDynamicMesh(
    GpuMesh& gpuMesh,
    std::span<const assets::ColladaGeometry> animatedGeometry,
    const std::array<float, 16>* worldTransform) {
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
            *destination++ = {
                {DirectX::XMVectorGetX(position), DirectX::XMVectorGetY(position),
                 DirectX::XMVectorGetZ(position)},
                {DirectX::XMVectorGetX(normal), DirectX::XMVectorGetY(normal),
                 DirectX::XMVectorGetZ(normal)},
                {source.textureCoordinate[0], source.textureCoordinate[1]},
                rgbaVertexColor(source.color),
            };
        }
    }
    context_->Unmap(gpuMesh.vertexBuffer.Get(), 0);
    return Result::success();
}

Result D3D11Renderer::uploadHudTexture(const game::LevelHudAsset& hud) {
    hudTexture_.Reset();
    hudVertexBuffer_.Reset();
    hudVertexCount_ = 0;
    hudVertexCapacity_ = 0;
    const assets::RgbaImage& image = hud.interfaceTexture.image();
    if (image.width == 0 || image.height == 0 ||
        image.pixels.size() !=
            static_cast<std::size_t>(image.width) * image.height * 4) {
        return Result::failure("Interface texture image is invalid");
    }
    return createTextureView(
        std::span<const assets::RgbaImage>(&image, 1), hudTexture_);
}

Result D3D11Renderer::updatePlayerHud(const game::LevelHudAsset& hud,
                                      float currentHealthRatio,
                                      float delayedHealthRatio,
                                      float webPowerRatio,
                                      const game::LevelEnemyState*
                                          shownHealthBarEnemy) {
    if (!device_ || !context_ || !hudTexture_ || width_ == 0 || height_ == 0) {
        return Result::failure("HUD GPU resources are incomplete");
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
    vertices.reserve(36);
    bool valid = true;
    const auto appendFrame = [&](std::size_t frameIndex, float originX,
                                 float originY, float fillRatio,
                                 float rightClipPixels,
                                 bool fillFromRight = false) {
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
            constexpr std::uint32_t white = 0xffffffffU;
            const GpuVertex topLeft{{x0, y0, 0.0F}, {}, {u0, v0}, white};
            const GpuVertex topRight{{x1, y0, 0.0F}, {}, {u1, v0}, white};
            const GpuVertex bottomLeft{{x0, y1, 0.0F}, {}, {u0, v1}, white};
            const GpuVertex bottomRight{{x1, y1, 0.0F}, {}, {u1, v1}, white};
            vertices.insert(vertices.end(),
                            {topLeft, topRight, bottomLeft, topRight,
                             bottomRight, bottomLeft});
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
    const game::CinematicUiFrame& frame) {
    cinematicUiColorVertexBuffer_.Reset();
    cinematicUiColorVertexCount_ = 0;
    if (!device_) {
        return Result::failure("Cinematic UI has no D3D11 device");
    }

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
        appendColorQuad(-1.0F, 1.0F, 1.0F, -1.0F, 0xb0000000U);
    }
    if (frame.letterboxVisible) {
        appendColorQuad(-1.0F, 1.0F, 1.0F, 0.78F, 0xe0000000U);
        appendColorQuad(-1.0F, -0.78F, 1.0F, -1.0F, 0xe0000000U);
    }
    if (frame.textVisible) {
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
    Result result = renderWindowsText(frame.text, textImage);
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
    const DirectX::XMMATRIX skyView = buildOriginalLookAtMatrix(
        DirectX::XMVectorZero(), direction, up);
    const DirectX::XMMATRIX projection = DirectX::XMMatrixPerspectiveFovLH(
        DirectX::XMConvertToRadians(camera.verticalFieldOfViewDegrees),
        static_cast<float>(width_) / static_cast<float>(height_),
        camera.nearPlane, camera.farPlane);
    updateRoomVisibility(view * projection);
    DirectX::XMStoreFloat4x4(
        &worldViewProjection_,
        DirectX::XMMatrixTranspose(view * projection));
    DirectX::XMStoreFloat4x4(
        &skyViewProjection_,
        DirectX::XMMatrixTranspose(skyView * projection));
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
    bool omitUntexturedMaterials) {
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
    std::size_t totalVertexCount = 0;
    for (const assets::ColladaGeometry& geometry : geometries) {
        totalVertexCount += geometry.vertices.size();
    }
    vertices.reserve(totalVertexCount);
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
    std::vector<std::uint16_t> indices;
    std::uint32_t baseVertex = 0;
    for (const assets::ColladaGeometry& geometry : geometries) {
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
            if (materialLibrary != nullptr) {
                const assets::ColladaMaterial* material =
                    materialLibrary->findMaterial(source.materialName);
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
                    batch.alphaTest = material->id.starts_with("alphatest") ||
                                      material->name.starts_with("alphatest");
                    batch.alphaBlend =
                        !batch.alphaTest && material->diffuseImageIndex &&
                        *material->diffuseImageIndex <
                            transparentTextures.size() &&
                        transparentTextures[*material->diffuseImageIndex];
                    if (material->secondaryImageIndex &&
                        *material->secondaryImageIndex < textures.size() &&
                        material->secondaryTextureMode == 0) {
                        batch.secondaryTextureIndex =
                            *material->secondaryImageIndex;
                        batch.reflectionTwoLayer = true;
                    }
                }
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
        for (const assets::BtexTexture& texture : textures) {
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
        const std::array<ID3D11Buffer*, 2> vertexBuffers{
            transformBuffer_.Get(), viewRotationBuffer_.Get()};
        context_->VSSetConstantBuffers(
            0, static_cast<UINT>(vertexBuffers.size()), vertexBuffers.data());
        context_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
        context_->RSSetState(rasterizerState_.Get());
        for (const GpuMesh& gpuMesh : gpuMeshes_) {
            if (!gpuMesh.visible || !gpuMesh.vertexBuffer ||
                !gpuMesh.indexBuffer ||
                gpuMesh.textures.empty()) {
                continue;
            }
            const DirectX::XMFLOAT4X4& transform =
                gpuMesh.cameraRelative ? skyViewProjection_
                                       : worldViewProjection_;
            context_->UpdateSubresource(transformBuffer_.Get(), 0, nullptr,
                                        &transform, 0, 0);
            context_->IASetVertexBuffers(
                0, 1, gpuMesh.vertexBuffer.GetAddressOf(), &stride, &offset);
            context_->IASetIndexBuffer(gpuMesh.indexBuffer.Get(),
                                       DXGI_FORMAT_R16_UINT, 0);
            for (const DrawBatch& batch : gpuMesh.drawBatches) {
                context_->OMSetBlendState(
                    batch.alphaBlend ? alphaBlendState_.Get() : nullptr,
                    nullptr, 0xffffffffU);
                context_->OMSetDepthStencilState(
                    batch.alphaBlend ? depthReadState_.Get()
                                     : depthWriteState_.Get(),
                    0);
                context_->PSSetShader(
                    batch.reflectionTwoLayer
                        ? reflectionPixelShader_.Get()
                        : batch.alphaTest ? alphaTestPixelShader_.Get()
                                          : pixelShader_.Get(),
                    nullptr, 0);
                const std::array<ID3D11ShaderResourceView*, 2> textureViews{
                    gpuMesh.textures[batch.textureIndex].Get(),
                    batch.reflectionTwoLayer
                        ? gpuMesh.textures[batch.secondaryTextureIndex].Get()
                        : nullptr};
                context_->PSSetShaderResources(
                    0, static_cast<UINT>(textureViews.size()),
                    textureViews.data());
                context_->IASetPrimitiveTopology(batch.topology);
                context_->DrawIndexed(batch.indexCount, batch.startIndex,
                                      batch.baseVertex);
            }
        }
    }

    if (webLineVertexCount_ != 0 && webLineVertexBuffer_) {
        constexpr UINT stride = sizeof(GpuVertex);
        constexpr UINT offset = 0;
        context_->UpdateSubresource(transformBuffer_.Get(), 0, nullptr,
                                    &worldViewProjection_, 0, 0);
        context_->IASetInputLayout(inputLayout_.Get());
        context_->IASetVertexBuffers(0, 1,
                                     webLineVertexBuffer_.GetAddressOf(),
                                     &stride, &offset);
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
        context_->RSSetState(rasterizerState_.Get());
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
        context_->RSSetState(rasterizerState_.Get());
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
        context_->RSSetState(rasterizerState_.Get());
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
        context_->PSSetShader(colorPixelShader_.Get(), nullptr, 0);
        context_->OMSetBlendState(alphaBlendState_.Get(), nullptr,
                                  0xffffffffU);
        context_->OMSetDepthStencilState(depthDisabledState_.Get(), 0);
        context_->RSSetState(rasterizerState_.Get());
        context_->Draw(cinematicUiColorVertexCount_, 0);
    }
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
        context_->RSSetState(rasterizerState_.Get());
        context_->Draw(cinematicUiTextVertexCount_, 0);
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
