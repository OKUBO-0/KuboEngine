#include "AssetThumbnailCache.h"
#include "AssetModelThumbnail.h"
#include "engine/assets/AssetRegistry.h"
#include "engine/base/directx/DirectXCommon.h"
#include "engine/base/directx/TextureManager.h"
#include "engine/3d/model/ModelManager.h"
#include "engine/base/AssetPath.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace Engine::Editor {
namespace {
using Assets::AssetRecord;using Assets::AssetType;
bool Same(const AssetRecord& a,const AssetRecord& b){
    return a.guid==b.guid&&a.type==b.type&&a.relativePath==b.relativePath&&a.absolutePath==b.absolutePath&&
        a.size==b.size&&a.modifiedTicks==b.modifiedTicks&&a.fingerprint==b.fingerprint&&a.importFingerprint==b.importFingerprint;
}
bool Eligible(const AssetRecord& asset){return asset.type==AssetType::Texture||asset.type==AssetType::Model;}
}
struct AssetThumbnailCache::State {
    struct Entry {
        AssetRecord asset;
        uint64_t serial=0,lastRequest=0,gui=0;
        uint32_t width=0,height=0;
        AssetThumbnailView::Status status=AssetThumbnailView::Status::Pending;
        std::shared_ptr<const Base::TextureLease> texture;
        std::unique_ptr<AssetModelThumbnail> model;
        bool heavyModel=false,imageInFlight=false;
    };
    std::shared_ptr<Base::DirectXCommon> graphics;
    ThumbnailGuiSlots slots;
    std::filesystem::path project,file,root;
    std::vector<Entry> entries;
    std::vector<AssetRecord> queue;
    AssetThumbnailImageJob worker;
    uint64_t nextSerial=0,frame=0;
    bool drained=false,collectModels=false,collectTextures=false;
    State(){entries.reserve(Capacity);queue.reserve(Capacity);}
    void Drain(){if(!drained&&graphics){graphics->CommandKick();graphics->WaitForAllFrames();drained=true;}}
    void Retire(Entry& entry){
        if(entry.gui||entry.model||entry.texture){Drain();if(entry.gui){slots.release(entry.gui);entry.gui=0;}}
        if(entry.model){entry.model->ReleaseAfterGpuIdle();entry.model.reset();collectModels=true;collectTextures=true;}
        if(entry.texture){entry.texture.reset();collectTextures=true;}
    }
    void Collect(){
        if(collectModels){Graphics3D::ModelManager::GetInstance()->CollectUnusedModels();collectModels=false;}
        if(collectTextures){Base::TextureManager::GetInstance()->CollectUnusedTextures();collectTextures=false;}
    }
    bool Current(const Entry& entry,const Assets::AssetRegistry& registry) const {
        const auto* current=registry.FindByGuid(entry.asset.guid);return current&&Same(*current,entry.asset);
    }
    void AdmitQueued(const Assets::AssetRegistry& registry){
        for(auto& asset:queue){
            const auto* current=registry.FindByGuid(asset.guid);if(!current||!Same(asset,*current))continue;
            auto found=std::find_if(entries.begin(),entries.end(),[&](const Entry& entry){return Same(entry.asset,asset);});
            if(found!=entries.end()){found->lastRequest=frame;continue;}
            if(entries.size()==Capacity){
                auto victim=std::min_element(entries.begin(),entries.end(),[](const Entry& a,const Entry& b){return a.lastRequest<b.lastRequest;});
                // All entries requested in this UI frame have equal admission
                // priority. Defer rather than churn the whole cache every frame.
                if(victim->lastRequest>=frame-1)continue;
                Retire(*victim);entries.erase(victim);
            }
            if(nextSerial==UINT64_MAX)throw std::overflow_error("Thumbnail request serial exhausted");
            Entry entry;entry.asset=std::move(asset);entry.serial=++nextSerial;entry.lastRequest=frame;entries.push_back(std::move(entry));
        }
        queue.clear();
    }
};
AssetThumbnailCache::AssetThumbnailCache():state_(std::make_unique<State>()){}
AssetThumbnailCache::~AssetThumbnailCache(){
    // A host must perform explicit retirement before shared services disappear.
    if(!state_->entries.empty())std::terminate();
}
void AssetThumbnailCache::BeforeUi(std::shared_ptr<Base::DirectXCommon> graphics,Base::SrvManager* srv,
    ThumbnailGuiSlots slots,const std::filesystem::path& projectRoot,const std::filesystem::path& projectFile,
    const Assets::AssetRegistry& registry){
    if(!graphics||!srv||!slots.createTextureSrv||!slots.createModelSrv||!slots.release)throw std::invalid_argument("Thumbnail rendering owners are missing");
    auto& s=*state_;s.drained=false;
    const auto project=std::filesystem::absolute(projectRoot).lexically_normal(),file=projectFile.lexically_normal(),root=registry.GetRoot().lexically_normal();
    if(s.graphics&&(s.graphics!=graphics||s.project!=project||s.file!=file||s.root!=root)){
        s.worker.Cancel();s.queue.clear();for(auto& entry:s.entries)s.Retire(entry);s.entries.clear();s.Collect();
    }
    s.graphics=std::move(graphics);s.slots=std::move(slots);s.project=project;s.file=file;s.root=root;
    if(s.frame==UINT64_MAX)throw std::overflow_error("Thumbnail frame serial exhausted");++s.frame;
    for(auto it=s.entries.begin();it!=s.entries.end();){
        if(!s.Current(*it,registry)||s.frame-it->lastRequest>120){s.Retire(*it);it=s.entries.erase(it);}
        else {if(it->heavyModel&&it->model&&it->model->Rendered()){s.Drain();it->model->ReleaseModelAfterGpuIdle();it->heavyModel=false;s.collectModels=true;s.collectTextures=true;}++it;}
    }
    ThumbnailImageResult result;
    if(s.worker.Poll(result)){
        auto entry=std::find_if(s.entries.begin(),s.entries.end(),[&](const State::Entry& item){return item.serial==result.serial;});
        if(entry!=s.entries.end()&&s.Current(*entry,registry)){
            if(result.status==ThumbnailImageStatus::Ready){
                try {
                    s.drained=false;
                    entry->texture=Base::TextureManager::GetInstance()->AcquireTextureFromMemory("__thumbnail/"+std::to_string(reinterpret_cast<uintptr_t>(this))+"/"+std::to_string(entry->serial),result.png.data(),result.png.size(),false);
                    entry->gui=s.slots.createTextureSrv(*entry->texture);
                    if(!entry->gui)throw std::runtime_error("No thumbnail UI slot");
                    entry->width=result.width;entry->height=result.height;entry->status=AssetThumbnailView::Status::Ready;
                }catch(...){s.Retire(*entry);entry->status=AssetThumbnailView::Status::Failed;}
            }else entry->status=AssetThumbnailView::Status::Failed;
        }
    }
    s.AdmitQueued(registry);
    // At most one new CPU job OR one prepared Model per host frame. Already
    // completed image publication above is also bounded to a single result.
    for(auto& entry:s.entries){
        if(entry.status!=AssetThumbnailView::Status::Pending||entry.model||entry.imageInFlight)continue;
        if(entry.asset.type==AssetType::Texture){
            if(!s.worker.IsBusy()){
                ThumbnailImageRequest request;request.serial=entry.serial;request.assetRoot=s.root;request.source=Base::PathFromUtf8(entry.asset.absolutePath);
                request.expectedBytes=entry.asset.size;request.expectedModifiedTicks=entry.asset.modifiedTicks;
                if(s.worker.Start(std::move(request)))entry.imageInFlight=true;
            }
            break;
        }
        entry.model=std::make_unique<AssetModelThumbnail>();
        try {
            s.drained=false;
            if(!entry.asset.size||entry.asset.size>ThumbnailMaximumFileBytes)throw std::runtime_error("Model thumbnail source exceeds byte limit");
            entry.model->Prepare(s.graphics,Base::PathToUtf8(s.root),entry.asset.relativePath,
                "__thumbnail/"+entry.asset.guid+":"+entry.asset.fingerprint+":"+std::to_string(entry.asset.modifiedTicks)+":"+std::to_string(entry.asset.size),entry.asset.importFingerprint);
            entry.heavyModel=true;entry.gui=s.slots.createModelSrv(entry.model->Image());
            if(!entry.gui)throw std::runtime_error("No model thumbnail UI slot");entry.width=entry.height=AssetModelThumbnail::Edge;
        }catch(...){s.Retire(entry);entry.status=AssetThumbnailView::Status::Failed;}
        break;
    }
    s.Collect();
}
AssetThumbnailView AssetThumbnailCache::Request(const Assets::AssetRecord& asset){
    auto& s=*state_;if(!Eligible(asset))return {0,0,0,AssetThumbnailView::Status::Unsupported};
    const auto entry=std::find_if(s.entries.begin(),s.entries.end(),[&](const State::Entry& item){return Same(item.asset,asset);});
    if(entry!=s.entries.end()){entry->lastRequest=s.frame;return {entry->status==AssetThumbnailView::Status::Ready?entry->gui:0,entry->status==AssetThumbnailView::Status::Ready?entry->width:0,entry->status==AssetThumbnailView::Status::Ready?entry->height:0,entry->status};}
    if(s.queue.size()<Capacity&&std::none_of(s.queue.begin(),s.queue.end(),[&](const AssetRecord& queued){return Same(queued,asset);}))s.queue.push_back(asset);
    return {};
}
void AssetThumbnailCache::DrawPrepared(){
    auto& s=*state_;for(auto& entry:s.entries)if(entry.model&&!entry.model->Rendered()&&entry.status==AssetThumbnailView::Status::Pending){entry.model->Draw();entry.status=AssetThumbnailView::Status::Ready;}
}
void AssetThumbnailCache::ReleaseAfterGpuIdle(){
    auto& s=*state_;s.worker.CancelAndWait();s.queue.clear();s.drained=true;
    // Join before the editor's shared services retire. No late result remains.
    for(auto& entry:s.entries)s.Retire(entry);s.entries.clear();s.graphics.reset();s.slots={};
}
size_t AssetThumbnailCache::CachedCount() const noexcept {return state_->entries.size();}
}


