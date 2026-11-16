#include "engine/editor/EntityBatchDuplicate.h"
#include "engine/editor/EntityDocumentState.h"
#include <iostream>
#include <stdexcept>
namespace {void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}}
int main() {
    using namespace Engine::Scene;using namespace Engine::Editor;
    try {
        EntityWorld world;const auto external=world.CreateEntity("External"),group=world.CreateEntity("Group",external),child=world.CreateEntity("Same name",group),leaf=world.CreateEntity("Same name",child),other=world.CreateEntity("Other",external);
        world.Find(group)->transform={{1,2,3},{0.2f,0.3f,0.4f},{-2,3,0.5f}};
        world.Find(child)->transform={{4,5,6},{13,0.2f,0.3f},{1,-2,3}};
        world.SetModel(child,"Assets/shared.glb","0123456789abcdef0123456789abcdef");
        EntitySelection selection;std::string error;Require(selection.Assign(world,{group,other,child},child,error),error.c_str());
        const auto initialIds=selection.Ids();const auto before=world.Capture().dump();EntityHistory history;EntityDocumentState document;document.Opened(world,"entry.scene");
        Require(DuplicateSelectedSubtrees(world,selection,history,error),error.c_str());document.Observe(world);
        Require(world.GetEntities().size()==9 && selection.Ids().size()==3 && history.UndoCount()==1 && document.IsDirty(),"Forest duplicated descendants twice or missing one history command");
        const auto newGroup=selection.Ids()[0],newOther=selection.Ids()[1],newChild=selection.Ids()[2];
        Require(newGroup>other && newOther>other && newChild>other && newGroup!=newChild && selection.Primary()==newChild,"New selection/primary IDs not remapped");
        Require(world.Find(newGroup)->parent==external && world.Find(newOther)->parent==external && world.Find(newChild)->parent==newGroup,"Forest root/descendant parent mapping wrong");
        size_t leafCount=0;for(const auto& entity:world.GetEntities())if(entity.parent==newChild){++leafCount;Require(entity.name==world.Find(leaf)->name,"Same-name descendant mismapped");}
        Require(leafCount==1 && world.Find(newChild)->model.assetGuid==world.Find(child)->model.assetGuid && world.Find(newChild)->model.assetPath==world.Find(child)->model.assetPath,"Leaf count/shared asset identity changed");
        Require(world.Find(newChild)->transform.rotation.x==13 && world.Find(newChild)->transform.scale.y==-2 && world.Find(group)->parent==external,"Authored transforms/original hierarchy changed");
        const auto after=world.Capture().dump();EntityId primary=newChild;
        Require(history.Undo(world,primary,error) && world.Capture().dump()==before && primary==child,"Forest undo incomplete/primary wrong");document.Observe(world);Require(!document.IsDirty(),"Forest undo did not restore saved baseline");
        Require(history.Redo(world,primary,error) && world.Capture().dump()==after && primary==newChild,"Forest redo incomplete/primary wrong");
        EntityWorld rejectedWorld;Require(rejectedWorld.Restore(SceneValue::parse(before),error),error.c_str());EntitySelection rejectedSelection;Require(rejectedSelection.Assign(rejectedWorld,initialIds,child,error),error.c_str());
        EntityHistory rejectedHistory;const auto rejectedIds=rejectedSelection.Ids();
        Require(!DuplicateSelectedSubtrees(rejectedWorld,rejectedSelection,rejectedHistory,error,3) && rejectedWorld.Capture().dump()==before && rejectedSelection.Ids()==rejectedIds && rejectedSelection.Primary()==child && !rejectedHistory.CanUndo(),"Aggregate clone budget partially published forest/selection");
        EntityHistory tiny(100,1);Require(!DuplicateSelectedSubtrees(rejectedWorld,rejectedSelection,tiny,error) && rejectedWorld.Capture().dump()==before && rejectedSelection.Ids()==rejectedIds && !tiny.CanUndo(),"History budget partially published forest/selection");
        const auto assignBefore=rejectedSelection.Ids();Require(!rejectedSelection.Assign(rejectedWorld,{group,group},group,error) && rejectedSelection.Ids()==assignBefore,"Duplicate Assign mutated selection");
        rejectedWorld.Find(group)->parent=leaf;const auto cyclic=rejectedWorld.Capture().dump();
        Require(!DuplicateSelectedSubtrees(rejectedWorld,rejectedSelection,rejectedHistory,error) && rejectedWorld.Capture().dump()==cyclic && rejectedSelection.Ids()==rejectedIds,"Cyclic forest accepted/mutated");
        EntityWorld reordered;Require(reordered.Restore(SceneValue::parse(before),error),error.c_str());EntitySelection reversed;Require(reversed.Assign(reordered,{child,other,group},group,error),error.c_str());EntityHistory reversedHistory;
        Require(DuplicateSelectedSubtrees(reordered,reversed,reversedHistory,error) && reordered.Capture().dump()==after,"Clone IDs/hierarchy depend on selection click order");
        EntitySelection empty;EntityHistory emptyHistory;Require(!DuplicateSelectedSubtrees(reordered,empty,emptyHistory,error) && !emptyHistory.CanUndo(),"Empty selection duplicated world");
        EntityWorld missing;const auto removed=missing.CreateEntity("Removed");EntitySelection missingSelection;Require(missingSelection.Assign(missing,{removed},removed,error),error.c_str());missing.DestroyEntity(removed);
        const auto missingBefore=missing.Capture().dump();Require(!DuplicateSelectedSubtrees(missing,missingSelection,emptyHistory,error) && missing.Capture().dump()==missingBefore && missingSelection.Primary()==removed,"Missing selection silently mutated source/selection");
        EntityWorld exhausted;exhausted.CreateEntity("Last ID");auto exact=exhausted.Capture();exact["entities"][size_t(0)]["id"]=9007199254740991.0;
        Require(exhausted.Restore(exact,error),error.c_str());EntitySelection exhaustedSelection;Require(exhaustedSelection.Assign(exhausted,{9007199254740991ULL},9007199254740991ULL,error),error.c_str());const auto exhaustedBefore=exhausted.Capture().dump();
        Require(!DuplicateSelectedSubtrees(exhausted,exhaustedSelection,emptyHistory,error) && exhausted.Capture().dump()==exhaustedBefore && exhaustedSelection.Primary()==9007199254740991ULL,"ID exhaustion changed source/selection");
        std::cout<<"PASS: selected forest ancestor deduplication, exact clone/primary/asset remapping, click-order determinism, one Undo/Redo/dirty and clone/history/cycle/selection rejection isolation\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
