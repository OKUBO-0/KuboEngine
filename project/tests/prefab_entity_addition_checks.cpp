#include "engine/editor/PrefabEntityAddition.h"
#include "engine/editor/PrefabInstance.h"
#include <iostream>
#include <cstdlib>
#include <new>
namespace {bool countAllocations=false;size_t allocations=0,failAllocation=0;void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}}
// Fail one observed ordinary allocation, then allow error reporting to allocate.
void* operator new(size_t bytes){if(countAllocations && ++allocations==failAllocation){countAllocations=false;throw std::bad_alloc();}if(auto* memory=std::malloc(bytes?bytes:1))return memory;throw std::bad_alloc();}
void* operator new[](size_t bytes){return ::operator new(bytes);}
void operator delete(void* memory) noexcept{std::free(memory);}
void operator delete[](void* memory) noexcept{std::free(memory);}
void operator delete(void* memory,size_t) noexcept{std::free(memory);}
void operator delete[](void* memory,size_t) noexcept{std::free(memory);}
int main(){try {
    using namespace Engine::Editor;using namespace Engine::Scene;std::string error;const std::string guid="0123456789abcdef0123456789abcdef";
    EntityWorld authored;authored.CreateEntity("Root");authored.CreateEntity("Known parent",1);PrefabValue definition;Require(CapturePrefabSubtree(authored,1,definition,error),error.c_str());
    EntityWorld world;world.CreateEntity("External parent");EntitySelection selection;EntityHistory history;EntityId root=0,other=0;
    Require(InstantiatePrefab(world,definition,guid,1,selection,history,root,error) && InstantiatePrefab(world,definition,guid,1,selection,history,other,error),error.c_str());history.Clear();
    world.Find(root)->name="Independent name";world.Find(root)->transform.position={7,8,9};const auto extra=world.CreateEntity("Instance-owned addition",root);Require(selection.Assign(world,{1,root,extra},extra,error),error.c_str());
    auto incoming=definition;auto parent=incoming["world"]["entities"][size_t(1)];parent["id"]=3.0;parent["name"]="New source parent";incoming["world"]["entities"].push_back(parent);
    auto child=parent;child["id"]=4.0;child["parent"]=3.0;child["name"]="New selected child";incoming["world"]["entities"].push_back(child);
    auto untouched=parent;untouched["id"]=5.0;untouched["name"]="Unselected source child";incoming["world"]["entities"].push_back(untouched);
    incoming["world"]["entities"][size_t(0)]["transform"]["scale"]=crude_json::array{2.0,2.0,2.0};
    PrefabThreeWayReport report;Require(InspectPrefabThreeWay(world,root,incoming,guid,report,error),error.c_str());
    const auto found=std::find_if(report.entries.begin(),report.entries.end(),[](const auto& row){return row.local==4 && row.kind==PrefabThreeWayKind::EntityPresence;});Require(found!=report.entries.end(),"New source child row missing");const auto row=*found;
    const auto before=world.Capture().dump(),sourceBefore=incoming.dump();const auto selected=selection.Capture();const auto high=world.AllocationHighWatermark();EntityDocumentState document;document.Opened(world,"addition.scene");
    Require(AdoptPrefabEntityAddition(world,root,incoming,guid,row,selection,history,error,&document),error.c_str());
    Require(world.Find(root)->parent==1 && world.Find(root)->name=="Independent name" && world.Find(root)->transform.position.x==7 && world.Find(root)->transform.scale.x==1 && world.Find(extra) && world.Find(extra)->name=="Instance-owned addition" && world.Find(other)->prefabLink["base"].dump()==definition["world"].dump(),"Addition overwrote unrelated root/fields/instance-owned node or other instance");
    Require(world.GetEntities().size()==8 && world.AllocationHighWatermark()==high+2 && world.Find(high)->name=="New source parent" && world.Find(high+1)->parent==high && world.Find(high+1)->name=="New selected child" && selection.Capture()==selected && document.IsDirty(),"Addition dependency/identity/full selection/dirty failed");
    const auto after=world.Capture().dump();Require(incoming.dump()==sourceBefore && history.Undo(world,selection,error) && world.Capture().dump()==before && selection.Capture()==selected,error.c_str());document.Observe(world);Require(!document.IsDirty() && history.Redo(world,selection,error) && world.Capture().dump()==after,error.c_str());
    EntityWorld held;held.CreateEntity("Output sentinel");const auto heldBefore=held.Capture().dump();
    Require(!BuildPrefabEntityAdditionCandidate(world,root,incoming,guid,row,held,error) && held.Capture().dump()==heldBefore,"Already adopted stale row changed output");
    auto original=EntityWorld{};Require(original.Restore(PrefabValue::parse(before),error),error.c_str());auto invented=row;invented.source="invented";
    Require(!BuildPrefabEntityAdditionCandidate(original,root,incoming,guid,invented,held,error) && held.Capture().dump()==heldBefore && !BuildPrefabEntityAdditionCandidate(original,root,incoming,"wrong-guid",row,held,error),"Invented/GUID row accepted");
    Require(!BuildPrefabEntityAdditionCandidate(original,root,incoming,guid,row,original,error) && original.Capture().dump()==before,"Input/output alias changed World");
    EntityDocumentState pending;pending.Opened(original,"pending.scene");Require(pending.Request(EntityDocumentState::Action::New),"Pending action setup failed");const auto pendingBefore=pending;
    const auto historyBefore=history;Require(!AdoptPrefabEntityAddition(original,root,incoming,guid,row,selection,history,error,&pending) && original.Capture().dump()==before && pending.HasSameState(pendingBefore) && history.HasSameState(historyBefore),"Pending document action partially adopted Entities");
    EntityHistory tiny(1,1);EntityDocumentState budget;budget.Opened(original,"budget.scene");const auto budgetBefore=budget;
    Require(!AdoptPrefabEntityAddition(original,root,incoming,guid,row,selection,tiny,error,&budget) && original.Capture().dump()==before && budget.HasSameState(budgetBefore) && tiny.UndoCount()==0 && selection.Capture()==selected,"History budget failure partially adopted Entities");
    // A source child needing an intentionally deleted existing parent must not restore it implicitly.
    auto deleted=original;Require(deleted.DestroyEntity(root+1),"Mapped parent deletion fixture failed");auto dependent=incoming;dependent["world"]["entities"][size_t(3)]["parent"]=2.0;
    Require(InspectPrefabThreeWay(deleted,root,dependent,guid,report,error),error.c_str());const auto dependentRow=std::find_if(report.entries.begin(),report.entries.end(),[](const auto& entry){return entry.local==4 && entry.kind==PrefabThreeWayKind::EntityPresence;});
    Require(dependentRow!=report.entries.end() && !BuildPrefabEntityAdditionCandidate(deleted,root,dependent,guid,*dependentRow,held,error) && held.Capture().dump()==heldBefore,"Addition implicitly restored a deleted mapped parent");
    // Sweep the allocating candidate and command separately. The fixture has
    // both a retained redo payload and a pending ordinary edit: failure must
    // neither clear redo nor commit the live pending edit.
    EntityWorld compactSource;compactSource.CreateEntity("Compact root");PrefabValue compactDefinition;
    Require(CapturePrefabSubtree(compactSource,1,compactDefinition,error),error.c_str());
    EntityWorld compactWorld;EntitySelection compactSelection;EntityHistory compactHistory;EntityId compactRoot=0;
    Require(InstantiatePrefab(compactWorld,compactDefinition,guid,0,compactSelection,compactHistory,compactRoot,error),error.c_str());compactHistory.Clear();
    const auto external=compactWorld.CreateEntity("External selection");Require(compactSelection.Assign(compactWorld,{compactRoot,external},external,error),error.c_str());
    auto ordinary=compactWorld.Capture();compactWorld.Find(compactRoot)->active=false;
    Require(compactHistory.Record(ordinary,compactWorld,compactSelection.Capture(),compactSelection.Capture(),error),error.c_str());compactHistory.Commit();
    ordinary=compactWorld.Capture();compactWorld.Find(compactRoot)->transform.position.x=1;
    Require(compactHistory.Record(ordinary,compactWorld,compactSelection.Capture(),compactSelection.Capture(),error),error.c_str());compactHistory.Commit();
    Require(compactHistory.Undo(compactWorld,compactSelection,error) && compactHistory.RedoCount()==1,error.c_str());
    ordinary=compactWorld.Capture();compactWorld.Find(compactRoot)->name="Pending ordinary edit";
    Require(compactHistory.Record(ordinary,compactWorld,compactSelection.Capture(),compactSelection.Capture(),error) && compactHistory.RedoCount()==1,error.c_str());
    auto compactIncoming=compactDefinition;auto newParent=compactIncoming["world"]["entities"][size_t(0)];
    newParent["id"]=2.0;newParent["parent"]=1.0;newParent["name"]="New parent";compactIncoming["world"]["entities"].push_back(newParent);
    auto newChild=newParent;newChild["id"]=3.0;newChild["parent"]=2.0;newChild["name"]="Selected child";compactIncoming["world"]["entities"].push_back(newChild);
    PrefabThreeWayReport compactReport;Require(InspectPrefabThreeWay(compactWorld,compactRoot,compactIncoming,guid,compactReport,error),error.c_str());
    const auto compactFound=std::find_if(compactReport.entries.begin(),compactReport.entries.end(),[](const auto& entry){return entry.local==3 && entry.kind==PrefabThreeWayKind::EntityPresence;});
    Require(compactFound!=compactReport.entries.end(),"Allocation fixture row missing");const auto compactRow=*compactFound;
    EntityDocumentState compactDocument;compactDocument.Opened(compactWorld,"allocation-addition.scene");
    const auto compactBefore=compactWorld.Capture().dump(),compactSourceBefore=compactIncoming.dump();const auto compactSelected=compactSelection.Capture();const auto compactHigh=compactWorld.AllocationHighWatermark();
    auto output=held;allocations=0;failAllocation=0;countAllocations=true;
    const bool candidateSuccess=BuildPrefabEntityAdditionCandidate(compactWorld,compactRoot,compactIncoming,guid,compactRow,output,error);countAllocations=false;
    Require(candidateSuccess && output.GetEntities().size()==4,"Counted addition candidate failed");const auto candidatePositions=allocations;
    for(size_t fault=1;fault<=candidatePositions;++fault){auto testedOutput=held;const auto outputHigh=testedOutput.AllocationHighWatermark();
        allocations=0;failAllocation=fault;countAllocations=true;const bool accepted=BuildPrefabEntityAdditionCandidate(compactWorld,compactRoot,compactIncoming,guid,compactRow,testedOutput,error);countAllocations=false;
        Require(!accepted && !error.empty() && testedOutput.Capture().dump()==heldBefore && testedOutput.AllocationHighWatermark()==outputHigh && compactWorld.Capture().dump()==compactBefore && compactWorld.AllocationHighWatermark()==compactHigh && compactIncoming.dump()==compactSourceBefore,"Candidate allocation failure replaced output or mutated input/IDs/source");
    }
    auto observedWorld=compactWorld;auto observedHistory=compactHistory;auto observedDocument=compactDocument;
    allocations=0;failAllocation=0;countAllocations=true;const bool commandSuccess=AdoptPrefabEntityAddition(observedWorld,compactRoot,compactIncoming,guid,compactRow,compactSelection,observedHistory,error,&observedDocument);countAllocations=false;
    Require(commandSuccess && observedDocument.IsDirty() && observedHistory.RedoCount()==0 && observedHistory.UndoCount()==3,"Counted addition command failed or merged ordinary pending edit");const auto commandPositions=allocations;
    for(size_t fault=1;fault<=commandPositions;++fault){auto testedWorld=compactWorld;auto testedHistory=compactHistory;auto testedDocument=compactDocument;
        allocations=0;failAllocation=fault;countAllocations=true;const bool accepted=AdoptPrefabEntityAddition(testedWorld,compactRoot,compactIncoming,guid,compactRow,compactSelection,testedHistory,error,&testedDocument);countAllocations=false;
        Require(!accepted && !error.empty() && testedWorld.Capture().dump()==compactBefore && testedWorld.AllocationHighWatermark()==compactHigh && testedHistory.HasSameState(compactHistory) && testedDocument.HasSameState(compactDocument) && compactSelection.Capture()==compactSelected && compactIncoming.dump()==compactSourceBefore,"Command allocation failure published addition or settled live pending/redo history");
    }
    auto replaySelection=compactSelection;const auto observedAfter=observedWorld.Capture().dump();
    Require(observedHistory.Undo(observedWorld,replaySelection,error,&observedDocument) && observedWorld.Capture().dump()==compactBefore && replaySelection.Capture()==compactSelected && !observedDocument.IsDirty(),"Allocation-observed command Undo lost saved baseline/full selection");
    Require(observedHistory.Redo(observedWorld,replaySelection,error,&observedDocument) && observedWorld.Capture().dump()==observedAfter && observedDocument.IsDirty(),"Allocation-observed command Redo failed");
    Require(observedHistory.Undo(observedWorld,replaySelection,error) && observedHistory.Undo(observedWorld,replaySelection,error) && observedWorld.Find(compactRoot)->name=="Compact root" && !observedWorld.Find(compactRoot)->active,"Addition Undo merged or lost previous pending ordinary command");
    std::cout<<"ENTITY_ADDITION_CANDIDATE_ALLOCATION_POSITIONS="<<candidatePositions<<'\n'<<"ENTITY_ADDITION_COMMAND_ALLOCATION_POSITIONS="<<commandPositions<<'\n';
    std::cout<<"PASS: selective new-source Entity addition with missing-new-parent closure, selected-only baseline, independent fields/other instance/additions/external parent/full selection/Undo/Redo/dirty, stale/GUID/alias/deleted-parent refusal and candidate/command ordinary allocation isolation\n";return 0;
}catch(const std::exception& exception){countAllocations=false;std::cerr<<"FAIL: "<<exception.what()<<'\n';return 1;}}
