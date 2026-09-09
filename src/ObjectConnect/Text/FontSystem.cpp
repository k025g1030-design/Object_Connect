#include "ObjectConnect/Text/FontSystem.hpp"

#include "ObjectConnect/Text/Detail/LazyGlyph.hpp"
#include "ObjectConnect/Text/Detail/MonotonicFontId.hpp"
#include "ObjectConnect/Text/TextLayout.hpp"
#include "ObjectConnect/Text/Utf8.hpp"

#include <base/DirectXCommon.h>

#include <Windows.h>
#include <d3dcompiler.h>
#include <d3dx12.h>
#include <wrl.h>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <imstb_truetype.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef DrawText
#undef DrawText
#endif

namespace object_connect {
namespace {

using Microsoft::WRL::ComPtr;

constexpr UINT kAtlasSize = 1024;
constexpr UINT kAtlasPadding = 1;
constexpr std::uint32_t kMaximumPixelSize =
    kAtlasSize - kAtlasPadding * 2;
constexpr std::size_t kMaximumQueuedGlyphs = 4096;
constexpr std::size_t kMaximumLayoutCacheEntries = 256;
constexpr std::uint64_t kMaximumFontFileBytes = 256ull * 1024ull * 1024ull;
constexpr char32_t kReplacementCodePoint = U'\uFFFD';

text_detail::MonotonicFontIdSource gFontIds;

[[nodiscard]] bool IsSupportedPixelSize(
    const std::uint32_t pixelSize) noexcept {
    return pixelSize > 0 && pixelSize <= kMaximumPixelSize;
}

[[nodiscard]] std::wstring Utf8ToWide(std::string_view text);

void LogFontWarning(const std::string_view message) noexcept {
    try {
        std::string output{"[FontSystem] "};
        output.append(message);
        output.push_back('\n');
        const std::wstring wide = Utf8ToWide(output);
        ::OutputDebugStringW(wide.c_str());
    } catch (...) {
        ::OutputDebugStringW(
            L"[FontSystem] An error occurred while preparing a diagnostic.\n");
    }
}

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

[[nodiscard]] std::wstring Utf8ToWide(const std::string_view text) {
    if (text.empty()) {
        return {};
    }
    if (text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        throw std::runtime_error("The UTF-8 path is too long.");
    }
    const int inputLength = static_cast<int>(text.size());
    const int required = ::MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), inputLength, nullptr, 0);
    if (required <= 0) {
        throw std::runtime_error("The font path is not valid UTF-8.");
    }
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    const int written = ::MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), inputLength,
        wide.data(), required);
    if (written != required) {
        throw std::runtime_error("The font path could not be converted to UTF-16.");
    }
    return wide;
}

[[nodiscard]] std::string WideToUtf8(const std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    if (text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
        throw std::runtime_error("The UTF-16 path is too long.");
    }
    const int inputLength = static_cast<int>(text.size());
    const int required = ::WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), inputLength,
        nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        throw std::runtime_error("The resolved font path is not valid UTF-16.");
    }
    std::string utf8(static_cast<std::size_t>(required), '\0');
    const int written = ::WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), inputLength,
        utf8.data(), required, nullptr, nullptr);
    if (written != required) {
        throw std::runtime_error("The resolved font path could not be converted to UTF-8.");
    }
    return utf8;
}

[[nodiscard]] std::uint16_t ReadBigEndian16(const std::uint8_t* const bytes) noexcept {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[0]) << 8u) |
        static_cast<std::uint16_t>(bytes[1]));
}

[[nodiscard]] std::uint32_t ReadBigEndian32(const std::uint8_t* const bytes) noexcept {
    return (static_cast<std::uint32_t>(bytes[0]) << 24u) |
           (static_cast<std::uint32_t>(bytes[1]) << 16u) |
           (static_cast<std::uint32_t>(bytes[2]) << 8u) |
           static_cast<std::uint32_t>(bytes[3]);
}

[[nodiscard]] bool HasValidSfntDirectory(const std::vector<std::uint8_t>& bytes) noexcept {
    if (bytes.size() < 12) {
        return false;
    }
    const std::uint32_t signature = ReadBigEndian32(bytes.data());
    constexpr std::uint32_t kTrueType10 = 0x00010000u;
    constexpr std::uint32_t kTrue = 0x74727565u;
    constexpr std::uint32_t kTyp1 = 0x74797031u;
    constexpr std::uint32_t kOpenType = 0x4F54544Fu;
    if (signature != kTrueType10 && signature != kTrue &&
        signature != kTyp1 && signature != kOpenType) {
        return false;
    }

    const std::size_t tableCount = ReadBigEndian16(bytes.data() + 4);
    if (tableCount > (bytes.size() - 12) / 16) {
        return false;
    }
    for (std::size_t index = 0; index < tableCount; ++index) {
        const std::uint8_t* const record = bytes.data() + 12 + index * 16;
        const std::size_t offset = ReadBigEndian32(record + 8);
        const std::size_t length = ReadBigEndian32(record + 12);
        if (offset > bytes.size() || length > bytes.size() - offset) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::vector<std::uint8_t> ReadFontFile(
    const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("The font file could not be opened.");
    }
    const std::streamoff end = input.tellg();
    if (end <= 0 || static_cast<std::uint64_t>(end) > kMaximumFontFileBytes) {
        throw std::runtime_error("The font file is empty or larger than 256 MiB.");
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(bytes.data()), end);
    if (!input) {
        throw std::runtime_error("The complete font file could not be read.");
    }
    return bytes;
}

[[nodiscard]] ComPtr<ID3DBlob> CompileShader(const wchar_t* const path,
                                             const char* const target) {
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
        std::string message{"Text shader compilation failed"};
        if (diagnostics && diagnostics->GetBufferPointer() != nullptr) {
            message += ": ";
            message.append(static_cast<const char*>(diagnostics->GetBufferPointer()),
                           diagnostics->GetBufferSize());
        }
        throw std::runtime_error(message);
    }
    return byteCode;
}

