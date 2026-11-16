#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "asset_thumbnail_fixture.h"
#include "Game.h"
#include "DebugEditorManager.h"
#include "ImGuiManager.h"
#include "DirectXCommon.h"
#include "OffscreenRenderManager.h"
#include "SrvManager.h"
#include "TextureManager.h"
#include "CameraManager.h"
#include "WinApp.h"
#include "engine/base/AssetPath.h"
#include "engine/base/EngineResourcePath.h"
#include "engine/assets/AssetImportFingerprint.h"
#include <d3d12sdklayers.h>
#include <imgui_internal.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>

#if !defined(KUBO_APPLICATION_CHECKS)
#error Thumbnail integration checks must never enter normal engine builds
#endif
namespace {
using namespace Engine;namespace fs=std::filesystem;
void Need(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
struct Row {uint64_t texture=0;Vector2 min{},max{};bool selected=false;};
std::map<std::string,Row> rows;
void Observe(const char* guid,uint64_t texture,Vector2 min,Vector2 max,bool selected){rows[guid]={texture,min,max,selected};}
void Write(const fs::path& file,const std::string& bytes){std::ofstream out(file,std::ios::binary);out<<bytes;out.close();Need(!out.fail(),"Thumbnail fixture write");}
std::string Read(const fs::path& file){std::ifstream in(file,std::ios::binary);Need(in.good(),"Thumbnail fixture read");return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
void SaveImage(const fs::path& file,bool blue){
    DirectX::ScratchImage image;Need(SUCCEEDED(image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM,16,8,1,1)),"Browser image fixture allocation");const auto* source=image.GetImage(0,0,0);
    for(size_t y=0;y<8;++y)for(size_t x=0;x<16;++x){auto* pixel=source->pixels+y*source->rowPitch+x*4;pixel[0]=!blue&&x<8?255:0;pixel[1]=!blue&&x>=8?255:0;pixel[2]=blue?255:0;pixel[3]=255;}
    Need(SUCCEEDED(DirectX::SaveToDDSFile(*source,DirectX::DDS_FLAGS_NONE,file.c_str())),"Browser image fixture save");
}
}
namespace Engine::Scene {
class GameThumbnailChecksAccess {
public:
    static void Windows(Game& game,bool browser){
        game.debugAssetBrowserOpen_=browser;game.debugStatsOpen_=game.debugKeyInputOpen_=game.debugOffscreenOpen_=false;
        game.debugAudioOpen_=game.debugConsoleOpen_=false;
        game.genericInspectorOpen_=game.genericHierarchyOpen_=false;game.debugGameViewOpen_=false;
    }
    static std::string World(const Game& game){return game.editorWorld_.Capture().dump();}
    static size_t History(const Game& game){return game.entityHistory_.UndoCount();}
    static void Refresh(Game& game,const fs::path& root){game.assetImportJob_.CancelAndWait();std::string error;Need(game.projectAssets_.Scan(root/"Assets",error),error);}
    static Assets::AssetRecord Asset(const Game& game,const std::string& guid){const auto* asset=game.projectAssets_.FindByGuid(guid);Need(asset,"Browser fixture lost GUID");return *asset;}
    static Editor::AssetThumbnailView View(Game& game,const std::string& guid){return game.assetThumbnails_.Request(Asset(game,guid));}
    static bool RevealFromDiagnostic(Game& game,const std::string& guid){
        return game.OpenDiagnosticAsset(game.DiagnosticTarget(0,guid));
    }
    static size_t Cached(const Game& game){return game.assetThumbnails_.CachedCount();}
    static size_t AssetCount(const Game& game) { return game.projectAssets_.GetAssets().size(); }
    static std::string GuidAt(const Game& game, const fs::path& path) {
        const auto* asset = game.projectAssets_.FindByPath(path);
        Need(asset != nullptr, "Large list last asset missing");
        return asset->guid;
    }
    static void Clear(Game& game){game.assetThumbnails_.ReleaseAfterGpuIdle();}
};
}
namespace {
class Checks final:public Scene::Game {
    static constexpr unsigned bulkAssetCount_ = 512;
    static constexpr unsigned finishedPhase_ = 11;
    fs::path root_,image_,model_;std::string imageGuid_,modelGuid_,badGuid_,world_,sceneFile_,modelFile_;
    std::string lastBulkGuid_;
    unsigned frames_=0,phase_=0,click_=0,wait_=0,indices_=0;bool passed_=false,retired_=false,sawPending_=false;
    std::vector<unsigned char> baseline_;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> messages_;
    decltype(Editor::assetBrowserChecksObserver) previousObserver_=nullptr;
    using Access=Scene::GameThumbnailChecksAccess;
    ImGuiWindow* AssetList() const {
        auto* browser = ImGui::FindWindowByName("Asset Browser");
        Need(browser != nullptr, "Asset Browser window missing");
        char childName[64];
        ImFormatString(childName, sizeof(childName), "Asset Browser/AssetList_%08X", browser->GetID("AssetList"));
        auto* child = ImGui::FindWindowByName(childName);
        Need(child != nullptr, "Asset list scroll window missing");
        return child;
    }
    void PopulateLargeList() {
        auto directory = root_ / L"Assets/z-日本語";
        fs::create_directory(directory);
        fs::path last;
        for (unsigned index = 0; index < bulkAssetCount_; ++index) {
            last = directory / ("script-" + std::to_string(1000 + index) + ".lua");
            Write(last, "-- asset browser list fixture\n");
        }
        Access::Refresh(*this, root_);
        lastBulkGuid_ = Access::GuidAt(*this, last);
    }
    void CheckLargeList() {
        const auto total = Access::AssetCount(*this);
        Need(total >= bulkAssetCount_, "Asset Browser lost the populated large list");
        const auto* child = AssetList();
        // The child calculates its scroll range from the previous frame's content.
        if (phase_ == 7 && wait_ == 1) return;
        const auto visibleBudget = static_cast<size_t>(
            std::ceil(child->InnerRect.GetHeight() / (68.0f + ImGui::GetStyle().ItemSpacing.y))) + 2;
        Need(!rows.empty() && rows.size() <= visibleBudget && child->ScrollMax.y > 20000,
            "Asset Browser submitted rows beyond its visible range or lost scroll range");
        if (phase_ == 7 || phase_ == 9) {
            Need(rows.contains(imageGuid_) && !rows.contains(lastBulkGuid_), "Asset Browser did not show the top of the list");
        } else {
            Need(rows.contains(lastBulkGuid_) && !rows.contains(imageGuid_), "Asset Browser could not reach the last row");
        }
        if(phase_==10)Need(rows.at(lastBulkGuid_).selected,"Diagnostic reveal did not select the nested Asset");
        std::cout << "BROWSER_LARGE_LIST phase=" << phase_ << " total=" << total
            << " submitted=" << rows.size() << " visible_budget=" << visibleBudget << '\n';
        ++phase_;
    }
    void GpuClean(){if(!messages_)return;for(UINT64 i=0;i<messages_->GetNumStoredMessagesAllowedByRetrievalFilter();++i){SIZE_T size=0;messages_->GetMessage(i,nullptr,&size);std::vector<unsigned char> bytes(size);auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());Need(SUCCEEDED(messages_->GetMessage(i,message,&size)),"Browser GPU message read");Need(message->Severity!=D3D12_MESSAGE_SEVERITY_ERROR&&message->Severity!=D3D12_MESSAGE_SEVERITY_CORRUPTION,message->pDescription);}}
    std::vector<unsigned char> Pixels(){
        auto* color=offscreenRenderManager->GetViewRenderTarget(false);const auto desc=color->GetDesc();Need(desc.Width==1280&&desc.Height==720,"Browser readback extent");
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rowCount=0;UINT64 rowBytes=0,total=0;dxCommon->GetDevice()->GetCopyableFootprints(&desc,0,1,0,&footprint,&rowCount,&rowBytes,&total);
        D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=total;buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;Microsoft::WRL::ComPtr<ID3D12Resource> readback;Need(SUCCEEDED(dxCommon->GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))),"Browser readback allocation");
        D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=color;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprint;
        dxCommon->TransitionResource(color,D3D12_RESOURCE_STATE_COPY_SOURCE);dxCommon->GetCommandList()->CopyTextureRegion(&to,0,0,0,&from,nullptr);dxCommon->TransitionResource(color,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);dxCommon->CommandKick();
        void* memory=nullptr;D3D12_RANGE range{0,size_t(total)};Need(SUCCEEDED(readback->Map(0,&range,&memory)),"Browser readback map");std::vector<unsigned char> result(1280*720*4);
        for(size_t y=0;y<720;++y)std::memcpy(result.data()+y*1280*4,static_cast<unsigned char*>(memory)+footprint.Offset+y*footprint.Footprint.RowPitch,1280*4);
        D3D12_RANGE written{0,0};readback->Unmap(0,&written);return result;
    }
    void UiPixels(bool blue){
        // Render the actual completed Game UI data into the owned full-size
        // target after draining its first submission. No test-only image widget.
        dxCommon->CommandKick();dxCommon->WaitForAllFrames();offscreenRenderManager->Begin();imGuiManager->Draw();offscreenRenderManager->End();
        const auto pixels=Pixels();const auto row=rows.at(imageGuid_);
        const auto pixel=[&](int x,int y){Need(x>=0&&x<1280&&y>=0&&y<720,"Browser preview outside readback");const auto offset=(size_t(y)*1280+size_t(x))*4;return std::array<unsigned char,3>{pixels[offset],pixels[offset+1],pixels[offset+2]};};
        const auto left=pixel(int(row.min.x+16),int(row.min.y+32)),right=pixel(int(row.min.x+48),int(row.min.y+32));
        Need(left==(blue?std::array<unsigned char,3>{0,0,255}:std::array<unsigned char,3>{255,0,0})&&right==(blue?std::array<unsigned char,3>{0,0,255}:std::array<unsigned char,3>{0,255,0}),"Actual Browser Texture thumbnail RGB/aspect/refresh mismatch");
        const auto modelRow=rows.at(modelGuid_);unsigned geometry=0;const auto clear=pixel(int(modelRow.min.x+2),int(modelRow.min.y+2));
        for(int y=int(modelRow.min.y+4);y<int(modelRow.max.y-4);++y)for(int x=int(modelRow.min.x+4);x<int(modelRow.max.x-4);++x)if(pixel(x,y)!=clear)++geometry;
        Need(geometry>30&&geometry<3000,"Actual Browser Model thumbnail has no fitted geometry");
        std::cout<<"BROWSER_THUMBNAIL_PIXEL phase="<<phase_<<" blue="<<blue<<" modelPixels="<<geometry<<" frame="<<frames_<<'\n';
    }
    void ViewOwnership(){
        auto* textures=Base::TextureManager::GetInstance();
        auto lease=textures->AcquireTextureRevision(Base::PathToUtf8(image_),Assets::HashAssetContent(image_));
        bool refused=false;try{textures->CreateLeasedTexture2DView(*lease,{});}catch(const std::invalid_argument&){refused=true;}Need(refused,"Texture view accepted missing destination");
        const auto forged=*lease;
        for(unsigned i=0;i<80;++i){refused=false;try{imGuiManager->CreateExternalTextureSrv(forged);}catch(const std::invalid_argument&){refused=true;}Need(refused,"Forged Texture lease accepted or failed UI slot was leaked");}
        const auto texture=imGuiManager->CreateExternalTextureSrv(*lease);Need(texture!=0,"Managed Texture view missing");
        dxCommon->CommandKick();dxCommon->WaitForAllFrames();
        refused=false;try{imGuiManager->ReleaseExternalTextureAfterGpuIdle(offscreenRenderManager->GetImGuiSceneTextureHandle().ptr);}catch(const std::invalid_argument&){refused=true;}Need(refused,"Preview release stole reserved Scene slot");
        imGuiManager->ReleaseExternalTextureAfterGpuIdle(texture);refused=false;try{imGuiManager->ReleaseExternalTextureAfterGpuIdle(texture);}catch(const std::invalid_argument&){refused=true;}Need(refused,"Preview double release accepted");
        const auto cubePath=Base::ResolveEngineResource("Resources/textures/skybox/test.dds");
        auto cube=textures->AcquireTextureRevision(Base::PathToUtf8(cubePath),Assets::HashAssetContent(cubePath));
        refused=false;try{imGuiManager->CreateExternalTextureSrv(*cube);}catch(const std::invalid_argument&){refused=true;}Need(refused,"TextureCube admitted as Texture2D preview");
        cube.reset();lease.reset();textures->CollectUnusedTextures();Need(imGuiManager->GetInvalidDescriptorReleaseCount()==0,"Preview altered backend descriptor ownership");
        std::cout<<"BROWSER_THUMBNAIL_VIEW_OWNERSHIP=managed lease/missing destination/forged lease80/no slot leak/reserved slot/double release/cube refusal\n";
    }
