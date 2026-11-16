#include "engine/scene/entity/EntityWorld.h"
#include <Windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
namespace {
void Require(bool value,const char* reason) {if(!value) throw std::runtime_error(reason);}
void Write(const std::filesystem::path& path,const std::string& bytes) {std::ofstream output(path,std::ios::binary);output.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));output.close();Require(!output.fail(),"Fixture write failed");}
std::string Nested(size_t arrays) {return "{\"extension\":"+std::string(arrays,'[')+"0"+std::string(arrays,']')+"}";}
}
int main() {
    namespace fs=std::filesystem;using namespace Engine::Scene;
    const auto fixture=fs::temp_directory_path()/(L"kubo-scene-read-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        fs::create_directories(fixture);const auto path=fixture/L"開始.scene";
        EntityWorld authored;authored.CreateEntity("日本語");const auto valid=authored.Capture();
        const auto validBytes=valid.dump();std::string error;SceneValue parsed=crude_json::object{{"retained",true}};
        Write(path,"\xef\xbb\xbf"+validBytes);
        Require(SceneDocument::Read(path,parsed,error) && parsed==valid,"BOM/Unicode native Scene read failed");
        EntityWorld loaded;loaded.CreateEntity("Previous");
        Require(loaded.Load(path,error) && loaded.Capture()==valid,"EntityWorld did not use BOM-aware bounded reader");
        SceneValue legacy=crude_json::object{{"version",1.0},{"scene","GAMEPLAY"}};
        Write(path,"\xef\xbb\xbf"+legacy.dump());Require(SceneDocument::Load(path,legacy,parsed,error) && parsed==legacy,"Legacy schema load diverged from reader");
        Write(path,Nested(127));Require(SceneDocument::Read(path,parsed,error),"128-level Scene document rejected");
        SceneValue quoted=crude_json::object{{"text",std::string(300,'[')+"\"\\escaped\""+std::string(300,']')}};
        Write(path,quoted.dump());Require(SceneDocument::Read(path,parsed,error) && parsed==quoted,"Quoted brackets/escaped quotes counted as nesting");
        std::string exact=validBytes;exact.resize(SceneDocument::MaxBytes,' ');Write(path,exact);
        Require(SceneDocument::Read(path,parsed,error) && parsed==valid,"Exactly 4 MiB Scene rejected");
        Require(loaded.Load(path,error),"EntityWorld exact-boundary load failed");
        const auto baseline=loaded.Capture();parsed=valid;
        const std::vector<std::string> invalid{"",std::string("\xef\xbb\xbf"),"[]","null","{ broken",validBytes+"{}",std::string("{\"text\":\"\xff\"}"),std::string("{\"text\":\"x\0y\"}",14),Nested(128),std::string(SceneDocument::MaxBytes+1,' ')};
        for(const auto& bytes:invalid) {
            Write(path,bytes);
            Require(!SceneDocument::Read(path,parsed,error) && parsed==valid,"Invalid/overlarge Scene changed read output");
            Require(!loaded.Load(path,error) && loaded.Capture()==baseline,"Rejected EntityWorld read mutated live world");
        }
        Write(path,"{\"version\":1,\"entities\":[{\"id\":1}]}");
        Require(!loaded.Load(path,error) && loaded.Capture()==baseline,"Schema-invalid Scene mutated live world");
        Write(path,validBytes);
        const HANDLE writer=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        Require(writer!=INVALID_HANDLE_VALUE,"Cannot open writer fixture");
        const bool busyRejected=!SceneDocument::Read(path,parsed,error);CloseHandle(writer);
        Require(busyRejected && parsed==valid,"Reader accepted incompatible writer or changed result");
        Require(SceneDocument::Read(path,parsed,error),"Reader did not recover after writer closed");
        for(const auto& invalidPath : {fs::path{},fixture,fixture/"missing.scene",fs::path(std::wstring(L"x\0y",3))}) {
            Require(!SceneDocument::Read(invalidPath,parsed,error) && parsed==valid,"Invalid path changed output");
        }
        // Read must release its handle before returning to save/reload workflows.
        Require(authored.Save(path,error) && loaded.Load(path,error),"Scene read leaked an incompatible handle");
        fs::remove_all(fixture);
        std::cout<<"PASS: shared native 4 MiB/BOM/UTF-8 Scene read, nesting/quoted escape guards, actual writer exclusion, exact boundary, candidate failure isolation and handle release\n";return 0;
    } catch(const std::exception& exception) {std::error_code cleanup;fs::remove_all(fixture,cleanup);std::cerr<<exception.what()<<'\n';return 1;}
}