[[nodiscard]] ComPtr<ID3D12Resource> CreateUploadBuffer(
    ID3D12Device& device, const UINT64 size, const char* const operation) {
    const D3D12_HEAP_PROPERTIES heap =
        CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    const D3D12_RESOURCE_DESC description = CD3DX12_RESOURCE_DESC::Buffer(size);
    ComPtr<ID3D12Resource> resource;
    RequireSuccess(device.CreateCommittedResource(
                       &heap, D3D12_HEAP_FLAG_NONE, &description,
                       D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                       IID_PPV_ARGS(&resource)),
                   operation);
    return resource;
}

struct TextVertex final {
    Vec2 position{};
    Vec2 uv{};
    Color color{};
};

struct CanvasConstants final {
    float left = 0.0f;
    float top = 0.0f;
    float width = 1280.0f;
    float height = 720.0f;
    float padding[60]{};
};

static_assert(sizeof(CanvasConstants) == 256);

struct AtlasPlacement final {
    std::size_t pageIndex = 0;
    UINT x = 0;
    UINT y = 0;
    UINT width = 0;
    UINT height = 0;
};

struct AtlasPage final {
    ComPtr<ID3D12Resource> texture;
    ComPtr<ID3D12Resource> upload;
    ComPtr<ID3D12DescriptorHeap> srvHeap;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    std::uint8_t* mappedUpload = nullptr;
    UINT cursorX = 0;
    UINT cursorY = 0;
    UINT shelfHeight = 0;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COPY_DEST;
    std::vector<D3D12_BOX> dirtyBoxes;

    ~AtlasPage() {
        if (upload && mappedUpload != nullptr) {
            upload->Unmap(0, nullptr);
        }
    }

    AtlasPage() = default;
    AtlasPage(const AtlasPage&) = delete;
    AtlasPage& operator=(const AtlasPage&) = delete;
};

struct QueuedQuad final {
    AtlasPage* page = nullptr;
    float left = 0.0f;
    float top = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    Color color{};
};

class TextureQuadBackend final {
public:
    [[nodiscard]] bool Initialize(std::string& error) {
        Finalize();
        error.clear();
        try {
            directX_ = KamataEngine::DirectXCommon::GetInstance();
            if (directX_ == nullptr || !directX_->IsInitialized()) {
                error = "DirectX must be initialized before FontSystem.";
                return false;
            }
            ID3D12Device* const device = directX_->GetDevice();
            if (device == nullptr) {
                error = "KamataEngine did not provide a D3D12 device for FontSystem.";
                return false;
            }

            const ComPtr<ID3DBlob> vertexShader =
                CompileShader(L"Resources/shaders/TextVS.hlsl", "vs_5_0");
            const ComPtr<ID3DBlob> pixelShader =
                CompileShader(L"Resources/shaders/TextPS.hlsl", "ps_5_0");

            CD3DX12_DESCRIPTOR_RANGE srvRange;
            srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
            std::array<CD3DX12_ROOT_PARAMETER, 2> rootParameters;
            rootParameters[0].InitAsConstantBufferView(
                0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
            rootParameters[1].InitAsDescriptorTable(
                1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);

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
                static_cast<UINT>(rootParameters.size()), rootParameters.data(),
                1, &sampler,
                D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
            ComPtr<ID3DBlob> serializedRoot;
            ComPtr<ID3DBlob> rootDiagnostics;
            RequireSuccess(D3D12SerializeRootSignature(
                               &rootDescription, D3D_ROOT_SIGNATURE_VERSION_1,
                               &serializedRoot, &rootDiagnostics),
                           "Serializing text root signature");
            RequireSuccess(device->CreateRootSignature(
                               0, serializedRoot->GetBufferPointer(),
                               serializedRoot->GetBufferSize(),
                               IID_PPV_ARGS(&rootSignature_)),
                           "Creating text root signature");

            constexpr std::array<D3D12_INPUT_ELEMENT_DESC, 3> inputLayout = {{
                {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
                 D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8,
                 D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16,
                 D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
            }};

            D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
            pipeline.pRootSignature = rootSignature_.Get();
            pipeline.VS = {vertexShader->GetBufferPointer(),
                           vertexShader->GetBufferSize()};
            pipeline.PS = {pixelShader->GetBufferPointer(),
                           pixelShader->GetBufferSize()};
            pipeline.InputLayout = {
                inputLayout.data(), static_cast<UINT>(inputLayout.size())};
            pipeline.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
            pipeline.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            D3D12_RENDER_TARGET_BLEND_DESC& blend =
                pipeline.BlendState.RenderTarget[0];
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
                               &pipeline, IID_PPV_ARGS(&pipelineState_)),
                           "Creating text graphics pipeline");

            constexpr std::size_t kVerticesPerGlyph = 6;
            const UINT64 vertexBytes = static_cast<UINT64>(
                kMaximumQueuedGlyphs * kVerticesPerGlyph * sizeof(TextVertex));
            vertexBuffer_ = CreateUploadBuffer(
                *device, vertexBytes, "Creating text vertex buffer");
            RequireSuccess(vertexBuffer_->Map(
                               0, nullptr,
                               reinterpret_cast<void**>(&mappedVertices_)),
                           "Mapping text vertex buffer");
            vertexView_.BufferLocation = vertexBuffer_->GetGPUVirtualAddress();
            vertexView_.SizeInBytes = static_cast<UINT>(vertexBytes);
            vertexView_.StrideInBytes = sizeof(TextVertex);

            constantBuffer_ = CreateUploadBuffer(
                *device, sizeof(CanvasConstants), "Creating text constant buffer");
            RequireSuccess(constantBuffer_->Map(
                               0, nullptr,
                               reinterpret_cast<void**>(&mappedConstants_)),
                           "Mapping text constant buffer");
            *mappedConstants_ = {};
            initialized_ = true;
            return true;
        } catch (const std::exception& exception) {
            error = "Failed to initialize FontSystem renderer: ";
            error += exception.what();
        } catch (...) {
            error = "Failed to initialize FontSystem renderer because of an unknown error.";
        }
        Finalize();
        return false;
    }

