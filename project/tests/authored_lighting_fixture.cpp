#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "authored_lighting_fixture.h"
#include "Framework.h"
#include "DirectXCommon.h"
#include "OffscreenRenderManager.h"
#include "SrvManager.h"
#include "Object3D.h"
#include "Object3DCommon.h"
#include "ModelManager.h"
#include "CameraManager.h"
#include "engine/scene/entity/EntityScene.h"
#include "engine/base/AssetPath.h"
#include "engine/assets/MaterialAsset.h"
#include "engine/base/EngineResourcePath.h"
#include "engine/script/ScriptRuntime.h"
#include "engine/services/McpCommandBridge.h"
#include <d3d12sdklayers.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <array>
#include <cstring>

class AuthoredLightingChecks final : public Engine::Base::Framework {
    Engine::Scene::EntityWorld world_;
    Engine::Assets::AssetRegistry assets_;
    std::unique_ptr<Engine::Scene::EntityScene> scene_;
    std::filesystem::path fixture_;
    Engine::Scene::EntityId model_=0,light_=0,parent_=0;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> messages_;
    bool done_=false,passed_=false;
    std::array<uint64_t,3> pixelSum_{};
    using Pixels=std::array<unsigned char,4>;
    static void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
    template<class Draw> Pixels Capture(Draw draw){
        dxCommon->PrepareForFrame();offscreenRenderManager->Begin();srvManager->PreDraw();draw();offscreenRenderManager->End();
        auto* color=offscreenRenderManager->GetViewRenderTarget(false);const auto desc=color->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rows=0;UINT64 rowBytes=0,total=0;
        dxCommon->GetDevice()->GetCopyableFootprints(&desc,0,1,0,&footprint,&rows,&rowBytes,&total);
        D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=total;buffer.Height=1;
        buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        Need(SUCCEEDED(dxCommon->GetDevice()->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))),"Lighting readback allocation failed");
        D3D12_TEXTURE_COPY_LOCATION source{};source.pResource=color;source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION destination{};destination.pResource=readback.Get();destination.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;destination.PlacedFootprint=footprint;
        dxCommon->TransitionResource(color,D3D12_RESOURCE_STATE_COPY_SOURCE);dxCommon->GetCommandList()->CopyTextureRegion(&destination,0,0,0,&source,nullptr);
        dxCommon->TransitionResource(color,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);dxCommon->CommandKick();
        void* mapped=nullptr;D3D12_RANGE range{0,size_t(total)};Need(SUCCEEDED(readback->Map(0,&range,&mapped)),"Lighting readback map failed");
        Pixels result{};pixelSum_={};const auto* bytes=static_cast<const unsigned char*>(mapped)+footprint.Offset;
        for(UINT y=0;y<desc.Height;++y)for(UINT64 x=0;x<desc.Width;++x)for(size_t channel=0;channel<3;++channel)pixelSum_[channel]+=bytes[size_t(y)*footprint.Footprint.RowPitch+size_t(x)*4+channel];
        const auto* pixel=bytes+size_t(desc.Height/2)*footprint.Footprint.RowPitch+size_t(desc.Width/2)*4;
        std::copy_n(pixel,4,result.begin());D3D12_RANGE written{0,0};readback->Unmap(0,&written);return result;
    }
    Pixels ScenePixel(){scene_->SynchronizeModels(0);const auto old=Engine::Graphics3D::Object3DCommon::GetInstance()->GetAuthoredLights();
        auto pixel=Capture([&]{scene_->Draw();});Need(!std::memcmp(&old,&Engine::Graphics3D::Object3DCommon::GetInstance()->GetAuthoredLights(),sizeof(old)),"EntityScene leaked authored light state");return pixel;}
    void GpuClean(){for(UINT64 i=0;i<messages_->GetNumStoredMessagesAllowedByRetrievalFilter();++i){SIZE_T bytes=0;messages_->GetMessage(i,nullptr,&bytes);std::vector<unsigned char> data(bytes);auto* message=reinterpret_cast<D3D12_MESSAGE*>(data.data());
        Need(SUCCEEDED(messages_->GetMessage(i,message,&bytes)),"GPU message retrieval failed");Need(message->Severity!=D3D12_MESSAGE_SEVERITY_ERROR && message->Severity!=D3D12_MESSAGE_SEVERITY_CORRUPTION,"Light GPU Error/Corruption");}}
    void CheckAuthoringPixels(){
        const auto saved=world_.Capture();
        const auto script="function on_create(e) assert(scene.set_material(e:id(),{lighting=true,textureInfluence=0}));assert(scene.set_light("+std::to_string(light_)+",{type='point',color={0,1,0},intensity=0.7})) end\nfunction on_fixed_update(e,dt) assert(scene.set_light("+std::to_string(light_)+",{type='spot',color={0,0,1},intensity=0.7})) end";
        {std::ofstream out(fixture_/"Assets"/"render.lua",std::ios::binary);out<<script;out.close();Need(!out.fail(),"Render script fixture write failed");}
        Engine::Scripting::ScriptRuntime runtime;Need(runtime.LoadAsset("Assets/render.lua",fixture_,error_),error_.c_str());
        Need(world_.SetScript(model_,"Assets/render.lua"),"Render script attachment failed");runtime.Update(world_,0);
        Need(runtime.GetLastError(model_).empty(),"Lua render Create failed");const auto green=ScenePixel();
        Need(green[1]>80 && green[0]<5 && green[2]<5,"Lua Point did not reach actual GPU surface");
        runtime.FixedUpdate(world_,1.f/60);Need(runtime.GetLastError(model_).empty(),"Lua render Fixed failed");const auto blue=ScenePixel();
        Need(blue[2]>80 && blue[0]<5 && blue[1]<5,"Lua Spot did not reach actual GPU surface");runtime.Shutdown(world_);
        Need(world_.Restore(saved,error_),error_.c_str());
        Engine::Services::McpCommandBridge bridge({});
        const auto edit=[&](Engine::Scene::EntityId id,const char* component,const char* patch){
            Engine::Scene::SceneValue request;request["id"]=1.0;request["method"]="scene.set_component";
            request["params"]["entityId"]=double(id);request["params"]["component"]=component;
            request["params"]["patch"]=Engine::Scene::SceneValue::parse(patch);Need(bridge.Handle(request,world_).contains("result"),"MCP render edit failed");
        };
        edit(model_,"material",R"({"lighting":false,"color":[0,1,0,1],"textureInfluence":0})");const auto tint=ScenePixel();
        Need(tint[1]>220 && tint[0]<5 && tint[2]<5,"MCP Material tint did not reach actual GPU surface");
        edit(model_,"material",R"({"lighting":true,"color":[1,1,1,1]})");
        edit(light_,"light",R"({"type":"point","color":[1,0,0],"intensity":0.7,"range":10})");const auto red=ScenePixel();
        Need(red[0]>80 && red[1]<5 && red[2]<5,"MCP Light did not reach actual GPU surface");
        Need(world_.Restore(saved,error_),error_.c_str());
        std::cout<<"LIGHT_AUTHORING_PIXELS lua_point="<<int(green[1])<<" lua_spot="<<int(blue[2])<<" mcp_tint="<<int(tint[1])<<" mcp_point="<<int(red[0])<<'\n';
        Engine::Scene::LightComponent zero;zero.intensity=0;Need(world_.SetLight(light_,zero),"Capacity base Light refused");
        Engine::Scene::MaterialComponent lit;lit.lighting=true;lit.textureInfluence=0;Need(world_.SetMaterial(model_,lit),"Capacity Material refused");
        for(int i=0;i<63;++i){const auto id=world_.CreateEntity("GPU light slot");auto value=zero;
            if(i==62){value.kind=Engine::Scene::LightKind::Point;value.color={1,0,0};value.intensity=0.7f;world_.Find(id)->transform.position={0,0,-2};}
            Need(world_.SetLight(id,value),"Capacity slot Light refused");}
        const auto last=ScenePixel();Need(scene_->GetLightingResult()==Engine::Scene::RenderLightResult::Success && scene_->GetLightingFrame().gpu.count==64 && last[0]>80 && last[1]<5 && last[2]<5,"64th Light slot did not reach shader");
        std::cout<<"LIGHT_GPU_CAPACITY count=64 last_slot_red="<<int(last[0])<<'\n';Need(world_.Restore(saved,error_),error_.c_str());
    }
    void CheckSharedMaterialPixels(){
        using namespace Engine;
        const auto before=world_.Capture();
        const auto source=fixture_/"Assets/shared.material";
        Scene::MaterialComponent shared;
        shared.color={0,1,0,1};shared.textureInfluence=0;
        Need(Assets::WriteMaterialAsset(source,shared,error_,false),error_.c_str());
        Scene::MaterialComponent first,second;
        first.color={1,0,0,1};first.textureInfluence=0;
        second.color={0,0,1,1};second.textureInfluence=0;
        Need(assets_.RegisterSource(source,first.assetGuid,error_),error_.c_str());
        second.assetGuid=first.assetGuid;
        const auto another=world_.CreateEntity("Second user of the same asset");
        Need(world_.SetModel(another,"Assets/plane.obj") &&
            world_.SetMaterial(model_,first) && world_.SetMaterial(another,second),"Shared Material assignment failed");
        world_.Find(another)->active=false;
        const auto green=ScenePixel();
        Need(green[1]>220 && green[0]<5 && green[2]<5,"Shared Material did not replace the first local tint");
        Scene::MaterialValues draft;
        draft.color={0,0,1,1};draft.textureInfluence=0;
        const auto preview=Capture([&]{scene_->DrawEditorView(model_,&draft);});
        Need(preview[2]>220 && preview[0]<5 && preview[1]<5,"Editor draft Material did not reach the Scene view");
        Need(ScenePixel()==green,"Editor draft Material leaked into the Game view");
        const auto path=fixture_/"shared.scene";
        Need(world_.Save(path,error_) && world_.Load(path,error_),error_.c_str());
        world_.Find(another)->active=true;world_.Find(model_)->active=false;
        Need(ScenePixel()==green,"Reloading Scene or switching to the second Entity lost the shared tint");
        shared.color={1,0,1,1};shared.roughness=.125f;
        Need(Assets::WriteMaterialAsset(source,shared,error_,true),error_.c_str());
        const auto violet=ScenePixel();
        Need(violet[0]>220 && violet[1]<5 && violet[2]>220,"Shared Material edit did not reach the second Entity");
        world_.Find(another)->active=false;world_.Find(model_)->active=true;
        Need(ScenePixel()==violet,"Shared Material edit did not reach the first Entity");
        const auto moved=fixture_/"Assets/moved.material";
        std::filesystem::rename(source,moved);
        std::filesystem::rename(Assets::AssetRegistry::MetadataPath(source),Assets::AssetRegistry::MetadataPath(moved));
        Need(assets_.Scan(fixture_/"Assets",error_,Assets::AssetRegistry::MetadataPolicy::ReadOnly),error_.c_str());
        Need(ScenePixel()==violet,"Renaming a shared Material broke a GUID assignment");
        std::filesystem::remove(moved);
        const auto fallback=ScenePixel();
        Need(fallback[0]>220 && fallback[1]<5 && fallback[2]<5,"Missing shared Material did not use the local tint");
        Need(world_.Restore(before,error_),error_.c_str());
        std::cout<<"LIGHT_SHARED_MATERIAL same_tint=1 shared_edit=1 scene_roundtrip=1 moved_guid=1 local_fallback=1\n";
    }
    void CheckEnvironmentPixels(){
        const auto saved=world_.Capture();auto light=world_.Find(light_)->light;light.enabled=false;Need(world_.SetLight(light_,light),"Environment Light disable failed");
        Engine::Scene::MaterialComponent material;material.lighting=true;material.textureInfluence=0;material.roughness=0.5f;
        Need(world_.SetMaterial(model_,material),"Environment Material setup failed");const auto darkPixel=ScenePixel();const auto dark=pixelSum_;
        material.reflectionStrength=0.25f;Need(world_.SetMaterial(model_,material),"Low reflection setup failed");const auto lowPixel=ScenePixel();const auto low=pixelSum_;
        material.reflectionStrength=0.75f;Need(world_.SetMaterial(model_,material),"High reflection setup failed");const auto highPixel=ScenePixel();const auto high=pixelSum_;
        const auto total=[](const auto& values){return values[0]+values[1]+values[2];};
        std::cout<<"LIGHT_ENVIRONMENT_PIXELS dark="<<total(dark)<<" reflection025="<<total(low)<<" reflection075="<<total(high)<<'\n';
        // Offscreen clear pixels outside the plane are not black; isolate the
        // surface baseline and require positive full-frame deltas separately.
        Need(darkPixel[0]<5 && darkPixel[1]<5 && darkPixel[2]<5 && total(lowPixel)>total(darkPixel)+5 && total(highPixel)>total(lowPixel)+5 && total(low)>total(dark)+500 && total(high)>total(low)+500,"Authored Material environment reflection did not produce increasing pixels");
        Need(world_.Restore(saved,error_),error_.c_str());
    }
    void CheckUploadAndLegacy(){
        using Engine::Graphics3D::Object3D;auto* common=Engine::Graphics3D::Object3DCommon::GetInstance();
        struct Restore {Engine::Graphics3D::Object3DCommon* owner;Engine::Math::AuthoredLightData authored;SceneLightData legacy;bool shadow;
            ~Restore(){owner->SetAuthoredLights(authored);owner->SetSceneLight(legacy);owner->SetShadowEnabled(shadow);}} restore{common,common->GetAuthoredLights(),common->GetSceneLight(),common->IsShadowEnabled()};
        auto* camera=Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera();camera->ClearWorldMatrix();camera->SetAspectRatio(1);camera->SetOrthographic(true,3);camera->SetTranslate({0,0,-3});camera->Update();
        auto object=Object3D::Create(common,"plane.obj",Engine::Base::PathToUtf8(fixture_/"Assets"));object->SetSkyboxFilePath(Engine::Base::PathToUtf8(Engine::Base::ResolveEngineResource("Resources/textures/skybox/test.dds")));
        object->SetLighting(true);object->SetTextureInfluence(0);object->SetEnvironmentReflectionStrength(0);object->SetFrustumCullingEnabled(false);object->SetColor({1,1,1,1});object->Update(0);
        Engine::Math::AuthoredLightData authored;authored.mode=1;authored.count=1;authored.lights[0].colorIntensity={1,0,0,0.7f};authored.lights[0].directionOuter={0,0,1,std::cos(0.6f)};authored.lights[0].positionRange={0,0,-2,10};authored.lights[0].parameters={std::cos(0.35f),2,0,0};
        Need(common->SetAuthoredLights(authored),"Upload test frame refused");
        const auto afterBind=[](size_t offset,bool upload){const auto allocate=[&](size_t size){offset=(offset+255)&~size_t(255);offset+=size;};allocate(sizeof(SceneLightData));allocate(sizeof(ShadowMapData));if(upload)allocate(sizeof(Engine::Math::AuthoredLightData));return offset;};
        uint64_t previousFence=0;
        for(int submission=0;submission<3;++submission){
            const auto fence=dxCommon->GetPendingSubmissionFenceValue();Need(fence>previousFence,"Upload test did not cross fenced submissions");previousFence=fence;
            Need(common->SetAuthoredLights(authored),"Repeated identical frame refused");
            const auto pixel=Capture([&]{common->CommonDraw();const auto begin=dxCommon->GetCurrentFrameUploadUsedBytes();common->BindSceneLighting();const auto first=dxCommon->GetCurrentFrameUploadUsedBytes();
                Need(first==afterBind(begin,true),"New fenced submission reused a retired authored upload");
                Need(common->SetAuthoredLights(authored),"Same-submission identical frame refused");common->BindSceneLighting();const auto reused=dxCommon->GetCurrentFrameUploadUsedBytes();
                Need(reused==afterBind(first,false),"Identical authored data was uploaded again within submission");
                auto changed=authored;changed.lights[0].colorIntensity.w=0.6f;Need(common->SetAuthoredLights(changed),"Dirty frame refused");common->BindSceneLighting();const auto dirty=dxCommon->GetCurrentFrameUploadUsedBytes();
                Need(dirty==afterBind(reused,true),"Changed authored data failed to allocate a fresh upload");
                Need(common->SetAuthoredLights(authored),"Upload original restore failed");object->Draw();
                std::cout<<"LIGHT_UPLOAD submission="<<submission<<" frame="<<dxCommon->GetCurrentFrameIndex()<<" fence="<<fence<<" first="<<first-begin<<" reuse="<<reused-first<<" dirty="<<dirty-reused<<'\n';});
            Need(pixel[0]>80 && pixel[1]<5 && pixel[2]<5,"Repeated fenced submission rendered stale/invalid authored data");
        }
        SceneLightData legacy{};legacy.enable=1;legacy.color={0,1,0,1};legacy.direction={0,0,1};legacy.intensity=0.5f;common->SetSceneLight(legacy);common->SetShadowEnabled(false);
        Need(common->SetAuthoredLights({}),"Legacy mode refused");const auto draw=[&]{common->CommonDraw();object->Draw();};const auto before=Capture(draw);const auto beforeSum=pixelSum_;
        Need(before[1]>80 && before[0]<5 && before[2]<5,"Legacy lit baseline not visible");
        Need(common->SetAuthoredLights(authored),"Legacy authored detour refused");const auto red=Capture(draw);Need(red[0]>80 && red[1]<5 && red[2]<5,"Authored detour not visible");
        Need(common->SetAuthoredLights({}),"Legacy mode restore refused");const auto after=Capture(draw);Need(after==before && pixelSum_==beforeSum,"Authored detour changed subsequent legacy lit pixels");
        std::cout<<"LIGHT_LEGACY_RETURN before_green="<<int(before[1])<<" authored_red="<<int(red[0])<<" after_green="<<int(after[1])<<" full_rgb_sums_equal=1\n";
    }
    void CheckBatches(){
        using Engine::Graphics3D::Object3D;auto* common=Engine::Graphics3D::Object3DCommon::GetInstance();
        struct Restore {Engine::Graphics3D::Object3DCommon* owner;Engine::Math::AuthoredLightData old;~Restore(){owner->SetAuthoredLights(old);}} restore{common,common->GetAuthoredLights()};
        auto* camera=Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera();
        camera->ClearWorldMatrix();camera->SetAspectRatio(1);camera->SetOrthographic(true,3);camera->SetTranslate({0,0,-3});camera->Update();
        const auto environment=Engine::Base::PathToUtf8(Engine::Base::ResolveEngineResource("Resources/textures/skybox/test.dds"));
        const auto configure=[&](Object3D& object){object.SetSkyboxFilePath(environment);object.SetLighting(true);object.SetTextureInfluence(0);object.SetEnvironmentReflectionStrength(0);object.SetFrustumCullingEnabled(false);object.SetColor({1,1,1,1});object.Update(0);};
        auto first=Object3D::Create(common,"plane.obj",Engine::Base::PathToUtf8(fixture_/"Assets"));
        auto second=Object3D::Create(common,"plane.obj",Engine::Base::PathToUtf8(fixture_/"Assets"));configure(*first);configure(*second);
        Vector3 center{0,0,0};float radius=1;
        const auto lightData=[&](int kind){Engine::Math::AuthoredLightData data;data.mode=1;data.count=6;
            const Vector3 directions[]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
            for(size_t i=0;i<6;++i){const auto d=directions[i];auto& light=data.lights[i];light.colorIntensity={1,0,0,0.4f};
                light.positionRange={center.x+d.x*radius*4,center.y+d.y*radius*4,center.z+d.z*radius*4,radius*10};
                light.directionOuter={-d.x,-d.y,-d.z,std::cos(0.6f)};light.parameters={std::cos(0.35f),2,float(kind),0};}
            return data;};
        const auto exercise=[&](bool skinned,Object3D& a,Object3D& b){
            const auto draw=[&]{if(skinned)Object3D::DrawSkinningBatch({&a,&b});else Object3D::DrawStaticBatch({&a,&b});};
            Engine::Math::AuthoredLightData dark;dark.mode=1;Need(common->SetAuthoredLights(dark),"Dark batch frame refused");Capture(draw);const auto held=pixelSum_;
            for(int kind=0;kind<3;++kind){Need(common->SetAuthoredLights(lightData(kind)),"Batch light data refused");common->ResetInstanceBatchStatistics();Capture(draw);
                const auto& stats=common->GetInstanceBatchStats();Need(skinned?stats.skinningBatchCount==1:stats.objectBatchCount==1,"Expected actual instanced draw not observed");
                std::cout<<"LIGHT_BATCH skinned="<<skinned<<" kind="<<kind<<" red_delta="<<(pixelSum_[0]-held[0])<<'\n';
                Need(pixelSum_[0]>held[0]+500 && pixelSum_[1]==held[1] && pixelSum_[2]==held[2],"Actual static/skinned batch did not receive colored 3-kind lighting");}
            Need(a.SetShininess(8) && b.SetShininess(128),"Batch shininess setup failed");common->ResetInstanceBatchStatistics();Capture(draw);
            const auto& stats=common->GetInstanceBatchStats();Need(skinned?stats.skinningBatchCount==2:stats.objectBatchCount==2,"Different material shininess incorrectly shared one batch");
            if(skinned){
                const auto single=[&]{common->SkinningCommonDraw();a.DrawSkinning();common->CommonDraw();};
                Need(common->SetAuthoredLights(dark),"Dark single frame refused");Capture(single);const auto base=pixelSum_;
                for(int kind=0;kind<3;++kind){Need(common->SetAuthoredLights(lightData(kind)),"Single skin light data refused");Capture(single);
                    std::cout<<"LIGHT_SINGLE skinned=1 kind="<<kind<<" red_delta="<<(pixelSum_[0]-base[0])<<'\n';
                    Need(pixelSum_[0]>base[0]+500 && pixelSum_[1]==base[1] && pixelSum_[2]==base[2],"Actual non-instanced skinning missed authored lighting");}
            }
        };
        exercise(false,*first,*second);
        const auto walk=Engine::Base::ResolveEngineResource("Resources/models/walk");
        auto skinA=Object3D::Create(common,"walk.gltf",Engine::Base::PathToUtf8(walk));auto skinB=Object3D::Create(common,"walk.gltf",Engine::Base::PathToUtf8(walk));
        Vector3 minimum{},maximum{};Need(skinA->GetLocalModelBounds(minimum,maximum),"Skinned batch bounds missing");
        center={(minimum.x+maximum.x)*0.5f,(minimum.y+maximum.y)*0.5f,(minimum.z+maximum.z)*0.5f};
        radius=(std::max)({maximum.x-minimum.x,maximum.y-minimum.y,maximum.z-minimum.z,1.0f});
        camera->SetOrthographic(true,radius*1.5f);camera->SetTranslate({center.x,center.y,minimum.z-radius*3});camera->Update();
        configure(*skinA);configure(*skinB);Need(skinA->CanDrawSkinning() && skinB->CanDrawSkinning(),"Actual skinning fixture unavailable");
        exercise(true,*skinA,*skinB);scene_->SynchronizeModels(0);
    }
