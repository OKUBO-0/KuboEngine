#include "engine/editor/PrefabEntityRemoval.h"
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
    using namespace Engine::Editor;using namespace Engine::Scene;std::string error;const std::string guid="0123456789abcdef0123456789abcdef";
    EntityWorld authored;authored.CreateEntity("Root");authored.CreateEntity("Deleted parent",1);authored.CreateEntity("Deleted child",2);authored.CreateEntity("Unselected deletion",1);authored.CreateEntity("Kept child",1);
    PrefabValue definition;Require(CapturePrefabSubtree(authored,1,definition,error),error.c_str());
    EntityWorld world;const auto external=world.CreateEntity("External");EntitySelection selection;EntityHistory history;EntityId root=0,other=0;
    Require(InstantiatePrefab(world,definition,guid,external,selection,history,root,error) && InstantiatePrefab(world,definition,guid,external,selection,history,other,error),error.c_str());history.Clear();
    world.Find(root)->name="Independent root";world.Find(root)->transform.scale={2,2,2};const auto extra=world.CreateEntity("Independent addition",root+4);
    Require(selection.Assign(world,{external,root,root+1,root+2},root+2,error),error.c_str());
    auto incoming=definition;auto& nodes=incoming["world"]["entities"].get<crude_json::array>();
    nodes.erase(std::remove_if(nodes.begin(),nodes.end(),[](const auto& node){const auto id=node["id"].template get<crude_json::number>();return id==2 || id==3 || id==4;}),nodes.end());
    incoming["world"]["entities"][size_t(0)]["active"]=false;
    const auto rowFor=[&](const EntityWorld& input,const PrefabValue& source,EntityId local){PrefabThreeWayReport report;Require(InspectPrefabThreeWay(input,root,source,guid,report,error),error.c_str());for(const auto& row:report.entries)if(row.local==local && row.kind==PrefabThreeWayKind::EntityPresence)return row;throw std::runtime_error("Removal fixture row missing");};
    const auto row=rowFor(world,incoming,2);const auto before=world.Capture().dump(),sourceBefore=incoming.dump();const auto selected=selection.Capture();const auto high=world.AllocationHighWatermark();
    PrefabEntityRemovalPlan plan;plan.world.CreateEntity("Held output");Require(BuildPrefabEntityRemovalPlan(world,root,incoming,guid,row,plan,error),error.c_str());
    Require(!plan.RequiresApproval() && plan.removals.size()==2 && plan.removedEntities==std::vector<EntityId>{root+1,root+2} && !plan.world.Find(root+1) && !plan.world.Find(root+2) && plan.world.Find(root+3) && plan.world.Find(root+4) && plan.world.Find(extra) && plan.world.Find(other+1) && plan.world.Find(other)->prefabLink["base"].dump()==definition["world"].dump() && plan.world.Find(root)->parent==external && plan.world.Find(root)->name=="Independent root" && plan.world.Find(root)->transform.scale.x==2 && plan.world.Find(root)->active && plan.world.AllocationHighWatermark()==high && world.Capture().dump()==before,"Removal closure overwrote independent fields/other root/addition/external parent/unselected deletion/IDs or input");
    EntityDocumentState document;document.Opened(world,"removal.scene");Require(AdoptPrefabEntityRemoval(world,root,incoming,guid,row,selection,history,error,&document),error.c_str());
    const auto after=world.Capture().dump();Require(document.IsDirty() && selection.Ids()==std::vector<EntityId>{external,root} && selection.Primary()==root && history.UndoCount()==1 && incoming.dump()==sourceBefore,"Removal selection/history/dirty/source failed");
    Require(history.Undo(world,selection,error,&document) && world.Capture().dump()==before && selection.Capture()==selected && !document.IsDirty() && history.Redo(world,selection,error,&document) && world.Capture().dump()==after && document.IsDirty() && history.Undo(world,selection,error,&document),"Removal Undo/Redo lost full selection/dirty");
    EntityWorld held;held.CreateEntity("Sentinel");const auto heldBefore=held.Capture().dump();auto invented=row;invented.baseline="invented";
    Require(!BuildPrefabEntityRemovalCandidate(world,root,incoming,guid,invented,held,error) && held.Capture().dump()==heldBefore && !BuildPrefabEntityRemovalCandidate(world,root,incoming,"wrong-guid",row,held,error) && !BuildPrefabEntityRemovalCandidate(world,root,incoming,guid,row,world,error) && world.Capture().dump()==before,"Removal invented/GUID/alias guard failed");
    auto changed=world;changed.Find(root+2)->name="Independent child edit";const auto target=rowFor(changed,incoming,2);
    Require(BuildPrefabEntityRemovalPlan(changed,root,incoming,guid,target,plan,error) && plan.RequiresApproval() && plan.removals[1].change==PrefabThreeWayChange::Conflict,"Inherited parent concealed descendant conflict");
    Require(!BuildPrefabEntityRemovalCandidate(changed,root,incoming,guid,target,held,error) && held.Capture().dump()==heldBefore && BuildPrefabEntityRemovalCandidate(changed,root,incoming,guid,target,held,error,true) && !held.Find(root+2),"Removal descendant conflict approval ignored");
    changed.Find(root+1)->transform.position.x=1;const auto directConflict=rowFor(changed,incoming,2);Require(directConflict.change==PrefabThreeWayChange::Conflict && !BuildPrefabEntityRemovalCandidate(changed,root,incoming,guid,directConflict,held,error),"Removal direct conflict silently discarded edit");
    // Instance-owned children remain protected even with conflict approval.
    auto protectedWorld=world;const auto protectedChild=protectedWorld.CreateEntity("Protected instance child",root+2);const auto protectedBefore=protectedWorld.Capture().dump();
    const auto retainedPlan=plan.world.Capture().dump();Require(!BuildPrefabEntityRemovalPlan(protectedWorld,root,incoming,guid,rowFor(protectedWorld,incoming,2),plan,error) && plan.world.Capture().dump()==retainedPlan && !BuildPrefabEntityRemovalCandidate(protectedWorld,root,incoming,guid,rowFor(protectedWorld,incoming,2),held,error,true) && protectedWorld.Capture().dump()==protectedBefore && protectedWorld.Find(protectedChild),"Removal deleted instance-owned dependency or changed previous plan on rejection");
    // Source-reparented surviving child requires explicit parent adoption first.
    auto surviving=definition;auto& survivingNodes=surviving["world"]["entities"].get<crude_json::array>();survivingNodes.erase(survivingNodes.begin()+1);surviving["world"]["entities"][size_t(1)]["parent"]=1.0;
    Require(!BuildPrefabEntityRemovalCandidate(world,root,surviving,guid,rowFor(world,surviving,2),held,error,true),"Removal deleted surviving source child");
    auto absent=world;Require(absent.DestroyEntity(root+2) && absent.DestroyEntity(root+1),"Converged fixture failed");const auto absentRow=rowFor(absent,incoming,2);
    Require(absentRow.change==PrefabThreeWayChange::Converged && BuildPrefabEntityRemovalPlan(absent,root,incoming,guid,absentRow,plan,error) && plan.removedEntities.empty() && plan.removals.size()==2 && !plan.RequiresApproval(),"Converged deletion baseline cleanup failed");
    const auto historyBefore=history;EntityDocumentState pending;pending.Opened(world,"pending.scene");Require(pending.Request(EntityDocumentState::Action::New),"Pending fixture setup failed");const auto pendingBefore=pending;
    Require(!AdoptPrefabEntityRemoval(world,root,incoming,guid,row,selection,history,error,&pending) && world.Capture().dump()==before && history.HasSameState(historyBefore) && pending.HasSameState(pendingBefore) && selection.Capture()==selected,"Pending document action partially removed Entities");
    EntityHistory tiny(1,1);const auto documentBefore=document;Require(!AdoptPrefabEntityRemoval(world,root,incoming,guid,row,selection,tiny,error,&document) && world.Capture().dump()==before && selection.Capture()==selected && document.HasSameState(documentBefore) && tiny.UndoCount()==0,"History budget partially removed Entities");
    Require(!BuildPrefabEntityRemovalCandidate(world,root,definition,guid,row,held,error),"Stale restored source accepted deletion");
    // A moved descendant appears through the baseline graph even when no
    // longer beneath the target in the visible graph. Review must disclose it.
    auto moved=world;moved.Find(root+2)->parent=root+4;
    Require(BuildPrefabEntityRemovalPlan(moved,root,incoming,guid,rowFor(moved,incoming,2),plan,error) && plan.RequiresApproval() && plan.removedEntities==std::vector<EntityId>{root+1,root+2} && !BuildPrefabEntityRemovalCandidate(moved,root,incoming,guid,rowFor(moved,incoming,2),held,error),"Removal missed moved baseline descendant conflict");
    auto attached=world;attached.Find(root+4)->parent=root+1;
    Require(!BuildPrefabEntityRemovalCandidate(attached,root,incoming,guid,rowFor(attached,incoming,2),held,error,true),"Removal missed protected visible-graph dependency");
    // Deletion needs no fresh Scene IDs, even with an exhausted allocator.
    auto exhaustedSnapshot=world.Capture();auto highNode=exhaustedSnapshot["entities"][size_t(0)];highNode["id"]=double(EntityWorld::MaximumEntityId);highNode["name"]="High-water sentinel";exhaustedSnapshot["entities"].push_back(highNode);
    EntityWorld exhausted;Require(exhausted.Restore(exhaustedSnapshot,error) && exhausted.DestroyEntity(EntityWorld::MaximumEntityId),error.c_str());
    Require(BuildPrefabEntityRemovalCandidate(exhausted,root,incoming,guid,rowFor(exhausted,incoming,2),held,error) && held.AllocationHighWatermark()==EntityWorld::MaximumEntityId+1 && !held.Find(root+1),"Removal allocated/reused identities at exhausted high-water");
    EntitySelection absentSelection;absentSelection.Replace(root);EntityHistory absentHistory;EntityDocumentState absentDocument;absentDocument.Opened(absent,"absent.scene");const auto absentBefore=absent.Capture().dump();
    Require(AdoptPrefabEntityRemoval(absent,root,incoming,guid,absentRow,absentSelection,absentHistory,error,&absentDocument) && absentDocument.IsDirty() && absent.Find(root)->prefabLink["mapping"].get<crude_json::array>().size()==3 && absentHistory.Undo(absent,absentSelection,error,&absentDocument) && absent.Capture().dump()==absentBefore && !absentDocument.IsDirty(),"Converged baseline-only removal command/history failed");
    auto aliasPlan=plan;const auto aliasBefore=aliasPlan.world.Capture().dump();
    Require(!BuildPrefabEntityRemovalPlan(aliasPlan.world,root,incoming,guid,row,aliasPlan,error) && aliasPlan.world.Capture().dump()==aliasBefore,"Removal plan input/output alias mutated World");
    // Single ordinary allocation failures must preserve deleted selection,
    // saved baseline and pending history until all preparation has completed.
    EntityWorld compactAuthored;compactAuthored.CreateEntity("Compact root");compactAuthored.CreateEntity("Removed child",1);PrefabValue compactDefinition;
    Require(CapturePrefabSubtree(compactAuthored,1,compactDefinition,error),error.c_str());
    EntityWorld compactWorld;EntitySelection compactSelection;EntityHistory compactHistory;EntityId compactRoot=0;
    Require(InstantiatePrefab(compactWorld,compactDefinition,guid,0,compactSelection,compactHistory,compactRoot,error) && compactSelection.Assign(compactWorld,{compactRoot,compactRoot+1},compactRoot+1,error),error.c_str());compactHistory.Clear();
    const auto ordinaryBefore=compactWorld.Capture();compactWorld.Find(compactRoot)->name="Pending edit";
    Require(compactHistory.Record(ordinaryBefore,compactWorld,compactSelection.Capture(),compactSelection.Capture(),error),error.c_str());
    auto compactIncoming=compactDefinition;compactIncoming["world"]["entities"].get<crude_json::array>().pop_back();
    PrefabThreeWayReport compactReport;Require(InspectPrefabThreeWay(compactWorld,compactRoot,compactIncoming,guid,compactReport,error),error.c_str());
    const auto compactFound=std::find_if(compactReport.entries.begin(),compactReport.entries.end(),[](const auto& entry){return entry.local==2 && entry.kind==PrefabThreeWayKind::EntityPresence;});Require(compactFound!=compactReport.entries.end(),"Compact removal row missing");const auto compactRow=*compactFound;
    EntityDocumentState compactDocument;compactDocument.Opened(compactWorld,"allocation-removal.scene");const auto compactBefore=compactWorld.Capture().dump(),compactSourceBefore=compactIncoming.dump();const auto compactSelected=compactSelection.Capture();const auto compactHigh=compactWorld.AllocationHighWatermark();
    auto observedWorld=compactWorld;auto observedSelection=compactSelection;auto observedHistory=compactHistory;auto observedDocument=compactDocument;
    allocations=0;failAllocation=0;countAllocations=true;const bool success=AdoptPrefabEntityRemoval(observedWorld,compactRoot,compactIncoming,guid,compactRow,observedSelection,observedHistory,error,&observedDocument);countAllocations=false;
    Require(success && observedSelection.Ids()==std::vector<EntityId>{compactRoot} && observedDocument.IsDirty() && observedHistory.UndoCount()==2,"Counted removal failed or merged pending ordinary edit");const auto positions=allocations;
    for(size_t fault=1;fault<=positions;++fault){auto testedWorld=compactWorld;auto testedSelection=compactSelection;auto testedHistory=compactHistory;auto testedDocument=compactDocument;
        allocations=0;failAllocation=fault;countAllocations=true;const bool accepted=AdoptPrefabEntityRemoval(testedWorld,compactRoot,compactIncoming,guid,compactRow,testedSelection,testedHistory,error,&testedDocument);countAllocations=false;
        Require(!accepted && !error.empty() && testedWorld.Capture().dump()==compactBefore && testedWorld.AllocationHighWatermark()==compactHigh && testedSelection.Capture()==compactSelected && testedHistory.HasSameState(compactHistory) && testedDocument.HasSameState(compactDocument) && compactIncoming.dump()==compactSourceBefore,"Removal allocation failure deleted World/selection or committed pending history/document");
    }
    Require(observedHistory.Undo(observedWorld,observedSelection,error,&observedDocument) && observedWorld.Capture().dump()==compactBefore && observedSelection.Capture()==compactSelected && !observedDocument.IsDirty() && observedHistory.Redo(observedWorld,observedSelection,error,&observedDocument) && observedSelection.Ids()==std::vector<EntityId>{compactRoot},"Counted removal replay lost full selection/saved baseline");
    std::cout<<"ENTITY_REMOVAL_COMMAND_ALLOCATION_POSITIONS="<<positions<<'\n';
    std::cout<<"PASS: selective source Entity removal closure, descendant conflict review/approval, protected instance/source dependencies, converged baseline cleanup, stable/exhausted IDs/unrelated fields/other roots, full-selection Undo/Redo/dirty, stale/GUID/alias/pending/budget and command allocation isolation (core only)\n";return 0;
}catch(const std::exception& exception){countAllocations=false;std::cerr<<"FAIL: "<<exception.what()<<'\n';return 1;}}
