#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "player_lighting_fixture.h"
#include "engine/app/RuntimeGame.h"
#include "engine/base/ProjectContext.h"
#include "engine/assets/MaterialAsset.h"
#include "engine/scene/entity/EntityScene.h"
#include "SceneManager.h"
#include "DirectXCommon.h"
#include "OffscreenRenderManager.h"
#include <d3d12sdklayers.h>
#include <Windows.h>
#include <array>
#include <algorithm>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream out(path,std::ios::binary);out<<bytes;out.close();Need(!out.fail(),"Player lighting fixture write failed");}
std::string Read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);Need(in.good(),"Player lighting source read failed");return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
class PlayerLightingChecks final : public Engine::App::RuntimeGame {
    std::filesystem::path root_,manifest_;std::array<std::string,9> sourceBytes_;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> messages_;
    int updates_=0,draws_=0;unsigned kinds_=0,frameIndices_=0;bool fixed_=false,retired_=false,passed_=false;
        const std::array<const char*,9> sources_{"entry.scene","Assets/lighting.lua","Assets/fault.lua","Assets/healthy.lua",
        "Assets/plane.obj","Assets/surface.mtl","Assets/shared.material","Assets/shared.material.kubo-meta.json","Game.kubo"};
    std::array<unsigned char,4> ReadPixel(){
        auto* color=offscreenRenderManager->GetViewRenderTarget(false);const auto desc=color->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rows=0;UINT64 rowBytes=0,total=0;
        dxCommon->GetDevice()->GetCopyableFootprints(&desc,0,1,0,&footprint,&rows,&rowBytes,&total);
        D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=total;buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        Need(SUCCEEDED(dxCommon->GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))),"Player light readback allocation failed");
        D3D12_TEXTURE_COPY_LOCATION source{};source.pResource=color;source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION destination{};destination.pResource=readback.Get();destination.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;destination.PlacedFootprint=footprint;
        dxCommon->TransitionResource(color,D3D12_RESOURCE_STATE_COPY_SOURCE);dxCommon->GetCommandList()->CopyTextureRegion(&destination,0,0,0,&source,nullptr);dxCommon->TransitionResource(color,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);dxCommon->CommandKick();
        void* mapped=nullptr;D3D12_RANGE range{0,size_t(total)};Need(SUCCEEDED(readback->Map(0,&range,&mapped)),"Player light map failed");
        const auto* pixel=static_cast<const unsigned char*>(mapped)+footprint.Offset+size_t(desc.Height/2)*footprint.Footprint.RowPitch+size_t(desc.Width/2)*4;
        std::array<unsigned char,4> result{};std::copy_n(pixel,4,result.begin());D3D12_RANGE written{0,0};readback->Unmap(0,&written);return result;
    }
    void GpuClean(){for(UINT64 i=0;i<messages_->GetNumStoredMessagesAllowedByRetrievalFilter();++i){SIZE_T size=0;messages_->GetMessage(i,nullptr,&size);std::vector<unsigned char> bytes(size);auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());Need(SUCCEEDED(messages_->GetMessage(i,message,&size)),"Player GPU message read failed");Need(message->Severity!=D3D12_MESSAGE_SEVERITY_ERROR && message->Severity!=D3D12_MESSAGE_SEVERITY_CORRUPTION,"Player lighting GPU Error/Corruption");}}