public:
    ~AuthoredLightingChecks(){if(!fixture_.empty()){if(passed_){std::error_code ec;std::filesystem::remove_all(fixture_,ec);}else std::cerr<<"Retained lighting fixture: "<<fixture_<<'\n';}}
    void Initialize() override {
        fixture_=std::filesystem::temp_directory_path()/(L"kubo-authored-lights-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        Need(std::filesystem::create_directory(fixture_),"Refuse existing lighting fixture");std::filesystem::create_directory(fixture_/"Assets");
        {std::ofstream material(fixture_/"Assets/surface.mtl");material<<"newmtl Surface\nKd 1 1 1\nd 1\n";Need(bool(material),"Lighting material fixture write failed");}
        {std::ofstream obj(fixture_/"Assets/plane.obj");obj<<"mtllib surface.mtl\nusemtl Surface\nv -1 -1 0\nv -1 1 0\nv 1 -1 0\nv 1 1 0\nvn 0 0 -1\nf 1//1 2//1 3//1\nf 3//1 2//1 4//1\n";Need(bool(obj),"Lighting fixture write failed");}
        Microsoft::WRL::ComPtr<ID3D12Debug1> validation;
        Need(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&validation))),"Lighting validation interface missing");
        validation->EnableDebugLayer();
        validation->SetEnableGPUBasedValidation(TRUE);
        SetProjectRoot(fixture_);SetGameOutputExtent({320,320});Framework::Initialize();
        Need(SUCCEEDED(dxCommon->GetDevice()->QueryInterface(IID_PPV_ARGS(&messages_))),"GPU validation queue unavailable");
        std::string error;Need(assets_.Scan(fixture_/"Assets",error,Engine::Assets::AssetRegistry::MetadataPolicy::ReadOnly),error.c_str());
        model_=world_.CreateEntity("Surface");Need(world_.SetModel(model_,"Assets/plane.obj"),"Lighting model setup failed");
        const auto camera=world_.CreateEntity("Camera");Engine::Scene::CameraComponent projection;projection.orthographic=true;projection.orthographicHeight=3;
        Need(world_.SetCamera(camera,projection),"Lighting camera setup failed");world_.Find(camera)->transform.position={0,0,-3};
        parent_=world_.CreateEntity("Light parent");light_=world_.CreateEntity("Light",parent_);world_.Find(light_)->transform.position={0,0,-2};
        scene_=std::make_unique<Engine::Scene::EntityScene>(world_,*dxCommon,assets_,fixture_);scene_->Initialize();
    }
    void Draw() override {}
    bool IsEndRequest() const override {return done_;}
    void Update() override {
        using namespace Engine::Scene;
        const auto legacy=ScenePixel();std::cout<<"LIGHT_LEGACY_PIXEL rgb="<<int(legacy[0])<<','<<int(legacy[1])<<','<<int(legacy[2])<<" model="<<scene_->GetModelRenderStatus(model_).loaded<<'\n';Need(legacy[0]>220 && legacy[1]>220 && legacy[2]>220,"Legacy generic unlit geometry not visible/white");
        MaterialComponent material;material.lighting=true;material.textureInfluence=0;Need(world_.SetMaterial(model_,material),"Lit material setup failed");
        const auto dark=ScenePixel();Need(dark[0]<5 && dark[1]<5 && dark[2]<5,"Explicit lit material without lights was not dark");
        for(const auto kind:{LightKind::Directional,LightKind::Point,LightKind::Spot}){
            LightComponent value;value.kind=kind;value.color={1,0,0};value.intensity=0.7f;Need(world_.SetLight(light_,value),"Light setup failed");
            const auto red=ScenePixel();std::cout<<"LIGHT_PIXEL kind="<<int(kind)<<" rgb="<<int(red[0])<<','<<int(red[1])<<','<<int(red[2])<<'\n';
            Need(red[0]>80 && red[0]>red[1]+50 && red[0]>red[2]+50,"Authored light did not illuminate actual surface");
            value.enabled=false;world_.SetLight(light_,value);const auto disabled=ScenePixel();Need(disabled[0]<5 && disabled[1]<5 && disabled[2]<5,"Disabled Light still illuminated");
        }
        LightComponent point;point.kind=LightKind::Point;point.color={0,1,0};point.intensity=0.7f;world_.SetLight(light_,point);
        const auto green=ScenePixel();Need(green[1]>80 && green[0]<5 && green[2]<5,"Point color was not applied");
        world_.Find(parent_)->active=false;const auto inactive=ScenePixel();Need(inactive[1]<5,"Inactive parent Light participated");world_.Find(parent_)->active=true;
        point.range=1;world_.SetLight(light_,point);const auto outside=ScenePixel();Need(outside[1]<5,"Point range did not attenuate outside");
        LightComponent spot;spot.kind=LightKind::Spot;spot.color={0,0,1};spot.innerAngle=spot.outerAngle;world_.SetLight(light_,spot);
        world_.Find(light_)->transform.rotation.y=3.14159265f;const auto away=ScenePixel();Need(away[2]<5,"Spot pointing away still illuminated");world_.Find(light_)->transform.rotation.y=0;
        const auto inside=ScenePixel();Need(inside[2]>80 && inside[0]<5,"Hard-cone Spot did not illuminate");
        world_.RemoveMaterial(model_);const auto removed=ScenePixel();Need(removed==legacy,"Material removal did not restore legacy output");
        const auto before=world_.Capture().dump();for(int i=0;i<65;++i){const auto id=world_.CreateEntity("Capacity");world_.SetLight(id,{});}
        const auto retained=scene_->GetLightingFrame().gpu;scene_->SynchronizeModels(0);
        Need(scene_->GetLightingResult()==RenderLightResult::Limit && !std::memcmp(&retained,&scene_->GetLightingFrame().gpu,sizeof(retained)),"Oversized light list published a prefix");
        Need(world_.Restore(Engine::Scene::SceneValue::parse(before),error_),error_.c_str());
        CheckAuthoringPixels();CheckSharedMaterialPixels();CheckEnvironmentPixels();CheckUploadAndLegacy();CheckBatches();GpuClean();done_=true;
    }
    void Finalize() override {if(dxCommon)dxCommon->WaitForAllFrames();if(scene_){scene_->Finalize();scene_.reset();}Framework::Finalize();if(messages_)GpuClean();passed_=done_;if(passed_)std::cout<<"PASS: actual EntityScene + static/skinned instanced Directional/Point/Spot/material pixels, material-key isolation, active ancestry/range/cone/removal, scope/capacity and GPU errors0/owned shutdown\n";}
private:std::string error_;
};

void RunAuthoredLightingChecks(){auto checks=std::make_unique<AuthoredLightingChecks>();checks->Run();}
