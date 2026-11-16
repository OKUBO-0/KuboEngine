#pragma once

#include <d3d12.h>
#include <wrl.h>
#include <filesystem>
#include <future>

namespace Engine::Base {
class DirectXCommon;
class OffscreenRenderManager;
}

namespace Engine::Editor {

class ProjectPreviewCapture final {
public:
    bool Record(Base::DirectXCommon& graphics, Base::OffscreenRenderManager& renderer,
        const std::filesystem::path& root) noexcept;
    void Poll(Base::DirectXCommon& graphics) noexcept;
    void RetireAfterGpuIdle(Base::DirectXCommon& graphics) noexcept;

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> target_, readback_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint_{};
    UINT64 fence_ = 0;
    std::filesystem::path root_;
    std::future<void> encode_;
};

} // namespace Engine::Editor
