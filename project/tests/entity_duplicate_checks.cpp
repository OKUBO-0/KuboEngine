#include "engine/scene/entity/EntityWorld.h"
#include "engine/editor/EntityHistory.h"
#include "engine/editor/EntityDocumentState.h"
#include <Windows.h>
#include <iostream>
#include <stdexcept>
namespace {
void Require(bool ok,const char* why) { if (!ok) throw std::runtime_error(why); }
Engine::Scene::SceneValue Entity(const Engine::Scene::SceneValue& world,Engine::Scene::EntityId id) {
    for (const auto& value : world["entities"].get<crude_json::array>())
        if (value["id"].get<crude_json::number>()==double(id)) return value;
    throw std::runtime_error("Missing serialized entity");
}
}
int main() {
    using namespace Engine::Scene;
    const auto fixture=std::filesystem::temp_directory_path() / (L"kubo-duplicate-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        EntityWorld world;
        const auto outside=world.CreateEntity("Outside"), root=world.CreateEntity("ルート",outside),
            child=world.CreateEntity("Child",root), grandchild=world.CreateEntity("Grandchild",child), unrelated=world.CreateEntity("Unrelated");
        world.Find(outside)->transform={{3,4,5},{0.1f,0.3f,-0.2f},{-2,3,4}};
        world.Find(root)->transform={{1,2,3},{0.3f,0.2f,0.1f},{-1,2,3}};
        Require(world.SetModel(root,"Assets/モデル.obj","0123456789abcdef"),"Model setup failed");
        Require(world.SetScript(root,"Assets/処理.lua",false,"fedcba9876543210"),"Script setup failed");
        Require(world.SetCamera(root,{true,0.8f,0.1f,100,true,12}),"Camera setup failed");
        Require(world.SetCollider(child,{{1,2,3},true,0.25f,0.75f}) && world.SetRigidBody(child,{{1,2,3},2,true,true,false}),"Physics setup failed");
        world.Find(grandchild)->active=false;
        const auto before=world.Capture(); const auto text=before.dump();
        Engine::Editor::EntityDocumentState document; document.Opened(world,fixture/"main.scene");
        Engine::Editor::EntityHistory history; EntityId selected=root,created=777; std::string error;
        Require(world.DuplicateSubtree(root,outside,created,error),error.c_str());
        Require(world.GetEntities().size()==8 && created!=root && world.Find(created)->parent==outside,"Duplicate root/count failed");
        const EntityId copiedChild=created+1,copiedGrandchild=created+2;
        Require(world.Find(copiedChild)->parent==created && world.Find(copiedGrandchild)->parent==copiedChild,"Descendant IDs were not remapped");
        const auto after=world.Capture();
        for (const auto pair : {std::pair{root,created},std::pair{child,copiedChild},std::pair{grandchild,copiedGrandchild}}) {
            auto expected=Entity(before,pair.first),actual=Entity(after,pair.second);
            expected["id"]=actual["id"]; expected["parent"]=actual["parent"];
            if (pair.first==root) expected["name"]=expected["name"].get<crude_json::string>()+" Copy";
            Require(actual.dump()==expected.dump(),"Authored component/name/identity payload was not copied exactly");
        }
        Require(Entity(before,unrelated).dump()==Entity(after,unrelated).dump(),"Unrelated entity changed");
        Matrix4x4 originalMatrix{},copyMatrix{}; world.GetWorldMatrix(grandchild,originalMatrix); world.GetWorldMatrix(copiedGrandchild,copyMatrix);
        for (int i=0;i<4;++i) for (int j=0;j<4;++j) Require(originalMatrix.m[i][j]==copyMatrix.m[i][j],"Duplicated subtree world transform changed");
        EntityId camera=0; Matrix4x4 rigid{}; Require(world.GetPrimaryCamera(camera,rigid) && camera==root,"Duplicated primary camera replaced original camera");
        Require(history.Record(before,world,root,created,error),error.c_str()); history.Commit(); selected=created;
        document.Observe(world); Require(document.IsDirty(),"Duplicate did not mark authored document dirty");
        Require(history.UndoCount()==1 && history.Undo(world,selected,error) && selected==root && world.Capture().dump()==text,"Subtree Undo failed");
        document.Observe(world); Require(!document.IsDirty(),"Undo did not restore clean baseline");
        Require(history.Redo(world,selected,error) && selected==created && world.Capture().dump()==after.dump(),"Subtree Redo failed");
        Require(world.Save(fixture/"main.scene",error),error.c_str()); EntityWorld reloaded;
        Require(reloaded.Load(fixture/"main.scene",error) && reloaded.Capture().dump()==after.dump(),"Duplicate save/load did not round-trip");
        const auto unchanged=world.Capture().dump(); created=777;
        Require(!world.DuplicateSubtree(root,outside,created,error,2) && created==777 && world.Capture().dump()==unchanged,"Clone budget failure mutated world/output");
        Require(!world.DuplicateSubtree(999,0,created,error) && !world.DuplicateSubtree(root,999,created,error) && world.Capture().dump()==unchanged,"Missing source/parent mutated world");
        EntityId leaf=0; Require(world.DuplicateSubtree(unrelated,0,leaf,error,1) && world.Find(leaf)->parent==0,"Detached leaf duplicate failed");
        auto malformed=world; malformed.Find(root)->parent=child; const auto corrupt=malformed.Capture().dump();
        Require(!malformed.DuplicateSubtree(root,0,created,error) && malformed.Capture().dump()==corrupt,"Cyclic exposed records were accepted/mutated");
        EntityWorld exhausted; const auto high=exhausted.CreateEntity("High"); exhausted.Find(high)->id=9007199254740991ULL;
        const auto exhaustedBefore=exhausted.Capture().dump();
        Require(!exhausted.DuplicateSubtree(9007199254740991ULL,0,created,error) && exhausted.Capture().dump()==exhaustedBefore,"JSON-exact ID exhaustion mutated world");
        std::error_code cleanup; std::filesystem::remove_all(fixture,cleanup);
        std::cout << "PASS: atomic subtree duplicate, fresh IDs/parent remapping, all components/shared GUIDs, transforms/camera, grouped history/dirty/save and failure isolation\n";
        return 0;
    } catch (const std::exception& error) {
        std::error_code cleanup; std::filesystem::remove_all(fixture,cleanup);
        std::cerr << "FAIL: " << error.what() << '\n'; return 1;
    }
}