public:
    explicit PlayerLightingChecks(std::filesystem::path manifest):manifest_(std::move(manifest)){}
    ~PlayerLightingChecks(){if(!passed_ && !root_.empty())std::cerr<<"PLAYER_LIGHT_FIXTURE_RETAINED="<<root_<<'\n';}
    static void Prepare(const std::filesystem::path& root_) {
        Need(std::filesystem::create_directory(root_),"Refuse existing Player light fixture");std::filesystem::create_directory(root_/"Assets");
        Write(root_/"Game.kubo",R"({"name":"Player lights","engineVersion":"0.1.0","startupScene":"entry.scene","assetDirectory":"Assets","gameOutput":{"width":320,"height":320}})");
        Write(root_/"Assets/surface.mtl","newmtl Surface\nKd 1 1 1\nd 1\n");
        Write(root_/"Assets/plane.obj","mtllib surface.mtl\nusemtl Surface\nv -1 -1 0\nv -1 1 0\nv 1 -1 0\nv 1 1 0\nvn 0 0 -1\nf 1//1 2//1 3//1\nf 3//1 2//1 4//1\n");
        Write(root_/"Assets/lighting.lua",R"(local n=0
function on_create(e) assert(scene.material(e:id()).lighting == false) end
function on_update(e,dt)
 n=n+1
 if n==1 then return end
 if n==2 then assert(scene.set_material(e:id(),{lighting=true,textureInfluence=0})) end
 local k=math.floor((n-2)/2)%3;local colors={{1,0,0},{0,1,0},{0,0,1}};local types={'directional','point','spot'}
 assert(scene.set_light(3,{type=types[k+1],color=colors[k+1],intensity=0.7}))
end
function on_fixed_update(e,dt) if n<2 then return end;local m=scene.material(e:id());m.roughness=0.5;assert(scene.set_material(e:id(),m)) end
function on_destroy(e) assert(scene.remove_material(e:id()));assert(scene.set_light(3,{enabled=false})) end
)");
        Write(root_/"Assets/fault.lua","function on_update(e,dt) assert(scene.set_light(3,{color={1,1,1},intensity=100}));assert(scene.set_material(e:id(),{lighting=true}));error('render rollback') end\n");
        Write(root_/"Assets/healthy.lua","function on_update(e,dt) e:translate(1,0,0) end\n");
        Engine::Scene::EntityWorld world;const auto model=world.CreateEntity("Surface"),camera=world.CreateEntity("Camera"),light=world.CreateEntity("Light"),healthy=world.CreateEntity("Healthy"),fault=world.CreateEntity("Fault");
        Need(model==1 && camera==2 && light==3 && healthy==4 && fault==5,"Player lighting IDs changed");
        Need(world.SetModel(model,"Assets/plane.obj") && world.SetScript(model,"Assets/lighting.lua") && world.SetScript(healthy,"Assets/healthy.lua") && world.SetScript(fault,"Assets/fault.lua"),"Player light components failed");
        Engine::Scene::CameraComponent projection;projection.orthographic=true;projection.orthographicHeight=3;Need(world.SetCamera(camera,projection),"Player light camera failed");world.Find(camera)->transform.position={0,0,-3};world.Find(light)->transform.position={0,0,-2};Need(world.SetLight(light,{}),"Startup authored Light failed");
        std::string error;
        Engine::Assets::AssetRegistry assets;
        Need(assets.Scan(root_/"Assets",error,Engine::Assets::AssetRegistry::MetadataPolicy::ReadOnly),error.c_str());
        Engine::Scene::MaterialComponent shared,local;
        shared.color={1,0,1,1};shared.textureInfluence=0;
        local.color={1,0,0,1};local.textureInfluence=0;
        const auto source=root_/"Assets/shared.material";
        Need(Engine::Assets::WriteMaterialAsset(source,shared,error,false) &&
            assets.RegisterSource(source,local.assetGuid,error) && world.SetMaterial(model,local),error.c_str());
        Need(world.SetEnvironment(Engine::Scene::SceneEnvironment::Night()),"Player Night profile refused");
        Need(world.Save(root_/"entry.scene",error),error.c_str());
    }
    void Initialize() override {
        Engine::Base::ProjectContext project;std::string error;Need(project.Open(manifest_,error),error.c_str());root_=project.GetRootPath();
        const auto& settings=project.GetSettings();Need(project.GetProjectFile().filename()==L"Game.kubo" && settings.startupScene=="entry.scene" && settings.assetDirectory=="Assets" && settings.gameOutput.width==320 && settings.gameOutput.height==320,"Player lighting project/export settings mismatch");
        for(size_t i=0;i<sources_.size();++i)sourceBytes_[i]=Read(root_/sources_[i]);
        // Release Runtime intentionally does not enable validation itself. The
        // test requests the same diagnostics before either build creates a device.
        Microsoft::WRL::ComPtr<ID3D12Debug1> validation;Need(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&validation))),"Player lighting validation interface missing");validation->EnableDebugLayer();validation->SetEnableGPUBasedValidation(TRUE);
        SetProjectRoot(root_);SetProjectFile(project.GetProjectFile());SetProjectAssetRoot(project.ResolvePath(settings.assetDirectory));SetGameOutputExtent(settings.gameOutput);Need(ConfigureStartupScene(project.ResolvePath(settings.startupScene),error),error.c_str());RuntimeGame::Initialize();Need(SUCCEEDED(dxCommon->GetDevice()->QueryInterface(IID_PPV_ARGS(&messages_))),"Player light GPU queue missing");
    }
    void Update() override {RuntimeGame::Update();++updates_;const auto& world=GetWorld();Need(GetLuaError(1).empty() && GetLuaError(4).empty() && !GetLuaError(5).empty(),"Player render script isolation failed");Need(!world.Find(5)->hasMaterial && world.Find(4)->transform.position.x==float(updates_),"Player failed render hook leaked or blocked healthy peer");fixed_|=world.Find(1)->material.roughness==0.5f;}
    void Draw() override {
        const auto index=dxCommon->GetCurrentFrameIndex();Need(index<32,"Unexpected swapchain frame index");frameIndices_|=1u<<index;RuntimeGame::Draw();
        const auto* scene=dynamic_cast<Engine::Scene::EntityScene*>(Engine::Scene::SceneManager::GetInstance()->GetCurrentScene());Need(scene && scene->GetModelRenderStatus(1).loaded && scene->GetLightingResult()==Engine::Scene::RenderLightResult::Success && scene->GetLightingFrame().gpu.count==1,"Player render scene/light frame missing");
        const auto kind=unsigned(GetWorld().Find(3)->light.kind);Need(kind<3,"Player Lua Light kind invalid");const auto pixel=ReadPixel();
        if(updates_==1){
            Need(pixel[0]>140 && pixel[0]<151 && pixel[1]<5 && pixel[2]>140 && pixel[2]<151,"Actual Player did not render the saved shared Material");
            std::cout<<"PLAYER_NIGHT_ENVIRONMENT shared_material_rgb="<<int(pixel[0])<<','<<int(pixel[1])<<','<<int(pixel[2])<<'\n';
            ++draws_;
            return;
        }
        Need(pixel[kind]>80 && pixel[(kind+1)%3]<5 && pixel[(kind+2)%3]<5,"Actual Player Lua lighting pixel not applied or rollback prefix leaked");kinds_|=1u<<kind;++draws_;
        std::cout<<"PLAYER_LIGHT_PIXEL frame="<<index<<" kind="<<kind<<" rgb="<<int(pixel[0])<<','<<int(pixel[1])<<','<<int(pixel[2])<<'\n';
    }
    bool IsEndRequest() const override{return draws_>=13 || updates_>20 || RuntimeGame::IsEndRequest();}
    void Finalize() override {RuntimeGame::Finalize();retired_=true;if(messages_)GpuClean();}
    void Verify(){Need(retired_ && draws_==13 && kinds_==7 && fixed_ && (frameIndices_&(frameIndices_-1)),"Player light scenario did not cover kinds/fixed/multiple swapchain indices/shutdown");Need(!GetWorld().Find(1)->hasMaterial && !GetWorld().Find(1)->script.started && !GetWorld().Find(3)->light.enabled,"Player render Destroy did not publish/retire");for(size_t i=0;i<sources_.size();++i)Need(Read(root_/sources_[i])==sourceBytes_[i],"Player lighting modified authored source");GpuClean();passed_=true;std::cout<<"PASS: actual Player Lua Light/Material Create/Update/Fixed/Destroy, 3-kind pixels across multiple swapchain indices, failed-hook rollback/healthy peer, source preservation, GPU errors0 and owned shutdown\n";}
};
}
void PreparePlayerLightingProject(const std::filesystem::path& root){PlayerLightingChecks::Prepare(std::filesystem::absolute(root).lexically_normal());}
void RunPlayerLightingProject(const std::filesystem::path& manifest){auto checks=std::make_unique<PlayerLightingChecks>(std::filesystem::absolute(manifest).lexically_normal());try{checks->Run();checks->Verify();}catch(...){auto failure=std::current_exception();try{checks->Finalize();}catch(const std::exception& error){std::cerr<<"Player lighting cleanup failure: "<<error.what()<<'\n';}std::rethrow_exception(failure);}}
void RunPlayerLightingChecks(){
    const auto root=std::filesystem::temp_directory_path()/(L"kubo-player-lights-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try{PreparePlayerLightingProject(root);RunPlayerLightingProject(root/"Game.kubo");}catch(...){std::cerr<<"PLAYER_LIGHT_FIXTURE_RETAINED="<<root<<'\n';throw;}
    std::error_code error;std::filesystem::remove_all(root,error);
}
