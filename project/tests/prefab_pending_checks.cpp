#include "engine/editor/PrefabPendingTransactions.h"
#include <fstream>
#include <iostream>
namespace {
namespace fs=std::filesystem;using namespace Engine::Editor;
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void Write(const fs::path& path,const std::string& bytes){std::ofstream out(path,std::ios::binary);out<<bytes;out.close();Require(!out.fail(),"Fixture write failed");}
}
int main(){
    const auto fixture=fs::temp_directory_path()/(L"kubo-prefab-pending-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        for(const auto& scenario:{"absent","empty","history","publication","incomplete","both","unknown-file","unknown-directory","linked-marker","custom-root","parent-file","root-file","budget","closed"}){
            const std::string mode=scenario;const auto root=fixture/scenario;fs::create_directories(root);Write(root/"Game.kubo",mode=="custom-root"?R"({"name":"Pending","assetDirectory":"Content/素材"})":R"({"name":"Pending"})");
            Engine::Base::ProjectContext project;std::string error;Require(project.Open(root/"Game.kubo",error),error.c_str());const auto assets=project.ResolvePath(project.GetSettings().assetDirectory);fs::create_directories(assets);
            const auto pending=assets/".kubo/prefab-transactions";const std::string guid(32,'a');
            if(mode!="absent" && mode!="root-file")fs::create_directories(pending);
            if(mode=="history" || mode=="publication" || mode=="incomplete" || mode=="both" || mode=="custom-root" || mode=="linked-marker"){
                fs::create_directory(pending/guid);
                if(mode=="history" || mode=="both" || mode=="custom-root" || mode=="linked-marker")Write(pending/guid/"history.json","untrusted marker, not parsed by inventory");
                if(mode=="publication" || mode=="both")Write(pending/guid/"pending.json","untrusted journal");
                if(mode=="linked-marker")Require(CreateHardLinkW((root/"marker-link").c_str(),(pending/guid/"history.json").c_str(),nullptr)!=0,"Fixture link failed");
            }
            if(mode=="unknown-file")Write(pending/guid,"retain unknown file");
            if(mode=="unknown-directory")fs::create_directory(pending/L"unknown-日本語");
            if(mode=="parent-file"){fs::rename(pending,assets/"retained-directory");Write(pending,"unknown namespace owner");}
            if(mode=="root-file"){fs::rename(assets,root/"retained-assets");Write(assets,"unknown root owner");}
            if(mode=="budget")for(size_t i=0;i<=PrefabPendingTransactions::MaxEntries;++i)fs::create_directory(pending/std::to_string(i));
            if(mode=="closed")project={};
            std::vector<PrefabPendingEntry> output(1);output[0].name="unchanged";const bool accepted=mode!="parent-file" && mode!="root-file" && mode!="budget" && mode!="closed";
            Require(PrefabPendingTransactions::Scan(project,output,error)==accepted,"Unexpected pending inventory result");
            if(!accepted){Require(output.size()==1 && output[0].name=="unchanged","Failed pending inventory published output");continue;}
            if(mode=="absent" || mode=="empty"){Require(output.empty(),"Empty pending inventory blocked authoring");continue;}
            const auto kind=mode=="history" || mode=="custom-root" || mode=="linked-marker"?PrefabPendingKind::History:mode=="publication"?PrefabPendingKind::Publication:mode=="incomplete"?PrefabPendingKind::Incomplete:PrefabPendingKind::Unknown;
            Require(output.size()==1 && output[0].kind==kind && output[0].directory.parent_path()==pending,"Pending inventory lost namespace/kind");
            if(mode=="linked-marker")Require(fs::exists(root/"marker-link") && fs::exists(pending/guid/"history.json"),"Inventory mutated linked marker");
        }
        fs::remove_all(fixture);std::cout<<"PASS: bounded read-only pending Prefab namespace inventory, history/publication/incomplete/unknown preservation, custom Unicode asset roots and fail-closed malformed/root/budget/output handling (marker contents untrusted)\n";return 0;
    }catch(const std::exception& exception){std::cerr<<"FAIL: "<<exception.what()<<" (owned fixture retained)\n";return 1;}
}
