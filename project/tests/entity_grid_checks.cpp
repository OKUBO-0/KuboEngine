#include "engine/editor/EntityGridAlignment.h"
#include "engine/editor/EntityDocumentState.h"
#include <iostream>
#include <stdexcept>
namespace {void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}}
int main() {
    using namespace Engine::Scene;using namespace Engine::Editor;
    try {
        EntityWorld world;const auto root=world.CreateEntity("Root"),child=world.CreateEntity("Child",root);
        world.Find(root)->transform={{4,-2,6},{0.3f,-0.4f,0.7f},{-2,3,0.5f}};
        world.Find(child)->transform={{1.17f,2.26f,3.49f},{13,0.3f,-0.4f},{-1,2,-3}};
        const auto original=world.Find(child)->transform;
        const auto before=world.Capture().dump();Matrix4x4 oldWorld{};world.GetWorldMatrix(child,oldWorld);
        EntityHistory history;EntityDocumentState document;document.Opened(world,"entry.scene");std::string error;
        Require(AlignEntityToWorldGrid(world,child,0.5f,history,error),error.c_str());document.Observe(world);
        Require(document.IsDirty() && history.UndoCount()==1,"Grid alignment missing dirty/history");
        Matrix4x4 aligned{};world.GetWorldMatrix(child,aligned);
        for(int axis=0;axis<3;++axis) Require(std::abs(aligned.m[3][axis]-std::round(oldWorld.m[3][axis]/0.5f)*0.5f)<0.00005f,"Parent-composed world position not aligned");
        const auto actual=world.Find(child)->transform;
        Require(actual.rotation.x==original.rotation.x && actual.rotation.y==original.rotation.y && actual.rotation.z==original.rotation.z &&
            actual.scale.x==original.scale.x && actual.scale.y==original.scale.y && actual.scale.z==original.scale.z,"Authored winding/scale changed");
        const auto after=world.Capture().dump();EntityId selection=child;
        Require(AlignEntityToWorldGrid(world,child,0.5f,history,error) && world.Capture().dump()==after && history.UndoCount()==1,"Parented repeated alignment drifted/created history");
        Require(history.Undo(world,selection,error) && world.Capture().dump()==before,"Grid undo failed");document.Observe(world);Require(!document.IsDirty(),"Grid undo did not restore saved baseline");
        Require(history.Redo(world,selection,error) && world.Capture().dump()==after && selection==child,"Grid redo failed");
        world.Find(root)->transform={};world.Find(child)->transform={{0.25f,-0.25f,0.74f},{},{1,1,1}};history.Clear();
        Require(AlignEntityToWorldGrid(world,child,0.5f,history,error) && world.Find(child)->transform.position.x==0.5f && world.Find(child)->transform.position.y==-0.5f && world.Find(child)->transform.position.z==0.5f,"Positive/negative half-step policy failed");
        Require(AlignEntityToWorldGrid(world,child,0.5f,history,error) && history.UndoCount()==1,"Already aligned command created extra history");
        for(float bad:{0.0f,-1.0f,1000001.0f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
            const auto snapshot=world.Capture().dump();Require(!AlignEntityToWorldGrid(world,child,bad,history,error) && world.Capture().dump()==snapshot,"Invalid step mutated world");
        }
        Require(!AlignEntityToWorldGrid(world,999,1,history,error),"Missing Entity accepted");
        world.Find(root)->transform.scale.x=0;const auto singular=world.Capture().dump();
        Require(!AlignEntityToWorldGrid(world,child,1,history,error) && world.Capture().dump()==singular,"Singular parent mutated world");
        world.Find(root)->transform={};world.Find(child)->transform.position={0.2f,0.3f,0.4f};
        EntityHistory tooSmall(100,1);const auto rejected=world.Capture().dump();
        Require(!AlignEntityToWorldGrid(world,child,1,tooSmall,error) && world.Capture().dump()==rejected && !tooSmall.CanUndo(),"Rejected history retained mutation");
        world.Find(child)->transform.position={100000003.0f,0,0};const auto coarse=world.Capture().dump();
        Require(!AlignEntityToWorldGrid(world,child,3,history,error) && world.Capture().dump()==coarse,"Unrepresentable grid accepted/mutated world");
        world.Find(root)->transform.position={100000000.0f,0,0};world.Find(child)->transform.position={-99999960.0f,0.3f,0.4f};
        const auto cancellation=world.Capture().dump();
        Require(!AlignEntityToWorldGrid(world,child,3,history,error) && world.Capture().dump()==cancellation,"Parent/local precision loss accepted/mutated world");
        std::cout<<"PASS: world grid parent/negative-scale translation, authored winding preservation, half steps, independent Undo/Redo/dirty, no-op and rejection rollback\n";return 0;
    } catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
