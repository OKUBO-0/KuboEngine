#include "engine/editor/ControllerPendingTransactions.h"
#include <fstream>
#include <iostream>
static void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream file(path,std::ios::binary);file<<bytes;file.close();Need(!file.fail(),"Archive inventory fixture write failed");}
int main(){namespace fs=std::filesystem;using namespace Engine::Editor;
    const auto root=fs::temp_directory_path()/(L"kubo-controller-archives-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try{
        fs::create_directories(root/"Assets");Write(root/"entry.kubo",R"({"name":"Archives","assetDirectory":"Assets","startupScene":"entry.scene"})");
        Engine::Base::ProjectContext project;std::string error;Need(project.Open(root/"entry.kubo",error),error.c_str());std::vector<ControllerArchiveEntry> entries;
        Need(ControllerPendingTransactions::ScanArchives(project,entries,error) && entries.empty(),"Empty archive inventory failed");
        const auto parent=root/"Assets/.kubo/controller-quarantine";fs::create_directories(parent);
        const std::string guid(32,'a'),legacy(16,'b');
        const auto publication=parent/("publication-"+guid+"-1-2-3"),creation=parent/("creation-"+guid+"-4-5-6"),old=parent/("publication-"+legacy+"-7-8-9");
        fs::create_directory(publication);Write(publication/"pending.json","invalid marker must not be parsed");fs::create_directory(creation);fs::create_directory(old);
        fs::create_directory(parent/("creation-"+legacy+"-1-2-3"));fs::create_directory(parent/("publication-"+guid+"-1-2"));
        fs::create_directory(parent/("publication-"+guid+"-1-2-x"));fs::create_directory(parent/("publication-"+guid+"-1-2-3-4"));
        fs::create_directory(parent/("publication-"+guid+"-1-2-"+std::string(21,'9')));
        Write(parent/L"foreign ### 日本語","foreign bytes");Write(parent/("creation-"+guid+"-1-2-3"),"not directory");
        Need(ControllerPendingTransactions::ScanArchives(project,entries,error) && entries.size()==10,"Unknown archive records hidden");
        size_t recognized=0;for(const auto& entry:entries)if(entry.diagnosable){++recognized;Need(entry.directory==publication || entry.directory==creation || entry.directory==old,"Invalid name admitted as diagnosable");}
        Need(recognized==3,"Valid filename classification failed");const auto retained=entries;
        Engine::Base::ProjectContext closed;Need(!ControllerPendingTransactions::ScanArchives(closed,entries,error) && entries==retained,"Closed project changed inventory");
        for(size_t i=0;i<119;++i)Write(parent/("extra-"+std::to_string(i)),"foreign");
        Need(!ControllerPendingTransactions::ScanArchives(project,entries,error) && entries==retained,"Archive cap silently truncated/changed output");
        fs::rename(parent,root/"retained-quarantine");Write(parent,"not directory");
        Need(!ControllerPendingTransactions::ScanArchives(project,entries,error) && entries==retained,"Malformed archive parent accepted/output changed");
        Write(root/"entry.kubo",R"({"name":"Changed","assetDirectory":"Other","startupScene":"entry.scene"})");fs::create_directory(root/"Other");
        Need(!ControllerPendingTransactions::ScanArchives(project,entries,error) && entries==retained,"Changed manifest admitted/output changed");
        std::cout<<"ARCHIVE_INVENTORY_FIXTURE="<<Engine::Base::PathToUtf8(root)<<'\n';std::cout<<"PASS: read-only quarantine inventory/recognized family+GUID+numeric suffix not content validity/unknown UTF and plain files preserved/empty/closed/root/malformed parent/128 cap/failed output retention (no Editor startup/render)\n";return 0;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<"\nRetained fixture: "<<root<<'\n';return 1;}
}