    void Finalize() noexcept {
        queuedQuads_.clear();
        if (vertexBuffer_ && mappedVertices_ != nullptr) {
            vertexBuffer_->Unmap(0, nullptr);
        }
        if (constantBuffer_ && mappedConstants_ != nullptr) {
            constantBuffer_->Unmap(0, nullptr);
        }
        mappedVertices_ = nullptr;
        mappedConstants_ = nullptr;
        constantBuffer_.Reset();
        vertexBuffer_.Reset();
        pipelineState_.Reset();
        rootSignature_.Reset();
        directX_ = nullptr;
        initialized_ = false;
    }

    [[nodiscard]] std::unique_ptr<AtlasPage> CreateAtlasPage() const {
        if (!initialized_ || directX_ == nullptr || directX_->GetDevice() == nullptr) {
            throw std::runtime_error("The text renderer is not initialized.");
        }
        ID3D12Device* const device = directX_->GetDevice();
        auto page = std::make_unique<AtlasPage>();

        const D3D12_RESOURCE_DESC textureDescription =
            CD3DX12_RESOURCE_DESC::Tex2D(
                DXGI_FORMAT_R8_UNORM, kAtlasSize, kAtlasSize, 1, 1);
        const D3D12_HEAP_PROPERTIES defaultHeap =
            CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        RequireSuccess(device->CreateCommittedResource(
                           &defaultHeap, D3D12_HEAP_FLAG_NONE,
                           &textureDescription, D3D12_RESOURCE_STATE_COPY_DEST,
                           nullptr, IID_PPV_ARGS(&page->texture)),
                       "Creating glyph atlas texture");

        UINT rowCount = 0;
        UINT64 rowSize = 0;
        UINT64 uploadSize = 0;
        device->GetCopyableFootprints(
            &textureDescription, 0, 1, 0, &page->footprint,
            &rowCount, &rowSize, &uploadSize);
        if (rowCount != kAtlasSize || rowSize < kAtlasSize || uploadSize == 0) {
            throw std::runtime_error("D3D12 returned an invalid glyph atlas footprint.");
        }
        page->upload = CreateUploadBuffer(
            *device, uploadSize, "Creating glyph atlas upload buffer");
        RequireSuccess(page->upload->Map(
                           0, nullptr,
                           reinterpret_cast<void**>(&page->mappedUpload)),
                       "Mapping glyph atlas upload buffer");
        std::memset(page->mappedUpload, 0, static_cast<std::size_t>(uploadSize));

        D3D12_DESCRIPTOR_HEAP_DESC heapDescription{};
        heapDescription.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDescription.NumDescriptors = 1;
        heapDescription.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        RequireSuccess(device->CreateDescriptorHeap(
                           &heapDescription, IID_PPV_ARGS(&page->srvHeap)),
                       "Creating glyph atlas descriptor heap");
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = DXGI_FORMAT_R8_UNORM;
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(
            page->texture.Get(), &srv,
            page->srvHeap->GetCPUDescriptorHandleForHeapStart());
        return page;
    }

    [[nodiscard]] bool Queue(const QueuedQuad& quad) {
        if (quad.page == nullptr || queuedQuads_.size() >= kMaximumQueuedGlyphs) {
            return false;
        }
        queuedQuads_.push_back(quad);
        return true;
    }

    [[nodiscard]] std::uint64_t UploadDirtyPage(AtlasPage& page) const {
        if (page.dirtyBoxes.empty()) {
            return 0;
        }
        ID3D12GraphicsCommandList* const commandList = directX_->GetCommandList();
        if (commandList == nullptr) {
            throw std::runtime_error("KamataEngine did not provide a render command list.");
        }
        if (page.state != D3D12_RESOURCE_STATE_COPY_DEST) {
            const D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                page.texture.Get(), page.state, D3D12_RESOURCE_STATE_COPY_DEST);
            commandList->ResourceBarrier(1, &barrier);
            page.state = D3D12_RESOURCE_STATE_COPY_DEST;
        }

        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = page.texture.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = page.upload.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = page.footprint;
        for (const D3D12_BOX& box : page.dirtyBoxes) {
            commandList->CopyTextureRegion(
                &destination, box.left, box.top, 0, &source, &box);
        }
        const std::uint64_t uploadCount = page.dirtyBoxes.size();
        page.dirtyBoxes.clear();

