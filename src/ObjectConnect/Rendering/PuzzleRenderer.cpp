#include "ObjectConnect/Rendering/PuzzleRenderer.hpp"

#include "ObjectConnect/Math/Vec2.hpp"
#include "ObjectConnect/Tentacle/RibbonStrip.hpp"

#include <KamataEngine.h>

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
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace object_connect {
namespace {

using Microsoft::WRL::ComPtr;

constexpr float kCanvasWidth = 1280.0f;
constexpr float kCanvasHeight = 720.0f;
constexpr int kCircleSegments = 32;
constexpr float kFleshCoreInset = 4.0f;
constexpr float kFleshPixelSpacing = 12.0f;

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

[[nodiscard]] ComPtr<ID3DBlob> CompileShader(const wchar_t* path, const char* target) {
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
        std::string message = "Flat 2D shader compilation failed";
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
                   "Creating flat 2D upload buffer");
    return resource;
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

[[nodiscard]] Color Darkened(const Color color, const float factor) noexcept {
    return ScaleRgb(color, factor);
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

} // namespace

struct PuzzleRenderer::Impl final {
    KamataEngine::DirectXCommon* directX = nullptr;
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12PipelineState> pipelineState;
    ComPtr<ID3D12Resource> vertexBuffer;
    ComPtr<ID3D12Resource> constantBuffer;
    RibbonVertex* mappedVertices = nullptr;
    CanvasConstants* mappedConstants = nullptr;
    D3D12_VERTEX_BUFFER_VIEW vertexView{};
    std::size_t maxVertices = 0;
};

PuzzleRenderer::PuzzleRenderer() noexcept = default;
PuzzleRenderer::~PuzzleRenderer() { Finalize(); }

bool PuzzleRenderer::Initialize(const std::size_t maxVertices, std::string& error) {
    Finalize();
    error.clear();
    if (maxVertices < 128) {
        error = "PuzzleRenderer requires room for at least 128 vertices.";
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

        const ComPtr<ID3DBlob> vertexShader =
            CompileShader(L"Resources/shaders/Flat2DVS.hlsl", "vs_5_0");
        const ComPtr<ID3DBlob> pixelShader =
            CompileShader(L"Resources/shaders/Flat2DPS.hlsl", "ps_5_0");

        CD3DX12_ROOT_PARAMETER rootParameter;
        rootParameter.InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
        const CD3DX12_ROOT_SIGNATURE_DESC rootDescription(
            1, &rootParameter, 0, nullptr,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
        ComPtr<ID3DBlob> serializedRoot;
        ComPtr<ID3DBlob> rootError;
        RequireSuccess(D3D12SerializeRootSignature(
                           &rootDescription, D3D_ROOT_SIGNATURE_VERSION_1,
                           &serializedRoot, &rootError),
                       "Serializing flat 2D root signature");
        RequireSuccess(device->CreateRootSignature(
                           0, serializedRoot->GetBufferPointer(),
                           serializedRoot->GetBufferSize(),
                           IID_PPV_ARGS(&next->rootSignature)),
                       "Creating flat 2D root signature");

        constexpr std::array<D3D12_INPUT_ELEMENT_DESC, 2> inputLayout = {{
            {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 8,
             D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        }};

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
        pipeline.pRootSignature = next->rootSignature.Get();
        pipeline.VS = {vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()};
        pipeline.PS = {pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};
        pipeline.InputLayout = {inputLayout.data(), static_cast<UINT>(inputLayout.size())};
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
        RequireSuccess(device->CreateGraphicsPipelineState(
                           &pipeline, IID_PPV_ARGS(&next->pipelineState)),
                       "Creating flat 2D graphics pipeline");

        next->maxVertices = maxVertices;
        next->vertexBuffer = CreateUploadBuffer(
            *device, static_cast<UINT64>(maxVertices * sizeof(RibbonVertex)));
        RequireSuccess(next->vertexBuffer->Map(
                           0, nullptr, reinterpret_cast<void**>(&next->mappedVertices)),
                       "Mapping flat 2D vertex buffer");
        next->vertexView.BufferLocation = next->vertexBuffer->GetGPUVirtualAddress();
        next->vertexView.SizeInBytes =
            static_cast<UINT>(maxVertices * sizeof(RibbonVertex));
        next->vertexView.StrideInBytes = sizeof(RibbonVertex);

        next->constantBuffer = CreateUploadBuffer(*device, sizeof(CanvasConstants));
        RequireSuccess(next->constantBuffer->Map(
                           0, nullptr, reinterpret_cast<void**>(&next->mappedConstants)),
                       "Mapping flat 2D constant buffer");
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

    std::vector<RibbonVertex> vertices;
    vertices.reserve(8192);
    std::vector<DrawBatch> batches;
    batches.reserve(snapshot.tentacles.size() * 2 + 4);

    const auto beginList = [&vertices]() { return vertices.size(); };
    const auto endList = [&vertices, &batches](const std::size_t start) {
        const std::size_t count = vertices.size() - start;
        if (count != 0) {
            batches.push_back({D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
                               static_cast<UINT>(start), static_cast<UINT>(count)});
        }
    };

    std::size_t listStart = beginList();
    AddRectangle(vertices, 0.0f, 0.0f, kCanvasWidth, kCanvasHeight,
                 definition.backgroundColor);
    if (definition.showTargetConnections) {
        const Color hint = WithAlpha(definition.vesselColor, 0.16f);
        for (const ConnectionDefinition& connection : definition.connections) {
            AddLine(vertices, definition.nodes[connection.fromNodeIndex].position,
                    definition.nodes[connection.toNodeIndex].position, 3.0f, hint);
        }
    }
    endList(listStart);

    const auto appendRibbon = [&vertices, &batches](
                                  const std::vector<Vec2>& points,
                                  const TentacleStyle& style) {
        std::vector<RibbonVertex> strip = BuildRibbonStrip(points, style);
        if (strip.size() < 4) {
            return;
        }
        const std::size_t start = vertices.size();
        vertices.insert(vertices.end(), strip.begin(), strip.end());
        batches.push_back({D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
                           static_cast<UINT>(start), static_cast<UINT>(strip.size())});
    };

    // A dark outer silhouette gives the procedural strip the chunky edge used by
    // pixel-art flesh. The inner crimson pass stays inside the gameplay width, so
    // obstacle clearance still matches the visible outer ribbon.
    for (const TentacleRenderSnapshot& tentacle : snapshot.tentacles) {
        TentacleStyle outline = tentacle.style;
        outline.color = ScaleRgb(outline.color, 0.32f);
        appendRibbon(tentacle.points, outline);
    }
    for (const TentacleRenderSnapshot& tentacle : snapshot.tentacles) {
        appendRibbon(tentacle.points, MakeFleshCoreStyle(tentacle.style));
    }

    listStart = beginList();
    for (const TentacleRenderSnapshot& tentacle : snapshot.tentacles) {
        AddFleshPixels(vertices, tentacle.points, tentacle.style);
    }
    endList(listStart);

    listStart = beginList();
    for (const ObstacleDefinition& obstacle : definition.obstacles) {
        if (obstacle.shape == ObstacleShape::Rectangle) {
            AddRectangle(vertices, obstacle.center.x - obstacle.width * 0.5f,
                         obstacle.center.y - obstacle.height * 0.5f,
                         obstacle.width, obstacle.height, obstacle.color);
        } else {
            AddCircle(vertices, obstacle.center, obstacle.radius, obstacle.color);
        }
    }

    for (std::size_t index = 0; index < definition.nodes.size(); ++index) {
        const NodeDefinition& node = definition.nodes[index];
        const bool active = snapshot.activeNodeIndex.has_value() &&
                            *snapshot.activeNodeIndex == index && !snapshot.solved;
        const float pulse = active ? 4.0f + 2.0f * std::sin(elapsedSeconds * 6.0f) : 2.0f;
        const Color ring = active ? definition.vesselColor : Darkened(node.color, 0.45f);
        AddCircle(vertices, node.position, node.radius + pulse, ring);
        AddCircle(vertices, node.position, node.radius, node.color);
    }
    endList(listStart);

    if (vertices.empty()) {
        return;
    }
    if (vertices.size() > impl_->maxVertices) {
        throw std::runtime_error("PuzzleRenderer vertex capacity was exceeded.");
    }

    std::memcpy(impl_->mappedVertices, vertices.data(), vertices.size() * sizeof(RibbonVertex));
    ID3D12GraphicsCommandList* const commandList = impl_->directX->GetCommandList();
    commandList->SetGraphicsRootSignature(impl_->rootSignature.Get());
    commandList->SetPipelineState(impl_->pipelineState.Get());
    commandList->IASetVertexBuffers(0, 1, &impl_->vertexView);
    commandList->SetGraphicsRootConstantBufferView(
        0, impl_->constantBuffer->GetGPUVirtualAddress());
    for (const DrawBatch& batch : batches) {
        commandList->IASetPrimitiveTopology(batch.topology);
        commandList->DrawInstanced(batch.vertexCount, 1, batch.startVertex, 0);
    }
}

void PuzzleRenderer::Finalize() noexcept {
    if (impl_) {
        if (impl_->vertexBuffer && impl_->mappedVertices != nullptr) {
            impl_->vertexBuffer->Unmap(0, nullptr);
        }
        if (impl_->constantBuffer && impl_->mappedConstants != nullptr) {
            impl_->constantBuffer->Unmap(0, nullptr);
        }
    }
    impl_.reset();
}

bool PuzzleRenderer::IsInitialized() const noexcept { return impl_ != nullptr; }

} // namespace object_connect
