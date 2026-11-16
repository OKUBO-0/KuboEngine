#include "engine/editor/EntityCreate.h"
#include "engine/editor/EntityDocumentState.h"
#include "engine/editor/PrefabInstance.h"
#include "engine/services/McpCommandBridge.h"
#include <iostream>
#include <stdexcept>
namespace {void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}}
int main(){
    using namespace Engine::Scene;using namespace Engine::Editor;
    try {
        std::string error;EntityWorld world;const auto parent=world.CreateEntity("Parent"),other=world.CreateEntity("Other");
        EntitySelection selection;Require(selection.Click(world,parent,false,false,error) && selection.Click(world,other,true,false,error),error.c_str());
        const auto oldSelection=selection.Capture();const auto before=world.Capture();EntityHistory history;EntityId created=777;EntityDocumentState document;document.Opened(world,"virtual.scene");
        Require(CreateAuthoredEntity(world,"Child",parent,selection,history,created,error) && created==3 && world.Find(created)->parent==parent && selection.Primary()==created,"Candidate create failed parent/selection");
        document.Observe(world);Require(document.IsDirty(),"Create did not dirty authored Scene");
        Require(history.Undo(world,selection,error) && world.Capture().dump()==before.dump() && selection.Capture()==oldSelection,"Create Undo lost world/full selection");
        document.Observe(world);Require(!document.IsDirty(),"Create Undo did not restore saved dirty baseline");
        Require(history.Redo(world,selection,error) && world.Find(created),"Create Redo failed");
        EntityHistory tiny(4,1);const auto held=world.Capture().dump();const auto heldSelection=selection.Capture();const auto high=world.AllocationHighWatermark();EntityId sentinel=555;
        Require(!CreateAuthoredEntity(world,"Rejected",0,selection,tiny,sentinel,error) && world.Capture().dump()==held && selection.Capture()==heldSelection && world.AllocationHighWatermark()==high && sentinel==555,"History refusal consumed ID/changed selection/output/world");
        Require(!CreateAuthoredEntity(world,"Missing parent",999,selection,history,sentinel,error) && world.Capture().dump()==held && sentinel==555,"Missing parent creation partially published");
        EntityWorld exhausted;exhausted.CreateEntity("Last");auto exact=exhausted.Capture();exact["entities"].get<crude_json::array>()[0]["id"]=double(EntityWorld::MaximumEntityId);
        Require(exhausted.Restore(exact,error),error.c_str());const auto exhaustedText=exhausted.Capture().dump();
        for(int repeat=0;repeat<3;++repeat)Require(exhausted.CreateEntity("No ID")==0 && exhausted.Capture().dump()==exhaustedText && exhausted.AllocationHighWatermark()==EntityWorld::MaximumEntityId+1,"Exhausted native create mutated/wrapped/reused ID");
        EntitySelection last;last.Replace(EntityWorld::MaximumEntityId);EntityHistory lastHistory;
        Require(!CreateAuthoredEntity(exhausted,"No ID",0,last,lastHistory,sentinel,error) && last.Primary()==EntityWorld::MaximumEntityId && !lastHistory.CanUndo() && sentinel==555,"Exhausted authored command changed state");
        Engine::Services::McpCommandBridge bridge(std::filesystem::current_path());const auto request=SceneValue::parse(R"({"id":1,"method":"scene.create_entity","params":{"name":"No ID"}})");
        const auto response=bridge.Handle(request,exhausted);Require(response.contains("error") && response["error"]["code"].get<crude_json::number>()==-32000 && exhausted.Capture().dump()==exhaustedText,"MCP reported fake success/changed exhausted world");
        for(const auto invalid:{9007199254740992.0,18446744073709551616.0}){auto unsafe=exact;unsafe["entities"].get<crude_json::array>()[0]["id"]=invalid;Require(!exhausted.Restore(unsafe,error) && exhausted.Capture().dump()==exhaustedText,"Unsafe Scene ID converted/published");}
        exhausted.Clear();Require(exhausted.CreateEntity("Restart")==1,"Explicit Clear did not reset exhausted allocator");
        EntityWorld source;const auto localRoot=source.CreateEntity("Prefab"),localChild=source.CreateEntity("Child",localRoot);PrefabValue prefab;Require(CapturePrefabSubtree(source,localRoot,prefab,error),error.c_str());
        EntityWorld withGap;withGap.CreateEntity("Keep");const auto retired=withGap.CreateEntity("Retired");Require(withGap.DestroyEntity(retired),"Gap setup failed");EntitySelection gapSelection;gapSelection.Replace(1);EntityHistory gapHistory;EntityId prefabRoot=0;
        Require(InstantiatePrefab(withGap,prefab,"0123456789abcdef0123456789abcdef",0,gapSelection,gapHistory,prefabRoot,error) && prefabRoot==3,"Prefab allocation reused current-session deleted plain ID");
        auto reserved=withGap.Capture();auto& root=reserved["entities"].get<crude_json::array>()[1];root["prefabLink"]["mapping"].get<crude_json::array>()[1]["entity"]=double(EntityWorld::MaximumEntityId);
        Require(withGap.Restore(reserved,error),error.c_str());Require(withGap.CreateEntity("Reserved")==0,"Prefab tombstone exhaustion bypassed native allocator");
        // Exposed valid record edits must not let either allocator use stale counters.
        EntityWorld edited;const auto id=edited.CreateEntity("Mutable");edited.Find(id)->id=100;
        Require(edited.CreateEntity("Fresh")==101,"Native create ignored mutable identity high-water");
        auto& link=withGap.Find(prefabRoot)->prefabLink;link["mapping"].get<crude_json::array>()[1]["entity"]=1000.0;
        EntityWorld stale;stale.CreateEntity("Keep");stale.GetEntities()=withGap.GetEntities();EntityId duplicate=0;
        Require(stale.DuplicateSubtree(prefabRoot,0,duplicate,error) && duplicate==1001,"Duplication ignored edited reserved identities");
        (void)localChild;
        std::cout<<"PASS: JSON-exact native/current-session/reserved allocation, strict Scene IDs, candidate create/full-selection Undo/Redo/dirty, history/parent/exhaustion isolation, real MCP refusal and Prefab/duplicate high-water preservation\n";return 0;
    }catch(const std::exception& exception){std::cerr<<"FAIL: "<<exception.what()<<'\n';return 1;}
}
