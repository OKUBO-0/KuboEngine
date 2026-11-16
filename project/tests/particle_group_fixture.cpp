#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "particle_group_fixture.h"
#include "Framework.h"
#include "DirectXCommon.h"
#include "OffscreenRenderManager.h"
#include "SrvManager.h"
#include "CameraManager.h"
#include "engine/particle/core/ParticleManager.h"
#include "externals/DirectXTex/DirectXTex.h"
#include <d3d12sdklayers.h>
#include <filesystem>
#include <array>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <limits>
#include <utility>
#include <exception>
namespace {
using namespace Engine;namespace fs=std::filesystem;using Pixel=std::array<unsigned char,4>;
void Need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct Fixed final:Particle::IParticleBehavior {
    Vector4 color;std::shared_ptr<unsigned> calls;explicit Fixed(Vector4 value,std::shared_ptr<unsigned> count={}):color(value),calls(std::move(count)){}
    void Update(Particle::Particle&,float,Math::Material*) override{if(calls)++*calls;}
    Particle::Particle Create(std::mt19937&,const Vector3& position) override {Particle::Particle value{};value.transform.scale={.5f,.5f,.5f};value.transform.translate=position;value.color=color;value.lifetime=100;return value;}
};
class ParticleChecks final:public Base::Framework {
    Particle::ParticleManager* manager_=Particle::ParticleManager::GetInstance();Particle::ParticleGroupOwner owner_,other_;
    Particle::ParticleGroupHandle stale_{},previous_{};fs::path root_;Microsoft::WRL::ComPtr<ID3D12InfoQueue> messages_;
    unsigned stage_=0,indices_=0,session_=0;bool retired_=false,passed_=false;
    std::array<Pixel,2> clear_{};std::shared_ptr<unsigned> updates_=std::make_shared<unsigned>(0);
    Particle::ParticleGroupOwner Make(const char* name,Vector4 color){return manager_->CreateOwnedParticleGroup(name,"white.dds",Particle::VerticesType::Quad,std::make_unique<Fixed>(color,updates_),4);}
    void GpuClean(){for(UINT64 i=0;i<messages_->GetNumStoredMessagesAllowedByRetrievalFilter();++i){SIZE_T size=0;messages_->GetMessage(i,nullptr,&size);std::vector<unsigned char> bytes(size);auto* message=reinterpret_cast<D3D12_MESSAGE*>(bytes.data());Need(SUCCEEDED(messages_->GetMessage(i,message,&size)),"Particle GPU diagnostic read");Need(message->Severity!=D3D12_MESSAGE_SEVERITY_ERROR && message->Severity!=D3D12_MESSAGE_SEVERITY_CORRUPTION,message->pDescription);}}
    std::array<Pixel,3> Capture(bool editor){
        auto* color=offscreenRenderManager->GetViewRenderTarget(editor);const auto desc=color->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rows=0;UINT64 rowBytes=0,total=0;dxCommon->GetDevice()->GetCopyableFootprints(&desc,0,1,0,&footprint,&rows,&rowBytes,&total);
        D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=total;buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;Microsoft::WRL::ComPtr<ID3D12Resource> readback;Need(SUCCEEDED(dxCommon->GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))),"Particle readback allocation");
        D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=color;from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprint;
        dxCommon->TransitionResource(color,D3D12_RESOURCE_STATE_COPY_SOURCE);dxCommon->GetCommandList()->CopyTextureRegion(&to,0,0,0,&from,nullptr);dxCommon->TransitionResource(color,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);dxCommon->CommandKick();
        void* mapped=nullptr;D3D12_RANGE range{0,size_t(total)};Need(SUCCEEDED(readback->Map(0,&range,&mapped)),"Particle readback map");std::array<Pixel,3> result{};
        for(unsigned i=0;i<3;++i){const auto x=i==2?8:unsigned(desc.Width)*(i?3:1)/4;const auto y=i==2?8:desc.Height/2;std::copy_n(static_cast<const unsigned char*>(mapped)+footprint.Offset+y*footprint.Footprint.RowPitch+x*4,4,result[i].begin());}
        D3D12_RANGE written{0,0};readback->Unmap(0,&written);return result;
    }
