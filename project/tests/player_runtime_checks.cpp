#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "engine/app/RuntimeGame.h"
#include "application_check_mode.h"
#include "runtime_scene_transition_fixture.h"
#include "animator_host_fixture.h"
#include "runtime_prefab_host_fixture.h"
#include "player_lighting_fixture.h"
#include "player_sprite_fixture.h"
#include "player_particle_fixture.h"
#include "engine/base/AssetPath.h"
#include "DirectXCommon.h"
#include "OffscreenRenderManager.h"
#include "SceneManager.h"
#include "GamePlayScene.h"
#include "engine/scene/entity/EntityScene.h"
#include "CameraManager.h"
#include "Logger.h"
#include "Input.h"
#include "Audio.h"
#include "wave_fixture.h"
#include "engine/base/ProjectContext.h"
#include <Windows.h>
#include <d3d12sdklayers.h>
#include <crtdbg.h>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <cmath>

#ifndef KUBO_RUNTIME_ONLY
#error Runtime checks must compile and link the independent Player target.
#endif
namespace {
void Require(bool condition, const char* why) { if (!condition) throw std::runtime_error(why); }
class SceneTransitionPlayerChecks final : public Engine::App::RuntimeGame {
public:
    explicit SceneTransitionPlayerChecks(bool button=false):button_(button){}
    ~SceneTransitionPlayerChecks() override {Engine::InputSystem::captureInputChecksOverride=nullptr;}
    void Initialize() override {
        fixture_.Prepare(button_);
        SetProjectRoot(fixture_.root);
        SetProjectFile(fixture_.root/"entry.kubo");
        SetProjectAssetRoot(fixture_.root/"Assets");
        std::string error;
        Require(ConfigureStartupScene(fixture_.root/"Assets/entry.scene",error),error.c_str());
        if(button_){
            SetGameOutputExtent({960,720});
            Engine::InputSystem::captureInputChecksOverride=[](Engine::InputSystem::InputFrame& input,bool enabled){
                RuntimeSceneTransitionFixture::ButtonInput(input,enabled,frames_);
                const auto rect=Engine::Base::GamePresentationRect({1280,720},{960,720});
                input.mouseX=RuntimeSceneTransitionFixture::ButtonTestOutside(frames_)?float(rect.left)-1:float(rect.left+rect.width/2);
                input.mouseY=float(rect.top+rect.height/2);
            };
        }
        RuntimeGame::Initialize();
    }
    void Update() override {
        RuntimeGame::Update();
        Require(++frames_<180,"Player Scene transition timed out");
        heard_ |= GetScriptAudioHost().VoiceCount()>0;
        arrived_=fixture_.Arrived(GetWorld());
        if (arrived_ && button_ && !renderedHud_) Require(frames_==24,"Player activated the wrong button or accepted an incomplete click");
        if (arrived_ && !button_) Require(heard_ && GetScriptAudioHost().VoiceCount()==0,
            "Player transition did not submit the WAV or did not retire the previous scene's audio");
    }
    void Draw() override {
        RuntimeGame::Draw();
        if(button_ && frames_==9)Require(fixture_.DrawnText(*dxCommon,*offscreenRenderManager,false),"Player title label did not render");
        if(button_ && arrived_){
            Require(fixture_.DrawnText(*dxCommon,*offscreenRenderManager,true),"Player HUD label did not render");
            renderedHud_=true;
        }
    }
    bool IsEndRequest() const override {return Arrived() || RuntimeGame::IsEndRequest();}
    bool Arrived() const {return arrived_ && (!button_ || renderedHud_);}
private:
    RuntimeSceneTransitionFixture fixture_;
    static inline int frames_=0;
    bool button_=false,renderedHud_=false;
    bool heard_=false,arrived_=false;
};
class AnimatorPlayerChecks final : public Engine::App::RuntimeGame {
public:
    void Initialize() override {
        fixture_.Prepare("Player");SetProjectRoot(fixture_.root);SetProjectFile(fixture_.root/"entry.kubo");SetProjectAssetRoot(fixture_.root/"Assets");std::string error;
        Require(ConfigureStartupScene(fixture_.root/"entry.scene",error),error.c_str());RuntimeGame::Initialize();fixture_.CaptureGpu(dxCommon->GetDevice());
    }
    void Update() override {RuntimeGame::Update();fixture_.Observe(GetWorld(),GetFrameDeltaTime(),GetLuaError(fixture_.fault));++updates_;}
    void Draw() override {RuntimeGame::Draw();++draws_;}
    bool IsEndRequest() const override {return fixture_.stage>=8 || updates_>20 || RuntimeGame::IsEndRequest();}
    void Finalize() override {RuntimeGame::Finalize();retired_=true;}
    void Verify(){Require(fixture_.stage==8 && fixture_.fixedTicks>0 && draws_>=7 && retired_,"Animator Player incomplete fixed/render/retirement");fixture_.VerifyGpu();Require(!GetWorld().Find(fixture_.owner)->script.started && !GetWorld().Find(fixture_.fixed)->script.started,"Animator Player retained Script instance");}
private:
    AnimatorHostFixture fixture_;int updates_=0,draws_=0;bool retired_=false;
};
class PrefabPlayerChecks final : public Engine::App::RuntimeGame {
    RuntimePrefabHostFixture fixture_;int frames_=0,draws_=0;bool retired_=false;
public:
    void Initialize() override{fixture_.Prepare("PrefabPlayer");SetProjectRoot(fixture_.Root());SetProjectFile(fixture_.Root()/"entry.kubo");SetProjectAssetRoot(fixture_.Root()/"Assets");std::string error;Require(ConfigureStartupScene(fixture_.Root()/"entry.scene",error),error.c_str());RuntimeGame::Initialize();fixture_.CaptureGpu(dxCommon->GetDevice());}
    void Update() override{RuntimeGame::Update();++frames_;fixture_.Observe(GetWorld(),GetEntityAudioHost(),GetScriptAudioHost(),GetLuaError(1),GetLuaError(2),GetLuaError(4));}
    void Draw() override{RuntimeGame::Draw();++draws_;}
    bool IsEndRequest() const override{return fixture_.Done() || frames_>40 || RuntimeGame::IsEndRequest();}
    void Finalize() override{RuntimeGame::Finalize();retired_=true;}
    void Verify(){Require(fixture_.Done() && draws_>=15 && retired_,"Prefab Player scenario incomplete");fixture_.VerifyGpu();const auto* record=GetWorld().Find(4);Require(record && !record->script.started && record->transform.rotation.y==0.75f,"Prefab Player Script Destroy did not retire");Require(GetEntityAudioHost().SourceCount()==0 && GetEntityAudioHost().VoiceCount()==0 && GetEntityAudioHost().BufferBytes()==0 && GetScriptAudioHost().OwnerCount()==0 && GetScriptAudioHost().VoiceCount()==0 && GetScriptAudioHost().BufferBytes()==0,"Prefab Player retained Audio owners");std::cout<<"PASS: actual Player Prefab spawn/new Model+Animator+Script Create/Update/Fixed+Physics+muted component/Lua Audio, failed-peer isolation, rendered frames and Destroy/owned cleanup/GPU errors0\n";}
};
class PlayerChecks final : public Engine::App::RuntimeGame {
public:
    bool IsEndRequest() const override { return frames_>=9 || RuntimeGame::IsEndRequest(); }
    void Update() override {
        if(frames_==3 && changeModelCwd_) std::filesystem::current_path(GetProjectRoot()/"UnrelatedAgain");
        RuntimeGame::Update(); ++frames_;
        const auto physics=GetPhysicsAdvanceUsage();physicsTicks_+=physics.steps;
        Require(physics.steps<=Engine::Physics::PhysicsWorld::MaximumFrameSteps,"Actual Player exceeded fixed physics frame budget");
    }
    void CheckRelativeModelOwnership() { changeModelCwd_=true; }
    void CheckMaterialContact(Engine::Scene::EntityId id) {materialBody_=id;}
    void Draw() override { RuntimeGame::Draw(); ++draws_; }
    void Verify(bool generic, Engine::Scene::EntityId mover, Engine::Scene::EntityId body, bool loadFailure) {
        Require(draws_==8, "Player did not render eight frames");
        if (generic) {
            Require(physicsTicks_>0,"Actual Player did not use fixed physics scheduler");
            bool sawFixed=false;
            for(const auto& entity:GetWorld().GetEntities())if(entity.name=="Fixed hook probe") {
                sawFixed=true;Require(entity.script.started && entity.transform.position.y==3 && std::abs(entity.transform.position.x-double(physicsTicks_)*Engine::Physics::PhysicsWorld::FixedStepSeconds)<1e-4,"Actual Player did not dispatch fixed-only Lua once before each physics tick");
            }
            Require(sawFixed,"Actual Player fixed-hook fixture missing");
            const auto& entities=GetWorld().GetEntities();
            const auto sensor=std::find_if(entities.begin(),entities.end(),[](const auto& entity){return entity.name=="Player trigger";});
            Require(sensor!=entities.end() && sensor->script.started && sensor->transform.rotation.z==1,
                "Actual Player did not dispatch one Lua enter for a persistent overlap");
            const auto* scene=dynamic_cast<Engine::Scene::EntityScene*>(Engine::Scene::SceneManager::GetInstance()->GetCurrentScene());
            Require(scene && scene->GetModelRenderStatus(mover).loaded,"Player did not actually load the authored model renderer");
            const auto* scripted=GetWorld().Find(mover); const auto* falling=GetWorld().Find(body);
            Require(scripted && scripted->script.started && scripted->transform.position.x>0,"Player did not execute authored script updates");
            Require(falling && falling->rigidBody.velocity.y<0 && falling->transform.position.y<10,"Player did not simulate authored Physics");
            const auto* bounced=GetWorld().Find(materialBody_);
            Require(bounced && bounced->collider.friction==1 && bounced->collider.restitution==1 &&
                std::abs(bounced->rigidBody.velocity.y-2)<0.001f && std::abs(bounced->rigidBody.velocity.x)<0.001f &&
                !bounced->rigidBody.grounded,"Player did not restore/simulate authored material bounce/friction");
            Matrix4x4 bouncedWorld{};
            Require(GetWorld().GetWorldMatrix(materialBody_,bouncedWorld) && bouncedWorld.m[3][1]>=1.49f &&
                std::abs(bouncedWorld.m[3][0]-1000)<0.2f && bounced->transform.position.x<-0.74f,
                "Player did not simulate parent-composed bounds or inverse-map response");
            size_t physicsDiagnostics=0;
            for(const auto& entry:Engine::Base::Logger::GetEntries()) if(entry.message.starts_with("Player physics: ")) ++physicsDiagnostics;
            Require(physicsDiagnostics==1,"Player repeated or omitted persistent physics diagnostic");
            for(const auto& entity:GetWorld().GetEntities()) {
                if(entity.name=="Inactive physics child") Require(entity.rigidBody.velocity.y==-2 && entity.transform.position.y==0.8f,"Player simulated inactive ancestor child");
                if(entity.name=="Singular physics child") Require(entity.rigidBody.velocity.x==1 && entity.transform.position.x==0,"Player mutated singular-parent child");
            }
            const auto projection=Engine::CameraSystem::CameraManager::GetInstance()->GetActiveCamera()->GetProjectionMatrix();
            Require(std::abs(projection.m[1][1]-0.25f)<0.0001f && std::abs(projection.m[1][1]/projection.m[0][0]-GetGameOutputExtent().Aspect())<0.0001f,"Player did not apply configured output aspect to authored camera");
            const auto color=offscreenRenderManager->GetViewRenderTarget(false)->GetDesc();
            const auto depth=offscreenRenderManager->GetViewDepthTarget(false)->GetDesc();
            Require(GetGameOutputExtent()==Engine::Base::ViewExtent{512,768} && color.Width==512 && color.Height==768 && depth.Width==512 && depth.Height==768,"Player Game color/depth did not use project output extent");
        } else {
            auto* scene=dynamic_cast<Engine::Scene::GamePlayScene*>(Engine::Scene::SceneManager::GetInstance()->GetCurrentScene());
            Require(scene && scene->GetPlayback()==Engine::Scene::GamePlayScene::Playback::Playing,"Debug Player sample started in editor playback state");
        }
        if(loadFailure) {
            size_t diagnostics=0;
            for(const auto& entry:Engine::Base::Logger::GetEntries())
                if(entry.message.starts_with("Player script load failed: ")) ++diagnostics;
            Require(diagnostics==1,"Player repeated or omitted unchanged script-load diagnostic");
            for(const auto& entity:GetWorld().GetEntities())
                if(entity.name=="Broken module") Require(!entity.script.started,"Failed Lua module started an instance");
        }
#ifdef _DEBUG
        Microsoft::WRL::ComPtr<ID3D12InfoQueue> queue;
        Require(SUCCEEDED(dxCommon->GetDevice()->QueryInterface(IID_PPV_ARGS(&queue))),"Debug Player did not expose DX12 validation queue");
        for(UINT64 index=0;index<queue->GetNumStoredMessages();++index) {
            SIZE_T size=0; queue->GetMessage(index,nullptr,&size); std::vector<unsigned char> storage(size);
            auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            if(SUCCEEDED(queue->GetMessage(index,message,&size))) Require(message->Severity!=D3D12_MESSAGE_SEVERITY_ERROR && message->Severity!=D3D12_MESSAGE_SEVERITY_CORRUPTION,message->pDescription);
        }
#endif
    }
private:
    int frames_=0,draws_=0;
    size_t physicsTicks_=0;
    bool changeModelCwd_=false;
    Engine::Scene::EntityId materialBody_=0;
};
}
int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR arguments,int) {
    std::cout<<std::unitbuf; std::cerr<<std::unitbuf;
#ifdef _DEBUG
    _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE); _CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
