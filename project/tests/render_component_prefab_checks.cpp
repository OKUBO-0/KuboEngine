#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "engine/editor/PrefabFieldRevert.h"
#include "engine/editor/PrefabInstance.h"
#include "engine/scene/prefab/RuntimePrefab.h"
#include "engine/scene/entity/WorldPlayback.h"
#include <iostream>
#include <new>
#include <cstdlib>

static long failAfter=-1;static bool counting=false;static size_t allocations=0;
void* operator new(size_t size){if(counting)++allocations;if(failAfter==0){failAfter=-1;throw std::bad_alloc();}if(failAfter>0)--failAfter;if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,size_t) noexcept {std::free(p);}
static void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(){using namespace Engine;using namespace Scene;using namespace Editor;std::filesystem::path fixture;try{
    EntityWorld source;const auto sourceRoot=source.CreateEntity("Lit model"),sourceChild=source.CreateEntity("Spot",sourceRoot);
    LightComponent directional,spot;spot.kind=LightKind::Spot;spot.enabled=false;spot.color={0.3f,0.7f,1};spot.intensity=5;
    MaterialComponent material;material.lighting=true;material.color={0.2f,0.5f,0.8f,1};material.roughness=0.8f;
    Need(source.SetLight(sourceRoot,directional) && source.SetLight(sourceChild,spot) && source.SetMaterial(sourceRoot,material),"Render source setup failed");
    const auto sourceBytes=source.Capture().dump();const auto sourceHigh=source.AllocationHighWatermark();
    EntityId clone=0;std::string error;auto duplicate=source;Need(duplicate.DuplicateSubtree(sourceRoot,0,clone,error),error.c_str());
    Need(duplicate.Find(clone)->hasLight && duplicate.Find(clone)->hasMaterial && duplicate.Find(clone)->material.lighting,"Subtree clone lost render components");
    const EntityRecord* clonedChild=nullptr;for(const auto& e:duplicate.GetEntities())if(e.parent==clone)clonedChild=&e;
    Need(clonedChild && clonedChild->hasLight && clonedChild->light.kind==LightKind::Spot && !clonedChild->light.enabled,"Subtree child Light changed");
    PrefabValue definition;Need(CapturePrefabSubtree(source,sourceRoot,definition,error),error.c_str());const auto definitionBytes=definition.dump();
    EntityWorld world;const auto outside=world.CreateEntity("Outside");EntitySelection selected;EntityHistory history;EntityId root=0;
    const std::string guid(32,'a');Need(InstantiatePrefab(world,definition,guid,outside,selected,history,root,error),error.c_str());
    EntityId child=0;for(const auto& e:world.GetEntities())if(e.parent==root)child=e.id;
    Need(child && world.Find(root)->hasMaterial && world.Find(child)->hasLight && world.Find(child)->light.kind==LightKind::Spot,"Prefab link instantiation lost components");
    PrefabOverrideReport report;Need(InspectPrefabOverrides(world,root,report,error) && report.entries.empty(),"Fresh render Prefab reported overrides");
    const auto before=world.Capture();material.color.w=0.5f;spot.intensity=8;
    Need(world.SetMaterial(root,material) && world.SetLight(child,spot),"Render instance edit failed");
    Need(history.Record(before,world,root,root,error),error.c_str());history.Commit();
    const auto changed=world.Capture().dump();EntityId selection=root;
    Need(history.Undo(world,selection,error) && world.Capture().dump()==before.dump(),"Render Undo lost baseline");
    Need(history.Redo(world,selection,error) && world.Capture().dump()==changed,"Render Redo changed settings");
    WorldPlayback playback;Scripting::ScriptRuntime scripts;Physics::PhysicsWorld physics;
    Need(playback.Play(world),"Render playback start refused");
    world.RemoveLight(child);world.RemoveMaterial(root);world.Find(root)->active=false;
    Need(playback.Stop(world,scripts,physics,error) && world.Capture().dump()==changed,"Play Stop lost authored Light/Material snapshot");
    Need(InspectPrefabOverrides(world,root,report,error),error.c_str());
    const PrefabOverrideEntry* lightRow=nullptr;bool materialRow=false;
    for(const auto& row:report.entries){if(row.scene==child && row.path=="/light/intensity")lightRow=&row;if(row.scene==root && row.path=="/material/color")materialRow=true;}
    Need(lightRow && materialRow,"Render fields were absent from Prefab override report");
    EntityWorld reverted;Need(BuildPrefabFieldRevertCandidate(world,root,definition,guid,*lightRow,reverted,error),error.c_str());
    Need(reverted.Find(child)->light.intensity==5 && reverted.Find(root)->material.color.w==0.5f && world.Capture().dump()==changed,"Field Revert changed unrelated material/live World");
    EntityWorld runtime;const auto runtimeParent=runtime.CreateEntity("Runtime parent");EntityId runtimeRoot=0;
    Need(Prefab::InstantiateRuntimePrefab(runtime,definition,runtimeParent,runtimeRoot,error),error.c_str());
    Need(runtime.Find(runtimeRoot)->hasMaterial && runtime.Find(runtimeRoot)->material.lighting && runtime.Find(runtimeRoot)->prefabLink.is_null(),"Runtime Prefab lost material or retained editor link");
    bool runtimeSpot=false;for(const auto& e:runtime.GetEntities())if(e.parent==runtimeRoot)runtimeSpot=e.hasLight && e.light.kind==LightKind::Spot && !e.light.enabled && e.light.intensity==5;
    Need(runtimeSpot,"Runtime Prefab lost child Light");
    auto badDefinition=definition;badDefinition["world"]["entities"][1]["light"]["innerAngle"]=1.2;
    badDefinition["world"]["entities"][1]["light"]["outerAngle"]=0.2;
    PrefabValue normalized="retained";Need(!ValidatePrefabDocument(badDefinition,normalized,error) && normalized.is_string() && normalized.get<crude_json::string>()=="retained","Malformed render Prefab replaced normalized output");
    fixture=std::filesystem::temp_directory_path()/(L"kubo-render-components-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    Need(std::filesystem::create_directory(fixture),"Refuse existing file fixture");
    const auto path=fixture/L"照明と材質.scene";
    Need(world.Save(path,error,false),error.c_str());EntityWorld loaded;Need(loaded.Load(path,error) && loaded.Capture().dump()==changed,"Saved Unicode Scene lost authored rendering data");
    auto damaged=world.Capture();damaged["entities"][1]["light"]["type"]="unknown";
    Need(!loaded.Restore(damaged,error) && loaded.Capture().dump()==changed,"Malformed light replaced saved World");
    const auto snapshot=source.Capture();EntityWorld counted;
    allocations=0;counting=true;const bool success=counted.Restore(snapshot,error);counting=false;
    Need(success,"Counted render restoration failed");const auto positions=allocations;
    EntityWorld original;original.CreateEntity("Unrelated");const auto originalBytes=original.Capture().dump();const auto originalHigh=original.AllocationHighWatermark();
    for(size_t fault=0;fault<positions;++fault){auto output=original;failAfter=static_cast<long>(fault);const bool ok=output.Restore(snapshot,error);failAfter=-1;
        Need(!ok && output.Capture().dump()==originalBytes && output.AllocationHighWatermark()==originalHigh,"Allocation failure published partial render World/IDs");}
    Need(source.Capture().dump()==sourceBytes && source.AllocationHighWatermark()==sourceHigh && definition.dump()==definitionBytes,"Render operations mutated source/baseline");
    std::filesystem::remove_all(fixture);fixture.clear();
    std::cout<<"PASS: Light/Material subtree clone, linked Prefab/field overrides+selected Revert, Undo/Redo/Play Stop, runtime baked factory, Unicode Scene save/load, source preservation and Restore allocation isolation positions="<<positions<<" (not rendered lighting)\n";return 0;
}catch(const std::exception& e){failAfter=-1;counting=false;std::cerr<<"FAIL: "<<e.what()<<'\n';if(!fixture.empty())std::cerr<<"Retained render fixture: "<<fixture<<'\n';return 1;}}
