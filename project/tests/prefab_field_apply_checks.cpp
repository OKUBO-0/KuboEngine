#include "engine/editor/PrefabFieldApplyPlan.h"
#include "engine/editor/PrefabInstance.h"
#include <iostream>
#include <cstdlib>
#include <new>
namespace {bool countAllocations=false;size_t allocations=0,failAllocation=0;void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}}
void* operator new(size_t bytes){if(countAllocations && ++allocations==failAllocation){countAllocations=false;throw std::bad_alloc();}if(auto* memory=std::malloc(bytes?bytes:1))return memory;throw std::bad_alloc();}
void* operator new[](size_t bytes){return ::operator new(bytes);}
void operator delete(void* memory) noexcept{std::free(memory);}
void operator delete[](void* memory) noexcept{std::free(memory);}
void operator delete(void* memory,size_t) noexcept{std::free(memory);}
void operator delete[](void* memory,size_t) noexcept{std::free(memory);}
int main(){try {
    using namespace Engine::Editor;using namespace Engine::Scene;
    const std::string guid="0123456789abcdef0123456789abcdef";std::string error;
    EntityWorld source;source.CreateEntity("Root");source.CreateEntity("Child",1);source.CreateEntity("Leaf",1);
    source.Find(2)->hasScript=true;source.Find(2)->script.assetPath="Scripts/check.lua";source.Find(2)->script.properties["a~/b"]=1.0;source.Find(2)->script.properties["keep"]=true;
    PrefabValue definition;Require(CapturePrefabSubtree(source,1,definition,error),error.c_str());
    EntityWorld world;world.CreateEntity("Outside");EntitySelection selection;EntityHistory history;EntityId chosen=0,inherited=0,overridden=0,deleted=0;
    Require(InstantiatePrefab(world,definition,guid,1,selection,history,chosen,error) && InstantiatePrefab(world,definition,guid,1,selection,history,inherited,error) && InstantiatePrefab(world,definition,guid,1,selection,history,overridden,error) && InstantiatePrefab(world,definition,guid,1,selection,history,deleted,error),error.c_str());
    const auto initial=world;
    world.Find(chosen)->transform.position={7,8,9};world.Find(chosen)->name="Chosen name";world.Find(chosen+1)->script.properties["a~/b"]=2.0;
    world.Find(inherited)->name="Inherited name";world.Find(overridden)->name="Override name";world.Find(overridden)->transform.position={3,0,0};world.Find(overridden+1)->active=false;
    Require(world.DestroyEntity(chosen+2) && world.DestroyEntity(deleted+1),"Deletion fixture failed");const auto addition=world.CreateEntity("Keep instance addition",chosen);
    const auto inspect=[&](const EntityWorld& input,EntityId local,const std::string& path){PrefabOverrideReport report;Require(InspectPrefabOverrides(input,chosen,report,error),error.c_str());const auto found=std::find_if(report.entries.begin(),report.entries.end(),[&](const auto& entry){return entry.local==local && entry.path==path;});Require(found!=report.entries.end(),"Fixture has no requested difference");return *found;};
    const auto field=inspect(world,1,"/transform/position");
    auto current=definition;PrefabApplyFieldNode(current,1)->operator[]("name")="Current source name";(*PrefabApplyFieldNode(current,2))["active"]=false;
    auto sourceAddition=(*PrefabApplyFieldNode(current,3));sourceAddition["id"]=4.0;sourceAddition["name"]="Unrelated source addition";current["world"]["entities"].push_back(sourceAddition);
    std::reverse(current["world"]["entities"].get<crude_json::array>().begin(),current["world"]["entities"].get<crude_json::array>().end());
    const auto input=world.Capture().dump(),sourceInput=current.dump();const auto selected=selection.Capture();const auto originalHistory=history;
    std::unique_ptr<PrefabApplyPlan> plan;
    Require(BuildPrefabFieldApplyPlan(world,chosen,current,guid,field,plan,error),error.c_str());
    auto expected=current;(*PrefabApplyFieldNode(expected,1))["transform"]["position"]=crude_json::array{7.0,8.0,9.0};
    Require(plan->source.dump()==expected.dump() && plan->instances.size()==4,"Apply changed unselected source fields/structure/storage order");
    Require(plan->world.Find(chosen)->name=="Chosen name" && plan->world.Find(inherited)->name=="Inherited name" && plan->world.Find(overridden)->name=="Override name" && plan->world.Find(inherited)->transform.position.x==7 && plan->world.Find(overridden)->transform.position.x==3,"Field propagation lost names/independent atomic override");
    Require(plan->world.Find(addition) && !plan->world.Find(chosen+2) && !plan->world.Find(deleted+1) && plan->world.GetEntities().size()==world.GetEntities().size() && plan->world.Find(inherited+1)->active && !plan->world.Find(overridden+1)->active,"Apply propagated unrelated source changes/Entities or resurrected deletions");
    Require(plan->world.Find(chosen)->prefabLink["mapping"].dump()==world.Find(chosen)->prefabLink["mapping"].dump() && plan->world.AllocationHighWatermark()==world.AllocationHighWatermark(),"Field Apply changed mapping/high water");
    Require(plan->world.Find(chosen)->prefabLink["base"]["entities"][size_t(0)]["name"].get<crude_json::string>()=="Root","Unselected baseline name refreshed");
    Require(world.Capture().dump()==input && current.dump()==sourceInput && selection.Capture()==selected && history.HasSameState(originalHistory),"Plan mutated live inputs");
    auto sameSource=current;(*PrefabApplyFieldNode(sameSource,1))["transform"]["position"]=crude_json::array{7.0,8.0,9.0};
    std::unique_ptr<PrefabApplyPlan> same;Require(BuildPrefabFieldApplyPlan(world,chosen,sameSource,guid,field,same,error) && same->source.dump()==sameSource.dump(),"Source-equal baseline refresh rewrote source");
    const auto name=inspect(world,1,"/name");Require(BuildPrefabFieldApplyPlan(world,chosen,current,guid,name,same,error) && (*PrefabApplyFieldNode(same->source,1))["name"].get<crude_json::string>()=="Chosen name" && same->world.Find(inherited)->name=="Inherited name",error.c_str());
    const auto property=inspect(world,2,"/script/properties/a~0~1b");auto withoutProperties=current;(*PrefabApplyFieldNode(withoutProperties,2))["script"].erase("properties");
    Require(BuildPrefabFieldApplyPlan(world,chosen,withoutProperties,guid,property,same,error) && (*PrefabApplyFieldNode(same->source,2))["script"]["properties"].get<crude_json::object>().size()==1 && same->world.Find(inherited+1)->script.properties.contains("keep"),"Missing object parent invented unselected source fields or removed instance properties");
    auto propertyWorld=world;propertyWorld.Find(chosen+1)->script.properties.erase("a~/b");const auto missing=inspect(propertyWorld,2,"/script/properties/a~0~1b");
    Require(BuildPrefabFieldApplyPlan(propertyWorld,chosen,current,guid,missing,same,error) && !(*PrefabApplyFieldNode(same->source,2))["script"]["properties"].contains("a~/b") && !same->world.Find(inherited+1)->script.properties.contains("a~/b") && !same->world.Find(deleted+1),"Selected missing property/deleted target handling failed");
    auto childWorld=world;childWorld.Find(chosen+1)->transform.position={2,3,4};childWorld.Find(overridden+1)->transform.position={0,5,0};
    const auto childField=inspect(childWorld,2,"/transform/position");
    Require(BuildPrefabFieldApplyPlan(childWorld,chosen,current,guid,childField,same,error) && same->world.Find(inherited+1)->transform.position.x==2 && same->world.Find(overridden+1)->transform.position.x==0 && same->world.Find(overridden+1)->transform.position.y==5 && !same->world.Find(deleted+1),"Atomic child inheritance/override/deletion failed");
    const auto held=plan.get();const auto heldSource=plan->source.dump(),heldWorld=plan->world.Capture().dump();
    auto stale=world;stale.Find(chosen)->transform.position.x=8;
    Require(!BuildPrefabFieldApplyPlan(stale,chosen,current,guid,field,plan,error) && plan.get()==held && plan->source.dump()==heldSource && plan->world.Capture().dump()==heldWorld,"Stale target altered held plan");
    Require(!BuildPrefabFieldApplyPlan(world,chosen,current,"11111111111111111111111111111111",field,plan,error) && plan.get()==held,"GUID mismatch altered plan");
    auto invalid=current;(*PrefabApplyFieldNode(invalid,1))["unsupported"]=true;
    Require(!BuildPrefabFieldApplyPlan(world,chosen,invalid,guid,field,plan,error) && plan.get()==held,"Invalid source schema replaced plan");
    auto rootChanged=current;rootChanged["root"]=100.0;
    for(auto& node:rootChanged["world"]["entities"].get<crude_json::array>()){if(node["id"].get<crude_json::number>()==1)node["id"]=100.0;if(node["parent"].get<crude_json::number>()==1)node["parent"]=100.0;}
    Require(!BuildPrefabFieldApplyPlan(world,chosen,rootChanged,guid,field,plan,error) && plan.get()==held,"Changed source root identity replaced plan");
    auto missingSourceTarget=current;auto& nodes=missingSourceTarget["world"]["entities"].get<crude_json::array>();nodes.erase(std::remove_if(nodes.begin(),nodes.end(),[](const auto& node){return node["id"].template get<crude_json::number>()==2.0;}),nodes.end());
    Require(!BuildPrefabFieldApplyPlan(world,chosen,missingSourceTarget,guid,property,plan,error) && plan.get()==held,"Source-deleted target silently became a source addition");
    auto unknownBaseline=world;auto& oldLink=unknownBaseline.Find(inherited)->prefabLink;
    auto oldRoot=oldLink["base"]["entities"][size_t(0)];oldLink["base"]["entities"]=crude_json::array{oldRoot};auto rootPair=oldLink["mapping"][size_t(0)];oldLink["mapping"]=crude_json::array{rootPair};
    Require(!BuildPrefabFieldApplyPlan(unknownBaseline,chosen,current,guid,property,plan,error) && plan.get()==held,"Unknown sibling baseline target was silently created");
    PrefabOverrideReport structural;Require(InspectPrefabOverrides(world,chosen,structural,error),error.c_str());const auto added=std::find_if(structural.entries.begin(),structural.entries.end(),[](const auto& entry){return entry.kind==PrefabOverrideKind::EntityAdded;});Require(added!=structural.entries.end(),"Missing structural fixture");
    Require(!BuildPrefabFieldApplyPlan(world,chosen,current,guid,*added,plan,error) && plan.get()==held,"Structural override accepted as a field");
    // Incoming ID 4 is a source Entity, not the elected addition assigned local
    // ID 4 by a capture without incoming reservations. Never alias the parent.
    auto collision=world;collision.Find(chosen+1)->parent=addition;const auto parentCollision=inspect(collision,2,"/parent");
    Require(!BuildPrefabFieldApplyPlan(collision,chosen,current,guid,parentCollision,plan,error) && plan.get()==held,"Instance-added parent aliased unrelated source identity");
    auto late=initial;late.Find(chosen+1)->parent=chosen+2;late.Find(inherited+2)->parent=inherited+1;
    const auto parent=inspect(late,2,"/parent");
    Require(!BuildPrefabFieldApplyPlan(late,chosen,definition,guid,parent,plan,error) && plan.get()==held && plan->source.dump()==heldSource && plan->world.Capture().dump()==heldWorld,"Late sibling cycle partially published source/World/report");
    EntityWorld smallSource;smallSource.CreateEntity("Baseline");PrefabValue smallDefinition;Require(CapturePrefabSubtree(smallSource,1,smallDefinition,error),error.c_str());
    EntityWorld allocationWorld;EntitySelection ss;EntityHistory sh;EntityId sr=0;Require(InstantiatePrefab(allocationWorld,smallDefinition,guid,0,ss,sh,sr,error),error.c_str());allocationWorld.Find(sr)->name="Changed";
    PrefabOverrideReport smallReport;Require(InspectPrefabOverrides(allocationWorld,sr,smallReport,error),error.c_str());const auto smallField=smallReport.entries.front();
    std::unique_ptr<PrefabApplyPlan> probe;allocations=0;failAllocation=0;countAllocations=true;const bool ok=BuildPrefabFieldApplyPlan(allocationWorld,sr,smallDefinition,guid,smallField,probe,error);countAllocations=false;
    const auto positions=allocations;Require(ok && positions>0,"Allocation probe failed");
    for(size_t position=1;position<=positions;++position){allocations=0;failAllocation=position;countAllocations=true;const bool applied=BuildPrefabFieldApplyPlan(allocationWorld,sr,smallDefinition,guid,smallField,plan,error);countAllocations=false;Require(!applied && plan.get()==held && plan->source.dump()==heldSource && plan->world.Capture().dump()==heldWorld,"Allocation failure replaced held plan");}
    std::cout<<"FIELD_APPLY_ALLOCATION_POSITIONS="<<positions<<'\n';
    std::cout<<"PASS: field-only source and all-instance candidates, selected-only baselines, independent atomic overrides/names/deletions/additions, incoming identity reservation, source-equal refresh, stale/late conflict and owned-output allocation isolation (core only)\n";return 0;
}catch(const std::exception& exception){countAllocations=false;std::cerr<<"FAIL: "<<exception.what()<<'\n';return 1;}}
