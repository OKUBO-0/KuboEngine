#pragma once
// Optional application-runner fixture; never linked into normal applications.
#include "engine/scene/entity/EntityScene.h"
#include "SceneManager.h"
#include "ModelManager.h"
#include "Model.h"
#include "Object3D.h"
#include "Object3DCommon.h"
#include "engine/animation/AnimationPose.h"
#include "layered_object_fixture.h"
#include "engine/base/AssetPath.h"
#include "engine/assets/AnimatorControllerAsset.h"
#include <Windows.h>
#include <d3d12sdklayers.h>
#include <fstream>
#include <cmath>
#include <iostream>

class AnimatorHostFixture final {
public:
    static void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
    ~AnimatorHostFixture(){
        std::error_code error;
        if(!ownedRoot.empty() && std::filesystem::canonical(ownedRoot,error)==ownedRoot && !error)
            std::filesystem::remove_all(ownedRoot,error);
    }
    void Prepare(const char* role) {
        const auto resource=std::filesystem::current_path()/"Resources/models/walk";
        root=std::filesystem::canonical(std::filesystem::temp_directory_path())/
            (L"kubo-animator-host-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        Check(std::filesystem::create_directory(root),"Refuse existing Animator fixture");
        ownedRoot=root;
        Microsoft::WRL::ComPtr<ID3D12Debug1> debug;
        Check(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))),"Animator GPU debug layer unavailable");
        debug->EnableDebugLayer();
        debug->SetEnableGPUBasedValidation(TRUE);
        std::filesystem::create_directories(root/"Assets/models");
        std::filesystem::copy(resource,root/"Assets/models",std::filesystem::copy_options::recursive);
        {std::ofstream out(root/"entry.kubo");out<<"{\"name\":\""<<role<<" Animator\",\"startupScene\":\"entry.scene\"}";Check(bool(out),"Animator manifest write failed");}
        {std::ofstream out(root/"Another.kubo");out<<R"({"name":"Not selected","assetDirectory":"OtherAssets","startupScene":"other.scene"})";Check(bool(out),"Alternative manifest write failed");}
        {std::ofstream out(root/"Assets/healthy.lua");out<<R"(local n=0
local function state(e)
 local r,p,t=scene.animator_state(e:id())
 assert(type(r)=='boolean' and type(p)=='boolean')
 if r then assert(type(t)=='number' and t>=0) else assert(not p and t==nil) end
 return r,p,t
end
function on_create(e)
 local r=state(e);assert(not r);assert(scene.animator_restart(e:id()))
 local c=assert(scene.find('Scene Controller owner'))
 assert(scene.controller_set_float(c,'speed',1));assert(scene.controller_set_bool(c,'enabled',true));assert(scene.controller_trigger(c,'go'))
 assert(scene.controller_set_layer_weight(c,1,0.75))
end
function on_update(e,dt)
 local r,p,t=state(e)
 n=n+1
 local owner=assert(scene.find('Scene Controller owner'))
 local weight=scene.controller_layer_weight(owner,1)
 if n>=2 then assert(weight~=nil,'Actual layer reader was unavailable') end
 if weight~=nil then
  assert(scene.controller_state(owner,1)==(n<=2 and 'Idle' or 'Cutscene') and scene.controller_state(owner,3)==nil)
  assert(weight==(n<=2 and 0.75 or 0.5),'Layer weight or failed-peer isolation lost')
  local time=scene.controller_time(owner,1);local progress=scene.controller_progress(owner,1);local normalized=scene.controller_normalized_time(owner,1)
  assert(time>=0 and progress>=0 and normalized>=0 and normalized<=1)
  if n==2 then assert(scene.controller_set_layer_weight(owner,1,0.5));assert(scene.controller_play_state(owner,'Cutscene',0,1));assert(scene.controller_state(owner,1)=='Idle','Pending force fabricated actual state');assert(scene.controller_layer_weight(owner,1)==0.75,'Pending layer weight fabricated actual state') end
 end
 local commands={false,'animator_pause','animator_stop','animator_play','animator_pause','animator_play','animator_play','animator_restart'}
 if n~=1 then assert(scene[commands[n] or 'animator_pause'](e:id())) end
 local a,b,c=state(e);assert(a==r and b==p and c==t)
 e:set_position(t or -1,p and 1 or 0,r and 1 or 0)
 e:set_rotation(0,n,0)
end
function on_destroy(e) local r=state(e);assert(r);assert(scene.animator_stop(e:id())) end
)";Check(bool(out),"Healthy Animator script write failed");}
        {std::ofstream out(root/"Assets/fault.lua");out<<R"(function on_update(e,dt)
 assert(scene.animator_stop(assert(scene.find('Animator owner'))))
 local c=assert(scene.find('Scene Controller owner'));assert(scene.controller_set_float(c,'speed',0));assert(scene.controller_set_bool(c,'enabled',false))
 assert(scene.controller_set_layer_weight(c,1,0))
 assert(scene.controller_play_state(c,'Cutscene',0,1))
 e:translate(99,0,0)
 error('animator-peer-discard')
end
)";Check(bool(out),"Failed Animator script write failed");}
        {std::ofstream out(root/"Assets/fixed.lua");out<<"function on_fixed_update(e,dt) local r,p,t=scene.animator_state(e:id());if r then assert(type(t)=='number') else assert(not p and t==nil) end;assert(scene.animator_restart(e:id()));e:set_rotation(0,t or -1,r and 1 or 0);e:translate(1,0,0) end\nfunction on_destroy(e) local r=scene.animator_state(e:id());assert(r);assert(scene.animator_stop(e:id())) end\n";Check(bool(out),"Fixed Animator script write failed");}
        const std::string sceneControllerText=R"({"format":"kubo-animator-controller-1","parameters":[{"name":"speed","type":"float","default":0},{"name":"enabled","type":"bool","default":false},{"name":"go","type":"trigger","default":false}],"layers":[{"name":"Base","mode":"override","weight":1,"mask":[],"entry":"Idle","states":[{"name":"Idle","clip":"default","speed":1,"loop":true},{"name":"Run","clip":"default","speed":2,"loop":true}],"transitions":[{"from":"Idle","to":"Run","blendSeconds":0.05,"exitTime":null,"reenter":false,"conditions":[{"parameter":"speed","op":"gt","value":0.5},{"parameter":"enabled","op":"eq","value":true},{"parameter":"go","op":"set","value":true}]}]}]})";
        auto sceneController=crude_json::value::parse(sceneControllerText);
        auto overlay=sceneController["layers"][0];overlay["name"]="Rendered overlay";overlay["weight"]=0.25;
        overlay["states"]=crude_json::array{overlay["states"][0]};overlay["states"][0]["speed"]=0.5;overlay["transitions"]=crude_json::array{};
        auto cutscene=overlay["states"][0];cutscene["name"]="Cutscene";overlay["states"].push_back(cutscene);
        sceneController["layers"].push_back(overlay);
        const auto layeredSceneText=sceneController.dump();
        Engine::AnimationSystem::Controller::Graph checkedController;std::string fixtureControllerError;
        Check(Engine::AnimationSystem::Controller::TextCodec::Decode(layeredSceneText,checkedController,fixtureControllerError),fixtureControllerError.c_str());
        {std::ofstream out(root/"Assets/scene-controller.animfsm");out<<layeredSceneText;Check(bool(out),"Scene Controller write failed");}
        Engine::Assets::AssetRegistry sceneControllerRegistry;std::string controllerScanError;
        Check(sceneControllerRegistry.Scan(root/"Assets",controllerScanError),controllerScanError.c_str());
        Engine::Scene::EntityWorld world;
        owner=world.CreateEntity("Animator owner");fault=world.CreateEntity("Animator fault");
        Engine::AnimationSystem::AnimatorComponent settings;settings.speed=0.5f;settings.loop=false;settings.playOnAwake=false;
        Check(world.SetModel(owner,"Assets/models/walk.gltf") && world.SetAnimator(owner,settings) && world.SetScript(owner,"Assets/healthy.lua") && world.SetScript(fault,"Assets/fault.lua"),"Animator host authored fixture failed");
        fixed=world.CreateEntity("Fixed Animator owner");Check(world.SetModel(fixed,"Assets/models/walk.gltf") && world.SetAnimator(fixed,settings) && world.SetScript(fixed,"Assets/fixed.lua"),"Fixed Animator host fixture failed");
        controllerEntity=world.CreateEntity("Scene Controller owner");auto controllerSettings=settings;
        controllerSettings.clip="not-a-real-legacy-clip";controllerSettings.controllerGuid=sceneControllerRegistry.FindByPath(root/"Assets/scene-controller.animfsm")->guid;
        Check(world.SetModel(controllerEntity,"Assets/models/walk.gltf") && world.SetAnimator(controllerEntity,controllerSettings),"Scene Controller authored assignment failed");
        std::string error;Check(world.Save(root/"entry.scene",error),error.c_str());baseline=world.Capture().dump();
        std::cout<<"ANIMATOR_HOST_FIXTURE="<<Engine::Base::PathToUtf8(root)<<'\n';
    }
    void CaptureGpu(ID3D12Device* device){Check(SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&messages))),"Animator host GPU queue unavailable");}
    void Observe(const Engine::Scene::EntityWorld& world,float delta,const std::string& error);
    void VerifyGpu() const {
        Check(messages!=nullptr,"Animator GPU observer missing");
        for(UINT64 i=0;i<messages->GetNumStoredMessages();++i){SIZE_T bytes=0;Check(SUCCEEDED(messages->GetMessage(i,nullptr,&bytes)),"Animator GPU size failed");std::vector<unsigned char> storage(bytes);auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());Check(SUCCEEDED(messages->GetMessage(i,message,&bytes)),"Animator GPU read failed");Check(message->Severity!=D3D12_MESSAGE_SEVERITY_ERROR && message->Severity!=D3D12_MESSAGE_SEVERITY_CORRUPTION,"Animator host GPU error");}
    }
    std::filesystem::path root;Engine::Scene::EntityId owner=0,fault=0,fixed=0,controllerEntity=0;std::string baseline;int stage=0;float time=0,fixedTicks=0,fixedTime=0,controllerTime=0;
private:
    std::filesystem::path ownedRoot;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> messages;
    bool lastPlaying=false;
    bool blendChecked=false;
};