        const D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            page.texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        commandList->ResourceBarrier(1, &barrier);
        page.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        return uploadCount;
    }

    [[nodiscard]] bool DrawQueued() {
        if (queuedQuads_.empty()) {
            return true;
        }
        if (!initialized_ || directX_ == nullptr || mappedVertices_ == nullptr ||
            mappedConstants_ == nullptr) {
            return false;
        }

        constexpr std::size_t kVerticesPerQuad = 6;
        std::size_t vertexIndex = 0;
        for (const QueuedQuad& quad : queuedQuads_) {
            const float right = quad.left + quad.width;
            const float bottom = quad.top + quad.height;
            const TextVertex topLeft{{quad.left, quad.top}, {quad.u0, quad.v0}, quad.color};
            const TextVertex topRight{{right, quad.top}, {quad.u1, quad.v0}, quad.color};
            const TextVertex bottomLeft{{quad.left, bottom}, {quad.u0, quad.v1}, quad.color};
            const TextVertex bottomRight{{right, bottom}, {quad.u1, quad.v1}, quad.color};
            mappedVertices_[vertexIndex++] = topLeft;
            mappedVertices_[vertexIndex++] = topRight;
            mappedVertices_[vertexIndex++] = bottomLeft;
            mappedVertices_[vertexIndex++] = bottomLeft;
            mappedVertices_[vertexIndex++] = topRight;
            mappedVertices_[vertexIndex++] = bottomRight;
        }

        mappedConstants_->width = static_cast<float>(directX_->GetBackBufferWidth());
        mappedConstants_->height = static_cast<float>(directX_->GetBackBufferHeight());
        if (!(mappedConstants_->width > 0.0f) || !(mappedConstants_->height > 0.0f)) {
            return false;
        }

        ID3D12GraphicsCommandList* const commandList = directX_->GetCommandList();
        if (commandList == nullptr) {
            return false;
        }
        commandList->SetGraphicsRootSignature(rootSignature_.Get());
        commandList->SetPipelineState(pipelineState_.Get());
        commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commandList->IASetVertexBuffers(0, 1, &vertexView_);
        commandList->SetGraphicsRootConstantBufferView(
            0, constantBuffer_->GetGPUVirtualAddress());

        std::size_t firstQuad = 0;
        while (firstQuad < queuedQuads_.size()) {
            AtlasPage* const page = queuedQuads_[firstQuad].page;
            std::size_t endQuad = firstQuad + 1;
            while (endQuad < queuedQuads_.size() &&
                   queuedQuads_[endQuad].page == page) {
                ++endQuad;
            }
            ID3D12DescriptorHeap* const heaps[] = {page->srvHeap.Get()};
            commandList->SetDescriptorHeaps(1, heaps);
            commandList->SetGraphicsRootDescriptorTable(
                1, page->srvHeap->GetGPUDescriptorHandleForHeapStart());
            commandList->DrawInstanced(
                static_cast<UINT>((endQuad - firstQuad) * kVerticesPerQuad),
                1, static_cast<UINT>(firstQuad * kVerticesPerQuad), 0);
            firstQuad = endQuad;
        }
        return true;
    }

    void ClearQueue() noexcept { queuedQuads_.clear(); }
    [[nodiscard]] bool IsInitialized() const noexcept { return initialized_; }

private:
    KamataEngine::DirectXCommon* directX_ = nullptr;
    ComPtr<ID3D12RootSignature> rootSignature_;
    ComPtr<ID3D12PipelineState> pipelineState_;
    ComPtr<ID3D12Resource> vertexBuffer_;
    ComPtr<ID3D12Resource> constantBuffer_;
    TextVertex* mappedVertices_ = nullptr;
    CanvasConstants* mappedConstants_ = nullptr;
    D3D12_VERTEX_BUFFER_VIEW vertexView_{};
    std::vector<QueuedQuad> queuedQuads_;
    bool initialized_ = false;
};

struct FontSizeMetrics final {
    float scale = 0.0f;
    float ascent = 0.0f;
    float descent = 0.0f;
    float lineHeight = 0.0f;
};

struct RasterKey final {
    std::uint32_t pixelSize = 0;
    int glyphIndex = 0;

    [[nodiscard]] bool operator==(const RasterKey&) const noexcept = default;
};

struct RasterKeyHash final {
    [[nodiscard]] std::size_t operator()(const RasterKey& key) const noexcept {
        return static_cast<std::size_t>(key.pixelSize) * 0x9E3779B1u ^
               static_cast<std::size_t>(static_cast<unsigned int>(key.glyphIndex));
    }
};

struct RasterGlyph final {
    int x0 = 0;
    int y0 = 0;
    int width = 0;
    int height = 0;
    text_detail::LazyGlyphResidency residency;
    std::optional<AtlasPlacement> placement;
};

struct GlyphKey final {
    std::uint32_t pixelSize = 0;
    char32_t codePoint = 0;

    [[nodiscard]] bool operator==(const GlyphKey&) const noexcept = default;
};

struct GlyphKeyHash final {
    [[nodiscard]] std::size_t operator()(const GlyphKey& key) const noexcept {
        return static_cast<std::size_t>(key.pixelSize) * 0x85EBCA6Bu ^
               static_cast<std::size_t>(key.codePoint);
    }
};

struct GlyphEntry final {
    int glyphIndex = 0;
    float advance = 0.0f;
    std::shared_ptr<RasterGlyph> raster;
};

struct FontRecord final {
    FontHandle handle{};
    std::wstring pathKey;
    std::string displayPath;
    std::vector<std::uint8_t> bytes;
    stbtt_fontinfo info{};
    std::size_t referenceCount = 1;
    int replacementGlyphIndex = 0;
    std::unordered_map<std::uint32_t, FontSizeMetrics> sizeMetrics;
    std::unordered_map<GlyphKey, GlyphEntry, GlyphKeyHash> glyphs;
    std::unordered_map<RasterKey, std::shared_ptr<RasterGlyph>, RasterKeyHash>
        rasterGlyphs;
    std::vector<std::unique_ptr<AtlasPage>> atlasPages;
};

struct LayoutGlyphBinding final {
    std::uint32_t pixelSize = 0;
    int glyphIndex = 0;
    std::shared_ptr<RasterGlyph> raster;
};

struct RuntimeTextLayout final {
    text_layout::Layout layout{};
    std::vector<LayoutGlyphBinding> glyphBindings;
};

