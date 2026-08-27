#include "RetroFPS/Rendering/ScenePostProcessRenderer.hpp"

#include <KamataEngine.h>

#include <Windows.h>
#include <d3dcompiler.h>
#include <d3dx12.h>
#include <wrl.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace fps {
namespace {

using Microsoft::WRL::ComPtr;

constexpr DXGI_FORMAT kSceneResourceFormat = DXGI_FORMAT_R8G8B8A8_TYPELESS;
constexpr DXGI_FORMAT kSceneViewFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
constexpr DXGI_FORMAT kSceneDepthFormat = DXGI_FORMAT_D32_FLOAT;
constexpr std::array<float, 4> kClearColor = {0.0f, 0.0f, 0.0f, 1.0f};

[[noreturn]] void ThrowFailure(const HRESULT result, const char* operation) {
    std::ostringstream message;
    message << operation << " failed with HRESULT 0x" << std::hex
            << static_cast<std::uint32_t>(result) << '.';
    throw std::runtime_error(message.str());
}

void RequireSuccess(const HRESULT result, const char* operation) {
    if (FAILED(result)) {
        ThrowFailure(result, operation);
    }
}

[[nodiscard]] ComPtr<ID3DBlob> CompileShader(
    const wchar_t* path,
    const char* target) {
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    ComPtr<ID3DBlob> byteCode;
    ComPtr<ID3DBlob> diagnostics;
    const HRESULT result = D3DCompileFromFile(
        path,
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "main",
        target,
        flags,
        0,
        &byteCode,
        &diagnostics);
    if (FAILED(result)) {
        std::string message = "Shader compilation failed";
        if (diagnostics && diagnostics->GetBufferPointer() != nullptr) {
            message += ": ";
            message.append(
                static_cast<const char*>(diagnostics->GetBufferPointer()),
                diagnostics->GetBufferSize());
        }
        throw std::runtime_error(message);
    }
    return byteCode;
}

void Transition(
    ID3D12GraphicsCommandList& commandList,
    ID3D12Resource& resource,
    const D3D12_RESOURCE_STATES before,
    const D3D12_RESOURCE_STATES after) {
    const D3D12_RESOURCE_BARRIER barrier =
        CD3DX12_RESOURCE_BARRIER::Transition(&resource, before, after);
    commandList.ResourceBarrier(1, &barrier);
}

} // namespace

struct ScenePostProcessRenderer::Impl final {
    KamataEngine::DirectXCommon* directX = nullptr;
    ScenePostProcessSettings settings{};
    ComPtr<ID3D12Resource> sceneColor;
    ComPtr<ID3D12Resource> sceneDepth;
    ComPtr<ID3D12DescriptorHeap> rtvHeap;
    ComPtr<ID3D12DescriptorHeap> dsvHeap;
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12PipelineState> pipelineState;
    D3D12_VIEWPORT viewport{};
    D3D12_RECT scissor{};
    bool sceneOpen = false;
};

ScenePostProcessRenderer::ScenePostProcessRenderer() noexcept = default;

ScenePostProcessRenderer::~ScenePostProcessRenderer() { Finalize(); }

