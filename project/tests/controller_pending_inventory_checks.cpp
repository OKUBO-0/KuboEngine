#include "engine/editor/ControllerPendingTransactions.h"
#include <fstream>
#include <iostream>
static void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void Write(const std::filesystem::path& path,const std::string& text){std::ofstream out(path,std::ios::binary);out<<text;out.close();Need(!out.fail(),"Inventory fixture write failed");}
int main(){namespace fs=std::filesystem;using namespace Engine::Editor;
    const auto root=fs::temp_directory_path()/(L"kubo-controller-inventory-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try{
        fs::create_directories(root/"Assets");Write(root/"entry.kubo",R"({"name":"Inventory","assetDirectory":"Assets","startupScene":"entry.scene"})");Engine::Base::ProjectContext project;std::string error;Need(project.Open(root/"entry.kubo",error),error.c_str());std::vector<ControllerPendingEntry> entries;
        Need(ControllerPendingTransactions::Scan(project,entries,error) && entries.empty(),"Empty inventory failed");
        const auto publication=root/"Assets/.kubo/controller-transactions",creation=root/"Assets/.kubo/controller-creations";
        fs::create_directories(publication/std::string(32,'a'));Write(publication/std::string(32,'a')/"pending.json","not JSON, do not execute");
        fs::create_directories(creation/std::string(32,'b'));Write(creation/L"foreign ### 日本語","preserve foreign record");
        Need(ControllerPendingTransactions::Scan(project,entries,error) && entries.size()==3 && entries[0].kind==ControllerPendingKind::Intent && entries[1].kind==ControllerPendingKind::Incomplete && entries[2].kind==ControllerPendingKind::Unknown && entries[2].guid.empty(),"Inventory classified marker as validity/ignored unknown");
        const auto retained=entries;Write(publication/"extra-file","foreign");Need(ControllerPendingTransactions::Scan(project,entries,error) && entries.size()==4,"Unknown plain entry disappeared");
        for(size_t i=0;i<125;++i)Write(creation/("extra-"+std::to_string(i)),"foreign");
        Need(!ControllerPendingTransactions::Scan(project,entries,error) && entries.size()==4,"Overflow changed prior inventory or silently truncated");
        fs::rename(creation,root/"retained-creations");Write(creation,"malformed parent");Need(!ControllerPendingTransactions::Scan(project,entries,error) && entries.size()==4,"Malformed parent accepted/failed output changed");
        Engine::Base::ProjectContext closed;Need(!ControllerPendingTransactions::Scan(closed,entries,error) && entries.size()==4,"Closed project inventory changed output");
        std::cout<<"INVENTORY_FIXTURE="<<Engine::Base::PathToUtf8(root)<<'\n';std::cout<<"PASS: read-only Controller combined publication/creation inventory/intent presence not validity/incomplete+unknown+UTF labels/128 cap/failed output retention/no mutations\n";return 0;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<"\nRetained inventory fixture: "<<root<<'\n';return 1;}
}
