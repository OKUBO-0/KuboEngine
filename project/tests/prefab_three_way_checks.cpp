#include "engine/editor/PrefabThreeWay.h"
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
namespace {
using namespace Engine::Editor;using namespace Engine::Scene;
std::string Fingerprint(const PrefabThreeWayReport& report){
    PrefabValue value;value["root"]=double(report.root);value["guid"]=report.sourceGuid;value["entries"]=crude_json::array{};
    for(const auto& entry:report.entries){PrefabValue row;row["kind"]=double(int(entry.kind));row["change"]=double(int(entry.change));row["local"]=double(entry.local);row["scene"]=double(entry.scene);row["name"]=entry.name;row["path"]=entry.path;
        for(const auto& pair:{std::pair{"baseline",&entry.baseline},std::pair{"source",&entry.source},std::pair{"instance",&entry.instance}}){row[std::string(pair.first)+"Present"]=pair.second->has_value();if(*pair.second)row[pair.first]=**pair.second;}
        value["entries"].push_back(std::move(row));}
    return value.dump();
}
}
int main(){try {
    using C=PrefabThreeWayChange;std::string error;const std::string guid="0123456789abcdef0123456789abcdef";
    const std::optional<std::string> absent,null="null",one="1",two="2";
    Require(ClassifyPrefabThreeWayValues(absent,absent,absent)==C::Unchanged && ClassifyPrefabThreeWayValues(null,null,absent)==C::InstanceOnly && ClassifyPrefabThreeWayValues(null,absent,null)==C::SourceOnly && ClassifyPrefabThreeWayValues(one,two,two)==C::Converged && ClassifyPrefabThreeWayValues(one,two,absent)==C::Conflict,"Missing/null or classification mismatch");
    EntityWorld authored;authored.CreateEntity("Root");for(int i=2;i<=6;++i)authored.CreateEntity("Child"+std::to_string(i),1);
    authored.Find(1)->hasScript=true;authored.Find(1)->script.assetPath="Scripts/check.lua";authored.Find(1)->script.properties["a~/b"]=1.0;authored.Find(1)->script.properties["flag"]=false;
    PrefabValue baseline;Require(CapturePrefabSubtree(authored,1,baseline,error),error.c_str());EntityWorld world;world.CreateEntity("Outside");EntitySelection selection;EntityHistory history;EntityId root=0;
    Require(InstantiatePrefab(world,baseline,guid,1,selection,history,root,error),error.c_str());PrefabThreeWayReport report;
    Require(InspectPrefabThreeWay(world,root,baseline,guid,report,error) && report.entries.empty(),"Fresh triple reported a change");
    world.Find(root)->name="Instance 日本語 %s";world.Find(root)->transform.position={7,0,0};world.Find(root)->transform.scale={2,2,2};world.Find(root)->active=false;
    world.Find(root)->script.properties.erase("a~/b");world.Find(root)->script.properties.erase("flag");world.Find(root)->script.properties["instanceOnly"]=5.0;
    Require(world.DestroyEntity(root+3) && world.DestroyEntity(root+4) && world.DestroyEntity(root+5),"Instance deletion setup failed");const auto added=world.CreateEntity("Instance addition",root);
    authored.Find(1)->name="Source name";authored.Find(1)->transform.position={9,0,0};authored.Find(1)->transform.rotation={0,1,0};authored.Find(1)->active=false;
    authored.Find(1)->script.properties["a~/b"]=2.0;authored.Find(1)->script.properties["sourceOnly"]=4.0;
    Require(authored.DestroyEntity(3) && authored.DestroyEntity(5),"Source deletion setup failed");authored.Find(6)->name="Source edited deleted instance";Require(authored.CreateEntity("Source addition",1)==7,"Source identity fixture changed");
    PrefabValue source;Require(CapturePrefabSubtree(authored,1,source,error),error.c_str());const auto worldBefore=world.Capture().dump(),sourceBefore=source.dump(),baselineBefore=baseline.dump();const auto selected=selection.Capture();const auto oldHistory=history;
    Require(InspectPrefabThreeWay(world,root,source,guid,report,error),error.c_str());
    const auto find=[&](EntityId local,const std::string& path)->const PrefabThreeWayEntry&{for(const auto& entry:report.entries)if(entry.local==local && entry.path==path)return entry;throw std::runtime_error("Expected three-way row missing");};
    Require(find(1,"/transform/position").change==C::Conflict && find(1,"/transform/rotation").change==C::SourceOnly && find(1,"/transform/scale").change==C::InstanceOnly && find(1,"/active").change==C::Converged,"Atomic TRS/active triple mismatch");
    Require(find(1,"/name").kind==PrefabThreeWayKind::InstanceName && find(1,"/name").change==C::Conflict,"Instance name policy was hidden");
    Require(find(1,"/script/properties/a~0~1b").change==C::Conflict && !find(1,"/script/properties/a~0~1b").instance && find(1,"/script/properties/flag").baseline==std::optional<std::string>("false") && !find(1,"/script/properties/flag").instance && find(1,"/script/properties/flag").change==C::InstanceOnly,"Escaped field deletion mismatch");
    Require(find(1,"/script/properties/sourceOnly").change==C::SourceOnly && find(1,"/script/properties/instanceOnly").change==C::InstanceOnly,"Added property classification mismatch");
    Require(find(3,"").kind==PrefabThreeWayKind::EntityPresence && find(3,"").change==C::SourceOnly && find(3,"").scene==root+2 && find(4,"").change==C::InstanceOnly && find(5,"").change==C::Converged && find(6,"").change==C::Conflict,"Delete/modify/tombstone classification mismatch");
    Require(find(7,"").change==C::SourceOnly && find(7,"").scene==0 && find(8,"").change==C::InstanceOnly && find(8,"").scene==added,"Incoming identity reservation aliased source/instance additions");
    Require(world.Capture().dump()==worldBefore && source.dump()==sourceBefore && baseline.dump()==baselineBefore && selection.Capture()==selected && history.HasSameState(oldHistory),"Three-way inspection mutated inputs");
    const auto retained=Fingerprint(report);auto reordered=world.Capture();std::reverse(reordered["entities"].get<crude_json::array>().begin(),reordered["entities"].get<crude_json::array>().end());EntityWorld shuffled;Require(shuffled.Restore(reordered,error),error.c_str());auto reversedSource=source;std::reverse(reversedSource["world"]["entities"].get<crude_json::array>().begin(),reversedSource["world"]["entities"].get<crude_json::array>().end());PrefabThreeWayReport reorderedReport;Require(InspectPrefabThreeWay(shuffled,root,reversedSource,guid,reorderedReport,error) && Fingerprint(reorderedReport)==retained,"Storage ordering changed the triple report");
    auto bad=source;bad["root"]=2.0;Require(!InspectPrefabThreeWay(world,root,bad,guid,report,error) && Fingerprint(report)==retained,"Changed root replaced report");
    bad=source;bad["format"]="unknown";Require(!InspectPrefabThreeWay(world,root,bad,guid,report,error) && Fingerprint(report)==retained,"Invalid schema replaced report");
    Require(!InspectPrefabThreeWay(world,root,source,"fedcba9876543210fedcba9876543210",report,error) && Fingerprint(report)==retained && !InspectPrefabThreeWay(world,1,source,guid,report,error) && Fingerprint(report)==retained,"GUID/unlinked refusal replaced report");
    EntityWorld changedRootSource;changedRootSource.CreateEntity("Outside");changedRootSource.CreateEntity("New source root");PrefabValue changedRootDefinition;
    Require(CapturePrefabSubtree(changedRootSource,2,changedRootDefinition,error) && !InspectPrefabThreeWay(world,root,changedRootDefinition,guid,report,error) && Fingerprint(report)==retained,"Valid changed root identity replaced report");
    // Valid small definitions can still expand into an oversized report when a
    // large Entity name is repeated in many field rows. Refuse, never truncate.
    EntityWorld budgetSource;budgetSource.CreateEntity(std::string(250000,'N'));budgetSource.Find(1)->hasScript=true;budgetSource.Find(1)->script.assetPath="Scripts/check.lua";
    for(int i=0;i<64;++i)budgetSource.Find(1)->script.properties["p"+std::to_string(i)]=0.0;
    PrefabValue budgetDefinition;Require(CapturePrefabSubtree(budgetSource,1,budgetDefinition,error),error.c_str());EntityWorld budgetWorld;EntitySelection budgetSelection;EntityHistory budgetHistory;EntityId budgetRoot=0;
    Require(InstantiatePrefab(budgetWorld,budgetDefinition,guid,0,budgetSelection,budgetHistory,budgetRoot,error),error.c_str());
    for(int i=0;i<64;++i){budgetSource.Find(1)->script.properties["p"+std::to_string(i)]=1.0;budgetWorld.Find(budgetRoot)->script.properties["p"+std::to_string(i)]=2.0;}
    PrefabValue budgetIncoming;Require(CapturePrefabSubtree(budgetSource,1,budgetIncoming,error),error.c_str());const auto budgetBefore=budgetWorld.Capture().dump();
    Require(!InspectPrefabThreeWay(budgetWorld,budgetRoot,budgetIncoming,guid,report,error) && error=="Three-way report budget exceeded" && Fingerprint(report)==retained && budgetWorld.Capture().dump()==budgetBefore,"Oversized report was truncated/published or changed input");
    // Exhaustive observed ordinary allocation failures on a small comparison.
    EntityWorld smallSource;smallSource.CreateEntity("Small");PrefabValue smallDefinition;Require(CapturePrefabSubtree(smallSource,1,smallDefinition,error),error.c_str());EntityWorld compactWorld;EntitySelection smallSelection;EntityHistory smallHistory;EntityId smallRoot=0;Require(InstantiatePrefab(compactWorld,smallDefinition,guid,0,smallSelection,smallHistory,smallRoot,error),error.c_str());compactWorld.Find(smallRoot)->name="Mine";const auto smallBefore=compactWorld.Capture().dump(),smallSourceBefore=smallDefinition.dump();
    allocations=0;failAllocation=0;countAllocations=true;PrefabThreeWayReport observed;const bool success=InspectPrefabThreeWay(compactWorld,smallRoot,smallDefinition,guid,observed,error);countAllocations=false;Require(success,"Allocation count comparison failed");const auto positions=allocations;
    for(size_t position=1;position<=positions;++position){allocations=0;failAllocation=position;countAllocations=true;bool accepted=false;try{accepted=InspectPrefabThreeWay(compactWorld,smallRoot,smallDefinition,guid,report,error);}catch(...){countAllocations=false;throw;}countAllocations=false;Require(!accepted && Fingerprint(report)==retained && compactWorld.Capture().dump()==smallBefore && smallDefinition.dump()==smallSourceBefore,"Allocation failure published output or mutated input");}
    std::cout<<"PREFAB_THREE_WAY_ALLOCATION_POSITIONS="<<positions<<'\n';std::cout<<"PASS: read-only three-way field/name/presence classification, atomic arrays, missing/null, escaped paths, delete/modify conflicts, incoming identity reservations, ordering and failed-output/allocation isolation (core only)\n";return 0;
}catch(const std::exception& exception){countAllocations=false;std::cerr<<"FAIL: "<<exception.what()<<'\n';return 1;}}
