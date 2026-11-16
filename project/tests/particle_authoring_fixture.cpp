#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "particle_authoring_fixture.h"
#include "Framework.h"
#include "DirectXCommon.h"
#include "OffscreenRenderManager.h"
#include "SrvManager.h"
#include "Camera.h"
#include "engine/scene/entity/EntityScene.h"
#include "engine/scene/prefab/RuntimePrefab.h"
#include "engine/particle/core/ParticleManager.h"
#include "externals/DirectXTex/DirectXTex.h"
#include <d3d12sdklayers.h>
#include <array>
#include <fstream>
#include <iostream>
#include <exception>
namespace {
using namespace Engine;namespace fs=std::filesystem;
void Need(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
struct Image {unsigned width=0,height=0,count=0,green=0;double x=0,y=0;std::vector<unsigned char> bytes;};
class Checks final:public Base::Framework {
    fs::path root_;Scene::EntityWorld world_;Assets::AssetRegistry assets_;std::unique_ptr<Scene::EntityScene> scene_;CameraSystem::Camera editor_;
    Scene::EntityId parent_=0,emitter_=0;Scene::ParticleComponent settings_;Microsoft::WRL::ComPtr<ID3D12InfoQueue> messages_;
    unsigned stage_=0,indices_=0;bool finalized_=false,passed_=false;std::string before_,saved_;std::array<Image,2> initial_,paused_,seeded_;std::array<double,2> movingX_{};
    Image Capture(bool editor){
        auto* color=offscreenRenderManager->GetViewRenderTarget(editor);const auto desc=color->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rows=0;UINT64 rowBytes=0,total=0;dxCommon->GetDevice()->GetCopyableFootprints(&desc,0,1,0,&footprint,&rows,&rowBytes,&total);
        D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=total;buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;Microsoft::WRL::ComPtr<ID3D12Resource> readback;Need(SUCCEEDED(dxCommon->GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))),"Authored Particle readback allocation");
        D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=color;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprint;
        dxCommon->TransitionResource(color,D3D12_RESOURCE_STATE_COPY_SOURCE);dxCommon->GetCommandList()->CopyTextureRegion(&to,0,0,0,&from,nullptr);dxCommon->TransitionResource(color,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);dxCommon->CommandKick();
        void* data=nullptr;D3D12_RANGE range{0,size_t(total)};Need(SUCCEEDED(readback->Map(0,&range,&data)),"Authored Particle readback map");Image result;result.width=unsigned(desc.Width);result.height=desc.Height;result.bytes.resize(size_t(result.width)*result.height*4);
        // The existing Particle pipeline is additive: untouched background G/B
        // remain in red particles. Compare against the actual clear corner.
        const auto* clear=static_cast<const unsigned char*>(data)+footprint.Offset;
        for(unsigned y=0;y<result.height;++y){const auto* row=static_cast<const unsigned char*>(data)+footprint.Offset+y*footprint.Footprint.RowPitch;std::copy_n(row,size_t(result.width)*4,result.bytes.data()+size_t(y)*result.width*4);for(unsigned x=0;x<result.width;++x){
            if(row[x*4]>unsigned(clear[0])+32&&row[x*4+1]==clear[1]&&row[x*4+2]==clear[2]){++result.count;result.x+=x;result.y+=y;}
            if(row[x*4]==clear[0]&&row[x*4+1]>unsigned(clear[1])+20&&row[x*4+2]==clear[2])++result.green;
        }}
        D3D12_RANGE written{0,0};readback->Unmap(0,&written);if(result.count){result.x/=result.count;result.y/=result.count;}return result;
    }
    void GpuClean(){for(UINT64 i=0;i<messages_->GetNumStoredMessagesAllowedByRetrievalFilter();++i){SIZE_T size=0;messages_->GetMessage(i,nullptr,&size);std::vector<unsigned char> bytes(size);auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());Need(SUCCEEDED(messages_->GetMessage(i,message,&size)),"Authored Particle GPU message read");Need(message->Severity!=D3D12_MESSAGE_SEVERITY_ERROR&&message->Severity!=D3D12_MESSAGE_SEVERITY_CORRUPTION,message->pDescription);}}
    void Apply(){Need(world_.SetParticle(emitter_,settings_),"Authored Particle settings rejected");}