[[nodiscard]] bool TryAllocateAtlasRect(AtlasPage& page, const UINT width,
                                        const UINT height,
                                        AtlasPlacement& placement) noexcept {
    if (width > kAtlasSize - kAtlasPadding * 2 ||
        height > kAtlasSize - kAtlasPadding * 2) {
        return false;
    }
    const UINT allocatedWidth = width + kAtlasPadding * 2;
    const UINT allocatedHeight = height + kAtlasPadding * 2;
    if (page.cursorX > kAtlasSize - allocatedWidth) {
        page.cursorX = 0;
        page.cursorY += page.shelfHeight;
        page.shelfHeight = 0;
    }
    if (page.cursorY > kAtlasSize - allocatedHeight) {
        return false;
    }
    placement.x = page.cursorX + kAtlasPadding;
    placement.y = page.cursorY + kAtlasPadding;
    placement.width = width;
    placement.height = height;
    page.cursorX += allocatedWidth;
    page.shelfHeight = (std::max)(page.shelfHeight, allocatedHeight);
    return true;
}

[[nodiscard]] std::string DescribeCodePoint(const char32_t codePoint) {
    std::ostringstream output;
    output << "U+" << std::uppercase << std::hex
           << static_cast<std::uint32_t>(codePoint);
    return output.str();
}

} // namespace

struct FontSystem::Impl final {
    TextureQuadBackend backend;
    std::unordered_map<std::uint32_t, std::unique_ptr<FontRecord>> fonts;
    std::unordered_map<std::wstring, std::uint32_t> fontIdsByPath;
    text_layout::LruCache<RuntimeTextLayout> layouts{
        kMaximumLayoutCacheEntries};
    FontSystemStats statistics{};
    bool frameActive = false;
    bool queueOverflowWarningIssued = false;

    [[nodiscard]] FontRecord* FindFont(const FontHandle handle) noexcept {
        const auto found = fonts.find(handle.value);
        return found == fonts.end() ? nullptr : found->second.get();
    }

    [[nodiscard]] FontSizeMetrics& GetSizeMetrics(FontRecord& font,
                                                  const std::uint32_t pixelSize) {
        const auto existing = font.sizeMetrics.find(pixelSize);
        if (existing != font.sizeMetrics.end()) {
            return existing->second;
        }
        int ascent = 0;
        int descent = 0;
        int lineGap = 0;
        stbtt_GetFontVMetrics(&font.info, &ascent, &descent, &lineGap);
        FontSizeMetrics metrics;
        metrics.scale = stbtt_ScaleForPixelHeight(
            &font.info, static_cast<float>(pixelSize));
        if (!(metrics.scale > 0.0f) || !std::isfinite(metrics.scale)) {
            throw std::runtime_error("The font produced an invalid pixel scale.");
        }
        metrics.ascent = static_cast<float>(ascent) * metrics.scale;
        metrics.descent = static_cast<float>(descent) * metrics.scale;
        metrics.lineHeight =
            (static_cast<float>(ascent) - static_cast<float>(descent) +
             static_cast<float>(lineGap)) * metrics.scale;
        if (!(metrics.lineHeight > 0.0f) || !std::isfinite(metrics.lineHeight)) {
            metrics.lineHeight = static_cast<float>(pixelSize);
        }
        return font.sizeMetrics.emplace(pixelSize, metrics).first->second;
    }

    [[nodiscard]] GlyphEntry& GetGlyph(FontRecord& font,
                                       const std::uint32_t pixelSize,
                                       const char32_t codePoint) {
        const GlyphKey key{pixelSize, codePoint};
        const auto existing = font.glyphs.find(key);
        if (existing != font.glyphs.end()) {
            ++statistics.glyphCacheHits;
            return existing->second;
        }
        ++statistics.glyphCacheMisses;

        const FontSizeMetrics& size = GetSizeMetrics(font, pixelSize);
        const int requestedGlyphIndex = stbtt_FindGlyphIndex(
            &font.info, static_cast<int>(codePoint));
        const text_detail::GlyphResolution resolution =
            text_detail::ResolveGlyphIndex(
                requestedGlyphIndex, font.replacementGlyphIndex);
        const int glyphIndex = resolution.glyphIndex;
        if (resolution.usedFallback) {
            ++statistics.missingGlyphFallbacks;
            LogFontWarning(font.displayPath + " does not contain " +
                           DescribeCodePoint(codePoint) +
                           "; using the replacement/missing glyph.");
        }

        int advance = 0;
        int leftSideBearing = 0;
        stbtt_GetGlyphHMetrics(
            &font.info, glyphIndex, &advance, &leftSideBearing);
        static_cast<void>(leftSideBearing);

        const RasterKey rasterKey{pixelSize, glyphIndex};
        std::shared_ptr<RasterGlyph> raster;
        const auto existingRaster = font.rasterGlyphs.find(rasterKey);
        if (existingRaster != font.rasterGlyphs.end()) {
            raster = existingRaster->second;
        } else {
            raster = std::make_shared<RasterGlyph>();
            int x1 = 0;
            int y1 = 0;
            stbtt_GetGlyphBitmapBox(
                &font.info, glyphIndex, size.scale, size.scale,
                &raster->x0, &raster->y0, &x1, &y1);
            const std::int64_t width =
                static_cast<std::int64_t>(x1) - raster->x0;
            const std::int64_t height =
                static_cast<std::int64_t>(y1) - raster->y0;
            raster->width = width <= 0
                                ? 0
                                : width > (std::numeric_limits<int>::max)()
                                      ? (std::numeric_limits<int>::max)()
                                      : static_cast<int>(width);
            raster->height = height <= 0
                                 ? 0
                                 : height > (std::numeric_limits<int>::max)()
                                       ? (std::numeric_limits<int>::max)()
                                       : static_cast<int>(height);
            font.rasterGlyphs.emplace(rasterKey, raster);
        }

        GlyphEntry entry;
        entry.glyphIndex = glyphIndex;
        entry.advance = static_cast<float>(advance) * size.scale;
        entry.raster = std::move(raster);
        return font.glyphs.emplace(key, std::move(entry)).first->second;
    }

