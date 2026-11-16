#include "Framework.h"
#include "WinApp.h"
#include "DirectXCommon.h"
#include "EntityScene.h"
#include "ModelManager.h"
#include "Model.h"
#include "AssetPath.h"
#include "LuaScript.h"
#include "AnimatorControllerAsset.h"
#include <fstream>
#include <d3d12sdklayers.h>
#include <Windows.h>
#include <iostream>
#include <vector>
#include <cmath>
static void Require(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
// Consumer fixture owns retained batches on the heap, rather than nesting many
// large fixed Pending values in Initialize + Controller verification frames.
__declspec(noinline) static std::unique_ptr<Engine::Scripting::ScriptAnimatorRequests::Pending> TakeHeap(Engine::Scripting::ScriptAnimatorRequests& requests) {
    auto result=std::make_unique<Engine::Scripting::ScriptAnimatorRequests::Pending>();
    *result=requests.Take();return result;
}
class Probe final : public Engine::Base::Framework {
    Engine::Scene::EntityWorld world_;
    Engine::Assets::AssetRegistry assets_;
    std::unique_ptr<Engine::Scene::EntityScene> scene_;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> messages_;
    bool checked_ = false, retired_ = false;
    __declspec(noinline) void VerifyController() {
        using namespace Engine;
        const auto root=GetProjectRoot(),file=root/"Resources/sdk-scene-controller.animfsm";
        const auto write=[&](const std::filesystem::path& path,const std::string& text){std::ofstream out(path,std::ios::binary);out<<text;out.close();Require(!out.fail(),"SDK Scene Controller write failed");};
        write(root/"ControllerScene.kubo",R"({"name":"SDK Scene Controller","assetDirectory":"Resources","startupScene":"entry.scene"})");
        const std::string guid(32,'e');
        write(file,R"({"format":"kubo-animator-controller-1","parameters":[{"name":"speed","type":"float","default":0},{"name":"enabled","type":"bool","default":false},{"name":"go","type":"trigger","default":false}],"layers":[{"name":"Base","mode":"override","weight":1,"mask":[],"entry":"Idle","states":[{"name":"Idle","clip":"default","speed":1,"loop":true},{"name":"Run","clip":"default","speed":2,"loop":true}],"transitions":[{"from":"Idle","to":"Run","blendSeconds":0.1,"exitTime":null,"reenter":false,"conditions":[{"parameter":"speed","op":"gt","value":0.5},{"parameter":"enabled","op":"eq","value":true},{"parameter":"go","op":"set","value":true}]}]}]})");
        write(Assets::AssetRegistry::MetadataPath(file),"{\"version\":1,\"guid\":\""+guid+"\"}");std::string error;
        std::ifstream layeredSource(file,std::ios::binary);
        const std::string layeredText((std::istreambuf_iterator<char>(layeredSource)),std::istreambuf_iterator<char>());
        layeredSource.close();auto layeredGraph=crude_json::value::parse(layeredText);
        auto overlay=layeredGraph["layers"][0];overlay["name"]="SDK overlay";overlay["weight"]=0.25;
        overlay["states"]=crude_json::array{overlay["states"][0]};overlay["states"][0]["speed"]=0.5;
        overlay["transitions"]=crude_json::array{};
        auto cutscene=overlay["states"][0];cutscene["name"]="Cutscene";overlay["states"].get<crude_json::array>().push_back(cutscene);
        layeredGraph["layers"].get<crude_json::array>().push_back(std::move(overlay));write(file,layeredGraph.dump());
        Require(assets_.Scan(root/"Resources",error,Assets::AssetRegistry::MetadataPolicy::ReadOnly),error.c_str());
        const auto id=world_.CreateEntity("SDK Controller owner");AnimationSystem::AnimatorComponent cfg;cfg.controllerGuid=guid;cfg.clip="not-a-real-legacy-clip";cfg.speed=0.5f;cfg.playOnAwake=false;
        Require(world_.SetModel(id,"Resources/models/walk/walk.gltf") && world_.SetAnimator(id,cfg),"SDK Scene Controller assignment failed");scene_->SynchronizeModels(0);
        Require(scene_->GetAnimatorRenderStatus(id).ready && scene_->GetControllerState(id)=="Idle","SDK Scene GUID admission failed");
        // Keep the legacy four-argument admission above, then exercise the new
        // selected-file overload against an actually ambiguous directory.
        write(root/"OtherScene.kubo",R"({"name":"Other SDK Scene","assetDirectory":"OtherResources","startupScene":"other.scene"})");
        const auto ambiguous=world_.CreateEntity("Legacy ambiguous Controller owner");
        Require(world_.SetModel(ambiguous,"Resources/models/walk/walk.gltf") && world_.SetAnimator(ambiguous,cfg),"SDK ambiguous Controller assignment failed");
        scene_->SynchronizeModels(0);
        Require(!scene_->GetAnimatorRenderStatus(ambiguous).ready && scene_->GetControllerState(ambiguous).empty(),"SDK legacy ambiguous discovery was silently accepted");
        scene_.reset();
        scene_=std::make_unique<Scene::EntityScene>(world_,*dxCommon,assets_,root,root/"ControllerScene.kubo");
        scene_->Initialize();scene_->SynchronizeModels(0);
        Require(scene_->GetAnimatorRenderStatus(id).ready && scene_->GetControllerState(id)=="Idle" && scene_->GetAnimatorRenderStatus(ambiguous).ready,"SDK explicit manifest did not admit selected Controller owners");
        auto view=std::make_shared<Scripting::ScriptSceneView>();auto requests=std::make_shared<Scripting::ScriptAnimatorRequests>();requests->SetEnabled(true);Scripting::ScriptProperties defaults;
        const auto hook=[&](const char* text){auto factory=Scripting::CreateLuaScriptFactory(text,"SDK Controller",{},defaults,{},{},view,requests);auto callback=factory();Scripting::ScriptSceneView::WriteScope scope(*view,world_,nullptr,scene_.get());callback.onUpdate(*world_.Find(id),0);};
        const auto before=world_.Capture().dump();
        hook("function on_update(e,dt) assert(scene.controller_state(e:id())=='Idle');assert(scene.controller_state(e:id(),1)=='Idle');assert(scene.controller_state(e:id(),3)==nil);assert(scene.controller_layer_weight(e:id(),1)==0.25);assert(scene.controller_set_layer_weight(e:id(),1,0.75));assert(scene.controller_layer_weight(e:id(),1)==0.25);assert(scene.controller_set_float(e:id(),'speed',1));assert(scene.controller_set_bool(e:id(),'enabled',true));assert(scene.controller_trigger(e:id(),'go'));assert(scene.animator_play(e:id()));assert(scene.controller_state(e:id())=='Idle') end");
        const auto queued=TakeHeap(*requests);const auto usage=scene_->ApplyAnimatorRequests(*queued);Require(queued->count==5 && usage.applied==5 && usage.rejected==0,"SDK typed FIFO application failed");
        scene_->SynchronizeModels(0.01f);Require(scene_->GetControllerState(id)=="Run" && std::abs(scene_->GetAnimatorRenderStatus(id).time-0.01f)<1e-5f && world_.Capture().dump()==before,"SDK Lua conditional transition/rate/authored isolation failed");
        hook("function on_update(e,dt) assert(scene.controller_state(e:id())=='Run');assert(scene.controller_state(e:id(),1)=='Idle');assert(scene.controller_layer_weight(e:id(),1)==0.75);assert(math.abs(scene.controller_time(e:id(),1)-0.0025)<0.00001);assert(scene.controller_progress(e:id(),1)>0);local t=scene.controller_normalized_time(e:id(),1);assert(t>0 and t<1);assert(scene.animator_pause(e:id()));assert(scene.controller_state(e:id())=='Run') end");
        const auto paused=TakeHeap(*requests);Require(scene_->ApplyAnimatorRequests(*paused).applied==1,"SDK queued Controller Pause failed");scene_->SynchronizeModels(0.01f);
        Require(scene_->GetControllerState(id)=="Run" && !scene_->GetAnimatorRenderStatus(id).playing && std::abs(scene_->GetAnimatorRenderStatus(id).time-0.01f)<1e-5f,"SDK Controller Pause advanced state/clock");
        bool refused=false;try{hook("function on_update(e,dt) assert(scene.controller_set_layer_weight(e:id(),1,0));assert(scene.controller_set_bool(e:id(),'enabled',false));e:translate(99,0,0);error('discard') end");}catch(const Scripting::LuaScriptError&){refused=true;}
        const auto discarded=TakeHeap(*requests);Require(refused && discarded->count==0 && world_.Capture().dump()==before,"SDK failed typed hook published queue/World");
        hook("function on_update(e,dt) assert(scene.controller_layer_weight(e:id(),1)==0.75);assert(math.abs(scene.controller_time(e:id(),1)-0.0025)<0.00001) end");
        Require(TakeHeap(*requests)->count==0,"SDK layer read unexpectedly queued a mutation");
        hook("function on_update(e,dt) assert(scene.controller_play_state(e:id(),'Cutscene',0,1));assert(scene.controller_state(e:id(),1)=='Idle') end");
        const auto force=TakeHeap(*requests);Require(force->count==1 && scene_->ApplyAnimatorRequests(*force).applied==1,"SDK force request refused");
        Require(scene_->GetControllerState(id,1)=="Cutscene" && scene_->GetControllerState(id)=="Run" && !scene_->GetAnimatorRenderStatus(id).playing && scene_->GetAnimatorRenderStatus(id).time==0.01f && world_.Capture().dump()==before,"SDK force changed base clock/playing/authored World");
        refused=false;try{hook("function on_update(e,dt) assert(scene.controller_play_state(e:id(),'Idle',0,1));e:translate(99,0,0);error('discard force') end");}catch(const Scripting::LuaScriptError&){refused=true;}
        Require(refused && TakeHeap(*requests)->count==0 && scene_->GetControllerState(id,1)=="Cutscene" && world_.Capture().dump()==before,"SDK failed force hook leaked effects");
        hook("function on_update(e,dt) assert(scene.controller_state(e:id(),1)=='Cutscene');assert(scene.controller_time(e:id(),1)==0 and scene.controller_progress(e:id(),1)==0 and scene.controller_normalized_time(e:id(),1)==0);assert(scene.controller_layer_weight(e:id(),1)==0.75);assert(scene.controller_play_state(e:id(),'missing',0,1));assert(scene.controller_play_state(e:id(),'Idle',0,3));assert(scene.controller_state(e:id(),1)=='Cutscene') end");
        const auto missingForce=TakeHeap(*requests);const auto missingUsage=scene_->ApplyAnimatorRequests(*missingForce);
        Require(missingForce->count==2 && missingUsage.applied==0 && missingUsage.rejected==2 && scene_->GetControllerState(id,1)=="Cutscene","SDK missing force state/layer accepted");
        Require(scene_->ReloadController(id,error) && scene_->GetControllerState(id)=="Idle" && scene_->GetControllerState(id,1)=="Idle" &&
            !scene_->GetAnimatorRenderStatus(id).playing && scene_->GetAnimatorRenderStatus(id).time==0 && world_.Capture().dump()==before,
            "SDK explicit Scene owner reload did not reset states/clocks or preserve Pause/World");
        Require(!scene_->ReloadController(999999,error) && scene_->GetControllerState(id)=="Idle" && world_.Capture().dump()==before,
            "SDK missing-owner reload changed live Scene");
        std::cout<<"PASS: SDK explicit existing Scene owner reload/reset/Pause/world isolation and missing-owner refusal\n";
        std::cout<<"PASS: SDK-only Lua force-state actual Cutscene/no prediction/paused reentry/peer clock+authored retention/failed hook/missing state+layer refusal\n";
        std::cout<<"PASS: SDK-only layered EntityScene Lua indexed state/time/progress/normalized/weight FIFO/no prediction/failed-hook isolation\n";
        Scripting::ScriptAnimatorBatch invalid;Require(invalid.AppendController(id,Scripting::ScriptAnimatorRequestKind::Float,"enabled",1,false,cfg),"SDK wrong-type fixture admission failed");Require(requests->PublishTogether(invalid,nullptr,{}),"SDK wrong-type publication failed");
        const auto wrong=TakeHeap(*requests);Require(scene_->ApplyAnimatorRequests(*wrong).rejected==1,"SDK actual owner accepted wrong declared type");
        const auto retryFile=root/"Resources/sdk-initial-retry.animfsm";const std::string retryGuid(32,'f');
        auto badInitial=layeredGraph;badInitial["layers"][0]["states"][0]["clip"]="missing-initial-clip";
        write(retryFile,badInitial.dump());write(Assets::AssetRegistry::MetadataPath(retryFile),"{\"version\":1,\"guid\":\""+retryGuid+"\"}");
        Require(assets_.Scan(root/"Resources",error,Assets::AssetRegistry::MetadataPolicy::ReadOnly),error.c_str());
        const auto retryId=world_.CreateEntity("SDK failed initial owner");auto retryCfg=cfg;retryCfg.controllerGuid=retryGuid;retryCfg.playOnAwake=true;
        Require(world_.SetModel(retryId,"Resources/models/walk/walk.gltf") && world_.SetAnimator(retryId,retryCfg),"SDK initial retry fixture setup failed");
        scene_->SynchronizeModels(0);const auto retryWorld=world_.Capture().dump();
        Require(!scene_->GetModelRenderStatus(retryId).loaded && !scene_->ReloadController(retryId,error) &&
            !scene_->GetModelRenderStatus(retryId).loaded && world_.Capture().dump()==retryWorld && scene_->GetControllerState(id)=="Idle",
            "Failed initial retry changed old owner/peer/World");
        write(retryFile,layeredGraph.dump());scene_->SynchronizeModels(0);
        Require(!scene_->GetModelRenderStatus(retryId).loaded,"Same failed Model fingerprint was silently cleared after Source correction");
        Require(scene_->ReloadController(retryId,error) && scene_->GetModelRenderStatus(retryId).loaded &&
            scene_->GetAnimatorRenderStatus(retryId).ready && scene_->GetAnimatorRenderStatus(retryId).playing &&
            scene_->GetAnimatorRenderStatus(retryId).time==0 && scene_->GetControllerState(retryId)=="Idle" && world_.Capture().dump()==retryWorld,
            "SDK corrected initial owner explicit creation/authored playOnAwake/World isolation failed");
        scene_->SynchronizeModels(.01f);Require(std::abs(scene_->GetAnimatorRenderStatus(retryId).time-.005f)<1e-5f &&
            scene_->GetAnimatorRenderStatus(id).time==0 && !scene_->GetAnimatorRenderStatus(id).playing && world_.Capture().dump()==retryWorld,
            "SDK recreated owner did not advance independently or changed paused peer");
        std::cout<<"PASS: SDK failed initial owner/same fingerprint suppression/private failed retry/source correction/explicit creation/authored playOnAwake/independent clock/peer+World isolation\n";
        const auto retainedId=world_.CreateEntity("SDK nonready retained Model owner");
        Require(world_.SetModel(retainedId,"Resources/models/walk/walk.gltf"),"Retained owner setup failed");scene_->SynchronizeModels(0);
        Require(scene_->GetModelRenderStatus(retainedId).loaded,"Retained Model owner missing before Controller assignment");
        write(retryFile,badInitial.dump());Require(world_.SetAnimator(retainedId,retryCfg),"Nonready Controller assignment failed");scene_->SynchronizeModels(0);
        const auto retainedWorld=world_.Capture().dump();const auto retainedModel=scene_->GetModelRenderStatus(retainedId);
        const auto retainedTime=scene_->GetAnimatorRenderStatus(retainedId).time;
        Require(retainedModel.loaded && !scene_->GetAnimatorRenderStatus(retainedId).ready && !scene_->ReloadController(retainedId,error) &&
            scene_->GetModelRenderStatus(retainedId).loaded && scene_->GetModelRenderStatus(retainedId).revision==retainedModel.revision &&
            scene_->GetAnimatorRenderStatus(retainedId).time==retainedTime && world_.Capture().dump()==retainedWorld,
            "Failed nonready owner retry replaced retained Model/time/World");
        write(retryFile,layeredGraph.dump());
        Require(scene_->ReloadController(retainedId,error) && scene_->GetAnimatorRenderStatus(retainedId).ready &&
            scene_->GetAnimatorRenderStatus(retainedId).playing && scene_->GetControllerState(retainedId)=="Idle" &&
            scene_->GetAnimatorRenderStatus(retainedId).time==0 && world_.Capture().dump()==retainedWorld,
            "Explicit nonready retained owner replacement failed");
        std::cout<<"PASS: SDK retained Model owner/nonready Controller/private failure model+clock retention/explicit replacement/World isolation\n";
        scene_.reset();
        scene_=std::make_unique<Scene::EntityScene>(world_,*dxCommon,assets_,root,root/"MissingScene.kubo");
        scene_->Initialize();scene_->SynchronizeModels(0);
        Require(!scene_->GetAnimatorRenderStatus(id).ready && scene_->GetControllerState(id).empty(),"SDK missing explicit manifest fell back to another project");
        scene_.reset();
        scene_=std::make_unique<Scene::EntityScene>(world_,*dxCommon,assets_,root,"ControllerScene.kubo");
        scene_->Initialize();scene_->SynchronizeModels(0);
        Require(scene_->GetAnimatorRenderStatus(id).ready && scene_->GetControllerState(id)=="Idle","SDK root-relative explicit manifest recovery failed");
        std::cout<<"PASS: SDK-only selected manifest/legacy ambiguity/missing no-fallback/root-relative recovery\n";
        std::cout<<"PASS: SDK-only EntityScene Controller GUID/Lua typed FIFO/actual state+time/conditional transition/Pause/failed-hook discard/typed owner refusal\n";
    }
public:
    void Draw() override {}
    void Initialize() override {
        // Enable before device creation in this probe, including Release archive
        // verification; do not change the production Runtime configuration.
        Microsoft::WRL::ComPtr<ID3D12Debug1> debug;
        Require(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))),"Probe GPU debug layer unavailable");
        debug->EnableDebugLayer();debug->SetEnableGPUBasedValidation(TRUE);
        ConfigureWindowCloseRequestPolicy(false);
        const auto* preparedWindow=winApp.get();
        Require(preparedWindow!=nullptr,"SDK pre-init close policy did not prepare an owner");
        Framework::Initialize(); ShowWindow(winApp->GetHwnd(), SW_HIDE);
        Require(winApp.get()==preparedWindow && IsWindow(winApp->GetHwnd()),"SDK core initialization replaced its configured window owner");
        bool latePolicyRefused=false;
        try{ConfigureWindowCloseRequestPolicy(true);}catch(const std::logic_error&){latePolicyRefused=true;}
        Require(latePolicyRefused && winApp.get()==preparedWindow && IsWindow(winApp->GetHwnd()),"SDK late close configuration changed the active owner");
        std::cout<<"PASS: SDK-only pre-init close policy/retained window owner/late configuration refusal\n";
        Require(SUCCEEDED(dxCommon->GetDevice()->QueryInterface(IID_PPV_ARGS(&messages_))), "GPU debug queue unavailable");
        using namespace Engine; using AnimationSystem::AnimatorCommand;
        const auto a = world_.CreateEntity("Paused owner"), b = world_.CreateEntity("Fast owner"), c = world_.CreateEntity("Missing clip"), legacy = world_.CreateEntity("Legacy model");
        for (const auto id : {a,b,c,legacy}) Require(world_.SetModel(id,"Resources/models/walk/walk.gltf"), "Model fixture failed");
        AnimationSystem::AnimatorComponent cfg; cfg.loop = false; cfg.playOnAwake = false;
        Require(world_.SetAnimator(a,cfg), "First Animator fixture failed");
        auto fast = cfg; fast.speed = 2; fast.playOnAwake = true; Require(world_.SetAnimator(b,fast), "Second Animator fixture failed");
        auto missing = cfg; missing.clip = "missing-owned-clip"; missing.playOnAwake = true; Require(world_.SetAnimator(c,missing), "Missing fixture failed");
        const auto modelRoot = Base::PathToUtf8(GetProjectRoot()/"Resources/models/walk");
        auto* models = Graphics3D::ModelManager::GetInstance();
        auto* model = models->GetOrLoadModel("walk.gltf",modelRoot);
        const auto* clip = model->FindAnimationClip("default"); Require(clip && clip->duration > 0, "Actual clip unavailable");
        const float duration = clip->duration, step = duration/128;
        const auto matches = [step](float actual, float expected) { return std::abs(actual-expected) < (std::max)(1e-5f,step*1e-4f); };
        scene_ = std::make_unique<Scene::EntityScene>(world_,*dxCommon,assets_,GetProjectRoot()); scene_->Initialize();
        scene_->SynchronizeModels(step);
        const auto names=model->GetAnimationClipNames();
        Require(!names.empty() && names.front()=="default" && scene_->GetAnimationClipNames(a)==names && scene_->GetAnimationClipNames(b)==names && scene_->GetAnimationClipNames(c)==names && scene_->GetAnimationClipNames(legacy)==names,"SDK real model catalogs unavailable for admitted or repairable Animator drafts");
        Require(scene_->GetAnimationClipNames(0).empty(),"SDK missing Entity advertised clips");
        const bool previouslyActive=world_.Find(b)->active;world_.Find(b)->active=false;
        Require(scene_->GetAnimationClipNames(b).empty(),"SDK inactive Entity advertised clips");
        world_.Find(b)->active=previouslyActive;
        auto status = scene_->GetAnimatorRenderStatus(a);
        Require(status.ready && !status.playing && status.time == 0 && matches(scene_->GetAnimatorRenderStatus(b).time,2*step), "Real Scene playOnAwake/independent speed failed");
        Require(scene_->GetModelRenderStatus(c).loaded && !scene_->GetAnimatorRenderStatus(c).ready && scene_->GetAnimatorRenderStatus(c).time == 0 && !scene_->ControlAnimator(c,AnimatorCommand::Play), "First missing clip was silently admitted/advanced");
        scene_->ResetAnimations(); scene_->SynchronizeModels(step);
        Require(!scene_->GetAnimatorRenderStatus(c).ready && scene_->GetAnimatorRenderStatus(c).time==0,"Reset thawed first missing clip owner");
        // Restore initial timing for the independent-clock checks below.
        scene_->ResetAnimations();scene_->SynchronizeModels(step);
        Require(scene_->GetModelRenderStatus(legacy).loaded && !scene_->ControlAnimator(legacy,AnimatorCommand::Play), "Legacy model/command admission failed");
        const auto authored = world_.Capture().dump();
        Require(scene_->ControlAnimator(a,AnimatorCommand::Play), "Scene Play refused"); scene_->SynchronizeModels(step);
        Require(matches(scene_->GetAnimatorRenderStatus(a).time,step) && matches(scene_->GetAnimatorRenderStatus(b).time,4*step), "Scene clock ownership failed");
        Require(scene_->ControlAnimator(a,AnimatorCommand::Pause), "Scene Pause refused"); scene_->SynchronizeModels(step);
        Require(matches(scene_->GetAnimatorRenderStatus(a).time,step) && matches(scene_->GetAnimatorRenderStatus(b).time,6*step), "Pause stopped peer/advanced owner");
        Require(scene_->ControlAnimator(a,AnimatorCommand::Stop) && scene_->GetAnimatorRenderStatus(a).time==0 && !scene_->GetAnimatorRenderStatus(a).playing, "Stop did not reset/stay stopped");
        Require(scene_->ControlAnimator(a,AnimatorCommand::Restart), "Restart refused"); scene_->SynchronizeModels(step);
        Require(matches(scene_->GetAnimatorRenderStatus(a).time,step), "Restart clock failed");
        Require(!scene_->ControlAnimator(a,static_cast<AnimatorCommand>(99)) && scene_->GetAnimatorRenderStatus(a).playing, "Invalid command mutated playback");
        Require(world_.Capture().dump()==authored, "Runtime commands persisted clock/state");
        Require(scene_->ControlAnimator(a,AnimatorCommand::Pause), "Pause before settings failed");
        cfg.speed=0.5f; cfg.playOnAwake=true; Require(world_.SetAnimator(a,cfg), "Same clip settings failed"); scene_->SynchronizeModels(step);
        Require(matches(scene_->GetAnimatorRenderStatus(a).time,step) && !scene_->GetAnimatorRenderStatus(a).playing, "Same clip settings reset/restarted paused owner");
        Require(scene_->ControlAnimator(a,AnimatorCommand::Play), "Resume refused"); scene_->SynchronizeModels(step);
        Require(matches(scene_->GetAnimatorRenderStatus(a).time,1.5f*step), "Updated speed ignored");
        const auto retained = scene_->GetAnimatorRenderStatus(a).time;
        auto bad = cfg; bad.clip="not-a-real-clip"; Require(world_.SetAnimator(a,bad), "Bad authored name fixture failed"); scene_->SynchronizeModels(0);
        Require(!scene_->GetAnimatorRenderStatus(a).ready && matches(scene_->GetAnimatorRenderStatus(a).time,retained) && !scene_->ControlAnimator(a,AnimatorCommand::Restart), "Missing change admitted/reset prior owner");
        Require(world_.SetAnimator(a,cfg), "Restore valid setting failed"); scene_->SynchronizeModels(0);
        Require(scene_->GetAnimatorRenderStatus(a).ready && matches(scene_->GetAnimatorRenderStatus(a).time,retained), "Admission recovery reset previous valid owner");
        Require(models->LoadAnimationClipFromResourceRoot(modelRoot,"walk.gltf","scene_verified_walk","walk.gltf"), "Actual alias load failed");
        cfg.clip="scene_verified_walk"; cfg.playOnAwake=false; Require(world_.SetAnimator(a,cfg), "New clip setting failed");
        const auto peerTime = scene_->GetAnimatorRenderStatus(b).time; scene_->SynchronizeModels(0);
        Require(scene_->GetAnimatorRenderStatus(a).ready && !scene_->GetAnimatorRenderStatus(a).playing && scene_->GetAnimatorRenderStatus(a).time==0 && scene_->GetAnimatorRenderStatus(b).time==peerTime, "Private-owner clip replacement failed/changed peer");
        Require(scene_->ControlAnimator(a,AnimatorCommand::Restart), "Alias Restart failed"); scene_->SynchronizeModels(step);
        Require(matches(scene_->GetAnimatorRenderStatus(a).time,0.5f*step), "Actual alias playback failed");
        missing.clip="default"; Require(world_.SetAnimator(c,missing), "Missing correction failed"); scene_->SynchronizeModels(step);
        Require(scene_->GetAnimatorRenderStatus(c).ready && matches(scene_->GetAnimatorRenderStatus(c).time,step), "First missing owner failed recovery");
        world_.RemoveAnimator(a); scene_->SynchronizeModels(0); Require(!scene_->GetAnimatorRenderStatus(a).ready && scene_->GetModelRenderStatus(a).loaded, "Removal destroyed model/left binding");
        Require(world_.SetAnimator(a,cfg), "Re-add failed"); scene_->SynchronizeModels(0);
        Require(scene_->GetAnimatorRenderStatus(a).ready && scene_->GetAnimatorRenderStatus(a).time==0 && !scene_->GetAnimatorRenderStatus(a).playing, "Re-add did not create fresh binding");
        scene_->ResetAnimations(); Require(scene_->GetAnimatorRenderStatus(b).time==0 && scene_->GetAnimatorRenderStatus(b).playing && scene_->GetAnimatorRenderStatus(a).time==0 && !scene_->GetAnimatorRenderStatus(a).playing, "Scene reset ignored playOnAwake");
        cfg.speed=1; Require(world_.SetAnimator(a,cfg), "Clamp setting failed"); scene_->SynchronizeModels(0); Require(scene_->ControlAnimator(a,AnimatorCommand::Restart), "Clamp restart failed"); scene_->SynchronizeModels(duration*2);
        Require(matches(scene_->GetAnimatorRenderStatus(a).time,duration), "Scene nonloop clamp failed");
        cfg.loop=true; Require(world_.SetAnimator(a,cfg), "Loop setting failed"); scene_->SynchronizeModels(step);
        Require(matches(scene_->GetAnimatorRenderStatus(a).time,step), "Scene loop wrap failed");
        // Match current Lua archive + public headers to the actual Scene owner.
        using namespace Scripting;
        auto view=std::make_shared<ScriptSceneView>(); ScriptProperties defaults;
        auto factory=CreateLuaScriptFactory("function on_update(e,dt) assert(scene.set_animator(e:id(),'default',0.25,false,false));local c,s,l,p=scene.animator(e:id());assert(c=='default' and s==0.25 and not l and not p) end","SDK Animator authored settings",{},defaults,{},{},view);
        auto hooks=factory(); {ScriptSceneView::WriteScope scope(*view,world_);hooks.onUpdate(*world_.Find(b),0);}
        scene_->ResetAnimations();scene_->SynchronizeModels(step);
        Require(scene_->GetAnimatorRenderStatus(b).ready && !scene_->GetAnimatorRenderStatus(b).playing && scene_->GetAnimatorRenderStatus(b).time==0,"Lua-authored playOnAwake did not reach actual owner reset");
        Require(scene_->ControlAnimator(b,AnimatorCommand::Play),"Lua-authored owner play refused");scene_->SynchronizeModels(step);
        Require(matches(scene_->GetAnimatorRenderStatus(b).time,0.25f*step),"Lua-authored speed did not reach actual Scene clock");
        const auto beforeLuaFailure=world_.Capture().dump();const auto priorTime=scene_->GetAnimatorRenderStatus(b).time;
        auto badFactory=CreateLuaScriptFactory("function on_update(e,dt) assert(scene.set_animator(e:id(),'discarded',9,true,true));e:translate(99,0,0);error('discard settings') end","SDK Animator rollback",{},defaults,{},{},view);
        hooks=badFactory();bool refused=false;{ScriptSceneView::WriteScope scope(*view,world_);try{hooks.onUpdate(*world_.Find(b),0);}catch(const LuaScriptError&){refused=true;}}
        scene_->SynchronizeModels(0);Require(refused && world_.Capture().dump()==beforeLuaFailure && scene_->GetAnimatorRenderStatus(b).ready && scene_->GetAnimatorRenderStatus(b).time==priorTime,"Failed Lua settings changed real Scene owner/World");
        auto commands=std::make_shared<ScriptAnimatorRequests>();commands->SetEnabled(true);
        const auto enqueue=[&](Scene::EntityId id,const char* name){
            auto commandFactory=CreateLuaScriptFactory(std::string("function on_update(e,dt) assert(scene.")+name+"(e:id())) end","SDK Animator command",{},defaults,{},{},view,commands);
            auto callback=commandFactory();{ScriptSceneView::WriteScope scope(*view,world_);callback.onUpdate(*world_.Find(id),0);}
        };
        const auto apply=[&](const char* name){enqueue(b,name);const auto queued=TakeHeap(*commands);Require(queued->count==1,"SDK Lua command was not queued");scene_->SynchronizeModels(0);const auto usage=scene_->ApplyAnimatorRequests(*queued);Require(usage.applied==1 && usage.rejected==0,"SDK command did not reach real owner");scene_->SynchronizeModels(step);};
        const auto beforeCommands=world_.Capture().dump();
        apply("animator_pause");Require(!scene_->GetAnimatorRenderStatus(b).playing && matches(scene_->GetAnimatorRenderStatus(b).time,priorTime),"Lua Pause advanced real clock");
        apply("animator_restart");Require(scene_->GetAnimatorRenderStatus(b).playing && matches(scene_->GetAnimatorRenderStatus(b).time,0.25f*step),"Lua Restart failed real clock");
        apply("animator_stop");Require(!scene_->GetAnimatorRenderStatus(b).playing && scene_->GetAnimatorRenderStatus(b).time==0,"Lua Stop failed real clock");
        apply("animator_play");Require(scene_->GetAnimatorRenderStatus(b).playing && matches(scene_->GetAnimatorRenderStatus(b).time,0.25f*step) && world_.Capture().dump()==beforeCommands,"Lua Play failed or runtime commands persisted");
        enqueue(b,"animator_pause");const auto stale=TakeHeap(*commands);auto changedB=world_.Find(b)->animator;changedB.speed=0.5f;Require(world_.SetAnimator(b,changedB),"Stale command fixture failed");scene_->SynchronizeModels(0);auto usage=scene_->ApplyAnimatorRequests(*stale);Require(usage.applied==0 && usage.rejected==1 && scene_->GetAnimatorRenderStatus(b).playing,"Stale settings command changed current owner");
        auto missingB=changedB;missingB.clip="command-missing";Require(world_.SetAnimator(b,missingB),"Missing command fixture failed");enqueue(b,"animator_restart");const auto missingCommand=TakeHeap(*commands);scene_->SynchronizeModels(0);usage=scene_->ApplyAnimatorRequests(*missingCommand);Require(usage.applied==0 && usage.rejected==1 && !scene_->GetAnimatorRenderStatus(b).ready,"Missing clip command was applied");
        Require(world_.SetAnimator(b,changedB),"Recover command owner failed");scene_->SynchronizeModels(0);
        enqueue(c,"animator_play");const auto deletedCommand=TakeHeap(*commands);Require(world_.DestroyEntity(c),"Deleted command fixture failed");scene_->SynchronizeModels(0);usage=scene_->ApplyAnimatorRequests(*deletedCommand);Require(usage.applied==0 && usage.rejected==1,"Deleted command target revived/applied");
        const auto query=[&](const char* text){auto queryFactory=CreateLuaScriptFactory(text,"SDK Animator state",{},defaults,{},{},view,commands);auto callback=queryFactory();{ScriptSceneView::WriteScope scope(*view,world_,nullptr,scene_.get());callback.onUpdate(*world_.Find(b),0);}};
        const auto liveQueryTime=scene_->GetAnimatorRenderStatus(b).time;
        query("function on_update(e,dt) local r,p,t=scene.animator_state(e:id());assert(r and p and type(t)=='number');assert(scene.animator_pause(e:id()));local a,b,c=scene.animator_state(e:id());assert(a==r and b==p and c==t);e:set_position(t,1,1) end");
        Require(matches(world_.Find(b)->transform.position.x,liveQueryTime) && world_.Find(b)->transform.position.y==1,"SDK Lua state did not read actual time");
        const auto queryPause=TakeHeap(*commands);usage=scene_->ApplyAnimatorRequests(*queryPause);Require(usage.applied==1,"SDK query Pause not admitted");scene_->SynchronizeModels(0);
        query("function on_update(e,dt) local r,p,t=scene.animator_state(e:id());assert(r and not p and type(t)=='number');e:set_position(t,1,0) end");Require(matches(world_.Find(b)->transform.position.x,liveQueryTime),"SDK Lua paused actual time changed");
        query("function on_update(e,dt) assert(scene.set_animator(e:id(),'default',2,false,false));local r,p,t=scene.animator_state(e:id());assert(not r and not p and t==nil);assert(scene.set_animator(e:id(),'default',0.5,false,false));local a,b,c=scene.animator_state(e:id());assert(a and not b and type(c)=='number');assert(scene.set_active(e:id(),false));r,p,t=scene.animator_state(e:id());assert(not r and not p and t==nil);assert(scene.set_active(e:id(),true));local q=scene.clone(e:id());r,p,t=scene.animator_state(q);assert(not r and not p and t==nil);assert(scene.destroy(q));assert(scene.animator_state(scene.find('Legacy model'))==nil) end");
        const auto priorModel=world_.Find(b)->model;world_.Find(b)->model.assetPath="Resources/models/axis/axis.obj";
        Require(scene_->GetAnimationClipNames(b).empty(),"SDK stale model owner advertised old clips");
        query("function on_update(e,dt) local r,p,t=scene.animator_state(e:id());assert(not r and not p and t==nil) end");world_.Find(b)->model=priorModel;
        Require(scene_->GetAnimationClipNames(b)==model->GetAnimationClipNames(),"SDK restored model identity lost clip choices");
        std::cout<<"SDK_SCENE_CLIP_CATALOG_NAMES="<<names.size()<<" INACTIVE_STALE_REFUSAL=1\n";
        query("function on_update(e,dt) assert(scene.set_animator(e:id(),'state-missing',0.5,false,false));local r,p,t=scene.animator_state(e:id());assert(not r and not p and t==nil) end");scene_->SynchronizeModels(0);
        query("function on_update(e,dt) local r,p,t=scene.animator_state(e:id());assert(not r and not p and t==nil) end");Require(world_.SetAnimator(b,changedB),"State recovery setting failed");scene_->SynchronizeModels(0);
        query("function on_update(e,dt) local r,p,t=scene.animator_state(e:id());assert(r and not p and type(t)=='number') end");
        VerifyController();checked_=true; endRequest_=true;
    }
    void Finalize() override { if(dxCommon)dxCommon->WaitForAllFrames(); scene_.reset(); Framework::Finalize(); retired_=true; }
    void Verify() const {
        Require(checked_ && retired_, "Scene probe/owned cleanup incomplete");
        for(UINT64 i=0;i<messages_->GetNumStoredMessages();++i){ SIZE_T bytes=0; Require(SUCCEEDED(messages_->GetMessage(i,nullptr,&bytes)),"GPU message size failed"); std::vector<unsigned char> data(bytes); auto* message=reinterpret_cast<D3D12_MESSAGE*>(data.data()); Require(SUCCEEDED(messages_->GetMessage(i,message,&bytes)),"GPU message read failed"); Require(message->Severity!=D3D12_MESSAGE_SEVERITY_ERROR && message->Severity!=D3D12_MESSAGE_SEVERITY_CORRUPTION,"Scene GPU debug error"); }
        std::cout << "PASS: SDK-only production EntityScene Animator real-model binding, independent Object3D clocks, commands/missing/recovery/alias/removal/reset/clamp/loop, Lua-authored settings and queued commands, Lua actual state/time with pending/candidate/inactive/new-owner/stale-model/missing-clip/recovery admission, no authored runtime fields and GPU errors0/owned cleanup (no draw/pixel/Inspector/full Editor-Player state-query E2E)\n";
    }
};
int main(){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);try{Probe probe;probe.Run();probe.Verify();return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
