#include "engine/editor/PrefabFieldRevert.h"
#include "engine/editor/PrefabInstance.h"
#include <iostream>
#include <cstdlib>
#include <new>
namespace {bool countFieldAllocations=false;size_t fieldAllocations=0,failFieldAllocation=0;}
void* operator new(size_t bytes){
    if(countFieldAllocations && ++fieldAllocations==failFieldAllocation){countFieldAllocations=false;throw std::bad_alloc();}
    if(auto* memory=std::malloc(bytes?bytes:1))return memory;throw std::bad_alloc();
}
void* operator new[](size_t bytes){return ::operator new(bytes);}
void operator delete(void* memory) noexcept{std::free(memory);}
void operator delete[](void* memory) noexcept{std::free(memory);}
void operator delete(void* memory,size_t) noexcept{std::free(memory);}
void operator delete[](void* memory,size_t) noexcept{std::free(memory);}
namespace {void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}}
int main(){
    using namespace Engine::Editor;using namespace Engine::Scene;
    try {
        const std::string guid="0123456789abcdef0123456789abcdef";std::string error;
        EntityWorld source;source.CreateEntity("Source root");source.CreateEntity("Source child",1);source.CreateEntity("Source removed",1);
        source.Find(2)->hasScript=true;source.Find(2)->script.assetPath="Scripts/check.lua";
        source.Find(2)->script.properties["a~/b"]=1.0;source.Find(2)->script.properties["keep"]=true;
        PrefabValue definition;Require(CapturePrefabSubtree(source,1,definition,error),error.c_str());
        EntityWorld world;world.CreateEntity("Outside");EntitySelection selection;EntityHistory history;EntityId root=0;
        Require(InstantiatePrefab(world,definition,guid,1,selection,history,root,error),error.c_str());
        const auto child=root+1,removed=root+2;
        world.Find(root)->transform.position={4,5,6};world.Find(root)->name="Instance name";
        world.Find(child)->script.properties["a~/b"]=2.0;world.Find(child)->script.properties["extra"]=3.0;
        Require(world.DestroyEntity(removed),"Deleted fixture failed");const auto added=world.CreateEntity("Independent addition",child);
        selection.Replace(root);Require(selection.Click(world,child,true,false,error),error.c_str());const auto selected=selection.Capture();
        history.Clear();EntityDocumentState document;document.Opened(world,std::filesystem::path("authored.scene"));
        const auto original=world.Capture().dump();const auto high=world.AllocationHighWatermark();
        const auto inspect=[&](const EntityWorld& input,const std::string& path,EntityId local=1){
            PrefabOverrideReport report;Require(InspectPrefabOverrides(input,root,report,error),error.c_str());
            const auto found=std::find_if(report.entries.begin(),report.entries.end(),[&](const auto& entry){return entry.local==local && entry.path==path;});
            Require(found!=report.entries.end(),"Fixture has no requested field difference");return *found;
        };
        const auto position=inspect(world,"/transform/position");
        auto current=definition;current["world"]["entities"][size_t(0)]["transform"]["position"][size_t(0)]=9.0;
        current["world"]["entities"][size_t(0)]["name"]="Fresh source name";
        EntityWorld candidate;Require(BuildPrefabFieldRevertCandidate(world,root,current,guid,position,candidate,error),error.c_str());
        Require(world.Capture().dump()==original && candidate.Find(root)->transform.position.x==9 && candidate.Find(root)->transform.position.y==0 && candidate.Find(root)->parent==1 && candidate.Find(root)->name=="Instance name" && candidate.Find(added) && !candidate.Find(removed),"Field candidate mutated input or unrelated fields/structure/external parent");
        Require(candidate.Find(root)->prefabLink["mapping"].dump()==world.Find(root)->prefabLink["mapping"].dump() && candidate.AllocationHighWatermark()==high,"Field candidate changed mappings/high water");
        Require(candidate.Find(root)->prefabLink["base"]["entities"][size_t(0)]["name"].get<crude_json::string>()=="Source root","Field Revert advanced unrelated baseline name to current source");
        PrefabOverrideReport after;Require(InspectPrefabOverrides(candidate,root,after,error),error.c_str());
        Require(std::none_of(after.entries.begin(),after.entries.end(),[](const auto& entry){return entry.path=="/transform/position";}),"Selected baseline did not advance to current source");
        Require(RevertPrefabField(world,root,current,guid,position,selection,history,error,&document) && document.IsDirty() && selection.Capture()==selected && history.UndoCount()==1,error.c_str());
        const auto reverted=world.Capture().dump();Require(reverted==candidate.Capture().dump(),"History command differs from candidate");
        Require(history.Undo(world,selection,error,&document) && world.Capture().dump()==original && !document.IsDirty() && selection.Capture()==selected,"Field Undo lost baseline/full selection/dirty");
        Require(history.Redo(world,selection,error,&document) && world.Capture().dump()==reverted && document.IsDirty() && selection.Capture()==selected,"Field Redo lost baseline/full selection/dirty");
        const auto hold=world.Capture().dump();const auto undo=history.UndoCount();
        Require(!RevertPrefabField(world,root,current,guid,position,selection,history,error,&document) && world.Capture().dump()==hold && history.UndoCount()==undo && document.IsDirty(),"Stale snapshot changed World/history/document");
        const auto name=inspect(world,"/name");Require(RevertPrefabField(world,root,current,guid,name,selection,history,error,&document) && world.Find(root)->name=="Fresh source name",error.c_str());
        const auto property=inspect(world,"/script/properties/a~0~1b",2);
        auto withoutProperty=current;withoutProperty["world"]["entities"][size_t(1)]["script"]["properties"].erase("a~/b");
        Require(RevertPrefabField(world,root,withoutProperty,guid,property,selection,history,error,&document) && !world.Find(child)->script.properties.contains("a~/b") && world.Find(child)->script.properties.contains("extra"),"Missing current source property was not removed selectively");
        const auto extra=inspect(world,"/script/properties/extra",2);
        Require(RevertPrefabField(world,root,withoutProperty,guid,extra,selection,history,error,&document) && !world.Find(child)->script.properties.contains("extra") && world.Find(child)->script.properties.contains("keep"),"Instance-added field was not removed selectively");
        const auto stable=world.Capture().dump();const auto stableHistory=history.UndoCount();const auto stableDocument=document;
        EntityWorld output;output.CreateEntity("Output sentinel");const auto sentinel=output.Capture().dump();
        auto invented=name;invented.path="/id";
        Require(!BuildPrefabFieldRevertCandidate(world,root,current,guid,invented,output,error) && output.Capture().dump()==sentinel,"Invented identity field replaced output");
        Require(!BuildPrefabFieldRevertCandidate(world,root,current,"11111111111111111111111111111111",property,output,error) && output.Capture().dump()==sentinel,"GUID mismatch changed output");
        PrefabOverrideReport structure;Require(InspectPrefabOverrides(world,root,structure,error),error.c_str());
        const auto structural=std::find_if(structure.entries.begin(),structure.entries.end(),[](const auto& entry){return entry.kind==PrefabOverrideKind::EntityAdded;});Require(structural!=structure.entries.end(),"Missing structural fixture");
        Require(!RevertPrefabField(world,root,current,guid,*structural,selection,history,error,&document) && world.Capture().dump()==stable && history.UndoCount()==stableHistory && document.HasSameState(stableDocument),"Structural rejection changed state");
        Require(document.Request(EntityDocumentState::Action::New),"Pending document fixture failed");const auto pending=document;
        Require(!RevertPrefabField(world,root,current,guid,*structural,selection,history,error,&document) && document.HasSameState(pending) && world.Capture().dump()==stable && history.UndoCount()==stableHistory,"Pending document action changed state");
        auto pendingWorld=world;pendingWorld.Find(child)->script.properties["keep"]=false;
        const auto pendingEntry=inspect(pendingWorld,"/script/properties/keep",2);const auto pendingBytes=pendingWorld.Capture().dump();
        Require(!RevertPrefabField(pendingWorld,root,current,guid,pendingEntry,selection,history,error,&document) && document.HasSameState(pending) && pendingWorld.Capture().dump()==pendingBytes && history.UndoCount()==stableHistory,"Valid field bypassed pending document action");
        std::vector<std::string> tokens{"sentinel"};Require(!PrefabOverrideTokens("/a~2b",tokens,error) && tokens==std::vector<std::string>{"sentinel"},"Invalid escape changed token output");
        // Both source and instance are individually valid, but changing this
        // parent alone would require creating a missing source Entity.
        auto parentWorld=world;parentWorld.Find(child)->parent=root;
        parentWorld.Find(added)->parent=root;
        parentWorld.Find(child)->parent=added;
        const auto parentEntry=inspect(parentWorld,"/parent",2);
        EntityWorld parentCandidate;Require(BuildPrefabFieldRevertCandidate(parentWorld,root,current,guid,parentEntry,parentCandidate,error) && parentCandidate.Find(child)->parent==root && parentCandidate.Find(added)->parent==root,error.c_str());
        auto parentSource=current;auto newParent=parentSource["world"]["entities"][size_t(0)];newParent["id"]=100.0;newParent["parent"]=1.0;
        parentSource["world"]["entities"].push_back(newParent);parentSource["world"]["entities"][size_t(1)]["parent"]=100.0;
        Require(!BuildPrefabFieldRevertCandidate(parentWorld,root,parentSource,guid,parentEntry,output,error) && output.Capture().dump()==sentinel,"Missing current source parent was silently created or changed output");
        auto rootChanged=current;rootChanged["root"]=50.0;
        for(auto& node:rootChanged["world"]["entities"].get<crude_json::array>()){if(node["id"].get<crude_json::number>()==1)node["id"]=50.0;if(node["parent"].get<crude_json::number>()==1)node["parent"]=50.0;}
        Require(!BuildPrefabFieldRevertCandidate(parentWorld,root,rootChanged,guid,parentEntry,output,error) && output.Capture().dump()==sentinel,"Changed source root identity replaced output");
        auto missingTarget=current;missingTarget["world"]["entities"].get<crude_json::array>().resize(1);
        Require(!BuildPrefabFieldRevertCandidate(parentWorld,root,missingTarget,guid,parentEntry,output,error) && output.Capture().dump()==sentinel,"Source-deleted target was silently recreated");
        auto unsupported=current;unsupported["world"]["entities"][size_t(0)]["unknown"]=true;
        Require(!BuildPrefabFieldRevertCandidate(parentWorld,root,unsupported,guid,parentEntry,output,error) && output.Capture().dump()==sentinel,"Unsupported source schema replaced output");
        // Updating one baseline parent can form a cycle even when the whole
        // current source and live instance are independently valid trees.
        EntityWorld chain;chain.CreateEntity("Root");chain.CreateEntity("Child",1);chain.CreateEntity("Grandchild",2);PrefabValue chainSource;
        Require(CapturePrefabSubtree(chain,1,chainSource,error),error.c_str());EntityWorld chainWorld;EntitySelection chainSelection;EntityHistory chainHistory;EntityId chainRoot=0;
        Require(InstantiatePrefab(chainWorld,chainSource,guid,0,chainSelection,chainHistory,chainRoot,error),error.c_str());
        chainWorld.Find(3)->parent=1;chainWorld.Find(2)->parent=3;PrefabOverrideReport chainReport;
        Require(InspectPrefabOverrides(chainWorld,chainRoot,chainReport,error),error.c_str());
        const auto chainEntry=std::find_if(chainReport.entries.begin(),chainReport.entries.end(),[](const auto& entry){return entry.local==2 && entry.path=="/parent";});Require(chainEntry!=chainReport.entries.end(),"Cycle fixture has no field difference");
        auto chainNext=chainSource;chainNext["world"]["entities"][size_t(1)]["parent"]=3.0;chainNext["world"]["entities"][size_t(2)]["parent"]=1.0;
        Require(!BuildPrefabFieldRevertCandidate(chainWorld,chainRoot,chainNext,guid,*chainEntry,output,error) && output.Capture().dump()==sentinel,"Partial baseline cycle replaced output");
        // Exhaust ordinary new/new[] failure positions for a small successful
        // command, including pending-history settlement and dirty preparation.
        EntityWorld allocSource;allocSource.CreateEntity("Baseline");PrefabValue allocDefinition;
        Require(CapturePrefabSubtree(allocSource,1,allocDefinition,error),error.c_str());
        EntityWorld allocWorld;EntitySelection allocSelection;EntityHistory allocHistory;EntityId allocRoot=0;
        Require(InstantiatePrefab(allocWorld,allocDefinition,guid,0,allocSelection,allocHistory,allocRoot,error),error.c_str());
        allocWorld.Find(allocRoot)->name="Override";allocHistory.Clear();const auto allocBefore=allocWorld.Capture();
        auto prior=allocBefore;prior["entities"][size_t(0)]["name"]="Earlier edit";
        Require(allocHistory.Record(prior,allocWorld,allocSelection.Capture(),allocSelection.Capture(),error),error.c_str());
        EntityDocumentState allocDocument;allocDocument.Opened(allocWorld,L"allocation.scene");
        PrefabOverrideReport allocReport;Require(InspectPrefabOverrides(allocWorld,allocRoot,allocReport,error) && allocReport.entries.size()==1,error.c_str());
        const auto allocationEntry=allocReport.entries.front();const auto allocBytes=allocWorld.Capture().dump();
        auto probeWorld=allocWorld;auto probeHistory=allocHistory;auto probeDocument=allocDocument;
        fieldAllocations=0;failFieldAllocation=0;countFieldAllocations=true;
        const bool probe=RevertPrefabField(probeWorld,allocRoot,allocDefinition,guid,allocationEntry,allocSelection,probeHistory,error,&probeDocument);
        countFieldAllocations=false;const auto positions=fieldAllocations;Require(probe && positions>0,"Allocation fixture command failed");
        for(size_t position=1;position<=positions;++position) {
            auto failureWorld=allocWorld;auto failureHistory=allocHistory;auto failureDocument=allocDocument;
            fieldAllocations=0;failFieldAllocation=position;countFieldAllocations=true;
            const bool applied=RevertPrefabField(failureWorld,allocRoot,allocDefinition,guid,allocationEntry,allocSelection,failureHistory,error,&failureDocument);
            countFieldAllocations=false;
            Require(!applied && failureWorld.Capture().dump()==allocBytes && failureHistory.HasSameState(allocHistory) && failureDocument.HasSameState(allocDocument),"Field allocation failure published World/history/document");
        }
        std::cout<<"FIELD_REVERT_ALLOCATION_POSITIONS="<<positions<<'\n';
        std::cout<<"PASS: current-source single-field Prefab Revert candidates and baseline advancement, atomic TRS/names/escaped-property absence, unrelated structure/mappings/high water, full-selection dirty Undo/Redo and stale/GUID/structural/document rejection isolation (core only)\n";return 0;
    }catch(const std::exception& exception){std::cerr<<"FAIL: "<<exception.what()<<'\n';return 1;}
}
