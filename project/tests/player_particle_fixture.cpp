#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "player_particle_fixture.h"
#include "engine/app/RuntimeGame.h"
#include "engine/base/EngineResourcePath.h"
#include "engine/base/AssetPath.h"
#include "engine/base/ProjectContext.h"
#include "engine/scene/entity/EntityScene.h"
#include "engine/scene/prefab/PrefabDocument.h"
#include "SceneManager.h"
#include "DirectXCommon.h"
#include "OffscreenRenderManager.h"
#include <d3d12sdklayers.h>
#include <array>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <exception>
namespace {
using namespace Engine;namespace fs=std::filesystem;using Pixel=std::array<unsigned char,4>;
void Need(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
std::string Read(const fs::path& file){std::ifstream in(file,std::ios::binary);Need(in.good(),"Player Particle authored file missing");return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
void Write(const fs::path& file,const std::string& bytes){std::ofstream out(file,std::ios::binary);out<<bytes;out.close();Need(!out.fail(),"Player Particle fixture write");}
class Checks final:public App::RuntimeGame {
    bool deployed_=false,passed_=false,retired_=false;fs::path root_;unsigned draws_=0,indices_=0,steps_=0;std::string authored_;std::vector<std::pair<fs::path,std::string>> original_;Microsoft::WRL::ComPtr<ID3D12InfoQueue> messages_;
    std::array<Pixel,2> Pixels(){
        auto* color=offscreenRenderManager->GetViewRenderTarget(false);const auto desc=color->GetDesc();Need(desc.Width==320&&desc.Height==180,"Player Particle output extent");D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rows=0;UINT64 rowBytes=0,total=0;dxCommon->GetDevice()->GetCopyableFootprints(&desc,0,1,0,&footprint,&rows,&rowBytes,&total);
        D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=total;buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;Microsoft::WRL::ComPtr<ID3D12Resource> readback;Need(SUCCEEDED(dxCommon->GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))),"Player Particle readback allocation");
        D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=color;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprint;dxCommon->TransitionResource(color,D3D12_RESOURCE_STATE_COPY_SOURCE);dxCommon->GetCommandList()->CopyTextureRegion(&to,0,0,0,&from,nullptr);dxCommon->TransitionResource(color,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);dxCommon->CommandKick();
        void* data=nullptr;D3D12_RANGE range{0,size_t(total)};Need(SUCCEEDED(readback->Map(0,&range,&data)),"Player Particle readback map");std::array<Pixel,2> result{};for(unsigned i=0;i<2;++i){const unsigned x=i?8:160,y=i?8:90;const auto* pixel=static_cast<const unsigned char*>(data)+footprint.Offset+y*footprint.Footprint.RowPitch+x*4;std::copy_n(pixel,4,result[i].begin());}D3D12_RANGE written{0,0};readback->Unmap(0,&written);return result;
    }
    void GpuClean(){for(UINT64 i=0;i<messages_->GetNumStoredMessagesAllowedByRetrievalFilter();++i){SIZE_T size=0;messages_->GetMessage(i,nullptr,&size);std::vector<unsigned char> bytes(size);auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());Need(SUCCEEDED(messages_->GetMessage(i,message,&size)),"Player Particle GPU message read");Need(message->Severity!=D3D12_MESSAGE_SEVERITY_ERROR&&message->Severity!=D3D12_MESSAGE_SEVERITY_CORRUPTION,message->pDescription);}}
    void Prepare(){
        root_=fs::temp_directory_path()/(L"kubo-player-particles-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));Need(fs::create_directory(root_),"Refuse existing Player Particle fixture");fs::create_directory(root_/"Assets");fs::copy_file(Base::ResolveEngineResource("Resources/textures/common/white.png"),root_/L"Assets/白.png");
        Write(root_/"Game.kubo",R"({"name":"Authored Particle Player","startupScene":"entry.scene","assetDirectory":"Assets","gameOutput":{"width":320,"height":180}})");
        Assets::AssetRegistry assets;std::string error;Need(assets.Scan(root_/"Assets",error),error);Scene::EntityWorld world;const auto id=world.CreateEntity("Particle");Scene::ParticleComponent value;value.assetPath="Assets/白.png";value.assetGuid=assets.FindByPath(root_/L"Assets/白.png")->guid;value.burst=1;value.rate=0;value.lifetime=60;value.size=1;value.velocity=value.spread={0,0,0};value.startColor=value.endColor={1,0,0,1};Need(world.SetParticle(id,value),"Player Particle fixture settings");const auto camera=world.CreateEntity("Camera");world.Find(camera)->transform.position={0,0,-5};Scene::CameraComponent projection;projection.orthographic=true;projection.orthographicHeight=4;Need(world.SetCamera(camera,projection)&&world.Save(root_/"entry.scene",error),error);Prefab::PrefabValue prefab;Need(Prefab::CapturePrefabSubtree(world,id,prefab,error),error);Write(root_/"Assets/emitter.prefab",prefab.dump());Need(assets.Scan(root_/"Assets",error),error);
    }
public:
    explicit Checks(bool deployed):deployed_(deployed){}
    ~Checks(){if(!deployed_&&!root_.empty()){if(passed_){std::error_code error;fs::remove_all(root_,error);}else std::cerr<<"PLAYER_PARTICLE_RETAINED="<<root_<<'\n';}}
    void Initialize() override {
        if(deployed_)root_=Base::ResolveEngineResource("Game.kubo").parent_path();else Prepare();Base::ProjectContext project;std::string error;Need(project.Open(root_/"Game.kubo",error),error);const auto& settings=project.GetSettings();Need(settings.startupScene=="entry.scene"&&settings.assetDirectory=="Assets"&&settings.gameOutput.width==320&&settings.gameOutput.height==180,"Player Particle fixture manifest");
        for(const auto& relative:std::array<fs::path,6>{L"Game.kubo",L"entry.scene",L"Assets/白.png",L"Assets/白.png.kubo-meta.json",L"Assets/emitter.prefab",L"Assets/emitter.prefab.kubo-meta.json"})original_.emplace_back(relative,Read(root_/relative));
        Microsoft::WRL::ComPtr<ID3D12Debug1> debug;Need(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))),"Player Particle GPU validation unavailable");debug->EnableDebugLayer();debug->SetEnableGPUBasedValidation(TRUE);SetProjectRoot(root_);SetProjectFile(project.GetProjectFile());SetProjectAssetRoot(root_/"Assets");SetGameOutputExtent(settings.gameOutput);Need(ConfigureStartupScene(root_/"entry.scene",error),error);RuntimeGame::Initialize();Need(SUCCEEDED(dxCommon->GetDevice()->QueryInterface(IID_PPV_ARGS(&messages_))),"Player Particle GPU queue");authored_=GetWorld().Capture().dump();Need(GetWorld().Find(1)&&GetWorld().Find(1)->hasParticle,"Player did not load authored Particle World2");
    }
    void Update() override {if(draws_>=8)return;RuntimeGame::Update();const auto* scene=dynamic_cast<Scene::EntityScene*>(Scene::SceneManager::GetInstance()->GetCurrentScene());Need(scene,"Player missing EntityScene");const auto state=scene->GetParticleRenderStatus();steps_+=unsigned(state.steps);Need(state.result==Scene::RenderParticleResult::Success&&state.emitters==1&&state.particles<=1,"Player Particle host synchronization");Need(GetWorld().Capture().dump()==authored_,"Player Particle simulation changed authored data");}
    void Draw() override {RuntimeGame::Draw();indices_|=1u<<dxCommon->GetCurrentFrameIndex();const auto pixels=Pixels();Need(pixels[0][0]==255&&pixels[0][1]==pixels[1][1]&&pixels[0][2]==pixels[1][2]&&pixels[1][0]<200,"Player Particle startup center/clear pixels");GpuClean();std::cout<<"PLAYER_PARTICLE_PIXEL deployed="<<deployed_<<" frame="<<draws_<<" rgb="<<int(pixels[0][0])<<','<<int(pixels[0][1])<<','<<int(pixels[0][2])<<'\n';++draws_;}
    bool IsEndRequest() const override {return draws_>=8||RuntimeGame::IsEndRequest();}
    void Finalize() override {if(retired_)return;RuntimeGame::Finalize();retired_=true;if(messages_)GpuClean();}
    void Verify(){Need(retired_&&draws_==8&&steps_>0&&(indices_&(indices_-1)),"Player Particle lifecycle incomplete");for(const auto& [path,bytes]:original_)Need(Read(root_/path)==bytes,"Player Particle changed authored project/asset/metadata/Prefab");passed_=true;}
};
}
void RunPlayerParticleChecks(bool deployed){auto checks=std::make_unique<Checks>(deployed);try{checks->Run();checks->Verify();}catch(...){const auto error=std::current_exception();try{checks->Finalize();}catch(const std::exception& cleanup){std::cerr<<"Player Particle cleanup: "<<cleanup.what()<<'\n';}std::rethrow_exception(error);}std::cout<<"PASS: actual RuntimeGame Particle World2 startup+Texture GUID, positive host clock, independent Player pixels8, authored Scene/Prefab/image/identity preservation and GPU errors0/shutdown deployed="<<deployed<<'\n';}
