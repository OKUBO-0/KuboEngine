#include "engine/editor/PrefabMerge.h"
#include <iostream>
#include <stdexcept>
#include <fstream>
namespace {
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
Engine::Scene::SceneValue& Node(Engine::Scene::SceneValue& document,Engine::Scene::EntityId id){for(auto& node:document["world"]["entities"].get<crude_json::array>())if(node["id"].get<crude_json::number>()==double(id))return node;throw std::runtime_error("Missing fixture node");}
void Erase(Engine::Scene::SceneValue& document,Engine::Scene::EntityId id){auto& nodes=document["world"]["entities"].get<crude_json::array>();nodes.erase(std::remove_if(nodes.begin(),nodes.end(),[&](const auto& node){return node["id"].template get<crude_json::number>()==double(id);}),nodes.end());}
}
int main(){
    using namespace Engine::Scene;using namespace Engine::Editor;
    const auto fixture=std::filesystem::temp_directory_path()/(L"kubo-prefab-基礎-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        EntityWorld world;const auto outside=world.CreateEntity("Outside"),root=world.CreateEntity("Root",outside),child=world.CreateEntity("子",root),other=world.CreateEntity("Other",root);
        world.Find(root)->transform.position={2,3,4};world.SetModel(child,"Assets/shared.glb","0123456789abcdef0123456789abcdef");world.SetScript(child,"Assets/move.lua",true,"fedcba9876543210fedcba9876543210");world.SetScriptProperty(child,"speed",1.0);world.SetScriptProperty(child,"remove",true);
        std::string error;PrefabValue base;const auto sceneBefore=world.Capture().dump();Require(CapturePrefabSubtree(world,root,base,error),error.c_str());
        Require(base["world"]["entities"].get<crude_json::array>().size()==3 && Node(base,root)["parent"].get<crude_json::number>()==0 && world.Capture().dump()==sceneBefore,"Prefab capture included external entity/mutated Scene");
        Require(Node(base,child)["modelGuid"].get<crude_json::string>()=="0123456789abcdef0123456789abcdef","Prefab capture lost shared GUID");
        auto next=base,mine=base;Node(next,root)["name"]="Source rename";Node(mine,root)["name"]="Placed instance";
        Node(next,root)["transform"]["scale"].get<crude_json::array>()[0]=2.0;Node(mine,root)["transform"]["position"].get<crude_json::array>()[0]=9.0;
        Node(next,child)["script"]["properties"]["speed"]=3.0;Node(mine,child)["script"]["properties"]["speed"]=7.0;
        Node(next,child)["script"]["properties"]["new"]=std::string("追加");Node(mine,child)["script"]["properties"].erase("remove");
        Node(next,child)["active"]=false;Node(mine,child)["modelGuid"]="11111111111111111111111111111111";
        PrefabValue merged;PrefabMergeReport report;
        Require(MergePrefabDocuments(base,next,mine,merged,report,error),error.c_str());
        Require(Node(merged,root)["name"].get<crude_json::string>()=="Placed instance" && Node(merged,root)["transform"]["position"].get<crude_json::array>()[0].get<crude_json::number>()==9 && Node(merged,root)["transform"]["scale"].get<crude_json::array>()[0].get<crude_json::number>()==2,"Three-way merge lost placement/name or failed to inherit scale");
        const auto& properties=Node(merged,child)["script"]["properties"];
        Require(properties["speed"].get<crude_json::number>()==7 && properties["new"].get<crude_json::string>()=="追加" && !properties.contains("remove") && !Node(merged,child)["active"].get<crude_json::boolean>() && Node(merged,child)["modelGuid"].get<crude_json::string>()=="11111111111111111111111111111111","Merge lost field additions/deletions/overrides/GUID/inheritance");
        Require(!report.inherited.empty() && !report.overrides.empty() && base.dump()!=merged.dump(),"Merge report missing actual changes");
        const auto rootNamePath="entities/"+std::to_string(root)+"/name";
        Require(std::find(report.inherited.begin(),report.inherited.end(),rootNamePath)==report.inherited.end() && std::find(report.overrides.begin(),report.overrides.end(),rootNamePath)==report.overrides.end(),"Instance-owned name incorrectly reported as merged field");
        auto sourceRename=base;Node(sourceRename,root)["name"]="Renamed source";
        Require(MergePrefabDocuments(base,sourceRename,base,merged,report,error) && merged.dump()==base.dump() && report.inherited.empty() && report.overrides.empty(),"Source-only rename affected instance or report");
        Require(MergePrefabDocuments(base,next,mine,merged,report,error),error.c_str());
        // Stable IDs, not array/BFS positions: reordering does not exchange components.
        auto reordered=next;auto& reorderedNodes=reordered["world"]["entities"].get<crude_json::array>();std::reverse(reorderedNodes.begin(),reorderedNodes.end());PrefabValue same;PrefabMergeReport sameReport;
        Require(MergePrefabDocuments(base,reordered,mine,same,sameReport,error) && same.dump()==merged.dump(),"Prefab merge depended on array order");
        auto sourceDeleted=base;Erase(sourceDeleted,other);auto renamedOnly=base;Node(renamedOnly,other)["name"]="Instance name";
        Require(MergePrefabDocuments(base,sourceDeleted,renamedOnly,same,sameReport,error) && same["world"]["entities"].get<crude_json::array>().size()==2 && sameReport.removed==std::vector<EntityId>{other},"Source deletion retained unchanged/name-only instance node");
        auto editedRemoved=base;Node(editedRemoved,other)["active"]=false;
        Require(MergePrefabDocuments(base,sourceDeleted,editedRemoved,same,sameReport,error) && sameReport.retainedRemoved==std::vector<EntityId>{other},"Source deletion discarded edited instance node");
        auto mineDeleted=base;Erase(mineDeleted,other);
        Require(MergePrefabDocuments(base,base,mineDeleted,same,sameReport,error) && sameReport.removed==std::vector<EntityId>{other},"Instance deletion override was resurrected");
        auto added=base;auto sourceNode=Node(added,other);sourceNode["id"]=10.0;sourceNode["name"]="Source addition";added["world"]["entities"].push_back(sourceNode);
        auto mineAdded=base;auto instanceNode=sourceNode;instanceNode["id"]=11.0;instanceNode["name"]="Instance addition";mineAdded["world"]["entities"].push_back(instanceNode);
        Require(MergePrefabDocuments(base,added,mineAdded,same,sameReport,error) && sameReport.added==std::vector<EntityId>({10,11}),"Source/instance independent additions lost");
        auto colliding=base;colliding["world"]["entities"].push_back(sourceNode);const auto held=same.dump();const auto heldReport=sameReport.added;
        Require(!MergePrefabDocuments(base,added,colliding,same,sameReport,error) && same.dump()==held && sameReport.added==heldReport && error.find("collision")!=std::string::npos,"Ambiguous local-ID collision partially published");
        auto parentChanged=base;Node(parentChanged,other)["parent"]=double(child);
        Require(MergePrefabDocuments(base,parentChanged,base,same,sameReport,error) && Node(same,other)["parent"].get<crude_json::number>()==double(child),"Unedited source parent change not inherited");
        auto mineParent=base;Node(mineParent,other)["parent"]=double(child);
        Require(MergePrefabDocuments(base,base,mineParent,same,sameReport,error) && Node(same,other)["parent"].get<crude_json::number>()==double(child),"Instance reparent override lost");
        auto missingParent=base;Erase(missingParent,child);auto customChild=base;auto custom=Node(customChild,other);custom["id"]=12.0;custom["parent"]=double(child);customChild["world"]["entities"].push_back(custom);
        const auto structuralHeld=same.dump();Require(!MergePrefabDocuments(base,missingParent,customChild,same,sameReport,error) && same.dump()==structuralHeld && error.find("conflict")!=std::string::npos,"Source parent removal orphaned custom instance child/partially published");
        auto unknown=base;Node(unknown,child)["futureComponent"]=true;
        Require(!ValidatePrefabDocument(unknown,same,error) && same.dump()==structuralHeld,"Unknown component silently dropped");
        auto cyclic=base;Node(cyclic,child)["parent"]=double(other);Node(cyclic,other)["parent"]=double(child);
        Require(!ValidatePrefabDocument(cyclic,same,error) && same.dump()==structuralHeld,"Cyclic Prefab accepted");
        auto nul=base;Node(nul,root)["name"]=std::string("bad\0name",8);Require(!ValidatePrefabDocument(nul,same,error),"NUL Prefab string accepted");
        auto utf8=base;Node(utf8,root)["name"]=std::string(1,char(0xff));Require(!ValidatePrefabDocument(utf8,same,error),"Invalid UTF-8 Prefab accepted");
        auto tooBig=base;Node(tooBig,root)["name"]=std::string(SceneDocument::MaxBytes+1,'x');Require(!ValidatePrefabDocument(tooBig,same,error),"Oversized Prefab accepted");
        auto disconnected=base;Node(disconnected,other)["parent"]=0.0;Require(!ValidatePrefabDocument(disconnected,same,error),"Disconnected Prefab accepted");
        auto wrongFormat=base;wrongFormat["format"]="kubo-prefab-999";Require(!ValidatePrefabDocument(wrongFormat,same,error),"Unknown Prefab format accepted");
        auto wrongRoot=base;wrongRoot["root"]=double(child);Require(!MergePrefabDocuments(base,wrongRoot,base,same,sameReport,error),"Invalid root change accepted");
        auto changedIdentity=base;Node(changedIdentity,root)["id"]=20.0;changedIdentity["root"]=20.0;
        for(auto& node:changedIdentity["world"]["entities"].get<crude_json::array>())if(node["parent"].get<crude_json::number>()==double(root))node["parent"]=20.0;
        PrefabValue checkedIdentity;Require(ValidatePrefabDocument(changedIdentity,checkedIdentity,error),"Changed identity fixture not independently valid");
        Require(!MergePrefabDocuments(base,changedIdentity,base,same,sameReport,error) && same.dump()==structuralHeld && error.find("identity changed")!=std::string::npos,"Independently valid changed root identity accepted/partially published");
        auto cameraBase=base;Node(cameraBase,root)["camera"]["enabled"]=true;Node(cameraBase,root)["camera"]["nearClip"]=1.0;Node(cameraBase,root)["camera"]["farClip"]=100.0;
        auto cameraNext=cameraBase,cameraMine=cameraBase;Node(cameraNext,root)["camera"]["farClip"]=5.0;Node(cameraMine,root)["camera"]["nearClip"]=10.0;
        const auto cameraHeld=same.dump();Require(!MergePrefabDocuments(cameraBase,cameraNext,cameraMine,same,sameReport,error) && same.dump()==cameraHeld && error.find("conflict")!=std::string::npos,"Individually valid camera fields produced invalid merged component");
        // Property presence is distinct from a JSON null, even though native Lua
        // properties currently reject null. Exercise the pure field merge directly.
        PrefabValue nullValue=nullptr,oldNumber=1.0,newNumber=2.0;std::optional<PrefabValue> field;PrefabMergeReport fieldReport;size_t visits=0;
        Require(MergePrefabField(nullptr,&newNumber,&nullValue,field,fieldReport,"property",0,visits,error) && field && field->is_null(),"Null override confused with absent field");
        Require(MergePrefabField(&oldNumber,&newNumber,nullptr,field,fieldReport,"property",0,visits,error) && !field,"Deleted override resurrected changed source field");
        PrefabValue nested=crude_json::object{};for(size_t depth=0;depth<66;++depth){PrefabValue parent;parent["child"]=std::move(nested);nested=std::move(parent);}size_t structures=0,textBytes=0;
        Require(!PrefabBounds(nested,0,structures,textBytes,error),"Excessively nested Prefab accepted");
        std::filesystem::create_directories(fixture);const auto path=fixture/L"再利用.prefab";
        Require(SavePrefabDocument(path,merged,error),error.c_str());PrefabValue loaded;Require(ReadPrefabDocument(path,loaded,error) && loaded.dump()==merged.dump(),"Unicode Prefab file roundtrip failed");
        Require(!SavePrefabDocument(path,base,error) && ReadPrefabDocument(path,loaded,error) && loaded.dump()==merged.dump(),"Default save overwrote existing Prefab");
        const HANDLE locked=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);Require(locked!=INVALID_HANDLE_VALUE,"Could not lock owned Prefab");
        const bool wroteLocked=SavePrefabDocument(path,base,error,true);CloseHandle(locked);Require(!wroteLocked && ReadPrefabDocument(path,loaded,error) && loaded.dump()==merged.dump(),"Locked Prefab save damaged source");
        Require(SavePrefabDocument(path,base,error,true) && ReadPrefabDocument(path,loaded,error) && loaded.dump()==base.dump(),"Explicit replace failed");
        const auto malformed=fixture/L"malformed.prefab";{std::ofstream file(malformed,std::ios::binary);file<<"{invalid}";}
        const auto loadedHeld=loaded.dump();Require(!ReadPrefabDocument(malformed,loaded,error) && loaded.dump()==loadedHeld,"Failed Prefab read changed output");
        std::filesystem::remove_all(fixture);
        std::cout<<"PASS: bounded canonical Prefab subtree/Unicode atomic files, stable-ID three-way fields/structure, additions/deletions/instance overrides/shared GUIDs and collision/orphan/cycle/schema/budget failure isolation (core only, not live linked-instance UI)\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<"; retained owned fixture: ";std::wcerr<<fixture.native()<<L'\n';return 1;}
}
