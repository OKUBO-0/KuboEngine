#include "engine/editor/PrefabRevert.h"
#include "engine/editor/PrefabInstance.h"
#include "engine/editor/EntityDocumentState.h"
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
int main(){
    using namespace Engine::Editor;using namespace Engine::Scene;
    const std::string guid="0123456789abcdef0123456789abcdef";
    const auto fixture=std::filesystem::temp_directory_path()/(L"kubo-prefab-revert-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        EntityWorld source;source.CreateEntity("Root");source.CreateEntity("Child",1);source.CreateEntity("Removed source",1);
        source.Find(1)->transform.position={1,2,3};source.SetModel(2,"Assets/shared.glb",guid);
        auto sourceWorld=source.Capture();sourceWorld["entities"][size_t(1)]["id"]=30.0;sourceWorld["entities"][size_t(2)]["id"]=20.0;
        std::string error;Require(source.Restore(sourceWorld,error),error.c_str());PrefabValue definition;Require(CapturePrefabSubtree(source,1,definition,error),error.c_str());
        EntityWorld world;world.CreateEntity("Outside");EntitySelection selection;selection.Replace(1);EntityHistory history;EntityId root=0;
        Require(InstantiatePrefab(world,definition,guid,1,selection,history,root,error),error.c_str());history.Clear();
        const auto pristine=world.Capture().dump();const auto originalCounter=world.AllocationHighWatermark();
        Require(RevertPrefabInstance(world,root,definition,guid,selection,history,error) && world.Capture().dump()==pristine && history.UndoCount()==0 && world.AllocationHighWatermark()==originalCounter,"Unchanged source created a false history edit/reordered Scene");
        world.Find(root)->name="Override";world.Find(root)->transform.position={9,8,7};world.RemoveModel(3);Require(world.DestroyEntity(4),"Cannot delete mapped child");
        const auto added=world.CreateEntity("Extra",root);world.CreateEntity("Extra child",added);
        Require(selection.Assign(world,{1,root,added},added,error),error.c_str());const auto beforeSelection=selection.Capture();const auto before=world.Capture().dump();
        const auto sceneFile=fixture/L"保存.scene";Require(world.Save(sceneFile,error),error.c_str());
        EntityDocumentState document;document.Opened(world,sceneFile);
        EntityWorld restored;Require(BuildPrefabRevertCandidate(world,root,definition,guid,restored,error) && restored.Find(4) && restored.Find(4)->name=="Removed source","Revert did not restore instance-deleted mapped child identity");
        auto incoming=definition;incoming["world"]["entities"].get<crude_json::array>().erase(incoming["world"]["entities"].get<crude_json::array>().begin()+2);
        auto newChild=definition["world"]["entities"][size_t(1)];newChild["id"]=31.0;newChild["name"]="Source new";incoming["world"]["entities"].push_back(newChild);
        const auto beforeCounter=world.AllocationHighWatermark();
        Require(RevertPrefabInstance(world,root,incoming,guid,selection,history,error,&document),error.c_str());
        Require(world.Find(root)->parent==1 && world.Find(root)->name=="Root" && world.Find(root)->transform.position.x==1 && world.Find(3)->hasModel && !world.Find(4) && !world.Find(added) && !world.Find(added+1),"Revert kept overrides/lost parent/removed unrelated source identity");
        Require(world.Find(beforeCounter) && world.Find(beforeCounter)->name=="Source new" && world.Find(beforeCounter)->parent==root && world.AllocationHighWatermark()>beforeCounter,"Source addition reused retired instance IDs");
        Require(selection.Ids()==std::vector<EntityId>{1,root} && selection.Primary()==root && document.IsDirty() && history.UndoCount()==1,"Revert selection/dirty/history admission failed");
        const auto after=world.Capture().dump();const auto afterSelection=selection.Capture();
        Require(history.Undo(world,selection,error) && world.Capture().dump()==before && selection.Capture()==beforeSelection,"Revert Undo lost source baseline/full selection");document.Observe(world);Require(!document.IsDirty(),"Revert Undo did not restore saved baseline");
        Require(history.Redo(world,selection,error) && world.Capture().dump()==after && selection.Capture()==afterSelection,"Revert Redo lost candidate identity map");
        const auto reintroducedId=world.AllocationHighWatermark();
        Require(BuildPrefabRevertCandidate(world,root,definition,guid,restored,error) && restored.Find(reintroducedId) && restored.Find(reintroducedId)->name=="Removed source" && !restored.Find(4),"Source reintroduction reused retired Scene identity");
        Require(world.Save(sceneFile,error),error.c_str());EntityWorld reopened;Require(reopened.Load(sceneFile,error) && reopened.Capture().dump()==after,"Reverted source baseline/identity map lost on Scene reload");
        const auto held=world.Capture().dump();const auto heldSelection=selection.Capture();EntityHistory tiny(4,1);
        Require(!RevertPrefabInstance(world,root,definition,guid,selection,tiny,error) && world.Capture().dump()==held && selection.Capture()==heldSelection && tiny.UndoCount()==0,"History refusal partially reverted");
        auto mismatch=incoming;mismatch["root"]=2.0;
        for(auto& node:mismatch["world"]["entities"].get<crude_json::array>()){if(node["id"].get<crude_json::number>()==1)node["id"]=2.0;if(node["parent"].get<crude_json::number>()==1)node["parent"]=2.0;}
        PrefabValue independentlyValid;Require(ValidatePrefabDocument(mismatch,independentlyValid,error),"Changed source-root fixture is not independently valid");
        const auto heldOutput=restored.Capture().dump();
        Require(!BuildPrefabRevertCandidate(world,root,mismatch,guid,restored,error) && restored.Capture().dump()==heldOutput,"Bad source changed candidate output");
        Require(!BuildPrefabRevertCandidate(world,root,incoming,"ffffffffffffffff",restored,error) && restored.Capture().dump()==heldOutput,"Wrong GUID changed candidate output");
        EntityWorld high;Require(high.Restore(crude_json::value::parse(pristine),error),error.c_str());
        auto* exposed=high.Find(4);exposed->id=100;exposed->parent=root;exposed->prefabLink=SceneValue{};
        // Original mapped Scene 4 is now a valid deletion tombstone, while 100 is
        // an unmapped instance addition not reflected in the private counter.
        Require(BuildPrefabRevertCandidate(high,root,definition,guid,restored,error) && restored.Find(4) && !restored.Find(100) && restored.AllocationHighWatermark()==101,"Reconstruction lost observed mutable-record high-water");
        auto maximum=high.Capture();maximum["entities"].get<crude_json::array>().back()["id"]=double(EntityWorld::MaximumEntityId);
        Require(high.Restore(maximum,error),error.c_str());const auto maxBefore=high.Capture().dump();
        Require(!BuildPrefabRevertCandidate(high,root,incoming,guid,restored,error) && high.Capture().dump()==maxBefore,"Exhausted Scene allocated a source addition");
        Require(BuildPrefabRevertCandidate(high,root,definition,guid,restored,error) && restored.AllocationHighWatermark()==EntityWorld::MaximumEntityId+1 && restored.CreateEntity("Refused")==0,"Revert without new IDs lost exhausted high-water");
        auto missingRoot=world;missingRoot.Find(root)->prefabLink=SceneValue{};
        const auto lastOutput=restored.Capture().dump();
        Require(!BuildPrefabRevertCandidate(missingRoot,root,definition,guid,restored,error) && restored.Capture().dump()==lastOutput,"Unlinked root was reverted/changed output");
        EntityWorld compactSource;compactSource.CreateEntity("Compact root");PrefabValue compactDefinition;Require(CapturePrefabSubtree(compactSource,1,compactDefinition,error),error.c_str());
        EntityWorld compactWorld;EntitySelection compactSelection;EntityHistory compactHistory;EntityId compactRoot=0;
        Require(InstantiatePrefab(compactWorld,compactDefinition,guid,0,compactSelection,compactHistory,compactRoot,error),error.c_str());compactHistory.Clear();
        const auto ordinaryBefore=compactWorld.Capture();compactWorld.Find(compactRoot)->name="Pending ordinary edit";
        Require(compactHistory.Record(ordinaryBefore,compactWorld,compactSelection.Capture(),compactSelection.Capture(),error),error.c_str());
        auto compactIncoming=compactDefinition;compactIncoming["world"]["entities"][size_t(0)]["transform"]["position"]=crude_json::array{1.0,0.0,0.0};
        EntityDocumentState compactDocument;compactDocument.Opened(compactWorld,"compact.scene");
        const auto compactBefore=compactWorld.Capture().dump(),sourceBefore=compactIncoming.dump();const auto selectedBefore=compactSelection.Capture();const auto highBefore=compactWorld.AllocationHighWatermark();
        auto pending=compactDocument;Require(pending.Request(EntityDocumentState::Action::New),"Pending document setup failed");const auto pendingBefore=pending;
        Require(!RevertPrefabInstance(compactWorld,compactRoot,compactIncoming,guid,compactSelection,compactHistory,error,&pending) && pending.HasSameState(pendingBefore) && compactWorld.Capture().dump()==compactBefore,"Pending document bypassed whole Revert guard");
        auto observedWorld=compactWorld;auto observedSelection=compactSelection;auto observedHistory=compactHistory;auto observedDocument=compactDocument;
        allocations=0;failAllocation=0;countAllocations=true;const bool success=RevertPrefabInstance(observedWorld,compactRoot,compactIncoming,guid,observedSelection,observedHistory,error,&observedDocument);countAllocations=false;
        Require(success && observedDocument.IsDirty(),"Counted prepared whole Revert failed");const auto positions=allocations;
        for(size_t fault=1;fault<=positions;++fault){auto testedWorld=compactWorld;auto testedSelection=compactSelection;auto testedHistory=compactHistory;auto testedDocument=compactDocument;
            allocations=0;failAllocation=fault;countAllocations=true;const bool accepted=RevertPrefabInstance(testedWorld,compactRoot,compactIncoming,guid,testedSelection,testedHistory,error,&testedDocument);countAllocations=false;
            Require(!accepted && testedWorld.Capture().dump()==compactBefore && testedWorld.AllocationHighWatermark()==highBefore && testedSelection.Capture()==selectedBefore && testedHistory.HasSameState(compactHistory) && testedDocument.HasSameState(compactDocument) && compactIncoming.dump()==sourceBefore,"Whole Revert allocation failure published or settled live pending history");
        }
        std::cout<<"ROOT_REVERT_ALLOCATION_POSITIONS="<<positions<<'\n';
        std::filesystem::remove_all(fixture);
        std::cout<<"PASS: Prefab Revert candidate names/TRS/components/parent/stable IDs, additions/removals/tombstones/Unicode Scene reload, full-selection Undo/Redo/dirty, no-op ordering and history/GUID/ID-exhaustion/high-water isolation\n";return 0;
    }catch(const std::exception& exception){countAllocations=false;std::cerr<<"FAIL: "<<exception.what()<<"; owned fixture retained\n";return 1;}
}
