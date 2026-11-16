#include "engine/editor/EntityGroupTransform.h"
#include "engine/editor/EntityDocumentState.h"
#include <iostream>
#include <stdexcept>
namespace {
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
Matrix4x4 Pose(const Engine::Scene::EntityWorld& world,Engine::Scene::EntityId id){Matrix4x4 value{};Require(world.GetWorldMatrix(id,value),"World matrix missing");return value;}
void Same(const Matrix4x4& a,const Matrix4x4& b){for(int i=0;i<4;++i)for(int j=0;j<4;++j)Require(std::abs(double(a.m[i][j])-b.m[i][j])<0.001*(std::max)(1.0,std::abs(double(a.m[i][j]))),"Group World matrix mismatch");}
}
int main(){
    using namespace Engine::Scene;using namespace Engine::Editor;
    try {
        EntityWorld world;const auto parent=world.CreateEntity("Parent"),root=world.CreateEntity("Root",parent),child=world.CreateEntity("Child",root),other=world.CreateEntity("Other"),outside=world.CreateEntity("Outside");
        world.Find(parent)->transform={{3,1,0},{0,0,0.2f},{-2,2,2}};world.Find(root)->transform={{1,2,0},{13,0,0},{1,-1,1}};world.Find(child)->transform.position={1,2,3};world.Find(other)->transform.position={4,2,1};
        EntitySelection selection;std::string error;Require(selection.Assign(world,{root,child,other},child,error),error.c_str());const auto selected=selection.Capture();
        Matrix4x4 pivot{};Require(BuildGroupPivot(world,selection,false,pivot,error),error.c_str());const auto rootBefore=Pose(world,root),childBefore=Pose(world,child),otherBefore=Pose(world,other),outsideBefore=Pose(world,outside);
        for(int axis=0;axis<3;++axis)Require(std::abs(pivot.m[3][axis]-(rootBefore.m[3][axis]+otherBefore.m[3][axis])*0.5f)<0.0001f,"Pivot counted selected descendant twice");
        const auto before=world.Capture().dump();EntityHistory history;EntityDocumentState document;document.Opened(world,"entry.scene");auto translated=pivot;translated.m[3][0]+=2;translated.m[3][1]-=1;
        const auto original=world.Find(root)->transform;Require(ApplyGroupTransform(world,selection,pivot,translated,0,history,error),error.c_str());document.Observe(world);
        auto translation=GroupIdentity();translation.m[3][0]=2;translation.m[3][1]=-1;
        Same(Pose(world,root),rootBefore*translation);Same(Pose(world,child),childBefore*translation);Same(Pose(world,other),otherBefore*translation);Same(Pose(world,outside),outsideBefore);
        Require(world.Find(root)->transform.rotation.x==original.rotation.x && world.Find(root)->transform.scale.y==original.scale.y && selection.Capture()==selected && document.IsDirty(),"Translation rewrote winding/scale/selection or missed dirty");
        auto translatedAgain=translated;translatedAgain.m[3][0]+=1;Require(ApplyGroupTransform(world,selection,translated,translatedAgain,0,history,error),error.c_str());history.Commit();Require(history.UndoCount()==1,"Group drag frames not grouped");
        Require(history.Undo(world,selection,error) && world.Capture().dump()==before && selection.Capture()==selected,"Group drag Undo lost world/selection");document.Observe(world);Require(!document.IsDirty(),"Group Undo missed baseline");
        Require(history.Redo(world,selection,error),error.c_str());Require(history.Undo(world,selection,error),error.c_str());history.Clear();
        Matrix4x4 localPivot{};Require(BuildGroupPivot(world,selection,true,localPivot,error),error.c_str());Require(std::abs(localPivot.m[0][1])>0.1f,"Primary authored hierarchy rotation frame omitted");
        // Rotation and uniform scaling around the shared pivot, with child propagation.
        auto rotated=pivot;const float angle=0.3f;rotated.m[0][0]=std::cos(angle);rotated.m[0][1]=std::sin(angle);rotated.m[1][0]=-std::sin(angle);rotated.m[1][1]=std::cos(angle);
        Require(ApplyGroupTransform(world,selection,pivot,rotated,1,history,error),error.c_str());history.Commit();auto inversePivot=GroupIdentity();for(int axis=0;axis<3;++axis)inversePivot.m[3][axis]=-pivot.m[3][axis];const auto rotationDelta=inversePivot*rotated;
        Same(Pose(world,root),rootBefore*rotationDelta);Same(Pose(world,child),childBefore*rotationDelta);Same(Pose(world,other),otherBefore*rotationDelta);
        Require(history.Undo(world,selection,error),error.c_str());history.Clear();auto scaled=pivot;for(int axis=0;axis<3;++axis)scaled.m[axis][axis]=1.5f;
        Require(ApplyGroupTransform(world,selection,pivot,scaled,2,history,error),error.c_str());Same(Pose(world,root),rootBefore*(inversePivot*scaled));Same(Pose(world,child),childBefore*(inversePivot*scaled));history.Commit();Require(history.Undo(world,selection,error),error.c_str());history.Clear();
        Require(ApplyGroupTransform(world,selection,pivot,pivot,2,history,error) && !history.CanUndo() && world.Capture().dump()==before,"No-op group created history");
        EntityHistory tiny(100,1);Require(!ApplyGroupTransform(world,selection,pivot,translated,0,tiny,error) && world.Capture().dump()==before && selection.Capture()==selected,"Group history refusal partially published");
        auto singular=pivot;singular.m[0][0]=0;Require(!ApplyGroupTransform(world,selection,singular,translated,0,history,error) && world.Capture().dump()==before,"Singular group source accepted");
        auto nonfinite=translated;nonfinite.m[3][0]=std::numeric_limits<float>::infinity();Require(!ApplyGroupTransform(world,selection,pivot,nonfinite,0,history,error),"Nonfinite group target accepted");
        auto perspective=translated;perspective.m[0][3]=1;Require(!ApplyGroupTransform(world,selection,pivot,perspective,0,history,error),"Non-affine group target accepted");
        Require(!ApplyGroupTransform(world,selection,pivot,scaled,0,history,error) && world.Capture().dump()==before,"Translation operation accepted scale delta");
        EntityWorld shear;const auto a=shear.CreateEntity("First"),b=shear.CreateEntity("Later");shear.Find(b)->transform.rotation.z=0.7f;EntitySelection shearSelection;Require(shearSelection.Assign(shear,{a,b},b,error),error.c_str());Matrix4x4 shearPivot{};Require(BuildGroupPivot(shear,shearSelection,false,shearPivot,error),error.c_str());auto nonuniform=shearPivot;nonuniform.m[0][0]=2;nonuniform.m[1][1]=3;const auto shearBefore=shear.Capture().dump();
        Require(!ApplyGroupTransform(shear,shearSelection,shearPivot,nonuniform,2,history,error) && shear.Capture().dump()==shearBefore,"Later shear failure partially transformed first root");
        auto zeroScale=pivot;zeroScale.m[0][0]=0;Require(!ApplyGroupTransform(world,selection,pivot,zeroScale,2,history,error) && world.Capture().dump()==before,"Zero scale group partially published");
        auto illConditioned=pivot;illConditioned.m[0][0]=1e-12f;Require(!ApplyGroupTransform(world,selection,illConditioned,translated,0,history,error),"Ill-conditioned source pivot accepted");
        EntityWorld stale;const auto staleId=stale.CreateEntity("Removed");EntitySelection staleSelection;staleSelection.Replace(staleId);stale.DestroyEntity(staleId);const auto staleBefore=stale.Capture().dump();
        Require(!ApplyGroupTransform(stale,staleSelection,pivot,translated,0,history,error) && stale.Capture().dump()==staleBefore && staleSelection.Primary()==staleId,"Stale group selection accepted/changed");
        EntityWorld huge;const auto hugeParent=huge.CreateEntity("Huge parent"),hugeChild=huge.CreateEntity("Huge child",hugeParent);huge.Find(hugeParent)->transform.scale={2,2,2};huge.Find(hugeChild)->transform.scale={2e38f,2e38f,2e38f};EntitySelection hugeSelection;hugeSelection.Replace(hugeChild);Matrix4x4 heldPivot=pivot;
        Require(!BuildGroupPivot(huge,hugeSelection,false,heldPivot,error),"Nonfinite composed World accepted for pivot");
        for(int i=0;i<4;++i)for(int j=0;j<4;++j)Require(heldPivot.m[i][j]==pivot.m[i][j],"Failed pivot partially published output");
        EntitySelection empty;Require(!BuildGroupPivot(world,empty,false,pivot,error),"Empty group pivot accepted");
        std::cout<<"PASS: shared root-origin pivot/local authored axes, group translation/rotation/scale, ancestor deduplication, child propagation, winding/sign preservation, grouped full-selection Undo/Redo/dirty and affine/singular/shear/history isolation\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
