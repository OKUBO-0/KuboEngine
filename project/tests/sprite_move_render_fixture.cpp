#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "sprite_move_render_fixture.h"
#include "Framework.h"
#include "DirectXCommon.h"
#include "OffscreenRenderManager.h"
#include "SrvManager.h"
#include "engine/assets/AssetImportJob.h"
#include "engine/scene/entity/EntityScene.h"
#include "engine/scene/prefab/RuntimePrefab.h"
#include "externals/DirectXTex/DirectXTex.h"
#include <d3d12sdklayers.h>
#include <array>
#include <fstream>
#include <iostream>
#include <thread>
#include <algorithm>
#include <vector>
#include <memory>
#include <chrono>
#include <exception>

namespace {
using namespace Engine;
namespace fs=std::filesystem;
using Pixel=std::array<unsigned char,4>;
using Quad=std::array<Pixel,4>;
const Pixel red{255,0,0,255},green{0,255,0,255},blue{0,0,255,255},white{255,255,255,255},magenta{255,0,255,255};
const Quad original{red,green,blue,white},changed{blue,white,green,red};
void Need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::string Read(const fs::path& path){std::ifstream in(path,std::ios::binary);Need(in.good(),"Sprite move render read failed");return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
void Write(const fs::path& path,const std::string& bytes){std::ofstream out(path,std::ios::binary);out<<bytes;out.close();Need(!out.fail(),"Sprite move render write failed");}
void SaveImage(const fs::path& path,const Quad& colors){
    DirectX::ScratchImage image;Need(SUCCEEDED(image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM,8,8,1,1)),"Image allocation failed");
    const auto* view=image.GetImage(0,0,0);for(size_t y=0;y<8;++y)for(size_t x=0;x<8;++x){const auto& color=colors[(y>=4?2:0)+(x>=4?1:0)];std::copy(color.begin(),color.end(),image.GetPixels()+y*view->rowPitch+x*4);}
    Need(SUCCEEDED(DirectX::SaveToDDSFile(image.GetImages(),image.GetImageCount(),image.GetMetadata(),DirectX::DDS_FLAGS_NONE,path.c_str())),"Image save failed");
}
class SpriteMoveRenderChecks final:public Base::Framework {
    fs::path root_,image_,moved_,manifest_;Scene::EntityWorld world_;Assets::AssetRegistry assets_;
    std::unique_ptr<Scene::EntityScene> scene_;Assets::AssetImportJob job_;Assets::AssetImportJob::Result completion_;
    Assets::AssetImportJob::Request request_;Prefab::PrefabValue prefab_;
    std::array<Pixel,2> clear_{};std::vector<std::pair<fs::path,std::string>> authored_;
    std::string imageBytes_,metadata_,guid_,movedBytes_;Scene::EntityId canvas_=0,sprite_=0,born_=0;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> messages_;unsigned indices_=0;int stage_=0;bool retired_=false,passed_=false;
    void GpuClean(){for(UINT64 i=0;i<messages_->GetNumStoredMessagesAllowedByRetrievalFilter();++i){SIZE_T count=0;messages_->GetMessage(i,nullptr,&count);std::vector<unsigned char> bytes(count);auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());Need(SUCCEEDED(messages_->GetMessage(i,message,&count)),"GPU diagnostic read failed");Need(message->Severity!=D3D12_MESSAGE_SEVERITY_ERROR && message->Severity!=D3D12_MESSAGE_SEVERITY_CORRUPTION,message->pDescription);}}
    std::array<Pixel,5> Capture(bool editor){
        auto* color=offscreenRenderManager->GetViewRenderTarget(editor);const auto desc=color->GetDesc();Need(desc.Width==(editor?480u:320u) && desc.Height==(editor?270u:180u),"View output extent mismatch");
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rows=0;UINT64 rowBytes=0,total=0;dxCommon->GetDevice()->GetCopyableFootprints(&desc,0,1,0,&footprint,&rows,&rowBytes,&total);
        D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=total;buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        Need(SUCCEEDED(dxCommon->GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))),"Readback allocation failed");
        D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=color;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprint;
        dxCommon->TransitionResource(color,D3D12_RESOURCE_STATE_COPY_SOURCE);dxCommon->GetCommandList()->CopyTextureRegion(&to,0,0,0,&from,nullptr);dxCommon->TransitionResource(color,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);dxCommon->CommandKick();
        void* mapped=nullptr;D3D12_RANGE range{0,size_t(total)};Need(SUCCEEDED(readback->Map(0,&range,&mapped)),"Readback map failed");std::array<Pixel,5> result{};
        const unsigned width=stage_>=18?320:static_cast<unsigned>(desc.Width),height=stage_>=18?180:desc.Height;
        for(size_t i=0;i<5;++i){const unsigned x=i==4?8:width*(i%2?3:2)/5,y=i==4?8:height*(i>=2?11:7)/18;const auto* pixel=static_cast<const unsigned char*>(mapped)+footprint.Offset+y*footprint.Footprint.RowPitch+x*4;std::copy_n(pixel,4,result[i].begin());}
        D3D12_RANGE written{0,0};readback->Unmap(0,&written);return result;
    }