    [[nodiscard]] bool EnsureRasterized(FontRecord& font,
                                        const std::uint32_t pixelSize,
                                        const int glyphIndex,
                                        const std::shared_ptr<RasterGlyph>& raster) {
        if (!raster || raster->width == 0 || raster->height == 0) {
            return true;
        }
        return raster->residency.Ensure([&]() {
            try {
                AtlasPlacement placement;
                bool allocated = false;
                for (std::size_t index = 0; index < font.atlasPages.size();
                     ++index) {
                    if (TryAllocateAtlasRect(
                            *font.atlasPages[index],
                            static_cast<UINT>(raster->width),
                            static_cast<UINT>(raster->height), placement)) {
                        placement.pageIndex = index;
                        allocated = true;
                        break;
                    }
                }
                if (!allocated) {
                    auto page = backend.CreateAtlasPage();
                    if (!TryAllocateAtlasRect(
                            *page, static_cast<UINT>(raster->width),
                            static_cast<UINT>(raster->height), placement)) {
                        LogFontWarning(
                            "A glyph is larger than a 1024x1024 atlas page.");
                        return false;
                    }
                    placement.pageIndex = font.atlasPages.size();
                    font.atlasPages.push_back(std::move(page));
                }

                AtlasPage& page = *font.atlasPages[placement.pageIndex];
                const std::size_t rowPitch =
                    page.footprint.Footprint.RowPitch;
                std::uint8_t* const destination =
                    page.mappedUpload + page.footprint.Offset +
                    static_cast<std::size_t>(placement.y) * rowPitch +
                    placement.x;
                const FontSizeMetrics& size =
                    GetSizeMetrics(font, pixelSize);
                stbtt_MakeGlyphBitmap(
                    &font.info, destination, raster->width, raster->height,
                    static_cast<int>(rowPitch), size.scale, size.scale,
                    glyphIndex);

                const UINT paddedLeft = placement.x - kAtlasPadding;
                const UINT paddedTop = placement.y - kAtlasPadding;
                page.dirtyBoxes.push_back({
                    paddedLeft,
                    paddedTop,
                    0,
                    placement.x + placement.width + kAtlasPadding,
                    placement.y + placement.height + kAtlasPadding,
                    1,
                });
                raster->placement = placement;
                ++statistics.glyphRasterizations;
                return true;
            } catch (const std::exception& exception) {
                LogFontWarning(std::string{"Glyph rasterization failed: "} +
                               exception.what());
            } catch (...) {
                LogFontWarning(
                    "Glyph rasterization failed because of an unknown error.");
            }
            return false;
        });
    }

    [[nodiscard]] RuntimeTextLayout BuildLayout(
        FontRecord& font, const std::uint32_t pixelSize,
        const std::string_view utf8Text) {
        RuntimeTextLayout layout;
        const FontSizeMetrics& size = GetSizeMetrics(font, pixelSize);
        if (utf8Text.empty()) {
            layout.layout = text_layout::Build({}, size.ascent, size.lineHeight);
            return layout;
        }

        ++statistics.utf8DecodeCount;
        const Utf8DecodeResult decoded = DecodeUtf8(utf8Text);
        if (decoded.replacementCount != 0) {
            LogFontWarning("Invalid UTF-8 was replaced with U+FFFD while laying out text.");
        }

        std::vector<text_layout::GlyphToken> tokens;
        tokens.reserve(decoded.codePoints.size());
        layout.glyphBindings.reserve(decoded.codePoints.size());
        std::optional<int> previousGlyph;
        for (const char32_t codePoint : decoded.codePoints) {
            if (codePoint == U'\r' || codePoint == U'\n') {
                tokens.push_back({codePoint});
                layout.glyphBindings.push_back({});
                previousGlyph.reset();
                continue;
            }

            GlyphEntry& glyph = GetGlyph(font, pixelSize, codePoint);
            float kerningBefore = 0.0f;
            if (previousGlyph.has_value()) {
                const int kerning = stbtt_GetGlyphKernAdvance(
                    &font.info, *previousGlyph, glyph.glyphIndex);
                kerningBefore = static_cast<float>(kerning) * size.scale;
            }
            tokens.push_back({
                codePoint,
                glyph.advance,
                kerningBefore,
                static_cast<float>(glyph.raster->x0),
                static_cast<float>(glyph.raster->y0),
                static_cast<float>(glyph.raster->width),
                static_cast<float>(glyph.raster->height),
            });
            layout.glyphBindings.push_back(
                {pixelSize, glyph.glyphIndex, glyph.raster});
            previousGlyph = glyph.glyphIndex;
        }

        layout.layout = text_layout::Build(tokens, size.ascent, size.lineHeight);
        return layout;
    }

    [[nodiscard]] RuntimeTextLayout* GetLayout(
        FontRecord& font, const std::uint32_t pixelSize,
        const std::string_view text) {
        const auto result = layouts.GetOrBuild(
            font.handle.value, pixelSize, text,
            [&]() { return BuildLayout(font, pixelSize, text); });
        if (result.wasHit) {
            ++statistics.layoutCacheHits;
        } else {
            ++statistics.layoutCacheMisses;
        }
        return result.value;
    }

    void EraseLayoutsForFont(const std::uint32_t fontId) noexcept {
        layouts.EraseFont(fontId);
    }
};

