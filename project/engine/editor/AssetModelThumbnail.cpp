#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "AssetModelThumbnail.h"
#include "engine/base/directx/DirectXCommon.h"
#include "engine/base/directx/SrvManager.h"
#include "engine/base/directx/TextureManager.h"
#include "engine/base/EngineResourcePath.h"
#include "engine/base/AssetPath.h"
#include "engine/assets/AssetImportFingerprint.h"
#include "engine/3d/object/Object3D.h"
#include "engine/3d/object/Object3DCommon.h"
#include "engine/camera/Camera.h"
#include "engine/camera/CameraManager.h"
#include "engine/base/directx/HResult.h"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace Engine::Editor {
namespace {
void Need(bool value,const char* error){if(!value)throw std::runtime_error(error);}
struct CameraScope {
    CameraSystem::Camera camera;
    CameraSystem::CameraManager* manager=CameraSystem::CameraManager::GetInstance();
    std::string name,previous;
    CameraScope(){
        // Fixed address identity is unique while this stack owner is alive.
        name="__asset_thumbnail/"+std::to_string(reinterpret_cast<uintptr_t>(this));
        Need(!manager->FindCamera(name),"Thumbnail camera name is already owned");
        previous=manager->GetActiveCameraName();
        camera.SetRotate({.3f,-.6f,0});camera.SetAspectRatio(1);camera.SetOrthographic(true,2.6f);
        camera.SetNearClip(.1f);camera.SetFarClip(10);camera.SetTranslate({0,0,0});camera.Update();
        const auto matrix=camera.GetWorldMatrix();
        camera.SetTranslate({-matrix.m[2][0]*4,-matrix.m[2][1]*4,-matrix.m[2][2]*4});camera.Update();
        manager->AddCamera(name,&camera);manager->SetActiveCamera(name);
    }
    ~CameraScope(){manager->DetachCamera(name,&camera,previous);}
};
struct LightScope {
    Graphics3D::Object3DCommon* common=Graphics3D::Object3DCommon::GetInstance();
    Math::AuthoredLightData authored=common->GetAuthoredLights();
    bool shadowEnabled=common->IsShadowEnabled();
    LightScope(){Need(common->SetAuthoredLights({}),"Cannot enter thumbnail lighting scope");common->SetShadowEnabled(false);}
    ~LightScope(){common->SetShadowEnabled(shadowEnabled);common->SetAuthoredLights(authored);}
};
}
AssetModelThumbnail::AssetModelThumbnail()=default;
AssetModelThumbnail::~AssetModelThumbnail(){
    // Explicit idle release is part of the host contract. Failing closed here
    // prevents silently releasing resources still named by recorded commands.
    if(color_||depth_||model_||environment_)std::terminate();
}
void AssetModelThumbnail::Prepare(std::shared_ptr<Base::DirectXCommon> graphics,const std::string& root,
    const std::string& path,const std::string& identity,const std::string& revision){
    Need(graphics&&graphics->GetDevice()&&!color_&&!model_,"Invalid or reused model thumbnail owner");
    graphics_=std::move(graphics);
    try {
        auto* common=Graphics3D::Object3DCommon::GetInstance();
        model_=Graphics3D::Object3D::Create(common,path,root,identity,revision);
        // Object3D binds a TextureCube even at reflection strength zero. Give
        // it the installed engine cube through a lease, never an empty key.
        const auto environment=Base::ResolveEngineResource("Resources/textures/skybox/test.dds");
        environment_=Base::TextureManager::GetInstance()->AcquireTextureRevision(Base::PathToUtf8(environment),Assets::HashAssetContent(environment));
        model_->SetSkyboxFilePath(environment_->key);
        Vector3 lo{},hi{};Need(model_->GetLocalModelBounds(lo,hi),"Model thumbnail has no bounds");
        const double center[]{(double(lo.x)+hi.x)/2,(double(lo.y)+hi.y)/2,(double(lo.z)+hi.z)/2};
        const double half[]{(double(hi.x)-lo.x)/2,(double(hi.y)-lo.y)/2,(double(hi.z)-lo.z)/2};
        const double radius=std::sqrt(half[0]*half[0]+half[1]*half[1]+half[2]*half[2]);
        Need(std::isfinite(radius)&&radius>=1e-6&&radius<=1e8&&half[0]>=0&&half[1]>=0&&half[2]>=0,"Invalid model thumbnail bounds");
        const float scale=static_cast<float>(1/radius);
        Vector3 translation{};float* components[]{&translation.x,&translation.y,&translation.z};
        for(size_t i=0;i<3;++i){const double value=-center[i]/radius;Need(std::isfinite(value)&&std::abs(value)<=1e8,"Model thumbnail center exceeds limits");*components[i]=static_cast<float>(value);}
        model_->SetScale({scale,scale,scale});model_->SetRotate({0,0,0});model_->SetTranslate(translation);
        model_->SetAnimationSpeed(0);model_->SetFrustumCullingEnabled(false);model_->SetCastsShadow(false);
        model_->SetLighting(true);model_->SetDirectionalLightEnable(true);model_->SetDirectionalLightDirection({.4082483f,-.8164966f,.4082483f});
        model_->SetDirectionalLightIntensity(1);model_->SetDirectionalLightColor({1,1,1,1});
        model_->SetPointLightEnable(false);model_->SetSpotLightEnable(false);model_->SetEnvironmentReflectionStrength(0);
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC description{};description.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        description.Width=Edge;description.Height=Edge;description.DepthOrArraySize=1;description.MipLevels=1;description.SampleDesc.Count=1;
        description.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;description.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D12_CLEAR_VALUE clear{};clear.Format=description.Format;clear.Color[0]=.045f;clear.Color[1]=.05f;clear.Color[2]=.06f;clear.Color[3]=1;
        Base::ThrowIfFailed(graphics_->GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&description,D3D12_RESOURCE_STATE_RENDER_TARGET,&clear,IID_PPV_ARGS(&color_)),"Thumbnail color");
        rtv_=graphics_->CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1,false);
        graphics_->GetDevice()->CreateRenderTargetView(color_.Get(),&graphics_->GetRTVDesc(),rtv_->GetCPUDescriptorHandleForHeapStart());
        description.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;description.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        clear={};clear.Format=description.Format;clear.DepthStencil.Depth=1;
        Base::ThrowIfFailed(graphics_->GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&description,D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear,IID_PPV_ARGS(&depth_)),"Thumbnail depth");
        dsv_=graphics_->CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV,1,false);
        graphics_->GetDevice()->CreateDepthStencilView(depth_.Get(),nullptr,dsv_->GetCPUDescriptorHandleForHeapStart());
        graphics_->TrackResourceState(color_.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET);
        graphics_->TrackResourceState(depth_.Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE);
    }catch(...){
        // Model preparation may already have recorded uploads. Keep every owner
        // until that work has completed; caller must retain this owner if wait fails.
        graphics_->CommandKick();graphics_->WaitForAllFrames();ReleaseAfterGpuIdle();throw;
    }
}
void AssetModelThumbnail::Draw(){
    Need(graphics_&&color_&&depth_&&model_&&!rendered_,"Model thumbnail is not prepared");
    CameraScope camera;LightScope lights;
    auto* commands=graphics_->GetCommandList();
    graphics_->TransitionResource(color_.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET);
    const auto rtv=rtv_->GetCPUDescriptorHandleForHeapStart(),dsv=dsv_->GetCPUDescriptorHandleForHeapStart();
    commands->OMSetRenderTargets(1,&rtv,false,&dsv);
    const float clear[]{.045f,.05f,.06f,1};commands->ClearRenderTargetView(rtv,clear,0,nullptr);
    commands->ClearDepthStencilView(dsv,D3D12_CLEAR_FLAG_DEPTH,1,0,0,nullptr);
    const D3D12_VIEWPORT viewport{0,0,float(Edge),float(Edge),0,1};const D3D12_RECT scissor{0,0,LONG(Edge),LONG(Edge)};
    commands->RSSetViewports(1,&viewport);commands->RSSetScissorRects(1,&scissor);
    auto* common=Graphics3D::Object3DCommon::GetInstance();common->GetSrvManager()->PreDraw();common->CommonDraw();
    model_->Update(0);model_->RefreshViewTransform();model_->DrawAnimated();
    graphics_->TransitionResource(color_.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);rendered_=true;
}
void AssetModelThumbnail::ReleaseModelAfterGpuIdle() noexcept {model_.reset();environment_.reset();}
void AssetModelThumbnail::ReleaseAfterGpuIdle() noexcept {
    model_.reset();environment_.reset();if(graphics_){graphics_->UntrackResourceState(color_.Get());graphics_->UntrackResourceState(depth_.Get());}
    color_.Reset();depth_.Reset();rtv_.Reset();dsv_.Reset();graphics_.reset();rendered_=false;
}
}


