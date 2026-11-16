#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "player_sprite_fixture.h"
#include "engine/app/RuntimeGame.h"
#include "engine/scene/entity/EntityScene.h"
#include "engine/base/AssetPath.h"
#include "engine/base/EngineResourcePath.h"
#include "engine/base/ProjectContext.h"
#include "SceneManager.h"
#include "DirectXCommon.h"
#include "OffscreenRenderManager.h"
#include <d3d12sdklayers.h>
#include <Windows.h>
#include <fstream>
#include <array>
#include <iostream>
#include <memory>
#include <exception>
#include <algorithm>
#include <vector>

namespace {
void Need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::string Read(const std::filesystem::path& path){std::ifstream input(path,std::ios::binary);Need(input.good(),"UI fixture read failed");return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};}
void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream output(path,std::ios::binary);output<<bytes;output.close();Need(!output.fail(),"UI fixture write failed");}
class PlayerSpriteChecks final : public Engine::App::RuntimeGame {
    std::filesystem::path root_;
    std::filesystem::path manifest_;
    std::array<std::string,3> original_;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> queue_;
    Engine::Scene::SpriteComponent base_,overlay_;
    int draws_=0;unsigned indices_=0;bool retired_=false;
    bool scripted_=false,fixed_=false;int updates_=0;
    std::vector<std::pair<std::filesystem::path,std::string>> scriptBytes_;
    std::array<unsigned char,4> baseline_{};
    // Read actual offscreen pixels, not screenshot/display scaling or counters.
    std::array<std::array<unsigned char,4>,3> Pixels(){
        auto* color=offscreenRenderManager->GetViewRenderTarget(false);const auto desc=color->GetDesc();
        Need(desc.Width==320 && desc.Height==180,"UI output extent not used");
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rows=0;UINT64 rowBytes=0,total=0;
        dxCommon->GetDevice()->GetCopyableFootprints(&desc,0,1,0,&footprint,&rows,&rowBytes,&total);
        D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=total;buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        Need(SUCCEEDED(dxCommon->GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))),"UI readback allocation failed");
        D3D12_TEXTURE_COPY_LOCATION source{};source.pResource=color;source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION destination{};destination.pResource=readback.Get();destination.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;destination.PlacedFootprint=footprint;
        dxCommon->TransitionResource(color,D3D12_RESOURCE_STATE_COPY_SOURCE);dxCommon->GetCommandList()->CopyTextureRegion(&destination,0,0,0,&source,nullptr);dxCommon->TransitionResource(color,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);dxCommon->CommandKick();
        void* mapped=nullptr;D3D12_RANGE range{0,size_t(total)};Need(SUCCEEDED(readback->Map(0,&range,&mapped)),"UI map failed");
        std::array<std::array<unsigned char,4>,3> result{};const unsigned xs[]{160,80,110};
        for(size_t i=0;i<result.size();++i){const auto* pixel=static_cast<const unsigned char*>(mapped)+footprint.Offset+90*footprint.Footprint.RowPitch+xs[i]*4;std::copy_n(pixel,4,result[i].begin());}
        D3D12_RANGE written{0,0};readback->Unmap(0,&written);return result;
    }
    void CleanGpu(){
        for(UINT64 i=0;i<queue_->GetNumStoredMessagesAllowedByRetrievalFilter();++i){SIZE_T size=0;queue_->GetMessage(i,nullptr,&size);std::vector<unsigned char> bytes(size);auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());Need(SUCCEEDED(queue_->GetMessage(i,message,&size)),"UI GPU message read failed");Need(message->Severity!=D3D12_MESSAGE_SEVERITY_ERROR && message->Severity!=D3D12_MESSAGE_SEVERITY_CORRUPTION,message->pDescription);}
    }