bool ScenePostProcessRenderer::Initialize(
    const ScenePostProcessSettings& settings,
    std::string& error) {
    Finalize();
    error.clear();
    if (!IsValidScenePostProcessSettings(settings)) {
        error = "Scene post-process brightness and gamma must be finite and greater than zero.";
        return false;
    }

    try {
        auto next = std::make_unique<Impl>();
        next->directX = KamataEngine::DirectXCommon::GetInstance();
        if (next->directX == nullptr || !next->directX->IsInitialized()) {
            error = "DirectX must be initialized before the scene post-process renderer.";
            return false;
        }
        ID3D12Device* const device = next->directX->GetDevice();
        if (device == nullptr) {
            error = "DirectX did not provide a D3D12 device.";
            return false;
        }

        const int width = next->directX->GetBackBufferWidth();
        const int height = next->directX->GetBackBufferHeight();
        if (width <= 0 || height <= 0) {
            error = "Scene post-process target dimensions must be greater than zero.";
            return false;
        }
        const UINT64 resourceWidth = static_cast<UINT64>(width);
        const UINT resourceHeight = static_cast<UINT>(height);

        const D3D12_HEAP_PROPERTIES defaultHeap =
            CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        const D3D12_RESOURCE_DESC colorDescription = CD3DX12_RESOURCE_DESC::Tex2D(
            kSceneResourceFormat,
            resourceWidth,
            resourceHeight,
            1,
            1,
            1,
            0,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        D3D12_CLEAR_VALUE colorClear{};
        colorClear.Format = kSceneViewFormat;
        std::copy(kClearColor.begin(), kClearColor.end(), colorClear.Color);
        RequireSuccess(
            device->CreateCommittedResource(
                &defaultHeap,
                D3D12_HEAP_FLAG_NONE,
                &colorDescription,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                &colorClear,
                IID_PPV_ARGS(&next->sceneColor)),
            "Creating scene color resource");

        const D3D12_RESOURCE_DESC depthDescription = CD3DX12_RESOURCE_DESC::Tex2D(
            kSceneDepthFormat,
            resourceWidth,
            resourceHeight,
            1,
            1,
            1,
            0,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
        D3D12_CLEAR_VALUE depthClear{};
        depthClear.Format = kSceneDepthFormat;
        depthClear.DepthStencil.Depth = 1.0f;
        depthClear.DepthStencil.Stencil = 0;
        RequireSuccess(
            device->CreateCommittedResource(
                &defaultHeap,
                D3D12_HEAP_FLAG_NONE,
                &depthDescription,
                D3D12_RESOURCE_STATE_DEPTH_WRITE,
                &depthClear,
                IID_PPV_ARGS(&next->sceneDepth)),
            "Creating scene depth resource");

        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDescription{};
        rtvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDescription.NumDescriptors = 1;
        RequireSuccess(
            device->CreateDescriptorHeap(&rtvHeapDescription, IID_PPV_ARGS(&next->rtvHeap)),
            "Creating scene RTV heap");

        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDescription{};
        dsvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDescription.NumDescriptors = 1;
        RequireSuccess(
            device->CreateDescriptorHeap(&dsvHeapDescription, IID_PPV_ARGS(&next->dsvHeap)),
            "Creating scene DSV heap");

        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDescription{};
        srvHeapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDescription.NumDescriptors = 1;
        srvHeapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        RequireSuccess(
            device->CreateDescriptorHeap(&srvHeapDescription, IID_PPV_ARGS(&next->srvHeap)),
            "Creating scene SRV heap");

        D3D12_RENDER_TARGET_VIEW_DESC rtvDescription{};
        rtvDescription.Format = kSceneViewFormat;
        rtvDescription.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        device->CreateRenderTargetView(
            next->sceneColor.Get(),
            &rtvDescription,
            next->rtvHeap->GetCPUDescriptorHandleForHeapStart());

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDescription{};
        dsvDescription.Format = kSceneDepthFormat;
        dsvDescription.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        device->CreateDepthStencilView(
            next->sceneDepth.Get(),
            &dsvDescription,
            next->dsvHeap->GetCPUDescriptorHandleForHeapStart());

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDescription{};
        srvDescription.Format = kSceneViewFormat;
        srvDescription.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDescription.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDescription.Texture2D.MostDetailedMip = 0;
        srvDescription.Texture2D.MipLevels = 1;
        srvDescription.Texture2D.ResourceMinLODClamp = 0.0f;
        device->CreateShaderResourceView(
            next->sceneColor.Get(),
            &srvDescription,
            next->srvHeap->GetCPUDescriptorHandleForHeapStart());

        CD3DX12_DESCRIPTOR_RANGE srvRange;
        srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        std::array<CD3DX12_ROOT_PARAMETER, 2> rootParameters{};
        rootParameters[0].InitAsDescriptorTable(
            1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);
        rootParameters[1].InitAsConstants(
            2, 0, 0, D3D12_SHADER_VISIBILITY_PIXEL);

        D3D12_STATIC_SAMPLER_DESC sampler{};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MipLODBias = 0.0f;
        sampler.MaxAnisotropy = 1;
        sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        sampler.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
        sampler.MinLOD = 0.0f;
        sampler.MaxLOD = D3D12_FLOAT32_MAX;
        sampler.ShaderRegister = 0;
        sampler.RegisterSpace = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        const CD3DX12_ROOT_SIGNATURE_DESC rootDescription(
            static_cast<UINT>(rootParameters.size()),
            rootParameters.data(),
            1,
            &sampler,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS);
        ComPtr<ID3DBlob> serializedRoot;
        ComPtr<ID3DBlob> rootDiagnostics;
        const HRESULT rootResult = D3D12SerializeRootSignature(
            &rootDescription,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &serializedRoot,
            &rootDiagnostics);
        if (FAILED(rootResult)) {
            std::string message = "Serializing scene post-process root signature failed";
            if (rootDiagnostics && rootDiagnostics->GetBufferPointer() != nullptr) {
                message += ": ";
                message.append(
                    static_cast<const char*>(rootDiagnostics->GetBufferPointer()),
                    rootDiagnostics->GetBufferSize());
            }
            throw std::runtime_error(message);
        }
        RequireSuccess(
            device->CreateRootSignature(
                0,
                serializedRoot->GetBufferPointer(),
                serializedRoot->GetBufferSize(),
                IID_PPV_ARGS(&next->rootSignature)),
            "Creating scene post-process root signature");

        const ComPtr<ID3DBlob> vertexShader =
            CompileShader(L"Resources/shaders/ScenePostVS.hlsl", "vs_5_0");
        const ComPtr<ID3DBlob> pixelShader =
            CompileShader(L"Resources/shaders/ScenePostPS.hlsl", "ps_5_0");

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDescription{};
        pipelineDescription.pRootSignature = next->rootSignature.Get();
        pipelineDescription.VS = {
            vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()};
        pipelineDescription.PS = {
            pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};
        pipelineDescription.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        pipelineDescription.SampleMask = (std::numeric_limits<UINT>::max)();
        pipelineDescription.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        pipelineDescription.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pipelineDescription.DepthStencilState =
            CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        pipelineDescription.DepthStencilState.DepthEnable = FALSE;
        pipelineDescription.DepthStencilState.StencilEnable = FALSE;
        pipelineDescription.InputLayout = {nullptr, 0};
        pipelineDescription.PrimitiveTopologyType =
            D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipelineDescription.NumRenderTargets = 1;
        pipelineDescription.RTVFormats[0] = kSceneViewFormat;
        // DirectXCommon::SetRenderTargets(true) binds the engine's D32 DSV
        // together with the sRGB backbuffer. The PSO must declare that bound
        // format even though depth testing is disabled for the full-screen pass.
        pipelineDescription.DSVFormat = kSceneDepthFormat;
        pipelineDescription.SampleDesc.Count = 1;
        RequireSuccess(
            device->CreateGraphicsPipelineState(
                &pipelineDescription, IID_PPV_ARGS(&next->pipelineState)),
            "Creating scene post-process pipeline");

        next->settings = settings;
        next->viewport.TopLeftX = 0.0f;
        next->viewport.TopLeftY = 0.0f;
        next->viewport.Width = static_cast<float>(width);
        next->viewport.Height = static_cast<float>(height);
        next->viewport.MinDepth = 0.0f;
        next->viewport.MaxDepth = 1.0f;
        next->scissor = {0, 0, width, height};

        impl_ = std::move(next);
        return true;
    } catch (const std::exception& exception) {
        error = "Failed to initialize scene post-processing: ";
        error += exception.what();
    } catch (...) {
        error = "Failed to initialize scene post-processing because of an unknown error.";
    }

    Finalize();
    return false;
}

void ScenePostProcessRenderer::BeginScene() const {
    if (!impl_) {
        throw std::logic_error("Scene post-process renderer is not initialized.");
    }
    if (impl_->sceneOpen) {
        throw std::logic_error("Scene post-process BeginScene was called twice.");
    }

    ID3D12GraphicsCommandList* const commandList = impl_->directX->GetCommandList();
    if (commandList == nullptr) {
        throw std::logic_error("DirectX did not provide a render command list.");
    }
    Transition(
        *commandList,
        *impl_->sceneColor.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET);

    const D3D12_CPU_DESCRIPTOR_HANDLE rtv =
        impl_->rtvHeap->GetCPUDescriptorHandleForHeapStart();
    const D3D12_CPU_DESCRIPTOR_HANDLE dsv =
        impl_->dsvHeap->GetCPUDescriptorHandleForHeapStart();
    commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    commandList->ClearRenderTargetView(rtv, kClearColor.data(), 0, nullptr);
    commandList->ClearDepthStencilView(
        dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    commandList->RSSetViewports(1, &impl_->viewport);
    commandList->RSSetScissorRects(1, &impl_->scissor);
    impl_->sceneOpen = true;
}

void ScenePostProcessRenderer::Composite() const {
    if (!impl_) {
        throw std::logic_error("Scene post-process renderer is not initialized.");
    }
    if (!impl_->sceneOpen) {
        throw std::logic_error("Scene post-process Composite requires BeginScene first.");
    }

    ID3D12GraphicsCommandList* const commandList = impl_->directX->GetCommandList();
    if (commandList == nullptr) {
        throw std::logic_error("DirectX did not provide a render command list.");
    }
    Transition(
        *commandList,
        *impl_->sceneColor.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    impl_->directX->SetRenderTargets(true);
    commandList->RSSetViewports(1, &impl_->viewport);
    commandList->RSSetScissorRects(1, &impl_->scissor);

    ID3D12DescriptorHeap* const heaps[] = {impl_->srvHeap.Get()};
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetGraphicsRootSignature(impl_->rootSignature.Get());
    commandList->SetPipelineState(impl_->pipelineState.Get());
    commandList->SetGraphicsRootDescriptorTable(
        0, impl_->srvHeap->GetGPUDescriptorHandleForHeapStart());
    const std::array<float, 2> constants = {
        impl_->settings.brightness, impl_->settings.gamma};
    commandList->SetGraphicsRoot32BitConstants(
        1, static_cast<UINT>(constants.size()), constants.data(), 0);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->DrawInstanced(3, 1, 0, 0);
    impl_->sceneOpen = false;
}

void ScenePostProcessRenderer::Finalize() noexcept { impl_.reset(); }

bool ScenePostProcessRenderer::IsInitialized() const noexcept {
    return impl_ != nullptr;
}

} // namespace fps
