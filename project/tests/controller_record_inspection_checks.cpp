#include "engine/editor/ControllerRecordInspection.h"
#include "engine/editor/AnimatorControllerEdits.h"
#include <fstream>
#include <iostream>
static void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void Write(const std::filesystem::path& path,const std::string& bytes){std::ofstream file(path,std::ios::binary);file<<bytes;file.close();Need(!file.fail(),"Fixture write failed");}
static std::string Read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);Need(file.good(),"Fixture read failed");return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
int main(){namespace fs=std::filesystem;using namespace Engine::Editor;
    const auto fixture=fs::temp_directory_path()/(L"kubo-controller-record-日本語-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    try{
        fs::create_directories(fixture/"Assets/.kubo/controller-writers");
        Write(fixture/"entry.kubo",R"({"name":"Record inspection","assetDirectory":"Assets","startupScene":"entry.scene"})");
        Engine::Base::ProjectContext project;std::string error;Need(project.Open(fixture/"entry.kubo",error),error.c_str());
        AnimatorControllerDocument document;Need(AnimatorControllerEdits(document).New(error),error.c_str());
        const auto original=fixture/"Assets/original.animfsm";Write(original,document.Text());
        const auto storage=fixture/"Assets/.kubo";const std::string guid(32,'a');
        for(const auto family:{ControllerPendingFamily::Publication,ControllerPendingFamily::Creation}){
            const auto parent=storage/(family==ControllerPendingFamily::Publication?"controller-transactions":"controller-creations");
            const auto directory=parent/guid;fs::create_directories(directory);
            const auto lockPath=family==ControllerPendingFamily::Publication?storage/"controller-writers"/(guid+".lock"):storage/"controller-create.lock";
            ControllerRecordReport report;
            Need(!ControllerRecordInspection::Inspect(project,family,guid,report,error),"Missing lock admitted");
            Write(lockPath,"");
            Need(ControllerRecordInspection::Inspect(project,family,guid,report,error) && report.files.empty() && !report.hasStage && !report.hasIntent,"Empty incomplete diagnosis failed");
            const auto stage=directory/(family==ControllerPendingFamily::Publication?"next.source":"source.stage");
            Write(stage,document.Text());Write(directory/"pending.json","{truncated");Write(directory/L"unknown 日本語.bin",std::string("a\0b",3));
            Need(ControllerRecordInspection::Inspect(project,family,guid,report,error) && report.files.size()==3 && report.hasStage && report.validStage && report.hasIntent,"Stage diagnosis or opaque invalid intent failed");
            Need(Read(stage)==document.Text() && Read(directory/"pending.json")=="{truncated" && Read(directory/L"unknown 日本語.bin")==std::string("a\0b",3),"Diagnosis wrote files");
            const auto retained=report;
            HANDLE active=CreateFileW(lockPath.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);Need(active!=INVALID_HANDLE_VALUE,"Cannot acquire active writer fixture");
            const bool refused=!ControllerRecordInspection::Inspect(project,family,guid,report,error);CloseHandle(active);
            Need(refused && report==retained,"Active writer admitted or output changed");
            Write(storage/"asset-move.pending.json","pending");Need(!ControllerRecordInspection::Inspect(project,family,guid,report,error) && report==retained,"Pending move admitted/output changed");
            fs::rename(storage/"asset-move.pending.json",fixture/(family==ControllerPendingFamily::Publication?"move-publication.retained":"move-creation.retained"));
            Write(stage,"invalid graph");Need(ControllerRecordInspection::Inspect(project,family,guid,report,error) && report.hasStage && !report.validStage && !report.stageError.empty(),"Invalid stage hidden or admitted as valid");
            const auto invalid=report;
            fs::create_directory(directory/"unknown-child");Need(!ControllerRecordInspection::Inspect(project,family,guid,report,error) && report==invalid,"Nested child admitted/output changed");
            fs::rename(directory/"unknown-child",fixture/(family==ControllerPendingFamily::Publication?"nested-publication.retained":"nested-creation.retained"));
            Write(directory/"oversized.bin",std::string(ControllerRecordInspection::MaxFileBytes+1,'x'));
            Need(!ControllerRecordInspection::Inspect(project,family,guid,report,error) && report==invalid,"Oversize record admitted/output changed");
            fs::rename(directory/"oversized.bin",fixture/(family==ControllerPendingFamily::Publication?"oversize-publication.retained":"oversize-creation.retained"));
            Need(!ControllerRecordInspection::Inspect(project,family,"../escape",report,error) && report==invalid,"Traversal admitted/output changed");
            const auto changed=fs::path(fixture/"Assets/original.animfsm");Need(Read(changed)==document.Text(),"Original asset changed");
            Write(directory/"duplicate-stage.bin","");
            Need(CreateHardLinkW((directory/"linked-stage.bin").c_str(),(directory/"duplicate-stage.bin").c_str(),nullptr),"Hardlink fixture failed");
            Need(!ControllerRecordInspection::Inspect(project,family,guid,report,error) && report==invalid,"Hardlinked file admitted/output changed");
            const std::string capGuid(32,'b');const auto capDirectory=parent/capGuid;fs::create_directory(capDirectory);
            if(family==ControllerPendingFamily::Publication)Write(storage/"controller-writers"/(capGuid+".lock"),"");
            for(size_t i=0;i<=ControllerRecordInspection::MaxFiles;++i)Write(capDirectory/("child-"+std::to_string(i)),"");
            Need(!ControllerRecordInspection::Inspect(project,family,capGuid,report,error) && report==invalid,"File count cap bypassed/output changed");
            const std::string bytesGuid(32,'c');const auto bytesDirectory=parent/bytesGuid;fs::create_directory(bytesDirectory);
            if(family==ControllerPendingFamily::Publication)Write(storage/"controller-writers"/(bytesGuid+".lock"),"");
            for(size_t i=0;i<4;++i)Write(bytesDirectory/("bytes-"+std::to_string(i)),std::string(ControllerRecordInspection::MaxFileBytes,'x'));
            Need(ControllerRecordInspection::Inspect(project,family,bytesGuid,report,error) && report.files.size()==4,"Exact aggregate byte limit denied");
            const auto bounded=report;Write(bytesDirectory/"one-more-byte","x");
            Need(!ControllerRecordInspection::Inspect(project,family,bytesGuid,report,error) && report==bounded,"Aggregate cap bypassed/output changed");
        }
        ControllerRecordReport output;output.guid="retained";const auto retained=output;
        Need(!ControllerRecordInspection::Inspect(project,static_cast<ControllerPendingFamily>(99),guid,output,error) && output==retained,"Unknown family admitted/output changed");
        Write(fixture/"entry.kubo",R"({"name":"Changed","assetDirectory":"Elsewhere","startupScene":"entry.scene"})");fs::create_directory(fixture/"Elsewhere");
        Need(!ControllerRecordInspection::Inspect(project,ControllerPendingFamily::Publication,guid,output,error) && output==retained,"Changed manifest root admitted/output changed");
        std::cout<<"RECORD_INSPECTION_FIXTURE="<<Engine::Base::PathToUtf8(fixture)<<'\n';
        std::cout<<"PASS: owned read-only record inspection/empty+missing intent/opaque truncated intent/valid+invalid stage/unknown binary retention/active writer+move+root+traversal+nested+oversize+hardlink refusal/output retention (no quarantine/salvage/GUI admission)\n";return 0;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<"\nRetained fixture: "<<fixture<<'\n';return 1;}
}