FontSystem::FontSystem() noexcept = default;
FontSystem::~FontSystem() { Finalize(); }

bool FontSystem::Initialize(std::string& error) {
    Finalize();
    error.clear();
    try {
        auto next = std::make_unique<Impl>();
        if (!next->backend.Initialize(error)) {
            return false;
        }
        impl_ = std::move(next);
        return true;
    } catch (const std::exception& exception) {
        error = "Failed to initialize FontSystem: ";
        error += exception.what();
    } catch (...) {
        error = "Failed to initialize FontSystem because of an unknown error.";
    }
    return false;
}

void FontSystem::Finalize() noexcept {
    if (impl_) {
        impl_->backend.ClearQueue();
        impl_->layouts.Clear();
        impl_->fontIdsByPath.clear();
        impl_->fonts.clear();
        impl_->backend.Finalize();
    }
    impl_.reset();
}

FontHandle FontSystem::LoadFont(const std::string& utf8Path,
                                std::string& error) {
    error.clear();
    if (!impl_ || !impl_->backend.IsInitialized()) {
        error = "FontSystem must be initialized before loading a font.";
        return {};
    }
    if (impl_->frameActive) {
        error = "Fonts can only be loaded outside an active render frame.";
        return {};
    }
    try {
        if (utf8Path.empty()) {
            error = "The font path must not be empty.";
            return {};
        }
        std::error_code filesystemError;
        std::filesystem::path path{Utf8ToWide(utf8Path)};
        path = std::filesystem::absolute(path, filesystemError);
        if (filesystemError) {
            error = "The font path could not be resolved: " + utf8Path;
            return {};
        }
        path = path.lexically_normal();
        const std::string displayPath = WideToUtf8(path.native());
        if (!std::filesystem::is_regular_file(path, filesystemError) ||
            filesystemError) {
            error = "Font file was not found: " + displayPath;
            return {};
        }

        std::wstring pathKey = path.native();
        std::transform(pathKey.begin(), pathKey.end(), pathKey.begin(),
                       [](const wchar_t character) {
                           return static_cast<wchar_t>(std::towlower(character));
                       });
        const auto existing = impl_->fontIdsByPath.find(pathKey);
        if (existing != impl_->fontIdsByPath.end()) {
            FontRecord* const font = impl_->FindFont({existing->second});
            if (font != nullptr &&
                font->referenceCount < (std::numeric_limits<std::size_t>::max)()) {
                ++font->referenceCount;
                return font->handle;
            }
            error = "The font reference count overflowed: " + displayPath;
            return {};
        }

        const std::uint32_t fontId = gFontIds.Allocate();
        if (fontId == 0) {
            error = "FontHandle identifiers were exhausted.";
            return {};
        }
        auto font = std::make_unique<FontRecord>();
        font->handle = {fontId};
        font->pathKey = std::move(pathKey);
        font->displayPath = displayPath;
        font->bytes = ReadFontFile(path);
        if (!HasValidSfntDirectory(font->bytes)) {
            error = "Font file has an invalid or unsupported SFNT table directory: " +
                    displayPath;
            return {};
        }
        const int offset = stbtt_GetFontOffsetForIndex(font->bytes.data(), 0);
        if (offset < 0 ||
            stbtt_InitFont(&font->info, font->bytes.data(), offset) == 0) {
            error = "stb_truetype could not load font: " + displayPath;
            return {};
        }
        int fontAscent = 0;
        int fontDescent = 0;
        int fontLineGap = 0;
        stbtt_GetFontVMetrics(
            &font->info, &fontAscent, &fontDescent, &fontLineGap);
        static_cast<void>(fontLineGap);
        if (fontAscent <= fontDescent) {
            error = "Font has invalid vertical metrics: " + displayPath;
            return {};
        }
        font->replacementGlyphIndex = stbtt_FindGlyphIndex(
            &font->info, static_cast<int>(kReplacementCodePoint));

        // Allocate one page up front so texture/descriptor failures are reported
        // by LoadFont instead of silently dropping the first text at runtime.
        font->atlasPages.push_back(impl_->backend.CreateAtlasPage());
        const FontHandle handle = font->handle;
        const auto pathInsertion = impl_->fontIdsByPath.emplace(
            font->pathKey, handle.value);
        if (!pathInsertion.second) {
            error = "The normalized font path was already registered: " +
                    displayPath;
            return {};
        }
        try {
            const auto fontInsertion = impl_->fonts.emplace(
                handle.value, std::move(font));
            if (!fontInsertion.second) {
                impl_->fontIdsByPath.erase(pathInsertion.first);
                error = "The allocated FontHandle was already registered.";
                return {};
            }
        } catch (...) {
            impl_->fontIdsByPath.erase(pathInsertion.first);
            throw;
        }
        return handle;
    } catch (const std::exception& exception) {
        error = "Failed to load font '" + utf8Path + "': ";
        error += exception.what();
    } catch (...) {
        error = "Failed to load font '" + utf8Path +
                "' because of an unknown error.";
    }
    return {};
}

void FontSystem::UnloadFont(const FontHandle fontHandle) noexcept {
    if (!impl_ || !fontHandle) {
        return;
    }
    if (impl_->frameActive) {
        LogFontWarning("UnloadFont was ignored during an active render frame.");
        return;
    }
    const auto found = impl_->fonts.find(fontHandle.value);
    if (found == impl_->fonts.end()) {
        LogFontWarning("UnloadFont received a stale or unknown FontHandle.");
        return;
    }
    FontRecord& font = *found->second;
    if (font.referenceCount > 1) {
        --font.referenceCount;
        return;
    }
    impl_->EraseLayoutsForFont(fontHandle.value);
    impl_->fontIdsByPath.erase(font.pathKey);
    impl_->fonts.erase(found);
}