public:
    ~Checks(){Editor::assetBrowserChecksObserver=previousObserver_;if(!root_.empty()){if(passed_){std::error_code error;fs::remove_all(root_,error);}else std::cerr<<"BROWSER_THUMBNAIL_RETAINED="<<root_<<'\n';}}
    void Initialize() override {
        root_=fs::temp_directory_path()/(L"kubo-browser-thumbnails-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-日本語");Need(fs::create_directory(root_),"Refuse existing Browser fixture");Need(fs::create_directory(root_/"Assets"),"Browser asset directory");
        image_=root_/L"Assets/a-画像.dds";model_=root_/L"Assets/b-三角.obj";SaveImage(image_,false);
        modelFile_="v -1 -1 0\nv 1 -1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0.5 1\nvn 0 0 -1\nvn 0 0 1\nf 1/1/1 3/3/1 2/2/1\nf 1/1/2 2/2/2 3/3/2\n";Write(model_,modelFile_);Write(root_/L"Assets/c-壊れ.png","not an image");
        Assets::AssetRegistry assets;std::string error;Need(assets.Scan(root_/"Assets",error),error);imageGuid_=assets.FindByPath(image_)->guid;modelGuid_=assets.FindByPath(model_)->guid;badGuid_=assets.FindByPath(root_/L"Assets/c-壊れ.png")->guid;
        Scene::EntityWorld world;const auto model=world.CreateEntity("Unchanged model");Need(world.SetModel(model,"Assets/b-三角.obj",modelGuid_),"Browser World model");const auto camera=world.CreateEntity("Game camera");world.Find(camera)->transform.position={0,0,-5};Scene::CameraComponent projection;projection.orthographic=true;projection.orthographicHeight=4;Need(world.SetCamera(camera,projection)&&world.Save(root_/"entry.scene",error),error);
        Write(root_/"Game.kubo",R"({"name":"Browser thumbnails","startupScene":"entry.scene","assetDirectory":"Assets","gameOutput":{"width":1280,"height":720}})");sceneFile_=Read(root_/"entry.scene");
        Microsoft::WRL::ComPtr<ID3D12Debug1> debug;Need(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))),"Browser GPU validation unavailable");debug->EnableDebugLayer();debug->SetEnableGPUBasedValidation(TRUE);
        SetProjectRoot(root_);SetProjectFile(root_/"Game.kubo");SetProjectAssetRoot(root_/"Assets");SetGameOutputExtent({1280,720});SetEditorMode(true);Need(ConfigureStartupScene(root_/"entry.scene",error),error);Game::Initialize();Access::Windows(*this,false);
        Need(SUCCEEDED(dxCommon->GetDevice()->QueryInterface(IID_PPV_ARGS(&messages_))),"Browser GPU queue");ViewOwnership();world_=Access::World(*this);previousObserver_=Editor::assetBrowserChecksObserver;Editor::assetBrowserChecksObserver=Observe;ImGui::GetIO().ConfigInputTrickleEventQueue=false;
    }
    void Update() override {
        if(phase_==finishedPhase_)return;Need(frames_<150,"Browser thumbnail phase did not complete within frame bound");
        if(auto* window=ImGui::FindWindowByName("Asset Browser")){ImGui::SetWindowDock(window,0,ImGuiCond_Always);ImGui::SetWindowPos("Asset Browser",{20,55},ImGuiCond_Always);ImGui::SetWindowSize("Asset Browser",{1100,590},ImGuiCond_Always);ImGui::FocusWindow(window);}
        if(phase_==2&&rows.contains(imageGuid_)){
            const auto row=rows.at(imageGuid_);auto& io=ImGui::GetIO();io.AddMousePosEvent(row.min.x+110,row.min.y+24);
            if(click_==1)io.AddMouseButtonEvent(0,true);if(click_==2)io.AddMouseButtonEvent(0,false);++click_;
        }
        if(phase_==3&&wait_==0){const auto stamp=fs::last_write_time(image_);SaveImage(image_,true);fs::last_write_time(image_,stamp+std::chrono::seconds(1));Access::Refresh(*this,root_);}
        if(phase_==4&&wait_==0){const auto next=root_/L"Assets/a-移動.dds";fs::rename(image_,next);fs::rename(Assets::AssetRegistry::MetadataPath(image_),Assets::AssetRegistry::MetadataPath(next));image_=next;Access::Refresh(*this,root_);Need(Access::Asset(*this,imageGuid_).absolutePath==Base::PathToUtf8(image_),"Moved thumbnail GUID did not follow path");}
        if(phase_==5&&wait_==0){dxCommon->CommandKick();dxCommon->WaitForAllFrames();Access::Clear(*this);Need(Access::Cached(*this)==0,"Browser explicit cache retirement retained entries");}
        if(phase_==6&&wait_==0){Write(root_/"Other.kubo",Read(root_/"Game.kubo"));SetProjectFile(root_/"Other.kubo");}
        if (phase_ == 7 && lastBulkGuid_.empty()) PopulateLargeList();
        if (phase_ == 8 || phase_ == 9) {
            auto* list = AssetList();
            ImGui::SetScrollY(list, phase_ == 8 ? list->ScrollMax.y : 0.0f);
        }
        if(phase_==10)Need(Access::RevealFromDiagnostic(*this,lastBulkGuid_),"Diagnostic did not request the nested Asset");
        rows.clear();Game::Update();Need(Access::World(*this)==world_&&Access::History(*this)==0&&!IsSceneDirty(),"Browser changed authored World/history/document");
        if(phase_>=3&&phase_<=6&&wait_==0)sawPending_=!rows.contains(imageGuid_)||!rows.at(imageGuid_).texture;
        ++wait_;
    }
    void Draw() override {
        Game::Draw();indices_|=1u<<dxCommon->GetCurrentFrameIndex();Need(Access::Cached(*this)<=Editor::AssetThumbnailCache::Capacity,"Browser cache exceeded cap");
        if(phase_==0){baseline_=Pixels();Access::Windows(*this,true);phase_=1;wait_=0;}
        else if (phase_ >= 7) CheckLargeList();
        else {
            const bool ready=rows.contains(imageGuid_)&&rows.contains(modelGuid_)&&rows.at(imageGuid_).texture&&rows.at(modelGuid_).texture&&Access::View(*this,badGuid_).status==Editor::AssetThumbnailView::Status::Failed;
            if(phase_==2){if(click_>=3){Need(rows.at(imageGuid_).selected,"Actual Browser selection mouse click failed");phase_=3;wait_=0;}}
            else if(ready){
                Need(Pixels()==baseline_,"Thumbnail changed actual Game output");UiPixels(phase_>=3);
                Need(Access::View(*this,badGuid_).status==Editor::AssetThumbnailView::Status::Failed,"Broken thumbnail silently accepted or retried indefinitely");
                if(phase_==1){phase_=2;click_=0;}else {Need(sawPending_,"Stale/retired thumbnail was exposed before regeneration");++phase_;}wait_=0;
            }
        }
        GpuClean();Need(Access::World(*this)==world_&&Access::History(*this)==0&&!IsSceneDirty(),"Browser rendering changed authored data");std::cout<<"BROWSER_THUMBNAIL_FRAME phase="<<phase_<<" frame="<<frames_<<" cache="<<Access::Cached(*this)<<'\n';++frames_;
    }
    bool IsEndRequest() const override{return phase_==finishedPhase_||Game::IsEndRequest();}
    void Finalize() override{if(retired_)return;Game::Finalize();retired_=true;GpuClean();}
    void Verify(){Need(retired_&&phase_==finishedPhase_&&(indices_&(indices_-1)),"Browser thumbnail lifecycle incomplete");Need(Read(root_/"entry.scene")==sceneFile_&&Read(model_)==modelFile_,"Browser rewrote Scene/model");passed_=true;}
};
}
void RunAssetThumbnailChecks(){auto checks=std::make_unique<Checks>();checks->Run();checks->Verify();std::cout<<"PASS: actual Game Asset Browser Texture RGB/Model geometry/selection, Japanese GUID update+move, stale removal+regeneration/cache retirement, unchanged Game output/World/history/Scene/model, bounded cache and GPU errors0/shutdown\n";}
