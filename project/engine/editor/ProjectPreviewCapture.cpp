#include "ProjectPreviewCapture.h"
#include "engine/base/AtomicFile.h"
#include "engine/base/ProjectPreviewPath.h"
#include "engine/base/directx/DirectXCommon.h"
#include "engine/base/render/OffscreenRenderManager.h"
#include <cstring>
#include <vector>

namespace Engine::Editor {
namespace {

void SavePreview(const std::filesystem::path& root, std::vector<uint8_t> pixels)
{
    const uint8_t* first = pixels.data();
    bool hasDetail = false;
    for (size_t i = 4; i < pixels.size(); i += 4) {
        if (std::abs(int(pixels[i]) - first[0]) > 3 ||
            std::abs(int(pixels[i+1]) - first[1]) > 3 ||
            std::abs(int(pixels[i+2]) - first[2]) > 3) {
            hasDetail = true;
            break;
        }
    }
    if (!hasDetail || !Base::PreviewDirectoriesAllowed(root, true)) return;
    for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) return;
    struct Uninitialize { ~Uninitialize() { CoUninitialize(); } } com;
    DirectX::Image image{};
    image.width = Base::ProjectPreviewWidth;
    image.height = Base::ProjectPreviewHeight;
    image.format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    image.rowPitch = image.width * 4;
    image.slicePitch = pixels.size();
    image.pixels = pixels.data();
    DirectX::Blob png;
    if (FAILED(DirectX::SaveToWICMemory(image, DirectX::WIC_FLAGS_NONE,
        DirectX::GetWICCodec(DirectX::WIC_CODEC_PNG), png)) ||
        !png.GetBufferSize() || png.GetBufferSize() > Base::ProjectPreviewMaxBytes) return;
    std::string error;
    Base::AtomicFile::Write(Base::ProjectPreviewPath(root),
        std::string(static_cast<const char*>(png.GetBufferPointer()), png.GetBufferSize()),
        Base::ProjectPreviewMaxBytes, error);
}

} // namespace

bool ProjectPreviewCapture::Record(Base::DirectXCommon& graphics, Base::OffscreenRenderManager& renderer,
    const std::filesystem::path& root) noexcept
{
    if (fence_ || encode_.valid() || !renderer.HasEditorView() || root.empty()) return false;
    const auto source = renderer.GetEditorViewExtent();
    if (!source.width || !source.height || source.width > source.height * 8 ||
        source.height > source.width * 8) return false;
    try {
        root_ = root;
        constexpr Base::ViewExtent extent{Base::ProjectPreviewWidth, Base::ProjectPreviewHeight};
        if (!target_) {
            auto color = renderer.CreateRenderTargetTextureResource(extent.width, extent.height,
                DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, {0, 0, 0, 1});
            auto rtv = graphics.CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 1, false);
            graphics.GetDevice()->CreateRenderTargetView(color.Get(), &graphics.GetRTVDesc(),
                rtv->GetCPUDescriptorHandleForHeapStart());
            UINT64 bytes = 0;
            const auto description = color->GetDesc();
            graphics.GetDevice()->GetCopyableFootprints(&description, 0, 1, 0, &footprint_,
                nullptr, nullptr, &bytes);
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC buffer{};
            buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            buffer.Width = bytes;
            buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = 1;
            buffer.SampleDesc.Count = 1;
            buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            Microsoft::WRL::ComPtr<ID3D12Resource> readback;
            if (FAILED(graphics.GetDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
                &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)))) return false;
            graphics.TrackResourceState(color.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
            target_ = std::move(color);
            rtv_ = std::move(rtv);
            readback_ = std::move(readback);
        }
        graphics.TransitionResource(target_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        renderer.DrawEditorPreview(rtv_->GetCPUDescriptorHandleForHeapStart(), extent);
        graphics.TransitionResource(target_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION sourceLocation{}, destination{};
        sourceLocation.pResource = target_.Get();
        sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.pResource = readback_.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint = footprint_;
        graphics.GetCommandList()->CopyTextureRegion(&destination, 0, 0, 0, &sourceLocation, nullptr);
        fence_ = graphics.GetPendingSubmissionFenceValue();
        return true;
    } catch (...) {
        return false;
    }
}

void ProjectPreviewCapture::Poll(Base::DirectXCommon& graphics) noexcept
{
    try {
        if (encode_.valid() && encode_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            encode_.get();
        if (!fence_ || encode_.valid()) return;
        const UINT64 completed = graphics.GetCompletedFenceValue();
        if (completed < fence_ || completed == UINT64_MAX) return;
        fence_ = 0;
        constexpr size_t rowBytes = Base::ProjectPreviewWidth * 4;
        std::vector<uint8_t> pixels(rowBytes * Base::ProjectPreviewHeight);
        D3D12_RANGE range{static_cast<SIZE_T>(footprint_.Offset),
            static_cast<SIZE_T>(footprint_.Offset + footprint_.Footprint.RowPitch * Base::ProjectPreviewHeight)};
        void* mapped = nullptr;
        if (FAILED(readback_->Map(0, &range, &mapped))) return;
        const auto* source = static_cast<const uint8_t*>(mapped) + footprint_.Offset;
        for (unsigned row = 0; row < Base::ProjectPreviewHeight; ++row)
            std::memcpy(pixels.data() + rowBytes * row, source + footprint_.Footprint.RowPitch * row, rowBytes);
        const D3D12_RANGE written{0,0};
        readback_->Unmap(0, &written);
        encode_ = std::async(std::launch::async, SavePreview, root_, std::move(pixels));
    } catch (...) {
        // A preview is optional. The previous complete PNG remains available.
    }
}

void ProjectPreviewCapture::RetireAfterGpuIdle(Base::DirectXCommon& graphics) noexcept
{
    Poll(graphics);
    if (encode_.valid()) {
        try { encode_.get(); } catch (...) {}
    }
    graphics.UntrackResourceState(target_.Get());
    target_.Reset();
    rtv_.Reset();
    readback_.Reset();
    fence_ = 0;
}

} // namespace Engine::Editor
