#include "engine/editor/AnimatorControllerSelection.h"
#include "engine/editor/AnimatorControllerEdits.h"
#include "engine/assets/ExportControllerDependencies.h"
#include <fstream>
#include <iostream>
static void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream stream(path,std::ios::binary);stream<<bytes;stream.close();Need(!stream.fail(),"Pending fixture write failed");}
int main(){namespace fs=std::filesystem;using namespace Engine::Assets;
    const auto fixture=fs::temp_directory_path()/(L"kubo-controller-pending-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try{
        for(const char* kind:{"controller-transactions","controller-creations","unrelated","malformed-parent","empty"}){
            const std::string mode=kind;const auto root=fixture/kind;fs::create_directories(root/"Assets");Write(root/"entry.kubo",R"({"name":"Pending Controller","assetDirectory":"Assets","startupScene":"entry.scene"})");
            Engine::Base::ProjectContext project;std::string error;Need(project.Open(root/"entry.kubo",error),error.c_str());Engine::Editor::AnimatorControllerDocument document;Need(Engine::Editor::AnimatorControllerEdits(document).New(error),error.c_str());const auto file=root/"Assets/main.animfsm";Write(file,document.Text());AssetRegistry registry;Need(registry.Scan(root/"Assets",error),error.c_str());const auto guid=registry.FindByPath(file)->guid;
            HeldAnimatorControllerAsset reader;Need(reader.Open(project,registry,guid,error),error.c_str());const auto expected=*reader.Snapshot();
            fs::path pending;
            if(mode=="malformed-parent"){fs::create_directory(root/"Assets/.kubo");pending=root/"Assets/.kubo/controller-transactions";Write(pending,"not a directory");}
            else{const auto category=mode=="controller-creations"?"controller-creations":"controller-transactions";pending=root/"Assets/.kubo"/category;if(mode!="empty")pending/=(mode=="unrelated"?std::string(32,'a'):guid);fs::create_directories(pending);}
            const bool readable=mode=="empty" || mode=="unrelated";
            Need(reader.Open(project,registry,guid,error,&expected)==readable && reader.Snapshot()->sourceIdentity==expected.sourceIdentity && reader.Snapshot()->bytes==expected.bytes,"Pending reader rule/failed-open retention violated");reader.Release();
            Engine::AnimationSystem::AnimatorComponent current,selected;const auto before=Engine::AnimationSystem::EncodeAnimator(selected).dump();
            Need(Engine::Editor::PrepareAnimatorControllerSelection(project,registry,current,guid,selected,error)==readable,"Pending selection policy violated");
            if(!readable)Need(Engine::AnimationSystem::EncodeAnimator(selected).dump()==before,"Refused pending selection changed authored output");
            for(const auto& guids:{std::vector<std::string>{guid},std::vector<std::string>{}}){bool accepted=true;try{ValidateExportControllerDependencies(root/"entry.kubo",guids,{"Assets"});}catch(const std::exception&){accepted=false;}Need(accepted==(mode=="empty"),"Export allowed pending/unreferenced/incomplete Controller records");}
            if(mode!="empty"){const auto retained=root/"retained-pending";fs::rename(pending,retained);Need(reader.Open(project,registry,guid,error,&expected),"Resolved normal admission did not recover");reader.Release();Need(ValidateExportControllerDependencies(root/"entry.kubo",{guid},{"Assets"})==1,"Resolved export did not recover");}
        }
        std::cout<<"PENDING_FIXTURE="<<Engine::Base::PathToUtf8(fixture)<<'\n';std::cout<<"PASS: normal reader/expected revision/selection pending denial+retention; unrelated GUID read; export global incomplete/unreferenced/malformed refusal; empty/resolved admission (observation not tree lock)\n";return 0;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<"\nRetained pending fixture: "<<fixture<<'\n';return 1;}
}