#endif
    const auto mode=ApplicationCheckMode(arguments);
    if(mode=="--particles"||mode=="--particle-export"){std::cout<<"RUNNER_MODE="<<mode<<'\n';try{RunPlayerParticleChecks(mode=="--particle-export");return 0;}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
    if(mode=="--lights"){std::cout<<"RUNNER_MODE="<<mode<<'\n';try{RunPlayerLightingChecks();return 0;}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
    if(mode=="--ui"){std::cout<<"RUNNER_MODE="<<mode<<'\n';try{RunPlayerSpriteChecks();return 0;}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
    if(mode=="--lua-ui"){std::cout<<"RUNNER_MODE="<<mode<<'\n';try{RunPlayerLuaUIChecks();return 0;}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
    if(mode=="--scene-transition" || mode=="--ui-button"){
        std::cout<<"RUNNER_MODE="<<mode<<'\n';
        try{const bool button=mode=="--ui-button";SceneTransitionPlayerChecks checks(button);checks.Run();Require(checks.Arrived(),"Player ended before the transition");
            std::cout<<(button?"PASS: independent Player 4:3 Japanese title and text-only HUD pixels, Button painter order and cancellation\n":
                "PASS: independent Player movement, Trigger WAV and destination Lua initialization\n");return 0;}
        catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
    }
    if(mode=="--prefab"){std::cout<<"RUNNER_MODE="<<mode<<'\n';try{auto checks=std::make_unique<PrefabPlayerChecks>();checks->Run();checks->Verify();return 0;}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
    if(!mode.empty() && mode!="--animator" && mode!="--generic" && mode!="--lua" && mode!="--lua-failure" && mode!="--lua-input" && mode!="--lua-properties") {std::cerr<<"Unknown Player runner mode\n";return 1;}
    std::cout<<"RUNNER_MODE="<<mode<<'\n';
    if(mode=="--animator") {
        try{AnimatorPlayerChecks checks;checks.Run();checks.Verify();std::cout<<"PASS: actual Player Lua Animator Create/Update/Fixed commands, failed-peer isolation, independent real clocks/render/GPU errors0 and owned shutdown\n";return 0;}
        catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
    }
    const bool loadFailure=mode=="--lua-failure";
    const bool luaInput=mode=="--lua-input";
    const bool luaProperties=mode=="--lua-properties";
    const bool lua=loadFailure || luaInput || luaProperties || mode=="--lua";
    const bool generic=lua || mode=="--generic";
    const auto previous=std::filesystem::current_path();
    const auto fixture=std::filesystem::temp_directory_path()/(L"kubo-player-runtime-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        Require(GetACP()==CP_UTF8,"Player UTF-8 process manifest was not embedded");
        PlayerChecks player; Engine::Scene::EntityId mover=0,body=0,commandTarget=0;std::string savedPrefabLink;
        if(!generic) {
            std::filesystem::create_directories(fixture);player.SetProjectRoot(fixture);std::string error;
            Require(player.ConfigureStartupScene(fixture/L"場面/初回.scene",error),error.c_str());
        }
        if(generic) {
            const auto model=previous/"Resources/models/axis/axis.obj";
            Require(std::filesystem::is_regular_file(model),"Player model fixture missing");
            std::filesystem::create_directories(fixture/"Assets");
            const auto ownedModelDirectory=fixture/L"Assets/モデル";
            std::filesystem::create_directories(ownedModelDirectory);
            std::filesystem::copy(model.parent_path(),ownedModelDirectory,std::filesystem::copy_options::recursive);
            const auto scriptName=lua ? "Assets/動作.lua" : "Assets/動作.kubo";
            { std::ofstream script(fixture/Engine::Base::PathFromUtf8(scriptName)); script<<(lua ? "local frames=0\nfunction on_create(e) assert(audio.available());audio.play('Assets/音声.wav',true,0) end\nfunction on_update(e,dt) frames=frames+1;audio.set_volume('Assets/音声.wav',0); e:translate(2*dt,0,0) end\n" : "on_update translate 2 0 0\n"); Require(bool(script),"Cannot create script fixture"); }
            if(luaInput) {
                std::filesystem::create_directories(fixture/"UserSettings");
                { std::ofstream bindings(fixture/"UserSettings/input_bindings.json");bindings<<R"({"version":2,"actions":{"script_test_action":[{"source":"keyboard","code":17,"contribution":[2,0,0]}],"jump":[{"source":"pad_button","code":10,"contribution":[1,0,0]}],"pad_move":[{"source":"pad_axis","code":0,"threshold":0.5,"contribution":[2,0,0]}]}})";Require(bool(bindings),"Cannot create project input bindings"); }
                std::ofstream script(fixture/Engine::Base::PathFromUtf8(scriptName));
                script<<R"(local cloneid
function on_update(e,dt)
assert(input.keys.W==17 and type(input.available())=='boolean')
assert(scene.find('Model/script')==e:id() and scene.exists(e:id()) and scene.name(e:id())=='Model/script' and scene.parent(e:id())==0 and scene.active(e:id()))
local camera=scene.find('Camera');assert(camera~=nil and scene.entity_count()>1)
local cx,cy,cz=scene.position(camera);assert(cx==0 and cy==0 and cz==-5)
local wx,wy,wz=scene.world_position(camera);local cm=scene.world_matrix(camera);assert(wx==cx and wy==cy and wz==cz and #cm==16 and cm[15]==-5)
assert(type(input.key_down(0))=='boolean' and type(input.key_pressed(255))=='boolean' and type(input.key_released(input.keys.W))=='boolean')
assert(type(input.mouse_down(0))=='boolean' and type(input.mouse_pressed(7))=='boolean' and type(input.mouse_released(0))=='boolean')
assert(not input.action_down('missing') and not input.action_pressed('missing') and not input.action_released('missing'))
local x,y,z=input.action_axis('missing');assert(x==0 and y==0 and z==0)
local ax,ay,az=input.action_axis('script_test_action');assert((ax==0 or ax==2) and ay==0 and az==0)
local mx,my=input.mouse_position();local dx,dy=input.mouse_delta()
assert(type(mx)=='number' and type(my)=='number' and type(dx)=='number' and type(dy)=='number' and type(input.mouse_wheel())=='number')
assert(input.pad_buttons.A==10 and input.pad_buttons.Y==13)
assert(input.action_down('jump')==input.pad_down(input.pad_buttons.A) and input.action_pressed('jump')==input.pad_pressed(input.pad_buttons.A) and input.action_released('jump')==input.pad_released(input.pad_buttons.A))
local px,py,pz=input.action_axis('pad_move');local sx=input.pad_stick();assert(px==2*sx and py==0 and pz==0)
assert(type(input.pad_connected())=='boolean' and type(input.pad_connected_this_frame())=='boolean' and type(input.pad_disconnected_this_frame())=='boolean')
for button=0,13 do assert(type(input.pad_down(button))=='boolean' and type(input.pad_pressed(button))=='boolean' and type(input.pad_released(button))=='boolean') end
for _,right in ipairs({false,true}) do
  local x,y=input.pad_stick(right);local t=input.pad_trigger(right)
  assert(x>=-1 and x<=1 and y>=-1 and y<=1 and t>=0 and t<=1)
  if not input.available() or not input.pad_connected() then assert(x==0 and y==0 and t==0) end
end
e:set_rotation(0,0.125,0);e:translate(2*dt,0,0)
local x,y,z=scene.position(e:id());local own=e:position();assert(x==own)
local wx=scene.world_position(e:id());local matrix=scene.world_matrix(e:id());assert(wx==own and matrix[13]==own)
local target=scene.find('Command target');assert(target and scene.set_position(target,7,8,9) and scene.translate(target,1,0,0))
assert(scene.set_rotation(target,0,0.25,0) and scene.set_scale(target,0,2,-1) and scene.set_active(target,false))
assert(scene.position(target)==8 and not scene.active(target) and scene.world_matrix(target)[13]==8)
assert(scene.velocity(target)==nil and not scene.set_velocity(target,1,0,0))
local born=scene.find('Player spawned')
if not born then born=scene.spawn('Player spawned',e:id());assert(born and scene.set_position(born,2,0,0)) end
assert(scene.parent(born)==e:id() and scene.position(born)==2)
local temporary=scene.spawn('Player temporary');local child=scene.spawn('Player temporary child',temporary)
assert(scene.destroy_subtree(temporary) and not scene.exists(temporary) and not scene.exists(child))
if not cloneid then
 cloneid=scene.clone(assert(scene.find('Clone template')))
 assert(cloneid and scene.set_position(cloneid,30,10,0))
end
assert(scene.exists(cloneid) and scene.parent(cloneid)==0)
if not scene.find('Player Model spawned') then
 local model=scene.spawn_model('Player Model spawned','Assets/モデル/axis.obj')
 assert(model and scene.set_position(model,-2,0,0) and scene.set_scale(model,0.5,0.5,0.5))
end
end
)";
                Require(bool(script),"Cannot create input Lua fixture");
            }
            if(luaProperties) {
                std::ofstream script(fixture/Engine::Base::PathFromUtf8(scriptName));
                script<<R"(properties={speed=2,enabled=true,label="default"}
function on_create(e) assert(e:property('speed')==5 and e:property('label')=="日本語") end
function on_update(e,dt)
assert(e:property('enabled')==true and e:property('unknown')==nil)
e:set_rotation(0,0.25,0);e:translate(e:property('speed')*dt,0,0)
end
function on_destroy(e) assert(e:property('label')=="日本語") end
)";
                Require(bool(script),"Cannot create property Lua fixture");
            }
            Engine::Scene::EntityWorld world; std::string error;
            {std::ofstream fixed(fixture/"Assets/fixed.lua");fixed<<"function on_create(e) e:translate(0,3,0) end\nfunction on_fixed_update(e,dt) e:translate(dt,0,0) end\n";Require(bool(fixed),"Cannot create fixed-only Player fixture");}
            const auto fixedProbe=world.CreateEntity("Fixed hook probe");world.SetScript(fixedProbe,"Assets/fixed.lua");
            {std::ofstream trigger(fixture/"Assets/trigger.lua");trigger<<
                "local n=0\nfunction on_trigger_enter(e,id) assert(id==scene.find('Player trigger visitor')); n=n+1; e:set_rotation(0,0,n) end\n";
                Require(bool(trigger),"Cannot create Player trigger-only Lua fixture");}
            const auto sensor=world.CreateEntity("Player trigger"),visitor=world.CreateEntity("Player trigger visitor");
            world.Find(sensor)->transform.position={0,100,0};
            world.Find(visitor)->transform.position={0,100,0};
            Require(world.SetCollider(sensor,{{1,1,1},true}) && world.SetCollider(visitor,{{1,1,1},false}) &&
                world.SetScript(sensor,"Assets/trigger.lua"),"Cannot author Player trigger");
            const auto camera=world.CreateEntity("Camera"); world.SetCamera(camera,{true,0.8f,0.1f,100,true,8}); world.Find(camera)->transform.position={0,0,-5};
            mover=world.CreateEntity("Model/script"); world.SetModel(mover,"Assets/モデル/axis.obj"); world.SetScript(mover,scriptName);
            Engine::AudioSystem::AudioComponent authoredAudio;authoredAudio.assetPath="Assets/音声.wav";authoredAudio.playOnAwake=true;authoredAudio.loop=true;authoredAudio.volume=0;
            Require(world.SetAudio(mover,authoredAudio),"Cannot author Player Audio component");
            if(luaProperties) Require(world.SetScriptProperty(mover,"speed",5.0) && world.SetScriptProperty(mover,"enabled",true) && world.SetScriptProperty(mover,"label",std::string("日本語")),"Cannot author Player property overrides");
            body=world.CreateEntity("Falling"); world.SetCollider(body,{}); world.SetRigidBody(body,{}); world.Find(body)->transform.position.y=10;
            const auto materialFloor=world.CreateEntity("Material floor"),materialGroup=world.CreateEntity("Material group"),materialBody=world.CreateEntity("Material bounce",materialGroup);
            world.Find(materialFloor)->transform.position={1000,0,0};
            world.Find(materialGroup)->transform={{1000,0,0},{0,0,1.57079632679f},{-2,3,1}};
            world.Find(materialBody)->transform.position={-0.4f,0,0};
            Require(world.SetCollider(materialFloor,{{5,0.5f,5},false,1,1}) &&
                world.SetCollider(materialBody,{{0.5f,0.5f,0.5f},false,1,1}) &&
                world.SetRigidBody(materialBody,{{1,-2,0},1,true,false,false}),"Cannot author material contact");
            player.CheckMaterialContact(materialBody);
            const auto inactiveGroup=world.CreateEntity("Inactive physics group"),inactiveBody=world.CreateEntity("Inactive physics child",inactiveGroup);
            world.Find(inactiveGroup)->active=false;world.Find(inactiveGroup)->transform.position={1000,0,0};world.Find(inactiveBody)->transform.position.y=0.8f;
            Require(world.SetCollider(inactiveBody,{}) && world.SetRigidBody(inactiveBody,{{0,-2,0},1,true,true,false}),"Cannot author inactive hierarchy");
            const auto singularGroup=world.CreateEntity("Singular physics group"),singularBody=world.CreateEntity("Singular physics child",singularGroup);
            world.Find(singularGroup)->transform.scale={0,1,1};
            Require(world.SetCollider(singularBody,{}) && world.SetRigidBody(singularBody,{{1,0,0},1,true,false,false}),"Cannot author singular hierarchy");
            if(loadFailure) {
                { std::ofstream script(fixture/"Assets/broken.lua");script<<"while true do end\n";Require(bool(script),"Cannot create broken module fixture"); }
                for(int index=0;index<2;++index) { const auto broken=world.CreateEntity("Broken module");world.SetScript(broken,"Assets/broken.lua"); }
            }
            if(luaInput){commandTarget=world.CreateEntity("Command target");
                {std::ofstream script(fixture/"Assets/clone-template.lua");script<<"local n=0;function on_create(e) e:translate(1,0,0) end;function on_update(e,dt) n=n+1;e:set_rotation(0,n*0.01,0) end";Require(bool(script),"Cannot create Player clone Script");}
                const auto templateId=world.CreateEntity("Clone template");world.Find(templateId)->transform.position={20,10,0};
                Require(world.SetModel(templateId,"Assets/モデル/axis.obj") && world.SetScript(templateId,"Assets/clone-template.lua") && world.SetAudio(templateId,authoredAudio) && world.SetCollider(templateId,{}) && world.SetRigidBody(templateId,{}) && world.SetScriptProperty(templateId,"label",std::string("日本語")),"Cannot author Player clone components");
            }
            const auto savedStartup=fixture/L"Assets/場面/開始.scene";
            Engine::Scene::EntityWorld prefabBase;prefabBase.GetEntities().push_back(*world.Find(mover));
            auto& link=world.Find(mover)->prefabLink;
            link["sourceGuid"]="0123456789abcdef0123456789abcdef";link["rootLocal"]=double(mover);link["base"]=prefabBase.Capture();
            Engine::Scene::SceneValue pair;pair["local"]=double(mover);pair["entity"]=double(mover);link["mapping"]=crude_json::array{pair};savedPrefabLink=link.dump();
            Require(world.Save(savedStartup,error),error.c_str());
            {std::ofstream scene(savedStartup,std::ios::binary);scene<<"\xef\xbb\xbf"<<world.Capture().dump();Require(bool(scene),"Cannot create BOM Player Scene");}
            {std::ofstream file(fixture/"Game.kubo");file<<R"({"name":"Runtime output","startupScene":"not-selected.scene","assetDirectory":"Assets"})";Require(bool(file),"Cannot create output manifest");}
            Engine::Base::ProjectContext project;Require(project.Open(fixture/"Game.kubo",error),error.c_str());
            Require(project.SaveLaunchSettings({512,768},"Assets/場面/開始.scene",error),error.c_str());
            Require(project.Open(fixture/"Game.kubo",error) && project.ResolvePath(project.GetSettings().startupScene)==savedStartup,"Saved startup Scene did not survive manifest reopening");
            player.SetGameOutputExtent(project.GetSettings().gameOutput);
            player.SetProjectAssetRoot(fixture/"Assets");
            player.SetProjectRoot(fixture);
            Require(player.ConfigureStartupScene(project.ResolvePath(project.GetSettings().startupScene),error),error.c_str());
            {std::ofstream invalid(fixture/"invalid.scene",std::ios::binary);invalid<<"{\"text\":\"\xff\"}";Require(bool(invalid),"Cannot create invalid Player Scene");}
            Require(!player.ConfigureStartupScene(fixture/"invalid.scene",error),"Player accepted malformed UTF-8 Scene");
            std::filesystem::create_directories(fixture/"Unrelated");
            std::filesystem::create_directories(fixture/"UnrelatedAgain");
            std::filesystem::current_path(fixture/"Unrelated");
            player.CheckRelativeModelOwnership();
            const auto sound=fixture/L"Assets/音声.wav";
            {std::ofstream file(sound,std::ios::binary);const auto bytes=WaveFixture::Pcm(321);file.write(bytes.data(),bytes.size());Require(bool(file),"Cannot create owned Wave");}
            std::filesystem::create_directories(fixture/"Unrelated/Assets");
            {std::ofstream file(fixture/L"Unrelated/Assets/音声.wav",std::ios::binary);file<<"invalid cwd competitor";}
        }
        std::cout<<"PLAYER_STAGE=initialize_begin tick="<<GetTickCount64()<<'\n';
        player.Initialize();
        std::cout<<"PLAYER_STAGE=initialize_end tick="<<GetTickCount64()<<'\n';
        if(generic) {
            bool denied=false;try {player.SetGameOutputExtent({640,480});} catch(const std::logic_error&) {denied=true;}
            Require(denied && player.GetGameOutputExtent()==Engine::Base::ViewExtent{512,768},"Live output setter desynchronized allocated Game targets");
        }
        if(generic) {
            auto* audio=Engine::AudioSystem::Audio::GetInstance();
            auto sound=audio->SoundLoadWave("Assets/音声.wav");std::string error;
            Require(sound.buffer.size()==4 && sound.buffer[0]==65 && sound.buffer[1]==1,"Player Wave was not loaded from owning root");
            const auto baseline=sound.buffer;
            Require(!audio->TrySoundLoadWave("Assets/missing.wav",sound,error) && sound.buffer==baseline,"Missing audio overwrote existing buffer");
            bool caught=false;try {audio->SoundLoadWave("Assets/missing.wav");} catch(const std::runtime_error&) {caught=true;}
            Require(caught,"Legacy Wave loading did not report recoverable failure");
            audio->SoundPlayWave(sound,true,0.0f);
            Require(audio->IsSoundPlaying(&sound),"Actual XAudio2 source voice did not queue loop buffer");
            const bool replaced=audio->TrySoundLoadWave(fixture/L"Assets/音声.wav",sound,error);
            audio->StopSpecificAudio(&sound);
            Require(!replaced && sound.buffer==baseline && !audio->IsSoundPlaying(&sound),"Live audio buffer was replaced or source voice did not stop");
            Require(audio->TrySoundLoadWave(fixture/L"Assets/音声.wav",sound,error),error.c_str());
            audio->SoundUnload(&sound);Require(sound.buffer.empty() && sound.bufferSize==0,"Audio unload retained buffer");
            std::cout<<"PASS: actual Player project-owned Unicode Wave load, nonfatal failure, muted XAudio2 loop/stop, active-buffer replacement guard and unload\n";
        }
        if(!generic) {
            // SceneManager installs its requested scene during the first update.
            std::cout<<"PLAYER_STAGE=first_update_begin tick="<<GetTickCount64()<<'\n';
            player.Update();
            std::cout<<"PLAYER_STAGE=first_update_end tick="<<GetTickCount64()<<'\n';
            auto* sample=dynamic_cast<Engine::Scene::GamePlayScene*>(Engine::Scene::SceneManager::GetInstance()->GetCurrentScene());
            Require(sample && sample->GetScenePath()==fixture/L"場面/初回.scene","Player discarded missing authored startup Scene path");
            std::cout<<"PLAYER_STAGE=first_draw_begin tick="<<GetTickCount64()<<'\n';
            player.Draw();
            std::cout<<"PLAYER_STAGE=first_draw_end tick="<<GetTickCount64()<<'\n';
        }
        if(luaInput) {
            auto* input=Engine::InputSystem::Input::GetInstance();std::string error;
            Require(input->GetActionBindingsPath()==fixture/"UserSettings/input_bindings.json","Player bindings were not anchored to the owning root");
            Require(input->GetActionMap().BindingCount("script_test_action")==1,"Player did not load project action bindings from unrelated cwd");
            std::filesystem::create_directories(fixture/"Unrelated");std::filesystem::current_path(fixture/"Unrelated");
            Require(input->SaveActionBindings(input->GetActionBindingsPath(),error) && input->LoadActionBindings(input->GetActionBindingsPath(),error),error.c_str());
            Require(!std::filesystem::exists(fixture/"Unrelated/UserSettings"),"Input save followed changed cwd");
        }
        while(true) { player.Update(); if(player.IsEndRequest()) break; player.Draw(); }
        player.Verify(generic,mover,body,loadFailure);
        if(generic) Require(player.GetWorld().Find(mover)->prefabLink.dump()==savedPrefabLink,"Runtime changed/lost authored Prefab link while executing baked components");
        if(generic) Require(player.GetEntityAudioHost().SourceCount()==(luaInput?3:1) && player.GetEntityAudioHost().VoiceCount()==(luaInput?3:1) && player.GetEntityAudioHost().BufferBytes()==(luaInput?12:4) && player.GetEntityAudioHost().QueuedVoicesHaveVolume(mover,0),"Actual Player did not restore/autoplay authored Audio component");
        if(luaInput){size_t copies=0;const auto* scene=dynamic_cast<Engine::Scene::EntityScene*>(Engine::Scene::SceneManager::GetInstance()->GetCurrentScene());
            for(const auto& entity:player.GetWorld().GetEntities())if(entity.name=="Clone template"){
                ++copies;Require(entity.hasModel && entity.hasScript && entity.script.started && entity.hasAudio && entity.hasCollider && entity.hasRigidBody && entity.rigidBody.velocity.y<0 && entity.transform.position.y<10 && entity.transform.rotation.y>0 && std::get<std::string>(entity.script.properties.at("label"))=="日本語" && entity.prefabLink.is_null(),"Player clone lost components/fresh Script/Physics");
                Require(scene && scene->GetModelRenderStatus(entity.id).loaded && player.GetEntityAudioHost().QueuedVoicesHaveVolume(entity.id,0),"Player clone did not synchronize model/muted Audio voice");
                Require(entity.transform.position.x==21 || entity.transform.position.x==31,"Player clone did not receive fresh Create");
            }Require(copies==2,"Player clone multiplied or missing");
            size_t models=0;for(const auto& entity:player.GetWorld().GetEntities())if(entity.name=="Player Model spawned"){
                ++models;Require(entity.hasModel && entity.model.assetPath=="Assets/モデル/axis.obj" && entity.parent==0 && entity.transform.position.x==-2 && entity.transform.scale.x==0.5f && scene->GetModelRenderStatus(entity.id).loaded,"Player Model generation did not publish/load authored relative model");
            }Require(models==1,"Player Model generation missing/multiplied");
        }
        if(lua && !luaInput && !luaProperties) Require(player.GetScriptAudioHost().OwnerCount()==1 && player.GetScriptAudioHost().VoiceCount()==1 && player.GetScriptAudioHost().BufferBytes()==4,"Actual Player Lua hook did not submit one owned muted loop");
        if(lua && !luaInput && !luaProperties) Require(player.GetScriptAudioHost().QueuedVoicesHaveVolume(mover,"Assets/音声.wav",0),"Player Lua volume update did not preserve muted voice");
        std::cout<<"PLAYER_STAGE=finalize_begin tick="<<GetTickCount64()<<'\n';
        player.Finalize();
        std::cout<<"PLAYER_STAGE=finalize_end tick="<<GetTickCount64()<<'\n';
        Require(player.GetScriptAudioHost().OwnerCount()==0 && player.GetScriptAudioHost().VoiceCount()==0 && player.GetScriptAudioHost().BufferBytes()==0,"Player Finalize retained Script audio resources");
        Require(player.GetEntityAudioHost().SourceCount()==0 && player.GetEntityAudioHost().VoiceCount()==0 && player.GetEntityAudioHost().BufferBytes()==0,"Player Finalize retained Audio component resources");
        if(luaInput) Require(player.GetWorld().Find(mover)->transform.rotation.y==0.125f,"Player did not execute Lua input getters");
        if(luaInput){const auto* target=player.GetWorld().Find(commandTarget);Require(target && target->transform.position.x==8 && target->transform.position.y==8 && target->transform.rotation.y==0.25f && target->transform.scale.x==0 && target->transform.scale.z==-1 && !target->active,"Player did not publish Lua peer Scene commands");}
        if(luaInput){size_t spawned=0;for(const auto& record:player.GetWorld().GetEntities()){
            Require(record.name!="Player temporary" && record.name!="Player temporary child","Player retained Lua-deleted subtree");
            if(record.name=="Player spawned"){++spawned;Require(record.parent==mover && record.transform.position.x==2,"Player spawned Entity lost parent/local transform");}
        }Require(spawned==1,"Player did not retain exactly one Lua-spawned Entity");}
        if(luaProperties) Require(player.GetWorld().Find(mover)->transform.rotation.y==0.25f && std::get<double>(player.GetWorld().Find(mover)->script.properties.at("speed"))==5,"Player did not restore/use authored property overrides");
        if(generic) Require(!player.GetWorld().Find(mover)->script.started,"Player shutdown retained script instance");
        if(generic) std::cout<<"PASS: actual Player project-relative Unicode model loads from unrelated cwd and remains rendered after another cwd change\n";
        if(generic) std::cout<<"PASS: actual rendered Player preserves Scene-restored Prefab source/base/identity map while executing baked Model/Lua/Audio components without a source Prefab file\n";
        if(generic) std::cout<<"PASS: actual rendered Player Scene-restored Unicode Audio component autoplays muted loop and Finalize releases owned voice/buffer\n";
        if(generic) std::cout<<"PASS: manifest-configured portrait Game color/depth resources and camera aspect during actual Player rendering\n";
        if(generic) std::cout<<"PASS: actual rendered Player Scene-restored collider material friction/bounce and non-grounded outgoing body\n";
        if(generic) std::cout<<"PASS: actual Player parent-composed physics, mirrored/scaled inverse response, inactive child exclusion and deduplicated singular-frame diagnostic\n";
        std::filesystem::current_path(previous); std::error_code cleanup; std::filesystem::remove_all(fixture,cleanup);
        std::cout<<"PASS: independent Player eight rendered frames, "<<(generic?"authored model/orthographic camera, actual script/Physics updates and script shutdown":"legacy sample automatically Playing")<<", DX12 validation and normal cleanup\n";
        if(lua) std::cout<<"PASS: actual independent Player loads and executes Lua asset hooks\n";
        if(lua && !luaInput && !luaProperties) std::cout<<"PASS: actual rendered Player Lua hook plays project-owned Unicode muted XAudio2 loop and Finalize releases voices/buffers\n";
        if(loadFailure) std::cout<<"PASS: failed Lua module beside healthy script/Physics, bounded loading and one repeated-error log\n";
        if(luaInput) std::cout<<"PASS: actual Player host snapshot and Lua keyboard/mouse/action getters during rendered updates (not physical-input gesture QA)\n";
        if(luaInput) std::cout<<"PASS: explicit Player root loads input settings from unrelated cwd, saves/reloads after cwd change, and resolves script hooks during rendering\n";
        if(luaInput) std::cout<<"PASS: actual Player Lua component clone loads model renderer, starts fresh Script, simulates Collider/RigidBody and owns separate muted Audio voice/buffer; Finalize releases voices/buffers\n";
        if(luaInput) std::cout<<"PASS: actual Player Lua Model-path generation resolves owning Unicode project asset, publishes candidate TRS and loads model renderer across cwd changes\n";
        if(luaProperties) std::cout<<"PASS: actual Player Scene-restored typed property overrides, Lua lifecycle getters, Unicode label and rendered movement\n";
        return 0;
    } catch(const std::exception& error) {
        std::filesystem::current_path(previous); std::error_code cleanup; std::filesystem::remove_all(fixture,cleanup);
        std::cerr<<"FAIL: "<<error.what()<<'\n'; return 1;
    }
}