public:
    ~Checks(){if(!root_.empty()){if(passed_){std::error_code error;fs::remove_all(root_,error);}else std::cerr<<"PARTICLE_AUTHORING_RETAINED="<<root_<<'\n';}}
    void Initialize() override {
        std::cout<<"PARTICLE_AUTHORING_STAGE=initialize\n";
        root_=fs::temp_directory_path()/(L"kubo-particle-authoring-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));Need(fs::create_directory(root_),"Refuse existing Particle authoring fixture");fs::create_directory(root_/"Assets");
        DirectX::ScratchImage image;Need(SUCCEEDED(image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM,4,4,1,1)),"Particle white image allocation");std::fill_n(image.GetPixels(),image.GetPixelsSize(),static_cast<unsigned char>(255));Need(SUCCEEDED(DirectX::SaveToDDSFile(image.GetImages(),image.GetImageCount(),image.GetMetadata(),DirectX::DDS_FLAGS_NONE,(root_/L"Assets/白.dds").c_str())),"Particle white DDS save");
        std::string error;Need(assets_.Scan(root_/"Assets",error),error);parent_=world_.CreateEntity("Affine parent");emitter_=world_.CreateEntity("Emitter",parent_);
        world_.Find(parent_)->transform={{-.4f,.1f,0},{0,0,.2f},{-1.5f,.8f,1}};world_.Find(emitter_)->transform={{.2f,0,0},{0,0,.3f},{.8f,1,1}};
        settings_.assetPath="Assets/白.dds";settings_.assetGuid=assets_.FindByPath(root_/L"Assets/白.dds")->guid;settings_.rate=0;settings_.burst=1;settings_.lifetime=2;settings_.size=.5f;settings_.velocity={0,0,0};settings_.spread={0,0,0};settings_.startColor=settings_.endColor={1,0,0,1};Apply();
        const auto camera=world_.CreateEntity("Game camera");world_.Find(camera)->transform.position={0,0,-5};Scene::CameraComponent projection;projection.orthographic=true;projection.orthographicHeight=4;Need(world_.SetCamera(camera,projection),"Particle camera");
        Need(world_.Save(root_/"entry.scene",error),error);Scene::EntityWorld loaded;Need(loaded.Load(root_/"entry.scene",error)&&loaded.Capture().dump()==world_.Capture().dump(),"Particle World2 disk roundtrip");world_=std::move(loaded);saved_=world_.Capture().dump();
        Prefab::PrefabValue prefab;Need(Prefab::CapturePrefabSubtree(world_,emitter_,prefab,error),error);Scene::EntityWorld runtime;Scene::EntityId born=0;Need(Prefab::InstantiateRuntimePrefab(runtime,prefab,0,born,error)&&runtime.Find(born)->particle==settings_,"Runtime Particle Prefab settings");
        std::cout<<"PARTICLE_AUTHORING_STAGE=framework_begin\n";
        Microsoft::WRL::ComPtr<ID3D12Debug1> debug;Need(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))),"Authored Particle GPU validation unavailable");debug->EnableDebugLayer();debug->SetEnableGPUBasedValidation(TRUE);SetProjectRoot(root_);SetGameOutputExtent({320,180});Framework::Initialize();Need(SUCCEEDED(dxCommon->GetDevice()->QueryInterface(IID_PPV_ARGS(&messages_))),"Authored Particle GPU queue");offscreenRenderManager->ResizeEditorView({480,270});
        std::cout<<"PARTICLE_AUTHORING_STAGE=framework_ready\n";
        editor_.SetTranslate({1,0,-5});editor_.SetRotate({0,0,0});editor_.SetOrthographic(true,4);editor_.SetAspectRatio(16.f/9);editor_.Update();scene_=std::make_unique<Scene::EntityScene>(world_,*dxCommon,assets_,root_);scene_->Initialize();scene_->SetEditorCamera(&editor_);scene_->SetSceneAspectRatio(16.f/9);
    }
    void Update() override {
        if(stage_>=26)return;
        std::cout<<"PARTICLE_AUTHORING_UPDATE="<<stage_<<'\n';Framework::Update();
        if(stage_==3){settings_.velocity={4,0,0};Apply();}
        if(stage_==6){settings_.enabled=false;Apply();}
        if(stage_==7){settings_.enabled=true;settings_.velocity={0,0,0};Apply();}
        if(stage_==8){settings_.assetGuid=std::string(32,'f');Apply();}
        if(stage_==9){settings_.assetGuid=assets_.FindByPath(root_/L"Assets/白.dds")->guid;Apply();world_.Find(parent_)->active=false;}
        if(stage_==10)world_.Find(parent_)->active=true;
        if(stage_==11){settings_.lifetime=1.f/60;settings_.burst=1;Apply();}
        if(stage_==13){settings_.rate=60;settings_.burst=0;settings_.lifetime=2;Apply();}
        if(stage_==14){settings_.rate=0;settings_.burst=8;settings_.seed=77;settings_.velocity={0,0,0};settings_.spread={3,2,0};Apply();}
        if(stage_==20){settings_.seed=78;Apply();}
        if(stage_==22){settings_.assetGuid.clear();settings_.assetPath.clear();Apply();}
        if(stage_==23){
            settings_.assetPath="Assets/白.dds";settings_.spread={};settings_.size=.5f;settings_.lifetime=2.f/60;
            settings_.burst=1;settings_.sizeKeys={{.5f,1.f}};settings_.colorKeys={{.5f,{0,1,0,1}}};Apply();
        }
        const bool preview=stage_==23||stage_==24;
        before_=world_.Capture().dump();scene_->SynchronizeModels(0);scene_->UpdateParticles(1.f/60,
            stage_!=2&&stage_!=17,stage_==1||stage_==5||stage_==16,preview?emitter_:0);const auto state=scene_->GetParticleRenderStatus();
        if(stage_==8)Need(state.result==Scene::RenderParticleResult::AssetFailure&&state.emitters==0,"Bad Particle GUID retained stale owner");else Need(state.result==Scene::RenderParticleResult::Success,"Authored Particle frame admission failed");
        const size_t expected=stage_==2||stage_==6||stage_==8||stage_==9||stage_==12||stage_==17||stage_==22?0:stage_>=14&&stage_<22?8:1;
        Need(state.particles==expected,"Particle burst/rate/lifetime/Pause/Stop count");
        const auto count=state.particles;scene_->SynchronizeModels(0);Need(scene_->GetParticleRenderStatus().particles==count&&world_.Capture().dump()==before_,"Zero-delta model synchronization reset Particle/changed World");
    }
    void Draw() override {
        std::cout<<"PARTICLE_AUTHORING_DRAW="<<stage_<<'\n';
        dxCommon->PrepareForFrame();const auto frame=dxCommon->GetCurrentFrameIndex();indices_|=1u<<frame;dxCommon->BeginGpuFrameTiming();
        const auto count=scene_->GetParticleRenderStatus().particles;offscreenRenderManager->Begin();srvManager->PreDraw();scene_->DrawGameView(16.f/9);offscreenRenderManager->End();offscreenRenderManager->BeginEditorView();scene_->DrawEditorView();offscreenRenderManager->EndEditorView();dxCommon->Begin();offscreenRenderManager->Draw();dxCommon->End();
        Need(scene_->GetParticleRenderStatus().particles==count&&world_.Capture().dump()==before_,"Drawing two views advanced Particle/authored World");
        for(unsigned view=0;view<2;++view){auto image=Capture(view!=0);const bool preview=stage_==23||stage_==24;
            const bool red=count!=0 && (!preview || (view && stage_==23));
            Need((image.count>0)==red,"Authored Particle view missing/stale red pixels");
            if(preview)Need((image.green>0)==(view==1 && stage_==24),"Preview is not isolated to Scene or did not render green key");
            if(view==1 && stage_==24)Need(image.green>2*initial_[view].count,"Size curve did not expand particle");
            if(stage_==25)Need(image.bytes==initial_[view].bytes,"Play inherited preview time or seed");
            if(stage_==0){initial_[view]=image;Matrix4x4 model{};Need(world_.GetWorldMatrix(emitter_,model),"Particle model transform");const auto expectedX=(model.m[3][0]-(view?1.f:0.f))*double(image.height)/4+double(image.width)/2-.5;const auto expectedY=double(image.height)/2-model.m[3][1]*double(image.height)/4-.5;Need(std::abs(image.x-expectedX)<2&&std::abs(image.y-expectedY)<2,"Particle parent affine/mirror or independent camera projection wrong");}
            if(stage_==1)Need(image.bytes==initial_[view].bytes,"Paused Particle pixels changed");
            if(stage_==3){Need(image.bytes==initial_[view].bytes,"Changed settings did not restart burst at origin");movingX_[view]=image.x;}
            if(stage_==4){Need(image.x<movingX_[view]-1,"Positive Particle clock did not move through mirrored parent");paused_[view]=image;}
            if(stage_==5)Need(image.bytes==paused_[view].bytes,"Pause advanced Particle position");
            if(stage_==7||stage_==10)Need(image.bytes==initial_[view].bytes,"Re-enable/ancestor activation did not restart deterministically");
            if(stage_==15)seeded_[view]=image;
            if(stage_==16||stage_==19)Need(image.bytes==seeded_[view].bytes,"Seeded Particle Pause/Stop+restart changed deterministic motion");
            if(stage_==21)Need(image.bytes!=seeded_[view].bytes,"Changed Particle seed did not change motion");
            std::cout<<"PARTICLE_AUTHORING_PIXEL stage="<<stage_<<" view="<<view<<" frame="<<frame<<" count="<<count<<" pixels="<<image.count<<" center="<<image.x<<','<<image.y<<'\n';
        }
        Need(scene_->GetParticleRenderStatus().result==(stage_==8?Scene::RenderParticleResult::AssetFailure:Scene::RenderParticleResult::Success),"Authored Particle draw error");GpuClean();++stage_;
    }
    bool IsEndRequest() const override{return stage_>=26||Framework::IsEndRequest();}
    void Finalize() override {if(finalized_)return;scene_.reset();Framework::Finalize();finalized_=true;if(messages_)GpuClean();}
    void Verify(){Need(finalized_&&stage_==26&&(indices_&(indices_-1)),"Authored Particle lifecycle incomplete");Scene::EntityWorld original;std::string error;Need(original.Load(root_/"entry.scene",error)&&original.Capture().dump()==saved_,"Rendering changed authored Scene file");passed_=true;}
};
}
void RunParticleAuthoringChecks(){auto checks=std::make_unique<Checks>();try{checks->Run();checks->Verify();}catch(...){const auto error=std::current_exception();try{checks->Finalize();}catch(const std::exception& cleanup){std::cerr<<"Particle authoring cleanup: "<<cleanup.what()<<'\n';}std::rethrow_exception(error);}std::cout<<"PASS: actual EntityScene Particle World2 disk+runtime Prefab settings, two independent affine/mirrored Game/Scene views46, fixed positive clock/Pause/Stop/settings restart/disabled+ancestor/GUID refusal/burst+rate/lifetime/seed reproducibility, authored data preservation and GPU errors0/shutdown (not exported startup/Lua/MCP)\n";}
