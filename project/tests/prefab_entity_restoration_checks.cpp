#include "engine/editor/PrefabEntityRestoration.h"
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
    EntityWorld authored;authored.CreateEntity("Root");authored.CreateEntity("Deleted parent",1);authored.CreateEntity("Selected deleted child",2);authored.CreateEntity("Unselected deleted sibling",2);authored.CreateEntity("Kept node",1);
    PrefabValue definition;Require(CapturePrefabSubtree(authored,1,definition,error),error.c_str());EntityWorld world;world.CreateEntity("External");EntitySelection selection;EntityHistory history;EntityId root=0,other=0;
    Require(InstantiatePrefab(world,definition,guid,1,selection,history,root,error) && InstantiatePrefab(world,definition,guid,1,selection,history,other,error),error.c_str());history.Clear();
    Require(world.DestroyEntity(root+2) && world.DestroyEntity(root+3) && world.DestroyEntity(root+1),"Restoration deletion fixture failed");
    world.Find(root)->name="Independent root name";world.Find(root)->transform.scale={2,2,2};const auto extra=world.CreateEntity("Independent addition",root+4);Require(selection.Assign(world,{1,root,extra},extra,error),error.c_str());
    auto incoming=definition;incoming["world"]["entities"][size_t(0)]["active"]=false;
    const auto rowFor=[&](const EntityWorld& input,const PrefabValue& source,EntityId local){PrefabThreeWayReport report;Require(InspectPrefabThreeWay(input,root,source,guid,report,error),error.c_str());for(const auto& row:report.entries)if(row.local==local && row.kind==PrefabThreeWayKind::EntityPresence)return row;throw std::runtime_error("Restoration row missing");};
    const auto row=rowFor(world,incoming,3);const auto before=world.Capture().dump(),sourceBefore=incoming.dump();const auto high=world.AllocationHighWatermark();const auto selected=selection.Capture();
    PrefabEntityRestorationPlan plan;Require(BuildPrefabEntityRestorationPlan(world,root,incoming,guid,row,plan,error),error.c_str());
    Require(!plan.RequiresApproval() && plan.restorations.size()==2 && plan.restorations[0].local==2 && plan.restorations[1].local==3 && plan.world.Find(root+1) && plan.world.Find(root+2)->parent==root+1 && !plan.world.Find(root+3) && plan.world.Find(root)->name=="Independent root name" && plan.world.Find(root)->transform.scale.x==2 && plan.world.Find(root)->active && plan.world.Find(root)->parent==1 && plan.world.Find(extra) && plan.world.Find(other)->prefabLink["base"].dump()==definition["world"].dump() && plan.world.AllocationHighWatermark()==high && world.Capture().dump()==before,"Restoration imported descendants or changed stable IDs/fields/external parent/other root/additions/input");
    EntityDocumentState document;document.Opened(world,"restoration.scene");Require(AdoptPrefabEntityRestoration(world,root,incoming,guid,row,selection,history,error,&document),error.c_str());const auto after=world.Capture().dump();
    Require(document.IsDirty() && selection.Capture()==selected && incoming.dump()==sourceBefore && history.Undo(world,selection,error,&document) && world.Capture().dump()==before && !document.IsDirty() && history.Redo(world,selection,error,&document) && world.Capture().dump()==after && history.Undo(world,selection,error,&document),"Restoration source/full selection/Undo/Redo/dirty failed");
    EntityWorld held;held.CreateEntity("Sentinel");const auto heldBefore=held.Capture().dump();auto invented=row;invented.instance="invented";
    Require(!BuildPrefabEntityRestorationCandidate(world,root,incoming,guid,invented,held,error) && held.Capture().dump()==heldBefore && !BuildPrefabEntityRestorationCandidate(world,root,incoming,"wrong-guid",row,held,error) && !BuildPrefabEntityRestorationCandidate(world,root,incoming,guid,row,world,error) && world.Capture().dump()==before,"Restoration stale/GUID/alias guard failed");
    // Changed deleted parent requires approval even if the requested child is
    // InstanceOnly. A caller must preview all ancestors, not just the target.
    auto conflict=incoming;conflict["world"]["entities"][size_t(1)]["name"]="Updated deleted parent";
    Require(BuildPrefabEntityRestorationPlan(world,root,conflict,guid,rowFor(world,conflict,3),plan,error) && plan.RequiresApproval() && plan.restorations[0].change==PrefabThreeWayChange::Conflict && !BuildPrefabEntityRestorationCandidate(world,root,conflict,guid,rowFor(world,conflict,3),held,error) && held.Capture().dump()==heldBefore && BuildPrefabEntityRestorationCandidate(world,root,conflict,guid,rowFor(world,conflict,3),held,error,true) && held.Find(root+1)->name=="Updated deleted parent","Restoration concealed deleted-parent conflict");
    auto direct=conflict;direct["world"]["entities"][size_t(2)]["active"]=false;const auto directRow=rowFor(world,direct,3);Require(directRow.change==PrefabThreeWayChange::Conflict && !BuildPrefabEntityRestorationCandidate(world,root,direct,guid,directRow,held,error),"Restoration silently accepted changed deleted target");
    // Source can move the requested deleted node beneath a new source parent.
    // Only that new dependency and target are adopted; old deleted parent stays.
    auto reparented=incoming;auto newParent=reparented["world"]["entities"][size_t(4)];newParent["id"]=6.0;newParent["name"]="New source parent";reparented["world"]["entities"].push_back(newParent);reparented["world"]["entities"][size_t(2)]["parent"]=6.0;
    Require(BuildPrefabEntityRestorationPlan(world,root,reparented,guid,rowFor(world,reparented,3),plan,error) && plan.RequiresApproval() && plan.restorations.size()==2 && plan.restorations[0].local==6 && !plan.world.Find(root+1) && plan.world.Find(root+2)->parent==high && plan.world.Find(high)->name=="New source parent" && plan.world.Find(extra) && plan.world.AllocationHighWatermark()==high+1,"Restoration new-source dependency aliased instance addition or restored unrelated deleted parent");
    auto exhaustedSnapshot=world.Capture();auto highNode=exhaustedSnapshot["entities"][size_t(0)];highNode["id"]=double(EntityWorld::MaximumEntityId);highNode["name"]="High-water sentinel";exhaustedSnapshot["entities"].push_back(highNode);EntityWorld exhausted;
    Require(exhausted.Restore(exhaustedSnapshot,error) && exhausted.DestroyEntity(EntityWorld::MaximumEntityId),error.c_str());
    Require(BuildPrefabEntityRestorationCandidate(exhausted,root,incoming,guid,rowFor(exhausted,incoming,3),held,error) && held.AllocationHighWatermark()==EntityWorld::MaximumEntityId+1 && held.Find(root+2),"Reserved mapped IDs could not restore with exhausted allocator");const auto exhaustedHeld=held.Capture().dump();
    Require(!BuildPrefabEntityRestorationCandidate(exhausted,root,reparented,guid,rowFor(exhausted,reparented,3),held,error,true) && held.Capture().dump()==exhaustedHeld,"New parent at exhausted allocator modified output");
    EntityDocumentState pending;pending.Opened(world,"pending.scene");Require(pending.Request(EntityDocumentState::Action::New),"Pending restoration setup failed");const auto pendingBefore=pending;const auto historyBefore=history;
    Require(!AdoptPrefabEntityRestoration(world,root,incoming,guid,row,selection,history,error,&pending) && world.Capture().dump()==before && selection.Capture()==selected && history.HasSameState(historyBefore) && pending.HasSameState(pendingBefore),"Pending document partially restored Entities");
    EntityHistory tiny(1,1);const auto documentBefore=document;Require(!AdoptPrefabEntityRestoration(world,root,incoming,guid,row,selection,tiny,error,&document) && world.Capture().dump()==before && document.HasSameState(documentBefore) && selection.Capture()==selected,"History budget partially restored Entities");
    Require(!BuildPrefabEntityRestorationCandidate(held,root,incoming,guid,row,held,error),"Restoration output alias accepted");
    EntityWorld restored;Require(restored.Restore(PrefabValue::parse(after),error),error.c_str());const auto outputBefore=held.Capture().dump();
    Require(!BuildPrefabEntityRestorationCandidate(restored,root,incoming,guid,row,held,error) && held.Capture().dump()==outputBefore,"Already restored stale row changed output");
    EntitySelection invalid;invalid.Replace(root+2);
    Require(!AdoptPrefabEntityRestoration(world,root,incoming,guid,row,invalid,history,error,&document) && world.Capture().dump()==before && history.HasSameState(historyBefore) && invalid.Primary()==root+2,"Restoration silently made stale missing selection valid");
    auto anchorSource=incoming;anchorSource["world"]["entities"][size_t(2)]["parent"]=5.0;anchorSource["world"]["entities"][size_t(4)]["active"]=false;
    Require(BuildPrefabEntityRestorationCandidate(world,root,anchorSource,guid,rowFor(world,anchorSource,3),held,error,true) && held.Find(root+2)->parent==root+4 && held.Find(root+4)->active && !held.Find(root+1) && held.AllocationHighWatermark()==high,"Restoration reset existing anchor or restored unrelated old parent");
    auto aliasPlan=plan;const auto aliasBefore=aliasPlan.world.Capture().dump();Require(!BuildPrefabEntityRestorationPlan(aliasPlan.world,root,incoming,guid,row,aliasPlan,error) && aliasPlan.world.Capture().dump()==aliasBefore,"Restoration plan alias changed output");
    EntityWorld compactAuthored;compactAuthored.CreateEntity("Compact root");compactAuthored.CreateEntity("Deleted child",1);PrefabValue compactDefinition;
    Require(CapturePrefabSubtree(compactAuthored,1,compactDefinition,error),error.c_str());EntityWorld compactWorld;EntitySelection compactSelection;EntityHistory compactHistory;EntityId compactRoot=0;
    Require(InstantiatePrefab(compactWorld,compactDefinition,guid,0,compactSelection,compactHistory,compactRoot,error) && compactWorld.DestroyEntity(compactRoot+1),error.c_str());compactHistory.Clear();
    const auto compactExtra=compactWorld.CreateEntity("Unrelated instance addition",compactRoot);Require(compactSelection.Assign(compactWorld,{compactRoot,compactExtra},compactExtra,error),error.c_str());
    const auto ordinaryBefore=compactWorld.Capture();compactWorld.Find(compactRoot)->name="Pending ordinary edit";
    Require(compactHistory.Record(ordinaryBefore,compactWorld,compactSelection.Capture(),compactSelection.Capture(),error),error.c_str());
    PrefabThreeWayReport compactReport;Require(InspectPrefabThreeWay(compactWorld,compactRoot,compactDefinition,guid,compactReport,error),error.c_str());
    const auto compactFound=std::find_if(compactReport.entries.begin(),compactReport.entries.end(),[](const auto& entry){return entry.local==2 && entry.kind==PrefabThreeWayKind::EntityPresence;});Require(compactFound!=compactReport.entries.end(),"Compact restoration row missing");const auto compactRow=*compactFound;
    EntityDocumentState compactDocument;compactDocument.Opened(compactWorld,"allocation-restoration.scene");const auto compactBefore=compactWorld.Capture().dump(),compactSourceBefore=compactDefinition.dump();const auto compactSelected=compactSelection.Capture();const auto compactHigh=compactWorld.AllocationHighWatermark();
    auto observedWorld=compactWorld;auto observedHistory=compactHistory;auto observedDocument=compactDocument;
    allocations=0;failAllocation=0;countAllocations=true;const bool success=AdoptPrefabEntityRestoration(observedWorld,compactRoot,compactDefinition,guid,compactRow,compactSelection,observedHistory,error,&observedDocument);countAllocations=false;
    Require(success && observedWorld.Find(compactRoot+1) && observedWorld.Find(compactExtra) && observedHistory.UndoCount()==2 && observedDocument.IsDirty(),"Counted restoration failed or merged pending ordinary edit");const auto positions=allocations;
    for(size_t fault=1;fault<=positions;++fault){auto testedWorld=compactWorld;auto testedHistory=compactHistory;auto testedDocument=compactDocument;
        allocations=0;failAllocation=fault;countAllocations=true;const bool accepted=AdoptPrefabEntityRestoration(testedWorld,compactRoot,compactDefinition,guid,compactRow,compactSelection,testedHistory,error,&testedDocument);countAllocations=false;
        Require(!accepted && !error.empty() && testedWorld.Capture().dump()==compactBefore && testedWorld.AllocationHighWatermark()==compactHigh && compactSelection.Capture()==compactSelected && testedHistory.HasSameState(compactHistory) && testedDocument.HasSameState(compactDocument) && compactDefinition.dump()==compactSourceBefore,"Restoration allocation failure published World/selection/history/document/source");
    }
    auto replaySelection=compactSelection;Require(observedHistory.Undo(observedWorld,replaySelection,error,&observedDocument) && observedWorld.Capture().dump()==compactBefore && replaySelection.Capture()==compactSelected && !observedDocument.IsDirty() && observedHistory.Redo(observedWorld,replaySelection,error,&observedDocument) && observedWorld.Find(compactRoot+1) && observedDocument.IsDirty(),"Counted restoration replay lost full selection/saved baseline");
    std::cout<<"ENTITY_RESTORATION_COMMAND_ALLOCATION_POSITIONS="<<positions<<'\n';
    std::cout<<"PASS: selective deleted mapped Entity restoration/missing-parent closure, changed-parent/target approval, new-source parent reservation, stable/exhausted identities, independent anchors/fields/other roots/additions/deletions, full-selection Undo/Redo/dirty and stale/GUID/alias/selection/pending/budget/command allocation isolation (core only)\n";return 0;
}catch(const std::exception& exception){countAllocations=false;std::cerr<<"FAIL: "<<exception.what()<<'\n';return 1;}}
