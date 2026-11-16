#include "engine/editor/ProjectPrefab.h"
#include "engine/editor/PrefabInstance.h"
#include "engine/assets/AssetImportJob.h"
#include <fstream>
#include <iostream>
namespace {
void Require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
void Write(const std::filesystem::path& path,const std::string& bytes){std::filesystem::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out<<bytes;out.close();Require(!out.fail(),"Fixture write failed");}
std::string Read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
}
int main(){
    namespace fs=std::filesystem;using namespace Engine::Editor;using namespace Engine::Assets;using namespace Engine::Scene;
    const auto fixture=fs::temp_directory_path()/(L"kubo-project-prefab-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    const auto prior=fs::current_path();
    try {
        const auto root=fixture/"Project",assets=root/"Content/Assets";fs::create_directories(assets);fs::create_directories(fixture/"Other");
        Write(root/"Game.kubo",R"({"name":"Prefab tests","assetDirectory":"Content/Assets","startupScene":"Content/Assets/Main.scene"})");
        Engine::Base::ProjectContext project;std::string error;Require(project.Open(root/"Game.kubo",error),error.c_str());
        EntityWorld world;const auto parent=world.CreateEntity("Outer"),subtree=world.CreateEntity("日本語 root",parent),child=world.CreateEntity("Child",subtree);
        world.Find(subtree)->transform.position={1,2,3};const auto before=world.Capture().dump();const auto manifest=Read(root/"Game.kubo");
        fs::current_path(fixture/"Other");fs::path saved="sentinel";
        Require(SaveProjectPrefab(project,world,subtree,L"プレハブ/車.PREFAB",saved,error),error.c_str());
        Require(saved==assets/L"プレハブ/車.PREFAB" && world.Capture().dump()==before && Read(root/"Game.kubo")==manifest,"Save mutated Scene/manifest or used cwd");
        Require(!fs::exists(AssetRegistry::MetadataPath(saved)),"Save implicitly generated identity");
        PrefabValue source;Require(ReadPrefabDocument(saved,source,error),error.c_str());EntityWorld captured;Require(captured.Restore(source["world"],error),error.c_str());
        Require(captured.GetEntities().size()==2 && captured.Find(subtree)->parent==0 && captured.Find(child)->parent==subtree && captured.Find(subtree)->transform.position.y==2,"Subtree capture changed local TRS/ancestry");
        const auto sourceBytes=Read(saved);fs::path refused="unchanged";
        Require(!SaveProjectPrefab(project,world,subtree,saved,refused,error) && refused=="unchanged" && Read(saved)==sourceBytes,"Existing source overwritten/output changed");
        AssetRegistry registry;Engine::Assets::AssetImportJob job;Engine::Assets::AssetImportJob::Result imported;
        Require(job.Start({root,assets}),"Cannot start Prefab import worker");const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
        while(!job.Poll(imported)){Require(std::chrono::steady_clock::now()<deadline,"Prefab import worker timed out");Sleep(2);}
        Require(imported.ApplyTo(registry,{root,assets}),imported.error.c_str());const auto* record=registry.FindByPath(saved);
        Require(record && record->type==AssetType::Prefab && AssetRegistry::Classify("UPPER.PREFAB")==AssetType::Prefab && AssetRegistry::Classify("file.scene")==AssetType::Scene,"Prefab classification failed");const auto guid=record->guid;
        Require(record->importFingerprint.size()==64 && registry.IsImportCurrent(*record,root/"Library/Imported"),"Prefab worker did not publish SHA/cache metadata");
        Require(registry.Search("",AssetType::Prefab).size()==1 && registry.SaveDatabase(root/"Library/registry.json",error),error.c_str());
        AssetRegistry persisted;Require(persisted.LoadDatabase(root/"Library/registry.json",error) && persisted.FindByGuid(guid)->type==AssetType::Prefab,"Prefab database roundtrip failed");
        Require(ReadProjectPrefab(project,persisted,guid,source,error),error.c_str());
        EntitySelection selection;selection.Replace(parent);EntityHistory history;EntityId placed=777;
        Require(InstantiatePrefab(world,source,guid,parent,selection,history,placed,error),error.c_str());
        Require(world.Find(placed)->parent==parent && world.Find(placed)->prefabLink["sourceGuid"].get<crude_json::string>()==guid,"Source GUID/parent link failed");
        const auto meta=AssetRegistry::MetadataPath(saved);const auto metadata=Read(meta);PrefabValue output="unchanged";
        fs::rename(meta,meta.wstring()+L".held");
        Require(!ReadProjectPrefab(project,persisted,guid,output,error) && output.is_string(),"Missing metadata manufactured identity/changed output");
        fs::rename(meta.wstring()+L".held",meta);
        Write(meta,R"({"version":1,"guid":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"})");
        Require(!ReadProjectPrefab(project,persisted,guid,output,error) && output.is_string(),"Replaced identity accepted");Write(meta,metadata);
        Write(saved,"{}");Require(!ReadProjectPrefab(project,persisted,guid,output,error) && output.is_string(),"Invalid source accepted/changed output");Write(saved,sourceBytes);
        const auto clone=assets/"Duplicate.prefab";Write(clone,sourceBytes);Write(AssetRegistry::MetadataPath(clone),metadata);
        Require(!ReadProjectPrefab(project,persisted,guid,output,error),"Ambiguous source GUID accepted");
        fs::remove(clone);fs::remove(AssetRegistry::MetadataPath(clone));
        const auto moved=assets/L"移動.prefab";fs::rename(saved,moved);fs::rename(meta,AssetRegistry::MetadataPath(moved));
        Require(!ReadProjectPrefab(project,persisted,guid,output,error),"Stale moved source path accepted");
        Require(persisted.Scan(assets,error) && ReadProjectPrefab(project,persisted,guid,output,error),error.c_str());
        for(const auto& invalid:std::vector<fs::path>{fixture/"escape.prefab",root/"outside.prefab",assets/"wrong.scene",assets/"CON.prefab",assets/"bad./x.prefab",assets/"Library/x.prefab",assets/"Builds/x.prefab",L"../escape.prefab",fs::path(std::wstring(L"bad\0x.prefab",12))}) {
            refused="unchanged";Require(!SaveProjectPrefab(project,world,subtree,invalid,refused,error) && refused=="unchanged","Unsafe destination accepted/changed output");
        }
        Write(AssetRegistry::MetadataPath(assets/"Orphan.prefab"),metadata);Require(!SaveProjectPrefab(project,world,subtree,"Orphan.prefab",refused,error),"Orphan identity destination accepted");
        Write(assets/".kubo/asset-move.pending.json","{}");Require(!SaveProjectPrefab(project,world,subtree,"Pending.prefab",refused,error) && !ReadProjectPrefab(project,persisted,guid,output,error),"Pending move guard bypassed");
        Require(!fs::exists(fixture/"escape.prefab") && Read(root/"Game.kubo")==manifest,"Refusal wrote outside source/manifest");
        fs::current_path(prior);fs::remove_all(fixture);
        std::cout<<"PASS: project-owned Unicode Prefab capture/no-overwrite, type/database/GUID identity, fresh read/move/duplicate/missing metadata guards, candidate placement and unsafe/pending destination refusal\n";return 0;
    } catch(const std::exception& exception){std::error_code ec;fs::current_path(prior,ec);std::cerr<<"FAIL: "<<exception.what()<<" (fixture retained)\n";return 1;}
}
