#include "engine/editor/PrefabSourcePublication.h"
#include <fstream>
#include <iostream>
namespace {
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream out(path,std::ios::binary);out<<bytes;out.close();Require(!out.fail(),"Fixture write failed");}
std::string Read(const std::filesystem::path& path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
}
int main(){namespace fs=std::filesystem;using namespace Engine::Editor;using namespace Engine::Assets;using namespace Engine::Scene;
    const auto fixture=fs::temp_directory_path()/(L"kubo-prefab-publication-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        for(const auto& scenario:{"success","conflict","rollback","backup","root-change"}) {
            const auto root=fixture/scenario;fs::create_directories(root/"Assets");Write(root/"Game.kubo",R"({"name":"Publication","startupScene":"Assets/Main.scene"})");
            Engine::Base::ProjectContext project;std::string error;Require(project.Open(root/"Game.kubo",error),error.c_str());
            EntityWorld world;world.CreateEntity("Before");PrefabValue definition;Require(CapturePrefabSubtree(world,1,definition,error),error.c_str());
            const auto file=root/L"Assets/ソース.prefab";Require(SavePrefabDocument(file,definition,error),error.c_str());
            AssetRegistry registry;Require(registry.Scan(root/"Assets",error),error.c_str());ProjectPrefabRevision revision;
            Require(ReadProjectPrefabRevision(project,registry,registry.FindByPath(file)->guid,revision,error),error.c_str());
            auto next=definition;next["world"]["entities"][size_t(0)]["name"]="After";fs::path transaction;
            if(std::string(scenario)=="root-change") {
                auto forged=revision;forged.definition["root"]=2.0;forged.definition["world"]["entities"][size_t(0)]["id"]=2.0;
                auto changedRoot=next;changedRoot["root"]=2.0;changedRoot["world"]["entities"][size_t(0)]["id"]=2.0;
                {PrefabSourcePublication rejected;Require(!rejected.Prepare(project,registry,forged,changedRoot,error),"Copied snapshot root mutation bypassed actual owned root");}
                Require(Read(file)==revision.bytes && !fs::exists(root/"Assets/.kubo/prefab-transactions"/revision.guid),"Root change wrote source/journal");continue;
            }
            {
                PrefabSourcePublication publication;Require(publication.Prepare(project,registry,revision,next,error),error.c_str());transaction=publication.Directory();
                if(std::string(scenario)=="success")Require(publication.Publish(error) && publication.IsPublished(),error.c_str());
                else if(std::string(scenario)=="conflict") {
                    Require(!publication.Publish(error,[&]{Write(file,"external owner data");}),"Competing destination overwritten");
                }else if(std::string(scenario)=="rollback") {
                    Require(!publication.Publish(error,[]{throw std::runtime_error("Injected after park");}),"Injected failure ignored");
                }else {
                    Write(transaction/"original.prefab","foreign backup");Require(!publication.Publish(error),"Occupied backup overwritten");
                }
            }
            Require(fs::exists(transaction/"pending.json") && Read(revision.metadata)==revision.metadataBytes,"Journal/metadata not retained");
            const auto journal=PrefabValue::parse(Read(transaction/"pending.json"));
            Require(journal.is_object() && journal["originalBytes"].get<crude_json::string>()==revision.bytes && journal["nextBytes"].get<crude_json::string>()==next.dump() && journal["metadataBytes"].get<crude_json::string>()==revision.metadataBytes,"Journal did not retain exact source/metadata recovery bytes");
            HeldProjectPrefabWriter writer;Require(!writer.Open(project,registry,revision,error),"New writer bypassed retained pending transaction");
            if(std::string(scenario)=="success") {
                PrefabValue visible;Require(ReadPrefabDocument(file,visible,error) && visible.dump()==next.dump() && Read(transaction/"original.prefab")==revision.bytes && !fs::exists(transaction/"next.prefab"),"Success source/backup identities incorrect");
            }else if(std::string(scenario)=="conflict") {
                Require(Read(file)=="external owner data" && Read(transaction/"original.prefab")==revision.bytes && fs::exists(transaction/"next.prefab"),"Conflict data/backup/stage lost");
            }else if(std::string(scenario)=="rollback") {
                Require(Read(file)==revision.bytes && !fs::exists(transaction/"original.prefab") && fs::exists(transaction/"next.prefab"),"Rollback failed to restore old source safely");
            }else Require(Read(file)==revision.bytes && Read(transaction/"original.prefab")=="foreign backup","Backup collision changed old source/foreign data");
        }
        fs::remove_all(fixture);std::cout<<"PASS: held-handle no-replace Prefab source publication, flushed immutable journal/backup retention, occupied source/backup preservation, injected rollback and pending transaction writer refusal (source core, not Scene/history/crash recovery)\n";return 0;
    }catch(const std::exception& exception){std::cerr<<"FAIL: "<<exception.what()<<" (fixture retained)\n";return 1;}
}
