#include "engine/base/AtomicFile.h"
#include "application_check_mode.h"
#include <fstream>
#include <iostream>
#include <vector>

namespace {
void Require(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
std::string Read(const std::filesystem::path& path) {std::ifstream file(path,std::ios::binary);Require(bool(file),"Cannot read owned fixture");return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
std::string Payload(int worker) {return "writer="+std::to_string(worker)+"\n"+std::string(32768,static_cast<char>('A'+worker));}
struct Children {
    std::vector<HANDLE> handles;
    ~Children(){for(const auto handle:handles){if(WaitForSingleObject(handle,0)==WAIT_TIMEOUT){TerminateProcess(handle,1);WaitForSingleObject(handle,5000);}CloseHandle(handle);}}
};
}
int wmain(int argc,wchar_t** argv) {
    using Engine::Base::AtomicFile;namespace fs=std::filesystem;
    if(argc==4 && std::wstring(argv[1])==L"--worker") {
        const auto bytes=Payload(static_cast<int>(wcstol(argv[3],nullptr,10)));std::string error;
        for(int iteration=0;iteration<20;++iteration) {
            bool saved=false;for(int retry=0;retry<10 && !saved;++retry) {saved=AtomicFile::Write(argv[2],bytes,65536,error);if(!saved) Sleep(10);}
            if(!saved) {std::cerr<<error<<'\n';return 1;}
        }return 0;
    }
    const auto fixture=fs::temp_directory_path()/(L"kubo-atomic-file-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        Require(ApplicationCheckMode(" --lua-input \t")=="--lua-input" && ApplicationCheckMode("\"--generic\" ")=="--generic" && ApplicationCheckMode(" \t").empty(),"Application mode whitespace/quoting normalization failed");
        Require(!fs::exists(fixture),"Owned fixture collision");fs::create_directories(fixture);
        struct Cleanup {fs::path path;~Cleanup(){std::error_code ignored;fs::remove_all(path,ignored);}} cleanup{fixture};
        const auto path=fixture/L"保存.data";auto legacyTemporary=path;legacyTemporary+=L".tmp";
        std::string error;
        Require(AtomicFile::Write(legacyTemporary,"unrelated temporary",65536,error),error.c_str());
        Require(AtomicFile::Write(path,Payload(0),65536,error),error.c_str());
        Require(Read(legacyTemporary)=="unrelated temporary","Save overwrote or removed another operation's legacy temporary file");
        Require(!AtomicFile::Write(path,std::string(65537,'x'),65536,error) && Read(path)==Payload(0),"Rejected size changed destination");
        const auto missing=fixture/"NotCreated/entry.data";
        Require(!AtomicFile::Write(missing,"large",1,error) && !fs::exists(missing.parent_path()),"Oversized preflight created directories");
        Require(!AtomicFile::Write(fixture/fs::path(std::wstring(L"unintended\0suffix",17)),"data",65536,error) && !fs::exists(fixture/"unintended"),"NUL filename reached a different Win32 destination");
        const auto locked=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        Require(locked!=INVALID_HANDLE_VALUE,"Cannot lock fixture");const bool saved=AtomicFile::Write(path,Payload(1),65536,error);CloseHandle(locked);
        Require(!saved && Read(path)==Payload(0),"Failed replacement changed locked destination");
        Require(Read(legacyTemporary)=="unrelated temporary","Failed replacement removed unrelated temporary file");
        const auto blocked=fixture/"Blocked";fs::create_directory(blocked);
        Require(!AtomicFile::Write(blocked,"data",65536,error) && fs::is_directory(blocked),"Directory destination was replaced");
        wchar_t executable[32768]{};const auto length=GetModuleFileNameW(nullptr,executable,32768);
        Require(length>0 && length<32768,"Cannot resolve own executable");Children children;
        for(int worker=0;worker<4;++worker) {
            auto command=L"\""+std::wstring(executable)+L"\" --worker \""+path.native()+L"\" "+std::to_wstring(worker);
            STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;PROCESS_INFORMATION process{};
            Require(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE,"Cannot launch owned writer");
            CloseHandle(process.hThread);children.handles.push_back(process.hProcess);
        }
        for(const auto handle:children.handles) {
            Require(WaitForSingleObject(handle,30000)==WAIT_OBJECT_0,"Owned concurrent writer timeout");DWORD code=1;Require(GetExitCodeProcess(handle,&code)!=FALSE && code==0,"Owned concurrent writer failed");
        }
        const auto final=Read(path);bool whole=false;for(int worker=0;worker<4;++worker) whole=whole || final==Payload(worker);
        Require(whole,"Concurrent writers produced a partial/mixed destination");
        for(const auto& entry:fs::directory_iterator(fixture)) Require(entry.path().filename().native().find(L".kubo-save-")==std::wstring::npos,"Owned temporary leaked");
        Require(Read(legacyTemporary)=="unrelated temporary","Concurrent writes touched unrelated temporary file");
        std::cout<<"PASS: Unicode native atomic save, bounded preflight, legacy temporary preservation, locked/directory failures and four actual concurrent process writers with complete-file output\n";return 0;
    } catch(const std::exception& exception) {std::cerr<<exception.what()<<'\n';return 1;}
}
