#include "engine/scene/entity/EntityWorld.h"
#include "engine/editor/PrefabInstance.h"
#include "engine/editor/PrefabInstanceCapture.h"
#include "engine/editor/PrefabRevert.h"
#include "engine/editor/PrefabMerge.h"
#include "engine/editor/PrefabFieldRevert.h"
#include "engine/editor/PrefabFieldApplyPlan.h"
#include "engine/scene/prefab/RuntimePrefab.h"
#include <iostream>
#include <limits>
#include <array>
namespace {
using namespace Engine;void Need(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
}
int main(){try{
 Scene::ParticleComponent settings;settings.enabled=false;settings.assetPath="Assets/粒子.dds";settings.assetGuid=std::string(32,'a');settings.capacity=256;settings.burst=128;settings.seed=(std::numeric_limits<uint32_t>::max)();settings.geometry=Scene::ParticleGeometry::Cylinder;
 const auto legacyCodec=Scene::EncodeParticle(settings);
 Scene::ParticleComponent legacyCurves;
 Need(Scene::DecodeParticle(legacyCodec,legacyCurves)&&legacyCurves.SizeAt(.5f)==settings.size&&
     legacyCurves.ColorAt(.5f).w==.5f,"Existing lifetime interpolation changed");
 settings.size=1;settings.endSize=0.f;settings.sizeKeys={{.5f,3}};
 settings.startColor={1,0,0,1};settings.endColor={0,0,1,0};
 settings.colorKeys={{.5f,{0,1,0,1}}};
 const auto color=settings.ColorAt(.75f);
 Need(settings.SizeAt(.25f)==2 && settings.SizeAt(.75f)==1.5f && color.x==0 && color.y==.5f && color.z==.5f,
     "Lifetime curves did not interpolate neighboring keys");
 Need(settings.IsValid(),"Valid Particle settings rejected");const auto encoded=Scene::EncodeParticle(settings);Scene::ParticleComponent decoded;Need(Scene::DecodeParticle(encoded,decoded)&&decoded==settings,"Particle codec lost fields");
 const auto bad=[&](Scene::SceneValue value){auto retained=settings;retained.capacity=17;const auto before=Scene::EncodeParticle(retained).dump();Need(!Scene::DecodeParticle(value,retained)&&Scene::EncodeParticle(retained).dump()==before,"Invalid Particle settings admitted/changed output");};
 auto badKeys=encoded;
 badKeys["sizeKeys"]=crude_json::array{crude_json::array{.5,1.},crude_json::array{.5,2.}};bad(badKeys);
 badKeys=encoded;badKeys["colorKeys"]=crude_json::array{crude_json::array{1.,0.,1.,0.,1.}};bad(badKeys);
 for(const auto* name:{"capacity","burst","seed"}){auto value=encoded;value[name]=-1.0;bad(value);value[name]=1.5;bad(value);value[name]=4294967296.0;bad(value);}
 for(const auto* name:{"rate","lifetime","size"}){auto value=encoded;value[name]=(std::numeric_limits<double>::quiet_NaN)();bad(value);value[name]=-std::numeric_limits<double>::denorm_min();bad(value);}
 auto value=encoded;value["unknown"]=true;bad(value);value=encoded;value["geometry"]="sphere";bad(value);value=encoded;value["enabled"]=1.0;bad(value);value=encoded;value["burst"]=257.0;bad(value);value=encoded;value["asset"]="../outside.dds";bad(value);value=encoded;value["guid"]="invalid";bad(value);value=encoded;value["spread"]=crude_json::array{-1.,0.,0.};bad(value);value=encoded;value["startColor"]=crude_json::array{1.,1.,1.,2.};bad(value);
 Scene::EntityWorld world;const auto root=world.CreateEntity("Root"),child=world.CreateEntity("Emitter",root);std::string error;
 Need(world.Capture()["version"].get<double>()==1 && world.SetParticle(child,settings),"Particle addition failed");const auto snapshot=world.Capture();Need(snapshot["version"].get<double>()==2,"Particle did not select protected World2");
 Scene::EntityWorld restored;Need(restored.Restore(snapshot,error)&&restored.Find(child)->hasParticle&&restored.Find(child)->particle==settings,error.c_str());
 auto wrong=snapshot;wrong["version"]=1.0;const auto preserved=restored.Capture().dump();Need(!restored.Restore(wrong,error)&&restored.Capture().dump()==preserved,"World1 silently accepted Particle/changed old World");
 auto legacy=world;legacy.RemoveParticle(child);auto plain=legacy.Capture();Need(plain["version"].get<double>()==1,"Removing last Particle retained World2");wrong=plain;wrong["version"]=2.0;Need(!restored.Restore(wrong,error),"Unknown World2 without Particle admitted");Need(restored.Restore(plain,error),"Existing World1 no longer loads");
 Scene::EntityId clone=0;Need(world.DuplicateSubtree(root,0,clone,error),error.c_str());Need(world.Find(clone+1)->particle==settings,"Duplication lost Particle settings");
 Prefab::PrefabValue definition;Need(Prefab::CapturePrefabSubtree(world,child,definition,error)&&definition["world"]["version"].get<double>()==2,error.c_str());
 Scene::EntityWorld bare;Scene::EntityId born=0;Need(Prefab::InstantiateRuntimePrefab(bare,definition,0,born,error)&&bare.Find(born)->particle==settings,error.c_str());
 Scene::EntityWorld templateWorld;const auto original=templateWorld.CreateEntity("Plain");Prefab::PrefabValue oldSource;Need(Prefab::CapturePrefabSubtree(templateWorld,original,oldSource,error),error.c_str());
 Scene::EntityWorld linked;Editor::EntitySelection selected;Editor::EntityHistory history;Scene::EntityId instance=0;const auto guid=std::string(32,'b');Need(Editor::InstantiatePrefab(linked,oldSource,guid,0,selected,history,instance,error),error.c_str());Need(linked.SetParticle(instance,settings),"Linked Particle addition");
 Editor::PrefabInstanceCapture capture;Need(Editor::CapturePrefabInstance(linked,instance,capture,error)&&capture.instance["world"]["version"].get<double>()==2&&capture.baseline["world"]["version"].get<double>()==1,error.c_str());
 Need(Editor::RevertPrefabInstance(linked,instance,oldSource,guid,selected,history,error)&&!linked.Find(instance)->hasParticle&&linked.Capture()["version"].get<double>()==1,error.c_str());
 Need(history.Undo(linked,selected,error)&&linked.Find(instance)->hasParticle&&linked.Capture()["version"].get<double>()==2,error.c_str());Need(history.Redo(linked,selected,error)&&!linked.Find(instance)->hasParticle,error.c_str());
 Need(templateWorld.SetParticle(original,settings),"Source Particle setup");Prefab::PrefabValue newSource;Need(Prefab::CapturePrefabSubtree(templateWorld,original,newSource,error),error.c_str());Need(Editor::RevertPrefabInstance(linked,instance,newSource,guid,selected,history,error)&&linked.Find(instance)->particle==settings&&linked.Capture()["version"].get<double>()==2,error.c_str());
 Editor::PrefabMergeReport report;Prefab::PrefabValue merged;Need(Editor::MergePrefabDocuments(oldSource,newSource,oldSource,merged,report,error)&&merged["world"]["version"].get<double>()==2,error.c_str());Need(Editor::MergePrefabDocuments(newSource,oldSource,newSource,merged,report,error)&&merged["world"]["version"].get<double>()==1,error.c_str());
 Scene::EntityWorld peers;Editor::EntitySelection peerSelection;Editor::EntityHistory peerHistory;Scene::EntityId chosen=0,inherited=0;Need(Editor::InstantiatePrefab(peers,oldSource,guid,0,peerSelection,peerHistory,chosen,error),"Particle peer chosen setup");Need(Editor::InstantiatePrefab(peers,oldSource,guid,0,peerSelection,peerHistory,inherited,error)&&peers.SetParticle(chosen,settings),"Particle peer inheritance setup");
 const auto field=[&](const Scene::EntityWorld& input){Editor::PrefabOverrideReport differences;Need(Editor::InspectPrefabOverrides(input,chosen,differences,error),"Particle field inspection");const auto found=std::find_if(differences.entries.begin(),differences.entries.end(),[](const auto& entry){return entry.path=="/particle";});Need(found!=differences.entries.end(),"Particle whole-field difference missing");return *found;};
 Scene::EntityWorld reverted;Need(Editor::BuildPrefabFieldRevertCandidate(peers,chosen,oldSource,guid,field(peers),reverted,error)&&!reverted.Find(chosen)->hasParticle&&reverted.Capture()["version"].get<double>()==1,"Particle whole-field Revert did not retag World1");
 std::unique_ptr<Editor::PrefabApplyPlan> plan;Need(Editor::BuildPrefabFieldApplyPlan(peers,chosen,oldSource,guid,field(peers),plan,error)&&plan->source["world"]["version"].get<double>()==2&&plan->world.Find(chosen)->particle==settings&&plan->world.Find(inherited)->hasParticle,"Particle whole-field Apply/inherited World2 failed");
 auto applied=plan->world;const auto particleSource=plan->source;applied.RemoveParticle(chosen);Need(Editor::BuildPrefabFieldApplyPlan(applied,chosen,particleSource,guid,field(applied),plan,error)&&plan->source["world"]["version"].get<double>()==1&&!plan->world.Find(chosen)->hasParticle&&!plan->world.Find(inherited)->hasParticle&&plan->world.Capture()["version"].get<double>()==1,"Particle whole-field removal Apply did not retag World1");
 std::cout<<"PASS: Particle strict settings/invalid-output isolation, protected World2/legacy1, duplicate/runtime Prefab, linked capture/Revert/history/three-way and selected-field Apply+Revert/peer inheritance addition+removal (CPU data, not GPU/Inspector/export)\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
