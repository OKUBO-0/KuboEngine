#include "engine/editor/EntityBatchReparent.h"
#include "engine/editor/EntityDocumentState.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
namespace {
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
Matrix4x4 Pose(const Engine::Scene::EntityWorld& world,Engine::Scene::EntityId id){Matrix4x4 pose{};Require(world.GetWorldMatrix(id,pose),"World matrix missing");return pose;}
void Same(const Matrix4x4& a,const Matrix4x4& b){for(int i=0;i<4;++i)for(int j=0;j<4;++j)Require(std::abs(double(a.m[i][j])-b.m[i][j])<0.001*std::max(1.0,std::abs(double(a.m[i][j]))),"Keep-World forest pose changed");}
}
int main(){
    using namespace Engine::Scene;using namespace Engine::Editor;
    try {
        EntityWorld world;const auto source=world.CreateEntity("Source"),destination=world.CreateEntity("Destination"),root=world.CreateEntity("Root",source),child=world.CreateEntity("Child",root),leaf=world.CreateEntity("Leaf",child),other=world.CreateEntity("Other",source),outside=world.CreateEntity("Outside");
        world.Find(source)->transform={{7,2,-5},{0.2f,0.3f,-0.4f},{-2,2,2}};
        world.Find(destination)->transform={{-4,3,8},{-0.1f,0.4f,0.2f},{3,3,3}};
        world.Find(root)->transform={{1,2,3},{0.5f,1.7f,0.8f},{1,2,-3}};
        world.Find(child)->transform.position={3,-1,2};world.Find(other)->transform.position={2,4,1};
        world.SetModel(leaf,"Assets/shared.glb","0123456789abcdef0123456789abcdef");
        const auto before=world.Capture().dump();const auto rootPose=Pose(world,root),childPose=Pose(world,child),leafPose=Pose(world,leaf),otherPose=Pose(world,other);
        EntitySelection selection;std::string error;Require(selection.Assign(world,{child,root,other},child,error),error.c_str());const auto selected=selection.Capture();
        EntityHistory history;EntityDocumentState document;document.Opened(world,"entry.scene");
        Require(ReparentSelectedSubtrees(world,selection,destination,true,history,error),error.c_str());document.Observe(world);
        Require(world.Find(root)->parent==destination && world.Find(other)->parent==destination && world.Find(child)->parent==root && world.Find(leaf)->parent==child,"Forest reparent flattened descendants/wrong roots");
        Same(rootPose,Pose(world,root));Same(childPose,Pose(world,child));Same(leafPose,Pose(world,leaf));Same(otherPose,Pose(world,other));
        Require(world.Find(leaf)->model.assetGuid=="0123456789abcdef0123456789abcdef" && world.Find(outside)->parent==0 && selection.Capture()==selected && document.IsDirty() && history.UndoCount()==1,"Reparent changed asset/selection/unrelated entity or missed dirty/history");
        const auto after=world.Capture().dump();
        Require(ReparentSelectedSubtrees(world,selection,destination,true,history,error) && world.Capture().dump()==after && history.UndoCount()==1,"No-op reparent recorded/changed TRS");
        selection.Replace(outside);Require(history.Undo(world,selection,error) && world.Capture().dump()==before && selection.Capture()==selected,"Reparent Undo lost world/full selection");document.Observe(world);Require(!document.IsDirty(),"Reparent Undo lost saved baseline");
        Require(history.Redo(world,selection,error) && world.Capture().dump()==after && selection.Capture()==selected,"Reparent Redo incomplete");
        Require(!ReparentSelectedSubtrees(world,selection,leaf,true,history,error) && world.Capture().dump()==after && selection.Capture()==selected,"Descendant destination accepted/partially published");
        Require(!ReparentSelectedSubtrees(world,selection,999,true,history,error) && world.Capture().dump()==after,"Missing destination accepted");
        Require(!ReparentSelectedSubtrees(world,selection,0,true,history,error,1) && world.Capture().dump()==after,"Aggregate root budget ignored");
        EntityHistory tiny(100,1);Require(!ReparentSelectedSubtrees(world,selection,0,true,tiny,error) && world.Capture().dump()==after && !tiny.CanUndo(),"History rejection published forest");
        Require(ReparentSelectedSubtrees(world,selection,0,true,history,error),error.c_str());Same(rootPose,Pose(world,root));Same(otherPose,Pose(world,other));
        EntityWorld local;Require(local.Restore(SceneValue::parse(before),error),error.c_str());EntitySelection localSelection;Require(localSelection.Restore(local,selected,error),error.c_str());EntityHistory localHistory;
        const auto localRoot=local.Find(root)->transform;Require(ReparentSelectedSubtrees(local,localSelection,destination,false,localHistory,error),error.c_str());
        Require(local.Find(root)->transform.position.x==localRoot.position.x && local.Find(root)->transform.rotation.y==localRoot.rotation.y && local.Find(root)->transform.scale.z==localRoot.scale.z && local.Find(child)->parent==root,"Local-TRS mode changed authored transform/hierarchy");
        EntityWorld reversed;Require(reversed.Restore(SceneValue::parse(before),error),error.c_str());EntitySelection reverseSelection;Require(reverseSelection.Assign(reversed,{other,root,child},other,error),error.c_str());EntityHistory reverseHistory;
        Require(ReparentSelectedSubtrees(reversed,reverseSelection,destination,false,reverseHistory,error) && reversed.Capture().dump()==local.Capture().dump(),"Forest reparent result depends on click order");
        EntityWorld staleWorld;const auto staleId=staleWorld.CreateEntity("Stale");EntitySelection staleSelection;staleSelection.Replace(staleId);staleWorld.DestroyEntity(staleId);EntityHistory staleHistory;const auto staleBefore=staleWorld.Capture().dump();
        Require(!ReparentSelectedSubtrees(staleWorld,staleSelection,0,false,staleHistory,error) && staleWorld.Capture().dump()==staleBefore && staleSelection.Primary()==staleId && !staleHistory.CanUndo(),"Missing selected Entity accepted/partially published");
        Require(!ReparentSelectedSubtrees(world,selection,0,true,history,error,0) && !ReparentSelectedSubtrees(world,selection,0,true,history,error,10001),"Invalid root budget accepted");
        // First root fits a nonuniform target; second requires shear. No first-root publication.
        EntityWorld partial;const auto target=partial.CreateEntity("Target"),first=partial.CreateEntity("First"),second=partial.CreateEntity("Second");partial.Find(target)->transform.scale={2,3,1};partial.Find(second)->transform.rotation.z=0.7f;
        EntitySelection partialSelection;Require(partialSelection.Assign(partial,{first,second},second,error),error.c_str());EntityHistory partialHistory;const auto partialBefore=partial.Capture().dump();
        Require(!ReparentSelectedSubtrees(partial,partialSelection,target,true,partialHistory,error) && partial.Capture().dump()==partialBefore && !partialHistory.CanUndo(),"Later shear failure partially moved earlier root");
        partial.Find(target)->transform.scale={0,1,1};const auto singular=partial.Capture().dump();
        Require(!ReparentSelectedSubtrees(partial,partialSelection,target,true,partialHistory,error) && partial.Capture().dump()==singular,"Singular keep-World accepted/published");
        Require(ReparentSelectedSubtrees(partial,partialSelection,target,false,partialHistory,error),error.c_str());
        EntitySelection empty;Require(!ReparentSelectedSubtrees(partial,empty,0,false,partialHistory,error),"Empty selection accepted");
        partial.Find(target)->parent=first;const auto cyclic=partial.Capture().dump();
        Require(!ReparentSelectedSubtrees(partial,partialSelection,0,false,partialHistory,error) && partial.Capture().dump()==cyclic,"Cyclic source accepted/changed");
        std::cout<<"PASS: selected forest root-only reparent, mirrored/rotated keep-World and descendants, local TRS, shared assets, full-selection Undo/Redo/dirty, no-op and budget/history/cycle/singular/later-shear isolation\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
