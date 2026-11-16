#pragma once
#include "AssetThumbnailImage.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>
#include <string>

struct ID3D12Resource;
namespace Engine::Base {class DirectXCommon;class SrvManager;struct TextureLease;}
namespace Engine::Assets {class AssetRegistry;struct AssetRecord;}
namespace Engine::Editor {
class AssetModelThumbnail;
// Host boundary: all returned handles belong to the ImGui heap.
// Allocate callbacks run only before UI construction. Release runs after a drain.
struct ThumbnailGuiSlots {
    std::function<uint64_t(const Base::TextureLease&)> createTextureSrv;
    std::function<uint64_t(ID3D12Resource*)> createModelSrv;
    std::function<void(uint64_t)> release;
};
struct AssetThumbnailView {
    uint64_t texture=0;
    uint32_t width=0,height=0;
    enum class Status {Pending,Ready,Failed,Unsupported} status=Status::Pending;
};
class AssetThumbnailCache final {
public:
    AssetThumbnailCache();
    ~AssetThumbnailCache();
    AssetThumbnailCache(const AssetThumbnailCache&)=delete;
    AssetThumbnailCache& operator=(const AssetThumbnailCache&)=delete;
    // One Game owns this cache. Every GPU mutation/eviction occurs before Begin.
    void BeforeUi(std::shared_ptr<Base::DirectXCommon> graphics,Base::SrvManager* srv,
        ThumbnailGuiSlots slots,const std::filesystem::path& projectRoot,
        const std::filesystem::path& projectFile,const Assets::AssetRegistry& registry);
    // No GPU calls or borrowed registry pointers retained here. Bounded queue.
    AssetThumbnailView Request(const Assets::AssetRecord& asset);
    void DrawPrepared();
    // Call after graphics PrepareForShutdown, before ImGui/shared managers retire.
    // Worker cancellation/join is independent of a live registry or World.
    void ReleaseAfterGpuIdle();
    size_t CachedCount() const noexcept;
    static constexpr size_t Capacity=32;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}


