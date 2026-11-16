#include "engine/editor/PrefabInstanceCapture.h"
#include "engine/editor/PrefabInstance.h"
#include "engine/editor/PrefabMerge.h"
#include <iostream>
namespace {void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}}
int main(){
    using namespace Engine::Editor;using namespace Engine::Scene;
    const auto fixture=std::filesystem::temp_directory_path()/(L"kubo-prefab-capture-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        EntityWorld source;source.CreateEntity("Root");source.CreateEntity("Child",1);source.CreateEntity("Deleted child",1);
        auto sourceData=source.Capture();sourceData["entities"][size_t(0)]["id"]=10.0;
        sourceData["entities"][size_t(1)]["id"]=30.0;sourceData["entities"][size_t(1)]["parent"]=10.0;
        sourceData["entities"][size_t(2)]["id"]=20.0;sourceData["entities"][size_t(2)]["parent"]=10.0;
        std::string error;Require(source.Restore(sourceData,error),error.c_str());PrefabValue definition;
        Require(CapturePrefabSubtree(source,10,definition,error),error.c_str());
        EntityWorld world;world.CreateEntity("Outside");EntitySelection selection;selection.Replace(1);EntityHistory history;EntityId root=0;
        Require(InstantiatePrefab(world,definition,"0123456789abcdef0123456789abcdef",1,selection,history,root,error),error.c_str());
        const auto initial=world.Capture().dump();PrefabInstanceCapture captured;
        Require(CapturePrefabInstance(world,root,captured,error),error.c_str());
        Require(captured.sceneByLocal.at(10)==root && captured.sceneByLocal.at(20)==4 && captured.sceneByLocal.at(30)==3,"Local map followed array position instead of identity");
        EntityWorld mine;Require(mine.Restore(captured.instance["world"],error) && mine.Find(10)->parent==0 && mine.Find(30)->parent==10,"Capture kept external Scene parent");
        world.Find(root)->name="Instance name";world.Find(root)->transform.position={2,3,4};world.Find(3)->transform.position.x=7;
        Require(world.DestroyEntity(4),"Cannot delete mapped leaf");const auto addition=world.CreateEntity("Added",3),nested=world.CreateEntity("Nested addition",addition);
        const auto edited=world.Capture().dump();const auto selected=selection.Capture();const auto undoCount=history.UndoCount();
        Require(CapturePrefabInstance(world,root,captured,error),error.c_str());
        Require(captured.sceneByLocal.at(20)==4 && captured.sceneByLocal.at(31)==addition && captured.sceneByLocal.at(32)==nested,"Tombstone/addition mapping lost");
        Require(mine.Restore(captured.instance["world"],error) && !mine.Find(20) && mine.Find(31)->parent==30 && mine.Find(32)->parent==31 && mine.Find(10)->name=="Instance name" && mine.Find(10)->transform.position.z==4,"Capture resurrected deletion or lost authored overrides");
        Require(world.Capture().dump()==edited && selection.Capture()==selected && history.UndoCount()==undoCount,"Capture mutated live World/history/selection");
        const auto sceneFile=fixture/L"保存.scene";
        Require(world.Save(sceneFile,error),error.c_str());EntityWorld reopened;Require(reopened.Load(sceneFile,error),error.c_str());PrefabInstanceCapture persisted;
        Require(CapturePrefabInstance(reopened,root,persisted,error) && persisted.instance.dump()==captured.instance.dump() && persisted.sceneByLocal==captured.sceneByLocal,"Scene reload changed tombstones/local capture");
        auto reordered=world.Capture();std::reverse(reordered["entities"].get<crude_json::array>().begin(),reordered["entities"].get<crude_json::array>().end());
        EntityWorld shuffled;Require(shuffled.Restore(reordered,error),error.c_str());PrefabInstanceCapture other;
        Require(CapturePrefabInstance(shuffled,root,other,error) && other.instance.dump()==captured.instance.dump() && other.sceneByLocal==captured.sceneByLocal,"Capture depended on storage order");
        auto incoming=definition;auto sourceAddition=definition["world"]["entities"][size_t(2)];sourceAddition["id"]=31.0;sourceAddition["name"]="Source addition";incoming["world"]["entities"].push_back(sourceAddition);
        Require(CapturePrefabInstance(world,root,captured,error,&incoming) && captured.sceneByLocal.at(32)==addition && captured.sceneByLocal.at(33)==nested,"Incoming additions collided with instance local IDs");
        PrefabValue merged;PrefabMergeReport report;Require(MergePrefabDocuments(captured.baseline,incoming,captured.instance,merged,report,error),error.c_str());
        Require(mine.Restore(merged["world"],error) && !mine.Find(20) && mine.Find(31)->name=="Source addition" && mine.Find(32)->name=="Added" && mine.Find(30)->transform.position.x==7,"Captured stable-ID merge lost source/instance additions or overrides");
        const auto held=captured.instance.dump();const auto heldMap=captured.sceneByLocal;
        Require(!CapturePrefabInstance(world,1,captured,error) && captured.instance.dump()==held && captured.sceneByLocal==heldMap,"Ordinary root capture altered output");
        auto invalid=world;invalid.Find(root)->prefabLink["mapping"][size_t(1)]["entity"]=1.0;
        Require(!CapturePrefabInstance(invalid,root,captured,error) && captured.instance.dump()==held && captured.sceneByLocal==heldMap,"Invalid mapping partially captured");
        auto changedRoot=definition;changedRoot["root"]=11.0;
        for(auto& node:changedRoot["world"]["entities"].get<crude_json::array>()){if(node["id"].get<crude_json::number>()==10)node["id"]=11.0;if(node["parent"].get<crude_json::number>()==10)node["parent"]=11.0;}
        PrefabValue validated;Require(ValidatePrefabDocument(changedRoot,validated,error),"Changed-root fixture is not independently valid");
        Require(!CapturePrefabInstance(world,root,captured,error,&changedRoot) && captured.instance.dump()==held && captured.sceneByLocal==heldMap,"Source root identity change accepted/changed output");
        auto oversized=world;oversized.Find(addition)->name=std::string(SceneDocument::MaxBytes+1,'x');
        Require(!CapturePrefabInstance(oversized,root,captured,error) && captured.instance.dump()==held && captured.sceneByLocal==heldMap,"Oversized capture changed output");
        incoming["world"]["entities"].get<crude_json::array>().back()["id"]=double(EntityWorld::MaximumEntityId);
        Require(!CapturePrefabInstance(world,root,captured,error,&incoming) && captured.instance.dump()==held && captured.sceneByLocal==heldMap && world.Capture().dump()==edited,"Local-ID exhaustion changed output/World");
        auto maximumLocal=definition;maximumLocal["root"]=double(EntityWorld::MaximumEntityId);
        for(auto& node:maximumLocal["world"]["entities"].get<crude_json::array>()){if(node["id"].get<crude_json::number>()==10)node["id"]=double(EntityWorld::MaximumEntityId);if(node["parent"].get<crude_json::number>()==10)node["parent"]=double(EntityWorld::MaximumEntityId);}
        EntityWorld boundary;EntitySelection boundarySelection;EntityHistory boundaryHistory;EntityId boundaryRoot=0;
        Require(InstantiatePrefab(boundary,maximumLocal,"22222222222222222222222222222222",0,boundarySelection,boundaryHistory,boundaryRoot,error) && CapturePrefabInstance(boundary,boundaryRoot,persisted,error),"Maximum local ID without additions was rejected");
        Require(boundary.CreateEntity("Added at exhaustion",boundaryRoot)!=0,"Scene-ID addition fixture failed");
        Require(!CapturePrefabInstance(boundary,boundaryRoot,captured,error) && captured.instance.dump()==held && captured.sceneByLocal==heldMap,"Baseline local-ID exhaustion wrapped/reused identity");
        Require(initial!=edited,"Overrides fixture did not edit Scene");
        std::filesystem::remove_all(fixture);
        std::cout<<"PASS: linked instance stable-local capture, tombstones/additions/parents/overrides, storage-order independence, incoming-source reservations, three-way merge and allocation/schema/output isolation (capture core only)\n";return 0;
    }catch(const std::exception& exception){std::cerr<<"FAIL: "<<exception.what()<<'\n';return 1;}
}
