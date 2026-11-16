#include "engine/editor/EntityBatchDelete.h"
#include "engine/editor/EntityDocumentState.h"
#include <iostream>
#include <stdexcept>
namespace {void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}}
int main() {
    using namespace Engine::Scene;using namespace Engine::Editor;
    try {
        EntityWorld world;const auto outside=world.CreateEntity("Outside"),root=world.CreateEntity("Root",outside),child=world.CreateEntity("Child",root),leaf=world.CreateEntity("Leaf",child),other=world.CreateEntity("Other",outside),survivor=world.CreateEntity("Survivor",outside);
        world.SetModel(survivor,"Assets/shared.glb","0123456789abcdef0123456789abcdef");
        world.Find(child)->transform={{1,2,3},{13,0.2f,0.3f},{1,-2,3}};
        const auto before=world.Capture().dump();const auto originalSurvivor=world.Find(survivor)->model.assetGuid;
        EntitySelection selection;std::string error;Require(selection.Assign(world,{child,root,other},child,error),error.c_str());const auto selected=selection.Ids();
        EntityHistory history;EntityDocumentState document;document.Opened(world,"entry.scene");
        Require(DeleteSelectedSubtrees(world,selection,history,error),error.c_str());document.Observe(world);
        Require(world.GetEntities().size()==2 && world.Find(outside) && world.Find(survivor) && !world.Find(root) && !world.Find(child) && !world.Find(leaf) && !world.Find(other),"Forest delete did not remove exact descendant union");
        Require(selection.Ids().empty() && selection.Primary()==0 && history.UndoCount()==1 && document.IsDirty(),"Forest delete missing cleared selection/one dirty command");
        Require(world.Find(survivor)->parent==outside && world.Find(survivor)->model.assetGuid==originalSurvivor,"Forest delete altered unrelated parent/shared asset reference");
        const auto after=world.Capture().dump();EntityId primary=0;
        Require(history.Undo(world,primary,error) && world.Capture().dump()==before && primary==child,"Forest delete undo incomplete/primary wrong");document.Observe(world);Require(!document.IsDirty(),"Forest undo failed baseline");
        Require(history.Redo(world,primary,error) && world.Capture().dump()==after && primary==0,"Forest delete redo incomplete");
        EntityWorld rejected;Require(rejected.Restore(SceneValue::parse(before),error),error.c_str());EntitySelection rejectedSelection;Require(rejectedSelection.Assign(rejected,selected,child,error),error.c_str());EntityHistory rejectedHistory;
        Require(!DeleteSelectedSubtrees(rejected,rejectedSelection,rejectedHistory,error,3) && rejected.Capture().dump()==before && rejectedSelection.Ids()==selected && !rejectedHistory.CanUndo(),"Delete budget partially published world/selection");
        EntityHistory tiny(100,1);Require(!DeleteSelectedSubtrees(rejected,rejectedSelection,tiny,error) && rejected.Capture().dump()==before && rejectedSelection.Ids()==selected && !tiny.CanUndo(),"Delete history rejection partially published world/selection");
        rejected.Find(root)->parent=leaf;const auto cyclic=rejected.Capture().dump();Require(!DeleteSelectedSubtrees(rejected,rejectedSelection,rejectedHistory,error) && rejected.Capture().dump()==cyclic && rejectedSelection.Primary()==child,"Cyclic delete accepted/changed source");
        EntityWorld single;const auto singleRoot=single.CreateEntity("Single"),singleChild=single.CreateEntity("Child",singleRoot);EntitySelection singleSelection;Require(singleSelection.Assign(single,{singleRoot},singleRoot,error),error.c_str());EntityHistory singleHistory;
        Require(DeleteSelectedSubtrees(single,singleSelection,singleHistory,error) && single.GetEntities().empty() && !single.Find(singleChild),"Delete-to-empty forest failed");
        Require(!DeleteSelectedSubtrees(single,singleSelection,singleHistory,error),"Empty selection delete accepted");
        EntityWorld missing;const auto removed=missing.CreateEntity("Removed");EntitySelection stale;Require(stale.Assign(missing,{removed},removed,error),error.c_str());missing.DestroyEntity(removed);const auto missingBefore=missing.Capture().dump();
        Require(!DeleteSelectedSubtrees(missing,stale,rejectedHistory,error) && missing.Capture().dump()==missingBefore && stale.Primary()==removed,"Missing selection delete changed source/selection");
        EntityWorld legacy;const auto legacyRoot=legacy.CreateEntity("Root"),legacyChild=legacy.CreateEntity("Child",legacyRoot);legacy.Find(legacyChild)->transform.position={1,2,3};
        Require(legacy.DestroyEntity(legacyRoot) && legacy.Find(legacyChild)->parent==0 && legacy.Find(legacyChild)->transform.position.x==1,"Legacy root-children single-delete policy changed");
        std::cout<<"PASS: forest descendant-union deletion, unrelated/shared assets retained, one Undo/Redo/dirty/primary, empty-world and budget/history/cycle/missing isolation with legacy single-delete compatibility\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
