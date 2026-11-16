#include "engine/editor/AnimatorControllerAcknowledgement.h"
#include "engine/editor/AnimatorControllerEdits.h"
#include <fstream>
#include <iostream>
static void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream stream(path,std::ios::binary);stream<<bytes;stream.close();Need(!stream.fail(),"Ack fixture write failed");}
static std::string Read(const std::filesystem::path& path){std::ifstream stream(path,std::ios::binary);Need(stream.good(),"Ack fixture read failed");return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};}
int main(){namespace fs=std::filesystem;using namespace Engine::Editor;using namespace Engine::Assets;
    const auto fixture=fs::temp_directory_path()/(L"kubo-controller-ack-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try{
        for(const char* scenario:{"prepared","published","restored","parked","altered","stale-source","stale-meta","foreign-archive","throw","pending-move","extra-child","active"}){
            const std::string mode=scenario;const auto root=fixture/scenario;fs::create_directories(root/"Assets");Write(root/"entry.kubo",R"({"name":"Controller Ack","assetDirectory":"Assets","startupScene":"entry.scene"})");
            Engine::Base::ProjectContext project;std::string error;Need(project.Open(root/"entry.kubo",error),error.c_str());
            AnimatorControllerDocument document;Need(AnimatorControllerEdits(document).New(error),error.c_str());const auto file=root/L"Assets/確定.animfsm";Write(file,document.Text());
            AssetRegistry registry;Need(registry.Scan(root/"Assets",error),error.c_str());const auto guid=registry.FindByPath(file)->guid;AnimatorControllerRevision expected;
            {HeldAnimatorControllerAsset read;Need(read.Open(project,registry,guid,error),error.c_str());expected=*read.Snapshot();}
            auto layer=document.Definition().layers[0];layer.weight=0.5f;Need(AnimatorControllerEdits(document).ReplaceLayer(0,layer,error),error.c_str());
            fs::path directory;{
                AnimatorControllerPublication publication;Need(publication.Prepare(project,registry,expected,document.Text(),error),error.c_str());directory=publication.Directory();
                if(mode=="active"){ControllerRecoveryReport report;report.directory=directory;fs::path archive="unchanged";Need(!AnimatorControllerAcknowledgement::ArchiveResolved(project,guid,report,archive,error) && archive=="unchanged","Ack bypassed active publication");continue;}
                if(mode=="parked")Need(!publication.Publish(error,[&]{Write(file,"foreign");}),"Park fixture failed");
                else if(mode!="prepared")Need(publication.Publish(error),error.c_str());
            }
            if(mode=="parked")fs::rename(file,root/"foreign-retained");
            ControllerRecoveryReport report;Need(AnimatorControllerRecovery::Inspect(project,guid,report,error),error.c_str());
            if(mode=="restored"){Need(AnimatorControllerRecovery::RestoreOriginal(project,guid,report,error),error.c_str());Need(AnimatorControllerRecovery::Inspect(project,guid,report,error),error.c_str());}
            const auto intent=Read(directory/"pending.json");fs::path archive="unchanged";
            if(mode=="altered")report.source=root/"foreign.animfsm";
            if(mode=="stale-source")Write(file,"foreign source");
            if(mode=="stale-meta")Write(expected.metadata,"{}");
            if(mode=="pending-move")Write(root/"Assets/.kubo/asset-move.pending.json","{}");
            if(mode=="extra-child")Write(directory/"foreign-child","keep this file");
            fs::path occupied;
            const auto hook=[&](const fs::path& destination){
                if(mode=="foreign-archive"){occupied=destination;fs::create_directory(destination);Write(destination/"foreign","archive owner");}
                if(mode=="throw")throw std::runtime_error("archive injection");
            };
            const bool success=AnimatorControllerAcknowledgement::ArchiveResolved(project,guid,report,archive,error,hook);
            if(mode=="prepared" || mode=="published" || mode=="restored" || mode=="extra-child"){
                Need(success && archive!="unchanged" && !fs::exists(directory) && Read(archive/"pending.json")==intent && Read(expected.metadata)==expected.metadataBytes,"Resolved archive incorrect");
                Need(Read(file)==((mode=="prepared" || mode=="restored")?expected.bytes:document.Text()),"Ack changed source");
                Need(fs::exists(archive/((mode=="prepared" || mode=="restored")?"next.source":"original.source")),"Ack removed retained revision");
                if(mode=="extra-child")Need(Read(archive/"foreign-child")=="keep this file","Ack dropped unknown child");
                AnimatorControllerRevision current;{HeldAnimatorControllerAsset read;Need(read.Open(project,registry,guid,error),error.c_str());current=*read.Snapshot();}
                HeldAnimatorControllerWriter writer;Need(writer.Open(project,registry,current,error),"Acknowledged source could not acquire next writer");writer.Release();
                fs::path repeat="unchanged";Need(!AnimatorControllerAcknowledgement::ArchiveResolved(project,guid,report,repeat,error) && repeat=="unchanged","Repeated acknowledgement accepted");
            }else{
                Need(!success && archive=="unchanged" && Read(directory/"pending.json")==intent,"Rejected Ack modified intent/output");
                if(mode=="foreign-archive")Need(Read(occupied/"foreign")=="archive owner","Foreign archive overwritten");
                if(mode=="stale-source")Need(Read(file)=="foreign source","Stale source overwritten");
                if(mode=="stale-meta")Need(Read(expected.metadata)=="{}","Stale metadata overwritten");
            }
        }
        std::cout<<"ACK_FIXTURE="<<Engine::Base::PathToUtf8(fixture)<<'\n';
        std::cout<<"PASS: Controller explicit Prepared/Published/restored archive; Parked/stale/altered/active/move/foreign destination/injected failure denial; retained unknown children+revisions; next writer admission\n";return 0;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<"\nRetained Ack fixture: "<<fixture<<'\n';return 1;}
}