public:
    ParticleChecks(unsigned session,Particle::ParticleGroupHandle previous):previous_(previous),session_(session){}
    ~ParticleChecks(){if(!root_.empty()){if(passed_){std::error_code error;fs::remove_all(root_,error);}else std::cerr<<"PARTICLE_GROUP_RETAINED="<<root_<<'\n';}}
    void Initialize() override {
        root_=fs::temp_directory_path()/(L"kubo-particle-groups-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));Need(fs::create_directory(root_),"Refuse existing Particle fixture");
        DirectX::ScratchImage image;Need(SUCCEEDED(image.Initialize2D(DXGI_FORMAT_R8G8B8A8_UNORM,4,4,1,1)),"White image allocation");std::fill_n(image.GetPixels(),image.GetPixelsSize(),static_cast<unsigned char>(255));Need(SUCCEEDED(DirectX::SaveToDDSFile(image.GetImages(),image.GetImageCount(),image.GetMetadata(),DirectX::DDS_FLAGS_NONE,(root_/"white.dds").c_str())),"White DDS save");
        Microsoft::WRL::ComPtr<ID3D12Debug1> debug;Need(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))),"Particle GPU validation unavailable");debug->EnableDebugLayer();debug->SetEnableGPUBasedValidation(TRUE);
        SetProjectRoot(root_);SetGameOutputExtent({320,180});Framework::Initialize();Need(SUCCEEDED(dxCommon->GetDevice()->QueryInterface(IID_PPV_ARGS(&messages_))),"Particle GPU queue missing");offscreenRenderManager->ResizeEditorView({480,270});
        const auto legacy=manager_->CreateParticleGroup("legacy","white.dds",Particle::VerticesType::Quad,std::make_unique<Fixed>(Vector4{0,0,1,1}),4);Need(manager_->CreateParticleGroup("legacy","ignored").index==legacy.index,"Legacy repeated name compatibility");
        bool refused=false;try{auto collision=Make("legacy",{1,0,0,1});}catch(const std::invalid_argument&){refused=true;}Need(refused && manager_->GetParticleGroupCount()==1,"Owned creation stole legacy group");
        refused=false;try{auto invalid=manager_->CreateOwnedParticleGroup("capacity","white.dds",Particle::VerticesType::Quad,nullptr,65537);}catch(const std::invalid_argument&){refused=true;}Need(refused,"Owned capacity bound missing");
        refused=false;try{auto missing=manager_->CreateOwnedParticleGroup("retry","missing.dds",Particle::VerticesType::Quad,std::make_unique<Fixed>(Vector4{1,0,0,1}),4);}catch(const std::exception&){refused=true;}Need(refused && !manager_->GetParticleGroupHandle("retry") && manager_->GetParticleGroupCount()==1,"Failed GPU candidate was published");
        {auto retry=Make("retry",{1,0,0,1});Need(bool(retry),"Candidate retry failed");}Need(manager_->GetParticleGroupCount()==1,"RAII temporary leaked group");
        owner_=Make("owned",{1,0,0,1});owner_.Emit({0,0,.5f},1);Need(owner_.Update(0),"Owned simulation without camera");
        if(previous_){manager_->Emit(previous_,{0,0,.5f},3);Need(*manager_->GetActiveParticleCount("owned")==1,"Old session handle rebound");}
        refused=false;try{auto duplicate=Make("owned",{0,1,0,1});}catch(const std::invalid_argument&){refused=true;}Need(refused && bool(owner_),"Owned duplicate stole group");
        Need(!owner_.Update((std::numeric_limits<float>::quiet_NaN)()),"Nonfinite owned clock admitted");
        auto invalid=Matrix4x4{}.MakeIdentity4x4();invalid.m[0][0]=(std::numeric_limits<float>::quiet_NaN)();refused=false;try{owner_.Draw(invalid,Matrix4x4{}.MakeIdentity4x4());}catch(const std::invalid_argument&){refused=true;}Need(refused,"Nonfinite view admitted");
        stale_=owner_.GetHandle();
    }
    void Update() override {
        const auto before=*updates_;Need(CameraSystem::CameraManager::GetInstance()->GetActiveCamera()!=nullptr,"Legacy camera missing from update isolation check");Framework::Update();Need(*updates_==before,"Legacy global update touched owned group");
        if(stage_==3){owner_=Make("owned",{0,1,0,1});Need(owner_.GetHandle().index==stale_.index && owner_.GetHandle().generation!=stale_.generation,"Freed slot not safely reused");owner_.Emit({0,0,.5f},1);manager_->Emit(stale_,{0,0,.5f},3);Need(*manager_->GetActiveParticleCount("owned")==1,"Stale handle changed replacement");Need(owner_.Update(0),"Replacement update");}
        if(stage_==4){auto transferred=std::move(owner_);Need(!owner_ && bool(transferred),"Move constructor duplicated ownership");other_=Make("other",{0,0,1,1});other_=std::move(transferred);Need(!transferred && bool(other_) && !manager_->GetParticleGroupHandle("other") && manager_->GetParticleGroupCount()==2,"Move assignment did not retire destination");}
        if(stage_==5){manager_->ClearParticles("owned");other_.Emit({0,0,.5f},1);Need(other_.Update(0),"Clear then emit/update failed");}
        if(stage_==6){other_.Reset();Need(manager_->GetParticleGroupCount()==1 && manager_->GetParticleGroupHandle("legacy").has_value(),"Reset removed unrelated legacy group");}
        if(stage_==7){owner_=Make("owned",{1,0,0,1});owner_.Emit({0,0,.5f},1);Need(owner_.Update(0),"Last owner update");}
    }
    void Draw() override {
        dxCommon->PrepareForFrame();const auto frame=dxCommon->GetCurrentFrameIndex();indices_|=1u<<frame;dxCommon->BeginGpuFrameTiming();
        auto identity=Matrix4x4{}.MakeIdentity4x4(),left=identity,right=identity;left.m[3][0]=-.5f;right.m[3][0]=.5f;
        auto& active=stage_==4 || stage_==5?other_:owner_;
        offscreenRenderManager->Begin();srvManager->PreDraw();manager_->Draw();Need(manager_->GetLastDrawCallCount()==0,"Legacy global draw included owned group");if(active)Need(active.Draw(left,identity),"Game Particle draw");offscreenRenderManager->End();
        if(stage_==2){owner_.Reset();const auto retired=manager_->GetRetiredParticleResourceCount();Need(retired>0,"Recorded vertex released immediately");manager_->CollectRetiredGroups();Need(manager_->GetRetiredParticleResourceCount()==retired,"Pending vertex reclaimed before submission");Need(!owner_.Draw(right,identity),"Reset owner still drew");}
        offscreenRenderManager->BeginEditorView();if(active)Need(active.Draw(right,identity),"Scene Particle draw");offscreenRenderManager->EndEditorView();
        dxCommon->Begin();offscreenRenderManager->Draw();dxCommon->End();
        for(unsigned view=0;view<2;++view){const auto pixels=Capture(view!=0);if(stage_==0)clear_[view]=pixels[2];Need(pixels[2]==clear_[view],"Particle outside clear changed");const bool visible=stage_!=6 && !(stage_==2 && view==1);auto expected=clear_[view];if(visible)expected[stage_>=3 && stage_<=5?1:0]=255;
            Need(pixels[view?1:0]==expected && pixels[view?0:1]==clear_[view],"Particle view pixels alias/stale/missing");std::cout<<"PARTICLE_GROUP_PIXEL session="<<session_<<" stage="<<stage_<<" frame="<<frame<<" view="<<view<<" visible="<<visible<<" rgb="<<int(pixels[view?1:0][0])<<','<<int(pixels[view?1:0][1])<<','<<int(pixels[view?1:0][2])<<'\n';}
        manager_->CollectRetiredGroups();Need(manager_->GetRetiredParticleResourceCount()==0,"Completed Particle vertex not collected");GpuClean();++stage_;
    }
    bool IsEndRequest() const override{return stage_>=8 || Framework::IsEndRequest();}
    void Finalize() override {if(retired_)return;Framework::Finalize();Need(!owner_ && !other_,"Framework retirement left live owners");owner_.Reset();other_.Reset();retired_=true;if(messages_)GpuClean();}
    Particle::ParticleGroupHandle Verify(){std::cout<<"PARTICLE_GROUP_FINAL session="<<session_<<" retired="<<retired_<<" stages="<<stage_<<" indices="<<indices_<<'\n';Need(retired_ && stage_==8 && (indices_&(indices_-1)),"Particle stages/indices/shutdown incomplete");passed_=true;return stale_;}
};
}
void RunParticleGroupChecks(){Particle::ParticleGroupHandle previous{};for(unsigned session=0;session<2;++session){if(session){MSG quit{};while(PeekMessageW(&quit,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)){} }auto checks=std::make_unique<ParticleChecks>(session,previous);try{checks->Run();previous=checks->Verify();}catch(...){const auto error=std::current_exception();try{checks->Finalize();}catch(const std::exception& cleanup){std::cerr<<"Particle cleanup: "<<cleanup.what()<<'\n';}std::rethrow_exception(error);}}std::cout<<"PASS: actual Particle owned groups/candidate failure+retry/legacy isolation/move+Reset/stale slots+sessions, independent Game320x180 Scene480x270 pixels32, pending vertex retention/completed collection, clear+reemit, multiple swapchain indices and GPU errors0/shutdown (not Entity authoring/Lua/MCP)\n";}
