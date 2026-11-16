#include "engine/editor/ProjectPrefabRevision.h"
#include "engine/base/AtomicFile.h"
#include <fstream>
#include <iostream>
namespace {
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream out(path,std::ios::binary);out<<bytes;out.close();Require(!out.fail(),"Fixture write failed");}
}
int main(){namespace fs=std::filesystem;using namespace Engine::Editor;using namespace Engine::Assets;using namespace Engine::Scene;
    const auto fixture=fs::temp_directory_path()/(L"kubo-prefab-revision-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        fs::create_directories(fixture/L"Assets/プレハブ");Write(fixture/"Game.kubo",R"({"name":"Revision","startupScene":"Assets/Main.scene"})");
        Engine::Base::ProjectContext project;std::string error;Require(project.Open(fixture/"Game.kubo",error),error.c_str());
        EntityWorld world;world.CreateEntity("Alpha");PrefabValue definition;Require(CapturePrefabSubtree(world,1,definition,error),error.c_str());
        const auto source=fixture/L"Assets/プレハブ/ソース.prefab";
        Require(SavePrefabDocument(source,definition,error),error.c_str());Write(source,"\xef\xbb\xbf"+definition.dump(2));AssetRegistry registry;Require(registry.Scan(fixture/"Assets",error),error.c_str());
        const auto guid=registry.FindByPath(source)->guid;const auto metadata=AssetRegistry::MetadataPath(source);
        ProjectPrefabRevision first;Require(ReadProjectPrefabRevision(project,registry,guid,first,error),error.c_str());
        Require(first.file==source && first.guid==guid && first.definition.dump()==definition.dump() && ValidateProjectPrefabRevision(project,registry,first,error),error.c_str());
        HeldProjectPrefabRevision held;Require(held.Open(project,registry,guid,error,&first),error.c_str());
        for(const auto& file:{source,metadata}) {
            const auto writer=CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(writer!=INVALID_HANDLE_VALUE){CloseHandle(writer);throw std::runtime_error("Held revision allowed writer");}
            const auto deleter=CreateFileW(file.c_str(),DELETE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(deleter!=INVALID_HANDLE_VALUE){CloseHandle(deleter);throw std::runtime_error("Held revision allowed replacement/delete");}
        }
        Require(!Engine::Base::AtomicFile::Write(source,first.bytes,SceneDocument::MaxBytes,error),"Held revision allowed atomic source replacement");
        held.Release();Require(ValidateProjectPrefabRevision(project,registry,first,error),error.c_str());
        const auto timestamp=fs::last_write_time(source);auto changed=first.bytes;const auto name=changed.find("Alpha");Require(name!=std::string::npos,"Name fixture missing");changed.replace(name,5,"Bravo");
        Write(source,changed);fs::last_write_time(source,timestamp);
        Require(!ValidateProjectPrefabRevision(project,registry,first,error),"Same-size/time source edit accepted");
        Write(source,first.bytes);fs::last_write_time(source,timestamp);Require(ValidateProjectPrefabRevision(project,registry,first,error),"Exact bytes restored in same file rejected");
        const auto newFile=fixture/"Replacement.prefab";Write(newFile,first.bytes);
        Require(MoveFileExW(newFile.c_str(),source.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE,"Identical-byte replacement fixture failed");
        Require(!ValidateProjectPrefabRevision(project,registry,first,error),"Identical-byte new file identity accepted");
        ProjectPrefabRevision second;Require(ReadProjectPrefabRevision(project,registry,guid,second,error),error.c_str());
        Write(metadata,first.metadataBytes+"\n");Require(!ValidateProjectPrefabRevision(project,registry,second,error),"Unknown metadata byte change accepted");Write(metadata,first.metadataBytes);
        Require(ValidateProjectPrefabRevision(project,registry,second,error),error.c_str());
        Write(source,std::string("{\"bad\":\"")+char(0xff)+"\"}");
        ProjectPrefabRevision invalidOutput=second;
        Require(!ReadProjectPrefabRevision(project,registry,guid,invalidOutput,error) && invalidOutput.bytes==second.bytes,"Invalid UTF-8 changed revision output");
        Write(source,std::string(129,'[')+std::string(129,']'));
        Require(!ReadProjectPrefabRevision(project,registry,guid,invalidOutput,error) && invalidOutput.fileIdentity==second.fileIdentity,"Deep source JSON accepted/changed output");
        Write(source,std::string(SceneDocument::MaxBytes+1,' '));
        Require(!ReadProjectPrefabRevision(project,registry,guid,invalidOutput,error),"Oversized source accepted");Write(source,second.bytes);
        Write(metadata,std::string(16385,' '));Require(!ReadProjectPrefabRevision(project,registry,guid,invalidOutput,error),"Oversized metadata accepted");Write(metadata,second.metadataBytes);
        ProjectPrefabRevision preserved=second;fs::rename(metadata,metadata.wstring()+L".held");
        Require(!ReadProjectPrefabRevision(project,registry,guid,preserved,error) && preserved.bytes==second.bytes && preserved.fileIdentity==second.fileIdentity,"Missing identity changed snapshot output");fs::rename(metadata.wstring()+L".held",metadata);
        const auto alias=fixture/"Hardlink.prefab";Require(CreateHardLinkW(alias.c_str(),source.c_str(),nullptr)!=FALSE,"Hardlink fixture failed");
        Require(!ReadProjectPrefabRevision(project,registry,guid,preserved,error),"Multiple native hardlinks accepted for writable revision");fs::remove(alias);
        HeldProjectPrefabRevision prior;Require(prior.Open(project,registry,guid,error,&second),error.c_str());
        Require(!prior.Open(project,registry,"ffffffffffffffffffffffffffffffff",error) && prior.Snapshot().fileIdentity==second.fileIdentity,"Failed lease Open lost previous read ownership");prior.Release();
        Require(Engine::Base::AtomicFile::Write(source,second.bytes,SceneDocument::MaxBytes,error),"Lease release retained source lock");
        fs::remove_all(fixture);std::cout<<"PASS: project-owned held Prefab source+metadata revision, native identity/bytes, writer/replacement exclusion, same-size/time edit and identical-byte replacement detection, metadata/output/lease isolation and hardlink refusal\n";return 0;
    }catch(const std::exception& exception){std::cerr<<"FAIL: "<<exception.what()<<" (fixture retained)\n";return 1;}
}
