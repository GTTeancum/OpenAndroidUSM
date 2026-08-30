#include "renderer/d3d11/D3D11Renderer.hpp"

#include "assets/ColladaSkinning.hpp"

#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
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

Result D3D11Renderer::uploadPreviewGeometry(
    const assets::ColladaGeometry& geometry,
    std::span<const assets::RgbaImage> mipLevels) {
    gpuMeshes_.clear();
    environmentMeshCount_ = 0;
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
    environmentMeshCount_ = 0;
    return uploadGeometrySet(mesh.sceneGeometries(), &mesh, textures, {});
}

Result D3D11Renderer::uploadLevelOneScene(
    const game::LevelOneBootstrap& levelOne) {
    gpuMeshes_.clear();
    environmentMeshCount_ = 0;
    Result result = Result::success();
    for (const game::LevelRoomAsset& room : levelOne.rooms()) {
        result = uploadGeometrySet(room.geometry.sceneGeometries(),
                                   &room.geometry, room.textures, {});
        if (!result) {
            gpuMeshes_.clear();
            return Result::failure("Could not upload " + room.name + ": " +
                                   result.message());
        }
    }
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
        gpuMeshes_.back().visible = actor.animationStartMilliseconds == 0;
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
        gpuMeshes_.back().visible = enemy.visible;
    }
    return uploadHudTexture(levelOne.hud());
}

Result D3D11Renderer::updateLevelOneActors(
    const game::LevelOneBootstrap& levelOne,
    std::uint32_t timestampMilliseconds) {
    if (gpuMeshes_.size() !=
        levelOne.introActors().size() + levelOne.enemies().size() +
            environmentMeshCount_) {
        return Result::failure("Level-one actor GPU resources are incomplete");
    }
    for (std::size_t actorIndex = 0;
         actorIndex < levelOne.introActors().size(); ++actorIndex) {
        const game::CinematicActorAsset& actor =
            levelOne.introActors()[actorIndex];
        GpuMesh& gpuMesh = gpuMeshes_[actorIndex + environmentMeshCount_];
        if (!gpuMesh.dynamicVertices) {
            return Result::failure("Actor vertex buffer is not dynamic");
        }
        gpuMesh.visible =
            timestampMilliseconds >= actor.animationStartMilliseconds;
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
        gpuMesh.visible = enemy.visible;
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
    if (actorIndex + environmentMeshCount_ >= gpuMeshes_.size()) {
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
    GpuMesh& gpuMesh = gpuMeshes_[actorIndex + environmentMeshCount_];
    gpuMesh.visible = true;
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
                                      float webPowerRatio) {
    if (!device_ || !context_ || !hudTexture_ || width_ == 0 || height_ == 0) {
        return Result::failure("HUD GPU resources are incomplete");
    }

    const assets::SpriteAtlas& atlas = hud.interfaceAtlas;
    const assets::RgbaImage& texture = hud.interfaceTexture.image();
    constexpr float virtualWidth = 480.0F;
    constexpr float virtualHeight = 320.0F;
    constexpr float hudX = 46.0F;
    constexpr float hudY = 32.0F;
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
    const auto appendFrame = [&](std::size_t frameIndex, float fillRatio,
                                 float rightClipPixels) {
        const float clampedRatio = std::clamp(fillRatio, 0.0F, 1.0F);
        for (const assets::SpriteFrameModule& frameModule :
             atlas.modulesForFrame(frameIndex)) {
            if (frameModule.moduleIndex >= atlas.modules().size()) {
                valid = false;
                continue;
            }
            const assets::SpriteModule& module =
                atlas.modules()[frameModule.moduleIndex];
            if (module.imageIndex != 0 || frameModule.flags != 0) {
                valid = false;
                continue;
            }
            const float unclippedWidth =
                std::max(0.0F, static_cast<float>(module.width) -
                                   rightClipPixels);
            const float visibleWidth = unclippedWidth * clampedRatio;
            if (visibleWidth <= 0.0F || module.height == 0) {
                continue;
            }

            const float left =
                screenOffsetX +
                (hudX + static_cast<float>(frameModule.x)) * screenScale;
            const float top = screenOffsetY +
                              (hudY + static_cast<float>(frameModule.y)) *
                                  screenScale;
            const float right = left + visibleWidth * screenScale;
            const float bottom =
                top + static_cast<float>(module.height) * screenScale;
            const float x0 = left / static_cast<float>(width_) * 2.0F - 1.0F;
            const float x1 = right / static_cast<float>(width_) * 2.0F - 1.0F;
            const float y0 = 1.0F - top / static_cast<float>(height_) * 2.0F;
            const float y1 =
                1.0F - bottom / static_cast<float>(height_) * 2.0F;

            const float u0 =
                static_cast<float>(module.x) / texture.width;
            const float v0 =
                static_cast<float>(module.y) / texture.height;
            const float u1 =
                (static_cast<float>(module.x) + visibleWidth) / texture.width;
            const float v1 =
                (static_cast<float>(module.y) + module.height) /
                texture.height;
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
    appendFrame(0x1b, 1.0F, 0.0F);
    if (delayedHealthRatio > currentHealthRatio) {
        appendFrame(0x1c, delayedHealthRatio, 0.0F);
    }
    appendFrame(0x1d, currentHealthRatio, 0.0F);
    appendFrame(0x18, 1.0F, 0.0F);
    appendFrame(0x19, webPowerRatio, 5.0F);
    appendFrame(0x1f, 1.0F, 0.0F);
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
    const DirectX::XMMATRIX skyView = buildOriginalLookAtMatrix(
        DirectX::XMVectorZero(), direction, up);
    const DirectX::XMMATRIX projection = DirectX::XMMatrixPerspectiveFovLH(
        DirectX::XMConvertToRadians(camera.verticalFieldOfViewDegrees),
        static_cast<float>(width_) / static_cast<float>(height_),
        camera.nearPlane, camera.farPlane);
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
    const std::array<float, 16>* transform, bool dynamicVertices) {
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
            ComPtr<ID3D11ShaderResourceView> view;
            Result textureResult = createTextureView(texture.mipLevels(), view);
            if (!textureResult) {
                return textureResult;
            }
            gpuMesh.textures.push_back(std::move(view));
        }
        assets::RgbaImage white;
        white.width = 1;
        white.height = 1;
        white.pixels = {255, 255, 255, 255};
        ComPtr<ID3D11ShaderResourceView> view;
        Result textureResult = createTextureView({&white, 1}, view);
        if (!textureResult) {
            return textureResult;
        }
        gpuMesh.textures.push_back(std::move(view));
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
