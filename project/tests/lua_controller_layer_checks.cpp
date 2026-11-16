#include "engine/script/LuaScript.h"
#include <iostream>
#include <limits>
static void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
__declspec(noinline) static std::unique_ptr<Engine::Scripting::ScriptAnimatorRequests::Pending> Drain(Engine::Scripting::ScriptAnimatorRequests& requests){auto p=std::make_unique<Engine::Scripting::ScriptAnimatorRequests::Pending>();*p=requests.Take();return p;}
class LayerReader final : public Engine::AnimationSystem::AnimatorStateReader {
    const Engine::Scene::EntityWorld& world_;
public:
    explicit LayerReader(const Engine::Scene::EntityWorld& w):world_(w){}
    const Engine::Scene::EntityWorld* OwnerWorld() const noexcept override{return &world_;}
    Engine::AnimationSystem::AnimatorPlaybackState ReadAnimatorState(const Engine::Scene::EntityRecord&,const Engine::AnimationSystem::AnimatorComponent&) const noexcept override{return {true,true,0.4f};}
    std::string_view ReadControllerState(const Engine::Scene::EntityRecord&,const Engine::AnimationSystem::AnimatorComponent& cfg) const noexcept override{return cfg.controllerGuid.empty()?std::string_view{}:std::string_view("Base");}
    Engine::AnimationSystem::ControllerLayerState sample{true,0.8f,0.25f,1.4,0.4,"Upper"};
    Engine::AnimationSystem::ControllerLayerState ReadControllerLayer(const Engine::Scene::EntityRecord&,const Engine::AnimationSystem::AnimatorComponent& cfg,size_t layer) const noexcept override {
        if(cfg.controllerGuid.empty() || cfg.speed!=1 || layer>=2)return {};return layer?sample:Engine::AnimationSystem::ControllerLayerState{true,0.4f,1,0.2,0.2,"Base"};
    }
};
int main(){try{
    using namespace Engine::Scene;using namespace Engine::Scripting;
    EntityWorld world;const auto id=world.CreateEntity("Layer owner");Engine::AnimationSystem::AnimatorComponent settings;settings.controllerGuid=std::string(32,'a');
    Need(world.SetModel(id,"owned.gltf") && world.SetAnimator(id,settings),"Layer World setup failed");
    auto view=std::make_shared<ScriptSceneView>();auto requests=std::make_shared<ScriptAnimatorRequests>();requests->SetEnabled(true);
    auto audio=std::make_shared<ScriptAudioRequests>();audio->SetEnabled(true);ScriptProperties defaults;LayerReader reader(world);
    const auto run=[&](const char* text,bool writable=true,const LayerReader* selected=nullptr){auto f=CreateLuaScriptFactory(text,"Controller layers",{},defaults,audio,{},view,requests);auto h=f();if(writable){ScriptSceneView::WriteScope scope(*view,world,nullptr,selected?selected:&reader);h.onUpdate(*world.Find(id),0);}else{ScriptSceneView::Scope scope(*view,world,selected?selected:&reader);h.onUpdate(*world.Find(id),0);}};
    run("function on_update(e,dt) assert(scene.controller_state(e:id())=='Base');assert(scene.controller_state(e:id(),1)=='Upper');assert(math.abs(scene.controller_time(e:id(),1)-0.8)<1e-6);assert(scene.controller_progress(e:id(),1)==1.4);assert(scene.controller_normalized_time(e:id(),1)==0.4);assert(scene.controller_layer_weight(e:id(),1)==0.25);assert(scene.controller_state(e:id(),3)==nil and scene.controller_time(e:id(),3)==nil and scene.controller_layer_weight(999,0)==nil) end",false);
    const auto before=world.Capture().dump();
    run("function on_update(e,dt) assert(scene.controller_set_layer_weight(e:id(),1,0.75));assert(scene.controller_layer_weight(e:id(),1)==0.25);assert(scene.controller_set_float(e:id(),'speed',2));assert(scene.animator_pause(e:id()));e:translate(1,0,0);audio.play('owned.wav') end");
    auto queued=Drain(*requests);Need(queued->count==3 && queued->values[0].kind==ScriptAnimatorRequestKind::LayerWeight && queued->values[0].layer==1 && queued->values[0].number==0.75 && queued->values[0].ValidControllerPayload() && queued->values[1].kind==ScriptAnimatorRequestKind::Float && queued->values[2].kind==ScriptAnimatorRequestKind::Playback && audio->Take().size()==1,"Layer/mixed FIFO + Audio publication failed");
    world.Find(id)->transform.position.x=0;
    for(const char* text:{
        "function on_update(e,dt) assert(scene.controller_set_layer_weight(e:id(),1,0.5));e:translate(9,0,0);audio.play('owned.wav');error('discard') end",
        "function on_update(e,dt) e:translate(9,0,0);scene.controller_set_layer_weight(e:id(),1,0/0) end",
        "function on_update(e,dt) e:translate(9,0,0);scene.controller_set_layer_weight(e:id(),1,1.1) end",
        "function on_update(e,dt) e:translate(9,0,0);scene.controller_set_layer_weight(e:id(),'1',0.5) end",
        "function on_update(e,dt) e:translate(9,0,0);scene.controller_state(e:id(),1.5) end",
        "function on_update(e,dt) e:translate(9,0,0);scene.controller_time(e:id(),-1) end",
        "function on_update(e,dt) e:translate(9,0,0);scene.controller_progress(e:id(),4) end",
        "function on_update(e,dt) assert(scene.translate(e:id(),1,0,0));for i=1,64 do assert(scene.controller_set_layer_weight(e:id(),1,0.5)) end end"
    }){bool denied=false;try{run(text);}catch(const LuaScriptError&){denied=true;}Need(denied && world.Capture().dump()==before && Drain(*requests)->count==0 && audio->Take().empty(),"Invalid/failed/quota layer hook leaked outputs");}
    bool denied=false;try{run("function on_update(e,dt) scene.controller_set_layer_weight(e:id(),1,0.5) end",false);}catch(const LuaScriptError&){denied=true;}Need(denied && Drain(*requests)->count==0,"Readonly layer setter allowed");
    ScriptAnimatorBatch full;for(int i=0;i<64;++i)Need(full.AppendLayerWeight(id,1,0.5,settings),"Layer full fixture failed");
    for(int i=0;i<4;++i)Need(requests->PublishTogether(full,nullptr,{}),"Layer queue fill failed");
    denied=false;try{run("function on_update(e,dt) assert(scene.controller_set_layer_weight(e:id(),1,0.5));e:translate(9,0,0);audio.play('owned.wav') end");}catch(const LuaScriptError&){denied=true;}
    Need(denied && Drain(*requests)->count==256 && audio->Take().empty() && world.Capture().dump()==before,"Full layer queue lost prior requests/leaked effects");
    std::vector<ScriptAudioRequest> sounds(32);for(auto& sound:sounds)sound.path="earlier.wav";for(int i=0;i<8;++i)Need(audio->Publish(sounds),"Audio fill failed");
    denied=false;try{run("function on_update(e,dt) assert(scene.controller_set_layer_weight(e:id(),1,0.5));e:translate(9,0,0);audio.play('owned.wav') end");}catch(const LuaScriptError&){denied=true;}
    Need(denied && Drain(*requests)->count==0 && audio->Take().size()==256 && world.Capture().dump()==before,"Full Audio queue published layer write");
    auto invalid=queued->values[0];invalid.layer=4;Need(!invalid.ValidControllerPayload(),"Invalid pending layer payload accepted");invalid=queued->values[0];invalid.parameterBytes=1;Need(!invalid.ValidControllerPayload(),"Named weight payload accepted");
    auto changed=settings;changed.speed=2;Need(!queued->values[0].Matches(changed),"Changed settings did not stale layer request");
    reader.sample.ready=false;run("function on_update(e,dt) assert(scene.controller_state(e:id(),1)==nil and scene.controller_time(e:id(),1)==nil) end",false);reader.sample.ready=true;
    reader.sample.normalized=std::numeric_limits<double>::quiet_NaN();run("function on_update(e,dt) assert(scene.controller_normalized_time(e:id(),1)==nil and scene.controller_layer_weight(e:id(),1)==nil) end",false);reader.sample.normalized=0.4;
    EntityWorld other;LayerReader wrong(other);denied=false;try{run("function on_update(e,dt) scene.controller_time(e:id(),1) end",false,&wrong);}catch(const LuaScriptError&){denied=true;}Need(denied,"Foreign World layer reader allowed");
    denied=false;try{auto module=CreateLuaScriptFactory("scene.controller_layer_weight(1,1)","Layer module",{},defaults,audio,{},view,requests);auto h=module();(void)h;}catch(const LuaScriptError&){denied=true;}Need(denied,"Module layer query allowed");
    run("function on_update(e,dt) assert(scene.controller_play_state(e:id(),'Run'));assert(scene.controller_play_state(e:id(),'UpperRun',0,1));assert(scene.controller_set_layer_weight(e:id(),1,0.5));assert(scene.controller_state(e:id())=='Base' and scene.controller_state(e:id(),1)=='Upper');e:translate(1,0,0);audio.play('owned.wav') end");
    auto forced=Drain(*requests);
    Need(forced->count==3 && forced->values[0].kind==ScriptAnimatorRequestKind::ForceState && forced->values[0].layer==0 && forced->values[0].number==0.2 && forced->values[0].parameterBytes==3 && forced->values[0].ValidControllerPayload() && forced->values[1].layer==1 && forced->values[1].number==0 && forced->values[2].kind==ScriptAnimatorRequestKind::LayerWeight && audio->Take().size()==1,"Force-state defaults/layer/mixed FIFO/paired Audio failed");
    world.Find(id)->transform.position.x=0;
    for(const char* text:{
        "function on_update(e,dt) assert(scene.controller_play_state(e:id(),'Run',0,1));e:translate(9,0,0);audio.play('owned.wav');error('discard force') end",
        "function on_update(e,dt) e:translate(9,0,0);scene.controller_play_state(e:id(),123) end",
        "function on_update(e,dt) e:translate(9,0,0);scene.controller_play_state(e:id(),'') end",
        "function on_update(e,dt) e:translate(9,0,0);scene.controller_play_state(e:id(),string.char(255)) end",
        "function on_update(e,dt) e:translate(9,0,0);scene.controller_play_state(e:id(),'Run'..string.char(0)) end",
        "function on_update(e,dt) e:translate(9,0,0);scene.controller_play_state(e:id(),string.rep('x',129)) end",
        "function on_update(e,dt) e:translate(9,0,0);scene.controller_play_state(e:id(),'Run','0') end",
        "function on_update(e,dt) e:translate(9,0,0);scene.controller_play_state(e:id(),'Run',0/0) end",
        "function on_update(e,dt) e:translate(9,0,0);scene.controller_play_state(e:id(),'Run',61) end",
        "function on_update(e,dt) e:translate(9,0,0);scene.controller_play_state(e:id(),'Run',-1) end",
        "function on_update(e,dt) e:translate(9,0,0);scene.controller_play_state(e:id(),'Run',0,1.5) end",
        "function on_update(e,dt) e:translate(9,0,0);scene.controller_play_state(e:id(),'Run',0,4) end",
        "function on_update(e,dt) assert(scene.translate(e:id(),1,0,0));for i=1,64 do assert(scene.controller_play_state(e:id(),'Run')) end end"
    }){denied=false;try{run(text);}catch(const LuaScriptError&){denied=true;}Need(denied && Drain(*requests)->count==0 && audio->Take().empty() && world.Capture().dump()==before,"Force invalid/failed/shared quota hook leaked outputs");}
    denied=false;try{run("function on_update(e,dt) scene.controller_play_state(e:id(),'Run') end",false);}catch(const LuaScriptError&){denied=true;}Need(denied && Drain(*requests)->count==0,"Readonly force accepted");
    run("function on_update(e,dt) assert(not scene.controller_play_state(999,'Run'));assert(scene.controller_play_state(e:id(),'unknown-state',nil,3)) end");
    Need(Drain(*requests)->count==1,"Force incorrectly predicted actual graph admission");
    for(int i=0;i<4;++i)Need(requests->PublishTogether(full,nullptr,{}),"Force full queue fixture failed");
    denied=false;try{run("function on_update(e,dt) assert(scene.controller_play_state(e:id(),'Run'));e:translate(9,0,0);audio.play('owned.wav') end");}catch(const LuaScriptError&){denied=true;}
    Need(denied && Drain(*requests)->count==256 && audio->Take().empty() && world.Capture().dump()==before,"Full Animator queue leaked force effects");
    for(int i=0;i<8;++i)Need(audio->Publish(sounds),"Force full Audio fixture failed");
    denied=false;try{run("function on_update(e,dt) assert(scene.controller_play_state(e:id(),'Run'));e:translate(9,0,0);audio.play('owned.wav') end");}catch(const LuaScriptError&){denied=true;}
    Need(denied && Drain(*requests)->count==0 && audio->Take().size()==256 && world.Capture().dump()==before,"Full Audio queue published force");
    invalid=forced->values[0];invalid.layer=4;Need(!invalid.ValidControllerPayload(),"Invalid force layer payload accepted");
    invalid=forced->values[0];invalid.number=61;Need(!invalid.ValidControllerPayload(),"Invalid force duration payload accepted");
    invalid=forced->values[0];invalid.parameterBytes=129;Need(!invalid.ValidControllerPayload(),"Force overlength payload accepted");
    Need(!forced->values[0].Matches(changed),"Changed settings did not stale force request");
    run("function on_update(e,dt) assert(scene.controller_play_state(e:id(),'Run')) end");
    denied=false;try{run("function on_update(e,dt) assert(scene.controller_play_state(e:id(),'Idle'));e:translate(9,0,0);error('failed later peer') end");}catch(const LuaScriptError&){denied=true;}
    const auto retainedForce=Drain(*requests);Need(denied && retainedForce->count==1 && std::string_view(retainedForce->values[0].parameter.data(),retainedForce->values[0].parameterBytes)=="Run" && world.Capture().dump()==before,"Failed force peer lost earlier healthy queue or published effects");
    denied=false;try{auto module=CreateLuaScriptFactory("scene.controller_play_state(1,'Run')","Force module",{},defaults,audio,{},view,requests);auto h=module();(void)h;}catch(const LuaScriptError&){denied=true;}Need(denied,"Module force accepted");
    requests->SetEnabled(false);denied=false;try{run("function on_update(e,dt) scene.controller_play_state(e:id(),'Run') end");}catch(const LuaScriptError&){denied=true;}Need(denied && world.Capture().dump()==before,"Disabled force host allowed mutation");
    denied=false;try{run("function on_update(e,dt) scene.controller_set_layer_weight(e:id(),1,0.5) end");}catch(const LuaScriptError&){denied=true;}Need(denied && Drain(*requests)->count==0,"Disabled layer host allowed mutation");
    std::cout<<"PASS: Lua queued force-state defaults/layer/strict bounds/mixed FIFO/no prediction/paired Audio+World/failed hook/quota/full/readonly/disabled/module/malformed payload isolation\n";
    std::cout<<"PASS: Lua indexed Controller state/seconds/progress/normalized/weight, strict indices, mixed queued weight FIFO/no prediction, Audio+World publication and failed/invalid/quota/full/readonly/disabled/wrong-owner/module isolation (actual Scene consumption separate)\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