public:
    explicit PlayerSpriteChecks(std::filesystem::path root,bool scripted=false):root_(std::move(root)),scripted_(scripted){}
    explicit PlayerSpriteChecks(std::filesystem::path manifest,bool scripted,bool existing):manifest_(std::move(manifest)),scripted_(scripted){Need(existing && scripted,"Existing UI fixture must use scripted mode");}
    void Prepare() {
        Need(std::filesystem::create_directory(root_),"Refuse existing UI fixture");std::filesystem::create_directory(root_/"Assets");
        std::filesystem::copy_file(Engine::Base::ResolveEngineResource("Resources/textures/common/white.png"),root_/L"Assets/白.png");
        Write(root_/"Game.kubo",R"({"name":"Canvas Sprite pixels","engineVersion":"0.1.0","startupScene":"entry.scene","assetDirectory":"Assets","gameOutput":{"width":320,"height":180}})");
        Engine::Scene::EntityWorld world;const auto canvas=world.CreateEntity("Canvas"),parent=world.CreateEntity("Parent",canvas),base=world.CreateEntity("Red",parent),overlay=world.CreateEntity("Blue",parent);
        Need(canvas==1 && parent==2 && base==3 && overlay==4,"UI fixture IDs");
        Engine::Scene::CanvasComponent settings;settings.referenceSize={640,360};if(!scripted_)Need(world.SetCanvas(canvas,settings),"UI Canvas setup");
        world.Find(parent)->transform.position={320,180,0};
        base_.assetPath="Assets/白.png";base_.size={128,80};base_.color={1,0,0,1};
        overlay_=base_;overlay_.color={0,0,1,1};overlay_.size={60,40};overlay_.sortOrder=1;overlay_.enabled=false;
        if(!scripted_)Need(world.SetSprite(base,base_) && world.SetSprite(overlay,overlay_),"UI Sprite setup");
        else {
            Write(root_/L"Assets/白.png.kubo-meta.json",R"({"version":1,"guid":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})");
            Write(root_/"Assets/ui.lua",R"(local n=0
function on_create(e)
 assert(scene.set_canvas(1,{referenceSize={640,360}}))
 assert(scene.set_sprite(e:id(),{asset='Assets/白.png',size={128,80}}))
 assert(scene.sprite(e:id()).guid=='aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa')
end
function on_update(e,dt)
 n=n+1;local colors={{1,0,0,1},{0,1,0,1},{0,0,1,1}};local s=scene.sprite(e:id())
 s.color=colors[(n-1)%3+1];s.sortOrder=n;s.flipX=n%2==0;assert(scene.set_sprite(e:id(),s))
 local c=scene.canvas(1);c.sortOrder=n;assert(scene.set_canvas(1,c))
end
function on_fixed_update(e,dt) local s=scene.sprite(e:id());s.flipY=true;assert(scene.set_sprite(e:id(),s)) end
function on_destroy(e) assert(scene.remove_sprite(e:id()));assert(scene.remove_canvas(1)) end
)");
            Write(root_/"Assets/fault.lua","function on_update(e,dt) assert(scene.set_canvas(1,{enabled=false}));assert(scene.set_sprite(e:id(),{}));error('UI rollback') end\n");
            Write(root_/"Assets/healthy.lua","function on_update(e,dt) e:translate(0,0,1) end\n");
            Need(world.SetScript(base,"Assets/ui.lua") && world.SetScript(overlay,"Assets/fault.lua") && world.SetScript(parent,"Assets/healthy.lua"),"UI Script setup");
        }
        std::string error;Need(world.Save(root_/"entry.scene",error),error.c_str());
    }
    void Initialize() override {
        if(manifest_.empty()){Prepare();manifest_=root_/"Game.kubo";}
        Engine::Base::ProjectContext project;std::string error;Need(project.Open(manifest_,error),error.c_str());root_=project.GetRootPath();
        const auto& settings=project.GetSettings();
        Need(project.ResolvePath(settings.startupScene)==root_/"entry.scene" && project.ResolvePath(settings.assetDirectory)==root_/"Assets" && settings.gameOutput.width==320 && settings.gameOutput.height==180,"Unexpected UI fixture project settings");
        original_={Read(manifest_),Read(root_/"entry.scene"),Read(root_/L"Assets/白.png")};
        if(scripted_)for(const auto& relative:std::array<std::filesystem::path,4>{L"Assets/ui.lua",L"Assets/fault.lua",L"Assets/healthy.lua",L"Assets/白.png.kubo-meta.json"})scriptBytes_.emplace_back(relative,Read(root_/relative));
        Microsoft::WRL::ComPtr<ID3D12Debug1> validation;Need(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&validation))),"UI debug interface missing");validation->EnableDebugLayer();validation->SetEnableGPUBasedValidation(TRUE);
        SetProjectRoot(root_);SetProjectFile(project.GetProjectFile());SetProjectAssetRoot(project.ResolvePath(settings.assetDirectory));SetGameOutputExtent(settings.gameOutput);Need(ConfigureStartupScene(project.ResolvePath(settings.startupScene),error),error.c_str());
        if(scripted_)std::cout<<"PLAYER_LUA_UI_STAGE=runtime-initialize\n";
        RuntimeGame::Initialize();Need(SUCCEEDED(dxCommon->GetDevice()->QueryInterface(IID_PPV_ARGS(&queue_))),"UI GPU queue missing");
        if(scripted_)std::cout<<"PLAYER_LUA_UI_STAGE=runtime-ready\n";
    }
    void Update() override {
        if(scripted_){
            if(!updates_)std::cout<<"PLAYER_LUA_UI_STAGE=first-update\n";
            RuntimeGame::Update();++updates_;
            fixed_|=GetWorld().Find(3)->sprite.flipY;
            Need(GetLuaError(2).empty() && GetLuaError(3).empty() && !GetLuaError(4).empty(),"Actual Player UI Lua isolation failed");
            Need(GetWorld().Find(2)->transform.position.z==float(updates_) && !GetWorld().Find(4)->hasSprite && GetWorld().Find(1)->canvas.enabled,"Failed UI hook leaked or blocked healthy peer");return;
        }
        // Test-only native authored edits. Production Lua/MCP UI APIs are not
        // implemented by this fixture or exposed through RuntimeGame.
        auto& world=const_cast<Engine::Scene::EntityWorld&>(GetWorld());
        if(draws_==1)world.Find(4)->sprite.enabled=true;
        if(draws_==2)world.Find(4)->sprite.sortOrder=-1;
        if(draws_==3)world.Find(1)->canvas.enabled=false;
        if(draws_==4){world.Find(1)->canvas.enabled=true;world.Find(4)->sprite.enabled=false;world.Find(3)->sprite.color={0,1,0,1};}
        if(draws_==5)world.Find(2)->transform.position.x=160;
        if(draws_==6){world.Find(1)->canvas.scaleMode=Engine::Scene::CanvasScaleMode::ConstantPixels;world.Find(2)->transform.position={160,90,0};}
        if(draws_==7){world.Find(2)->transform.rotation.z=0.4f;world.Find(2)->transform.scale={2,0.5f,1};world.Find(3)->transform.rotation.z=-0.7f;}
        if(draws_==8){world.Find(3)->sprite.assetPath.clear();world.Find(3)->sprite.assetGuid.clear();}
        if(draws_==9){world.Find(3)->sprite.assetPath=base_.assetPath;world.Find(3)->sprite.assetGuid=std::string(16,'f');}
        if(draws_==10){Need(world.SetSprite(3,base_),"UI restore reference");world.Find(2)->transform.rotation={0,0,0};world.Find(2)->transform.scale={1,1,1};world.Find(3)->transform.rotation={0,0,0};}
        if(draws_==11)world.Find(3)->sprite.size.x=-1; // Fail whole frame, never draw the old frame.
        if(draws_==12){world.SetSprite(3,base_);world.Find(2)->active=false;}
        if(draws_==13){world.Find(2)->active=true;world.RemoveSprite(3);}
        if(draws_==14){world.SetSprite(3,base_);world.Find(3)->sprite.color={0,1,0,1};}
        if(draws_==15){world.Find(2)->transform.position.x=0;world.Find(3)->sprite.anchor={0.25f,0};}
        RuntimeGame::Update();
    }
    void Draw() override {
        if(scripted_ && !draws_)std::cout<<"PLAYER_LUA_UI_STAGE=first-draw\n";
        indices_|=1u<<dxCommon->GetCurrentFrameIndex();RuntimeGame::Draw();const auto pixels=Pixels();const auto& center=pixels[0];
        const auto* scene=dynamic_cast<Engine::Scene::EntityScene*>(Engine::Scene::SceneManager::GetInstance()->GetCurrentScene());Need(scene,"UI scene missing");
        if(scripted_){
            Need(scene->GetSpriteResult()==Engine::Scene::RenderSpriteResult::Success && scene->GetSpriteFrame().items.size()==1 && GetWorld().Find(3)->sprite.assetGuid=="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","Actual Lua UI frame/reference missing");
            const auto channel=size_t(draws_%3);Need(center[channel]>240 && center[(channel+1)%3]<5 && center[(channel+2)%3]<5,"Actual Lua UI color pixel incorrect or rollback leaked");
            fixed_|=GetWorld().Find(3)->sprite.flipY;Need(GetWorld().Find(3)->sprite.sortOrder==draws_+1 && GetWorld().Find(1)->canvas.sortOrder==draws_+1,"Lua UI getter/full replacement lost order");
            std::cout<<"PLAYER_LUA_UI_PIXEL stage="<<draws_<<" frame="<<dxCommon->GetCurrentFrameIndex()<<" rgb="<<int(center[0])<<','<<int(center[1])<<','<<int(center[2])<<'\n';++draws_;return;
        }
        if(draws_==11)Need(scene->GetSpriteResult()==Engine::Scene::RenderSpriteResult::InvalidComponent,"Invalid UI frame accepted");
        else Need(scene->GetSpriteResult()==Engine::Scene::RenderSpriteResult::Success,"Valid UI frame refused");
        const auto channel=[&](const auto& p,size_t c){return p[c]>240 && p[(c+1)%3]<5 && p[(c+2)%3]<5;};
        if(draws_==0 || draws_==2 || draws_==10)Need(channel(center,0),"UI expected red center");
        if(draws_==1)Need(channel(center,2),"UI painter order expected blue");
        if(draws_==4 || draws_==6 || draws_==7 || draws_==14)Need(channel(center,1),"UI expected green center");
        if(draws_==0){baseline_=pixels[2];Need(!channel(baseline_,0),"UI output scale unexpectedly covered outside pixel");}
        if(draws_==3 || draws_==5 || draws_==8 || draws_==9 || draws_==11 || draws_==12 || draws_==13)Need(center==baseline_,"Hidden/unassigned/bad GUID/invalid/removed UI left stale pixels");
        if(draws_==5)Need(channel(pixels[1],1),"Parent translation lost");
        if(draws_==6)Need(channel(pixels[2],1) && pixels[1]==baseline_,"ConstantPixels output sizing incorrect");
        if(draws_==15)Need(channel(pixels[1],1) && center==baseline_,"Sprite anchor missed output pixels");
        std::cout<<"PLAYER_UI_PIXEL stage="<<draws_<<" frame="<<dxCommon->GetCurrentFrameIndex()<<" rgb="<<int(center[0])<<','<<int(center[1])<<','<<int(center[2])<<'\n';++draws_;
    }
    bool IsEndRequest() const override{return draws_>=(scripted_?12:16) || RuntimeGame::IsEndRequest();}
    void Finalize() override{RuntimeGame::Finalize();retired_=true;if(queue_)CleanGpu();}
    void Verify(){
        Need(retired_ && draws_==(scripted_?12:16) && (indices_&(indices_-1)),"UI fixture lacked frames/multiple indices/shutdown");CleanGpu();
        Need(original_[0]==Read(manifest_) && original_[1]==Read(root_/"entry.scene") && original_[2]==Read(root_/L"Assets/白.png"),"UI rendering modified authored sources");
        if(scripted_){
            Need(fixed_ && !GetWorld().Find(3)->hasSprite && !GetWorld().Find(1)->hasCanvas && !GetWorld().Find(3)->script.started,"Actual UI Fixed/Destroy/retirement missing");
            for(const auto& [relative,bytes]:scriptBytes_)Need(Read(root_/relative)==bytes,"UI Lua modified source/identity files");
            std::cout<<"PASS: actual Player Lua Canvas/Sprite Create/Update/Fixed/Destroy, Unicode GUID Texture + RGB pixels12/multiple frame indices, getter/full replacement/order, failed-hook rollback/healthy peer, source/identity preservation and GPU errors0/shutdown (not MCP/Inspector)\n";return;
        }
        std::cout<<"PASS: actual Player Canvas/Sprite Unicode texture pixels, output/reference scale and constant pixels, parent shear/mirror-ready affine path, sort/disable/unassigned/GUID refusal/remove/recover, no stale/prefix draw, multiple frame indices, source preservation/GPU errors0/shutdown (not Inspector/Lua/UI input)\n";
    }
};
}
void PreparePlayerLuaUIProject(const std::filesystem::path& root){PlayerSpriteChecks(std::filesystem::absolute(root).lexically_normal(),true).Prepare();}
void RunPlayerLuaUIProject(const std::filesystem::path& manifest){
    auto checks=std::make_unique<PlayerSpriteChecks>(std::filesystem::absolute(manifest).lexically_normal(),true,true);
    try{checks->Run();checks->Verify();}catch(...){auto failure=std::current_exception();try{checks->Finalize();}catch(const std::exception& error){std::cerr<<"UI cleanup failure: "<<error.what()<<'\n';}std::rethrow_exception(failure);}
}
static void RunPlayerSpriteCase(bool scripted){
    const auto root=std::filesystem::temp_directory_path()/(L"kubo-player-ui-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    auto checks=std::make_unique<PlayerSpriteChecks>(root,scripted);
    try{checks->Run();checks->Verify();}catch(...){auto failure=std::current_exception();try{checks->Finalize();}catch(const std::exception& error){std::cerr<<"UI cleanup failure: "<<error.what()<<'\n';}std::cerr<<"PLAYER_UI_FIXTURE_RETAINED="<<root<<'\n';std::rethrow_exception(failure);}
    checks.reset();std::filesystem::remove_all(root);
}
void RunPlayerSpriteChecks(){RunPlayerSpriteCase(false);}
void RunPlayerLuaUIChecks(){RunPlayerSpriteCase(true);}
