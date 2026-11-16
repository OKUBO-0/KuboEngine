#include "engine/editor/AnimatorControllerWriter.h"
#include "engine/editor/AnimatorControllerEdits.h"
#include <fstream>
#include <iostream>
static void Need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static void Write(const std::filesystem::path& file,const std::string& bytes){std::ofstream stream(file,std::ios::binary);stream<<bytes;stream.close();Need(!stream.fail(),"Writer fixture write failed");}
int main(){
    namespace fs=std::filesystem;using namespace Engine::Assets;using namespace Engine::Editor;
    const auto root=fs::temp_directory_path()/(L"kubo-controller-writer-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try {
        fs::create_directories(root/"Assets");Write(root/"entry.kubo",R"({"name":"Controller writer","assetDirectory":"Assets","startupScene":"entry.scene"})");
        Engine::Base::ProjectContext project;std::string error;Need(project.Open(root/"entry.kubo",error),error.c_str());
        AnimatorControllerDocument doc;Need(AnimatorControllerEdits(doc).New(error),"Fixture graph failed");
        const auto file=root/L"Assets/移動.animfsm";Write(file,doc.Text());AssetRegistry registry;Need(registry.Scan(root/"Assets",error),error.c_str());
        const auto guid=registry.FindByPath(file)->guid;HeldAnimatorControllerAsset read;Need(read.Open(project,registry,guid,error),error.c_str());const auto expected=*read.Snapshot();
        HeldAnimatorControllerWriter writer,other;
        Need(!writer.Open(project,registry,expected,error) && !writer.Snapshot(),"Active read lease permitted DELETE writer upgrade");read.Release();
        Need(writer.Open(project,registry,expected,error) && writer.Revalidate(error) && writer.Snapshot()->bytes==expected.bytes,"Writer acquisition failed");
        const auto first=writer.Snapshot();auto wrong=expected;wrong.guid="missing";
        Need(!writer.Open(project,registry,wrong,error) && writer.Snapshot()==first && writer.Revalidate(error),"Failed acquisition replaced existing writer ownership");
        Need(!other.Open(project,registry,expected,error),"Competing writer admitted");
        for(const auto& path:{file,expected.metadata,writer.LockPath()})for(const DWORD access:{GENERIC_WRITE,DELETE}) {
            const auto foreign=CreateFileW(path.c_str(),access,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(foreign!=INVALID_HANDLE_VALUE){CloseHandle(foreign);throw std::runtime_error("Writer permitted foreign write/delete");}
        }
        const auto lock=writer.LockPath();writer.Release();writer.Release();
        Need(!writer.Snapshot() && other.Open(project,registry,expected,error),"Release did not permit next writer");other.Release();
        auto changed=expected.bytes;const auto offset=changed.find("\"weight\":1");Need(offset!=std::string::npos,"Fixture weight absent");changed.replace(offset,10,"\"weight\":0");
        const auto time=fs::last_write_time(file);Write(file,changed);fs::last_write_time(file,time);
        Need(!writer.Open(project,registry,expected,error) && !writer.Snapshot(),"Same-size/time source changed baseline accepted");Write(file,expected.bytes);
        Write(expected.metadata,"{\"version\":1,\"guid\":\""+guid+"\",\"external\":true}");
        Need(!writer.Open(project,registry,expected,error),"Changed metadata accepted");Write(expected.metadata,expected.metadataBytes);
        fs::create_directories(root/"Assets/.kubo/controller-transactions"/guid);
        Need(!writer.Open(project,registry,expected,error),"Pending transaction admitted");
        fs::rename(root/"Assets/.kubo/controller-transactions"/guid,root/"Assets/.kubo/controller-transactions"/(guid+".retained"));
        Need(writer.Open(project,registry,expected,error),error.c_str());Write(root/"Assets/.kubo/asset-move.pending.json","{}");
        Need(!writer.Revalidate(error),"Move pending missed during ownership");writer.Release();
        std::ifstream source(file,std::ios::binary),metadata(expected.metadata,std::ios::binary);
        Need(std::string(std::istreambuf_iterator<char>(source),std::istreambuf_iterator<char>())==expected.bytes && std::string(std::istreambuf_iterator<char>(metadata),std::istreambuf_iterator<char>())==expected.metadataBytes && fs::file_size(lock)==0,"Writer acquisition modified source/meta/lock payload");
        std::cout<<"WRITER_FIXTURE="<<Engine::Base::PathToUtf8(root)<<'\n';
        std::cout<<"PASS: Controller DELETE/read writer ownership/cooperating exclusion/read-upgrade refusal/ordinary write+delete denial/source+metadata baseline conflicts/pending guards/failed-open retention/idempotent release (no source publication)\n";return 0;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<"\nRetained writer fixture: "<<root<<'\n';return 1;}
}
