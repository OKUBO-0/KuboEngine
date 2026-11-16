#include "engine/assets/AnimatorControllerAsset.h"
#include <fstream>
#include <iostream>
namespace {
void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void Write(const std::filesystem::path& path,const std::string& bytes) {
    std::ofstream out(path,std::ios::binary);out<<bytes;out.close();Need(!out.fail(),"Fixture write failed");
}
}
int main() {
    namespace fs=std::filesystem;using namespace Engine::Assets;
    const auto fixture=fs::temp_directory_path()/(L"kubo-controller-revision-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        fs::create_directories(fixture/L"Assets/アニメーション");
        Write(fixture/"Game.kubo",R"({"name":"Controller revision","startupScene":"Assets/Main.scene"})");
        Engine::Base::ProjectContext project;std::string error;Need(project.Open(fixture/"Game.kubo",error),error.c_str());
        const auto source=fixture/L"Assets/アニメーション/移動.animfsm";
        const std::string good=R"({"format":"kubo-animator-controller-1","parameters":[],"layers":[{"name":"Base","mode":"override","weight":1,"mask":[],"entry":"Idle","states":[{"name":"Idle","clip":"idle","speed":1,"loop":true}],"transitions":[]}]})";
        Write(source,good);Write(fixture/"Assets/Wrong.lua","return {}");
        AssetRegistry registry;Need(registry.Scan(fixture/"Assets",error),error.c_str());
        const auto guid=registry.FindByPath(source)->guid;const auto metadata=AssetRegistry::MetadataPath(source);
        HeldAnimatorControllerAsset held;Need(held.Open(project,registry,guid,error),error.c_str());
        const auto first=*held.Snapshot();
        Need(first.bytes==good && first.contentHash.size()==64 && first.graph.layers[0].name=="Base","Revision contents missing");
        for(const auto& path:{source,metadata})for(const DWORD access:{GENERIC_WRITE,DELETE}) {
            const auto writer=CreateFileW(path.c_str(),access,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(writer!=INVALID_HANDLE_VALUE){CloseHandle(writer);throw std::runtime_error("Lease permitted writer/delete");}
        }
        Need(!held.Open(project,registry,"missing",error) && held.Snapshot()->contentHash==first.contentHash,"Failed open changed held revision");
        Need(!held.Open(project,registry,registry.FindByPath(fixture/"Assets/Wrong.lua")->guid,error),"Wrong asset type accepted");
        held.Release();Need(held.Open(project,registry,guid,error,&first),error.c_str());held.Release();
        const auto timestamp=fs::last_write_time(source);auto changed=good;changed.replace(changed.find("idle"),4,"walk");
        Write(source,changed);fs::last_write_time(source,timestamp);
        Need(!held.Open(project,registry,guid,error,&first),"Same size/time edit accepted");
        Need(held.Open(project,registry,guid,error) && held.Snapshot()->contentHash!=first.contentHash,"Content hash missed edit");held.Release();
        Write(source,good);fs::last_write_time(source,timestamp);
        Need(held.Open(project,registry,guid,error,&first),error.c_str());held.Release();
        Write(metadata,"{\"version\":1,\"guid\":\""+guid+"\",\"g\\u0075id\":\""+guid+"\"}");
        Need(!held.Open(project,registry,guid,error),"Decoded duplicate metadata key accepted");Write(metadata,first.metadataBytes);
        Write(source,"{}");Need(!held.Open(project,registry,guid,error),"Malformed graph accepted");
        Write(source,std::string(Engine::AnimationSystem::Controller::TextCodec::MaxBytes+1,' '));
        Need(!held.Open(project,registry,guid,error),"Oversized graph accepted");Write(source,good);
        fs::rename(metadata,metadata.wstring()+L".saved");
        Need(!held.Open(project,registry,guid,error) && !fs::exists(metadata),"Missing metadata manufactured identity");
        fs::rename(metadata.wstring()+L".saved",metadata);
        const auto replacement=fixture/"Replacement.animfsm";Write(replacement,good);
        Need(MoveFileExW(replacement.c_str(),source.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE,"Replacement fixture failed");
        Need(!held.Open(project,registry,guid,error,&first),"Same bytes new file identity accepted");
        Need(held.Open(project,registry,guid,error),error.c_str());held.Release();
        const auto alias=fixture/"Alias.animfsm";fs::create_hard_link(source,alias);
        Need(!held.Open(project,registry,guid,error),"Hard linked Controller accepted");fs::remove(alias);
        Write(fixture/"Assets/Duplicate.animfsm",good);Write(AssetRegistry::MetadataPath(fixture/"Assets/Duplicate.animfsm"),first.metadataBytes);
        Need(!held.Open(project,registry,guid,error),"Duplicate GUID accepted");
        // Retain this uniquely owned fixture for inspection, including negative files.
        std::cout<<"PASS: Controller GUID/revision/strict metadata/lease/content hash/stale/type/size/link/duplicate checks\n";
        return 0;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<'\n';return 1;}
}
