#include "engine/editor/PrefabInstance.h"
#include "engine/editor/EntityBatchDelete.h"
#include "engine/editor/EntityDocumentState.h"
#include <iostream>
#include <stdexcept>
namespace {void Require(bool result,const char* message){if(!result)throw std::runtime_error(message);}}
int main(){
    using namespace Engine::Scene;using namespace Engine::Editor;
    const auto fixture=std::filesystem::temp_directory_path()/(L"kubo-prefab-link-履歴-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        EntityWorld source;const auto a=source.CreateEntity("Root"),b=source.CreateEntity("Child",a);
        source.SetModel(b,"Assets/shared.glb","0123456789abcdef0123456789abcdef");
        std::string error;PrefabValue definition;Require(CapturePrefabSubtree(source,a,definition,error),error.c_str());
        EntityWorld world;const auto outside=world.CreateEntity("External");EntitySelection selection;selection.Replace(outside);const auto oldSelection=selection.Capture();
        EntityHistory history;EntityId root=777;const auto before=world.Capture().dump();
        Require(InstantiatePrefab(world,definition,"11111111111111111111111111111111",outside,selection,history,root,error),error.c_str());
        Require(root==2 && world.Find(root)->parent==outside && world.Find(3)->parent==root && selection.Primary()==root && world.Find(3)->model.assetGuid==source.Find(b)->model.assetGuid,"Instantiate lost parent/selection/shared identity");
        const auto linked=world.Capture().dump();Require(!world.Find(root)->prefabLink.is_null() && history.Undo(world,selection,error) && world.Capture().dump()==before && selection.Capture()==oldSelection,"Instantiation Undo lost world/selection");
        Require(history.Redo(world,selection,error) && world.Capture().dump()==linked,"Instantiation Redo lost link/map/baseline");
        EntityId second=0;
        Require(InstantiatePrefab(world,definition,"11111111111111111111111111111111",outside,selection,history,second,error) && second==4 && world.Find(5)->parent==second,"Independent instance Scene maps collided");
        Require(history.Undo(world,selection,error) && world.Capture().dump()==linked && selection.Primary()==root,"Second instance Undo changed original link");
        std::filesystem::create_directories(fixture);const auto path=fixture/L"保存.scene";
        Require(world.Save(path,error),error.c_str());EntityWorld reopened;Require(reopened.Load(path,error) && reopened.Capture().dump()==linked,"Linked Scene save/reopen lost metadata");
        auto captured=definition;Require(CapturePrefabSubtree(world,root,captured,error) && !captured["world"]["entities"].get<crude_json::array>()[0].contains("prefabLink"),"Prefab capture exported instance link");
        auto invalidDefinition=definition;invalidDefinition["world"]["entities"].get<crude_json::array>()[0]["prefabLink"]=world.Find(root)->prefabLink;
        Require(!ValidatePrefabDocument(invalidDefinition,captured,error),"Definition accepted instance metadata");
        const auto bad=[&](SceneValue document,const char* reason){const auto held=reopened.Capture().dump();Require(!reopened.Restore(document,error) && reopened.Capture().dump()==held,reason);};
        auto document=world.Capture();auto& link=document["entities"].get<crude_json::array>()[1]["prefabLink"];
        link["mapping"].get<crude_json::array>()[1]["entity"]=double(outside);bad(document,"Outside mapping accepted/partially published");
        document=world.Capture();document["entities"].get<crude_json::array>()[1]["prefabLink"]["sourceGuid"]="invalid";bad(document,"Malformed source GUID accepted");
        document=world.Capture();document["entities"].get<crude_json::array>()[1]["prefabLink"]["mapping"].get<crude_json::array>()[1]["local"]=1.0;bad(document,"Repeated local identity accepted");
        document=world.Capture();document["entities"].get<crude_json::array>()[1]["prefabLink"]["base"]["entities"].get<crude_json::array>()[0]["prefabLink"]=crude_json::object{};bad(document,"Recursive baseline link accepted");
        document=world.Capture();document["entities"].get<crude_json::array>()[1]["prefabLink"]["base"]["entities"].get<crude_json::array>()[0]["futureComponent"]=true;bad(document,"Unknown baseline component silently dropped");
        document=world.Capture();document["entities"].get<crude_json::array>()[1]["prefabLink"]["base"]["entities"].get<crude_json::array>()[0]["name"]=std::string("bad\0name",8);bad(document,"NUL baseline text accepted");
        document=world.Capture();document["entities"].get<crude_json::array>()[1]["prefabLink"]["base"]["entities"].get<crude_json::array>()[0]["name"]=std::string(1,char(0xff));bad(document,"Invalid UTF-8 baseline text accepted");
        document=world.Capture();document["entities"].get<crude_json::array>()[1]["prefabLink"]["base"]["entities"].get<crude_json::array>()[0]["name"]=std::string(SceneDocument::MaxBytes+1,'x');bad(document,"Oversized link baseline accepted");
        SceneValue deep=crude_json::object{};for(size_t depth=0;depth<66;++depth){SceneValue wrapper;wrapper["child"]=std::move(deep);deep=std::move(wrapper);}
        document=world.Capture();document["entities"].get<crude_json::array>()[1]["prefabLink"]["future"]=std::move(deep);bad(document,"Deep link JSON accepted");Require(error.find("budget")!=std::string::npos,"Deep link was not refused before recursive schema handling");
        auto exhausted=world.Capture();exhausted["entities"].get<crude_json::array>()[1]["prefabLink"]["mapping"].get<crude_json::array>()[1]["entity"]=9007199254740991.0;
        EntityWorld reserved;Require(reserved.Restore(exhausted,error),"Valid tombstone ID exhaustion fixture refused");EntitySelection reservedSelection;reservedSelection.Replace(root);EntityHistory reservedHistory;EntityId sentinel=777;
        Require(!InstantiatePrefab(reserved,definition,"22222222222222222222222222222222",0,reservedSelection,reservedHistory,sentinel,error) && reserved.Capture().dump()==exhausted.dump() && sentinel==777,"Reserved-ID exhaustion partially instantiated");
        auto candidate=world;candidate.Find(root)->prefabLink["future"]=true;Require(!candidate.Save(path,error) && reopened.Load(path,error) && reopened.Capture().dump()==linked,"Invalid link save replaced valid Scene");
        EntityHistory tiny(4,1);EntityId untouched=999;const auto selectionBefore=selection.Capture();
        Require(!InstantiatePrefab(world,definition,"22222222222222222222222222222222",0,selection,tiny,untouched,error) && world.Capture().dump()==linked && selection.Capture()==selectionBefore && untouched==999,"History refusal partially instantiated");
        Require(!InstantiatePrefab(world,definition,"22222222222222222222222222222222",root,selection,history,untouched,error) && world.Capture().dump()==linked,"Unsupported nested instance partially published");
        EntityDocumentState documentState;documentState.Opened(world,path);
        Require(UnlinkPrefab(world,root,selection,history,error) && world.Find(root)->prefabLink.is_null(),"Unlink failed");
        documentState.Observe(world);Require(documentState.IsDirty(),"Link-only edit did not dirty Scene");
        Require(history.Undo(world,selection,error) && world.Capture().dump()==linked && history.Redo(world,selection,error) && world.Find(root)->prefabLink.is_null(),"Unlink history lost exact link");
        Require(history.Undo(world,selection,error),error.c_str());
        documentState.Observe(world);Require(!documentState.IsDirty(),"Undo of link-only edit did not restore saved dirty baseline");
        EntityId copy=0;Require(world.DuplicateSubtree(root,outside,copy,error) && world.Find(copy)->prefabLink.is_null() && !world.Find(root)->prefabLink.is_null(),"Legacy duplication did not detach clone/retained source");
        // A deleted mapped ID stays reserved after save/reopen, even if it was
        // larger than every remaining live Entity ID.
        world=reopened;Require(world.DestroyEntity(3),"Leaf deletion failed");
        Require(world.Save(path,error) && reopened.Load(path,error) && reopened.CreateEntity("Not reused")==4,"Deleted mapped identity reused after reload");
        world=reopened;selection.Replace(root);history.Clear();const auto withTombstone=world.Capture().dump();
        Require(DeleteSelectedSubtrees(world,selection,history,error) && !world.Find(root),"Linked subtree deletion failed");
        Require(history.Undo(world,selection,error) && world.Capture().dump()==withTombstone,"Subtree deletion Undo lost link/tombstone");
        std::filesystem::remove_all(fixture);
        std::cout<<"PASS: linked Prefab candidate instantiation, baked components/shared GUIDs, local/Scene maps and tombstone IDs, Scene Unicode save/reopen, full-selection Undo/Redo/unlink/delete, detached legacy copies and schema/nesting/history failure isolation (no Apply/Revert UI yet)\n";return 0;
    }catch(const std::exception& exception){std::cerr<<"FAIL: "<<exception.what()<<"; retained owned fixture: ";std::wcerr<<fixture.native()<<L'\n';return 1;}
}
