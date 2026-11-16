#include "engine/editor/EntityGizmoTransform.h"
#include "engine/editor/EntityHistory.h"
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
void Require(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
void Same(const Matrix4x4& a,const Matrix4x4& b) {
    for (int i=0;i<4;++i) for (int j=0;j<4;++j)
        Require(std::abs(a.m[i][j]-b.m[i][j])<0.002f*(std::max)(1.0f,std::abs(a.m[i][j])),"Gizmo world reconstruction mismatch");
}
}
int main() {
    using namespace Engine::Scene;
    using namespace Engine::Editor;
    try {
        EntityWorld world;
        const auto parentId=world.CreateEntity("Parent"), child=world.CreateEntity("Child",parentId);
        world.Find(parentId)->transform={{4,-2,6},{0.3f,-0.4f,0.7f},{-2,3,0.5f}};
        world.Find(child)->transform={{1,2,3},{0.2f,0.3f,-0.4f},{-1,2,-3}};
        const auto original=world.Find(child)->transform;
        Matrix4x4 parent; Require(world.GetWorldMatrix(parentId,parent),"Parent world unavailable");
        std::string error;
        for (const auto desired : {TransformComponent{{7,-4,2},{0.4f,0.5f,-0.6f},{-2,4,-1}},
            TransformComponent{{0,1,0},{0.8f,std::numbers::pi_v<float>/2,-0.6f},{-1,2,-3}},
            TransformComponent{{0,1,0},{-0.8f,-std::numbers::pi_v<float>/2,0.6f},{-1,2,-3}}}) {
            world.Find(child)->transform=desired; Matrix4x4 target; Require(world.GetWorldMatrix(child,target),"Target world unavailable");
            world.Find(child)->transform=original;
            TransformComponent result;
            Require(GizmoWorldToLocal(target,parent,original,result,error),error.c_str());
            world.Find(child)->transform=result; Matrix4x4 rebuilt; world.GetWorldMatrix(child,rebuilt); Same(target,rebuilt);
            Require(result.scale.x<0 && result.scale.y>0 && result.scale.z<0,"Negative scale signs lost");
        }
        world.Find(child)->transform=original;
        Matrix4x4 target; world.GetWorldMatrix(child,target);
        // Pure world translation retains the existing child orientation/scale.
        target.m[3][0]+=10; target.m[3][1]-=3;
        TransformComponent result;
        Require(GizmoWorldToLocal(target,parent,original,result,error),error.c_str());
        world.Find(child)->transform=result; Matrix4x4 rebuilt; world.GetWorldMatrix(child,rebuilt); Same(target,rebuilt);
        TransformComponent unchanged=original;
        auto singular=parent; for (int j=0;j<3;++j) singular.m[0][j]=0;
        Require(!GizmoWorldToLocal(target,singular,original,unchanged,error) && unchanged.position.x==original.position.x,"Singular parent was accepted/mutated output");
        auto nonfinite=target; nonfinite.m[1][0]=std::numeric_limits<float>::infinity();
        Require(!GizmoWorldToLocal(nonfinite,parent,original,unchanged,error),"Nonfinite matrix accepted");
        auto projective=target; projective.m[0][3]=0.1f;
        Require(!GizmoWorldToLocal(projective,parent,original,unchanged,error),"Projective matrix accepted");
        Matrix4x4 identity{}; for (int i=0;i<4;++i) identity.m[i][i]=1;
        auto shear=identity; shear.m[0][1]=0.5f;
        Require(!GizmoWorldToLocal(shear,identity,{},unchanged,error),"Shear silently approximated");
        auto degenerate=identity; degenerate.m[1][1]=0;
        Require(!GizmoWorldToLocal(degenerate,identity,{},unchanged,error),"Degenerate scale accepted");
        // A continuous Y rotation must not flip X/Z by pi at the asin branch.
        for (float direction : {-1.0f,1.0f}) {
            auto previous=original; previous.rotation={0.35f,direction*1.29f,-0.65f};
            for (int step=0;step<=50;++step) {
                auto desired=previous; desired.rotation={0.35f,direction*(1.3f+step*0.01f),-0.65f};
                world.Find(child)->transform=desired; world.GetWorldMatrix(child,target);
                Require(GizmoWorldToLocal(target,parent,previous,result,error),error.c_str());
                Require(std::abs(result.rotation.x-desired.rotation.x)<0.002f &&
                    std::abs(result.rotation.y-desired.rotation.y)<0.002f &&
                    std::abs(result.rotation.z-desired.rotation.z)<0.002f,"Continuous Y rotation flipped its Euler branch");
                world.Find(child)->transform=result; world.GetWorldMatrix(child,rebuilt); Same(target,rebuilt);
                previous=result;
            }
        }
        auto previous=original; previous.rotation={0.35f,-6.45f,-0.65f};
        for (int step=0;step<=256;++step) {
            auto desired=previous; desired.rotation={0.35f,-6.4f+step*0.05f,-0.65f};
            world.Find(child)->transform=desired; world.GetWorldMatrix(child,target);
            Require(GizmoWorldToLocal(target,parent,previous,result,error),error.c_str());
            Require(std::abs(result.rotation.x-desired.rotation.x)<0.002f &&
                std::abs(result.rotation.y-desired.rotation.y)<0.002f &&
                std::abs(result.rotation.z-desired.rotation.z)<0.002f,"Multi-turn Y rotation was wrapped or changed branch");
            world.Find(child)->transform=result; world.GetWorldMatrix(child,rebuilt); Same(target,rebuilt);
            previous=result;
        }
        for (float pitch : {-std::numbers::pi_v<float>/2,std::numbers::pi_v<float>/2}) {
            auto desired=original; desired.rotation={4*std::numbers::pi_v<float>+0.35f,pitch,-0.65f};
            world.Find(child)->transform=desired; world.GetWorldMatrix(child,target);
            Require(GizmoWorldToLocal(target,parent,desired,result,error),error.c_str());
            Require(std::abs(result.rotation.x-desired.rotation.x)<0.002f &&
                std::abs(result.rotation.z-desired.rotation.z)<0.002f,"Gimbal representation did not retain reference winding");
            world.Find(child)->transform=result; world.GetWorldMatrix(child,rebuilt); Same(target,rebuilt);
        }
        // Real authored snapshots merge repeated drag changes into one command.
        world.Find(child)->transform=original;
        const auto start=world.Capture().dump();
        EntityHistory history; history.Commit(); EntityId selection=child;
        for (int step=1;step<=5;++step) {
            const auto before=world.Capture(); world.GetWorldMatrix(child,target); target.m[3][0]+=float(step);
            Require(GizmoWorldToLocal(target,parent,world.Find(child)->transform,result,error),error.c_str());
            world.Find(child)->transform=result;
            Require(history.Record(before,world,child,child,error),error.c_str());
        }
        const auto end=world.Capture().dump(); history.Commit();
        Require(history.UndoCount()==1 && history.Undo(world,selection,error) && world.Capture().dump()==start,"Gizmo drag did not undo as one command");
        Require(history.Redo(world,selection,error) && world.Capture().dump()==end && selection==child,"Gizmo drag redo failed");
        std::cout << "PASS: parent-aware gizmo TRS, negative scales, gimbal/continuous/multi-turn Euler branches, world translation, shear/singular/nonfinite isolation and grouped Undo/Redo\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