public:
    ~SpriteMoveRenderChecks(){if(!root_.empty()){if(passed_){std::error_code error;fs::remove_all(root_,error);}else std::cerr<<"SPRITE_MOVE_RENDER_RETAINED="<<root_<<'\n';}}
    void Initialize() override {
        root_=fs::temp_directory_path()/(L"kubo-sprite-move-render-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));Need(fs::create_directory(root_),"Refuse existing render fixture");fs::create_directories(root_/"Assets/Moved");
        image_=root_/L"Assets/画像.dds";moved_=root_/L"Assets/Moved/移動.dds";manifest_=root_/"Game.kubo";SaveImage(image_,original);
        Write(manifest_,R"({"name":"Sprite moved pixels","assetDirectory":"Assets","startupScene":"entry.scene","gameOutput":{"width":320,"height":180}})");std::string error;
        Need(assets_.Scan(root_/"Assets",error),error.c_str());guid_=assets_.FindByPath(image_)->guid;imageBytes_=Read(image_);metadata_=Read(Assets::AssetRegistry::MetadataPath(image_));
        canvas_=world_.CreateEntity("Canvas");sprite_=world_.CreateEntity("Image",canvas_);Scene::CanvasComponent canvas;canvas.referenceSize={320,180};Need(world_.SetCanvas(canvas_,canvas),"Canvas setup");world_.Find(sprite_)->transform.position={160,90,0};Scene::SpriteComponent sprite;sprite.assetPath="Assets/画像.dds";sprite.assetGuid=guid_;sprite.size={128,80};Need(world_.SetSprite(sprite_,sprite),"Sprite setup");
        Need(world_.Save(root_/"entry.scene",error) && Prefab::CapturePrefabSubtree(world_,canvas_,prefab_,error),error.c_str());Write(root_/"Assets/ui.prefab",prefab_.dump());Need(assets_.Scan(root_/"Assets",error),error.c_str());
        for(const auto& relative:{"Game.kubo","entry.scene","Assets/ui.prefab","Assets/ui.prefab.kubo-meta.json"})authored_.emplace_back(relative,Read(root_/relative));
        request_={root_,root_/"Assets",{"画像.dds","Moved/移動.dds",guid_},false,manifest_};
        Microsoft::WRL::ComPtr<ID3D12Debug1> debug;Need(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))),"GPU validation unavailable");debug->EnableDebugLayer();debug->SetEnableGPUBasedValidation(TRUE);
        SetProjectRoot(root_);SetGameOutputExtent({320,180});Framework::Initialize();Need(SUCCEEDED(dxCommon->GetDevice()->QueryInterface(IID_PPV_ARGS(&messages_))),"GPU diagnostic queue missing");offscreenRenderManager->ResizeEditorView({480,270});
        Need(offscreenRenderManager->GetViewRenderTarget(true)!=offscreenRenderManager->GetViewRenderTarget(false),"Views share one target");scene_=std::make_unique<Scene::EntityScene>(world_,*dxCommon,assets_,root_,manifest_);scene_->Initialize();
    }
    void Update() override {
        Framework::Update();
        if(stage_==2){Need(job_.Start(request_),"Move worker start failed");const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);while(!job_.Poll(completion_)){Need(std::chrono::steady_clock::now()<deadline,"Move worker timeout");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            Need(completion_.success && completion_.moveCommitted,completion_.error.c_str());Need(!fs::exists(image_) && Read(moved_)==imageBytes_ && Read(Assets::AssetRegistry::MetadataPath(moved_))==metadata_,"Move changed original source/identity");
            // Distinguish the first load at the new path from old cached pixels.
            // This is a fixture edit before first new-path load, not hot reload.
            SaveImage(moved_,changed);movedBytes_=Read(moved_);}
        if(stage_==4){SaveImage(image_,Quad{magenta,magenta,magenta,magenta});Write(Assets::AssetRegistry::MetadataPath(image_),R"({"version":1,"guid":"eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee"})");}
        if(stage_==6){auto wrong=request_;wrong.projectFile=root_/"other.kubo";Need(!completion_.ApplyTo(assets_,wrong) && completion_.success,"Wrong manifest consumed move result");Need(completion_.ApplyTo(assets_,request_),"Move registry publication failed");}
        if(stage_==8)Write(root_/"Assets/.kubo/asset-move.pending.json","owned simulated pending move");
        if(stage_==10)Need(fs::remove(root_/"Assets/.kubo/asset-move.pending.json"),"Pending fixture removal failed");
        if(stage_==12)world_.Find(sprite_)->sprite.flipX=true;
        if(stage_==14)world_.Find(sprite_)->sprite.flipY=true;
        if(stage_==16){world_.Find(canvas_)->active=false;std::string error;Need(Prefab::InstantiateRuntimePrefab(world_,prefab_,0,born_,error),error.c_str());const auto* clone=world_.Find(born_+1);Need(born_ && clone && clone->parent==born_ && clone->sprite.assetGuid==guid_,"Prefab lost image GUID");}
        if(stage_==18)world_.Find(born_)->canvas.scaleMode=Scene::CanvasScaleMode::ConstantPixels;
        if(stage_==20)world_.Find(born_+1)->sprite.size.x=-1;
        if(stage_==22){world_.Find(born_+1)->sprite.size.x=128;world_.Find(born_)->active=false;}
    }
    void Draw() override {
        dxCommon->PrepareForFrame();scene_->SetUIOutputExtent({320,180});scene_->SynchronizeModels(0);const auto frame=dxCommon->GetCurrentFrameIndex();indices_|=1u<<frame;dxCommon->BeginGpuFrameTiming();
        offscreenRenderManager->Begin();srvManager->PreDraw();scene_->DrawGameView(16.f/9);offscreenRenderManager->End();
        scene_->SetUIOutputExtent({480,270});offscreenRenderManager->BeginEditorView();scene_->Draw();offscreenRenderManager->EndEditorView();
        dxCommon->Begin();offscreenRenderManager->Draw();dxCommon->End();
        const bool blank=(stage_>=2 && stage_<6) || (stage_>=8 && stage_<10) || stage_>=20;
        Quad expected=stage_<2?original:changed;if(stage_>=12 && stage_<14){std::swap(expected[0],expected[1]);std::swap(expected[2],expected[3]);}if(stage_>=14 && stage_<16){std::swap(expected[0],expected[3]);std::swap(expected[1],expected[2]);}
        for(int editor=0;editor<2;++editor){const auto pixels=Capture(editor!=0);if(stage_==0)clear_[editor]=pixels[4];Need(pixels[4]==clear_[editor],"Outside UI clear pixel changed");
            for(size_t i=0;i<4;++i)Need(pixels[i]==(blank?clear_[editor]:expected[i]),"Moved Sprite/flip/view pixel mismatch or stale fallback");
            Need(scene_->GetSpriteResult()==((stage_==20 || stage_==21)?Scene::RenderSpriteResult::InvalidComponent:Scene::RenderSpriteResult::Success),"Sprite frame admission mismatch");
            std::cout<<"SPRITE_MOVE_PIXEL stage="<<stage_<<" frame="<<frame<<" view="<<(editor?"scene":"game")<<" blank="<<blank<<" rgb=";
            for(size_t i=0;i<4;++i){if(i)std::cout<<'|';std::cout<<int(pixels[i][0])<<','<<int(pixels[i][1])<<','<<int(pixels[i][2]);}std::cout<<'\n';
        }GpuClean();++stage_;
    }
    bool IsEndRequest() const override{return stage_>=24 || Framework::IsEndRequest();}
    void Finalize() override {if(retired_)return;job_.CancelAndWait();if(dxCommon)dxCommon->WaitForAllFrames();if(scene_){scene_->Finalize();scene_.reset();}Framework::Finalize();retired_=true;if(messages_)GpuClean();}
    void Verify(){Need(retired_ && stage_==24 && (indices_&(indices_-1)),"Missing stages/frame indices/shutdown");for(const auto& [path,bytes]:authored_)Need(Read(root_/path)==bytes,"Sprite render modified authored Scene/Prefab/manifest");Need(Read(moved_)==movedBytes_ && Read(Assets::AssetRegistry::MetadataPath(moved_))==metadata_,"Renderer changed moved source/identity");passed_=true;
        std::cout<<"PASS: actual EntityScene moved Sprite Game320x180/Scene480x270 pixels48, real worker+manifest-owned registry publication, stale/missing/old-name replacement/pending refusal, new-path image versus old GPU cache, asymmetric flips, runtime Prefab, constant pixels, invalid/hidden no stale draw, multiple swapchain indices, authored bytes and GPU errors0/shutdown (not physical Browser/hot reload)\n";}
};
}
void RunSpriteMoveRenderChecks(){auto checks=std::make_unique<SpriteMoveRenderChecks>();try{checks->Run();checks->Verify();}catch(...){auto failure=std::current_exception();try{checks->Finalize();}catch(const std::exception& error){std::cerr<<"Sprite move cleanup: "<<error.what()<<'\n';}std::rethrow_exception(failure);}}