TextMetrics FontSystem::MeasureText(const FontHandle fontHandle,
                                    const std::uint32_t pixelSize,
                                    const std::string_view utf8Text) {
    if (!impl_) {
        LogFontWarning("MeasureText requires an initialized FontSystem.");
        return {};
    }
    if (!IsSupportedPixelSize(pixelSize)) {
        LogFontWarning(
            "MeasureText pixel size must be between 1 and 1022 pixels.");
        return {};
    }
    FontRecord* const font = impl_->FindFont(fontHandle);
    if (font == nullptr) {
        LogFontWarning("MeasureText received a stale or unknown FontHandle.");
        return {};
    }
    try {
        RuntimeTextLayout* const layout =
            impl_->GetLayout(*font, pixelSize, utf8Text);
        return layout != nullptr ? layout->layout.metrics : TextMetrics{};
    } catch (const std::exception& exception) {
        LogFontWarning(std::string{"MeasureText failed: "} + exception.what());
    } catch (...) {
        LogFontWarning("MeasureText failed because of an unknown error.");
    }
    return {};
}

void FontSystem::BeginFrame() {
    if (!impl_) {
        return;
    }
    if (impl_->frameActive) {
        LogFontWarning("BeginFrame discarded text that was not flushed.");
    }
    impl_->backend.ClearQueue();
    impl_->frameActive = true;
    impl_->queueOverflowWarningIssued = false;
}

bool FontSystem::DrawText(const FontHandle fontHandle,
                          const std::string_view utf8Text,
                          const TextDrawOptions& options) {
    if (!impl_ || !impl_->frameActive) {
        LogFontWarning("DrawText was called outside a valid active frame.");
        return false;
    }
    if (!IsSupportedPixelSize(options.pixelSize)) {
        LogFontWarning(
            "DrawText pixel size must be between 1 and 1022 pixels.");
        return false;
    }
    FontRecord* const font = impl_->FindFont(fontHandle);
    if (font == nullptr) {
        LogFontWarning("DrawText received a stale or unknown FontHandle.");
        return false;
    }
    try {
        RuntimeTextLayout* const layout =
            impl_->GetLayout(*font, options.pixelSize, utf8Text);
        if (layout == nullptr || layout->layout.metrics.lineCount == 0) {
            return true;
        }

        bool allGlyphsQueued = true;
        for (const text_layout::PositionedGlyph& positioned :
             layout->layout.glyphs) {
            if (positioned.sourceIndex >= layout->glyphBindings.size()) {
                LogFontWarning("A cached text layout contained an invalid glyph binding.");
                return false;
            }
            const LayoutGlyphBinding& glyph =
                layout->glyphBindings[positioned.sourceIndex];
            if (!impl_->EnsureRasterized(
                    *font, glyph.pixelSize, glyph.glyphIndex, glyph.raster)) {
                allGlyphsQueued = false;
                continue;
            }
            if (!glyph.raster) {
                allGlyphsQueued = false;
                continue;
            }
            if (glyph.raster->width == 0 || glyph.raster->height == 0) {
                continue;
            }
            if (!glyph.raster->placement.has_value()) {
                allGlyphsQueued = false;
                continue;
            }

            const text_layout::GlyphQuad bounds = text_layout::ResolveGlyphQuad(
                layout->layout, positioned, options.position,
                options.horizontalAlignment, options.verticalAlignment);
            const AtlasPlacement& placement = *glyph.raster->placement;
            AtlasPage& page = *font->atlasPages[placement.pageIndex];
            const float inverseAtlas = 1.0f / static_cast<float>(kAtlasSize);
            const QueuedQuad quad{
                &page,
                bounds.left,
                bounds.top,
                bounds.width,
                bounds.height,
                static_cast<float>(placement.x) * inverseAtlas,
                static_cast<float>(placement.y) * inverseAtlas,
                static_cast<float>(placement.x + placement.width) * inverseAtlas,
                static_cast<float>(placement.y + placement.height) * inverseAtlas,
                options.color,
            };
            if (!impl_->backend.Queue(quad)) {
                if (!impl_->queueOverflowWarningIssued) {
                    LogFontWarning(
                        "The per-frame limit of 4096 visible glyphs was reached; "
                        "remaining glyphs were skipped.");
                    impl_->queueOverflowWarningIssued = true;
                }
                return false;
            }
        }
        return allGlyphsQueued;
    } catch (const std::exception& exception) {
        LogFontWarning(std::string{"DrawText failed: "} + exception.what());
    } catch (...) {
        LogFontWarning("DrawText failed because of an unknown error.");
    }
    return false;
}

bool FontSystem::Flush() {
    if (!impl_ || !impl_->frameActive) {
        return false;
    }
    bool result = true;
    try {
        for (auto& fontEntry : impl_->fonts) {
            for (auto& page : fontEntry.second->atlasPages) {
                impl_->statistics.atlasUploads +=
                    impl_->backend.UploadDirtyPage(*page);
            }
        }
        result = impl_->backend.DrawQueued();
        if (!result) {
            LogFontWarning("The queued text could not be drawn.");
        }
    } catch (const std::exception& exception) {
        LogFontWarning(std::string{"FontSystem::Flush failed: "} +
                       exception.what());
        result = false;
    } catch (...) {
        LogFontWarning("FontSystem::Flush failed because of an unknown error.");
        result = false;
    }
    impl_->backend.ClearQueue();
    impl_->frameActive = false;
    return result;
}

FontSystemStats FontSystem::GetStatistics() const noexcept {
    return impl_ ? impl_->statistics : FontSystemStats{};
}

bool FontSystem::IsInitialized() const noexcept { return impl_ != nullptr; }

} // namespace object_connect
