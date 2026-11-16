#include "engine/editor/ProjectPrefabWriter.h"
#include <fstream>
#include <iostream>
namespace {
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream out(path,std::ios::binary);out<<bytes;out.close();Require(!out.fail(),"Fixture write failed");}
void Probe(const std::filesystem::path& lock,bool canOpen) {
    wchar_t executable[32768]{};Require(GetModuleFileNameW(nullptr,executable,32768)!=0,"Missing executable path");
    auto command=L"\""+std::wstring(executable)+L"\" "+(canOpen?L"--can-lock":L"--busy-lock")+L" \""+lock.native()+L"\"";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION process{};
    Require(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE,"Child probe launch failed");
    CloseHandle(process.hThread);const auto wait=WaitForSingleObject(process.hProcess,5000);DWORD code=99;
    if(wait!=WAIT_OBJECT_0){TerminateProcess(process.hProcess,99);WaitForSingleObject(process.hProcess,5000);}
    GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hProcess);Require(wait==WAIT_OBJECT_0 && code==0,"Child writer-lock probe failed");
}
}
int wmain(int argc,wchar_t** argv) {
    if(argc==3) {
        const auto file=CreateFileW(argv[2],GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        const auto error=GetLastError();const bool opened=file!=INVALID_HANDLE_VALUE;if(opened)CloseHandle(file);
        if(std::wstring(argv[1])==L"--can-lock")return opened?0:1;
        return !opened && error==ERROR_SHARING_VIOLATION?0:1;
    }
    namespace fs=std::filesystem;using namespace Engine::Editor;using namespace Engine::Assets;using namespace Engine::Scene;
    const auto fixture=fs::temp_directory_path()/(L"kubo-prefab-writer-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        fs::create_directories(fixture/"Assets");Write(fixture/"Game.kubo",R"({"name":"Writer","startupScene":"Assets/Main.scene"})");
        Engine::Base::ProjectContext project;std::string error;Require(project.Open(fixture/"Game.kubo",error),error.c_str());
        EntityWorld world;world.CreateEntity("Root");PrefabValue definition;Require(CapturePrefabSubtree(world,1,definition,error),error.c_str());
        const auto file=fixture/L"Assets/ソース.prefab";Require(SavePrefabDocument(file,definition,error),error.c_str());AssetRegistry registry;Require(registry.Scan(fixture/"Assets",error),error.c_str());
        ProjectPrefabRevision expected;Require(ReadProjectPrefabRevision(project,registry,registry.FindByPath(file)->guid,expected,error),error.c_str());
        HeldProjectPrefabWriter writer;Require(writer.Open(project,registry,expected,error) && writer.Revalidate(error) && writer.Revalidate(error),error.c_str());
        const auto lock=writer.LockPath();Require(fs::file_size(lock)==0,"Lock is not persistent empty file");Probe(lock,false);
        const auto pending=fixture/"Assets/.kubo/asset-move.pending.json";Write(pending,"{}");
        Require(!writer.Revalidate(error),"Late pending asset move bypassed owned revision guard");fs::remove(pending);Require(writer.Revalidate(error),error.c_str());
        HeldProjectPrefabWriter competing;Require(!competing.Open(project,registry,expected,error),"Second same-process writer acquired lock");
        Require(!Engine::Base::AtomicFile::Write(file,expected.bytes,SceneDocument::MaxBytes,error),"Acquired source allowed pathname replacement");
        Require(!Engine::Base::AtomicFile::Write(expected.metadata,expected.metadataBytes,16384,error),"Acquired metadata allowed replacement");
        HeldProjectPrefabRevision reader;Require(!reader.Open(project,registry,expected.guid,error),"Incompatible new read lease bypassed DELETE ownership");
        Require(writer.Snapshot().fileIdentity==expected.fileIdentity && writer.Revalidate(error),"Failed competitors changed source or ownership");
        writer.Release();Probe(lock,true);Require(fs::exists(lock) && ValidateProjectPrefabRevision(project,registry,expected,error),"Release deleted persistent lock or changed source");
        auto stale=expected;Write(file,expected.bytes+"\n");Require(!competing.Open(project,registry,stale,error),"Stale source acquired writer ownership");Write(file,expected.bytes);
        Write(lock,"occupied");Require(!competing.Open(project,registry,expected,error),"Nonempty writer lock accepted");Write(lock,"");
        HeldProjectPrefabRevision held;Require(held.Open(project,registry,expected.guid,error),error.c_str());
        Require(!competing.Open(project,registry,expected,error),"Existing source read lease permitted writer DELETE upgrade");held.Release();
        Require(competing.Open(project,registry,expected,error),error.c_str());
        auto wrong=expected;wrong.guid="ffffffffffffffffffffffffffffffff";
        Require(!competing.Open(project,registry,wrong,error) && competing.Revalidate(error),"Failed reopen lost prior writer ownership");competing.Release();
        Require(Engine::Base::AtomicFile::Write(file,expected.bytes,SceneDocument::MaxBytes,error),"Released writer left native handles locked");
        fs::remove_all(fixture);std::cout<<"PASS: persistent project Prefab writer exclusion, child-process lock contention/release, held DELETE/read source+metadata ownership, exact revision upgrade/revalidation, stale/invalid lock/read-lease refusal and failure ownership isolation (no source publication)\n";return 0;
    }catch(const std::exception& exception){std::cerr<<"FAIL: "<<exception.what()<<" (fixture retained)\n";return 1;}
}
