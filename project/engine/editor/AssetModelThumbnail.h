#pragma once
#include <d3d12.h>
#include <wrl.h>
#include <memory>
#include <string>

namespace Engine::Base {class DirectXCommon;struct TextureLease;}
namespace Engine::Graphics3D {class Object3D;}
namespace Engine::Editor {
// Browser preview owner. Host owns the resource through
// every ImGui draw which borrows it. Release only after an explicit GPU drain.
class AssetModelThumbnail final {
public:
    AssetModelThumbnail();
    ~AssetModelThumbnail();
    AssetModelThumbnail(const AssetModelThumbnail&)=delete;
    AssetModelThumbnail& operator=(const AssetModelThumbnail&)=delete;
    // Host thread, before building UI or recording any preview/world draws.
    // Uses the existing leased model importer, not a second World/simulation.
    void Prepare(std::shared_ptr<Base::DirectXCommon> graphics,const std::string& root,
        const std::string& path,const std::string& identity,const std::string& revision);
    // After PrepareForFrame, before the main render target is bound. No loads.
    void Draw();
    // Host has drained submitted AND unsubmitted commands before calling.
    // Can release the heavy model separately from its cached output image.
    void ReleaseModelAfterGpuIdle() noexcept;
    void ReleaseAfterGpuIdle() noexcept;
    ID3D12Resource* Image() const noexcept {return color_.Get();}
    bool Rendered() const noexcept {return rendered_;}
    static constexpr uint32_t Edge=128;
private:
    std::shared_ptr<Base::DirectXCommon> graphics_;
    std::unique_ptr<Graphics3D::Object3D> model_;
    std::shared_ptr<const Base::TextureLease> environment_;
    Microsoft::WRL::ComPtr<ID3D12Resource> color_,depth_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_,dsv_;
    bool rendered_=false;
};
}


