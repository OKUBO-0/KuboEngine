#include "engine/assets/ExportControllerDependencies.h"
#include <fstream>
#include <iostream>
namespace {
void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream out(path,std::ios::binary);out<<bytes;out.close();Need(!out.fail(),"Fixture write failed");}
}
int main(){namespace fs=std::filesystem;using namespace Engine::Assets;
    const auto root=fs::temp_directory_path()/(L"kubo-controller-export-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        fs::create_directories(root/"Assets");const auto manifest=root/"Game.kubo";Write(manifest,R"({"name":"Controller export","startupScene":"Assets/Main.scene"})");
        const auto source=root/"Assets/controller.animfsm";
        const std::string good=R"({"format":"kubo-animator-controller-1","parameters":[],"layers":[{"name":"Base","mode":"override","weight":1,"mask":[],"entry":"Idle","states":[{"name":"Idle","clip":"idle","speed":1,"loop":true}],"transitions":[]}]})";
        Write(source,good);Write(root/"Assets/wrong.lua","return {}");AssetRegistry registry;std::string error;Need(registry.Scan(root/"Assets",error),error.c_str());
        const auto guid=registry.FindByPath(source)->guid;const auto meta=AssetRegistry::MetadataPath(source);
        const auto sourceHash=HashAssetContent(source),metaHash=HashAssetContent(meta);
        Need(ValidateExportControllerDependencies(manifest,{guid,guid},{"Assets"})==1,"Export unique GUID count failed");
        Need(HashAssetContent(source)==sourceHash && HashAssetContent(meta)==metaHash,"Export mutated source/identity");
        const auto refuse=[&](const std::vector<std::string>& guids,const std::vector<fs::path>& copies){bool rejected=false;try{ValidateExportControllerDependencies(manifest,guids,copies);}catch(const std::exception&){rejected=true;}Need(rejected,"Invalid Controller export accepted");};
        refuse({guid},{"Uncopied"});refuse({std::string(32,'a')},{"Assets"});refuse({registry.FindByPath(root/"Assets/wrong.lua")->guid},{"Assets"});
        Write(source,"{}");refuse({guid},{"Assets"});Write(source,good);
        fs::rename(meta,meta.wstring()+L".saved");refuse({guid},{"Assets"});Need(!fs::exists(meta),"Export created missing metadata");fs::rename(meta.wstring()+L".saved",meta);
        const auto alias=root/"Alias.animfsm";fs::create_hard_link(source,alias);refuse({guid},{"Assets"});fs::remove(alias);
        Need(ValidateExportControllerDependencies(manifest,{guid},{"Assets"})==1,"Valid Controller export failed after refusals");
        std::cout<<"PASS: export Controller GUID typed source/strict graph+metadata/copied roots/unique closure/read-only hashes/missing+hardlink refusal (not Model clip compatibility or package publication)\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
