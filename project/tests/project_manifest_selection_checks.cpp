#include "engine/base/ProjectManifestSelection.h"
#include <Windows.h>
#include <fstream>
#include <iostream>
static void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static void Write(const std::filesystem::path& path,const char* text){std::ofstream out(path,std::ios::binary);out<<text;out.close();Need(!out.fail(),"Fixture write failed");}
int main(){namespace fs=std::filesystem;using namespace Engine::Base;
    const auto root=fs::temp_directory_path()/(L"kubo-manifest-selection-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    const auto original=fs::current_path();
    try {
        fs::create_directories(root/"Assets");fs::create_directories(root/"Foreign");
        const auto selected=root/L"選択.KUBO";
        Write(selected,R"({"name":"Selected","assetDirectory":"Assets","startupScene":"entry.scene"})");
        Write(root/"Other.kubo",R"({"name":"Other","assetDirectory":"OtherAssets","startupScene":"other.scene"})");
        Write(root/"Foreign/Game.kubo",R"({"name":"Foreign","startupScene":"entry.scene"})");
        ProjectContext output;std::string error;
        fs::current_path(root/"Foreign");
        Need(OpenProjectManifestAtRoot(root,selected,output,error) && output.GetSettings().name=="Selected",error.c_str());
        Need(OpenProjectManifestAtRoot(root,fs::path(L"選択.KUBO"),output,error) && output.GetProjectFile()==selected,"Relative explicit selection used cwd");
        const auto retained=output.GetProjectFile();
        for(const auto& bad:{root/"Missing.kubo",root/"Foreign/Game.kubo",root})
            Need(!OpenProjectManifestAtRoot(root,bad,output,error) && output.GetProjectFile()==retained,"Invalid explicit selection fell back/changed context");
        Need(!OpenProjectManifestAtRoot(root,{},output,error) && output.GetProjectFile()==retained,"Ambiguous legacy directory silently chose a manifest");
        const auto foreignLink=root/"ForeignLink.kubo";
        const auto foreignTarget=root/"Foreign/Game.kubo";
        if(CreateSymbolicLinkW(foreignLink.c_str(),foreignTarget.c_str(),SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
            Need(!OpenProjectManifestAtRoot(root,foreignLink,output,error) && output.GetProjectFile()==retained,"Foreign-target manifest link escaped the owning root");
            const auto ownedLink=root/"OwnedLink.kubo";
            Need(CreateSymbolicLinkW(ownedLink.c_str(),selected.c_str(),SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)!=FALSE,"Owned-target symbolic link fixture failed");
            Need(OpenProjectManifestAtRoot(root,ownedLink,output,error) && output.GetSettings().name=="Selected","Same-root manifest link was rejected");
            Need(OpenProjectManifestAtRoot(root,selected,output,error),"Explicit selection recovery after link failed");
            std::cout<<"MANIFEST_LINK_CASES=2 FOREIGN_REFUSED=1 OWNED_ADMITTED=1\n";
        } else {
            const auto code=GetLastError();
            Need(code==ERROR_PRIVILEGE_NOT_HELD || code==ERROR_NOT_SUPPORTED,"Unexpected symbolic link fixture failure");
            std::cout<<"SKIP: manifest symbolic links unavailable, win32="<<code<<" (not link-policy coverage)\n";
        }
        Write(selected,"{}");Need(!OpenProjectManifestAtRoot(root,selected,output,error) && output.GetProjectFile()==retained,"Malformed selected manifest changed context");
        fs::create_directories(root/"Unique");Write(root/"Unique/Game.kubo",R"({"name":"Unique","startupScene":"entry.scene"})");
        Need(OpenProjectManifestAtRoot(root/"Unique",{},output,error) && output.GetSettings().name=="Unique","Unique legacy discovery changed");
        fs::current_path(original);
        std::cout<<"PASS: exact project manifest/root selection with multiple manifests/Unicode+case/cwd isolation/no directory or missing-file fallback/foreign-root rejection/unchanged output/unique legacy discovery\n";return 0;
    }catch(const std::exception& e){fs::current_path(original);std::cerr<<e.what()<<'\n';return 1;}
}
