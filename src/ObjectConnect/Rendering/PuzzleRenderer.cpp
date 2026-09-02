#include "ObjectConnect/Rendering/PuzzleRenderer.hpp"

#include "ObjectConnect/Math/Vec2.hpp"
#include "ObjectConnect/Rendering/TileMesh.hpp"
#include "ObjectConnect/Tentacle/RibbonStrip.hpp"

#include <2d/Sprite.h>
#include <base/DirectXCommon.h>
#include <base/TextureManager.h>

#include <Windows.h>
#include <d3dcompiler.h>
#include <d3dx12.h>
#include <wrl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace object_connect {
namespace {

using Microsoft::WRL::ComPtr;

constexpr float kCanvasWidth = static_cast<float>(kPuzzleCanvasWidth);
constexpr float kCanvasHeight = static_cast<float>(kPuzzleCanvasHeight);
constexpr float kTileSize = static_cast<float>(kPuzzleTileSize);
constexpr int kCircleSegments = 32;
constexpr float kFleshCoreInset = 4.0f;
constexpr float kFleshPixelSpacing = 12.0f;
constexpr std::size_t kMinimumVertexCapacity = 128;

constexpr Color kInactiveNodeTint{0.34f, 0.30f, 0.33f, 1.0f};
constexpr Color kActivatedNodeTint{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Color kSelectedNodeTint{1.0f, 0.76f, 0.80f, 1.0f};

struct CanvasConstants final {
    float width = kCanvasWidth;
    float height = kCanvasHeight;
    float padding[62]{};
};

static_assert(sizeof(CanvasConstants) == 256);

struct DrawBatch final {
    D3D_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    UINT startVertex = 0;
    UINT vertexCount = 0;
};

struct VertexRange final {
    UINT startVertex = 0;
    UINT vertexCount = 0;
};

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

[[nodiscard]] UINT ToUint(const std::size_t value, const char* label) {
    if (value > (std::numeric_limits<UINT>::max)()) {
        throw std::runtime_error(std::string{label} + " exceeds the Direct3D UINT range.");
    }
    return static_cast<UINT>(value);
}

[[nodiscard]] ComPtr<ID3DBlob> CompileShaderFile(const wchar_t* path,
                                                 const char* target,
                                                 const char* label) {
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
    ComPtr<ID3DBlob> byteCode;
    ComPtr<ID3DBlob> diagnostics;
    const HRESULT result = D3DCompileFromFile(
        path, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main", target,
        flags, 0, &byteCode, &diagnostics);
    if (FAILED(result)) {
        std::string message = std::string{label} + " shader compilation failed";
        if (diagnostics && diagnostics->GetBufferPointer() != nullptr) {
            message += ": ";
            message.append(static_cast<const char*>(diagnostics->GetBufferPointer()),
                           diagnostics->GetBufferSize());
        }
        throw std::runtime_error(message);
    }
    return byteCode;
}

[[nodiscard]] ComPtr<ID3D12Resource> CreateUploadBuffer(ID3D12Device& device,
                                                        const UINT64 size) {
    const D3D12_HEAP_PROPERTIES heap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    const D3D12_RESOURCE_DESC description = CD3DX12_RESOURCE_DESC::Buffer(size);
    ComPtr<ID3D12Resource> resource;
    RequireSuccess(device.CreateCommittedResource(
                       &heap, D3D12_HEAP_FLAG_NONE, &description,
                       D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                       IID_PPV_ARGS(&resource)),
                   "Creating 2D upload buffer");
    return resource;
}

[[nodiscard]] ComPtr<ID3D12RootSignature> CreateFlatRootSignature(
    ID3D12Device& device) {
    CD3DX12_ROOT_PARAMETER rootParameter;
    rootParameter.InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
    const CD3DX12_ROOT_SIGNATURE_DESC description(
        1, &rootParameter, 0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> diagnostics;
    RequireSuccess(D3D12SerializeRootSignature(
                       &description, D3D_ROOT_SIGNATURE_VERSION_1,
                       &serialized, &diagnostics),
                   "Serializing flat 2D root signature");
    ComPtr<ID3D12RootSignature> rootSignature;
    RequireSuccess(device.CreateRootSignature(
                       0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                       IID_PPV_ARGS(&rootSignature)),
                   "Creating flat 2D root signature");
    return rootSignature;
}

[[nodiscard]] ComPtr<ID3D12RootSignature> CreateTileRootSignature(
    ID3D12Device& device) {
    CD3DX12_DESCRIPTOR_RANGE textureRange;
    textureRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    std::array<CD3DX12_ROOT_PARAMETER, 2> parameters;
    parameters[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
    parameters[1].InitAsDescriptorTable(1, &textureRange,
                                        D3D12_SHADER_VISIBILITY_PIXEL);

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
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

    const CD3DX12_ROOT_SIGNATURE_DESC description(
        static_cast<UINT>(parameters.size()), parameters.data(), 1, &sampler,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> diagnostics;
    RequireSuccess(D3D12SerializeRootSignature(
                       &description, D3D_ROOT_SIGNATURE_VERSION_1,
                       &serialized, &diagnostics),
                   "Serializing tile 2D root signature");
    ComPtr<ID3D12RootSignature> rootSignature;
    RequireSuccess(device.CreateRootSignature(
                       0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                       IID_PPV_ARGS(&rootSignature)),
                   "Creating tile 2D root signature");
    return rootSignature;
}

[[nodiscard]] ComPtr<ID3D12PipelineState> CreatePipeline(
    ID3D12Device& device, ID3D12RootSignature& rootSignature,
    ID3DBlob& vertexShader, ID3DBlob& pixelShader,
    const std::span<const D3D12_INPUT_ELEMENT_DESC> inputLayout,
    const char* operation) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
    pipeline.pRootSignature = &rootSignature;
    pipeline.VS = {vertexShader.GetBufferPointer(), vertexShader.GetBufferSize()};
    pipeline.PS = {pixelShader.GetBufferPointer(), pixelShader.GetBufferSize()};
    pipeline.InputLayout = {inputLayout.data(), ToUint(inputLayout.size(), "Input layout")};
    pipeline.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pipeline.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    D3D12_RENDER_TARGET_BLEND_DESC& blend = pipeline.BlendState.RenderTarget[0];
    blend.BlendEnable = TRUE;
    blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOp = D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha = D3D12_BLEND_ONE;
    blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pipeline.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pipeline.DepthStencilState.DepthEnable = FALSE;
    pipeline.DepthStencilState.StencilEnable = FALSE;
    pipeline.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
    pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline.NumRenderTargets = 1;
    pipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    pipeline.SampleDesc.Count = 1;

    ComPtr<ID3D12PipelineState> pipelineState;
    RequireSuccess(device.CreateGraphicsPipelineState(
                       &pipeline, IID_PPV_ARGS(&pipelineState)),
                   operation);
    return pipelineState;
}

template <typename Vertex>
void ResetMappedVertexBuffer(ID3D12Device& device,
                             const std::size_t capacity,
                             ComPtr<ID3D12Resource>& buffer,
                             Vertex*& mapped,
                             D3D12_VERTEX_BUFFER_VIEW& view) {
    if (capacity == 0 ||
        capacity > (std::numeric_limits<UINT>::max)() / sizeof(Vertex)) {
        throw std::runtime_error("2D vertex buffer capacity exceeds Direct3D limits.");
    }
    if (buffer && mapped != nullptr) {
        buffer->Unmap(0, nullptr);
        mapped = nullptr;
    }

    const std::size_t byteCount = capacity * sizeof(Vertex);
    buffer = CreateUploadBuffer(device, static_cast<UINT64>(byteCount));
    RequireSuccess(buffer->Map(0, nullptr, reinterpret_cast<void**>(&mapped)),
                   "Mapping 2D vertex buffer");
    view.BufferLocation = buffer->GetGPUVirtualAddress();
    view.SizeInBytes = static_cast<UINT>(byteCount);
    view.StrideInBytes = static_cast<UINT>(sizeof(Vertex));
}

void AddTriangle(std::vector<RibbonVertex>& vertices,
                 const Vec2 a, const Vec2 b, const Vec2 c,
                 const Color color) {
    vertices.push_back({a, color});
    vertices.push_back({b, color});
    vertices.push_back({c, color});
}

void AddRectangle(std::vector<RibbonVertex>& vertices,
                  const float left, const float top,
                  const float width, const float height,
                  const Color color) {
    const Vec2 topLeft{left, top};
    const Vec2 topRight{left + width, top};
    const Vec2 bottomLeft{left, top + height};
    const Vec2 bottomRight{left + width, top + height};
    AddTriangle(vertices, topLeft, topRight, bottomLeft, color);
    AddTriangle(vertices, bottomLeft, topRight, bottomRight, color);
}

void AddCircle(std::vector<RibbonVertex>& vertices, const Vec2 center,
               const float radius, const Color color) {
    constexpr float kTau = 6.2831853071795864769f;
    for (int index = 0; index < kCircleSegments; ++index) {
        const float firstAngle = kTau * static_cast<float>(index) /
                                 static_cast<float>(kCircleSegments);
        const float secondAngle = kTau * static_cast<float>(index + 1) /
                                  static_cast<float>(kCircleSegments);
        const Vec2 first{center.x + std::cos(firstAngle) * radius,
                         center.y + std::sin(firstAngle) * radius};
        const Vec2 second{center.x + std::cos(secondAngle) * radius,
                          center.y + std::sin(secondAngle) * radius};
        AddTriangle(vertices, center, first, second, color);
    }
}

void AddLine(std::vector<RibbonVertex>& vertices, const Vec2 start,
             const Vec2 end, const float width, const Color color) {
    const Vec2 direction = NormalizeOr(end - start, {1.0f, 0.0f});
    const Vec2 normal = Perpendicular(direction) * (width * 0.5f);
    AddTriangle(vertices, start + normal, end + normal, start - normal, color);
    AddTriangle(vertices, start - normal, end + normal, end - normal, color);
}

[[nodiscard]] float ResolvePixelGridSize(const TentacleStyle& style) noexcept {
    return std::isfinite(style.pixelGridSize)
               ? (std::max)(0.0f, style.pixelGridSize)
               : kDefaultTentaclePixelGridSize;
}

[[nodiscard]] Vec2 SnapToPixelGrid(const Vec2 point,
                                   const float gridSize) noexcept {
    return {
        std::round(point.x / gridSize) * gridSize,
        std::round(point.y / gridSize) * gridSize,
    };
}

[[nodiscard]] std::uint32_t MixBits(std::uint32_t value) noexcept {
    value ^= value >> 16u;
    value *= 0x7FEB352Du;
    value ^= value >> 15u;
    value *= 0x846CA68Bu;
    value ^= value >> 16u;
    return value;
}

[[nodiscard]] TentacleStyle MakeFleshCoreStyle(TentacleStyle style) noexcept {
    const auto insetWidth = [](const float outerWidth) noexcept {
        const float finiteOuter =
            std::isfinite(outerWidth) ? (std::max)(0.0f, outerWidth) : 0.0f;
        return std::clamp(finiteOuter - kFleshCoreInset, 0.0f, finiteOuter);
    };
    style.baseWidth = insetWidth(style.baseWidth);
    style.tipWidth = insetWidth(style.tipWidth);
    return style;
}

void AddFleshPixels(std::vector<RibbonVertex>& vertices,
                    const std::vector<Vec2>& centerline,
                    const TentacleStyle& outerStyle) {
    if (centerline.size() < 2) {
        return;
    }

    const TentacleStyle coreStyle = MakeFleshCoreStyle(outerStyle);
    const float gridSize = ResolvePixelGridSize(coreStyle);
    if (gridSize <= 0.0f) {
        return;
    }
    const float minimumCoreWidth =
        (std::min)(coreStyle.baseWidth, coreStyle.tipWidth);
    if (minimumCoreWidth < gridSize) {
        return;
    }
    const float maximumOffset =
        (std::max)(0.0f, minimumCoreWidth * 0.5f - gridSize);
    const Color highlight = ScaleRgb(coreStyle.color, 1.55f);
    const Color shadow = ScaleRgb(coreStyle.color, 0.48f);
    const float finitePhase = std::isfinite(coreStyle.widthPhase)
                                  ? std::fabs(coreStyle.widthPhase)
                                  : 0.0f;
    const std::uint32_t phaseSeed =
        static_cast<std::uint32_t>(finitePhase * 4096.0f);

    for (std::size_t segmentIndex = 0;
         segmentIndex + 1 < centerline.size(); ++segmentIndex) {
        const Vec2 start = centerline[segmentIndex];
        const Vec2 end = centerline[segmentIndex + 1];
        if (!IsFinite(start) || !IsFinite(end)) {
            continue;
        }

        const Vec2 delta = end - start;
        const float segmentLength = Length(delta);
        if (!std::isfinite(segmentLength) ||
            segmentLength <= kFleshPixelSpacing) {
            continue;
        }
        const Vec2 normal = Perpendicular(NormalizeOr(delta, {1.0f, 0.0f}));
        const std::size_t sampleCount = static_cast<std::size_t>(
            segmentLength / kFleshPixelSpacing);
        for (std::size_t sampleIndex = 1; sampleIndex <= sampleCount;
             ++sampleIndex) {
            const float distance =
                static_cast<float>(sampleIndex) * kFleshPixelSpacing;
            if (distance >= segmentLength) {
                break;
            }

            const std::uint32_t seed =
                phaseSeed ^
                static_cast<std::uint32_t>(segmentIndex * 73856093u) ^
                static_cast<std::uint32_t>(sampleIndex * 19349663u);
            const std::uint32_t hash = MixBits(seed);
            if ((hash & 3u) == 0u) {
                continue;
            }

            const float along = distance / segmentLength;
            const int offsetStep = static_cast<int>((hash >> 8u) % 5u) - 2;
            const float offset = std::clamp(
                static_cast<float>(offsetStep) * gridSize,
                -maximumOffset, maximumOffset);
            const bool widePixel = (hash & 16u) != 0u;
            const float pixelWidth = widePixel ? gridSize * 2.0f : gridSize;
            const Color pixelColor = (hash & 1u) != 0u ? highlight : shadow;
            const Vec2 pixelCenter = start + delta * along + normal * offset;
            const Vec2 pixelCorner = SnapToPixelGrid(
                {pixelCenter.x - pixelWidth * 0.5f,
                 pixelCenter.y - gridSize * 0.5f},
                gridSize);
            AddRectangle(vertices, pixelCorner.x, pixelCorner.y,
                         pixelWidth, gridSize, pixelColor);
        }
    }
}

[[nodiscard]] Vec2 NodeStampOrigin(const NodeDefinition& node) noexcept {
    return {
        static_cast<float>(node.origin.column) * kTileSize,
        static_cast<float>(node.origin.row) * kTileSize,
    };
}

[[nodiscard]] Vec2 NodeStampCenter(const NodeDefinition& node) noexcept {
    return {
        (static_cast<float>(node.bounds.column) +
         static_cast<float>(node.bounds.columns) * 0.5f) * kTileSize,
        (static_cast<float>(node.bounds.row) +
         static_cast<float>(node.bounds.rows) * 0.5f) * kTileSize,
    };
}

[[nodiscard]] float NodePulseRadius(const NodeDefinition& node) noexcept {
    const std::size_t maximumCells =
        (std::max)(node.bounds.columns, node.bounds.rows);
    return static_cast<float>(maximumCells) * kTileSize * 0.5f + 5.0f;
}

[[nodiscard]] std::vector<bool> MakeIndexFlags(
    const std::size_t count, const std::span<const std::size_t> indices) {
    std::vector<bool> flags(count, false);
    for (const std::size_t index : indices) {
        if (index < count) {
            flags[index] = true;
        }
    }
    return flags;
}

class SpriteDrawScope final {
public:
    explicit SpriteDrawScope(ID3D12GraphicsCommandList* const commandList) {
        KamataEngine::Sprite::PreDraw(
            commandList, KamataEngine::Sprite::BlendMode::kNormal);
    }
    ~SpriteDrawScope() { KamataEngine::Sprite::PostDraw(); }

    SpriteDrawScope(const SpriteDrawScope&) = delete;
    SpriteDrawScope& operator=(const SpriteDrawScope&) = delete;
};

} // namespace

struct PuzzleRenderer::Impl final {
    KamataEngine::DirectXCommon* directX = nullptr;
    TilesetDefinition tileset;
    std::uint32_t atlasTextureHandle = 0;
    bool atlasLoaded = false;

    ComPtr<ID3D12RootSignature> flatRootSignature;
    ComPtr<ID3D12PipelineState> flatPipelineState;
    ComPtr<ID3D12Resource> flatVertexBuffer;
    RibbonVertex* mappedFlatVertices = nullptr;
    D3D12_VERTEX_BUFFER_VIEW flatVertexView{};
    std::size_t flatVertexCapacity = 0;

    ComPtr<ID3D12RootSignature> tileRootSignature;
    ComPtr<ID3D12PipelineState> tilePipelineState;
    ComPtr<ID3D12Resource> tileVertexBuffer;
    TileMeshVertex* mappedTileVertices = nullptr;
    D3D12_VERTEX_BUFFER_VIEW tileVertexView{};
    std::size_t tileVertexCapacity = 0;

    ComPtr<ID3D12Resource> constantBuffer;
    CanvasConstants* mappedConstants = nullptr;

    ~Impl() {
        if (flatVertexBuffer && mappedFlatVertices != nullptr) {
            flatVertexBuffer->Unmap(0, nullptr);
        }
        if (tileVertexBuffer && mappedTileVertices != nullptr) {
            tileVertexBuffer->Unmap(0, nullptr);
        }
        if (constantBuffer && mappedConstants != nullptr) {
            constantBuffer->Unmap(0, nullptr);
        }
        if (atlasLoaded) {
            static_cast<void>(
                KamataEngine::TextureManager::Unload(atlasTextureHandle));
        }
    }

    void EnsureFlatCapacity(const std::size_t required) {
        if (required <= flatVertexCapacity) {
            return;
        }
        const std::size_t next =
            GrowTileMeshCapacity(flatVertexCapacity, required);
        ResetMappedVertexBuffer(*directX->GetDevice(), next, flatVertexBuffer,
                                mappedFlatVertices, flatVertexView);
        flatVertexCapacity = next;
    }

    void EnsureTileCapacity(const std::size_t required) {
        if (required <= tileVertexCapacity) {
            return;
        }
        const std::size_t next =
            GrowTileMeshCapacity(tileVertexCapacity, required);
        ResetMappedVertexBuffer(*directX->GetDevice(), next, tileVertexBuffer,
                                mappedTileVertices, tileVertexView);
        tileVertexCapacity = next;
    }

    void DrawFlat(const std::span<const DrawBatch> batches) const {
        if (batches.empty()) {
            return;
        }
        ID3D12GraphicsCommandList* const commandList = directX->GetCommandList();
        commandList->SetGraphicsRootSignature(flatRootSignature.Get());
        commandList->SetPipelineState(flatPipelineState.Get());
        commandList->IASetVertexBuffers(0, 1, &flatVertexView);
        commandList->SetGraphicsRootConstantBufferView(
            0, constantBuffer->GetGPUVirtualAddress());
        for (const DrawBatch& batch : batches) {
            commandList->IASetPrimitiveTopology(batch.topology);
            commandList->DrawInstanced(batch.vertexCount, 1,
                                       batch.startVertex, 0);
        }
    }

    void DrawTiles(const VertexRange range) const {
        if (range.vertexCount == 0) {
            return;
        }
        ID3D12GraphicsCommandList* const commandList = directX->GetCommandList();
        commandList->SetGraphicsRootSignature(tileRootSignature.Get());
        commandList->SetPipelineState(tilePipelineState.Get());
        commandList->IASetVertexBuffers(0, 1, &tileVertexView);
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commandList->SetGraphicsRootConstantBufferView(
            0, constantBuffer->GetGPUVirtualAddress());
        KamataEngine::TextureManager::GetInstance()->SetGraphicsRootDescriptorTable(
            commandList, 1, atlasTextureHandle);
        commandList->DrawInstanced(range.vertexCount, 1, range.startVertex, 0);
    }
};

PuzzleRenderer::PuzzleRenderer() noexcept = default;
PuzzleRenderer::~PuzzleRenderer() { Finalize(); }

bool PuzzleRenderer::Initialize(const std::size_t initialVertexCapacity,
                                const TilesetDefinition& tileset,
                                std::string& error) {
    Finalize();
    error.clear();
    if (initialVertexCapacity < kMinimumVertexCapacity) {
        error = "PuzzleRenderer requires room for at least 128 vertices.";
        return false;
    }
    if (tileset.atlasPath.empty() || tileset.atlasColumns == 0 ||
        tileset.atlasRows == 0) {
        error = "PuzzleRenderer requires one valid tile atlas definition.";
        return false;
    }

    try {
        auto next = std::make_unique<Impl>();
        next->directX = KamataEngine::DirectXCommon::GetInstance();
        if (next->directX == nullptr || !next->directX->IsInitialized()) {
            error = "DirectX must be initialized before PuzzleRenderer.";
            return false;
        }
        ID3D12Device* const device = next->directX->GetDevice();
        if (device == nullptr) {
            error = "KamataEngine did not provide a D3D12 device.";
            return false;
        }
        if (KamataEngine::TextureManager::GetInstance() == nullptr) {
            error = "KamataEngine texture manager is not available.";
            return false;
        }

        next->tileset = tileset;
        next->atlasTextureHandle =
            KamataEngine::TextureManager::Load(tileset.atlasPath);
        next->atlasLoaded = true;

        const ComPtr<ID3DBlob> flatVertexShader = CompileShaderFile(
            L"Resources/shaders/Flat2DVS.hlsl", "vs_5_0", "Flat 2D vertex");
        const ComPtr<ID3DBlob> flatPixelShader = CompileShaderFile(
            L"Resources/shaders/Flat2DPS.hlsl", "ps_5_0", "Flat 2D pixel");
        const ComPtr<ID3DBlob> tileVertexShader = CompileShaderFile(
            L"Resources/shaders/Tile2DVS.hlsl", "vs_5_0", "Tile 2D vertex");
        const ComPtr<ID3DBlob> tilePixelShader = CompileShaderFile(
            L"Resources/shaders/Tile2DPS.hlsl", "ps_5_0", "Tile 2D pixel");

        next->flatRootSignature = CreateFlatRootSignature(*device);
        next->tileRootSignature = CreateTileRootSignature(*device);

        const std::array<D3D12_INPUT_ELEMENT_DESC, 2> flatInputLayout = {{
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
             static_cast<UINT>(offsetof(RibbonVertex, position)),
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
             static_cast<UINT>(offsetof(RibbonVertex, color)),
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        }};
        const std::array<D3D12_INPUT_ELEMENT_DESC, 3> tileInputLayout = {{
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
             static_cast<UINT>(offsetof(TileMeshVertex, position)),
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0,
             static_cast<UINT>(offsetof(TileMeshVertex, uv)),
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,
             static_cast<UINT>(offsetof(TileMeshVertex, tint)),
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        }};

        next->flatPipelineState = CreatePipeline(
            *device, *next->flatRootSignature.Get(), *flatVertexShader.Get(),
            *flatPixelShader.Get(), flatInputLayout, "Creating flat 2D pipeline");
        next->tilePipelineState = CreatePipeline(
            *device, *next->tileRootSignature.Get(), *tileVertexShader.Get(),
            *tilePixelShader.Get(), tileInputLayout, "Creating tile 2D pipeline");

        ResetMappedVertexBuffer(*device, initialVertexCapacity,
                                next->flatVertexBuffer,
                                next->mappedFlatVertices,
                                next->flatVertexView);
        next->flatVertexCapacity = initialVertexCapacity;
        ResetMappedVertexBuffer(*device, initialVertexCapacity,
                                next->tileVertexBuffer,
                                next->mappedTileVertices,
                                next->tileVertexView);
        next->tileVertexCapacity = initialVertexCapacity;

        next->constantBuffer = CreateUploadBuffer(*device, sizeof(CanvasConstants));
        RequireSuccess(next->constantBuffer->Map(
                           0, nullptr,
                           reinterpret_cast<void**>(&next->mappedConstants)),
                       "Mapping 2D canvas constant buffer");
        *next->mappedConstants = {};

        impl_ = std::move(next);
        return true;
    } catch (const std::exception& exception) {
        error = "Failed to initialize PuzzleRenderer: ";
        error += exception.what();
    } catch (...) {
        error = "Failed to initialize PuzzleRenderer because of an unknown error.";
    }
    Finalize();
    return false;
}

void PuzzleRenderer::Draw(const PuzzleDefinition& definition,
                          const PuzzleBoardSnapshot& snapshot,
                          const float elapsedSeconds) {
    if (!impl_) {
        return;
    }

    std::vector<RibbonVertex> flatVertices;
    flatVertices.reserve(8192);
    std::vector<DrawBatch> fallbackBatches;
    std::vector<DrawBatch> hintBatches;
    std::vector<DrawBatch> vesselBatches;
    std::vector<DrawBatch> pulseBatches;

    const auto appendListBatch = [&flatVertices](
                                     std::vector<DrawBatch>& batches,
                                     const std::size_t start) {
        const std::size_t count = flatVertices.size() - start;
        if (count != 0) {
            batches.push_back({D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
                               ToUint(start, "Flat vertex start"),
                               ToUint(count, "Flat vertex count")});
        }
    };
    const auto appendRibbon = [&flatVertices, &vesselBatches](
                                  const std::vector<Vec2>& points,
                                  const TentacleStyle& style) {
        std::vector<RibbonVertex> strip = BuildRibbonStrip(points, style);
        if (strip.size() < 4) {
            return;
        }
        const std::size_t start = flatVertices.size();
        flatVertices.insert(flatVertices.end(), strip.begin(), strip.end());
        vesselBatches.push_back({D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
                                 ToUint(start, "Ribbon vertex start"),
                                 ToUint(strip.size(), "Ribbon vertex count")});
    };

    // Fallback color sits below the authored background grid. The visible
    // gameplay order after it is deliberately kept explicit below.
    std::size_t start = flatVertices.size();
    AddRectangle(flatVertices, 0.0f, 0.0f, kCanvasWidth, kCanvasHeight,
                 definition.backgroundColor);
    appendListBatch(fallbackBatches, start);

    start = flatVertices.size();
    if (definition.showTargetConnections) {
        const Color hint = WithAlpha(definition.vesselColor, 0.16f);
        for (const ConnectionDefinition& connection : definition.connections) {
            if (connection.fromNodeIndex >= definition.nodes.size() ||
                connection.toNodeIndex >= definition.nodes.size()) {
                continue;
            }
            AddLine(flatVertices,
                    definition.nodes[connection.fromNodeIndex].anchorPosition,
                    definition.nodes[connection.toNodeIndex].anchorPosition,
                    3.0f, hint);
        }
    }
    appendListBatch(hintBatches, start);

    // Preserve the established deep-red pixel ribbon: dark outer strip,
    // crimson inset strip, then stable highlight/shadow flesh pixels.
    for (const TentacleRenderSnapshot& tentacle : snapshot.tentacles) {
        TentacleStyle outline = tentacle.style;
        outline.color = ScaleRgb(outline.color, 0.32f);
        appendRibbon(tentacle.points, outline);
    }
    for (const TentacleRenderSnapshot& tentacle : snapshot.tentacles) {
        appendRibbon(tentacle.points, MakeFleshCoreStyle(tentacle.style));
    }
    start = flatVertices.size();
    for (const TentacleRenderSnapshot& tentacle : snapshot.tentacles) {
        AddFleshPixels(flatVertices, tentacle.points, tentacle.style);
    }
    appendListBatch(vesselBatches, start);

    const std::vector<bool> activated = MakeIndexFlags(
        definition.nodes.size(), snapshot.activatedNodeIndices);
    const std::vector<bool> available = MakeIndexFlags(
        definition.nodes.size(), snapshot.availableSourceNodeIndices);

    start = flatVertices.size();
    if (!snapshot.solved) {
        for (std::size_t index = 0; index < definition.nodes.size(); ++index) {
            const bool selected = snapshot.selectedSourceNodeIndex.has_value() &&
                                  *snapshot.selectedSourceNodeIndex == index;
            if (!available[index] && !selected) {
                continue;
            }
            const NodeDefinition& node = definition.nodes[index];
            const float pulseWave = 0.5f +
                                    0.5f * std::sin(elapsedSeconds *
                                                    (selected ? 8.0f : 5.5f));
            const float radius = NodePulseRadius(node) +
                                 (selected ? 8.0f : 4.0f) + pulseWave * 3.0f;
            const Color pulseColor = selected
                ? WithAlpha(ScaleRgb(definition.vesselColor, 1.55f), 0.78f)
                : WithAlpha(definition.vesselColor, 0.48f);
            AddCircle(flatVertices, NodeStampCenter(node), radius, pulseColor);
        }
    }
    appendListBatch(pulseBatches, start);

    std::vector<TileMeshVertex> tileVertices;
    tileVertices.reserve(8192);
    std::string meshError;
    const auto appendGrid = [this, &tileVertices, &meshError](
                                const TileGrid& grid,
                                const Color tint) {
        std::vector<TileMeshVertex> mesh;
        if (!BuildTileMesh(grid, impl_->tileset, {}, kTileSize,
                           mesh, meshError, {}, tint)) {
            throw std::runtime_error("PuzzleRenderer tile grid: " + meshError);
        }
        const VertexRange range{
            ToUint(tileVertices.size(), "Tile vertex start"),
            ToUint(mesh.size(), "Tile vertex count"),
        };
        tileVertices.insert(tileVertices.end(), mesh.begin(), mesh.end());
        return range;
    };
    const auto appendStamp = [this, &tileVertices, &meshError](
                                 const NodeDefinition& node,
                                 const Color tint) {
        std::vector<TileMeshVertex> mesh;
        if (!BuildTileStampMesh(node.stamp, impl_->tileset,
                                NodeStampOrigin(node), kTileSize, tint,
                                mesh, meshError)) {
            throw std::runtime_error("PuzzleRenderer node stamp '" +
                                     node.id + "': " + meshError);
        }
        const VertexRange range{
            ToUint(tileVertices.size(), "Node tile vertex start"),
            ToUint(mesh.size(), "Node tile vertex count"),
        };
        tileVertices.insert(tileVertices.end(), mesh.begin(), mesh.end());
        return range;
    };

    const VertexRange backgroundRange =
        appendGrid(definition.backgroundTiles, Color{});
    const VertexRange solidRange =
        appendGrid(definition.obstacleTiles, Color{});
    const std::size_t nodeStart = tileVertices.size();
    for (std::size_t index = 0; index < definition.nodes.size(); ++index) {
        const bool selected = snapshot.selectedSourceNodeIndex.has_value() &&
                              *snapshot.selectedSourceNodeIndex == index;
        const Color tint = selected ? kSelectedNodeTint
                           : activated[index] ? kActivatedNodeTint
                                              : kInactiveNodeTint;
        static_cast<void>(appendStamp(definition.nodes[index], tint));
    }
    const VertexRange nodeRange{
        ToUint(nodeStart, "Node layer vertex start"),
        ToUint(tileVertices.size() - nodeStart, "Node layer vertex count"),
    };

    impl_->EnsureFlatCapacity(flatVertices.size());
    impl_->EnsureTileCapacity(tileVertices.size());
    if (!flatVertices.empty()) {
        std::memcpy(impl_->mappedFlatVertices, flatVertices.data(),
                    flatVertices.size() * sizeof(RibbonVertex));
    }
    if (!tileVertices.empty()) {
        std::memcpy(impl_->mappedTileVertices, tileVertices.data(),
                    tileVertices.size() * sizeof(TileMeshVertex));
    }

    ID3D12GraphicsCommandList* const commandList = impl_->directX->GetCommandList();
    SpriteDrawScope descriptorHeapScope{commandList};

    // Explicit draw order:
    // fallback color -> background tiles -> target hints -> pixel vessels ->
    // solid obstacle tiles -> available/selected source pulse -> node stamps.
    impl_->DrawFlat(fallbackBatches);
    impl_->DrawTiles(backgroundRange);
    impl_->DrawFlat(hintBatches);
    impl_->DrawFlat(vesselBatches);
    impl_->DrawTiles(solidRange);
    impl_->DrawFlat(pulseBatches);
    impl_->DrawTiles(nodeRange);
}

void PuzzleRenderer::Finalize() noexcept { impl_.reset(); }

bool PuzzleRenderer::IsInitialized() const noexcept { return impl_ != nullptr; }

} // namespace object_connect
